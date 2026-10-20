#pragma once
// Format-independent description of an imported model: what every importer
// produces and what the resource layer (Mesh / Material / AnimationClip) and
// the scene instantiator consume. Coordinates are already in engine space
// (left-handed, +Y up, +Z forward, meters).
#include <string>
#include <vector>
#include <cstdint>
#include "math/math.h"

namespace csyren::render::assets
{
	struct ModelMaterial
	{
		std::string name;
		math::Vector4 baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		std::string baseColorTexture;   // asset path (relative to the assets mount), empty = none
		float metallic = 1.0f;          // glTF metallicFactor default
		float roughness = 1.0f;         // glTF roughnessFactor default
		math::Vector3 emissive{ 0.0f, 0.0f, 0.0f };
		bool doubleSided = false;
	};

	struct ModelPrimitive
	{
		std::vector<math::Vector3> positions;
		std::vector<math::Vector3> normals;   // same size as positions (generated when missing)
		std::vector<math::Vector2> uvs;       // same size as positions (zero when missing)
		std::vector<math::Vector4> colors;    // optional, empty or same size as positions
		std::vector<uint32_t> indices;        // triangle list
		int material = -1;
	};

	struct ModelMesh
	{
		std::string name;
		std::vector<ModelPrimitive> primitives;
	};

	struct ModelNode
	{
		std::string name;
		int parent = -1;
		std::vector<int> children;
		math::Vector3 translation{ 0.0f, 0.0f, 0.0f };
		math::Quaternion rotation;
		math::Vector3 scale{ 1.0f, 1.0f, 1.0f };
		int mesh = -1;
	};

	enum class AnimationPath : int { Translation = 0, Rotation = 1, Scale = 2 };
	enum class AnimationInterpolation : int { Step = 0, Linear = 1 };

	struct AnimationChannel
	{
		int node = -1;
		std::string nodeName;            // targets are matched by name when instantiated
		AnimationPath path = AnimationPath::Translation;
		AnimationInterpolation interpolation = AnimationInterpolation::Linear;
		std::vector<float> times;        // seconds, ascending
		std::vector<math::Vector4> values; // xyz for translation/scale, xyzw quaternion for rotation
	};

	struct ModelAnimation
	{
		std::string name;
		float duration = 0.0f;
		std::vector<AnimationChannel> channels;
	};

	struct ModelData
	{
		std::string source;              // asset path the model was imported from
		std::vector<ModelMaterial> materials;
		std::vector<ModelMesh> meshes;
		std::vector<ModelNode> nodes;
		std::vector<int> roots;
		std::vector<ModelAnimation> animations;

		int findMesh(const std::string& name) const
		{
			for (size_t i = 0; i < meshes.size(); ++i) if (meshes[i].name == name) return int(i);
			return -1;
		}
		int findMaterial(const std::string& name) const
		{
			for (size_t i = 0; i < materials.size(); ++i) if (materials[i].name == name) return int(i);
			return -1;
		}
		int findAnimation(const std::string& name) const
		{
			for (size_t i = 0; i < animations.size(); ++i) if (animations[i].name == name) return int(i);
			return -1;
		}
		int findNode(const std::string& name) const
		{
			for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].name == name) return int(i);
			return -1;
		}
	};
}
