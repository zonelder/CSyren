#ifndef __SYREN_COLOR_H__
#define __SYREN_COLOR_H__

#include <DirectXMath.h>
#include <cmath>
#include <algorithm>


#undef max
#undef min

namespace csyren::math
{
	class alignas(16) Color
	{
		union
		{
			struct alignas(16) { float r, g, b, a; };
			DirectX::XMVECTOR _vec;
		};
		static constexpr float s_epsilon = 0.001f;
	public:
		Color() noexcept : _vec(DirectX::XMVectorZero()) {};
		Color(float x,float y,float z,float a = 1.0f) noexcept : _vec(DirectX::XMVectorSet(x,y,z,a)){}
		explicit Color(DirectX::XMVECTOR vec) noexcept : _vec(vec) {};

		operator DirectX::XMVECTOR() const noexcept { return _vec; };
	
		bool operator==(const Color& other) const noexcept;
		bool operator!=(const Color& other) const noexcept;



		friend Color operator*(const Color& color,const float scalar)noexcept;
		friend Color operator*(const float scalar, const Color& color) noexcept;
		Color operator/(const float scalar) const noexcept;

		Color operator+(const Color& other) const noexcept;
		Color operator-(const Color& other) const noexcept;
		Color operator*(const Color& other) const noexcept;
		Color operator/(const Color& other) const noexcept;

		Color& operator+=(const Color& other) noexcept;
		Color& operator-=(const Color& other) noexcept;
		Color& operator*=(const float scalar) noexcept;
		Color& operator*=(const Color& scalar) noexcept;
		Color& operator/=(const Color& other) noexcept;
		Color& operator/=(const float scalar) noexcept;


		float maxComponent() const noexcept;
		float minComponent() const noexcept;
		float grayscale() const noexcept;
		Color gamma() const noexcept;
		Color linear() const noexcept;

		static Color lerp(const Color& a, const Color& b, float t) noexcept;
		static Color lerpUnclamped(const Color& a, const Color& b, float t) noexcept;
		static Color HSVToRGB(float H, float S, float V, bool hdr = true) noexcept;
		static void RGBToHSV(const Color& rgbColor, float& H, float& S, float& V) noexcept;

		float operator[](size_t index) const noexcept
		{
			assert(index < 4 && "Color index out of bound.");
			return (&r)[index];
		}

		float& operator[](size_t index) noexcept
		{
			assert(index < 4 && "Color index out of bound.");
			return (&r)[index];
		}


		static const Color black;
		static const Color white;
		static const Color red;
		static const Color green;
		static const Color blue;
		static const Color yellow;
		static const Color cyan;
		static const Color magenta;
		static const Color gray;

	};


	inline bool Color::operator==(const Color& other) const noexcept
	{
		static const auto eps = DirectX::XMVectorReplicate(Color::s_epsilon);
		return DirectX::XMVector4NearEqual(_vec, other._vec, eps);
	}

	inline bool Color::operator!=(const Color& other) const noexcept
	{
		return !(*this == other);
	}

	inline Color operator*(const Color& color, const float scalar) noexcept
	{
		return Color(DirectX::XMVectorScale(color._vec, scalar));
	}

	inline Color operator*(const float scalar, const Color& color ) noexcept
	{
		return Color(DirectX::XMVectorScale(color._vec, scalar));
	}

	inline Color Color::operator/(const float scalar) const noexcept
	{
		assert(scalar != 0.0f && "Vector4 division by zero");
		return Color(DirectX::XMVectorScale(_vec, 1.0f / scalar));

	}

	inline Color Color::operator+(const Color& other) const noexcept
	{
		return Color(DirectX::XMVectorAdd(_vec,other._vec));
	}

	inline Color Color::operator-(const Color& other) const noexcept
	{
		return Color(DirectX::XMVectorSubtract(_vec, other._vec));
	}

