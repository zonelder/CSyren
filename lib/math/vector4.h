#ifndef __VECTOR4_H__
#define __VECTOR4_H__
#include <DirectXMath.h>
#include <cmath>
#include <algorithm>
#include <iostream>

#undef max
#undef min

namespace csyren::math
{

    class alignas(16) Vector4
    {
        union
        {
            struct alignas(16) { float x, y, z, w; };
            DirectX::XMVECTOR _vec;
        };

    public:
        Vector4() : _vec(DirectX::XMVectorZero()) {}
        Vector4(float x, float y, float z, float w) : _vec(DirectX::XMVectorSet(x, y, z, w)) {}
        explicit Vector4(const DirectX::XMVECTOR& reg) : _vec(reg) {}

        Vector4(const Vector4& other) = default;
        Vector4& operator=(const Vector4& other) { _vec = other._vec; return *this; };
        Vector4(Vector4&& other) noexcept = default;
        Vector4& operator=(Vector4&& other) noexcept = default;
        Vector4& operator=(const DirectX::XMVECTOR& vec) { _vec = vec; return *this; };

        operator DirectX::XMVECTOR() const { return _vec; };
        operator DirectX::XMVECTOR() { return _vec; };

        friend inline Vector4 operator+(const Vector4& v1, const Vector4& v2);
        friend Vector4 operator-(const Vector4& v1, const Vector4& v2);
        friend Vector4 operator*(const Vector4& v, float m);
        friend Vector4 operator*(float m, const Vector4& v);
        friend Vector4 operator/(const Vector4& v, float d);
        friend Vector4 operator/(float d, const Vector4& v);
        friend bool operator==(const Vector4& v1, const Vector4& v2) noexcept;

        bool equal(const Vector4& other) const noexcept;

        Vector4& operator+=(const Vector4& other) noexcept;
        Vector4& operator-=(const Vector4& other) noexcept;
        Vector4& operator*=(float scalar) noexcept;
        Vector4& operator/=(float scalar) noexcept;
        Vector4 operator-() const noexcept;

        float dot(const Vector4& v) const noexcept;
        Vector4 normalized() const& noexcept;
        void normalize() noexcept;
        Vector4 scale(const Vector4& v) const noexcept;

        float magnitude() const noexcept;
        float sqrMagnitude() const noexcept;
        Vector4 clampMagnitude(float maxLength) const noexcept;

        static float dot(const Vector4& v1, const Vector4& v2) noexcept;
        static float distance(const Vector4& a, const Vector4& b) noexcept;
        static Vector4 lerp(const Vector4& a, const Vector4& b, float t);
        static Vector4 lerpUnclamped(const Vector4& a, const Vector4& b, float t);
        static Vector4 reflect(const Vector4& incident, const Vector4& normal);
        static Vector4 project(const Vector4& v, const Vector4& onto);
        static float angle(const Vector4& a, const Vector4& b);
        static Vector4 max(const Vector4& a, const Vector4& b);
        static Vector4 min(const Vector4& a, const Vector4& b);
        static Vector4 moveTowards(const Vector4& current, const Vector4& target, float maxDistDelta);
        static Vector4 scale(const Vector4& a, const Vector4& b);
        static bool equal(const Vector4& a, const Vector4& b) noexcept;

        float operator[](size_t index) const noexcept
        {
            assert(index < 4 && "Vector4 index out of bound.");
            return (&x)[index];
        }

        float& operator[](size_t index) noexcept
        {
            assert(index < 4 && "Vector4 index out of bound.");
            return (&x)[index];
        }

        static const Vector4 one;
        static const Vector4 zero;
        static const Vector4 negativeInfinity;
        static const Vector4 positiveInfinity;

        static constexpr float s_epsilon = 0.001f;
    };

    inline bool operator==(const Vector4& lhs, const Vector4& rhs) noexcept
    {
        static const auto eps = DirectX::XMVectorReplicate(Vector4::s_epsilon);
        return DirectX::XMVector4NearEqual(lhs._vec, rhs._vec, eps);
    }

    inline Vector4 operator+(const Vector4& v1, const Vector4& v2)
    {
        return Vector4(DirectX::XMVectorAdd(v1._vec, v2._vec));
    }

    inline Vector4 operator-(const Vector4& v1, const Vector4& v2)
    {
        return Vector4(DirectX::XMVectorSubtract(v1._vec, v2._vec));
    }

    inline Vector4 operator*(const Vector4& v, float d)
    {
        return Vector4(DirectX::XMVectorScale(v._vec, d));
    }

    inline Vector4 operator*(float d, const Vector4& v)
    {
        return v * d;
    }

    inline Vector4 operator/(const Vector4& v, float d)
    {
        assert(d != 0.0f && "Vector4 division by zero");
        return Vector4(DirectX::XMVectorScale(v._vec, 1.0f / d));
    }

