#pragma once
#include "scene.h"
#include "transform.h"
#include "typed_system.h"
#include "cstdmf/tree_algorithms.h"

namespace csyren::core
{
    // Direct queries are useful while editing a branch: they see current local
    // values immediately, including transaction-local components.
    inline math::Matrix4x4 worldMatrix(Scene& scene, Entity::ID id)
    {
        math::Matrix4x4 result;
        const auto& entities = scene.entities();
        for (auto* entity = entities.try_get(id); entity; entity = entities.try_get(entity->parent))
            if (auto tr = scene.getComponent<const Transform>(entity->id)) result *= tr->localMatrix();
        return result;
    }
    inline math::Quaternion worldRotation(Scene& scene, Entity::ID id)
    {
        math::Quaternion result;
        const auto& entities = scene.entities();
        for (auto* entity = entities.try_get(id); entity; entity = entities.try_get(entity->parent))
            if (auto tr = scene.getComponent<const Transform>(entity->id)) result *= tr->rotation;
        return result;
    }
    inline Entity::ID parentOf(Scene& scene, Entity::ID id)
    {
        const auto* entity = scene.entities().try_get(id);
        return entity ? entity->parent : Entity::invalidID;
    }
    inline bool setWorldPose(Scene& scene, Entity::ID id, const math::Vector3& position,
        const math::Quaternion& rotation, const math::Matrix4x4& parentMatrix, const math::Quaternion& parentRotation)
    {
        auto transform = scene.getComponent<Transform>(id);
        if (!transform) return false;
        math::Matrix4x4 inverseParent;
        const bool identity = DirectX::XMMatrixIsIdentity(parentMatrix);
        if (!identity && !math::Matrix4x4::inverse3DAffine(parentMatrix, inverseParent)) return false;
        const auto localPosition = identity ? position : inverseParent.multiplyPoint3x4(position);
        const auto localRotation = rotation * parentRotation.inverse();
        if (transform->position.exactEqual(localPosition) && transform->rotation.exactEqual(localRotation)) return true;
        return scene.patch(transform, [&](Transform& tr) { tr.position = localPosition; tr.rotation = localRotation; });
    }
    inline bool setWorldPose(Scene& scene, Entity::ID id, const math::Vector3& position, const math::Quaternion& rotation)
    {
        const auto parent = parentOf(scene, id);
        return setWorldPose(scene, id, position, rotation, worldMatrix(scene, parent), worldRotation(scene, parent));
    }

    // The same queries for typed systems (core/typed_system.h): pass any Select or
    // Update that lists Transform; setWorldPose needs Transform in an Update.
    template<class Query> requires (QueryAccess<Query, Transform>::reads)
    math::Matrix4x4 worldMatrix(const Query& query, Entity::ID id)
    {
        math::Matrix4x4 result;
        const auto& entities = query.entities();
        for (auto* entity = entities.try_get(id); entity; entity = entities.try_get(entity->parent))
            if (auto tr = query.template get<const Transform>(entity->id)) result *= tr->localMatrix();
        return result;
    }
    template<class Query> requires (QueryAccess<Query, Transform>::reads)
    math::Quaternion worldRotation(const Query& query, Entity::ID id)
    {
        math::Quaternion result;
        const auto& entities = query.entities();
        for (auto* entity = entities.try_get(id); entity; entity = entities.try_get(entity->parent))
            if (auto tr = query.template get<const Transform>(entity->id)) result *= tr->rotation;
        return result;
    }
    template<class Query> requires (QueryAccess<Query, Transform>::reads)
    Entity::ID parentOf(const Query& query, Entity::ID id)
    {
        const auto* entity = query.entities().try_get(id);
        return entity ? entity->parent : Entity::invalidID;
    }
    template<class Query> requires (QueryAccess<Query, Transform>::writes)
    bool setWorldPose(const Query& query, Entity::ID id, const math::Vector3& position,
        const math::Quaternion& rotation, const math::Matrix4x4& parentMatrix, const math::Quaternion& parentRotation)
    {
        auto transform = query.template get<Transform>(id);
        if (!transform) return false;
        math::Matrix4x4 inverseParent;
        const bool identity = DirectX::XMMatrixIsIdentity(parentMatrix);
        if (!identity && !math::Matrix4x4::inverse3DAffine(parentMatrix, inverseParent)) return false;
        const auto localPosition = identity ? position : inverseParent.multiplyPoint3x4(position);
        const auto localRotation = rotation * parentRotation.inverse();
        if (transform->position.exactEqual(localPosition) && transform->rotation.exactEqual(localRotation)) return true;
        return query.patch(transform, [&](Transform& tr) { tr.position = localPosition; tr.rotation = localRotation; });
    }
    template<class Query> requires (QueryAccess<Query, Transform>::writes)
    bool setWorldPose(const Query& query, Entity::ID id, const math::Vector3& position, const math::Quaternion& rotation)
    {
        const auto parent = parentOf(query, id);
        return setWorldPose(query, id, position, rotation, worldMatrix(query, parent), worldRotation(query, parent));
    }

