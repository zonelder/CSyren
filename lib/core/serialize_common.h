#pragma once
#include "core/field_context.h"
#include "cstdmf/log.h"
#include "core/renderer.h"
#include "math/math.h"


namespace nlohmann
{
//---------------------------------------Vector2---------------------------------------------
	template<>
	struct adl_serializer<csyren::math::Vector2>
	{
		static void to_json(json& j, const csyren::math::Vector2& vec)
		{
			DirectX::XMFLOAT2 storedFloat2;
			DirectX::XMStoreFloat2(&storedFloat2, vec);
			j = { storedFloat2.x,storedFloat2.y };
		}


		static void from_json(const json& j, csyren::math::Vector2& vec)
		{
			if (j.is_array() && j.size() == 2)
			{
				DirectX::XMFLOAT2 loadedFloat2;
				j.at(0).get_to(loadedFloat2.x);
				j.at(1).get_to(loadedFloat2.y);
				vec = DirectX::XMLoadFloat2(&loadedFloat2);
			}
		}
	};
//-------------------------------------------------------------------------------------------

//---------------------------------------Vector3---------------------------------------------
	template<>
	struct adl_serializer<csyren::math::Vector3>
	{
		static void to_json(json& j, const csyren::math::Vector3& vec)
		{
			DirectX::XMFLOAT3 storedFloat3;
			DirectX::XMStoreFloat3(&storedFloat3, vec);
			j = { storedFloat3.x,storedFloat3.y,storedFloat3.z };
		}


		static void from_json(const json& j, csyren::math::Vector3& vec)
		{
			if (j.is_array() && j.size() == 3)
			{
				DirectX::XMFLOAT3 loadedFloat3;
				j.at(0).get_to(loadedFloat3.x);
				j.at(1).get_to(loadedFloat3.y);
				j.at(2).get_to(loadedFloat3.z);
				vec = DirectX::XMLoadFloat3(&loadedFloat3);
			}
		}
	};
//-------------------------------------------------------------------------------------------

//---------------------------------------Vector4---------------------------------------------
	template<>
	struct adl_serializer<csyren::math::Vector4>
	{
		static void to_json(json& j, const csyren::math::Vector4& vec)
		{
			DirectX::XMFLOAT4 storedFloat4;
			DirectX::XMStoreFloat4(&storedFloat4, vec);
			j = { storedFloat4.x,storedFloat4.y,storedFloat4.z,storedFloat4.w };
		}


		static void from_json(const json& j, csyren::math::Vector4& vec)
		{
			if (j.is_array() && j.size() == 4)
			{
				DirectX::XMFLOAT4 loadedFloat4;
				j.at(0).get_to(loadedFloat4.x);
				j.at(1).get_to(loadedFloat4.y);
				j.at(2).get_to(loadedFloat4.z);
				j.at(3).get_to(loadedFloat4.w);
				vec = DirectX::XMLoadFloat4(&loadedFloat4);
			}
		}
	};
	//-------------------------------------------------------------------------------------------

//---------------------------------------Color---------------------------------------------
	template<>
	struct adl_serializer<csyren::math::Color>
	{
		static void to_json(json& j, const csyren::math::Color& vec)
		{
			DirectX::XMFLOAT4 storedFloat4;
			DirectX::XMStoreFloat4(&storedFloat4, vec);
			j = { storedFloat4.x,storedFloat4.y,storedFloat4.z,storedFloat4.w };
		}


		static void from_json(const json& j, csyren::math::Color& vec)
		{
			if (j.is_array() && j.size() == 4)
			{
				DirectX::XMFLOAT4 loadedFloat4;
				j.at(0).get_to(loadedFloat4.x);
				j.at(1).get_to(loadedFloat4.y);
				j.at(2).get_to(loadedFloat4.z);
				j.at(3).get_to(loadedFloat4.w);
				vec = DirectX::XMLoadFloat4(&loadedFloat4);
			}
		}
	};

	//-------------------------------------------------------------------------------------------

	//---------------------------------------Vector4---------------------------------------------
	template <>
	struct adl_serializer<csyren::math::Matrix4x4>
	{
		static void to_json(json& j, const csyren::math::Matrix4x4& m)
		{
			DirectX::XMFLOAT4X4 stored;
			DirectX::XMStoreFloat4x4(&stored, m);
			// Сохраняем как плоский массив из 16 чисел
			j = {
				stored._11, stored._12, stored._13, stored._14,
				stored._21, stored._22, stored._23, stored._24,
				stored._31, stored._32, stored._33, stored._34,
				stored._41, stored._42, stored._43, stored._44
			};
		}

		static void from_json(const json& j, csyren::math::Matrix4x4& m)
		{
			if (j.is_array() && j.size() == 16)
			{
				DirectX::XMFLOAT4X4 loaded = {
					j[0], j[1], j[2], j[3],
					j[4], j[5], j[6], j[7],
					j[8], j[9], j[10], j[11],
					j[12], j[13], j[14], j[15]
				};
				m = csyren::math::Matrix4x4(DirectX::XMLoadFloat4x4(&loaded));
			}
		}
	};
//-------------------------------------------------------------------------------------------

//---------------------------------------Quaternion-------------------------------------------
	template <>
	struct adl_serializer<csyren::math::Quaternion>
	{
		static void to_json(json& j, const csyren::math::Quaternion& q)
		{
			DirectX::XMFLOAT4 stored;
			DirectX::XMStoreFloat4(&stored, q);
			j = { stored.x, stored.y, stored.z, stored.w };
		}