    inline Vector4 operator/(float d, const Vector4& v)
    {
        return v / d;
    }

    inline Vector4& Vector4::operator+=(const Vector4& other) noexcept
    {
        _vec = DirectX::XMVectorAdd(_vec, other._vec);
        return *this;
    }

    inline Vector4& Vector4::operator-=(const Vector4& other) noexcept
    {
        _vec = DirectX::XMVectorSubtract(_vec, other._vec);
        return *this;
    }

    inline Vector4& Vector4::operator*=(float scalar) noexcept
    {
        _vec = DirectX::XMVectorScale(_vec, scalar);
        return *this;
    }

    inline Vector4& Vector4::operator/=(float scalar) noexcept
    {
        assert(scalar != 0.0f && "Vector4::attempt to divide by zero.");
        _vec = DirectX::XMVectorScale(_vec, 1.0f / scalar);
        return *this;
    }

    inline Vector4 Vector4::operator-() const noexcept
    {
        return Vector4(DirectX::XMVectorNegate(_vec));
    }

    inline float Vector4::dot(const Vector4& v1, const Vector4& v2) noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMVector4Dot(v1._vec, v2._vec));
    }

    inline float Vector4::dot(const Vector4& v) const noexcept
    {
        return dot(*this, v);
    }

    inline Vector4 Vector4::normalized() const& noexcept
    {
        return Vector4(DirectX::XMVector4Normalize(_vec));
    }

    inline void Vector4::normalize() noexcept
    {
        _vec = DirectX::XMVector4Normalize(_vec);
    }

    inline Vector4 Vector4::scale(const Vector4& v) const noexcept
    {
        return Vector4(DirectX::XMVectorMultiply(_vec, v._vec));
    }

    inline float Vector4::magnitude() const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMVector4Length(_vec));
    }

    inline float Vector4::sqrMagnitude() const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMVector4LengthSq(_vec));
    }

    inline float Vector4::distance(const Vector4& a, const Vector4& b) noexcept
    {
        return (a - b).magnitude();
    }

    inline Vector4 Vector4::lerp(const Vector4& a, const Vector4& b, float t)
    {
        return Vector4(DirectX::XMVectorLerp(a._vec, b._vec, std::clamp(t, 0.0f, 1.0f)));
    }

    inline Vector4 Vector4::lerpUnclamped(const Vector4& a, const Vector4& b, float t)
    {
        return Vector4(DirectX::XMVectorLerp(a._vec, b._vec, t));
    }

    inline Vector4 Vector4::reflect(const Vector4& incident, const Vector4& normal)
    {
        return Vector4(DirectX::XMVector4Reflect(incident._vec, normal._vec));
    }

    inline Vector4 Vector4::project(const Vector4& vector, const Vector4& onto)
    {
        float sqrMag = onto.sqrMagnitude();
        if (sqrMag == 0.0f)
            return Vector4();
        return (dot(vector, onto) / sqrMag) * onto;
    }

    inline float Vector4::angle(const Vector4& a, const Vector4& b)
    {
        float magA = a.magnitude();
        float magB = b.magnitude();
        if (magA == 0.0f || magB == 0.0f)
            return 0.0f;
        float cosTheta = dot(a, b) / (magA * magB);
        cosTheta = std::clamp(cosTheta, -1.0f, 1.0f);
        return std::acos(cosTheta) * (180.0f / DirectX::XM_PI);
    }

    inline Vector4 Vector4::clampMagnitude(float maxLength) const noexcept
    {
        float sqrtMag = sqrMagnitude();
        if (sqrtMag > maxLength * maxLength)
            return normalized() * maxLength;
        return *this;
    }

    inline Vector4 Vector4::max(const Vector4& a, const Vector4& b)
    {
        return Vector4(DirectX::XMVectorMax(a._vec, b._vec));
    }

    inline Vector4 Vector4::min(const Vector4& a, const Vector4& b)
    {
        return Vector4(DirectX::XMVectorMin(a._vec, b._vec));
    }

    inline Vector4 Vector4::moveTowards(const Vector4& current, const Vector4& target, float maxDelta)
    {
        Vector4 toTarget = target - current;
        float dist = toTarget.magnitude();
        if (dist < maxDelta || dist == 0.0f)
            return target;
        return current + toTarget / dist * maxDelta;
    }

    inline Vector4 Vector4::scale(const Vector4& a, const Vector4& b)
    {
        return a.scale(b);
    }

    inline bool Vector4::equal(const Vector4& a) const noexcept
    {
        return DirectX::XMVector4Equal(_vec, a._vec);
    }

    inline bool Vector4::equal(const Vector4& a, const Vector4& b) noexcept
    {
        return a.equal(b);
    }

    inline std::ostream& operator<<(std::ostream& os, const Vector4& v)
    {
        os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
        return os;
    }
}

#endif

