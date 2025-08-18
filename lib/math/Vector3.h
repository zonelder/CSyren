#ifndef __SYREN_VECTOR3_H__
#define __SYREN_VECTOR3_H__
#include <DirectXMath.h>
#include <cmath>
#include <algorithm>
#include <iostream>

//focking WIPAPI issue
#undef max
#undef min

namespace csyren::math
{

	class alignas(16) Vector3
	{

		union
		{
			struct alignas(16) { float x, y, z, _pad; };
			DirectX::XMVECTOR _vec;
		};
	public:
		Vector3() : _vec(DirectX::XMVectorZero()) {}
		Vector3(float x, float y, float z) : _vec(DirectX::XMVectorSet(x, y, z, 0.0f)) {}
		explicit Vector3(const DirectX::XMVECTOR& reg) : _vec(reg) {}

		Vector3(const Vector3& other) = default;
		Vector3& operator=(const Vector3& other) { _vec = other._vec; return *this; };
		Vector3(Vector3&& other) noexcept = default;
		Vector3& operator=(Vector3&& other) noexcept = default;
		Vector3& operator=(const DirectX::XMVECTOR& vec) { _vec = vec; return *this; };

		operator DirectX::XMVECTOR() const { return _vec; };
		operator DirectX::XMVECTOR() { return _vec; };

		friend inline Vector3 operator+(const Vector3& v1, const Vector3& v2);
		friend Vector3 operator-(const Vector3& v1, const Vector3& v2);
		friend Vector3 operator*(const Vector3& v, float m);
		friend Vector3 operator*(float m, const Vector3& v);
		friend Vector3 operator/(const Vector3& v, float d);
		friend Vector3 operator/(float d, const Vector3& v);
		friend bool operator==(const Vector3& v1, const Vector3& v2) noexcept;

		bool equal(const Vector3& other) const noexcept;

		Vector3& operator+=(const Vector3& other) noexcept;
		Vector3& operator-=(const Vector3& other) noexcept;
		Vector3& operator*=(float scalar) noexcept;
		Vector3& operator/=(float acalar) noexcept;
		Vector3 operator-() const noexcept;


		float dot(const Vector3& v) const noexcept;
		Vector3 cross(const Vector3& v1) const noexcept;

		Vector3 normalized() const& noexcept;
		void normalize() noexcept;
		Vector3 scale(const Vector3& v) const noexcept;

		float magnitude() const noexcept;
		float sqrMagnitude() const noexcept;
		Vector3 clampMagnitude(float maxLength) const noexcept;

		static float dot(const Vector3& v1, const Vector3& v2) noexcept;
		static Vector3 cross(const Vector3& v1, const Vector3& v2) noexcept;
		static float distance(const Vector3& a, const Vector3& b) noexcept;
		static Vector3 lerp(const Vector3& a, const Vector3& b, float t);
		static Vector3 lerpUnclamped(const Vector3& a, const Vector3& b, float t);
		static Vector3 reflect(const Vector3& incident, const Vector3& normal);
		static Vector3 project(const Vector3& v, const Vector3& onto);
		static float angle(const Vector3& a, const Vector3& b);
		static Vector3 max(const Vector3& a, const Vector3& b);
		static Vector3 min(const Vector3& a, const Vector3& b);
		static Vector3 moveTowards(const Vector3& current, const Vector3& target, float maxDistDelta);
		static Vector3 scale(const Vector3& a, const Vector3& b);
		static bool equal(const Vector3& a, const Vector3& b) noexcept;


		float operator[](size_t index) const noexcept
		{
			assert(index < 3 && "Vector3 index out of bound.");
			return (&x)[index];
		}

		float& operator[](size_t index) noexcept
		{
			assert(index < 3 && "Vector3 index out of bound.");
			return (&x)[index];
		}

		static const Vector3 up;
		static const Vector3 down;
		static const Vector3 right;
		static const Vector3 left;
		static const Vector3 forward;
		static const Vector3 back;
		static const Vector3 one;
		static const Vector3 zero;

		static const Vector3 negativeInfinity;
		static const Vector3 positiveInfinity;

		static constexpr float s_epsilon = 0.001f;
	};



	inline bool operator==(const Vector3& lhs, const Vector3& rhs) noexcept
	{
		static const auto eps = DirectX::XMVectorReplicate(Vector3::s_epsilon);
		return DirectX::XMVector3NearEqual(lhs._vec, rhs._vec, eps);
	}

	inline Vector3 operator+(const Vector3& v1, const Vector3& v2)
	{
		return Vector3(DirectX::XMVectorAdd(v1._vec, v2._vec));
	}

	inline Vector3 operator-(const Vector3& v1, const Vector3& v2)
	{
		return Vector3(DirectX::XMVectorSubtract(v1._vec, v2._vec));
	}

	inline Vector3 operator*(const Vector3& v, float d)
	{
		return Vector3(DirectX::XMVectorScale(v._vec, d));
	}

	inline Vector3 operator*(float d, const Vector3& v)
	{
		return v * d;
	}

	inline Vector3 operator/(const Vector3& v, float d)
	{
		assert(d != 0.0f && "Vector3 division by zero");
		return Vector3(DirectX::XMVectorScale(v._vec, 1.0f / d));
	}

