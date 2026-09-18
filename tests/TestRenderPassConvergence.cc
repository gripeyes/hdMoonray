// SPDX-License-Identifier: Apache-2.0

#include <hydramoonray/NullRenderer.h>
#include <hydramoonray/renderDelegate.h>
#include <hydramoonray/renderPass.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/repr.h>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace pxr;
using namespace hdMoonray;

namespace {
class Backend final : public NullRenderer {
public:
    bool complete = true;
    bool isFrameComplete() const override { return !isUpdateActive() && complete; }
};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main()
{
    try {
        auto* backend = new Backend;
        HdMoonray_RenderDelegate delegate(backend);
        std::unique_ptr<HdRenderIndex> index(HdRenderIndex::New(&delegate, {}));
        require(bool(index), "cannot create render index");
        HdRprimCollection collection(TfToken("geometry"), HdReprSelector(TfToken("smoothHull")));
        HdMoonray_RenderPass pass(index.get(), collection, &delegate);
        backend->endUpdate();
        require(!pass.IsConverged(), "final image must get a presentation opportunity");
        require(pass.IsConverged(), "completed image must converge on second query");

        // Light Sync's UpdateGuard marks the backend pending after completion.
        // Hydra must see non-convergence before Execute can submit that edit.
        for (int edit = 0; edit < 3; ++edit) {
            backend->beginUpdate();
            require(!pass.IsConverged(), "pending scene edit retained cached convergence");
            require(!pass.IsConverged(), "repeated pending query converged");
            backend->complete = false;
            backend->endUpdate();
            require(!pass.IsConverged(), "new render retained previous completion");
            backend->complete = true;
            require(!pass.IsConverged(), "updated final image lost presentation opportunity");
            require(pass.IsConverged(), "updated completed frame never converged");
            require(pass.IsConverged(), "unchanged completed frame lost convergence");
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