    // A consumer-owned snapshot. Rebuild once in parent-first order when local
    // transforms or structure change. It contains no renderer/physics state.
    // Call update before reading; a snapshot is never shared between task writers.
    class WorldTransforms
    {
        // The same data through a Scene or through a typed-system query.
        struct SceneSource
        {
            Scene& scene;
            uint64_t identity() const { return scene.identity(); }
            uint64_t hierarchy() const { return scene.hierarchyRevision(); }
            uint64_t transforms() const { return scene.componentRevision<Transform>(); }
            const cstdmf::SparseSet<Entity>& entities() const { return scene.entities(); }
            auto view() const { return scene.view<const Transform>(); }
        };
        template<class Query> struct QuerySource
        {
            const Query& query;
            uint64_t identity() const { return query.sceneIdentity(); }
            uint64_t hierarchy() const { return query.hierarchyRevision(); }
            uint64_t transforms() const { return query.template componentRevision<Transform>(); }
            const cstdmf::SparseSet<Entity>& entities() const { return query.entities(); }
            auto view() const { return query.template select<Transform>(); }
        };
    public:
        bool update(Scene& scene) { return updateFrom(SceneSource{ scene }); }
        // Typed systems: any Select/Update that lists Transform.
        template<class Query> requires (QueryAccess<Query, Transform>::reads)
        bool update(const Query& query) { return updateFrom(QuerySource<Query>{ query }); }

        // Run once in tree order. A visitor may edit existing local values;
        // descendants immediately inherit the updated parent. No structural edits
        // are allowed during propagation (the same borrowing rule as SceneView).
        template<class Before> void propagate(Scene& scene, Before before) { propagateFrom(SceneSource{ scene }, before); }
        template<class Query, class Before> requires (QueryAccess<Query, Transform>::reads)
        void propagate(const Query& query, Before before) { propagateFrom(QuerySource<Query>{ query }, before); }

        const math::Matrix4x4& matrix(Entity::ID id) const { return _worlds[id].matrix; }
        const math::Quaternion& rotation(Entity::ID id) const { return _worlds[id].rotation; }
        const std::vector<Entity::ID>& order() const { return _order; }
    private:
        template<class Source> bool updateFrom(const Source& source)
        {
            if (_scene == source.identity() && _hierarchy == source.hierarchy() && _components == source.transforms()) return false;
            propagateFrom(source, [](auto, const auto&, const auto&) {});
            return true;
        }
        template<class Source, class Before> void propagateFrom(const Source& source, Before before)
        {
            if (_scene != source.identity() || _hierarchy != source.hierarchy()) {
                _worlds.clear(); _order.clear();
                const auto& entities = source.entities();
                _worlds.reserve(entities.size()); _order.reserve(entities.size());
                cstdmf::tree::preorder(EntityManager::ROOT_PARENT,
                    [&](auto id) -> const auto& { return entities.try_get(id)->children; }, [&](auto id) {
                        World value;
                        if (const auto* parent = _worlds.try_get(entities.try_get(id)->parent))
                            value.parent = size_t(parent - _worlds.data());
                        _worlds.emplace(id, value); _order.push_back(id);
                    });
            }
            _locals.assign(_worlds.size(), nullptr);
            // Borrow pool values once instead of constructing generation-checked
            // ComponentRefs (and looking up metadata) for every tree node.
            for (auto [id, transform] : source.view())
                if (auto* world = _worlds.try_get(id)) _locals[size_t(world - _worlds.data())] = &transform;
            const World identity;
            auto* values = _worlds.data();
            for (size_t i = 0; i < _worlds.size(); ++i) {
                auto& world = values[i];
                const auto& parent = world.parent == noParent ? identity : values[world.parent];
                before(_order[i], parent.matrix, parent.rotation);
                if (const auto* transform = _locals[i]) {
                    world.matrix = transform->localMatrix() * parent.matrix;
                    world.rotation = transform->rotation * parent.rotation;
                } else {
                    world.matrix = parent.matrix; world.rotation = parent.rotation;
                }
            }
            _scene = source.identity(); _hierarchy = source.hierarchy();
            _components = source.transforms();
        }
        static constexpr size_t noParent = std::numeric_limits<size_t>::max();
        struct World { math::Matrix4x4 matrix; math::Quaternion rotation; size_t parent = noParent; };
        cstdmf::SparseSet<World> _worlds;
        std::vector<const Transform*> _locals;
        std::vector<Entity::ID> _order;
        uint64_t _scene = 0, _hierarchy = 0, _components = 0;
    };
}
