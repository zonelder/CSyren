#include "math_editor.h"
using namespace csyren::math;

namespace csyren::editor
{
	REGISTER_EDITOR(Vector2, Vector2Editor);
	REGISTER_EDITOR(Vector3, Vector3Editor);
	REGISTER_EDITOR(Vector4, Vector4Editor);
	REGISTER_EDITOR(Color, ColorEditor);
	REGISTER_EDITOR(Quaternion, QuaternionEditor);
}