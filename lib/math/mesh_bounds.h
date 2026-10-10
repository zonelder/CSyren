#pragma once
#include "vector3.h"
#include <algorithm>
#include <cmath>

namespace csyren::math
{
    struct MeshBounds
    {
        Vector3 min{}, max{};
        bool valid = false;
        void include(const Vector3& p)
        {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return;
            if (!valid) { min = max = p; valid = true; return; }
            for (int i = 0; i < 3; ++i)
            { min[i] = std::min(min[i], p[i]); max[i] = std::max(max[i], p[i]); }
        }
    };
}
