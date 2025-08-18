#include "pch.h"
#include "vector3.h"
#include <limits>

namespace csyren::math
{
	const Vector3 Vector3::up(0.0f, 1.0f, 0.0f);
	const Vector3 Vector3::down(0.0f, -1.0f, 0.0f);
	const Vector3 Vector3::right(1.0f, 0.0f, 0.0f);
	const Vector3 Vector3::left(-1.0f, 0.0f, 0.0f);
	const Vector3 Vector3::forward(0.0f, 0.0f, 1.0f);
	const Vector3 Vector3::back(0.0f, 0.0f, -1.0f);
	const Vector3 Vector3::zero(0.0f, 0.0f, 0.0f);
	const Vector3 Vector3::one(1.0f, 1.0f, 1.0f);

	const Vector3 Vector3::positiveInfinity(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
	const Vector3 Vector3::negativeInfinity(-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity());
}
