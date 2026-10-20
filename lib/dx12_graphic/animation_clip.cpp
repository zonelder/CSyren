#include "pch.h"
#include "animation_clip.h"
#include "import/model_importer.h"
#include <algorithm>

namespace csyren::render
{
	math::Vector4 AnimationTrack::sample(float t) const
	{
		using namespace math;
		if (values.empty()) return path == assets::AnimationPath::Rotation ? Vector4(0, 0, 0, 1) : Vector4(0, 0, 0, 0);
		if (times.size() != values.size() || times.size() == 1 || t <= times.front()) return values.front();
		if (t >= times.back()) return values.back();
		const auto upper = std::upper_bound(times.begin(), times.end(), t);
		const size_t next = size_t(upper - times.begin());
		const size_t prev = next - 1;
		if (interpolation == assets::AnimationInterpolation::Step) return values[prev];
		const float span = times[next] - times[prev];
		const float alpha = span > 1e-6f ? (t - times[prev]) / span : 0.0f;
		const auto& a = values[prev];
		const auto& b = values[next];
		if (path == assets::AnimationPath::Rotation)
		{
			Quaternion qa(a.x, a.y, a.z, a.w), qb(b.x, b.y, b.z, b.w);
			if (Quaternion::dot(qa, qb) < 0.0f) qb = Quaternion(-qb.x, -qb.y, -qb.z, -qb.w);
			const auto q = Quaternion::slerp(qa, qb, alpha).normalized();
			return Vector4(q.x, q.y, q.z, q.w);
		}
		return Vector4(a.x + (b.x - a.x) * alpha, a.y + (b.y - a.y) * alpha, a.z + (b.z - a.z) * alpha, 0.0f);
	}

	bool AnimationClip::init(const std::string& filepath)
	{
		const auto reference = assets::ModelReference::parse(filepath);
		if (!reference.valid || reference.kind != assets::ModelReference::Kind::Animation)
		{
			log::error("AnimationClip: '{}' is not an animation reference (expected <model>#anim:<name|index>)", filepath);
			return false;
		}
		const auto model = assets::ModelImporterRegistry::instance().load(reference.asset);
		if (!model) return false;
		const auto* animation = assets::resolveAnimation(*model, reference);
		return animation && init(*animation);
	}

	bool AnimationClip::init(const assets::ModelAnimation& animation)
	{
		_name = animation.name;
		_duration = animation.duration;
		_tracks.clear();
		for (const auto& channel : animation.channels)
		{
			if (channel.times.empty() || channel.times.size() != channel.values.size()) continue;
			AnimationTrack track;
			track.node = channel.nodeName; track.path = channel.path; track.interpolation = channel.interpolation;
			track.times = channel.times; track.values = channel.values;
			_tracks.push_back(std::move(track));
		}
		return true;
	}
}
