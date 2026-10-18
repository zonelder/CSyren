#pragma once
#include "scene.h"
#include "transform.h"
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

    // A consumer-owned snapshot. Rebuild once in parent-first order when local
    // transforms or structure change. It contains no renderer/physics state.
    // Call update before reading; a snapshot is never shared between task writers.
    class WorldTransforms
    {
    public:
        bool update(Scene& scene)
        {
            if (_scene == scene.identity() && _hierarchy == scene.hierarchyRevision() &&
                _components == scene.componentRevision<Transform>()) return false;
            propagate(scene, [](auto, const auto&, const auto&) {});
            return true;
        }

        // Run once in tree order. A visitor may edit existing local values;
        // descendants immediately inherit the updated parent. No structural edits
        // are allowed during propagation (the same borrowing rule as SceneView).
        template<class Before> void propagate(Scene& scene, Before before)
        {
            if (_scene != scene.identity() || _hierarchy != scene.hierarchyRevision()) {
                _worlds.clear(); _order.clear();
                const auto& entities = scene.entities();
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
            for (auto [id, transform] : scene.view<const Transform>())
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
            _scene = scene.identity(); _hierarchy = scene.hierarchyRevision();
            _components = scene.componentRevision<Transform>();
        }
        const math::Matrix4x4& matrix(Entity::ID id) const { return _worlds[id].matrix; }
        const math::Quaternion& rotation(Entity::ID id) const { return _worlds[id].rotation; }
        const std::vector<Entity::ID>& order() const { return _order; }
    private:
        static constexpr size_t noParent = std::numeric_limits<size_t>::max();
        struct World { math::Matrix4x4 matrix; math::Quaternion rotation; size_t parent = noParent; };
        cstdmf::SparseSet<World> _worlds;
        std::vector<const Transform*> _locals;
        std::vector<Entity::ID> _order;
        uint64_t _scene = 0, _hierarchy = 0, _components = 0;
    };
}
