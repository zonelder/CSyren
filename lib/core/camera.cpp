#include "pch.h"
#include "camera.h"
#include "meta.h"


namespace csyren::core
{
	void Camera::describe()
	{
		reflection::MetaFactory<ViewportRect>{}.type("viewportRect"_hs)
			.data< &ViewportRect::x>("x"_hs)
			.data< &ViewportRect::y>("y"_hs)
			.data< &ViewportRect::w>("w"_hs)
			.data< &ViewportRect::h>("h"_hs);

		reflection::MetaFactory<Camera>{}.type("Camera"_hs)
			.data<&Camera::far>("far"_hs)
			.data<&Camera::near>("near"_hs)
			.data<&Camera::fov>("fov"_hs)
			.data<&Camera::aspectRatio>("aspectRatio"_hs)
			.data<&Camera::priority>("priority"_hs)
			.data<&Camera::background>("background"_hs)
			.data<&Camera::viewportRect>("viewportRect"_hs);
	}
}