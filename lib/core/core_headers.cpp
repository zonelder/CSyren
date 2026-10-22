// IntelliSense anchor and self-containment check for header-only code of this project.
// Headers listed here are not included by any other .cpp of core; without a source file
// in their own project Visual Studio parses them in a temporary translation unit (or in
// another project's), which made highlighting unstable. Compiling them here also checks that
// they build with this project's settings. Add new header-only headers of the project here.
#include "pch.h"

#include "core/camera_service.h"
#include "core/frame_graph.h"
#include "core/gpu_execution_plan.h"
#include "core/meta_any.h"
#include "core/meta_json.h"
#include "core/project_paths.h"
#include "core/scene_events_system.h"
#include "core/scene_serialization_context.h"
#include "core/serialize_common.h"
#include "core/system_manager.h"
#include "core/time.h"
#include "core/transform_hierarchy.h"
#include "core/typed_system.h"
#include "core/spawn.h"
