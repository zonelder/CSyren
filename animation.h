#pragma once
// Node animation playback. An Animator on an entity drives the Transforms of
// that entity and its descendants whose names match the clip's tracks.
#include <cstdint>
#include "dx12_graphic/resource_handle.h"

namespace csyren
{
    struct Animator
    {
        render::AnimationClipHandle clip;
        float time = 0.0f;
        float speed = 1.0f;
        bool loop = true;
        bool playing = true;
        bool finished = false;        // set when a non-looping clip reaches its end
        // runtime (not reflected)
        uint64_t hierarchyRevision = 0; // when the target lookup was built
        bool bound = false;
        static void describe();
    };

    inline void playAnimation(Animator& animator, render::AnimationClipHandle clip, bool loop, float speed = 1.0f)
    {
        animator.clip = clip; animator.time = 0.0f; animator.speed = speed; animator.loop = loop;
        animator.playing = true; animator.finished = false;
    }
}
