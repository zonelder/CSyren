#pragma once
#include "core/registration.h"


namespace csyren
{
	struct EditorCameraController
	{
		float speed = 1.0f;
		float movementSpeed = 0.1f;

		static void describe();
	};
	CS_COMPONENT(EditorCameraController);
}
