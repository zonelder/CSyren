#include "pch.h"
#include "physic_system.h"
#include "physics_layers.h"

#include <iostream>
#include <unordered_set>
#include <mutex>
#include <algorithm>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include "core/time.h"
#include "core/scene.h"
#include "core/entity.h"
#include "core/transform.h"
#include "core/transform_hierarchy.h"

#include "core/event_bus.h"
#include "core/services.h"
#include "math/math.h"

#include "rigid_body.h"
#include "colliders.h"
#include "character_controller.h"
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
    core::TaskDescription PhysicsSystem::describe() const {
        core::TaskDescription d; d.phase = "simulation";
        d.write<core::Transform>().write<RigidBody>().write<CharacterController>().writeResource<PhysicsEngine>().readResource<core::Time>();
        d.readResource<PhysicsLayers>(); // collision matrix consulted by the Jolt filters
        d.readResource<core::SceneEntities>();
        return d;
    }

    // Jolt object layer = (PhysicsLayers index << 1) | moving bit. The moving
    // bit comes from the body type and selects the broadphase layer; the index
    // selects the row of the project's collision matrix (PhysicsLayers).
    namespace Layers
    {
        static constexpr JPH::ObjectLayer MovingBit = 1;
        inline JPH::ObjectLayer make(uint32_t index, bool moving) { return static_cast<JPH::ObjectLayer>((index << 1) | (moving ? MovingBit : 0)); }
        inline uint32_t index(JPH::ObjectLayer layer) { return static_cast<uint32_t>(layer >> 1); }
        inline bool moving(JPH::ObjectLayer layer) { return (layer & MovingBit) != 0; }
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
        virtual JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::COUNT; }
        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
        {
            return Layers::moving(inLayer) ? BroadPhaseLayers::MOVING : BroadPhaseLayers::NON_MOVING;
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
    };

    // Static bodies never collide with each other; everything else is decided by the matrix.
    class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
        {
            if (!Layers::moving(inLayer1)) return inLayer2 == BroadPhaseLayers::MOVING;
            return true;
        }
    };

    class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
    {
    public:
        explicit ObjectLayerPairFilterImpl(const PhysicsLayers* layers) : _layers(layers) {}
        virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
        {
            if (!Layers::moving(inObject1) && !Layers::moving(inObject2)) return false;
            return _layers->collides(Layers::index(inObject1), Layers::index(inObject2));
        }
    private:
        const PhysicsLayers* _layers;
    };

    // Accepts object layers whose PhysicsLayers bit is set in the mask.
    class LayerMaskFilter : public JPH::ObjectLayerFilter
    {
    public:
        explicit LayerMaskFilter(uint32_t mask) : _mask(mask) {}
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer) const override { return (_mask & PhysicsLayers::bit(Layers::index(inLayer))) != 0; }
    private:
        uint32_t _mask;
    };

    class ContactRecorder final : public JPH::ContactListener
    {
    public:
        struct Record { JPH::BodyID a, b; JPH::Vec3 point, normal; };
        virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings&) override
        {
            std::lock_guard lock(_mutex);
            JPH::Vec3 point = inManifold.mRelativeContactPointsOn1.empty()
                ? JPH::Vec3(inManifold.mBaseOffset) : JPH::Vec3(inManifold.GetWorldSpaceContactPointOn1(0));
            _records.push_back({ inBody1.GetID(), inBody2.GetID(), point, inManifold.mWorldSpaceNormal });
        }
        std::vector<Record> take()
        {
            std::lock_guard lock(_mutex);
            std::vector<Record> result; result.swap(_records); return result;
        }
    private:
        std::mutex _mutex;
        std::vector<Record> _records;
    };

    namespace
    {
        // Resolves a layer name; unknown names fall back to Default (logged once per name).
        uint32_t layerIndex(const PhysicsLayers& layers, const std::string& name)
        {
            const int index = layers.find(name);
            if (index >= 0) return static_cast<uint32_t>(index);
            static std::unordered_set<std::string> warned;
            if (warned.insert(name).second)
                log::warning("Physics: unknown collision layer '{}', using 'Default' (register it in PhysicsLayers)", name);
            return PhysicsLayers::Default;
        }

        // Capsule standing on its feet: the shape origin is the feet position.
        JPH::Ref<JPH::Shape> makeCharacterShape(float height, float radius)
        {
            const float r = (std::max)(radius, 0.05f);
            const float h = (std::max)(height, 2.0f * r + 0.01f);
            JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(0.5f * h - r, r);
            return new JPH::RotatedTranslatedShape(JPH::Vec3(0.0f, 0.5f * h, 0.0f), JPH::Quat::sIdentity(), capsule);
        }

        bool nearlyEqual(const math::Vector3& a, const math::Vector3& b, float eps = 1e-4f)
        {
            return (a - b).sqrMagnitude() <= eps * eps;
        }
        bool nearlyEqual(const math::Quaternion& a, const math::Quaternion& b, float eps = 1e-4f)
        {
            return std::abs(a.x - b.x) <= eps && std::abs(a.y - b.y) <= eps && std::abs(a.z - b.z) <= eps && std::abs(a.w - b.w) <= eps;
        }
    }

    class PhysicsEngine::Impl
    {
    public:
        core::Scene* scene{};
        core::EventBus2* bus{};
        const PhysicsLayers* layers{};
        core::WorldTransforms worlds;
        std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;
        std::unique_ptr<JPH::PhysicsSystem> m_physicsSystem;

        std::unique_ptr<BPLayerInterfaceImpl> m_broadPhaseLayerInterface;
        std::unique_ptr<ObjectVsBroadPhaseLayerFilterImpl> m_objectVsBroadPhaseLayerFilter;
        std::unique_ptr<ObjectLayerPairFilterImpl> m_objectLayerPairFilter;
        std::unique_ptr<ContactRecorder> m_contactRecorder;

        // Подписки на события ECS
        std::vector<core::SubscriberToken> tokens;

        struct BodyRecord
        {
            JPH::BodyID id;
            BodyType type = BodyType::Static;
            JPH::RVec3 prevPos = JPH::RVec3::sZero(), currPos = JPH::RVec3::sZero();
            JPH::Quat prevRot = JPH::Quat::sIdentity(), currRot = JPH::Quat::sIdentity();
            // Last world pose written to the Transform; an unchanged Transform is not a teleport.
            math::Vector3 writtenPos; math::Quaternion writtenRot; bool written = false;
        };
        struct CharacterRecord
        {
            JPH::Ref<JPH::CharacterVirtual> character;
            float shapeHeight = 0.0f, shapeRadius = 0.0f;
            JPH::RVec3 prevPos = JPH::RVec3::sZero(), currPos = JPH::RVec3::sZero();
            math::Vector3 writtenPos; bool written = false;
        };

        // Entity -> BodyID
        std::unordered_map<core::Entity::ID, BodyRecord> bodies;
        std::unordered_map<JPH::BodyID, core::Entity::ID> bodyToEntity;
        std::unordered_map<core::Entity::ID, CharacterRecord> characters;

        std::unordered_map<core::Entity::ID, math::Vector3> pendingForces;
        std::unordered_map<core::Entity::ID, math::Vector3> pendingImpulses;
        std::vector<ContactInfo> contacts;
        math::Vector3 gravity{ 0.0f,-9.81f,0.0f };
        bool isGravityDirty{ false };
        bool broadPhaseDirty{ false };
        float accumulator{ 0.0f };
        float alpha{ 0.0f };

        void initialize(core::Scene& target, core::EventBus2& events)
        {
            scene = &target;
            bus = &events;

            m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
            m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 4);

            layers = core::Services::get<const PhysicsLayers>();
            m_broadPhaseLayerInterface = std::make_unique<BPLayerInterfaceImpl>();
            m_objectVsBroadPhaseLayerFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
            m_objectLayerPairFilter = std::make_unique<ObjectLayerPairFilterImpl>(layers);
            m_contactRecorder = std::make_unique<ContactRecorder>();


            m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
            m_physicsSystem->Init(
                2048, 0, 2048, 2048,
                *m_broadPhaseLayerInterface,
                *m_objectVsBroadPhaseLayerFilter,
                *m_objectLayerPairFilter
            );
            m_physicsSystem->SetContactListener(m_contactRecorder.get());

            tokens.push_back(bus->subscribe<csyren::core::ComponentCreateEvent<RigidBody>>(
                [this](const auto& event) { rebuildBody(event.comp.id()); }));
            tokens.push_back(bus->subscribe<csyren::core::ComponentDestroyEvent<RigidBody>>(
                [this](const auto& event) { onRigidBodyRemoved(event.comp.id()); }));
            tokens.push_back(bus->subscribe<csyren::core::ComponentCreateEvent<BoxCollider>>(
                [this](const auto& event) { rebuildBody(event.comp.id()); }));
            tokens.push_back(bus->subscribe<csyren::core::ComponentDestroyEvent<BoxCollider>>(
                [this](const auto& event) { rebuildBody(event.comp.id()); }));
            tokens.push_back(bus->subscribe<csyren::core::ComponentCreateEvent<SphereCollider>>(
                [this](const auto& event) { rebuildBody(event.comp.id()); }));
            tokens.push_back(bus->subscribe<csyren::core::ComponentDestroyEvent<SphereCollider>>(
                [this](const auto& event) { rebuildBody(event.comp.id()); }));
            tokens.push_back(bus->subscribe<csyren::core::ComponentCreateEvent<CapsuleCollider>>(
                [this](const auto& event) { rebuildBody(event.comp.id()); }));
            tokens.push_back(bus->subscribe<csyren::core::ComponentDestroyEvent<CapsuleCollider>>(
                [this](const auto& event) { rebuildBody(event.comp.id()); }));

            tokens.push_back(bus->subscribe<csyren::core::ComponentCreateEvent<CharacterController>>(
                [this](const auto& event) { rebuildCharacter(event.comp.id()); }));
            tokens.push_back(bus->subscribe<csyren::core::ComponentDestroyEvent<CharacterController>>(
                [this](const auto& event) {
                    if (scene->hasComponent<CharacterController>(event.comp.id())) rebuildCharacter(event.comp.id());
                    else removeCharacter(event.comp.id());
                }));

            m_physicsSystem->SetGravity(details::to_jolt(gravity));
            tokens.push_back(bus->subscribe<core::ComponentPatchEvent<RigidBody>>(
                [this](auto& event) { if (event.comp == scene->getComponent<RigidBody>(event.comp.id())) rebuildBody(event.comp.id()); }));
            tokens.push_back(bus->subscribe<core::ComponentPatchEvent<BoxCollider>>(
                [this](auto& event) { if (event.comp == scene->getComponent<BoxCollider>(event.comp.id())) rebuildBody(event.comp.id()); }));
            tokens.push_back(bus->subscribe<core::ComponentPatchEvent<SphereCollider>>(
                [this](auto& event) { if (event.comp == scene->getComponent<SphereCollider>(event.comp.id())) rebuildBody(event.comp.id()); }));
            tokens.push_back(bus->subscribe<core::ComponentPatchEvent<CapsuleCollider>>(
                [this](auto& event) { if (event.comp == scene->getComponent<CapsuleCollider>(event.comp.id())) rebuildBody(event.comp.id()); }));
            for (auto [entity, rb] : scene->view<RigidBody>()) rebuildBody(entity);
            for (auto [entity, cc] : scene->view<CharacterController>()) rebuildCharacter(entity);
            m_physicsSystem->OptimizeBroadPhase();
            broadPhaseDirty = false;
            accumulator = 0.0f; alpha = 0.0f;
        }

        void onRigidBodyRemoved(core::Entity::ID ent) {
            // A deferred event can outlive this component's generation.
            if (scene->hasComponent<RigidBody>(ent)) rebuildBody(ent);
            else removeBody(ent);
        }

        void rebuildBody(core::Entity::ID ent)
        {
            removeBody(ent);
            if (!scene->hasComponent<RigidBody>(ent))
            {
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
            const bool singleCentered = shapes.size() == 1 && offsets[0].IsNearZero() && rotations[0].IsClose(JPH::Quat::sIdentity());
            if (singleCentered)
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

            auto transform = scene->getComponent<core::Transform>(ent);

            if (!transform) return;

            JPH::BodyCreationSettings settings;
            settings.SetShape(finalShape);
            settings.mMotionType =
                  rb->type == BodyType::Static  ?   JPH::EMotionType::Static
                : rb->type == BodyType::Dynamic ?   JPH::EMotionType::Dynamic
                :                                   JPH::EMotionType::Kinematic;

            const auto worldPos = core::worldMatrix(*scene, ent).translation();
            const auto worldRot = core::worldRotation(*scene, ent);
            settings.mPosition = details::to_jolt(worldPos);
            settings.mRotation = details::to_jolt(worldRot);
            settings.mObjectLayer  = Layers::make(layerIndex(*layers, rb->layer.name), rb->type != BodyType::Static);
            settings.mFriction = rb->friction;
            settings.mRestitution = rb->restitution;


            if (rb->type == BodyType::Dynamic)
            {
                JPH::MassProperties massProps = finalShape->GetMassProperties();
                massProps.ScaleToMass(rb->mass);
                settings.mMassPropertiesOverride = massProps;
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
                settings.mLinearVelocity = details::to_jolt(rb->linearVelocity);
                settings.mAngularVelocity = details::to_jolt(rb->angularVelocity);
                settings.mGravityFactor = rb->useGravity ? 1.0f : 0.0f;
                if (rb->continuousCollision) settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
            }

            auto& bodyInterface = m_physicsSystem->GetBodyInterface();
            JPH::BodyID body = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);
            if (body.IsInvalid()) throw std::runtime_error("Physics body capacity exhausted");

            BodyRecord record;
            record.id = body; record.type = rb->type;
            record.prevPos = record.currPos = settings.mPosition;
            record.prevRot = record.currRot = settings.mRotation;
            bodies[ent] = record;
            bodyToEntity[body] = ent;
            if (rb->type == BodyType::Static) broadPhaseDirty = true;
        };

        void removeBody(core::Entity::ID ent)
        {
            auto it = bodies.find(ent);
            if (it == bodies.end()) return;

            JPH::BodyID bodyId = it->second.id;
            auto& bodyInterface = m_physicsSystem->GetBodyInterface();
            bodyInterface.RemoveBody(bodyId);
            bodyInterface.DestroyBody(bodyId);

            bodyToEntity.erase(bodyId);
            bodies.erase(it);
        }

        //---------------------------------------- characters ----------------------------------------
        void rebuildCharacter(core::Entity::ID ent)
        {
            removeCharacter(ent);
            auto cc = scene->getComponent<CharacterController>(ent);
            auto transform = scene->getComponent<core::Transform>(ent);
            if (!cc || !transform) return;

            JPH::CharacterVirtualSettings settings;
            settings.mShape = makeCharacterShape(cc->height, cc->radius);
            settings.mMaxSlopeAngle = JPH::DegreesToRadians(cc->maxSlopeDegrees);
            settings.mMass = cc->mass;
            settings.mMaxStrength = 100.0f;
            settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -cc->radius);
            settings.mCharacterPadding = 0.02f;
            settings.mPenetrationRecoverySpeed = 1.0f;
            settings.mPredictiveContactDistance = 0.1f;
            settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
            settings.mEnhancedInternalEdgeRemoval = true;

            const auto worldPos = core::worldMatrix(*scene, ent).translation();
            CharacterRecord record;
            record.character = new JPH::CharacterVirtual(&settings, details::to_jolt(worldPos), JPH::Quat::sIdentity(), 0, m_physicsSystem.get());
            record.shapeHeight = (std::max)(cc->height, 2.0f * cc->radius + 0.01f);
            record.shapeRadius = cc->radius;
            record.prevPos = record.currPos = details::to_jolt(worldPos);
            cc->currentHeight = record.shapeHeight;
            characters[ent] = std::move(record);
        }

        void removeCharacter(core::Entity::ID ent)
        {
            characters.erase(ent);
        }

        void stepCharacter(core::Entity::ID ent, CharacterRecord& record, CharacterController& cc, float dt)
        {
            auto& character = *record.character;
            const auto characterLayer = Layers::make(layerIndex(*layers, cc.layer.name), true);
            const auto broadPhaseFilter = m_physicsSystem->GetDefaultBroadPhaseLayerFilter(characterLayer);
            const auto layerFilter = m_physicsSystem->GetDefaultLayerFilter(characterLayer);

            // Resize the capsule when gameplay asks for a different height.
            const float wantedHeight = (std::max)(cc.height, 2.0f * record.shapeRadius + 0.01f);
            if (std::abs(wantedHeight - record.shapeHeight) > 1e-3f)
            {
                auto shape = makeCharacterShape(wantedHeight, record.shapeRadius);
                if (character.SetShape(shape, 0.05f, broadPhaseFilter, layerFilter, {}, {}, *m_tempAllocator))
                    record.shapeHeight = wantedHeight;
            }
            cc.currentHeight = record.shapeHeight;

            JPH::CharacterVirtual::ExtendedUpdateSettings update;
            update.mStickToFloorStepDown = JPH::Vec3(0.0f, -(std::max)(cc.stickToFloor, 0.0f), 0.0f);
            update.mWalkStairsStepUp = JPH::Vec3(0.0f, (std::max)(cc.stepHeight, 0.0f), 0.0f);

            character.SetLinearVelocity(details::to_jolt(cc.velocity));
            record.prevPos = character.GetPosition();
            character.ExtendedUpdate(dt, details::to_jolt(gravity), update, broadPhaseFilter, layerFilter, {}, {}, *m_tempAllocator);
            record.currPos = character.GetPosition();
            // CharacterVirtual keeps the velocity it was given; derive the
            // post-collision velocity by removing components into contacts.
            JPH::Vec3 effective = character.GetLinearVelocity();
            for (const auto& contact : character.GetActiveContacts())
            {
                if (!contact.mHadCollision) continue;
                const float into = effective.Dot(contact.mContactNormal);
                if (into < 0.0f) effective -= contact.mContactNormal * into;
            }
            cc.velocity = details::from_jolt(effective);

            const auto ground = character.GetGroundState();
            cc.grounded = ground == JPH::CharacterBase::EGroundState::OnGround;
            cc.steepGround = ground == JPH::CharacterBase::EGroundState::OnSteepGround;
            cc.groundNormal = (cc.grounded || cc.steepGround) ? details::from_jolt(character.GetGroundNormal()) : math::Vector3::up;
            cc.groundVelocity = cc.grounded ? details::from_jolt(character.GetGroundVelocity()) : math::Vector3::zero;

            cc.wallContact = false; cc.wallNormal = math::Vector3::zero;
            for (const auto& contact : character.GetActiveContacts())
            {
                if (!contact.mHadCollision) continue;
                const auto normal = contact.mContactNormal;
                if (std::abs(normal.GetY()) < 0.35f)
                {
                    cc.wallContact = true;
                    cc.wallNormal = details::from_jolt(normal.Normalized());
                    break;
                }
            }
        }

        //---------------------------------------- forces ----------------------------------------
        void addForce(core::Entity::ID ent, const math::Vector3& force)
        {
            pendingForces[ent] += force;
        }
        void addImpulse(core::Entity::ID ent, const math::Vector3& impulse)
        {
            pendingImpulses[ent] += impulse;
        }

        void setGravity(const math::Vector3& g)
        {
            gravity = g;
            isGravityDirty = true;
        }

        void applyPendingForces(bool impulses)
        {
            auto& bodyInterface = m_physicsSystem->GetBodyInterface();
            if (isGravityDirty)
            {
                m_physicsSystem->SetGravity(details::to_jolt(gravity));
                isGravityDirty = false;
            }
            for (const auto& [ent, force] : pendingForces)
            {
                auto it = bodies.find(ent);
                if (it == bodies.end()) continue;
                bodyInterface.AddForce(it->second.id, details::to_jolt(force));
            }
            if (impulses)
            {
                for (const auto& [ent, impulse] : pendingImpulses)
                {
                    auto it = bodies.find(ent);
                    if (it == bodies.end()) continue;
                    bodyInterface.AddImpulse(it->second.id, details::to_jolt(impulse));
                }
                pendingImpulses.clear();
            }
        }

        //---------------------------------------- transforms ----------------------------------------
        void pushTransform()
        {
            auto& bodyInterface = m_physicsSystem->GetBodyInterface();
            worlds.update(*scene);
            for (auto [ent, rb, tr] : scene->view<RigidBody, core::Transform>())
            {
                auto found = bodies.find(ent);
                if (found == bodies.end()) continue;
                auto& record = found->second;
                const auto position = worlds.matrix(ent).translation();
                const auto rotation = worlds.rotation(ent);
                // A Transform equal to the pose we wrote last frame is not an edit.
                if (record.written && nearlyEqual(position, record.writtenPos) && nearlyEqual(rotation, record.writtenRot)) continue;
                const auto joltPos = details::to_jolt(position);
                const auto joltRot = details::to_jolt(rotation);
                bodyInterface.SetPositionAndRotationWhenChanged(record.id, joltPos, joltRot,
                    rb.type == BodyType::Dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
                record.prevPos = record.currPos = joltPos;
                record.prevRot = record.currRot = joltRot;
                record.written = false;
            }
            for (auto [ent, cc, tr] : scene->view<CharacterController, core::Transform>())
            {
                auto found = characters.find(ent);
                if (found == characters.end()) continue;
                auto& record = found->second;
                const auto position = worlds.matrix(ent).translation();
                if (record.written && nearlyEqual(position, record.writtenPos)) continue;
                const auto joltPos = details::to_jolt(position);
                record.character->SetPosition(joltPos);
                record.prevPos = record.currPos = joltPos;
                record.written = false;
            }
        }

        void stepBodies(float dt)
        {
            for (auto& [ent, record] : bodies)
            {
                if (record.type != BodyType::Dynamic) continue;
                record.prevPos = record.currPos; record.prevRot = record.currRot;
            }
            m_physicsSystem->Update(dt, 1, m_tempAllocator.get(), m_jobSystem.get());
            const auto& lockInterface = m_physicsSystem->GetBodyLockInterface();
            for (auto& [ent, record] : bodies)
            {
                if (record.type != BodyType::Dynamic) continue;
                const JPH::BodyLockRead lock(lockInterface, record.id);
                if (!lock.Succeeded()) continue;
                record.currPos = lock.GetBody().GetPosition();
                record.currRot = lock.GetBody().GetRotation();
            }
        }

        void pullTransforms()
        {
            const float t = std::clamp(alpha, 0.0f, 1.0f);
            const auto& lockInterface = m_physicsSystem->GetBodyLockInterface();
            // Propagate the new body poses and inherited local transforms in one
            // pass. No ancestor walks or rebuilding the snapshot for each body.
            worlds.propagate(*scene, [&](auto ent, const auto& parentMatrix, const auto& parentRotation) {
                if (auto found = bodies.find(ent); found != bodies.end())
                {
                    auto& record = found->second;
                    auto rb = scene->getComponent<RigidBody>(ent);
                    if (!rb || rb->type != BodyType::Dynamic) return;
                    const auto position = details::from_jolt(record.prevPos + (record.currPos - record.prevPos) * t);
                    const auto rotation = details::from_jolt(record.prevRot.SLERP(record.currRot, t).Normalized());
                    core::setWorldPose(*scene, ent, position, rotation, parentMatrix, parentRotation);
                    record.writtenPos = position; record.writtenRot = rotation; record.written = true;
                    const JPH::BodyLockRead lock(lockInterface, record.id);
                    if (lock.Succeeded())
                    {
                        rb->linearVelocity = details::from_jolt(lock.GetBody().GetLinearVelocity());
                        rb->angularVelocity = details::from_jolt(lock.GetBody().GetAngularVelocity());
                    }
                    return;
                }
                if (auto found = characters.find(ent); found != characters.end())
                {
                    auto& record = found->second;
                    const auto position = details::from_jolt(record.prevPos + (record.currPos - record.prevPos) * t);
                    auto transform = scene->getComponent<core::Transform>(ent);
                    if (!transform) return;
                    math::Vector3 local = position;
                    if (!DirectX::XMMatrixIsIdentity(parentMatrix))
                    {
                        math::Matrix4x4 inverseParent;
                        if (!math::Matrix4x4::inverse3DAffine(parentMatrix, inverseParent)) return;
                        local = inverseParent.multiplyPoint3x4(position);
                    }
                    if (!transform->position.exactEqual(local))
                        scene->patch(transform, [&](core::Transform& tr) { tr.position = local; });
                    record.writtenPos = position; record.written = true;
                }
            });
        }

        void collectContacts()
        {
            for (const auto& record : m_contactRecorder->take())
            {
                auto a = bodyToEntity.find(record.a);
                auto b = bodyToEntity.find(record.b);
                if (a == bodyToEntity.end() || b == bodyToEntity.end()) continue;
                contacts.push_back({ a->second, b->second, details::from_jolt(record.point), details::from_jolt(record.normal) });
            }
        }

        //---------------------------------------- queries ----------------------------------------
        JPH::BodyID bodyOf(core::Entity::ID ent) const
        {
            auto it = bodies.find(ent);
            return it == bodies.end() ? JPH::BodyID() : it->second.id;
        }
        core::Entity::ID entityOf(JPH::BodyID id) const
        {
            auto it = bodyToEntity.find(id);
            return it == bodyToEntity.end() ? core::Entity::invalidID : it->second;
        }

        RaycastHit raycast(const math::Vector3& origin, const math::Vector3& direction, float maxDistance, const QueryFilter& filter) const
        {
            RaycastHit hit;
            if (!m_physicsSystem || maxDistance <= 0.0f) return hit;
            const auto dir = direction.normalized();
            if (dir.sqrMagnitude() < 1e-8f) return hit;
            JPH::RRayCast ray(details::to_jolt(origin), details::to_jolt(dir * maxDistance));
            JPH::RayCastSettings settings;
            settings.mTreatConvexAsSolid = false; // a ray starting inside a body is not a hit
            JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
            LayerMaskFilter layerFilter(filter.layerMask);
            JPH::IgnoreSingleBodyFilter bodyFilter(bodyOf(filter.ignore));
            m_physicsSystem->GetNarrowPhaseQuery().CastRay(ray, settings, collector, {}, layerFilter, bodyFilter);
            if (!collector.HadHit()) return hit;
            hit.hit = true;
            hit.fraction = collector.mHit.mFraction;
            hit.distance = hit.fraction * maxDistance;
            hit.point = origin + dir * hit.distance;
            hit.entity = entityOf(collector.mHit.mBodyID);
            const JPH::BodyLockRead lock(m_physicsSystem->GetBodyLockInterface(), collector.mHit.mBodyID);
            if (lock.Succeeded())
                hit.normal = details::from_jolt(lock.GetBody().GetWorldSpaceSurfaceNormal(collector.mHit.mSubShapeID2, details::to_jolt(hit.point)));
            return hit;
        }

        RaycastHit sphereCast(const math::Vector3& origin, float radius, const math::Vector3& direction, float maxDistance, const QueryFilter& filter) const
        {
            RaycastHit hit;
            if (!m_physicsSystem || maxDistance <= 0.0f) return hit;
            const auto dir = direction.normalized();
            if (dir.sqrMagnitude() < 1e-8f) return hit;
            JPH::Ref<JPH::Shape> sphere = new JPH::SphereShape((std::max)(radius, 0.01f));
            const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(sphere.GetPtr(), JPH::Vec3::sReplicate(1.0f),
                JPH::RMat44::sTranslation(details::to_jolt(origin)), details::to_jolt(dir * maxDistance));
            JPH::ShapeCastSettings settings;
            settings.mReturnDeepestPoint = false;
            JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
            LayerMaskFilter layerFilter(filter.layerMask);
            JPH::IgnoreSingleBodyFilter bodyFilter(bodyOf(filter.ignore));
            m_physicsSystem->GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, layerFilter, bodyFilter);
            if (!collector.HadHit()) return hit;
            hit.hit = true;
            hit.fraction = collector.mHit.mFraction;
            hit.distance = hit.fraction * maxDistance;
            hit.point = details::from_jolt(collector.mHit.mContactPointOn2);
            const auto axis = collector.mHit.mPenetrationAxis;
            hit.normal = axis.IsNearZero() ? -dir : details::from_jolt((-axis).Normalized());
            hit.entity = entityOf(collector.mHit.mBodyID2);
            return hit;
        }

        std::vector<core::Entity::ID> overlapSphere(const math::Vector3& center, float radius, const QueryFilter& filter) const
        {
            std::vector<core::Entity::ID> result;
            if (!m_physicsSystem) return result;
            JPH::Ref<JPH::Shape> sphere = new JPH::SphereShape((std::max)(radius, 0.01f));
            JPH::CollideShapeSettings settings;
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            LayerMaskFilter layerFilter(filter.layerMask);
            JPH::IgnoreSingleBodyFilter bodyFilter(bodyOf(filter.ignore));
            m_physicsSystem->GetNarrowPhaseQuery().CollideShape(sphere.GetPtr(), JPH::Vec3::sReplicate(1.0f),
                JPH::RMat44::sTranslation(details::to_jolt(center)), settings, JPH::RVec3::sZero(), collector, {}, layerFilter, bodyFilter);
            for (const auto& hit : collector.mHits)
            {
                const auto entity = entityOf(hit.mBodyID2);
                if (entity == core::Entity::invalidID) continue;
                if (std::find(result.begin(), result.end(), entity) == result.end()) result.push_back(entity);
            }
            return result;
        }

        //---------------------------------------- frame ----------------------------------------
        void frame(float frameDt, float fixedDt)
        {
            contacts.clear();
            if (broadPhaseDirty) { m_physicsSystem->OptimizeBroadPhase(); broadPhaseDirty = false; }
            pushTransform();

            accumulator += std::clamp(frameDt, 0.0f, 0.1f);
            const float step = std::clamp(fixedDt, 1.0f / 480.0f, 0.1f);
            constexpr int maxSteps = 8;
            int steps = 0;
            while (accumulator >= step && steps < maxSteps)
            {
                for (auto [ent, cc] : scene->view<CharacterController>())
                {
                    auto found = characters.find(ent);
                    if (found != characters.end()) stepCharacter(ent, found->second, cc, step);
                }
                applyPendingForces(steps == 0);
                stepBodies(step);
                accumulator -= step;
                ++steps;
            }
            if (steps == maxSteps) accumulator = 0.0f; // too slow: drop time instead of spiralling
            if (steps > 0) pendingForces.clear();
            alpha = accumulator / step;

            collectContacts();
            pullTransforms();
        }

        void shutdown() noexcept
        {
            if (!bus) return;
            for (auto token : tokens) bus->unsubscribe(token);
            tokens.clear();

            characters.clear();
            while (!bodies.empty()) removeBody(bodies.begin()->first);
            bodyToEntity.clear();
            pendingForces.clear();
            pendingImpulses.clear();
            contacts.clear();
            gravity = math::Vector3{ 0.0f, -9.81f, 0.0f };
            isGravityDirty = false;
            accumulator = 0.0f; alpha = 0.0f;
            if (m_physicsSystem) m_physicsSystem->SetContactListener(nullptr);
            m_physicsSystem.reset();
            m_contactRecorder.reset();
            m_jobSystem.reset();
            m_tempAllocator.reset();
            m_objectLayerPairFilter.reset();
            m_objectVsBroadPhaseLayerFilter.reset();
            m_broadPhaseLayerInterface.reset();
            bus = nullptr; scene = nullptr;
        }
    };


    PhysicsEngine::PhysicsEngine()
        : _pImpl(new Impl())
    {
        csyren::log::debug("PhysicsEngine created.");
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    PhysicsEngine::~PhysicsEngine()
    {
        _pImpl->shutdown();
        delete _pImpl;
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    void PhysicsEngine::addForce(core::Entity::ID ent, const math::Vector3& force)
    {
        _pImpl->addForce(ent, force);
    }

    void PhysicsEngine::addImpulse(core::Entity::ID ent, const math::Vector3& impulse)
    {
        _pImpl->addImpulse(ent, impulse);
    }

    void PhysicsEngine::setGravity(const math::Vector3& g)
    {
        _pImpl->setGravity(g);
    }

    math::Vector3 PhysicsEngine::gravity() const
    {
        return _pImpl->gravity;
    }

    RaycastHit PhysicsEngine::raycast(const math::Vector3& origin, const math::Vector3& direction, float maxDistance, const QueryFilter& filter) const
    {
        return _pImpl->raycast(origin, direction, maxDistance, filter);
    }

    RaycastHit PhysicsEngine::sphereCast(const math::Vector3& origin, float radius, const math::Vector3& direction, float maxDistance, const QueryFilter& filter) const
    {
        return _pImpl->sphereCast(origin, radius, direction, maxDistance, filter);
    }

    std::vector<core::Entity::ID> PhysicsEngine::overlapSphere(const math::Vector3& center, float radius, const QueryFilter& filter) const
    {
        return _pImpl->overlapSphere(center, radius, filter);
    }

    const std::vector<ContactInfo>& PhysicsEngine::contacts() const
    {
        return _pImpl->contacts;
    }

    float PhysicsEngine::interpolationAlpha() const
    {
        return _pImpl->alpha;
    }

    //-----------------------------------------------------------------------------------------

    void csyren::physics::PhysicsSystem::onAttach()
    {
        auto physic = core::Services::get<physics::PhysicsEngine>();
        physic->_pImpl->initialize(scene(), sceneBus());
    }

    void csyren::physics::PhysicsSystem::execute()
    {
        using ctx = core::Services;
        auto physic = ctx::get<PhysicsEngine>();
        auto time = ctx::get<const core::Time>();
        physic->_pImpl->frame(time->deltaTime(), time->fixedDeltaTime);
    }

    void csyren::physics::PhysicsSystem::onDetach() noexcept
    {
        core::Services::get<PhysicsEngine>()->_pImpl->shutdown();
    }

    void PhysicsSystem::init() {}
    void PhysicsSystem::shutdown() {}

}
