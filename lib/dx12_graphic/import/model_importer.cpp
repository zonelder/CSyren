#include "model_importer.h"
#include "gltf_importer.h"
#include "cstdmf/file_system.h"
#include "cstdmf/log.h"
#include "core/project_paths.h"
#include <algorithm>
#include <cctype>

namespace csyren::render::assets
{
	namespace
	{
		std::string lowerExtension(const std::filesystem::path& path)
		{
			auto ext = core::pathToUtf8(path.extension());
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
			return ext;
		}
		bool parseInt(const std::string& text, int& out)
		{
			if (text.empty()) return false;
			for (char c : text) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
			out = std::stoi(text); return true;
		}
	}

	ModelReference ModelReference::parse(const std::string& reference)
	{
		ModelReference result;
		const auto hash = reference.find('#');
		result.asset = reference.substr(0, hash);
		if (result.asset.empty()) return result;
		std::string selector = hash == std::string::npos ? std::string() : reference.substr(hash + 1);
		result.valid = true;
		if (selector.empty()) { result.kind = Kind::Mesh; result.index = 0; return result; }
		auto takePrimitive = [&](std::string& text) {
			const auto colon = text.find_last_of(':');
			if (colon == std::string::npos) return;
			int primitive{};
			if (parseInt(text.substr(colon + 1), primitive)) { result.primitive = primitive; text = text.substr(0, colon); }
		};
		if (selector.rfind("mat:", 0) == 0) { result.kind = Kind::Material; selector = selector.substr(4); }
		else if (selector.rfind("anim:", 0) == 0) { result.kind = Kind::Animation; selector = selector.substr(5); }
		else if (selector.rfind("mesh:", 0) == 0) { result.kind = Kind::Mesh; selector = selector.substr(5); takePrimitive(selector); }
		else { result.kind = Kind::Mesh; takePrimitive(selector); }
		if (!parseInt(selector, result.index)) { result.index = -1; result.name = selector; }
		if (result.index < 0 && result.name.empty()) result.valid = false;
		return result;
	}
	std::string ModelReference::makeMesh(const std::string& asset, int mesh, int primitive)
	{
		return primitive > 0 ? asset + "#" + std::to_string(mesh) + ":" + std::to_string(primitive) : asset + "#" + std::to_string(mesh);
	}
	std::string ModelReference::makeMaterial(const std::string& asset, int material) { return asset + "#mat:" + std::to_string(material); }
	std::string ModelReference::makeAnimation(const std::string& asset, const std::string& animation) { return asset + "#anim:" + animation; }

	ModelImporterRegistry& ModelImporterRegistry::instance()
	{
		static ModelImporterRegistry registry;
		return registry;
	}

	ModelImporterRegistry::ModelImporterRegistry()
	{
		_importers.push_back(std::make_unique<GltfImporter>());
	}

	void ModelImporterRegistry::add(std::unique_ptr<IModelImporter> importer)
	{
		if (!importer) return;
		std::lock_guard lock(_mutex);
		log::info("ModelImporterRegistry: importer '{}' registered", importer->name());
		_importers.push_back(std::move(importer));
	}

	bool ModelImporterRegistry::importable(const std::filesystem::path& path) const
	{
		const auto ext = lowerExtension(path);
		std::lock_guard lock(_mutex);
		for (const auto& importer : _importers) if (importer->supports(ext)) return true;
		return false;
	}

	std::vector<std::string> ModelImporterRegistry::extensions() const
	{
		std::vector<std::string> result;
		std::lock_guard lock(_mutex);
		for (const char* ext : { ".glb", ".gltf", ".fbx", ".obj" })
			for (const auto& importer : _importers) if (importer->supports(ext)) { result.emplace_back(ext); break; }
		return result;
	}

	std::shared_ptr<const ModelData> ModelImporterRegistry::load(const std::string& assetPath, std::string* error)
	{
		{
			std::lock_guard lock(_mutex);
			if (auto it = _cache.find(assetPath); it != _cache.end()) return it->second;
		}
		auto fail = [&](const std::string& message) {
			log::error("Model import failed '{}': {}", assetPath, message);
			if (error) *error = message;
			return std::shared_ptr<const ModelData>{};
		};
		const auto physical = cstdmf::FileSystem::instance().resolveRead(core::pathFromUtf8(assetPath));
		if (!physical) return fail("file not found");
		IModelImporter* importer = nullptr;
		const auto ext = lowerExtension(*physical);
		{
			std::lock_guard lock(_mutex);
			for (const auto& candidate : _importers) if (candidate->supports(ext)) { importer = candidate.get(); break; }
		}
		if (!importer) return fail("no importer for extension '" + ext + "'");
		auto model = std::make_shared<ModelData>();
		model->source = assetPath;
		std::string message;
		if (!importer->import(*physical, assetPath, *model, message)) return fail(message.empty() ? "importer error" : message);
		log::info("Model '{}' imported with {}: {} meshes, {} materials, {} nodes, {} animations", assetPath, importer->name(),
			model->meshes.size(), model->materials.size(), model->nodes.size(), model->animations.size());
		std::lock_guard lock(_mutex);
		auto [it, inserted] = _cache.emplace(assetPath, std::move(model));
		return it->second;
	}

	void ModelImporterRegistry::invalidate(const std::string& assetPath)
	{
		std::lock_guard lock(_mutex);
		if (assetPath.empty()) _cache.clear(); else _cache.erase(assetPath);
	}

	const ModelPrimitive* resolvePrimitive(const ModelData& model, const ModelReference& reference, int* meshIndex)
	{
		if (!reference.valid || reference.kind != ModelReference::Kind::Mesh) return nullptr;
		const int index = reference.index >= 0 ? reference.index : model.findMesh(reference.name);
		if (index < 0 || index >= int(model.meshes.size()))
		{
			log::error("Model '{}': mesh '{}' not found", model.source, reference.index >= 0 ? std::to_string(reference.index) : reference.name);
			return nullptr;
		}
		const auto& mesh = model.meshes[index];
		if (reference.primitive < 0 || reference.primitive >= int(mesh.primitives.size()))
		{
			log::error("Model '{}': mesh {} has no primitive {}", model.source, index, reference.primitive);
			return nullptr;
		}
		if (meshIndex) *meshIndex = index;
		return &mesh.primitives[reference.primitive];
	}

	const ModelMaterial* resolveMaterial(const ModelData& model, const ModelReference& reference)
	{
		if (!reference.valid || reference.kind != ModelReference::Kind::Material) return nullptr;
		const int index = reference.index >= 0 ? reference.index : model.findMaterial(reference.name);
		if (index < 0 || index >= int(model.materials.size()))
		{
			log::error("Model '{}': material '{}' not found", model.source, reference.index >= 0 ? std::to_string(reference.index) : reference.name);
			return nullptr;
		}
		return &model.materials[index];
	}

	const ModelAnimation* resolveAnimation(const ModelData& model, const ModelReference& reference)
	{
		if (!reference.valid || reference.kind != ModelReference::Kind::Animation) return nullptr;
		const int index = reference.index >= 0 ? reference.index : model.findAnimation(reference.name);
		if (index < 0 || index >= int(model.animations.size()))
		{
			log::error("Model '{}': animation '{}' not found", model.source, reference.index >= 0 ? std::to_string(reference.index) : reference.name);
			return nullptr;
		}
		return &model.animations[index];
	}
}
