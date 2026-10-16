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

    bool ShaderParameterBinder::updateMaterialBuffer(ID3D12Device*, ID3D12GraphicsCommandList* commands,
        const ResourceManager* resources, UploadRingBuffer& upload, MaterialHandle handle)
    {
        auto* material = resources->getMaterial(handle);
        if (!material) return false;
        auto* shader = resources->getShader(material->getShader());
        if (!shader) return false;
        const auto* layout = shader->getConstantBuffer(details::CBufferUpdateType::Material);
        if (!layout) return true;
        uint8_t* memory = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS address = 0;
        if (upload.allocate(layout->size, reinterpret_cast<void**>(&memory), &address) == size_t(-1)) return false;
        std::memset(memory, 0, layout->size);
        for (const auto& variable : layout->variables) {
            const auto& value = material->getVariable(variable.name);
            if (value.size() == variable.size)
                copyWithTranspose(memory + variable.offset, value.data(), variable.size, variable.needTranspose);
        }
        commands->SetGraphicsRootConstantBufferView(static_cast<UINT>(layout->rootParameterIndex), address);
        return true;
    }

	bool ShaderParameterBinder::updateFrameBuffer(ID3D12GraphicsCommandList* cmdList, const EngineVariableBuffer& engineVars, const SemanticBufferLayout* layout,
		UploadRingBuffer& ringBuffer)
	{
		if (!layout)
		{
			//nothing to do here
			return true;
		}

		std::vector<uint8_t> cpuData(layout->size, 0);
		const uint8_t* srcBase = reinterpret_cast<const uint8_t*>(&engineVars);
		uint8_t* dstBase = cpuData.data();
		for (const auto&  cmd : layout->copyCommands)
		{
			copyWithTranspose(dstBase + cmd.dstOffset, srcBase + cmd.srcOffset, cmd.size, cmd.transpose);
		}
		D3D12_GPU_VIRTUAL_ADDRESS addr = 0;
        if (ringBuffer.update(cpuData.data(), cpuData.size(), &addr) == size_t(-1)) return false;
		cmdList->SetGraphicsRootConstantBufferView(layout->rootParameterIndex, addr);
		return true;
	}

	bool ShaderParameterBinder::updateEntityBuffer(ID3D12GraphicsCommandList* cmdList,const EntityVariableBuffer& entityVars,const SemanticBufferLayout* layout, UploadRingBuffer& ringBuffer)
	{
		if (!layout)
		{
			return true;//nothing to update;
		}

		D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = 0;
		uint8_t* dstBase = nullptr;

		if (ringBuffer.allocate(layout->size, (void**)&dstBase, &gpuAddr) == size_t(-1))
		{
			log::error("ShaderParameterBinder::updateEntityBuffer : failed to allocate per-entity memory");
			return false;
		}


		const uint8_t* srcBase = reinterpret_cast<const uint8_t*>(&entityVars);
		for (const auto& cmd : layout->copyCommands)
		{
			copyWithTranspose(
				dstBase + cmd.dstOffset,
				srcBase + cmd.srcOffset,
				cmd.size,
				cmd.transpose
			);
		}
		cmdList->SetGraphicsRootConstantBufferView(
			layout->rootParameterIndex,
			gpuAddr
		);

		return true;
	}
}
