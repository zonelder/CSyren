#ifndef __MATRIX_H__
#define __MATRIX_H__

#include <DirectXMath.h>
#include <iostream>
#include "quaternion.h"
#include "vector3.h"
#include "vector4.h"


namespace csyren::math
{

	class Matrix4x4 : public DirectX::XMFLOAT4X4
	{

	public:
		Matrix4x4() noexcept
		{
			DirectX::XMStoreFloat4x4(this, DirectX::XMMatrixIdentity());
		}

		Matrix4x4(const DirectX::XMFLOAT4X4& m) noexcept : DirectX::XMFLOAT4X4(m) {}

		explicit Matrix4x4(DirectX::XMMATRIX m) noexcept
		{
			DirectX::XMStoreFloat4x4(this, m);
		}

		operator DirectX::XMMATRIX() const noexcept
		{
			return DirectX::XMLoadFloat4x4(this);
		}

		Quaternion rotation() const noexcept;
		Vector3 scale() const noexcept;
		Matrix4x4 transpose() const noexcept;
		Matrix4x4 inverse() const noexcept;
		Vector3 translation() const noexcept;

		Vector3 forward() const noexcept;
		Vector3 up() const noexcept;
		Vector3 right() const noexcept;

		bool isValidTRS() const noexcept;
		bool isIdentity() const noexcept;
		void setTRS(
			const Vector3& translation,
			const Quaternion& rotationQuat,
			const Vector3& scale) noexcept;


		float determinant() const noexcept;

		Vector3 multiplyPoint(const Vector3& point) const noexcept;
		Vector3 multiplyPoint3x4(const Vector3& point) const noexcept;
		Vector3 multiplyVector(const Vector3& direction) const noexcept;


		Matrix4x4 operator*(const Matrix4x4& other) const noexcept;
		Vector4 operator*(const Vector4& other) const noexcept;
		Matrix4x4& operator*=(const Matrix4x4& other) noexcept;


		bool operator==(const Matrix4x4& other) const noexcept;

		static Matrix4x4 TRS(
			const Vector3& translation,
			const Quaternion& rotationQuat,
			const Vector3& scale) noexcept;

		static Matrix4x4 translate(const Vector3& translation) noexcept;
		static Matrix4x4 rotate(const Quaternion& rotation) noexcept;
		static Matrix4x4 scale(const Vector3& scale) noexcept;

		static Matrix4x4 frustum(float left, float right, float bottom, float top, float zNear, float zFar) noexcept;
		static bool inverse3DAffine(const Matrix4x4& input, Matrix4x4& result) noexcept;
		static Matrix4x4 lookAt(const Vector3& from, const Vector3& to, const Vector3& up) noexcept;
		static Matrix4x4 ortho(float left, float right, float bottom, float top, float zNear, float zFar) noexcept;
		static Matrix4x4 perspective(float fov, float aspect, float zNear, float zFar) noexcept;

		Vector4 getRow(int index) const noexcept;
		void setRow(int index, const Vector4& row) noexcept;
		Vector4 getColumn(int index) const noexcept;
		void setColumn(int index, const Vector4& column) noexcept;

		static const Matrix4x4 identity;
		static const Matrix4x4 zero;
	};

	inline Matrix4x4 Matrix4x4::operator*(const Matrix4x4& other) const noexcept
	{
		return Matrix4x4(DirectX::XMMatrixMultiply(*this, other));
	}

	inline Vector4 Matrix4x4::operator*(const Vector4& other) const noexcept
	{
		return Vector4(DirectX::XMVector4Transform(other, *this));
	}

	inline Matrix4x4& Matrix4x4::operator*=(const Matrix4x4& other) noexcept
	{
		DirectX::XMMATRIX result = DirectX::XMMatrixMultiply(*this, other);
		DirectX::XMStoreFloat4x4(this, result);
		return *this;
	}

	inline bool Matrix4x4::operator==(const Matrix4x4& other) const noexcept
	{
		static const auto eps = DirectX::XMVectorReplicate(0.01f);
		const DirectX::XMMATRIX m1 = *this;
		const DirectX::XMMATRIX m2 = other;
		for (size_t i = 0; i < 4; ++i)
		{
			if (!DirectX::XMVector4NearEqual(m1.r[i], m2.r[i], eps))
				return false;
		}
		return true;
	}

