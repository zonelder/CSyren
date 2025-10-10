#include "pch.h"
#include "shader_parameter_binder.h"


namespace
{
	static void copyWithTranspose(uint8_t* dst, const uint8_t* src, size_t size, bool transpose)
	{
		if (transpose && size == sizeof(DirectX::XMFLOAT4X4))
		{
			const DirectX::XMFLOAT4X4* srcMat = reinterpret_cast<const DirectX::XMFLOAT4X4*>(src);
			DirectX::XMFLOAT4X4 tmp;
			DirectX::XMStoreFloat4x4(&tmp, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(srcMat)));
			memcpy(dst, &tmp, sizeof(tmp));
		}
		else
		{
			memcpy(dst, src, size);
		}
	}
}

namespace csyren::render
{

	bool ShaderParameterBinder::updateMaterialBuffer(ID3D12Device* device,ID3D12GraphicsCommandList* cmdList, ResourceManager* rm,UploadRingBuffer& ringBuffer,MaterialHandle mathandle)
	{
		auto it = _materialCache.find(mathandle);
		auto material = rm->getMaterial(mathandle);
		if (!material)
		{
			if (it != _materialCache.end())
			{
				log::warning("Material (handle: {}) not found.Destroy its cached buffer.", mathandle.id);
				_materialCache.erase(it);
			}
			return false;
		}
		if (it == _materialCache.end())
		{

			it = _materialCache.emplace(mathandle, MaterialBufferCache{}).first;
			log::info("Created new material buffer cache for handle {}.", mathandle.id);
		}


		auto& cache = it->second;

		auto shader = rm->getShader(material->getShader());
		if (!shader)
		{
			log::warning("Shader for material( handle: {}) not found. Cannot update buffer.", material->getShader().id);
			return false;
		}
		const LinkedBuffer* materialBufferDesc = shader->getConstantBuffer(details::CBufferUpdateType::Material);

		if (!materialBufferDesc)
		{
			if (cache.buffer.size() > 0)
			{
				log::info("Material (handle: {}) switched to s shader with no material buffer.Destroy cache.", mathandle.id);
				_materialCache.erase(it);
			}
			return false;
		}
		//everything up to date;
		if (material->version() != cache.lastUpdatedVersion)
		{

			if (cache.buffer.size() < materialBufferDesc->size)
			{
				log::info("Resizing material buffer for handle {} from {} to {} bytes.",
					mathandle.id, cache.buffer.size(), materialBufferDesc->size);
				if (!cache.buffer.init(device, materialBufferDesc->size,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER))
				{
					log::error("Failed to initialize/resize material buffer for handle {}.", mathandle.id);
					_materialCache.erase(it); // Удаляем кеш, т.к. он в невалидном состоянии
					return false;
				}

			}

			std::vector<uint8_t> cpuData(materialBufferDesc->size, 0);
			for (const auto& varDesc : materialBufferDesc->variables)
			{
				const auto& varData = material->getVariable(varDesc.name);
				if (varData.empty())
				{
					log::debug("Variable '{}' not found in material.using default value.", varDesc.name);
					continue;
				}

				if (varData.size() != varDesc.size)
				{
					log::error("Size mismatch for variable '{}' in material (handle: {}). Shader expects {} bytes, but material provides {} bytes. Skipping update for this variable.",
						varDesc.name, mathandle.id, varDesc.size, varData.size());
					continue;
				}
				copyWithTranspose(cpuData.data() + varDesc.offset, varData.data(), varDesc.size, varDesc.needTranspose);
			};

			D3D12_RESOURCE_BARRIER preCopyBarrier = {};
			preCopyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			preCopyBarrier.Transition.pResource = cache.buffer.getResource();
			preCopyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER; // Состояние от прошлого кадра
			preCopyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			cmdList->ResourceBarrier(1, &preCopyBarrier);

			auto uploadOffset = ringBuffer.update(cpuData.data(), cpuData.size(), nullptr);
			if (uploadOffset == -1)
			{
				log::error("Failed to update ring buffer for material handle {}.", mathandle.id);
				return false;
			}

			cmdList->CopyBufferRegion(
				cache.buffer.getResource(),         // Destination resource (наш DEFAULT буфер)
				0,                                  // Destination offset
				ringBuffer.currentResource().Get(), // Source resource (текущий буфер из кольца)
				uploadOffset,                       // Source offset (смещение, которое мы получили)
				materialBufferDesc->size            // Size of data to copy
			);

			cache.lastUpdatedVersion = material->version();
			D3D12_RESOURCE_BARRIER postCopyBarrier = {};
			postCopyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			postCopyBarrier.Transition.pResource = cache.buffer.getResource();
			postCopyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			postCopyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
			cmdList->ResourceBarrier(1, &postCopyBarrier);

			log::info("Material buffer for handle {} updated to version {}.", mathandle.id, material->version());
		}

		cmdList->SetGraphicsRootConstantBufferView(materialBufferDesc->rootParameterIndex, cache.buffer.getGpuAddress());
		return true;
	}
	bool ShaderParameterBinder::updateFrameBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, EngineVariableBuffer& engineVars, UploadRingBuffer& ringBuffer, Shader* shader)
	{
		const SemanticBufferLayout* frameBufferDesc = shader->getSemanticBuffer(details::CBufferUpdateType::Pass);
		if (!frameBufferDesc)
		{
			//nothing to do here
			return true;
		}

		std::vector<uint8_t> cpuData(frameBufferDesc->size, 0);
		const uint8_t* srcBase = reinterpret_cast<const uint8_t*>(&engineVars);
		uint8_t* dstBase = cpuData.data();
		for (const auto&  cmd : frameBufferDesc->copyCommands)
		{
			copyWithTranspose(dstBase + cmd.dstOffset, srcBase + cmd.srcOffset, cmd.size, cmd.transpose);
		}
		D3D12_GPU_VIRTUAL_ADDRESS addr = 0;
		ringBuffer.update(cpuData.data(), cpuData.size(), &addr);
		cmdList->SetGraphicsRootConstantBufferView(frameBufferDesc->rootParameterIndex, addr);
		return true;
	}
}
