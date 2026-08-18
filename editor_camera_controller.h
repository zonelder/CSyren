#pragma once
#include "core/serialize_base.h"


namespace csyren
{
	struct EditorCameraController
	{
		float speed = 1.0f;
		float movementSpeed = 0.1f;

		static void describe();
		SERIALIZABLE(EditorCameraController, speed, movementSpeed);
	};
}