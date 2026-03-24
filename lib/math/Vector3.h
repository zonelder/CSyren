#pragma once
#include <DirectXMath.h>
#include <cmath>
#include <algorithm>
#include <iostream>

//focking WIPAPI issue
#undef max
#undef min

namespace csyren::math
{

	class Vector3 : public DirectX::XMFLOAT3
	{
	public:
		Vector3()  noexcept : DirectX::XMFLOAT3(0.0f,0.0f,0.0f) {}
		Vector3(float x, float y, float z) noexcept : DirectX::XMFLOAT3(x, y, z) {}
		explicit Vector3(DirectX::XMVECTOR reg)  noexcept {DirectX::XMStoreFloat3(this, reg); }

		Vector3(const Vector3& other)				noexcept = default;
		Vector3& operator=(const Vector3& other)	noexcept = default;
		Vector3(Vector3&& other)					noexcept = default;
		Vector3& operator=(Vector3&& other)			noexcept = default;
		Vector3& operator=(DirectX::XMVECTOR vec)	noexcept 
		{
			DirectX::XMStoreFloat3(this, vec);
			return *this;
		}

		operator DirectX::XMVECTOR() const noexcept { return DirectX::XMLoadFloat3(this); };

		float operator[](size_t index) const noexcept
		{
			assert(index < 3 && "Vector3 index out of bounds.");
			return reinterpret_cast<const float*>(this)[index];
		}

		float& operator[](size_t index) noexcept
		{
			assert(index < 3 && "Vector3 index out of bounds.");
			return reinterpret_cast<float*>(this)[index];
		}

		friend inline Vector3 operator+(const Vector3& v1, const Vector3& v2);
		friend Vector3 operator-(const Vector3& v1, const Vector3& v2);
		friend Vector3 operator*(const Vector3& v, float m);
		friend Vector3 operator*(float m, const Vector3& v);
		friend Vector3 operator/(const Vector3& v, float d);
		friend Vector3 operator/(float d, const Vector3& v);
		friend bool operator==(const Vector3& v1, const Vector3& v2) noexcept;

		bool exactEqual(const Vector3& other) const noexcept;

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
		static bool exactEqual(const Vector3& a, const Vector3& b) noexcept;

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
		return DirectX::XMVector3NearEqual(lhs, rhs, eps);
	}

	inline Vector3 operator+(const Vector3& v1, const Vector3& v2)
	{
		return Vector3(DirectX::XMVectorAdd(v1, v2));
	}

	inline Vector3 operator-(const Vector3& v1, const Vector3& v2)
	{
		return Vector3(DirectX::XMVectorSubtract(v1, v2));
	}

	inline Vector3 operator*(const Vector3& v, float d)
	{
		return Vector3(DirectX::XMVectorScale(v, d));
	}

	inline Vector3 operator*(float d, const Vector3& v)
	{
		return v * d;
	}

	inline Vector3 operator/(const Vector3& v, float d)
	{
		assert(d != 0.0f && "Vector3 division by zero");
		return Vector3(DirectX::XMVectorScale(v, 1.0f / d));
	}

	inline Vector3 operator/(float d, const Vector3& v)
	{
		return v / d;
	}

	inline Vector3& Vector3::operator+=(const Vector3& other) noexcept
	{
		DirectX::XMVECTOR result = DirectX::XMVectorAdd(*this, other);
		DirectX::XMStoreFloat3(this, result);
		return *this;
	}
	inline Vector3& Vector3::operator-=(const Vector3& other) noexcept
	{
		DirectX::XMVECTOR result = DirectX::XMVectorSubtract(*this, other);
		DirectX::XMStoreFloat3(this, result);
		return *this;
	}

	inline Vector3& Vector3::operator*=(float scalar) noexcept
	{
		DirectX::XMVECTOR result = DirectX::XMVectorScale(*this, scalar);
		DirectX::XMStoreFloat3(this, result);
		return *this;
	}

	inline Vector3& Vector3::operator/=(float scalar) noexcept
	{
		assert(scalar != 0.0f && "Division by zero");
		DirectX::XMVECTOR result = DirectX::XMVectorScale(*this, 1.0f / scalar);
		DirectX::XMStoreFloat3(this, result);
		return *this;
	}

	inline Vector3 Vector3::operator-() const noexcept
	{
		return Vector3(DirectX::XMVectorNegate(*this));
	}

	inline float Vector3::dot(const Vector3& v1, const Vector3& v2) noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3Dot(v1, v2));
	}

	inline float Vector3::dot(const Vector3& v) const noexcept
	{
		return dot(*this, v);
	}

	inline Vector3 Vector3::cross(const Vector3& a, const Vector3& b) noexcept
	{
		return Vector3(DirectX::XMVector3Cross(a, b));
	}

	inline Vector3 Vector3::cross(const Vector3& v1) const noexcept
	{
		return cross(*this, v1);
	}


	inline Vector3 Vector3::normalized() const& noexcept
	{
		return Vector3(DirectX::XMVector3Normalize(*this));

	}

	inline void Vector3::normalize() noexcept
	{
		DirectX::XMStoreFloat3(this, DirectX::XMVector3Normalize(*this));
	}

	inline Vector3 Vector3::scale(const Vector3& v) const noexcept
	{
		return Vector3(DirectX::XMVectorMultiply(*this, v));
	}

	inline float Vector3::magnitude() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3Length(*this));
	}

	inline float Vector3::sqrMagnitude() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(*this));
	}
	inline float Vector3::distance(const Vector3& a, const Vector3& b) noexcept
	{
		return (a - b).magnitude();
	}

	inline Vector3 Vector3::lerp(const Vector3& a, const Vector3& b, float t)
	{
		return Vector3(DirectX::XMVectorLerp(a, b, std::clamp(t, 0.0f, 1.0f)));
	}

	inline Vector3 Vector3::lerpUnclamped(const Vector3& a, const Vector3& b, float t)
	{
		return Vector3(DirectX::XMVectorLerp(a, b, t));
	}

	inline Vector3 Vector3::reflect(const Vector3& incident, const Vector3& normal)
	{
		return Vector3(DirectX::XMVector3Reflect(incident, normal));
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

	inline Vector3 Vector3::max(const Vector3& a, const Vector3& b)
	{
		return Vector3(DirectX::XMVectorMax(a, b));
	}
	inline Vector3 Vector3::min(const Vector3& a, const Vector3& b)
	{
		return Vector3(DirectX::XMVectorMin(a, b));
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

	inline bool Vector3::exactEqual(const Vector3& a) const noexcept
	{
		return DirectX::XMVector3Equal(*this, a);
	}
	inline bool Vector3::exactEqual(const Vector3& a, const Vector3& b) noexcept
	{
		return a.exactEqual(b);
	}



	inline std::ostream& operator<<(std::ostream& os, const Vector3& v)
	{
		os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
		return os;
	}
}