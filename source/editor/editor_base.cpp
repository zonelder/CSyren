#include "editor_base.h"

namespace csyren::editor
{
	IEditor* EditorRegistry::_defaultEditor = new details::DefaultEditor;
	IEditor* EditorRegistry::_enumEditor = new details::EnumEditor;
	std::unordered_map<core::reflection::MetaType*, IEditor*> EditorRegistry::_metaMap{};
}