		static void from_json(const json& j, csyren::math::Quaternion& q)
		{
			if (j.is_array() && j.size() == 4)
			{
				DirectX::XMFLOAT4 loaded = { j[0],j[1],j[2],j[3]};
				q = DirectX::XMLoadFloat4(&loaded);
			}
		}
	};
//-------------------------------------------------------------------------------------------

//----------------------------------------ContexedData---------------------------------------

	//--------------------------------TextureHandle-------------------------------------------
	template<>
	struct adl_serializer<csyren::core::reflection::FieldContext<csyren::render::TextureHandle>>
	{
		static void to_json(json& j, const csyren::core::reflection::FieldContext<csyren::render::TextureHandle>& ctx)
		{
			if (!ctx.value) {
				j = nullptr;
				return;
			}

			auto& rm = ctx.resourceManager;
			auto& root = ctx.rootJson;
			std::string resourcePath = rm.getTextureName(ctx.value);

			if (!root.contains("resources"))
			{
				root["resources"] = json::array();
			}

			for (const auto& res : root["resources"])
			{
				if (resourcePath == res.value("path",std::string()))
				{
					j = res["id"];
					return;
				}
			}

			uint32_t newId = root["resources"].size();
			json newRes;
			newRes["id"] = newId;
			newRes["type"] = "texture";
			newRes["path"] = resourcePath;
			root["resources"].push_back(newRes);

			j = newId;
		}

		static void from_json(const json& j, csyren::core::reflection::FieldContext<csyren::render::TextureHandle>& ctx)
		{
			uint32_t resourceID;
			j.get_to(resourceID);

			auto& root = ctx.rootJson;

			if (!root.contains("resources"))
			{
				csyren::log::error("Deserialize: Texture handler was saved but resources did not set up in file.");
				return;
			}

			for (auto& res : root["resources"])
			{
				uint32_t currentID;
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
					ctx.value = ctx.resourceManager.loadTexture(path);
					return;
				}
			}

		}
	};


	//-----------------------------------------MeshHandle---------------------------------------------------------------
	template<>
	struct adl_serializer<csyren::core::reflection::FieldContext<csyren::render::MeshHandle>>
	{
		static void to_json(json& j, const csyren::core::reflection::FieldContext<csyren::render::MeshHandle>& ctx)
		{
			if (!ctx.value) {
				j = nullptr;
				return;
			}

			auto& rm = ctx.resourceManager;
			auto& root = ctx.rootJson;
			std::string resourcePath = rm.getMeshName(ctx.value);

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

			uint32_t newId = root["resources"].size();
			json newRes;
			newRes["id"] = newId;
			newRes["type"] = "mesh";
			newRes["path"] = resourcePath;
			root["resources"].push_back(newRes);

			j = newId;
		}

		static void from_json(const json& j, csyren::core::reflection::FieldContext<csyren::render::MeshHandle>& ctx)
		{
			uint32_t resourceID;
			j.get_to(resourceID);

			auto& root = ctx.rootJson;

			if (!root.contains("resources"))
			{
				csyren::log::error("Deserialize: Mesh handler was saved but resources did not set up in file.");
				return;
			}

			for (auto& res : root["resources"])
			{
				uint32_t currentID;
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
					ctx.value = ctx.resourceManager.loadMesh(path);
					return;
				}
			}

		}
	};
	//--------------------------------------------------MaterialHandle---------------------------------------------------------------
	template<>
	struct adl_serializer<csyren::core::reflection::FieldContext<csyren::render::MaterialHandle>>
	{
		static void to_json(json& j, const csyren::core::reflection::FieldContext<csyren::render::MaterialHandle>& ctx)
		{
			if (!ctx.value) {
				j = nullptr;
				return;
			}

			auto& rm = ctx.resourceManager;
			auto& root = ctx.rootJson;
			std::string resourcePath = rm.getMaterialName(ctx.value);

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

			uint32_t newId = root["resources"].size();
			json newRes;
			newRes["id"] = newId;
			newRes["type"] = "material";
			newRes["path"] = resourcePath;
			root["resources"].push_back(newRes);

			j = newId;
		}

		static void from_json(const json& j, csyren::core::reflection::FieldContext<csyren::render::MaterialHandle>& ctx)
		{
			uint32_t resourceID;
			j.get_to(resourceID);

			auto& root = ctx.rootJson;

			if (!root.contains("resources"))
			{
				csyren::log::error("Deserialize: Material handler was saved but resources did not set up in file.");
				return;
			}

			for (auto& res : root["resources"])
			{
				uint32_t currentID;
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
					ctx.value = ctx.resourceManager.loadMaterial(path);
					return;
				}
			}

		}
	};
}
