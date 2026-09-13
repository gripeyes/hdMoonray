// Copyright 2023-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ColorManagement.h"

#include <OpenColorIO/OpenColorIO.h>
#include <scene_rdl2/render/logging/logging.h>

#include <array>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace OCIO = OCIO_NAMESPACE;

namespace {

constexpr const char* sTextureTargetEnv = "MOONRAY_OCIO_RENDERING_COLOR_SPACE";

std::string colorSpaceName(const OCIO::ConstColorSpaceRcPtr& colorSpace)
{
    if (!colorSpace || !colorSpace->getName()) return {};
    return colorSpace->getName();
}

std::string resolve(const OCIO::ConstConfigRcPtr& config, const std::string& name)
{
    if (!config || name.empty()) return {};
    try {
        return colorSpaceName(config->getColorSpace(name.c_str()));
    } catch (const OCIO::Exception&) {
        return {};
    }
}

bool isData(const OCIO::ConstConfigRcPtr& config, const std::string& name)
{
    if (name.empty()) return false;
    try {
        const OCIO::ConstColorSpaceRcPtr colorSpace = config->getColorSpace(name.c_str());
        return colorSpace && colorSpace->isData();
    } catch (const OCIO::Exception&) {
        return false;
    }
}

struct Resolution
{
    std::string name;
    std::string method;
    bool warning = false;
};

Resolution firstRole(const OCIO::ConstConfigRcPtr& config,
                     const std::initializer_list<const char*>& roles)
{
    for (const char* role : roles) {
        const std::string name = resolve(config, role);
        if (!name.empty() && !isData(config, name)) {
            return {name, std::string("role:") + role, role == OCIO::ROLE_DEFAULT};
        }
    }
    return {};
}

bool isDefaultToken(const pxr::TfToken& token)
{
    return token.IsEmpty() || token == pxr::TfToken("auto") ||
           token == pxr::TfToken("none");
}

} // namespace

namespace hdMoonray {

using scene_rdl2::logging::Logger;

struct ColorManagement::Impl
{
    Impl()
    {
        const char* ocio = std::getenv("OCIO");
        ocioEnvironment = ocio ? ocio : "";
        try {
            config = OCIO::GetCurrentConfig();
            if (config) configCacheId = config->getCacheID();
        } catch (const OCIO::Exception& e) {
            loadError = e.what();
        }

        source = firstRole(config, {
            OCIO::ROLE_SCENE_LINEAR, "default_float", "reference", OCIO::ROLE_DEFAULT});
        target = firstRole(config, {
            OCIO::ROLE_RENDERING, OCIO::ROLE_SCENE_LINEAR,
            "default_float", "reference", OCIO::ROLE_DEFAULT});
        rebuild();
    }

    bool usable() const
    {
        return config && !ocioEnvironment.empty() && !source.name.empty() && !target.name.empty();
    }

    void publishTarget() const
    {
        if (usable()) setenv(sTextureTargetEnv, target.name.c_str(), 1);
        else unsetenv(sTextureTargetEnv);
    }

    void rebuild()
    {
        processor.reset();
        cpu.reset();
        processorError.clear();
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            sourceProcessors.clear();
            warnedSources.clear();
        }
        publishTarget();
        if (!usable() || source.name == target.name) return;
        try {
            processor = config->getProcessor(source.name.c_str(), target.name.c_str());
            if (processor) cpu = processor->getDefaultCPUProcessor();
        } catch (const OCIO::Exception& e) {
            processorError = e.what();
            Logger::warn("hdMoonray OCIO processor failed from '", source.name,
                         "' to '", target.name, "': ", processorError);
        }
    }

    void transform(float* values, int channels) const
    {
        if (!cpu) return;
        try {
            if (channels == 4) cpu->applyRGBA(values);
            else cpu->applyRGB(values);
        } catch (const OCIO::Exception& e) {
            Logger::warn("hdMoonray OCIO color conversion failed: ", e.what());
        }
    }

