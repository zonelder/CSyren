#include "math_editor.h"
using namespace csyren::math;

namespace csyren::editor
{
    using core::CameraRenderType;
    REGISTER_EDITOR(CameraRenderType, CameraRenderTypeEditor);
	REGISTER_EDITOR(float, FloatEditor);
	REGISTER_EDITOR(int, IntEditor);
	REGISTER_EDITOR(bool, BoolEditor);
	REGISTER_EDITOR(Vector2, Vector2Editor);
	REGISTER_EDITOR(Vector3, Vector3Editor);
	REGISTER_EDITOR(Vector4, Vector4Editor);
	REGISTER_EDITOR(Color, ColorEditor);
	REGISTER_EDITOR(Quaternion, QuaternionEditor);
}
