#ifndef __QUATERNION_H__
#define __QUATERNION_H__
#include <DirectXMath.h>
#include <cmath>
#include "vector3.h"
#include <numbers>


namespace csyren::math
{

    class alignas(16) Quaternion
    {
        static const Quaternion s_qepsilon;
        union
        {
            struct alignas(16) { float x, y, z, w; };
            DirectX::XMVECTOR _vec;
        };
    public:
        Quaternion() noexcept :_vec(DirectX::XMQuaternionIdentity()) {}
        Quaternion(float px, float py, float pz, float pw) noexcept : x(px), y(py), z(pz), w(pw) {}
        explicit Quaternion(const DirectX::XMVECTOR& vec) noexcept : _vec(vec) {}
        explicit Quaternion(DirectX::XMVECTOR&& vec) noexcept :_vec(std::move(vec)) {};

        Quaternion(Quaternion&& other) noexcept = default;
        Quaternion& operator=(Quaternion&& other) noexcept = default;

        Quaternion(const Quaternion& other) noexcept = default;
        Quaternion& operator=(const Quaternion& other) noexcept = default;
        Quaternion& operator=(const DirectX::XMVECTOR& vec) noexcept { _vec = vec; return *this; };
        Quaternion& operator=(DirectX::XMVECTOR&& vec) noexcept { _vec = std::move(vec); return *this; };

        operator DirectX::XMVECTOR() const { return _vec; };
        operator DirectX::XMVECTOR() { return _vec; };

        Quaternion operator*(const Quaternion& other) const noexcept;
        Vector3 operator*(const Vector3& point) const noexcept;
        friend bool operator==(const Quaternion& lhs, const Quaternion& rhs) noexcept;

        Quaternion& operator*=(const Quaternion& other) noexcept;

        static Quaternion euler(float x, float y, float z) noexcept;
        static Quaternion euler(const Vector3& angles) noexcept;
        static Quaternion angleAxis(float angle, const Vector3& axis) noexcept;
        static Quaternion lookRotation(const Vector3& forward, const Vector3& upward = Vector3::up) noexcept;
        static Quaternion fromToRotation(const Vector3& fromDirection, const Vector3& toDirection) noexcept;
        static float dot(const Quaternion& q1, const Quaternion& q2) noexcept;
        static float angle(const Quaternion& a, const Quaternion& b) noexcept;
        static Quaternion rotateTowards(const Quaternion& a, const Quaternion& b, float maxDelta) noexcept;

        static Quaternion lerp(const Quaternion& a, const Quaternion& b, float t) noexcept;
        static Quaternion lerpUnclamped(const Quaternion& a, const Quaternion& b, float t)noexcept;

        static Quaternion slerp(const Quaternion& a, const Quaternion& b, float t) noexcept;
        static Quaternion slerpUnclamped(const Quaternion& a, const Quaternion& b, float t) noexcept;
        static bool equal(const Quaternion& q1, const Quaternion& q2) noexcept;


        void invert() noexcept;
        Quaternion inverse() const noexcept;
        Quaternion conjugated() const noexcept;

        Vector3 eulerAngles() const noexcept;
        float angle() const noexcept;
        Vector3 axis() const noexcept;

        float magnitude() const noexcept;
        float sqrMagnitude() const noexcept;
        void normalize() noexcept;
        Quaternion normalized() const noexcept;

        float dot(const Quaternion& other) const noexcept;

        void setFromToRotation(const Vector3& fromDirection, const Vector3& toDirection) noexcept;
        void setLookRotation(const Vector3& forward, const Vector3& upward = Vector3::up) noexcept;
        void toAngleAxis(float angle, const Vector3& axis) noexcept;
        bool equal(const Quaternion& other) const noexcept;


        float operator[](size_t index) const noexcept
        {
            assert(index < 4 && "Quaternion index out of bound.");
            return (&x)[index];
        }

        static const Quaternion identity;

    };


    inline bool operator==(const Quaternion& lhs, const Quaternion& rhs) noexcept
    {
        static const auto eps = DirectX::XMVectorReplicate(Vector3::s_epsilon);
        return DirectX::XMVector4NearEqual(lhs._vec, rhs._vec, eps);
    }


    inline Quaternion Quaternion::operator*(const Quaternion& other) const noexcept
    {
        return Quaternion(DirectX::XMQuaternionMultiply(_vec, other._vec));
    }