    OCIO::ConstCPUProcessorRcPtr processorForSource(
        const pxr::TfToken& deliveredSource) const
    {
        if (deliveredSource.IsEmpty()) return cpu;

        const std::string resolved = resolve(config, deliveredSource.GetString());
        if (resolved.empty()) {
            std::lock_guard<std::mutex> lock(cacheMutex);
            if (warnedSources.insert(deliveredSource.GetString()).second) {
                Logger::warn("hdMoonray OCIO: delivered source color space '",
                             deliveredSource,
                             "' is unresolved; using scene-linear source '",
                             source.name, "'");
            }
            return cpu;
        }
        if (isData(config, resolved)) return {};
        if (!usable() || resolved == target.name) return {};

        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto found = sourceProcessors.find(resolved);
        if (found != sourceProcessors.end()) return found->second;
        try {
            OCIO::ConstProcessorRcPtr sourceProcessor =
                config->getProcessor(resolved.c_str(), target.name.c_str());
            OCIO::ConstCPUProcessorRcPtr sourceCpu = sourceProcessor ?
                sourceProcessor->getDefaultCPUProcessor() : OCIO::ConstCPUProcessorRcPtr();
            sourceProcessors.emplace(resolved, sourceCpu);
            Logger::info("hdMoonray OCIO parameter processor: version=", OCIO::GetVersion(),
                         " configCacheId=", configCacheId,
                         " source=", resolved, " sourceBy=metadata target=", target.name,
                         " targetBy=", target.method,
                         " conversionEnabled=", sourceCpu ? 1 : 0);
            return sourceCpu;
        } catch (const OCIO::Exception& e) {
            if (warnedSources.insert(resolved).second) {
                Logger::warn("hdMoonray OCIO parameter processor failed: source=", resolved,
                             " target=", target.name, " reason=", e.what(),
                             "; using scene-linear source");
            }
            return cpu;
        }
    }

    void transform(float* values, int channels,
                   const pxr::TfToken& deliveredSource) const
    {
        OCIO::ConstCPUProcessorRcPtr sourceCpu = processorForSource(deliveredSource);
        if (!sourceCpu) return;
        try {
            if (channels == 4) sourceCpu->applyRGBA(values);
            else sourceCpu->applyRGB(values);
        } catch (const OCIO::Exception& e) {
            Logger::warn("hdMoonray OCIO color conversion failed: ", e.what());
        }
    }

    OCIO::ConstConfigRcPtr config;
    OCIO::ConstProcessorRcPtr processor;
    OCIO::ConstCPUProcessorRcPtr cpu;
    pxr::TfToken requestedTarget;
    Resolution source;
    Resolution target;
    std::string ocioEnvironment;
    std::string configCacheId;
    std::string loadError;
    std::string processorError;
    bool unsupportedTarget = false;
    mutable std::mutex cacheMutex;
    mutable std::unordered_map<std::string, OCIO::ConstCPUProcessorRcPtr> sourceProcessors;
    mutable std::unordered_set<std::string> warnedSources;
};

ColorManagement::ColorManagement() : mImpl(new Impl())
{
    const std::string message = "hdMoonray OCIO runtime state: " + diagnosticSummary();
    if (diagnosticNeedsWarning()) Logger::warn(message);
    else Logger::info(message);
}

ColorManagement::~ColorManagement() = default;

bool ColorManagement::setRenderingColorSpace(const pxr::TfToken& token)
{
    if (token == mImpl->requestedTarget) return false;
    mImpl->requestedTarget = token;
    mImpl->unsupportedTarget = false;

    if (isDefaultToken(token)) {
        mImpl->target = firstRole(mImpl->config, {
            OCIO::ROLE_RENDERING, OCIO::ROLE_SCENE_LINEAR,
            "default_float", "reference", OCIO::ROLE_DEFAULT});
    } else {
        const std::string resolved = resolve(mImpl->config, token.GetString());
        if (!resolved.empty() && !isData(mImpl->config, resolved)) {
            mImpl->target = {resolved, "authored", false};
        } else {
            mImpl->unsupportedTarget = true;
            mImpl->target = firstRole(mImpl->config, {
                OCIO::ROLE_RENDERING, OCIO::ROLE_SCENE_LINEAR,
                "default_float", "reference", OCIO::ROLE_DEFAULT});
            Logger::warn("hdMoonray: renderingColorSpace '", token,
                         "' is not a usable color space in the active OCIO config; using '",
                         mImpl->target.name, "'");
        }
    }
    mImpl->rebuild();
    return true;
}

const std::string& ColorManagement::sourceColorSpace() const { return mImpl->source.name; }
const std::string& ColorManagement::workingColorSpace() const { return mImpl->target.name; }
bool ColorManagement::hasWorkingColorTransform() const { return bool(mImpl->cpu); }
bool ColorManagement::isUsable() const { return mImpl->usable(); }

