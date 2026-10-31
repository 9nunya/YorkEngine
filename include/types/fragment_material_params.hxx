#ifndef YORK_FRAGMENT_MATERIAL_PARAMS_HXX
#define YORK_FRAGMENT_MATERIAL_PARAMS_HXX

#include <cstdint>

#include <glm/glm.hpp>

namespace york::types {
    // Layout must match the push constant block the shaders share: the model
    // matrix occupies global bytes [0, 64), this block lands at [64, 88) —
    // a single shared block keeps MoltenVK's strict per-block validation happy.
    struct FragmentMaterialParams {
        glm::vec4 baseColorFactor{1.0f};
        float alphaCutoff{0.0f};
        // Unused until alpha modes land; reserved so the shader layout is stable.
        uint32_t reservedAlphaMode{0};
    };

    static_assert(sizeof(FragmentMaterialParams) == 24);
    static_assert(offsetof(FragmentMaterialParams, baseColorFactor) == 0);
    static_assert(offsetof(FragmentMaterialParams, alphaCutoff) == 16);
    static_assert(offsetof(FragmentMaterialParams, reservedAlphaMode) == 20);
}

#endif // YORK_FRAGMENT_MATERIAL_PARAMS_HXX
