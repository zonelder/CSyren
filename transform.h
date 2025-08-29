#ifndef __CSYREN_TRANSFORM__
#define __CSYREN_TRANSFORM__

#include "core/serializer.h"
#include "math/math.h"

namespace csyren::components
{
	struct Transform
	{
        math::Vector3 position{ 0,0,0 };
        math::Quaternion rotation;//radians;
        math::Vector3 scale{ 1,1,1 };
        math::Matrix4x4 world() const
        {
            using namespace DirectX;
            return math::Matrix4x4::TRS(position, rotation, scale);
        }
    private:
        SERIALIZABLE(Transform, position,rotation,scale);
	};
}

#endif
