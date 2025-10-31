#include "pch.h"
#include "physic_system.h"

#include <iostream>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include "core/time.h"
#include "core/scene.h"
#include "core/entity.h"
#include "core/transform.h"

#include "core/event_bus.h"
#include "math/math.h"

#include "rigid_body.h"
#include "colliders.h"
#include "math_converter.h"
#include "default_shapes.h"

namespace
{
    void* sAllocate(size_t inSize) { return malloc(inSize); }
    void sFree(void* inBlock) { free(inBlock); }
    void* sAlignedAllocate(size_t inSize, size_t inAlignment) { return _aligned_malloc(inSize, inAlignment); }
    void sAlignedFree(void* inBlock) { _aligned_free(inBlock); }
}

namespace csyren::physics
{
    namespace Layers
    {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::ObjectLayer COUNT = 2;
    };

    namespace BroadPhaseLayers
    {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr JPH::uint COUNT(2);
    };

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
    {
    public:
        BPLayerInterfaceImpl()
        {
            mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
            mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
        }

        virtual JPH::uint GetNumBroadPhaseLayers() const override
        {
            return BroadPhaseLayers::COUNT;
        }

        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
        {
            JPH_ASSERT(inLayer < Layers::COUNT);
            return mObjectToBroadPhase[inLayer];
        }

        virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
        {
            switch ((JPH::BroadPhaseLayer::Type)inLayer)
            {
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
            default:                                                      return "INVALID";
            }
        }

    private:
        JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::COUNT];
    };


    class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
        {
            switch (inLayer1)
            {
            case Layers::NON_MOVING:
                return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING:
                return true;
            default:
                return false;
            }
        }
    };

    class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
        {
            switch (inObject1)
            {
            case Layers::NON_MOVING:
                return inObject2 == Layers::MOVING;
            case Layers::MOVING:
                return true;
            default:
                return false;
            }
        }
    };

    class PhysicsEngine::Impl
    {
    public:
        core::Scene* scene;
        std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;
        std::unique_ptr<JPH::PhysicsSystem> m_physicsSystem;

        std::unique_ptr<BPLayerInterfaceImpl> m_broadPhaseLayerInterface;
        std::unique_ptr<ObjectVsBroadPhaseLayerFilterImpl> m_objectVsBroadPhaseLayerFilter;
        std::unique_ptr<ObjectLayerPairFilterImpl> m_objectLayerPairFilter;

        // Подписки на события ECS
        core::events::SubscriberToken rbAddedToken;
        core::events::SubscriberToken rbRemovedToken;
        core::events::SubscriberToken boxAddedToken;
        core::events::SubscriberToken boxRemovedToken;
        core::events::SubscriberToken sphereAddedToken;
        core::events::SubscriberToken sphereRemovedToken;
        core::events::SubscriberToken capsuleAddedToken;
        core::events::SubscriberToken capsuleRemovedToken;

        // Entity -> BodyID
        std::unordered_map<core::Entity::ID, JPH::BodyID> entityToBody;
        std::unordered_map<JPH::BodyID, core::Entity::ID> bodyToEntity;
        std::vector<core::Entity::ID> m_activeBodies;

        std::unordered_map<core::Entity::ID, math::Vector3> pendingForces;
        math::Vector3 gravity{ 0.0f,-9.98f,0.0f };
        bool isGravityDirty{ false };

        void initialize(core::ServiceContext& ctx)
        {
            scene = ctx.get<core::Scene>();
            auto bus = ctx.get<core::events::EventBus2>();

            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();

            m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
            m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 4);

            m_broadPhaseLayerInterface = std::make_unique<BPLayerInterfaceImpl>();
            m_objectVsBroadPhaseLayerFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
            m_objectLayerPairFilter = std::make_unique<ObjectLayerPairFilterImpl>();


            m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
            m_physicsSystem->Init(
                1024, 0, 1024, 1024,
                *m_broadPhaseLayerInterface,
                *m_objectVsBroadPhaseLayerFilter,
                *m_objectLayerPairFilter
            );

            // Подписка на RigidBody
            rbAddedToken = bus->subscribe<csyren::core::events::ComponentCreateEvent<RigidBody>>(
                [this](const auto& event) { this->onRigidBodyAdded(event.comp.id()); });

            rbRemovedToken = bus->subscribe<csyren::core::events::ComponentDestroyEvent<RigidBody>>(
                [this](const auto& event) { this->onRigidBodyRemoved(event.comp.id()); });

            // Подписка на BoxCollider
            boxAddedToken = bus->subscribe<csyren::core::events::ComponentCreateEvent<BoxCollider>>(
                [this](const auto& event) { this->onColliderAdded(event.comp.id()); });
            boxRemovedToken = bus->subscribe<csyren::core::events::ComponentDestroyEvent<BoxCollider>>(
                [this](const auto& event) { this->onColliderRemoved(event.comp.id()); });

            // Подписка на SphereCollider
            sphereAddedToken = bus->subscribe<csyren::core::events::ComponentCreateEvent<SphereCollider>>(
                [this](const auto& event) { this->onColliderAdded(event.comp.id()); });
            sphereRemovedToken = bus->subscribe<csyren::core::events::ComponentDestroyEvent<SphereCollider>>(
                [this](const auto& event) { this->onColliderRemoved(event.comp.id()); });

            // Подписка на CapsuleCollider
            capsuleAddedToken = bus->subscribe<csyren::core::events::ComponentCreateEvent<CapsuleCollider>>(
                [this](const auto& event) { this->onColliderAdded(event.comp.id()); });
            capsuleRemovedToken = bus->subscribe<csyren::core::events::ComponentDestroyEvent<CapsuleCollider>>(
                [this](const auto& event) { this->onColliderRemoved(event.comp.id()); });

            m_physicsSystem->SetGravity(details::to_jolt(gravity));
        }

        void onRigidBodyAdded(core::Entity::ID ent) {
            rebuildBody(ent);
        }

        void onRigidBodyRemoved(core::Entity::ID ent) {
            removeBody(ent);
        }

        void onColliderAdded(core::Entity::ID ent) {
            rebuildBody(ent);
        }

        void onColliderRemoved(core::Entity::ID ent) {
            rebuildBody(ent);
        }


        void rebuildBody(core::Entity::ID ent) 
        {
            removeBody(ent);
            if (!scene->hasComponent<RigidBody>(ent))
            {
                log::debug("cant find rb");
                return;
            }
            auto rb = scene->getComponent<RigidBody>(ent);

            std::vector<JPH::Ref<JPH::Shape>> shapes;
            std::vector<JPH::Vec3> offsets;
            std::vector<JPH::Quat> rotations;

            if (auto box = scene->getComponent<BoxCollider>(ent))
            {
                auto shape = details::createBoxShape(*box);
                shapes.push_back(shape);
                offsets.push_back(details::to_jolt(box->offset));
                rotations.push_back(details::to_jolt(box->rotation));
            }

            if (auto sphere = scene->getComponent<SphereCollider>(ent))
            {
                auto shape = details::createSphereShape(*sphere);
                shapes.push_back(shape);
                offsets.push_back(details::to_jolt(sphere->center));
                rotations.push_back(JPH::Quat::sIdentity());
            }

            if (auto cap = scene->getComponent<CapsuleCollider>(ent))
            {
                auto shape = details::createCapsuleShape(*cap);
                shapes.push_back(shape);
                offsets.push_back(details::to_jolt(cap->offset));
                rotations.push_back(details::to_jolt(cap->rotation));
            }

            if (shapes.empty())
                return;

            JPH::Ref<JPH::Shape> finalShape;
            if (shapes.size() == 1)
            {
                finalShape = shapes[0];
            }
            else
            {
                if (rb->type == BodyType::Static)
                {
                    JPH::StaticCompoundShapeSettings compound;
                    for (size_t i = 0; i < shapes.size(); ++i)
                        compound.AddShape(offsets[i], rotations[i], shapes[i].GetPtr(), 0);

                    auto result = compound.Create();
                    finalShape = result.Get();  
                }
                else
                {
                    JPH::MutableCompoundShapeSettings compound;
                    for (size_t i = 0; i < shapes.size(); ++i)
                        compound.AddShape(offsets[i], rotations[i], shapes[i].GetPtr(), 0);
                    auto result = compound.Create();
                    finalShape = result.Get();
                }
            }

            auto transform = scene->getComponent<core::components::Transform>(ent);

            if (!transform) return;

            JPH::BodyCreationSettings settings;
            settings.SetShape(finalShape);
            settings.mMotionType = 
                  rb->type == BodyType::Static  ?   JPH::EMotionType::Static
                : rb->type == BodyType::Dynamic ?   JPH::EMotionType::Dynamic
                :                                   JPH::EMotionType::Kinematic;

            settings.mPosition = details::to_jolt(transform->position);
            settings.mRotation = details::to_jolt(transform->rotation);
            settings.mObjectLayer  = Layers::MOVING;
            settings.mFriction = rb->friction;
            settings.mRestitution = rb->restitution;


            if (rb->type == BodyType::Dynamic)
            {
                JPH::MassProperties massProps = finalShape->GetMassProperties();
                massProps.ScaleToMass(rb->mass);
                settings.mMassPropertiesOverride = massProps;
                settings.mLinearVelocity = details::to_jolt(rb->linearVelocity);
                settings.mAngularVelocity = details::to_jolt(rb->angularVelocity);

            }

            auto& bodyInterface = m_physicsSystem->GetBodyInterface();
            JPH::BodyID body = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);

            entityToBody[ent] = body;
            bodyToEntity[body] = ent;
            m_activeBodies.push_back(ent);
        };

        void removeBody(core::Entity::ID ent) 
        {
            auto it = entityToBody.find(ent);
            if (it == entityToBody.end()) return;

            JPH::BodyID bodyId = it->second;
            auto& bodyInterface = m_physicsSystem->GetBodyInterface();
            bodyInterface.RemoveBody(bodyId);
            bodyInterface.DestroyBody(bodyId);

            bodyToEntity.erase(bodyId);
            entityToBody.erase(it);
            std::erase(m_activeBodies, ent);
        }

        void addForce(core::Entity::ID ent, const math::Vector3& force)
        {
            //no previous checks. expect user not to do shit;
            pendingForces[ent] += force;
        }

        void setGravity(const math::Vector3& g)
        {
            gravity = g;
            isGravityDirty = true;
        }

        void applyPendingForces()
        {
            auto& bodyInterface = m_physicsSystem->GetBodyInterface();
            if (isGravityDirty)
            {
                m_physicsSystem->SetGravity(details::to_jolt(gravity));
                isGravityDirty = false;
            }
            for (const auto& [ent, force] : pendingForces)
            {
                auto it = entityToBody.find(ent);
                if (it == entityToBody.end())
                {
                    log::warning("PhysicEngine::addForce: attempt to add force to unexist body.");
                    continue;
                }
                auto bodyID = it->second;
                bodyInterface.AddForce(bodyID, details::to_jolt(force));
            }
            pendingForces.clear();
        }

        void pushTransform()
        {
            auto& bodyInterface = m_physicsSystem->GetBodyInterface();
            auto view = scene->view<RigidBody, core::components::Transform>();
            for (auto [ent,rb,tr] : view)
            {
                //body is not created yet;
                if (entityToBody.find(ent) == entityToBody.end())
                    continue;

                if (rb.type == BodyType::Static || rb.type == BodyType::Kinematic)
                {
                    auto bodyID = entityToBody[ent];
                    bodyInterface.SetPositionAndRotation(
                        bodyID,
                        details::to_jolt(tr.position),
                        details::to_jolt(tr.rotation),
                        JPH::EActivation::DontActivate
                    );
                }
            }
        }
        void pullTransforms()
        {

            auto view = scene->view<RigidBody, core::components::Transform>();
            for (auto [ent,rb,tr] : view)
            {
                if (entityToBody.find(ent) == entityToBody.end())
                    continue;

                if (rb.type == BodyType::Dynamic)
                {
                    auto bodyID = entityToBody[ent];
                    const JPH::BodyLockRead lock(m_physicsSystem->GetBodyLockInterface(), bodyID);
                    if (!lock.Succeeded()) continue;

                    const JPH::Body& body = lock.GetBody();
                    tr.position = details::from_jolt(body.GetPosition());
                    tr.rotation = details::from_jolt(body.GetRotation());

                    if (rb.type == physics::BodyType::Dynamic)
                    {
                        rb.linearVelocity = details::from_jolt(body.GetLinearVelocity());
                        rb.angularVelocity = details::from_jolt(body.GetAngularVelocity());
                    }
                }
            }
        }
        void shutdown(core::ServiceContext& ctx) 
        {
            auto bus = ctx.get<core::events::EventBus2>();
            bus->unsubscribe(rbAddedToken);
            bus->unsubscribe(rbRemovedToken);
            bus->unsubscribe(boxAddedToken);
            bus->unsubscribe(boxRemovedToken);
            bus->unsubscribe(sphereAddedToken);
            bus->unsubscribe(sphereRemovedToken);
            bus->unsubscribe(capsuleAddedToken);
            bus->unsubscribe(capsuleRemovedToken);

            entityToBody.clear();
            bodyToEntity.clear();
        }
    };


    PhysicsEngine::PhysicsEngine()
        : _pImpl(new Impl())
    {
        csyren::log::debug("PhysicsEngine created.");
    }

    PhysicsEngine::~PhysicsEngine()
    {
        delete _pImpl;
    }

    void PhysicsEngine::addForce(core::Entity::ID ent, const math::Vector3& force)
    {
        _pImpl->addForce(ent, force);
    }

    void PhysicsEngine::setGravity(const math::Vector3& g)
    {
        _pImpl->setGravity(g);
    }

    //-----------------------------------------------------------------------------------------

    void PhysicsSystem::init(core::ServiceContext& ctx)
    {
        auto physic = ctx.get<PhysicsEngine>();
        physic->_pImpl->initialize(ctx);
    }

    void PhysicsSystem::update(core::ServiceContext& ctx)
    {
        auto physic = ctx.get<PhysicsEngine>();
        auto impl = physic->_pImpl;
        auto time = ctx.get<core::Time>();
        auto deltaTime = time->deltaTime();

        impl->pushTransform();
        impl->applyPendingForces();

        impl->m_physicsSystem->Update(deltaTime, 1, impl->m_tempAllocator.get(), impl->m_jobSystem.get());

        impl->pullTransforms();
    }

    void PhysicsSystem::shutdown(core::ServiceContext& ctx)
    {
        auto physic = ctx.get<PhysicsEngine>();
        auto impl = physic->_pImpl;
        impl->shutdown(ctx);
    }

}