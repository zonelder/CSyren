#pragma once
// Advances Animators and writes sampled TRS tracks into the Transforms of the
// animated entity's subtree (targets matched by entity name).
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "core/system_base.h"
#include "core/services.h"
#include "core/scene.h"
#include "core/transform.h"
#include "core/time.h"
#include "cstdmf/tree_algorithms.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/animation_clip.h"
#include "animation.h"
#include "core/registration.h"

namespace csyren
{
    class AnimationSystem : public core::System
    {
    public:
        core::TaskDescription describe() const override
        {
            core::TaskDescription d; d.phase = "simulation"; d.after = { "PhysicsSystem" };
            d.write<Animator>().write<core::Transform>();
            d.readResource<render::ResourceManager>().readResource<core::Time>().readResource<core::SceneEntities>();
            return d;
        }

        void execute() override
        {
            using namespace math;
            const auto* time = core::Services::get<const core::Time>();
            const auto& resources = *core::Services::get<const render::ResourceManager>();
            const float dt = std::clamp(time->deltaTime(), 0.0f, 0.1f);
            auto& scene = this->scene();
            const uint64_t revision = scene.hierarchyRevision();
            std::unordered_set<core::Entity::ID> alive;

            for (auto [ent, animator] : scene.view<Animator>())
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
                    const auto& entities = scene.entities();
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
                    auto transform = scene.getComponent<core::Transform>(target->second);
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
                for (auto id : touched) scene.patch<core::Transform>(id);
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
