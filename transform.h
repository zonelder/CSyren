#ifndef __CSYREN_TRANSFORM__
#define __CSYREN_TRANSFORM__
#include <DirectXMath.h>

namespace csyren::components
{
	struct Transform
	{
        DirectX::XMFLOAT3 pos{ 0,0,0 };
        DirectX::XMFLOAT3 rot{ 0,0,0 };   // radians
        DirectX::XMFLOAT3 scale{ 1,1,1 };

        DirectX::XMMATRIX world() const
        {
            using namespace DirectX;
            return XMMatrixScaling(scale.x, scale.y, scale.z) *
                XMMatrixRotationRollPitchYaw(rot.x, rot.y, rot.z) *
                XMMatrixTranslation(pos.x, pos.y, pos.z);
        }

	};
}

#endif
