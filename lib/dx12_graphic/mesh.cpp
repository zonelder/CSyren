#include "pch.h"
#include "mesh.h"
#include "material.h"
#include "resource_manager.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace csyren::render
{
    bool Mesh::createBuffer(ID3D12Device* device, D3D12_HEAP_TYPE heapType, UINT64 size, D3D12_RESOURCE_STATES initialState, Microsoft::WRL::ComPtr<ID3D12Resource>& outResource)
    {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = heapType;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = size;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(&outResource)
        );

        return SUCCEEDED(hr);
    }

    bool Mesh::init(Renderer& renderer,const std::string& filepath)
    {
        log::error("Mesh: attempt to load mesh from file but its not implemented. file = {}", filepath);
        return false;
    }

    bool Mesh::init(Renderer& renderer, const void* vertexData, size_t vertexDataSize, uint32_t vertexStride, const std::vector<MeshIndex>& indices, Usage usage)
    {

        static_assert(sizeof(MeshIndex) == 2, "MeshIndex must be a 16-bit unsigned integer for DXGI_FORMAT_R16_UINT.");
        if (vertexDataSize == 0 || indices.empty())
        {
            log::error("Mesh::init failed: vertex or index data is empty.");
            return false;
        }
        if (!vertexData)
        {
            log::error("Mesh::init failed: expect valid ptr to vertex data but got nullptr");
        }
        auto* device = renderer.device();
        auto* cmdList = renderer.commandList();
        _usage = usage;

        const size_t indexDataSize = indices.size() * sizeof(MeshIndex);
        _indexCount = static_cast<UINT>(indices.size());

        if (_usage == Usage::Dynamic)
        {
            if (!createBuffer(device, D3D12_HEAP_TYPE_UPLOAD, vertexDataSize, D3D12_RESOURCE_STATE_GENERIC_READ, _vertexBuffer)) return false;
            if (!createBuffer(device, D3D12_HEAP_TYPE_UPLOAD, indexDataSize, D3D12_RESOURCE_STATE_GENERIC_READ, _indexBuffer)) return false;

            if (vertexData && !indices.empty())
            {
                update(vertexData, vertexDataSize, indices);
            }
        }
        else
        {
            if (!createBuffer(device, D3D12_HEAP_TYPE_DEFAULT, vertexDataSize, D3D12_RESOURCE_STATE_COMMON, _vertexBuffer)) return false;
            if (!createBuffer(device, D3D12_HEAP_TYPE_DEFAULT, indexDataSize, D3D12_RESOURCE_STATE_COMMON, _indexBuffer)) return false;

            auto uploadBuffer = renderer.getUploadBuffer();
            void* uploadVertexDataPtr;
            D3D12_GPU_VIRTUAL_ADDRESS uploadVertexDataGPUPtr;
            UINT uploadVertexOffset = uploadBuffer->allocate(vertexDataSize, &uploadVertexDataPtr, &uploadVertexDataGPUPtr);
            if (uploadVertexOffset == static_cast<size_t>(-1))
            {
                log::error("Mesh::init failed: Not enough space in the upload buffer for vertices.");
                return false;
            }

            void* uploadIndexBufferPtr;
            D3D12_GPU_VIRTUAL_ADDRESS uploadIndexDataGPUPtr;
            UINT uploadIndexOffset = uploadBuffer->allocate(indexDataSize, &uploadIndexBufferPtr, &uploadIndexDataGPUPtr);
            if (uploadIndexOffset == static_cast<size_t>(-1))
            {
                log::error("Mesh::init failed: Not enough space in the upload buffer for indices.");
                return false;
            }

            memcpy(uploadVertexDataPtr, vertexData, vertexDataSize);
            memcpy(uploadIndexBufferPtr, indices.data(), indexDataSize);

            D3D12_RESOURCE_BARRIER barriers[2];
            barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barriers[0].Transition.pResource = _vertexBuffer.Get();
            barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

            barriers[1] = barriers[0]; // Копируем общие поля
            barriers[1].Transition.pResource = _indexBuffer.Get();
            cmdList->ResourceBarrier(2, barriers);

            cmdList->CopyBufferRegion(_vertexBuffer.Get(), 0, uploadBuffer->currentResource().Get(), uploadVertexOffset, vertexDataSize);
            cmdList->CopyBufferRegion(_indexBuffer.Get(), 0, uploadBuffer->currentResource().Get(), uploadIndexOffset, indexDataSize);

            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
            cmdList->ResourceBarrier(2, barriers);
        }

        _vertexBuffer->SetName(L"VertexBuffer");
        _vertexView.BufferLocation = _vertexBuffer->GetGPUVirtualAddress();
        _vertexView.SizeInBytes = static_cast<UINT>(vertexDataSize);
        _vertexView.StrideInBytes = vertexStride;

        _indexBuffer->SetName(L"IndexBuffer");
        _indexView.BufferLocation = _indexBuffer->GetGPUVirtualAddress();
        _indexView.SizeInBytes = static_cast<UINT>(indexDataSize);
        _indexView.Format = DXGI_FORMAT_R16_UINT;

        return true;
    }

    bool Mesh::update(const void* vertexData, size_t vertexDataSize, const std::vector<MeshIndex>& indices)
    {
        if (_usage == Usage::Static)
        {
            log::warning("Mesh: attempt to update a static mesh. Operation ignored.");
            return false;
        }
        if (!_vertexBuffer || !_indexBuffer) return false;

        const size_t indexDataSize = indices.size() * sizeof(MeshIndex);
        D3D12_RANGE readRange{ 0, 0 };
        void* mappedData = nullptr;

        // Обновляем вершинный буфер
        if (SUCCEEDED(_vertexBuffer->Map(0, &readRange, &mappedData)))
        {
            memcpy(mappedData, vertexData, vertexDataSize);
            _vertexBuffer->Unmap(0, nullptr);
        }
        else return false;

        // Обновляем индексный буфер
        if (SUCCEEDED(_indexBuffer->Map(0, &readRange, &mappedData)))
        {
            memcpy(mappedData, indices.data(), indexDataSize);
            _indexBuffer->Unmap(0, nullptr);
        }
        else return false;

        _indexCount = static_cast<UINT>(indices.size());
        _vertexView.SizeInBytes = static_cast<UINT>(vertexDataSize);
        _indexView.SizeInBytes = static_cast<UINT>(indexDataSize);

        return true;
    }

    void Mesh::bind(Renderer& renderer)
    {
        ID3D12GraphicsCommandList* cmd = renderer.commandList();

        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0, 1, &_vertexView);
        cmd->IASetIndexBuffer(&_indexView);
    }

    void Mesh::draw(Renderer& renderer)
    {
        ID3D12GraphicsCommandList* cmd = renderer.commandList();
        cmd->DrawIndexedInstanced(_indexCount, 1, 0, 0, 0);
    }
}
