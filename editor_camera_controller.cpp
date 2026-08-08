#include "editor_camera_controller.h"
#include "core/meta.h"

namespace csyren
{
	void EditorCameraController::describe()
	{
		core::reflection::MetaFactory<EditorCameraController>{}.type("EditorCameraController"_hs)
			.data< &EditorCameraController::movementSpeed>("move speed"_hs)
			.data<&EditorCameraController::speed>("speed"_hs);
	}

	REFLECT(EditorCameraController)
}