    inline Vector3 Quaternion::operator*(const Vector3& point) const noexcept
    {
        return Vector3(DirectX::XMVector3Rotate(point, _vec));
    }

    inline Quaternion& Quaternion::operator*=(const Quaternion& other) noexcept
    {
        _vec = DirectX::XMQuaternionMultiply(_vec, other._vec);
        return *this;
    }

    inline Quaternion Quaternion::euler(float x, float y, float z)noexcept
    {
        DirectX::XMVECTOR angles = DirectX::XMVectorSet(
            DirectX::XMConvertToRadians(x),
            DirectX::XMConvertToRadians(y),
            DirectX::XMConvertToRadians(z),
            0.0f
        );
        return Quaternion(DirectX::XMQuaternionRotationRollPitchYawFromVector(angles));
    }

    inline Quaternion Quaternion::Quaternion::euler(const Vector3& a) noexcept
    {
        DirectX::XMVECTOR angles = DirectX::XMVectorSet(
            DirectX::XMConvertToRadians(a[0]),
            DirectX::XMConvertToRadians(a[1]),
            DirectX::XMConvertToRadians(a[2]),
            0.0f
        );
        return Quaternion(DirectX::XMQuaternionRotationRollPitchYawFromVector(angles));
    }

    inline Quaternion Quaternion::angleAxis(float angle, const Vector3& axis) noexcept
    {
        float radians = DirectX::XMConvertToRadians(angle);
        return Quaternion(DirectX::XMQuaternionRotationAxis(axis, radians));
    }

    inline void Quaternion::toAngleAxis(float angle, const Vector3& axis) noexcept
    {
        float rad = DirectX::XMConvertToRadians(angle);
        _vec = DirectX::XMQuaternionRotationAxis(axis, rad);
    }

    inline Quaternion Quaternion::lookRotation(const Vector3& forward, const Vector3& upward) noexcept
    {
        DirectX::XMMATRIX matrix = DirectX::XMMatrixLookToLH(Vector3::zero, forward, upward);
        return Quaternion(DirectX::XMQuaternionRotationMatrix(matrix));
    }

    inline void Quaternion::setLookRotation(const Vector3& forward, const Vector3& upward) noexcept
    {
        DirectX::XMMATRIX matrix = DirectX::XMMatrixLookToLH(Vector3::zero, forward, upward);
        _vec = DirectX::XMQuaternionRotationMatrix(matrix);
    }

    inline Quaternion Quaternion::inverse() const noexcept
    {
        return Quaternion(DirectX::XMQuaternionInverse(_vec));
    }

    inline void Quaternion::invert() noexcept
    {
        _vec = DirectX::XMQuaternionInverse(_vec);
    }

    inline Quaternion Quaternion::conjugated() const noexcept
    {
        return Quaternion(DirectX::XMQuaternionConjugate(_vec));
    }

