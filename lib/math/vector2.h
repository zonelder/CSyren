#ifndef __SYREN_VECTOR2_H__
#define __SYREN_VECTOR2_H__

#include <DirectXMath.h>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <iostream>

#undef max
#undef min

namespace csyren::math
{

    class Vector2 : public DirectX::XMFLOAT2
    {
    public:
        // Constructors
        Vector2() noexcept : DirectX::XMFLOAT2(0.0f, 0.0f) {}
        Vector2(float x, float y) noexcept : DirectX::XMFLOAT2(x, y) {}
        explicit Vector2(DirectX::XMVECTOR vec) noexcept
        {
            DirectX::XMStoreFloat2(this, vec);
        }

        Vector2(const Vector2&) = default;
        Vector2& operator=(const Vector2&) = default;
        Vector2(Vector2&&) = default;
        Vector2& operator=(Vector2&&) = default;

        Vector2& operator=(DirectX::FXMVECTOR vec) noexcept
        {
            DirectX::XMStoreFloat2(this, vec);
            return *this;
        }

        operator DirectX::XMVECTOR() const noexcept
        {
            return DirectX::XMLoadFloat2(this);
        }

        // Доступ по индексу
        float operator[](size_t index) const noexcept
        {
            assert(index < 2 && "Vector2 index out of bounds.");
            return reinterpret_cast<const float*>(this)[index];
        }

        float& operator[](size_t index) noexcept
        {
            assert(index < 2 && "Vector2 index out of bounds.");
            return reinterpret_cast<float*>(this)[index];
        }

        // Arithmetic operators
        friend Vector2 operator+(const Vector2& v1, const Vector2& v2);
        friend Vector2 operator-(const Vector2& v1, const Vector2& v2);
        friend Vector2 operator*(const Vector2& v, float scalar);
        friend Vector2 operator*(float scalar, const Vector2& v);
        friend Vector2 operator/(const Vector2& v, float divisor);
        friend Vector2 operator/(float divisor, const Vector2& v);
        friend bool operator==(const Vector2& v1, const Vector2& v2) noexcept;

        // Compound assignment operators
        Vector2& operator+=(const Vector2& other) noexcept;
        Vector2& operator-=(const Vector2& other) noexcept;
        Vector2& operator*=(float scalar) noexcept;
        Vector2& operator/=(float scalar) noexcept;
        Vector2 operator-() const noexcept;

        // Vector operations
        float dot(const Vector2& v) const noexcept;
        Vector2 perpendicular() const noexcept;
        Vector2 normalized() const noexcept;
        void normalize() noexcept;
        Vector2 scale(const Vector2& v) const noexcept;
        float magnitude() const noexcept;
        float sqrMagnitude() const noexcept;
        Vector2 clampMagnitude(float maxLength) const noexcept;

        // Static utility functions
        static float dot(const Vector2& v1, const Vector2& v2) noexcept;
        static float distance(const Vector2& a, const Vector2& b) noexcept;
        static Vector2 lerp(const Vector2& a, const Vector2& b, float t);
        static Vector2 lerpUnclamped(const Vector2& a, const Vector2& b, float t);
        static Vector2 reflect(const Vector2& incident, const Vector2& normal);
        static Vector2 project(const Vector2& vector, const Vector2& onto);
        static float angle(const Vector2& a, const Vector2& b);
        static Vector2 max(const Vector2& a, const Vector2& b);
        static Vector2 min(const Vector2& a, const Vector2& b);
        static Vector2 moveTowards(const Vector2& current, const Vector2& target, float maxDistanceDelta);
        static Vector2 scale(const Vector2& a, const Vector2& b);
        static bool exactEqual(const Vector2& a, const Vector2& b) noexcept;

        // Constants
        static const Vector2 up;
        static const Vector2 down;
        static const Vector2 right;
        static const Vector2 left;
        static const Vector2 one;
        static const Vector2 zero;
        static const Vector2 positiveInfinity;
        static const Vector2 negativeInfinity;

        static constexpr float Epsilon = 0.001f;
    };

    // Inline operator implementations
    inline bool operator==(const Vector2& lhs, const Vector2& rhs) noexcept
    {
        const auto eps = DirectX::XMVectorReplicate(Vector2::Epsilon);
        return DirectX::XMVector2NearEqual(lhs, rhs, eps);
    }

    inline Vector2 operator+(const Vector2& v1, const Vector2& v2)
    {
        return Vector2(DirectX::XMVectorAdd(v1, v2));
    }

    inline Vector2 operator-(const Vector2& v1, const Vector2& v2)
    {
        return Vector2(DirectX::XMVectorSubtract(v1, v2));
    }

    inline Vector2 operator*(const Vector2& v, float scalar)
    {
        return Vector2(DirectX::XMVectorScale(v, scalar));
    }

    inline Vector2 operator*(float scalar, const Vector2& v)
    {
        return v * scalar;
    }

