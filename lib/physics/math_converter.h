#pragma once
#include "math/math.h"

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>

namespace csyren::physics::details
{
	inline JPH::Vec3 to_jolt(const math::Vector3& v)
	{
		return JPH::Vec3(v.x, v.y, v.z);
	}

	inline JPH::Quat to_jolt(const math::Quaternion& q) 
	{
		return JPH::Quat(q.x, q.y, q.z, q.w);
	}

	inline math::Vector3 from_jolt(const JPH::Vec3& v)
	{
		return math::Vector3(v.GetX(), v.GetY(), v.GetZ());
	}

	inline math::Quaternion from_jolt(const JPH::Quat& q)
	{
		return math::Quaternion(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
	}

	inline JPH::Mat44 to_jolt(const math::Matrix4x4& m)
	{
		JPH::Mat44 out;
		out.SetColumn4(0, JPH::Vec4(m.m[0][0], m.m[1][0], m.m[2][0], m.m[3][0]));
		out.SetColumn4(1, JPH::Vec4(m.m[0][1], m.m[1][1], m.m[2][1], m.m[3][1]));
		out.SetColumn4(2, JPH::Vec4(m.m[0][2], m.m[1][2], m.m[2][2], m.m[3][2]));
		out.SetColumn4(3, JPH::Vec4(m.m[0][3], m.m[1][3], m.m[2][3], m.m[3][3]));
		return out;
	}

	inline math::Matrix4x4 from_jolt(const JPH::Mat44& m)
	{
		math::Matrix4x4 out;
		for (size_t x = 0; x < 4; ++x)
		{
			for (size_t y = 0; y < 4; ++y)
			{
				out.m[x][y] = m(static_cast<JPH::uint>(x), static_cast<JPH::uint>(y));
			}
		}
		return out;
	}

}
