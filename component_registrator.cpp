#include "component_registrator.h"
#include "core/component_registry.h"
#include "core/scene.h"

#include "core/meta.h"

#include "core/transform.h"
#include "core/camera.h"

#include "physics/colliders.h"
#include "physics/rigid_body.h"
#include "physics/character_controller.h"

#include "mesh_filter.h"
#include "dx12_graphic/resource_handle_meta.h"
#include "dx12_graphic/lighting/light_components.h"
#include "editor_camera_controller.h"

#include "meta_common.h"

#include "serializer.h"
#include "animation.h"

namespace csyren
{
	void registerAllComponents()
	{
		{
			using namespace csyren;

			REFLECT(MetaCommonDescriber);
			render::describeResourceHandleMeta();
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
			REGISTER_COMPONENT(SphereCollider);
			REGISTER_COMPONENT(CapsuleCollider);
			REGISTER_COMPONENT(RigidBody);
			REGISTER_COMPONENT(CharacterController);
		}

		{
			using namespace csyren::render;
			REGISTER_COMPONENT(MeshFilter);
			REGISTER_COMPONENT(MeshRenderer);
			REGISTER_COMPONENT(DirectionalLight);
			REGISTER_COMPONENT(PointLight);
			REGISTER_COMPONENT(LightingSettings);
		}
		{
			using namespace csyren;
			REGISTER_COMPONENT(Animator);
		}
	}
}