    inline Vector2 operator/(const Vector2& v, float divisor)
    {
        assert(divisor != 0.0f && "Vector2 division by zero.");
        return Vector2(DirectX::XMVectorScale(v, 1.0f / divisor));
    }

    inline Vector2 operator/(float divisor, const Vector2& v)
    {
        return v / divisor;
    }

    // Member function implementations
    inline Vector2& Vector2::operator+=(const Vector2& other) noexcept
    {
        DirectX::XMStoreFloat2(this, DirectX::XMVectorAdd(*this, other));
        return *this;
    }

    inline Vector2& Vector2::operator-=(const Vector2& other) noexcept
    {
        DirectX::XMStoreFloat2(this, DirectX::XMVectorSubtract(*this, other));
        return *this;
    }

    inline Vector2& Vector2::operator*=(float scalar) noexcept
    {
        DirectX::XMStoreFloat2(this, DirectX::XMVectorScale(*this, scalar));
        return *this;
    }

    inline Vector2& Vector2::operator/=(float scalar) noexcept
    {
        assert(scalar != 0.0f && "Division by zero");
        DirectX::XMStoreFloat2(this, DirectX::XMVectorScale(*this, 1.0f / scalar));
        return *this;
    }

    inline Vector2 Vector2::operator-() const noexcept
    {
        return Vector2(DirectX::XMVectorNegate(*this));
    }

    inline float Vector2::dot(const Vector2& v1, const Vector2& v2) noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMVector2Dot(v1, v2));
    }


    inline float Vector2::dot(const Vector2& v) const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMVector2Dot(*this, v));
    }

    inline Vector2 Vector2::perpendicular() const noexcept
    {
        return Vector2(-y, x);
    }

    inline Vector2 Vector2::normalized() const noexcept
    {
        return Vector2(DirectX::XMVector2Normalize(*this));
    }

    inline void Vector2::normalize() noexcept
    {
        DirectX::XMStoreFloat2(this, DirectX::XMVector2Normalize(*this));
    }

    inline Vector2 Vector2::scale(const Vector2& v) const noexcept
    {
        return Vector2(DirectX::XMVectorMultiply(*this, v));
    }

    inline float Vector2::magnitude() const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMVector2Length(*this));
    }

    inline float Vector2::sqrMagnitude() const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMVector2LengthSq(*this));
    }

    inline Vector2 Vector2::clampMagnitude(float maxLength) const noexcept
    {
        float mag = magnitude();
        return (mag > maxLength) ? (*this * (maxLength / mag)) : *this;
    }

    inline float Vector2::distance(const Vector2& a, const Vector2& b) noexcept
    {
        return (a - b).magnitude();
    }

    inline Vector2 Vector2::lerp(const Vector2& a, const Vector2& b, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return Vector2(DirectX::XMVectorLerp(a, b, t));
    }

    inline Vector2 Vector2::lerpUnclamped(const Vector2& a, const Vector2& b, float t)
    {
        return Vector2(DirectX::XMVectorLerp(a, b, t));
    }

    inline Vector2 Vector2::reflect(const Vector2& incident, const Vector2& normal)
    {
        return Vector2(DirectX::XMVector2Reflect(incident, normal));
    }

    inline Vector2 Vector2::project(const Vector2& vector, const Vector2& onto)
    {
        float denom = onto.sqrMagnitude();
        return (denom < Epsilon) ? Vector2::zero : (onto * (dot(vector, onto) / denom));
    }

    inline float Vector2::angle(const Vector2& a, const Vector2& b)
    {
        float mag = std::sqrt(a.sqrMagnitude() * b.sqrMagnitude());
        return (mag < Epsilon) ? 0.0f : std::acos(dot(a, b) / mag) * (180.0f / DirectX::XM_PI);
    }

    inline Vector2 Vector2::max(const Vector2& a, const Vector2& b)
    {
        return Vector2(DirectX::XMVectorMax(a, b));
    }

    inline Vector2 Vector2::min(const Vector2& a, const Vector2& b)
    {
        return Vector2(DirectX::XMVectorMin(a, b));
    }

    inline Vector2 Vector2::moveTowards(const Vector2& current, const Vector2& target, float maxDistanceDelta)
    {
        Vector2 delta = target - current;
        float mag = delta.magnitude();
        return (mag <= maxDistanceDelta || mag == 0.0f) ? target : current + delta / mag * maxDistanceDelta;
    }

    inline Vector2 Vector2::scale(const Vector2& a, const Vector2& b)
    {
        return a.scale(b);
    }

    inline bool Vector2::exactEqual(const Vector2& a, const Vector2& b) noexcept
    {
        return DirectX::XMVector2Equal(a, b);
    }

    inline std::ostream& operator<<(std::ostream& os, const Vector2& v)
    {
        os << "(" << v[0] << ", " << v[1] << ", " << ")";
        return os;
    }

}


#endif // __SYREN_VECTOR2_H__