#pragma once

#include <DirectXMath.h>
#include <unordered_map>
#include "core/entity.h"

namespace csyren::render
{
	// Per-view constants. Shaders bind them by semantic name in a
	// "//@update frame" cbuffer (see EngineSemanticRegistry below).
	struct alignas(16) EngineVariableBuffer
	{
		//-------------------------------perFrame-------------------------------
		DirectX::XMFLOAT4X4 viewMatrix{};
		DirectX::XMFLOAT4X4 invViewMatrix{};
		DirectX::XMFLOAT4X4 projectionMatrix{};
		DirectX::XMFLOAT4X4 viewProjectionMatrix{};
		DirectX::XMFLOAT4	cameraPosition{};
		float				totalTime		= 0.0f;
		DirectX::XMFLOAT4	lightDirection{ 1, 0, 0, 0 };	// direction the first directional light travels (world)
		//-------------------------------lighting-------------------------------
		DirectX::XMFLOAT4X4 invProjectionMatrix{};
		DirectX::XMFLOAT4X4 invViewProjectionMatrix{};
		DirectX::XMFLOAT4	screenParams{ 1, 1, 1, 1 };			// width, height, 1/width, 1/height
		DirectX::XMFLOAT4	cameraParams{ 0.1f, 1000.0f, 1, 1 };	// near, far, projection[0][0], projection[1][1]
		DirectX::XMFLOAT4	lightParams{ 0, 0, 0, 0 };			// directional count, point count, debug view, clustered (0/1)
		DirectX::XMFLOAT4	clusterParams{ 16, 8, 24, 64 };		// grid x, y, z, max lights per cluster
		DirectX::XMFLOAT4	clusterDepthParams{ 1, 0, 0, 0 };	// slice = floor(log2(viewZ) * x + y); z = log2(far/near)
		DirectX::XMFLOAT4	ambientColor{ 0.2f, 0.2f, 0.22f, 1 };// rgb = ambient radiance, w = exposure
		DirectX::XMFLOAT4	sunColor{ 0, 0, 0, 0 };				// rgb = radiance of the first directional light
		DirectX::XMFLOAT4	backgroundColor{ 0, 0, 0, 1 };		// camera clear color (sky where depth == 0)
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
			registerSemantic<DirectX::XMVECTOR>("LightDirection", offsetof(EngineVariableBuffer, lightDirection)		, SemanticDataType::Float3);
			registerSemantic<float>("Time"						, offsetof(EngineVariableBuffer, totalTime)				, SemanticDataType::Float);
			// lighting / post processing
			registerSemantic<DirectX::XMMATRIX>("InvProjection"		, offsetof(EngineVariableBuffer, invProjectionMatrix)		, SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>("InvViewProjection"	, offsetof(EngineVariableBuffer, invViewProjectionMatrix)	, SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMVECTOR>("ScreenParams"		, offsetof(EngineVariableBuffer, screenParams)		, SemanticDataType::Float4);
			registerSemantic<DirectX::XMVECTOR>("CameraParams"		, offsetof(EngineVariableBuffer, cameraParams)		, SemanticDataType::Float4);
			registerSemantic<DirectX::XMVECTOR>("LightParams"		, offsetof(EngineVariableBuffer, lightParams)		, SemanticDataType::Float4);
			registerSemantic<DirectX::XMVECTOR>("ClusterParams"		, offsetof(EngineVariableBuffer, clusterParams)		, SemanticDataType::Float4);
			registerSemantic<DirectX::XMVECTOR>("ClusterDepthParams", offsetof(EngineVariableBuffer, clusterDepthParams)	, SemanticDataType::Float4);
			registerSemantic<DirectX::XMVECTOR>("AmbientColor"		, offsetof(EngineVariableBuffer, ambientColor)		, SemanticDataType::Float4);
			registerSemantic<DirectX::XMVECTOR>("SunColor"			, offsetof(EngineVariableBuffer, sunColor)			, SemanticDataType::Float4);
			registerSemantic<DirectX::XMVECTOR>("BackgroundColor"	, offsetof(EngineVariableBuffer, backgroundColor)	, SemanticDataType::Float4);

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