bool ColorManagement::diagnosticNeedsWarning() const
{
    return !mImpl->usable() || !mImpl->loadError.empty() || !mImpl->processorError.empty() ||
           mImpl->unsupportedTarget || mImpl->source.warning || mImpl->target.warning;
}

std::string ColorManagement::diagnosticSummary() const
{
    std::ostringstream out;
    out << "version=" << OCIO::GetVersion()
        << " OCIO=\"" << (mImpl->ocioEnvironment.empty() ? "<unset>" : mImpl->ocioEnvironment) << '"'
        << " configCacheId=" << (mImpl->configCacheId.empty() ? "<unavailable>" : mImpl->configCacheId)
        << " requested=" << (mImpl->requestedTarget.IsEmpty() ? "<default>" : mImpl->requestedTarget.GetString())
        << " source=" << (mImpl->source.name.empty() ? "<unresolved>" : mImpl->source.name)
        << " sourceBy=" << (mImpl->source.method.empty() ? "<unresolved>" : mImpl->source.method)
        << " target=" << (mImpl->target.name.empty() ? "<unresolved>" : mImpl->target.name)
        << " targetBy=" << (mImpl->target.method.empty() ? "<unresolved>" : mImpl->target.method)
        << " conversionEnabled=" << (mImpl->cpu ? 1 : 0);
    if (mImpl->ocioEnvironment.empty()) out << " fallbackReason=\"OCIO is unset\"";
    else if (!mImpl->loadError.empty()) out << " fallbackReason=\"config load failed\"";
    else if (mImpl->source.name.empty()) out << " fallbackReason=\"source unresolved\"";
    else if (mImpl->target.name.empty()) out << " fallbackReason=\"target unresolved\"";
    else if (mImpl->source.name == mImpl->target.name) out << " fallbackReason=identity";
    else if (!mImpl->processorError.empty()) out << " fallbackReason=\"processor unavailable\"";
    else out << " fallbackReason=none";
    if (!mImpl->loadError.empty()) out << " loadError=\"" << mImpl->loadError << '"';
    if (!mImpl->processorError.empty()) out << " processorError=\"" << mImpl->processorError << '"';
    if (mImpl->unsupportedTarget) out << " unsupportedTarget=1";
    return out.str();
}

pxr::GfVec3f ColorManagement::toWorkingSpace(const pxr::GfVec3f& color) const
{
    std::array<float, 3> values = {color[0], color[1], color[2]};
    mImpl->transform(values.data(), 3);
    return pxr::GfVec3f(values[0], values[1], values[2]);
}

pxr::GfVec3f ColorManagement::toWorkingSpace(
    const pxr::GfVec3f& color, const pxr::TfToken& sourceColorSpace) const
{
    std::array<float, 3> values = {color[0], color[1], color[2]};
    mImpl->transform(values.data(), 3, sourceColorSpace);
    return pxr::GfVec3f(values[0], values[1], values[2]);
}

pxr::GfVec4f ColorManagement::toWorkingSpace(const pxr::GfVec4f& color) const
{
    std::array<float, 4> values = {color[0], color[1], color[2], color[3]};
    mImpl->transform(values.data(), 4);
    return pxr::GfVec4f(values[0], values[1], values[2], values[3]);
}

pxr::GfVec4f ColorManagement::toWorkingSpace(
    const pxr::GfVec4f& color, const pxr::TfToken& sourceColorSpace) const
{
    std::array<float, 4> values = {color[0], color[1], color[2], color[3]};
    mImpl->transform(values.data(), 4, sourceColorSpace);
    return pxr::GfVec4f(values[0], values[1], values[2], values[3]);
}

pxr::VtArray<pxr::GfVec3f> ColorManagement::toWorkingSpace(
    const pxr::VtArray<pxr::GfVec3f>& colors) const
{
    pxr::VtArray<pxr::GfVec3f> result(colors);
    for (pxr::GfVec3f& color : result) color = toWorkingSpace(color);
    return result;
}

pxr::VtArray<pxr::GfVec4f> ColorManagement::toWorkingSpace(
    const pxr::VtArray<pxr::GfVec4f>& colors) const
{
    pxr::VtArray<pxr::GfVec4f> result(colors);
    for (pxr::GfVec4f& color : result) color = toWorkingSpace(color);
    return result;
}

} // namespace hdMoonray
