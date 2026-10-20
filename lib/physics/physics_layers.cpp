#include "pch.h"
#include "physics_layers.h"
#include "cstdmf/file_system.h"
#include "third_party/json/json.hpp"
#include "core/meta.h"

namespace csyren::physics
{
	void CollisionLayer::describe()
	{
		using namespace core::reflection;
		registerNamedEnum<CollisionLayer>("CollisionLayer", []() -> std::vector<MetaEnumEntry> {
			const auto* layers = PhysicsLayers::active();
			return enumEntries(layers ? layers->names() : std::vector<std::string>{ "Default" });
		});
	}

	bool PhysicsLayers::load(const std::string& assetPath)
	{
		std::string text;
		if (!cstdmf::FileSystem::instance().readFile(assetPath, text))
		{
			log::error("PhysicsLayers: cannot read '{}'", assetPath);
			return false;
		}
		nlohmann::json doc;
		try { doc = nlohmann::json::parse(text); }
		catch (const nlohmann::json::exception& error)
		{
			log::error("PhysicsLayers: invalid JSON in '{}': {}", assetPath, error.what());
			return false;
		}
		if (doc.contains("layers") && doc["layers"].is_array())
			for (const auto& entry : doc["layers"])
				if (entry.is_string()) define(entry.get<std::string>());
		if (doc.contains("ignore") && doc["ignore"].is_array())
		{
			for (const auto& pair : doc["ignore"])
			{
				if (!pair.is_array() || pair.size() != 2 || !pair[0].is_string() || !pair[1].is_string()) continue;
				const auto a = pair[0].get<std::string>(), b = pair[1].get<std::string>();
				if (find(a) < 0 || find(b) < 0)
				{
					log::warning("PhysicsLayers: ignore pair references an unknown layer ({}, {})", a, b);
					continue;
				}
				setCollision(a, b, false);
			}
		}
		log::info("PhysicsLayers: loaded {} layers from '{}'", count(), assetPath);
		return true;
	}
}
