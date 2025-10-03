#pragma once

#include <DirectXMath.h>
#include <unordered_map>


namespace csyren::render
{
	struct EngineVariableBuffer
	{
		//-------------------------------perFrame-------------------------------
		DirectX::XMMATRIX worldMatrix;
		DirectX::XMMATRIX viewMatrix;
		DirectX::XMMATRIX invViewMatrix;
		DirectX::XMMATRIX projectionMatrix;
		DirectX::XMMATRIX viewProjectionMatrix;
		DirectX::XMVECTOR cameraPosition;
		float totalTime;
		//----------------------------------------------------------------------


	};
}

namespace csyren::render::details
{

	enum class SemanticDataType { Unknown,Float,Float2,Float3,Float4, Matrix4x4 };

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
			
			registerSemantic("Object", CBufferUpdateType::Entity);
			registerSemantic("Entity", CBufferUpdateType::Entity);

			registerSemantic("Material", CBufferUpdateType::Material);
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
			registerSemantic<DirectX::XMMATRIX>("World", offsetof(EngineVariableBuffer, worldMatrix), SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>("View", offsetof(EngineVariableBuffer, viewMatrix), SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>("InvView", offsetof(EngineVariableBuffer, invViewMatrix), SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>("Projection", offsetof(EngineVariableBuffer, projectionMatrix), SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>("ViewProjection", offsetof(EngineVariableBuffer, viewProjectionMatrix), SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMVECTOR>("CameraPosition", offsetof(EngineVariableBuffer, cameraPosition), SemanticDataType::Float4);
			registerSemantic<float>("Time", offsetof(EngineVariableBuffer, totalTime), SemanticDataType::Float);
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