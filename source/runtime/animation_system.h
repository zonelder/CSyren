#pragma once
// Advances Animators and writes sampled TRS tracks into the Transforms of the
// animated entity's subtree (targets matched by entity name).
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "core/typed_system.h"
#include "core/transform.h"
#include "core/time.h"
#include "cstdmf/tree_algorithms.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/animation_clip.h"
#include "animation.h"
#include "core/registration.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren
{
    class AnimationSystem : public core::TypedSystem<AnimationSystem>
    {
    public:
        static constexpr auto phase = "simulation";
        static constexpr std::array after{ "PhysicsSystem" };

        // Animated targets are any entities with a Transform in the animator's subtree.
        void execute(core::Update<Animator> animators, core::Update<core::Transform> transforms,
            core::Res<const render::ResourceManager> resourceManager, core::Res<const core::Time> time)
        {
            using namespace math;
            const auto& resources = *resourceManager;
            const float dt = std::clamp(time->deltaTime(), 0.0f, 0.1f);
            const uint64_t revision = animators.hierarchyRevision();
            std::unordered_set<core::Entity::ID> alive;

            for (auto [ent, animator] : animators)
            {
                alive.insert(ent);
                const auto* clip = resources.getAnimationClip(animator.clip);
                if (!clip) { animator.bound = false; continue; }
                if (animator.playing)
                {
                    animator.time += dt * animator.speed;
                    const float duration = (std::max)(clip->duration(), 1e-4f);
                    if (animator.loop)
                    {
                        animator.time = std::fmod(animator.time, duration);
                        if (animator.time < 0.0f) animator.time += duration;
                    }
                    else if (animator.time >= duration) { animator.time = duration; animator.playing = false; animator.finished = true; }
                    else if (animator.time < 0.0f) { animator.time = 0.0f; animator.playing = false; animator.finished = true; }
                }
                auto& targets = _targets[ent];
                if (!animator.bound || animator.hierarchyRevision != revision)
                {
                    targets.clear();
                    const auto& entities = animators.entities();
                    cstdmf::tree::preorder(ent, [&](auto id) -> const auto& { return entities.try_get(id)->children; }, [&](auto id) {
                        if (const auto* entity = entities.try_get(id)) targets.emplace(entity->name, id);
                    });
                    animator.bound = true; animator.hierarchyRevision = revision;
                }
                std::unordered_set<core::Entity::ID> touched;
                for (const auto& track : clip->tracks())
                {
                    auto target = targets.find(track.node);
                    if (target == targets.end()) continue;
                    auto transform = transforms.get<core::Transform>(target->second);
                    if (!transform) continue;
                    const auto value = track.sample(animator.time);
                    switch (track.path)
                    {
                    case render::assets::AnimationPath::Translation: transform->position = Vector3(value.x, value.y, value.z); break;
                    case render::assets::AnimationPath::Rotation: transform->rotation = Quaternion(value.x, value.y, value.z, value.w); break;
                    case render::assets::AnimationPath::Scale: transform->scale = Vector3(value.x, value.y, value.z); break;
                    }
                    touched.insert(target->second);
                }
                for (auto id : touched) transforms.patch<core::Transform>(id);
            }
            std::erase_if(_targets, [&](const auto& entry) { return !alive.contains(entry.first); });
        }

    protected:
        void onDetach() noexcept override { _targets.clear(); }

    private:
        std::unordered_map<core::Entity::ID, std::unordered_map<std::string, core::Entity::ID>> _targets;
    };

    CS_SYSTEM(AnimationSystem);
}
CS_STRICT_SYSTEMS_END