    inline float Quaternion::dot(const Quaternion& q) const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMQuaternionDot(_vec, q._vec));
    }

    inline float Quaternion::angle(const Quaternion& q1, const Quaternion& q2) noexcept
    {
        float t = std::clamp(q1.dot(q2), 0.0f, 1.0f);//suppose Quaternion.magnitude is always 1.0f

        return std::acos(t);
    }

    inline float Quaternion::dot(const Quaternion& q1, const Quaternion& q2) noexcept
    {
        return q1.dot(q2);
    }

    inline Quaternion Quaternion::lerp(const Quaternion& a, const Quaternion& b, float t) noexcept
    {
        return Quaternion(DirectX::XMVectorLerp(a._vec, b._vec, std::clamp(t, 0.0f, 1.0f)));
    }
    inline Quaternion Quaternion::lerpUnclamped(const Quaternion& a, const Quaternion& b, float t) noexcept
    {
        return Quaternion(DirectX::XMVectorLerp(a._vec, b._vec, t));
    }

    inline Quaternion Quaternion::slerp(const Quaternion& a, const Quaternion& b, float t) noexcept
    {
        return Quaternion(DirectX::XMQuaternionSlerp(a._vec, b._vec, std::clamp(t, 0.0f, 1.0f)));
    }
    inline Quaternion Quaternion::slerpUnclamped(const Quaternion& a, const Quaternion& b, float t) noexcept
    {
        return Quaternion(DirectX::XMQuaternionSlerp(a._vec, b._vec, t));
    }

    inline float Quaternion::angle() const noexcept
    {
        float rad = 2.0f * std::acos(w);
        return DirectX::XMConvertToDegrees(rad);
    }

    inline Vector3 Quaternion::axis() const noexcept
    {
        float s_sq = 1.0f - w * w;
        if (s_sq < 0.000001f)
            return Vector3::up;
        float s = 1.0f / std::sqrt(s_sq);
        return Vector3(x * s, y * s, z * s);
    }

    inline Vector3 Quaternion::eulerAngles() const noexcept
    {
        using namespace DirectX;
        XMMATRIX rotMatrix = XMMatrixRotationQuaternion(_vec);
        XMFLOAT3X3 mat;
        XMStoreFloat3x3(&mat, rotMatrix);

        float m20 = mat._31;

        float pitchY = asin(m20);
        float rollX = 0.0f, yawZ = 0.0f;

        if (fabs(m20) < 0.9999999f) { // gimbal lock check
            rollX = atan2(-mat._32, mat._33); // -m[2][1], m[2][2]
            yawZ = atan2(-mat._21, mat._11); // -m[1][0], m[0][0]
        }
        else
        {
            yawZ = 0.0f;
            rollX = atan2(mat._12, mat._22); // m[0][1], m[1][1]
        }

        // Конвертируем радианы в градусы
        rollX = XMConvertToDegrees(rollX);
        pitchY = XMConvertToDegrees(pitchY);
        yawZ = XMConvertToDegrees(yawZ);

        return Vector3(rollX, pitchY, yawZ);
    }


    inline Quaternion Quaternion::fromToRotation(const Vector3& fromDir, const Vector3& toDir) noexcept
    {
        auto v0 = fromDir.normalized();
        auto v1 = toDir.normalized();
        const float cos_theta = v0.dot(v1);
        const auto axis = fromDir.cross(toDir);

        if (cos_theta > 0.9995f)
        {
            return Quaternion();
        }

        if (cos_theta < -0.9995f)
        {
            return Quaternion(DirectX::XMQuaternionRotationAxis(DirectX::XMVector3Orthogonal(v0), DirectX::XM_PI));
        }

        const float angle = std::acos(cos_theta);
        return Quaternion(DirectX::XMQuaternionRotationNormal(axis.normalized(), angle));
    }


    inline void Quaternion::setFromToRotation(const Vector3& fromDir, const Vector3& toDir) noexcept
    {
        auto v0 = fromDir.normalized();
        auto v1 = toDir.normalized();
        const float cos_theta = v0.dot(v1);
        const auto axis = fromDir.cross(toDir);

        if (cos_theta > 0.9995f)
        {
            _vec = identity._vec;
        }

        if (cos_theta < -0.9995f)
        {
            _vec = DirectX::XMQuaternionRotationAxis(DirectX::XMVector3Orthogonal(v0), DirectX::XM_PI);
            return;
        }

        const float angle = std::acos(cos_theta);
        _vec = DirectX::XMQuaternionRotationNormal(axis.normalized(), angle);
        return;
    }

    inline Quaternion Quaternion::rotateTowards(const Quaternion& from, const Quaternion& to, float maxDegreeDelta) noexcept
    {
        const float angle = Quaternion::angle(from, to);
        if (angle <= maxDegreeDelta || angle == 0.0f)
            return to;
        return Quaternion::slerp(from, to, maxDegreeDelta / angle);
    }

    inline float Quaternion::magnitude() const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMQuaternionLength(_vec));
    }

    inline float Quaternion::sqrMagnitude() const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(_vec));
    }

    inline void Quaternion::normalize() noexcept
    {
        _vec = DirectX::XMQuaternionNormalize(_vec);
    }

    inline Quaternion Quaternion::normalized() const noexcept
    {
        return Quaternion(DirectX::XMQuaternionNormalize(_vec));
    }


    inline bool Quaternion::equal(const Quaternion& other) const noexcept
    {
        return DirectX::XMVector4Equal(_vec, other._vec);
    }

    inline bool Quaternion::equal(const Quaternion& lhs, const Quaternion& rhs) noexcept
    {
        return lhs.equal(rhs);
    }
}
#endif