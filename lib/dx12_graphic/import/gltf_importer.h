#pragma once
// glTF 2.0 importer (.gltf with external/embedded buffers and .glb).
// Supported: meshes (positions, normals, uv0, color0, any index type), PBR
// base color factor/texture, node hierarchy with TRS, node TRS animations
// (STEP/LINEAR; CUBICSPLINE is sampled as LINEAR). Not supported: skins,
// morph targets, sparse accessors, cameras, lights, embedded images.
#include "model_importer.h"

namespace csyren::render::assets
{
	class GltfImporter final : public IModelImporter
	{
	public:
		std::string_view name() const override { return "glTF"; }
		bool supports(std::string_view extension) const override { return extension == ".gltf" || extension == ".glb"; }
		bool import(const std::filesystem::path& physicalPath, const std::string& assetPath, ModelData& out, std::string& error) override;
	};
}
