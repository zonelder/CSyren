#ifndef __CSYREN_ANIMATION_CLIP__
#define __CSYREN_ANIMATION_CLIP__
// Node (TRS) animation clip resource. Loaded from a model reference such as
// "models/rifle.glb#anim:reload"; tracks target nodes by name, so a clip
// applies to any entity hierarchy instantiated from the same model.
#include <string>
#include <vector>
#include "forward_decl.h"
#include "math/math.h"
#include "import/model_data.h"

namespace csyren::render
{
	struct AnimationTrack
	{
		std::string node;
		assets::AnimationPath path = assets::AnimationPath::Translation;
		assets::AnimationInterpolation interpolation = assets::AnimationInterpolation::Linear;
		std::vector<float> times;
		std::vector<math::Vector4> values;

		// Samples the track at time t (seconds, clamped to the key range).
		math::Vector4 sample(float t) const;
	};

	class AnimationClip
	{
		friend class ResourceStorage<AnimationClip>;
	public:
		AnimationClip() noexcept = default;
		const std::string& name() const noexcept { return _name; }
		float duration() const noexcept { return _duration; }
		const std::vector<AnimationTrack>& tracks() const noexcept { return _tracks; }

	private:
		bool init(const std::string& filepath);
		bool init(const assets::ModelAnimation& animation);

		std::string _name;
		float _duration = 0.0f;
		std::vector<AnimationTrack> _tracks;
	};
}

#endif
