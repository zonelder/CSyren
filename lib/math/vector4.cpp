#include "pch.h"
#include "vector4.h"
#include <limits>

namespace csyren::math
{
	const Vector4 Vector4::zero(0.0f, 0.0f, 0.0f, 0.0f);
	const Vector4 Vector4::one(1.0f, 1.0f, 1.0f, 1.0f);

	const Vector4 Vector4::positiveInfinity(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
	const Vector4 Vector4::negativeInfinity(-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity());
}
