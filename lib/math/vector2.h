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

    class alignas(16) Vector2
    {
        union
        {
            struct alignas(16) { float x, y, _pad1, _pad2; };
            DirectX::XMVECTOR _vec;
        };

    public:
        // Constructors
        Vector2() : _vec(DirectX::XMVectorZero()) {}
        Vector2(float x, float y) : _vec(DirectX::XMVectorSet(x, y, 0.0f, 0.0f)) {}
        explicit Vector2(const DirectX::XMVECTOR& reg) : _vec(reg) {}

        // Copy and move operations
        Vector2(const Vector2& other) = default;
        Vector2& operator=(const Vector2& other) = default;
        Vector2(Vector2&& other) noexcept = default;
        Vector2& operator=(Vector2&& other) noexcept = default;
        Vector2& operator=(const DirectX::XMVECTOR& vec) { _vec = vec; return *this; }

        // Conversion to XMVECTOR
        operator DirectX::XMVECTOR() const { return _vec; }
        operator DirectX::XMVECTOR() { return _vec; };

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
        static bool equal(const Vector2& a, const Vector2& b) noexcept;

        // Accessors
        float operator[](size_t index) const noexcept
        {
            assert(index < 2 && "Vector2 index out of bounds.");
            return (&x)[index];
        }

        float& operator[](size_t index) noexcept
        {
            assert(index < 2 && "Vector2 index out of bounds.");
            return (&x)[index];
        }

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
        return DirectX::XMVector2NearEqual(lhs._vec, rhs._vec, eps);
    }

    inline Vector2 operator+(const Vector2& v1, const Vector2& v2)
    {
        return Vector2(DirectX::XMVectorAdd(v1._vec, v2._vec));
    }

    inline Vector2 operator-(const Vector2& v1, const Vector2& v2)
    {
        return Vector2(DirectX::XMVectorSubtract(v1._vec, v2._vec));
    }

    inline Vector2 operator*(const Vector2& v, float scalar)
    {
        return Vector2(DirectX::XMVectorScale(v._vec, scalar));
    }

    inline Vector2 operator*(float scalar, const Vector2& v)
    {
        return v * scalar;
    }

    inline Vector2 operator/(const Vector2& v, float divisor)
    {
        assert(divisor != 0.0f && "Vector2 division by zero.");
        return Vector2(DirectX::XMVectorScale(v._vec, 1.0f / divisor));
    }

    inline Vector2 operator/(float divisor, const Vector2& v)
    {
        return v / divisor;
    }

    // Member function implementations
    inline Vector2& Vector2::operator+=(const Vector2& other) noexcept
    {
        _vec = DirectX::XMVectorAdd(_vec, other._vec);
        return *this;
    }

    inline Vector2& Vector2::operator-=(const Vector2& other) noexcept
    {
        _vec = DirectX::XMVectorSubtract(_vec, other._vec);
        return *this;
    }

    inline Vector2& Vector2::operator*=(float scalar) noexcept
    {
        _vec = DirectX::XMVectorScale(_vec, scalar);
        return *this;
    }

    inline Vector2& Vector2::operator/=(float scalar) noexcept
    {
        assert(scalar != 0.0f && "Vector2 division by zero.");
        _vec = DirectX::XMVectorScale(_vec, 1.0f / scalar);
        return *this;
    }

    inline Vector2 Vector2::operator-() const noexcept
    {
        return Vector2(DirectX::XMVectorNegate(_vec));
    }

    inline float Vector2::dot(const Vector2& v1, const Vector2& v2) noexcept
    {
        return v1.dot(v2);
    }


    inline float Vector2::dot(const Vector2& v) const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMVector2Dot(_vec, v._vec));
    }

    inline Vector2 Vector2::perpendicular() const noexcept
    {
        return Vector2(-y, x);
    }

    inline Vector2 Vector2::normalized() const noexcept
    {
        return Vector2(DirectX::XMVector2Normalize(_vec));
    }

    inline void Vector2::normalize() noexcept
    {
        _vec = DirectX::XMVector2Normalize(_vec);
    }

    inline Vector2 Vector2::scale(const Vector2& v) const noexcept
    {
        return Vector2(DirectX::XMVectorMultiply(_vec, v._vec));
    }

    inline float Vector2::magnitude() const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMVector2Length(_vec));
    }

    inline float Vector2::sqrMagnitude() const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMVector2LengthSq(_vec));
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
        return Vector2(DirectX::XMVectorLerp(a._vec, b._vec, t));
    }

    inline Vector2 Vector2::lerpUnclamped(const Vector2& a, const Vector2& b, float t)
    {
        return Vector2(DirectX::XMVectorLerp(a._vec, b._vec, t));
    }

    inline Vector2 Vector2::reflect(const Vector2& incident, const Vector2& normal)
    {
        float v = dot(incident, normal);
        return incident - 2.0f * v * normal;
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
        return Vector2(DirectX::XMVectorMax(a._vec, b._vec));
    }

    inline Vector2 Vector2::min(const Vector2& a, const Vector2& b)
    {
        return Vector2(DirectX::XMVectorMin(a._vec, b._vec));
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

    inline bool Vector2::equal(const Vector2& a, const Vector2& b) noexcept
    {
        return DirectX::XMVector2Equal(a._vec, b._vec);
    }

    inline std::ostream& operator<<(std::ostream& os, const Vector2& v)
    {
        os << "(" << v[0] << ", " << v[1] << ", " << ")";
        return os;
    }

}


#endif // __SYREN_VECTOR2_H__