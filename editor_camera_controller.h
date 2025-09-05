#pragma once
#include "core/serializer.h"


namespace csyren::components
{
	struct EditorCameraController
	{
		float speed = 1.0f;
		float movementSpeed = 0.1f;
	private:
		SERIALIZABLE(EditorCameraController, speed, movementSpeed);
	};
}