	inline Vector3 operator/(float d, const Vector3& v)
	{
		return v / d;
	}

	inline Vector3& Vector3::operator+=(const Vector3& other) noexcept
	{
		_vec = DirectX::XMVectorAdd(_vec, other._vec);
		return *this;
	}
	inline Vector3& Vector3::operator-=(const Vector3& other) noexcept
	{
		_vec = DirectX::XMVectorSubtract(_vec, other._vec);
		return *this;
	}

	inline Vector3& Vector3::operator*=(float scalar) noexcept
	{
		_vec = DirectX::XMVectorScale(_vec, scalar);
		return *this;
	}

	inline Vector3& Vector3::operator/=(float scalar) noexcept
	{
		assert(scalar != 0.0f && "Vector3::attempt to divide by zero.");

		_vec = DirectX::XMVectorScale(_vec, 1.0f / scalar);
		return *this;
	}

	inline Vector3 Vector3::operator-() const noexcept
	{
		return Vector3(DirectX::XMVectorNegate(_vec));
	}

	inline float Vector3::dot(const Vector3& v1, const Vector3& v2) noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3Dot(v1._vec, v2._vec));
	}

	inline float Vector3::dot(const Vector3& v) const noexcept
	{
		return dot(*this, v);
	}

	inline Vector3 Vector3::cross(const Vector3& a, const Vector3& b) noexcept
	{
		return Vector3(DirectX::XMVector3Cross(a._vec, b._vec));
	}

	inline Vector3 Vector3::cross(const Vector3& v1) const noexcept
	{
		return cross(*this, v1);
	}


	inline Vector3 Vector3::normalized() const& noexcept
	{
		return Vector3(DirectX::XMVector3Normalize(_vec));

	}

	inline void Vector3::normalize() noexcept
	{
		_vec = DirectX::XMVector3Normalize(_vec);
	}

	inline Vector3 Vector3::scale(const Vector3& v) const noexcept
	{
		return Vector3(DirectX::XMVectorMultiply(_vec, v._vec));
	}

	inline float Vector3::magnitude() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3Length(_vec));
	}

	inline float Vector3::sqrMagnitude() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(_vec));
	}
	inline float Vector3::distance(const Vector3& a, const Vector3& b) noexcept
	{
		return (a - b).magnitude();
	}

	inline Vector3 Vector3::lerp(const Vector3& a, const Vector3& b, float t)
	{
		return Vector3(DirectX::XMVectorLerp(a._vec, b._vec, std::clamp(t, 0.0f, 1.0f)));
	}

	inline Vector3 Vector3::lerpUnclamped(const Vector3& a, const Vector3& b, float t)
	{
		return Vector3(DirectX::XMVectorLerp(a._vec, b._vec, t));
	}

	inline Vector3 Vector3::reflect(const Vector3& incident, const Vector3& normal)
	{
		return Vector3(DirectX::XMVector3Reflect(incident._vec, normal._vec));
	}

	inline Vector3 Vector3::project(const Vector3& vector, const Vector3& onto)
	{
		//TODO test simd inlining perfomance here
		float sqrMag = onto.sqrMagnitude();
		if (sqrMag == 0.0f)
			return Vector3();
		return (dot(vector, onto) / sqrMag) * onto;
	}

	inline float Vector3::angle(const Vector3& a, const Vector3& b)
	{
		//TODO test simd inlining here
		float magA = a.magnitude();
		float magB = b.magnitude();
		if (magA == 0.0f || magB == 0.0f)
			return 0.0f;
		float cosTheta = dot(a, b) / (magA * magB);
		cosTheta = std::clamp(cosTheta, -1.0f, 1.0f);
		return std::acos(cosTheta) * (180.0f / DirectX::XM_PI);
	}

	inline Vector3 Vector3::clampMagnitude(float maxLength) const noexcept
	{
		float sqrtMag = sqrMagnitude();
		if (sqrtMag > maxLength * maxLength)
		{
			return normalized() * maxLength;
		}
		return *this;
	}
	//TODO smging strange about this 2 function. should find out what
	inline Vector3 Vector3::max(const Vector3& a, const Vector3& b)
	{
		return Vector3(DirectX::XMVectorMax(a._vec, b._vec));
	}
	inline Vector3 Vector3::min(const Vector3& a, const Vector3& b)
	{
		return Vector3(DirectX::XMVectorMin(a._vec, b._vec));
	}

	inline Vector3 Vector3::moveTowards(const Vector3& current, const Vector3& target, float maxDelta)
	{
		Vector3 toTarget = target - current;
		float dist = toTarget.magnitude();
		if (dist < maxDelta || dist == 0.0f)
			return target;
		return current + toTarget / dist * maxDelta;
	}

	inline Vector3 Vector3::scale(const Vector3& a, const Vector3& b)
	{
		return a.scale(b);
	}

	inline bool Vector3::equal(const Vector3& a) const noexcept
	{
		return DirectX::XMVector3Equal(_vec, a._vec);
	}
	inline bool Vector3::equal(const Vector3& a, const Vector3& b) noexcept
	{
		return a.equal(b);
	}



	inline std::ostream& operator<<(std::ostream& os, const Vector3& v)
	{
		os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
		return os;
	}
}

#endif