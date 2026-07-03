#pragma once

#include "core/event_bus.h"
#include "core/services.h"
#include "core/system_base.h"
#include "core/scene.h"

#include "debug_rotator.h"
#include "core/transform.h"
#include "math/math.h"
#include "core/time.h"

using namespace csyren::core;
using namespace csyren;

namespace csyren
{
    class CubeShooterSystem : public core::System
    {
    public:
        explicit CubeShooterSystem() = default;

        void init() override
        {
			auto bus = core::Services::get<core::EventBus2>();
			auto scene = core::Services::get<core::Scene>();

			bus->subscribe<core::InputEvent>(static_cast<uint32_t>(core::InputEvent::Type::KeyDown), [scene](core::InputEvent& event)
				{
					using namespace core;
					using namespace physics;
					using namespace render;
					if (event.code != static_cast<int>(core::KeyCode::Space))
					{
						return;
					}
					auto camServ = core::Services::get<core::CameraContextService>();
					auto cameraEntity = camServ->get();
					if (cameraEntity == core::Entity::invalidID)
					{
						log::warning("No main camera in scene!");
						return;
					}

					auto camTr = scene->getComponent<Transform>(cameraEntity);
					if (!camTr)
					{
						log::warning("Camera has no Transform!");
						return;
					}
					auto cubeMat = render::Primitives::getDefaultMaterial();
					auto cubeMesh = render::Primitives::getCube();
					auto world = camTr->world();
					Vector3 spawnOffset = world.forward() * 1.0f;
					Vector3 spawnPos = camTr->position + spawnOffset;
					Vector3 shootDir = world.forward();
					Vector3 velocity = shootDir * 15.0f;

					auto cube = scene->createEntity("cube projectile");
					auto tr = scene->addComponent<Transform>(cube);

					tr->position = spawnPos;
					tr->rotation = camTr->rotation;
					tr->scale = Vector3(0.3f, 0.3f, 0.3f);

					auto collider = scene->addComponent<BoxCollider>(cube);
					collider->size = Vector3(0.3f, 0.3f, 0.3f);

					RigidBody rb;
					rb.type = BodyType::Dynamic;
					rb.mass = 1.0f;
					rb.linearVelocity = velocity;
					rb.useGravity = true;

					scene->addComponent<RigidBody>(cube, rb);

					auto meshRenderer = scene->addComponent<MeshRenderer>(cube);
					meshRenderer->material = cubeMat;
					auto meshFilter = scene->addComponent<MeshFilter>(cube);
					meshFilter->mesh = cubeMesh;

					log::debug("Cube spawned at {}, {}, {}", spawnPos.x, spawnPos.y, spawnPos.z);

				});
        }
    private:
        core::SubscriberToken sub_;
    };

    REGISTER_SYSTEM(CubeShooterSystem);
}

