// Copyright 2023-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>

#include <memory>
#include <string>

namespace hdMoonray {

class ColorManagement
{
public:
    ColorManagement();
    ~ColorManagement();

    bool setRenderingColorSpace(const pxr::TfToken& token);
    const std::string& sourceColorSpace() const;
    const std::string& workingColorSpace() const;
    bool hasWorkingColorTransform() const;
    bool isUsable() const;
    bool diagnosticNeedsWarning() const;
    std::string diagnosticSummary() const;

    pxr::GfVec3f toWorkingSpace(const pxr::GfVec3f& color) const;
    pxr::GfVec3f toWorkingSpace(const pxr::GfVec3f& color,
                                const pxr::TfToken& sourceColorSpace) const;
    pxr::GfVec4f toWorkingSpace(const pxr::GfVec4f& color) const;
    pxr::GfVec4f toWorkingSpace(const pxr::GfVec4f& color,
                                const pxr::TfToken& sourceColorSpace) const;
    pxr::VtArray<pxr::GfVec3f> toWorkingSpace(
        const pxr::VtArray<pxr::GfVec3f>& colors) const;
    pxr::VtArray<pxr::GfVec4f> toWorkingSpace(
        const pxr::VtArray<pxr::GfVec4f>& colors) const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace hdMoonray
