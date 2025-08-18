#ifndef __MATRIX_H__
#define __MATRIX_H__

#include <DirectXMath.h>
#include <iostream>
#include "quaternion.h"
#include "vector3.h"
#include "vector4.h"


namespace csyren::math
{

	class Matrix4x4
	{
		alignas(16) DirectX::XMMATRIX _matrix;

	public:
		Matrix4x4() noexcept : _matrix(DirectX::XMMatrixIdentity()) {}
		explicit Matrix4x4(DirectX::XMMATRIX matrix) noexcept : _matrix(matrix) {}

		Quaternion rotation() const noexcept;
		Vector3 scale() const noexcept;
		Matrix4x4 transpose() const noexcept;
		Matrix4x4 inverse() const noexcept;
		Vector3 translation() const noexcept;
		Vector4 getRow(int index)  const noexcept;
		Vector4 getColumn(int index) const noexcept;
		void setRow(int index, const Vector4& row) noexcept;
		void setRow(int index, const Vector3& row) noexcept;
		void setColumn(int index, const Vector4& column) noexcept;

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
		operator DirectX::XMMATRIX() const noexcept { return _matrix; };
		operator DirectX::XMMATRIX() noexcept { return _matrix; };

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

		static const Matrix4x4 identity;
		static const Matrix4x4 zero;
	};

	inline Matrix4x4 Matrix4x4::operator*(const Matrix4x4& other) const noexcept
	{
		return Matrix4x4(DirectX::XMMatrixMultiply(_matrix, other._matrix));
	}

	inline Vector4 Matrix4x4::operator*(const Vector4& other) const noexcept
	{
		return Vector4(DirectX::XMVector4Transform(other, _matrix));
	}

	inline Matrix4x4& Matrix4x4::operator*=(const Matrix4x4& other) noexcept
	{
		_matrix = DirectX::XMMatrixMultiply(_matrix, other._matrix);
		return *this;
	}

	inline bool Matrix4x4::operator==(const Matrix4x4& other) const noexcept
	{
		static const auto eps = DirectX::XMVectorReplicate(0.01f);
		for (size_t i = 0; i < 4; ++i)
		{
			if (!DirectX::XMVector4NearEqual(_matrix.r[i], other._matrix.r[i], eps))
				return false;
		}
		return true;
	}

	inline void Matrix4x4::setTRS(const Vector3& translation, const Quaternion& rotation, const Vector3& scale) noexcept
	{
		_matrix = DirectX::XMMatrixAffineTransformation(
			scale,
			DirectX::XMVectorZero(),
			rotation,
			translation
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
		return DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(_matrix));
	}

	inline Vector3 Matrix4x4::multiplyPoint(const Vector3& point) const noexcept
	{
		DirectX::XMVECTOR v = DirectX::XMVectorSetW(point, 1.0f);
		DirectX::XMVECTOR result = DirectX::XMVector4Transform(v, _matrix);
		result = DirectX::XMVectorDivide(result, DirectX::XMVectorSplatW(result));
		return Vector3(result);
	}
	inline Vector3 Matrix4x4::multiplyPoint3x4(const Vector3& point) const noexcept
	{
		return Vector3(DirectX::XMVector3Transform(point, _matrix));
	}
	inline Vector3 Matrix4x4::multiplyVector(const Vector3& direction) const noexcept
	{
		return Vector3(DirectX::XMVector3TransformNormal(direction, _matrix));
	}

	inline Matrix4x4 Matrix4x4::transpose() const noexcept
	{
		return Matrix4x4(DirectX::XMMatrixTranspose(_matrix));
	}

	inline Matrix4x4 Matrix4x4::inverse() const noexcept
	{
		return Matrix4x4(DirectX::XMMatrixInverse(nullptr, _matrix));
	}

	inline Vector3 Matrix4x4::translation() const noexcept
	{
		const DirectX::XMVECTOR& row3 = _matrix.r[3];
		const DirectX::XMVECTOR wVec = DirectX::XMVectorSplatW(row3);

		const DirectX::XMVECTOR zeroMask = DirectX::XMVectorEqual(wVec, DirectX::XMVectorZero());

		const DirectX::XMVECTOR safeDivided = DirectX::XMVectorSelect(
			DirectX::XMVectorDivide(row3, wVec),
			DirectX::XMVectorZero(),
			zeroMask
		);

		return Vector3(safeDivided);
	}

