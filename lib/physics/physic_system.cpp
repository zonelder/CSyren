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

//#include "core/time.h"
//#include "core/scene.h"
//#include "core/entity.h"
//#include "core/transform.h"

//#include "core/event_bus.h"

#include "rigid_body.h"

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
       // std::vector<core::Entity::ID> _pendingBodies;
       // core::events::SubscriberToken _rbAddedToken;
    public:

        const float cPhysicsUpdateFrequency = 60.0f; // Симулировать 60 раз в секунду
        float m_timeAccumulator = 0.0f;
        JPH::PhysicsSystem* m_physicsSystem = nullptr;
        JPH::TempAllocator* m_tempAllocator = nullptr;
        JPH::JobSystem* m_jobSystem = nullptr;

        BPLayerInterfaceImpl m_broadPhaseLayerInterface;
        ObjectVsBroadPhaseLayerFilterImpl m_objectVsBroadPhaseLayerFilter;
        ObjectLayerPairFilterImpl m_objectLayerPairFilter;

        Impl() {}

        ~Impl()
        {
        }
        /*
        void onRigidBodyAdded(const core::events::ComponentCreateEvent<RigidBody>& event)
        {

            _pendingBodies.push_back(event.entity);
            log::debug("PhysicsEngine:: Queued entity for physics body creation");
        }
        */
        void initialize(core::ServiceContext& ctx)
        {
            //auto bus = ctx.get<core::events::EventBus2>();
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();

            m_tempAllocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
            m_jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

            m_physicsSystem = new JPH::PhysicsSystem();
            m_physicsSystem->Init(1024, 0, 1024, 1024,
                m_broadPhaseLayerInterface,
                m_objectVsBroadPhaseLayerFilter,
                m_objectLayerPairFilter
            );
            /*
            event.bus.subscribe<csyren::core::events::ComponentCreateEvent<RigidBody>>(
                [this](const auto& event) {
                    this->onRigidBodyAdded(event);
                }
            );
            */
            csyren::log::debug("Jolt Physics System Initialized CORRECTLY.");
        }

        void shutdown(core::ServiceContext& ctx)
        {
            if (!m_physicsSystem) return; // Защита от двойного вызова

            delete m_physicsSystem;
            m_physicsSystem = nullptr;

            delete m_jobSystem;
            m_jobSystem = nullptr;

            delete m_tempAllocator;
            m_tempAllocator = nullptr;

            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;

            csyren::log::debug("Jolt Physics System shutdown.");
        }

        void update(core::ServiceContext& ctx)
        {
            /*
            using namespace core::components;
            auto view = scene.view<RigidBody, Transform>();
            float deltaTime = event.time.fixedDeltaTime;
            if (!_pendingBodies.empty())
            {
                for (core::Entity::ID entt : _pendingBodies)
                {
                    if (!view.contains(entt))
                        continue;

                    auto [rb,transform] = view.get(entt);
                }
            }

            for (auto [entt, rb, transform] : scene.view<RigidBody, Transform>())
            {
                if (rb.type == BodyType::Dynamic && rb.internalID != 0xFFFFFFFF)
                {

                }
            }
            */
        }
    };


    PhysicsEngine::PhysicsEngine()
        : _pImpl(new Impl())
    {
        csyren::log::debug("PhysicsEngine created.");
    }

    PhysicsEngine::~PhysicsEngine()
    {
        delete _pImpl; // Удаляем экземпляр реализации
    }

    void PhysicsEngine::initialize(core::ServiceContext& ctx)
    {
        _pImpl->initialize(ctx);
    }

    void PhysicsEngine::shutdown(core::ServiceContext& ctx)
    {
        _pImpl->shutdown(ctx);
    }


    void PhysicsEngine::update(core::ServiceContext& ctx)
    {
        _pImpl->update(ctx);
    }



}