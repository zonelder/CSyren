#pragma once

#include "camera.h"
#include "transform.h"

#include <functional>


namespace csyren::core
{
	class CameraContextService
	{
		using CameraEntityGetter = std::function<Entity::ID(void)>;
	public:

		CameraContextService(CameraEntityGetter&& getter) noexcept : _getter(std::move(getter)) {}

        using View = std::pair<const Camera*, const Transform*>;
        void viewProvider(std::function<View()> provider) { _view = std::move(provider); }
        View view() const { return _view ? _view() : View{}; }
		Entity::ID get() const { return _getter(); }

	private:
		CameraEntityGetter _getter;
        std::function<View()> _view;
	};
}
