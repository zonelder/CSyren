#pragma once

#include "camera.h"

#include <functional>


namespace csyren::core
{
	class CameraContextService
	{
		using CameraEntityGetter = std::function<Entity::ID(void)>;
	public:

		CameraContextService(CameraEntityGetter&& getter) noexcept : _getter(std::move(getter)) {}

		Entity::ID get() const { return _getter(); }

	private:
		CameraEntityGetter _getter;
	};
}
