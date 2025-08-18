#include "pch.h"
#include "matrix4x4.h"

namespace csyren::math
{
    const Matrix4x4 Matrix4x4::identity{};
    const Matrix4x4 Matrix4x4::zero = Matrix4x4(DirectX::XMMatrixSet(
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    ));
}
