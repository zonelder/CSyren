// IntelliSense anchor and self-containment check for header-only code of this project.
// Headers listed here are not included by any other .cpp of csyren_host; without a source file
// in their own project Visual Studio parses them in a temporary translation unit (or in
// another project's), which made highlighting unstable. Compiling them here also checks that
// they build with this project's settings. Add new header-only headers of the project here.

#include "editor/editor_camera_controller_system.h"
#include "editor/editor_layout.h"
#include "editor/editor_toolbar_layout.h"
#include "editor/editor_transform_tools.h"
#include "editor/scene_gizmo.h"
#include "editor/scene_hierarchy.h"
#include "editor/scene_picking.h"
#include "editor/scene_viewport.h"
#include "editor/scene_workflow_window.h"
#include "editor/start_render_system.h"
#include "editor/statistic_window.h"
#include "editor/system_window.h"
#include "editor/profiler_window.h"
#include "runtime/animation_system.h"
#include "runtime/forward_render_system.h"
#include "runtime/mesh_render_system.h"
#include "runtime/render_environment_system.h"
#include "runtime/scene_loader.h"