	inline Color Color::operator*(const Color& other) const noexcept
	{
		return Color(DirectX::XMVectorMultiply(_vec,other._vec));
	}

	inline Color Color::operator/(const Color& other) const noexcept
	{
		return Color(DirectX::XMVectorDivide(_vec,other._vec));
	}

	inline Color& Color::operator+=(const Color& other) noexcept
	{
		_vec = DirectX::XMVectorAdd(_vec, other._vec);

		return *this;
	}

	inline Color& Color::operator-=(const Color& other) noexcept
	{
		_vec = DirectX::XMVectorSubtract(_vec, other._vec);

		return *this;
	}

	inline Color& Color::operator*=(const float scalar) noexcept
	{
		_vec = DirectX::XMVectorScale(_vec, scalar);
		return *this;
	}

	inline Color& Color::operator*=(const Color& other) noexcept
	{
		_vec = DirectX::XMVectorMultiply(_vec, other._vec);
		return *this;
	}

	inline Color& Color::operator/=(const Color& other) noexcept
	{
		_vec = DirectX::XMVectorDivide(_vec,other._vec);
		return *this;
	}

	inline Color& Color::operator/=(const float scalar) noexcept
	{
		assert(scalar != 0.0f && "Vector4 division by zero");
		_vec = DirectX::XMVectorScale(_vec, 1.0f / scalar);
	}

	inline Color Color::lerp(const Color& a,const Color& b, float t) noexcept 
	{
		return Color(DirectX::XMVectorLerp(a._vec, b._vec, std::clamp(t, 0.0f, 1.0f)));
	}

	inline Color Color::lerpUnclamped(const Color& a,const Color& b, float t) noexcept 
	{
		return Color(DirectX::XMVectorLerp(a._vec, b._vec,t));
	}

	inline Color Color::HSVToRGB(float H, float S, float V, bool hdr) noexcept
	{
	
		DirectX::XMVECTOR hsv = DirectX::XMVectorSet(H, S, V, 1.0f);
		Color rgb(DirectX::XMColorHSVToRGB(hsv));
		if (!hdr) rgb._vec = DirectX::XMVectorClamp(rgb._vec,
			DirectX::XMVectorZero(), DirectX::XMVectorSplatOne());
		return rgb;
	}

	inline void Color::RGBToHSV(const Color& rgbColor, float& H, float& S, float& V) noexcept
	{
		DirectX::XMVECTOR hsv = DirectX::XMColorRGBToHSV(rgbColor._vec);
		H = DirectX::XMVectorGetX(hsv);
		S = DirectX::XMVectorGetY(hsv);
		V = DirectX::XMVectorGetZ(hsv);
	}


	inline Color Color::gamma() const noexcept 
	{
		static const auto pow = DirectX::XMVectorReplicate(1.0f / 2.2f);
		return Color(DirectX::XMVectorPow(_vec, pow));
	}

	inline Color Color::linear() const noexcept 
	{
		static const auto pow = DirectX::XMVectorReplicate(2.2f);
		return Color(DirectX::XMVectorPow(_vec, pow));
	}

	inline float Color::maxComponent() const noexcept
	{
		float x, y, z, w;
		x = DirectX::XMVectorGetX(_vec);
		y = DirectX::XMVectorGetY(_vec);
		z = DirectX::XMVectorGetZ(_vec);
		w = DirectX::XMVectorGetW(_vec);

		return std::max(std::max(x, y),std::max( z, w));
	}
	inline float Color::minComponent() const noexcept
	{
		float x, y, z, w;
		x = DirectX::XMVectorGetX(_vec);
		y = DirectX::XMVectorGetY(_vec);
		z = DirectX::XMVectorGetZ(_vec);
		w = DirectX::XMVectorGetW(_vec);

		return std::min(std::min(x, y), std::min(z, w));
	}

	inline float Color::grayscale() const noexcept
	{
		return 0.299f * r + 0.587f * g + 0.114f * b;
	}
};

#endif

