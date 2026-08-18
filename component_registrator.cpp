#include "component_registrator.h"
#include "core/component_registry.h"
#include "core/scene.h"

#include "core/meta.h"

#include "core/transform.h"
#include "core/camera.h"
#include "debug_rotator.h"

#include "physics/colliders.h"
#include "physics/rigid_body.h"

#include "mesh_filter.h"
#include "editor_camera_controller.h"

#include "meta_common.h"

#include "serializer.h"

namespace csyren
{
	void registerAllComponents()
	{
		{
			using namespace csyren;

			REFLECT(MetaCommonDescriber);
			REGISTER_COMPONENT(DebugRotator);
			REGISTER_COMPONENT(EditorCameraController);
			REGISTER_COMPONENT(SceneLoaderRequest);
		}
		{
			using namespace csyren::core;
			REGISTER_COMPONENT(Transform);
		}
		{
			using namespace csyren::core;
			REGISTER_COMPONENT(Camera);
		}

		{
			using namespace csyren::physics;
			REGISTER_COMPONENT(BoxCollider);
			REGISTER_COMPONENT(SphereCollider);;
			REGISTER_COMPONENT(CapsuleCollider);
			REGISTER_COMPONENT(RigidBody);
		}

		{
			using namespace csyren::render;
			REGISTER_COMPONENT(MeshFilter);
			REGISTER_COMPONENT(MeshRenderer);
		}
	}
}