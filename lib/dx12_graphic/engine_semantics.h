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

#define REGISTER_SEMANTIC_NAME(name) getNameMap()[std::string(#name)] = EngineSemantic::##name;

namespace csyren::render::details
{

	enum class EngineSemantic
	{
		None,
		World,
		View,
		InvView,
		Projection,
		ViewProjection,
		CameraPosition,
		Time,
	};

	enum class SemanticDataType { Unknown,Float,Float2,Float3,Float4, Matrix4x4 };


	struct SemanticInfo
	{
		EngineSemantic semantic;
		size_t offset;
		size_t size;
		SemanticDataType type;
	};


	class EngineSemanticRegistry
	{
	public:
		static void initialize()
		{
			registerSemantic<DirectX::XMMATRIX>(EngineSemantic::World, offsetof(EngineVariableBuffer, worldMatrix), SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>(EngineSemantic::View, offsetof(EngineVariableBuffer, viewMatrix), SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>(EngineSemantic::InvView, offsetof(EngineVariableBuffer, invViewMatrix), SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>(EngineSemantic::Projection, offsetof(EngineVariableBuffer, projectionMatrix), SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMMATRIX>(EngineSemantic::ViewProjection, offsetof(EngineVariableBuffer, viewProjectionMatrix), SemanticDataType::Matrix4x4);
			registerSemantic<DirectX::XMVECTOR>(EngineSemantic::CameraPosition, offsetof(EngineVariableBuffer, cameraPosition), SemanticDataType::Float4);
			registerSemantic<float>(EngineSemantic::Time, offsetof(EngineVariableBuffer, totalTime), SemanticDataType::Float);


			REGISTER_SEMANTIC_NAME(World);
			REGISTER_SEMANTIC_NAME(View);
			REGISTER_SEMANTIC_NAME(InvView);
			REGISTER_SEMANTIC_NAME(Projection);
			REGISTER_SEMANTIC_NAME(ViewProjection);
			REGISTER_SEMANTIC_NAME(CameraPosition);
			REGISTER_SEMANTIC_NAME(Time);

		}

		static const SemanticInfo* find(EngineSemantic semantic)
		{
			auto it = getMap().find(semantic);
			return (it != getMap().end()) ? &it->second : nullptr;
		}

		static EngineSemantic findSemantic(const std::string semanticName)
		{
			auto it = getNameMap().find(semanticName);
			return (it != getNameMap().end()) ? it->second : EngineSemantic::None;
		}
	private:
		template<typename T>
		static void registerSemantic(EngineSemantic semantic, size_t offset, SemanticDataType type)
		{
			getMap()[semantic] = { semantic, offset, sizeof(T), type };
		}

		// Singleton-like accessor
		static std::unordered_map<EngineSemantic, SemanticInfo>& getMap()
		{
			static std::unordered_map<EngineSemantic, SemanticInfo> instance;
			return instance;
		}

		static std::unordered_map<std::string, EngineSemantic>& getNameMap()
		{
			static std::unordered_map<std::string, EngineSemantic> instance;
			return instance;
		}
	};

}

#undef REGISTER_SEMANTIC_NAME