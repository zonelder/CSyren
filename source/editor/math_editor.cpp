#include "math_editor.h"
using namespace csyren::math;

namespace csyren::editor::details
{
    // Field editors for the built-in value types. Enums (CS_ENUM) and MaskOf<E>
    // use the generic EnumEditor; project editors use the CS_EDITOR marker.
    void registerBuiltinEditors()
    {
        static StringEditor stringEditor;
        static FloatEditor floatEditor;
        static IntEditor intEditor;
        static BoolEditor boolEditor;
        static Vector2Editor vector2Editor;
        static Vector3Editor vector3Editor;
        static Vector4Editor vector4Editor;
        static ColorEditor colorEditor;
        static QuaternionEditor quaternionEditor;
        using core::reflection::resolve;
        EditorRegistry::add(resolve<std::string>(), &stringEditor);
        EditorRegistry::add(resolve<float>(), &floatEditor);
        EditorRegistry::add(resolve<int>(), &intEditor);
        EditorRegistry::add(resolve<bool>(), &boolEditor);
        EditorRegistry::add(resolve<Vector2>(), &vector2Editor);
        EditorRegistry::add(resolve<Vector3>(), &vector3Editor);
        EditorRegistry::add(resolve<Vector4>(), &vector4Editor);
        EditorRegistry::add(resolve<Color>(), &colorEditor);
        EditorRegistry::add(resolve<Quaternion>(), &quaternionEditor);
    }
}