	inline void Matrix4x4::setTRS(const Vector3& translation, const Quaternion& rotation, const Vector3& scale) noexcept
	{
		*this = Matrix4x4(
			DirectX::XMMatrixScalingFromVector(scale) *
			DirectX::XMMatrixRotationQuaternion(rotation) *
			DirectX::XMMatrixTranslationFromVector(translation)
		);
	}

	inline Matrix4x4 Matrix4x4::TRS(const Vector3& translation, const Quaternion& rotation, const Vector3& scale) noexcept
	{
		return Matrix4x4(DirectX::XMMatrixAffineTransformation(
			scale,
			DirectX::XMVectorZero(),
			rotation,
			translation
		));
	}

	inline float Matrix4x4::determinant() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(*this));
	}

	inline Vector3 Matrix4x4::multiplyPoint(const Vector3& point) const noexcept
	{
		DirectX::XMVECTOR v = DirectX::XMVectorSetW(point, 1.0f);
		DirectX::XMVECTOR result = DirectX::XMVector4Transform(v, *this);
		result = DirectX::XMVectorDivide(result, DirectX::XMVectorSplatW(result));
		return Vector3(result);
	}
	inline Vector3 Matrix4x4::multiplyPoint3x4(const Vector3& point) const noexcept
	{
		return Vector3(DirectX::XMVector3Transform(point, *this));
	}
	inline Vector3 Matrix4x4::multiplyVector(const Vector3& direction) const noexcept
	{
		return Vector3(DirectX::XMVector3TransformNormal(direction, *this));
	}

	inline Matrix4x4 Matrix4x4::transpose() const noexcept
	{
		return Matrix4x4(DirectX::XMMatrixTranspose(*this));
	}

	inline Matrix4x4 Matrix4x4::inverse() const noexcept
	{
		return Matrix4x4(DirectX::XMMatrixInverse(nullptr, *this));
	}

	inline Vector3 Matrix4x4::translation() const noexcept
	{
		return Vector3(this->_41, this->_42, this->_43);
	}

	inline Vector3 Matrix4x4::scale() const noexcept
	{
		const DirectX::XMMATRIX m = *this;
		DirectX::XMVECTOR s_x = DirectX::XMVector3Length(m.r[0]);
		DirectX::XMVECTOR s_y = DirectX::XMVector3Length(m.r[1]);
		DirectX::XMVECTOR s_z = DirectX::XMVector3Length(m.r[2]);

		return Vector3(
			DirectX::XMVectorGetX(s_x),
			DirectX::XMVectorGetX(s_y),
			DirectX::XMVectorGetX(s_z)
		);
	}

	inline Matrix4x4 Matrix4x4::translate(const Vector3& translation) noexcept
	{
		return Matrix4x4(DirectX::XMMatrixTranslationFromVector(translation));
	}

	inline Matrix4x4 Matrix4x4::rotate(const Quaternion& q) noexcept
	{
		return Matrix4x4(DirectX::XMMatrixRotationQuaternion(q));
	}

	inline Matrix4x4 Matrix4x4::scale(const Vector3& s) noexcept
	{
		return Matrix4x4(DirectX::XMMatrixScalingFromVector(s));
	}


	inline Matrix4x4 Matrix4x4::perspective(float fov, float aspect, float zNear, float zFar) noexcept
	{
		// Конвертируем градусы в радианы
		float fovRadians = DirectX::XMConvertToRadians(fov);

		// Создаем матрицу для right-handed системы координат
		return Matrix4x4(DirectX::XMMatrixPerspectiveFovLH(
			fovRadians,
			aspect,
			zNear,
			zFar
		));
	}

	inline Matrix4x4 Matrix4x4::frustum(float left, float right, float bottom, float top, float zNear, float zFar) noexcept
	{
		return Matrix4x4(DirectX::XMMatrixPerspectiveOffCenterLH(left, right, bottom, top, zNear, zFar));
	}

	inline bool Matrix4x4::inverse3DAffine(const Matrix4x4& input, Matrix4x4& output) noexcept
	{
		using namespace DirectX;

		XMVECTOR scale, rotationQuat, translation;
		if (!XMMatrixDecompose(&scale, &rotationQuat, &translation, input))
			return false;

		if (XMVector4NearEqual(scale, XMVectorZero(), XMVectorReplicate(1e-6f)))
			return false;

		XMMATRIX invTranslate = XMMatrixTranslationFromVector(XMVectorNegate(translation));
		XMVECTOR invRotation = XMQuaternionInverse(rotationQuat);
		XMMATRIX invScale = XMMatrixScalingFromVector(XMVectorReciprocal(scale));

		output = Matrix4x4(invTranslate * XMMatrixRotationQuaternion(invRotation) * invScale);

		return true;
	}

	inline Matrix4x4 Matrix4x4::ortho(float left, float right, float bottom, float top, float zNear, float zFar) noexcept
	{
		return Matrix4x4(DirectX::XMMatrixOrthographicOffCenterLH(left, right, bottom, top, zNear, zFar));
	}

	inline Matrix4x4 Matrix4x4::lookAt(const Vector3& from, const Vector3& to, const Vector3& up = Vector3::up) noexcept
	{
		return Matrix4x4(DirectX::XMMatrixLookAtLH(from, to, up));
	}

	inline Quaternion Matrix4x4::rotation() const noexcept
	{
		DirectX::XMVECTOR scale, translation, rotation;
		DirectX::XMMatrixDecompose(&scale, &rotation, &translation, *this);
		return Quaternion(rotation);
	}

	inline bool Matrix4x4::isValidTRS() const noexcept
	{
		if (this->_41 != 0.0f || this->_42 != 0.0f || this->_43 != 0.0f || this->_44 != 1.0f)
		{
			return false;
		}

		// Проверяем, что детерминант не равен нулю
		DirectX::XMVECTOR det = DirectX::XMMatrixDeterminant(*this);
		if (DirectX::XMVector4NearEqual(det, DirectX::XMVectorZero(), DirectX::XMVectorReplicate(1e-6f)))
		{
			return false;
		}

		return true;
	}

	inline bool Matrix4x4::isIdentity() const noexcept
	{
		const DirectX::XMMATRIX m = *this;
		const DirectX::XMVECTOR eps = DirectX::XMVectorReplicate(0.001f);
		return DirectX::XMMatrixIsIdentity(m);
	};

	inline Vector3 Matrix4x4::right() const noexcept
	{
		return Vector3(DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(
			reinterpret_cast<const DirectX::XMFLOAT3*>(&this->_11)
		)));
	}

	inline Vector3 Matrix4x4::up() const noexcept
	{
		return Vector3(DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(
			reinterpret_cast<const DirectX::XMFLOAT3*>(&this->_21)
		)));
	}

	inline Vector3 Matrix4x4::forward() const noexcept
	{
		return Vector3(DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(
			reinterpret_cast<const DirectX::XMFLOAT3*>(&this->_31)
		)));
	}

	inline Vector4 Matrix4x4::getRow(int index) const noexcept
	{
		assert(index >= 0 && index < 4);
		return Vector4(DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m[index][0])));
	}

	inline void Matrix4x4::setRow(int index, const Vector4& row) noexcept
	{
		assert(index >= 0 && index < 4);
		DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&m[index][0]), row);
	}

	inline Vector4 Matrix4x4::getColumn(int index) const noexcept
	{
		assert(index >= 0 && index < 4);
		return Vector4(m[0][index], m[1][index], m[2][index], m[3][index]);
	}

	inline void Matrix4x4::setColumn(int index, const Vector4& column) noexcept
	{
		assert(index >= 0 && index < 4);
		m[0][index] = column.x;
		m[1][index] = column.y;
		m[2][index] = column.z;
		m[3][index] = column.w;
	}



	inline std::ostream& operator<<(std::ostream& os, const Matrix4x4& m) 
	{
		os << "[ " << m._11 << ", " << m._12 << ", " << m._13 << ", " << m._14 << " ]\n";
		os << "[ " << m._21 << ", " << m._22 << ", " << m._23 << ", " << m._24 << " ]\n";
		os << "[ " << m._31 << ", " << m._32 << ", " << m._33 << ", " << m._34 << " ]\n";
		os << "[ " << m._41 << ", " << m._42 << ", " << m._43 << ", " << m._44 << " ]\n";
		return os;
	}
}
#endif

