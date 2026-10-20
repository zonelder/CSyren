#pragma once
// Model import entry point. Importers translate a file format into ModelData;
// the registry picks one by extension and caches the result per asset path so
// the mesh, material and animation resources of one file share a single parse.
//
// Resource references into a model use the form  <asset path>#<selector>:
//   models/rifle.glb#0          mesh 0, primitive 0
//   models/rifle.glb#0:2        mesh 0, primitive 2
//   models/rifle.glb#mesh:Body  mesh named "Body", primitive 0 (":2" selects another)
//   models/rifle.glb#mat:1      material 1          models/rifle.glb#mat:Steel
//   models/rifle.glb#anim:0     animation 0         models/rifle.glb#anim:reload
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "model_data.h"

namespace csyren::render::assets
{
	class IModelImporter
	{
	public:
		virtual ~IModelImporter() = default;
		virtual std::string_view name() const = 0;
		// Lower-case extension including the dot, e.g. ".glb".
		virtual bool supports(std::string_view extension) const = 0;
		// assetPath is the logical path (used for texture references), physicalPath the resolved file.
		virtual bool import(const std::filesystem::path& physicalPath, const std::string& assetPath, ModelData& out, std::string& error) = 0;
	};

	struct ModelReference
	{
		enum class Kind { Mesh, Material, Animation };
		std::string asset;      // asset path without the selector
		Kind kind = Kind::Mesh;
		int index = -1;         // mesh/material/animation index when numeric
		std::string name;       // or a name
		int primitive = 0;
		bool valid = false;

		// Parses "<asset>#<selector>". A reference without '#' is mesh 0.
		static ModelReference parse(const std::string& reference);
		static std::string makeMesh(const std::string& asset, int mesh, int primitive = 0);
		static std::string makeMaterial(const std::string& asset, int material);
		static std::string makeAnimation(const std::string& asset, const std::string& animation);
	};

	class ModelImporterRegistry
	{
	public:
		static ModelImporterRegistry& instance();

		void add(std::unique_ptr<IModelImporter> importer);
		bool importable(const std::filesystem::path& path) const;
		std::vector<std::string> extensions() const;

		// Imports (or returns the cached) model for an asset path. Errors are logged.
		std::shared_ptr<const ModelData> load(const std::string& assetPath, std::string* error = nullptr);
		// Drops the cache (for reimport after the file changed).
		void invalidate(const std::string& assetPath = {});

	private:
		ModelImporterRegistry();
		mutable std::mutex _mutex;
		std::vector<std::unique_ptr<IModelImporter>> _importers;
		std::unordered_map<std::string, std::shared_ptr<const ModelData>> _cache;
	};

	// Resolves a model reference; returns nullptr (and logs) when anything is missing.
	const ModelPrimitive* resolvePrimitive(const ModelData& model, const ModelReference& reference, int* meshIndex = nullptr);
	const ModelMaterial* resolveMaterial(const ModelData& model, const ModelReference& reference);
	const ModelAnimation* resolveAnimation(const ModelData& model, const ModelReference& reference);
}
