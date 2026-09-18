// SPDX-License-Identifier: Apache-2.0

#include <hydramoonray/geometryBase.h>
#include <hydramoonray/mesh.h>
#include <hydramoonray/NullRenderer.h>
#include <hydramoonray/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <scene_rdl2/scene/rdl2/Layer.h>
#include <scene_rdl2/scene/rdl2/Light.h>
#include <scene_rdl2/scene/rdl2/LightSet.h>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace pxr;
using namespace hdMoonray;

namespace {
class Samples final : public HdSceneDelegate {
public:
    explicit Samples(HdRenderIndex* index) : HdSceneDelegate(index, SdfPath("/samples")) {}
    VtVec3fArray first, second;
    VtArray<TfToken> GetCategories(const SdfPath&) override { return {}; }
    size_t SamplePrimvar(const SdfPath&, const TfToken&, float start, float end,
                         size_t capacity, float* times, VtValue* values) override
    {
        if (capacity >= 2) {
            times[0] = start; times[1] = end;
            values[0] = VtValue(first); values[1] = VtValue(second);
        }
        return 2;
    }
};

class Probe final : public HdMoonray_GeometryBase {
public:
    Probe(HdRprim* prim, rdl2::SceneObject* object)
        : HdMoonray_GeometryBase(prim) { mGeometry = MoonrayObject(object); }
    void sample(Samples& samples, HdMoonray_RenderDelegate& delegate)
    {
        UpdateGuard guard(delegate, mGeometry);
        // Use the production points dispatch and a mesh proxy's actual attributes.
        primvarChanged(&samples, delegate, HdTokens->points, VtValue(samples.second),
                       HdInterpolationVertex, TfToken());
    }
};

void check(rdl2::SceneObject* object, const char* name, const VtVec3fArray& expected)
{
    const auto& actual = object->get<rdl2::Vec3fVector>(name);
    if (actual.size() != expected.size()) throw std::runtime_error("sample array size mismatch");
    for (size_t i = 0; i < actual.size(); ++i) {
        for (int c = 0; c < 3; ++c) {
            if (actual[i][c] != expected[i][c]) throw std::runtime_error("stale sample vertex");
        }
    }
}
}

int main()
{
    try {
        HdMoonray_RenderDelegate delegate(new NullRenderer);
        delegate.scene().enableTimeSamplingInterval(true);
        std::unique_ptr<HdRenderIndex> index(HdRenderIndex::New(&delegate, {}));
        Samples samples(index.get());
        HdMoonray_Mesh mesh(SdfPath("/samples/mesh"));
        auto* object = delegate.acquireSceneContext().createSceneObject("RdlMeshGeometry", "/samples/data");
        Probe probe(&mesh, object);
        // Returning to an earlier frame must replace every vertex in both arrays.
        for (const float frame : {1.0f, 10.0f, 1.0f}) {
            samples.first = {GfVec3f(frame-1, 0, 0), GfVec3f(0, frame-1, 0), GfVec3f(0, 0, frame-1)};
            samples.second = {GfVec3f(frame, 0, 0), GfVec3f(0, frame, 0), GfVec3f(0, 0, frame)};
            probe.sample(samples, delegate);
            check(object, "vertex_list_0", samples.first);
            check(object, "vertex_list_1", samples.second);
        }
        samples.first.clear(); samples.second.clear();
        probe.sample(samples, delegate);
        check(object, "vertex_list_0", samples.first);
        check(object, "vertex_list_1", samples.second);

        // Native scene-index rig replacement can sync only lights, not mesh
        // categories. Refresh the existing layer assignment in that case.
        auto& scene = delegate.scene();
        auto firstLight = scene.createObject("RectLight", SdfPath("/lights/first"));
        auto secondLight = scene.createObject("RectLight", SdfPath("/lights/second"));
        scene.addLight(); // prevent the default dome from entering these sets
        scene.setCategory(firstLight, LightCategory, TfToken());
        if (!scene.consumeCategoryChanges()) throw std::runtime_error("new light membership not recorded");
        if (scene.consumeCategoryChanges()) throw std::runtime_error("unchanged categories stayed dirty");
        const auto* layer = scene.sceneContext().getSceneObject("/DEFAULT/defaultLayer")->asA<rdl2::Layer>();
        for (const MoonrayObject light : {firstLight, secondLight, firstLight}) {
            scene.releaseCategory(firstLight, LightCategory, TfToken());
            scene.releaseCategory(secondLight, LightCategory, TfToken());
            scene.setCategory(light, LightCategory, TfToken());
            probe.refreshLightAssignments(&samples, delegate);
            const auto assignment = layer->begin(object->asA<rdl2::Geometry>());
            if (assignment == layer->end(object->asA<rdl2::Geometry>())) {
                throw std::runtime_error("missing refreshed geometry assignment");
            }
            const auto* lights = layer->lookupLightSet(*assignment);
            if (!lights || lights->getLights().size() != 1 ||
                !lights->contains(light.sceneObjectAs<rdl2::Light>())) {
                throw std::runtime_error("stale rig light assignment");
            }
            scene.consumeCategoryChanges();
            scene.setCategory(light, LightCategory, TfToken());
            if (scene.consumeCategoryChanges()) throw std::runtime_error("duplicate light membership caused update");
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
