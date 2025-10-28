#pragma once
#include "core/serialize_common.h"
#include "resource_manager.h"

namespace csyren::render
{
	struct SerializationServices
	{
		render::ResourceManager* resourceManager{ nullptr };
		nlohmann::json* rootJson = nullptr;

		static SerializationServices& get()
		{
			static SerializationServices inst;
			return inst;
		}

		static render::ResourceManager& getResourceManager()
		{
			auto* rm = get().resourceManager;
			assert(rm != nullptr && "ResourceManager service is not available.");
			return *rm;
		}

		static nlohmann::json& getRootJson()
		{
			auto* rj = get().rootJson;
			assert(rj != nullptr && "RootJson service is not available.");
			return *rj;
		}

	private:
		SerializationServices() = default;
	};
}


namespace nlohmann
{

	//--------------------------------TextureHandle-------------------------------------------
	template<>
	struct adl_serializer<csyren::render::TextureHandle>
	{
		static void to_json(json& j, const csyren::render::TextureHandle& value)
		{

			auto& rm = csyren::render::SerializationServices::getResourceManager();
			auto& root = csyren::render::SerializationServices::getRootJson();
			std::string resourcePath = rm.getTextureName(value);

			if (!root.contains("resources"))
			{
				root["resources"] = json::array();
			}

			for (const auto& res : root["resources"])
			{
				if (resourcePath == res.value("path", std::string()))
				{
					j = res["id"];
					return;
				}
			}

			size_t newId = root["resources"].size();
			json newRes;
			newRes["id"] = newId;
			newRes["type"] = "texture";
			newRes["path"] = resourcePath;
			root["resources"].push_back(newRes);

			j = newId;
		}

		static void from_json(const json& j, csyren::render::TextureHandle& ctx)
		{
			size_t resourceID;
			j.get_to(resourceID);

			auto& root = csyren::render::SerializationServices::getRootJson();
			auto& rm = csyren::render::SerializationServices::getResourceManager();
			if (!root.contains("resources"))
			{
				csyren::log::error("Deserialize: Texture handler was saved but resources did not set up in file.");
				return;
			}

			for (auto& res : root["resources"])
			{
				size_t currentID;
				res["id"].get_to(currentID);
				if (currentID == resourceID)
				{
					std::string type;
					res["type"].get_to(type);
					if (res["type"] != "texture")
					{
						csyren::log::error("Deserialize: wrong handler type was saved.expected - \'texture\',received - \'{}\'", type);
						return;
					}
					std::string path;
					res["path"].get_to(path);
					ctx = rm.get<csyren::render::Texture>(path);
					return;
				}
			}

		}
	};


	//-----------------------------------------MeshHandle---------------------------------------------------------------
	template<>
	struct adl_serializer<csyren::render::MeshHandle>
	{
		static void to_json(json& j, const csyren::render::MeshHandle& value)
		{
			auto& root = csyren::render::SerializationServices::getRootJson();
			auto& rm = csyren::render::SerializationServices::getResourceManager();

			std::string resourcePath = rm.getMeshName(value);

			if (!root.contains("resources"))
			{
				root["resources"] = json::array();
			}

			for (const auto& res : root["resources"])
			{
				if (resourcePath == res.value("path", std::string()))
				{
					j = res["id"];
					return;
				}
			}

			size_t newId = root["resources"].size();
			json newRes;
			newRes["id"] = newId;
			newRes["type"] = "mesh";
			newRes["path"] = resourcePath;
			root["resources"].push_back(newRes);

			j = newId;
		}

		static void from_json(const json& j, csyren::render::MeshHandle& value)
		{
			size_t resourceID;
			j.get_to(resourceID);

			auto& root = csyren::render::SerializationServices::getRootJson();
			auto& rm = csyren::render::SerializationServices::getResourceManager();

			if (!root.contains("resources"))
			{
				csyren::log::error("Deserialize: Mesh handler was saved but resources did not set up in file.");
				return;
			}

			for (auto& res : root["resources"])
			{
				size_t currentID;
				res["id"].get_to(currentID);
				if (currentID == resourceID)
				{
					std::string type;
					res["type"].get_to(type);
					if (res["type"] != "mesh")
					{
						csyren::log::error("Deserialize: wrong handler type was saved.expected - \'mesh\',received - \'{}\'", type);
						return;
					}
					std::string path;
					res["path"].get_to(path);
					value = rm.get<csyren::render::Mesh>(path);
					return;
				}
			}

		}
	};
	//--------------------------------------------------MaterialHandle---------------------------------------------------------------
	template<>
	struct adl_serializer<csyren::render::MaterialHandle>
	{
		static void to_json(json& j, const csyren::render::MaterialHandle& value)
		{

			auto& root = csyren::render::SerializationServices::getRootJson();
			auto& rm = csyren::render::SerializationServices::getResourceManager();
			std::string resourcePath = rm.getMaterialName(value);

			if (!root.contains("resources"))
			{
				root["resources"] = json::array();
			}

			for (const auto& res : root["resources"])
			{
				if (resourcePath == res.value("path", std::string()))
				{
					j = res["id"];
					return;
				}
			}

			size_t newId = root["resources"].size();
			json newRes;
			newRes["id"] = newId;
			newRes["type"] = "material";
			newRes["path"] = resourcePath;
			root["resources"].push_back(newRes);

			j = newId;
		}

		static void from_json(const json& j, csyren::render::MaterialHandle& value)
		{
			size_t resourceID;
			j.get_to(resourceID);

			auto& root = csyren::render::SerializationServices::getRootJson();
			auto& rm = csyren::render::SerializationServices::getResourceManager();

			if (!root.contains("resources"))
			{
				csyren::log::error("Deserialize: Material handler was saved but resources did not set up in file.");
				return;
			}

			for (auto& res : root["resources"])
			{
				size_t currentID;
				res["id"].get_to(currentID);
				if (currentID == resourceID)
				{
					std::string type;
					res["type"].get_to(type);
					if (res["type"] != "material")
					{
						csyren::log::error("Deserialize: wrong handler type was saved.expected - \'material\',received - \'{}\'", type);
						return;
					}
					std::string path;
					res["path"].get_to(path);
					value = rm.get<csyren::render::Material>(path);
					return;
				}
			}

		}
	};

}
