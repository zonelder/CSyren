#include "pch.h"
#include "vector2.h"

namespace csyren::math
{
	// Static constants initialization
	const Vector2 Vector2::up(0.0f, 1.0f);
	const Vector2 Vector2::down(0.0f, -1.0f);
	const Vector2 Vector2::right(1.0f, 0.0f);
	const Vector2 Vector2::left(-1.0f, 0.0f);
	const Vector2 Vector2::one(1.0f, 1.0f);
	const Vector2 Vector2::zero(0.0f, 0.0f);
	const Vector2 Vector2::positiveInfinity(FLT_MAX, FLT_MAX);
	const Vector2 Vector2::negativeInfinity(-FLT_MAX, -FLT_MAX);
}