	inline Vector3 Matrix4x4::scale() const noexcept
	{
		DirectX::XMVECTOR x = DirectX::XMVector3Length(_matrix.r[0]);
		DirectX::XMVECTOR y = DirectX::XMVector3Length(_matrix.r[1]);
		DirectX::XMVECTOR z = DirectX::XMVector3Length(_matrix.r[2]);

		return Vector3(
			DirectX::XMVectorGetX(x),
			DirectX::XMVectorGetY(y),
			DirectX::XMVectorGetZ(z)
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
		if (!XMMatrixDecompose(&scale, &rotationQuat, &translation, input._matrix))
			return false;

		if (XMVector4NearEqual(scale, XMVectorZero(), XMVectorReplicate(1e-6f)))
			return false;

		XMMATRIX invTranslate = XMMatrixTranslationFromVector(XMVectorNegate(translation));
		XMVECTOR invRotation = XMQuaternionInverse(rotationQuat);
		XMMATRIX invScale = XMMatrixScalingFromVector(XMVectorReciprocal(scale));

		output._matrix = invTranslate * XMMatrixRotationQuaternion(invRotation) * invScale;

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
		auto scale = this->scale();
		if (DirectX::XMVector4NearEqual(scale, DirectX::XMVectorZero(),
			DirectX::XMVectorReplicate(0.001f))) {
			return Quaternion::identity;
		}

		DirectX::XMVECTOR v0 = DirectX::XMVectorDivide(_matrix.r[0], scale);
		DirectX::XMVECTOR v1 = DirectX::XMVectorDivide(_matrix.r[1], scale);
		DirectX::XMVECTOR v2 = DirectX::XMVectorDivide(_matrix.r[2], scale);

		DirectX::XMMATRIX rotMatrix = { v0, v1, v2, _matrix.r[3] };

		return Quaternion(DirectX::XMQuaternionRotationMatrix(rotMatrix));
	}

	inline void Matrix4x4::setRow(int index, const Vector4& row) noexcept
	{
		assert(index >= 0 && index < 4);
		_matrix.r[index] = row;
	}

	inline void Matrix4x4::setRow(int index, const Vector3& row) noexcept
	{
		assert(index >= 0 && index < 4);
		_matrix.r[index] = DirectX::XMVectorSet(row[0], row[1], row[2], 0.0f);
	}

	inline Vector4 Matrix4x4::getRow(int index) const noexcept
	{
		assert(index >= 0 && index < 4);
		return Vector4(_matrix.r[index]);
	}

	inline Vector4 Matrix4x4::getColumn(int index) const noexcept {
		assert(index >= 0 && index < 4);
		DirectX::XMVECTOR c = DirectX::XMVectorSet(
			DirectX::XMVectorGetByIndex(_matrix.r[0], index),
			DirectX::XMVectorGetByIndex(_matrix.r[1], index),
			DirectX::XMVectorGetByIndex(_matrix.r[2], index),
			DirectX::XMVectorGetByIndex(_matrix.r[3], index)
		);
		return Vector4(c);
	}

	inline void Matrix4x4::setColumn(int index, const Vector4& column) noexcept {
		assert(index >= 0 && index < 4);
		_matrix.r[0] = DirectX::XMVectorSetByIndex(_matrix.r[0], DirectX::XMVectorGetX(column), index);
		_matrix.r[1] = DirectX::XMVectorSetByIndex(_matrix.r[1], DirectX::XMVectorGetY(column), index);
		_matrix.r[2] = DirectX::XMVectorSetByIndex(_matrix.r[2], DirectX::XMVectorGetZ(column), index);
		_matrix.r[3] = DirectX::XMVectorSetByIndex(_matrix.r[3], DirectX::XMVectorGetW(column), index);
	}


	inline bool Matrix4x4::isValidTRS() const noexcept
	{
		static const DirectX::XMVECTORF32 lastRowCheck = { 0,0,0,1 };
		if (!DirectX::XMVector4Equal(getColumn(3), lastRowCheck))
			return false;

		DirectX::XMVECTOR det = DirectX::XMMatrixDeterminant(_matrix);
		if (DirectX::XMVector4NearEqual(det, DirectX::XMVectorZero(),
			DirectX::XMVectorReplicate(1e-6f)))
			return false;

		return true;

	}

	inline bool Matrix4x4::isIdentity() const noexcept
	{
		const DirectX::XMVECTOR eps = DirectX::XMVectorReplicate(0.001f);
		return
			DirectX::XMVector4NearEqual(_matrix.r[0], DirectX::g_XMIdentityR0, eps) &&
			DirectX::XMVector4NearEqual(_matrix.r[1], DirectX::g_XMIdentityR1, eps) &&
			DirectX::XMVector4NearEqual(_matrix.r[2], DirectX::g_XMIdentityR2, eps) &&
			DirectX::XMVector4NearEqual(_matrix.r[3], DirectX::g_XMIdentityR3, eps);
	};


	inline std::ostream& operator<<(std::ostream& os, const Matrix4x4& m) {
		for (int i = 0; i < 4; ++i)
		{
			Vector4 row = m.getRow(i);
			os << "[ "
				<< row[0] << ", " << row[1] << ", " << row[2] << ", " << row[3]
				<< " ]\n";
		}
		return os;
	}
}
#endif

