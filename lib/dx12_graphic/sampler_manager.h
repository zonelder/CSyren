#pragma once
#include <array>
#include "descriptor_heap_manager.h"
#include <cassert>

namespace csyren::render
{
    enum class SamplerType
    {
        PointWrap,
        PointClamp,
        LinearWrap,
        LinearClamp,
        AnisotropicWrap,
        AnisotropicClamp,
        ShadowPCF, // Для сравнения PCF на картах теней
        _MAX,
	};

    class SamplerManager
    {
    public:
        void init(ID3D12Device* device, DescriptorHeapManager* samplerHeapManager)
        {
            assert(samplerHeapManager != nullptr);

            D3D12_SAMPLER_DESC samplerDesc = {};
            samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
            samplerDesc.MinLOD = 0;
            samplerDesc.MipLODBias = 0.0f;
            samplerDesc.MaxAnisotropy = 1;
            samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

            // --- PointWrap ---
            size_t index = static_cast<size_t>(SamplerType::PointWrap);
            samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

            _samplerHandles[index] = samplerHeapManager->allocate();
            device->CreateSampler(&samplerDesc, _samplerHandles[index].cpuHandle);

            // --- PointClamp ---
            size_t index = static_cast<size_t>(SamplerType::PointClamp);
            samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            _samplerHandles[index] = samplerHeapManager->allocate();
            device->CreateSampler(&samplerDesc, _samplerHandles[index].cpuHandle);

            // --- LinearWrap ---
            index = static_cast<size_t>(SamplerType::LinearWrap);
            samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            _samplerHandles[index] = samplerHeapManager->allocate();
            device->CreateSampler(&samplerDesc, _samplerHandles[index].cpuHandle);

            // --- LinearClamp ---
            index = static_cast<size_t>(SamplerType::LinearClamp);
            samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            _samplerHandles[index] = samplerHeapManager->allocate();
            device->CreateSampler(&samplerDesc, _samplerHandles[index].cpuHandle);

            // --- AnisotropicWrap ---
            index = static_cast<size_t>(SamplerType::AnisotropicWrap);
            samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
            samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplerDesc.MaxAnisotropy = 16; // Стандартное хорошее значение
            _samplerHandles[index] = samplerHeapManager->allocate();
            device->CreateSampler(&samplerDesc, _samplerHandles[index].cpuHandle);
            samplerDesc.MaxAnisotropy = 1; // Сброс для следующих

            // --- Shadow PCF ---
            index = static_cast<size_t>(SamplerType::ShadowPCF);
            samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
            samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            samplerDesc.BorderColor[0] = 1.0f; // Цвет границы для Shadow карт
            samplerDesc.BorderColor[1] = 1.0f;
            samplerDesc.BorderColor[2] = 1.0f;
            samplerDesc.BorderColor[3] = 1.0f;
            samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; // Функция сравнения для PCF
            _samplerHandles[index] = samplerHeapManager->allocate();
            device->CreateSampler(&samplerDesc, _samplerHandles[index].cpuHandle);


        }


        D3D12_GPU_DESCRIPTOR_HANDLE GetSampler(SamplerType type)
        {
            return _samplerHandles[static_cast<size_t>(type)].gpuHandle;
        }
    private:
        std::array< DescriptorHandles, static_cast<size_t>(SamplerType::_MAX)> _samplerHandles;
    };
}

