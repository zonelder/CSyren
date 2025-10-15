#pragma once

#include <DirectXMath.h>
#include <unordered_map>
#include "core/entity.h"

namespace csyren::render
{
	struct alignas(16) EngineVariableBuffer
	{
		//-------------------------------perFrame-------------------------------
		DirectX::XMFLOAT4X4 viewMatrix{};
		DirectX::XMFLOAT4X4 invViewMatrix{};
		DirectX::XMFLOAT4X4 projectionMatrix{};
		DirectX::XMFLOAT4X4 viewProjectionMatrix{};
		DirectX::XMFLOAT4	cameraPosition{};
		float				totalTime		= 0.0f;
		//----------------------------------------------------------------------


	};

	struct alignas(16) EntityVariableBuffer
	{
		DirectX::XMFLOAT4X4 worldMatrix{};
		core::Entity::ID	entityID;
		size_t				materialID;
		size_t				meshID;
	};
}

namespace csyren::render::details
{

	enum class SemanticDataType { Unknown,Float,Float2,Float3,Float4, Matrix4x4,Uint };

	enum class CBufferUpdateType {
		Custom,		// user handle this parameters by yourself
		Pass,		// engine update this buffer at the start of each frame 
		Material,	// this buffer attach to the material and update only when material is dirty
		Entity		// this buffer update per Entity.
	};

	struct UpdateInfo
	{
		CBufferUpdateType type;
	};

	class EngineUpdateRegistry
	{
	public:
		void initialize()
		{
			registerSemantic("pass", CBufferUpdateType::Pass);
			registerSemantic("frame", CBufferUpdateType::Pass);
			
			registerSemantic("object", CBufferUpdateType::Entity);
			registerSemantic("entity", CBufferUpdateType::Entity);

			registerSemantic("material", CBufferUpdateType::Material);
		}
		static EngineUpdateRegistry& instance()
		{
			static EngineUpdateRegistry m;
			return m;
		}

		const UpdateInfo* find(const std::string& name) const
		{
			auto it = _registry.find(name);
			return (it != _registry.end()) ? &it->second : nullptr;
		}
	private:
		//for the future. cant be sure we wont add logic to UpdateInfo
		void registerSemantic(const std::string& name, CBufferUpdateType type)
		{
			_registry[name] = { type };
		}
		std::unordered_map<std::string, UpdateInfo> _registry;
	};


	struct SemanticInfo
	{
		SemanticDataType type;
		size_t offset;
		size_t size;
	};

	/**
	 * @class EngineSemanticRegistry
	 * @brief Capture information about all registered sematics.
	 * work directly with string literals.
	 */
	class EngineSemanticRegistry
	{
	public:
		void initialize()
		{
			//pass buffers
			registerSemantic<DirectX::XMMATRIX>("View"			, offsetof(EngineVariableBuffer, viewMatrix)			, SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>("InvView"		, offsetof(EngineVariableBuffer, invViewMatrix)			, SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>("Projection"	, offsetof(EngineVariableBuffer, projectionMatrix)		, SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>("ViewProjection", offsetof(EngineVariableBuffer, viewProjectionMatrix)	, SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMVECTOR>("CameraPosition", offsetof(EngineVariableBuffer, cameraPosition)		, SemanticDataType::Float4);
			registerSemantic<float>("Time"						, offsetof(EngineVariableBuffer, totalTime)				, SemanticDataType::Float);

			//entity buffers
			registerSemantic<DirectX::XMMATRIX>("World"			, offsetof(EntityVariableBuffer, worldMatrix)	, SemanticDataType::Matrix4x4);
			registerSemantic<uint32_t>("EntityID"				, offsetof(EntityVariableBuffer, entityID)		, SemanticDataType::Uint);
			registerSemantic<uint32_t>("MaterialID"				, offsetof(EntityVariableBuffer, materialID)	, SemanticDataType::Uint);
			registerSemantic<uint32_t>("MeshID"					, offsetof(EntityVariableBuffer, meshID)		, SemanticDataType::Matrix4x4);
		}

		const SemanticInfo* find(const std::string& name) const
		{
			auto it = _registry.find(name);
			return (it != _registry.end()) ? &it->second : nullptr;
		}

		static EngineSemanticRegistry& instance()
		{
			static EngineSemanticRegistry m;
			return m;
		}

	private:
		EngineSemanticRegistry() = default;
		~EngineSemanticRegistry() = default;
		EngineSemanticRegistry(const EngineSemanticRegistry&) = delete;
		EngineSemanticRegistry& operator=(const EngineSemanticRegistry&) = delete;

		template<typename T>
		void registerSemantic(const std::string& name, size_t offset, SemanticDataType type)
		{
			_registry[name] = { type, offset, sizeof(T) };
		}

		std::unordered_map<std::string, SemanticInfo> _registry;
	};

}