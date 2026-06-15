#include "pch.h"
#include "mesh.h"
#include "material.h"
#include "resource_manager.h"
#include "renderer.h"

using Microsoft::WRL::ComPtr;


namespace csyren::render
{
    bool Mesh::createBuffer(ID3D12Device* device, D3D12_HEAP_TYPE heapType, UINT64 size, D3D12_RESOURCE_STATES initialState, Microsoft::WRL::ComPtr<ID3D12Resource>& outResource)
    {
        using namespace DirectX;
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

    bool Mesh::init(const std::string& filepath)
    {
        log::error("Mesh: attempt to load mesh from file but its not implemented. file = {}", filepath);
        return false;
    }

    bool Mesh::init(const MeshBuilder& builder, Usage usage)
    {

        static_assert(sizeof(vertex_meta::index_type) == 2, "MeshIndex must be a 16-bit unsigned integer for DXGI_FORMAT_R16_UINT.");

        auto raw = builder.build();

        if (raw.vertexBuffer.empty()|| raw.indices.empty())
        {
            log::error("Mesh::init failed: vertex or index data is empty.");
            return false;
        }
        auto& r = *core::Services::get<Renderer>();
        auto* device =  r.device();
        auto* cmdList = r.commandList();

        const size_t vertexDataSize = raw.vertexBuffer.size();
        const size_t indexDataSize = raw.indices.size() * sizeof(vertex_meta::index_type);
        _usage = usage;
        _indexCount = static_cast<UINT>(raw.indices.size());
        _layout = std::move(raw.layout);

        if (_usage == Usage::Dynamic)
        {
            if (!createBuffer(device, D3D12_HEAP_TYPE_UPLOAD, vertexDataSize, D3D12_RESOURCE_STATE_GENERIC_READ, _vertexBuffer)) return false;
            if (!createBuffer(device, D3D12_HEAP_TYPE_UPLOAD, indexDataSize, D3D12_RESOURCE_STATE_GENERIC_READ, _indexBuffer)) return false;
            update(raw);
        }
        else
        {
            if (!createBuffer(device, D3D12_HEAP_TYPE_DEFAULT, vertexDataSize, D3D12_RESOURCE_STATE_COMMON, _vertexBuffer)) return false;
            if (!createBuffer(device, D3D12_HEAP_TYPE_DEFAULT, indexDataSize, D3D12_RESOURCE_STATE_COMMON, _indexBuffer)) return false;

            auto uploadBuffer = r.getUploadBuffer();
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

            memcpy(uploadVertexDataPtr, raw.vertexBuffer.data(), vertexDataSize);
            memcpy(uploadIndexBufferPtr, raw.indices.data(), indexDataSize);

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

            cmdList->CopyBufferRegion(_vertexBuffer.Get(), 0, uploadBuffer->resource().Get(), uploadVertexOffset, vertexDataSize);
            cmdList->CopyBufferRegion(_indexBuffer.Get(), 0, uploadBuffer->resource().Get(), uploadIndexOffset, indexDataSize);

            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
            cmdList->ResourceBarrier(2, barriers);
        }

        _vertexBuffer->SetName(L"VertexBuffer");
        _vertexView.BufferLocation = _vertexBuffer->GetGPUVirtualAddress();
        _vertexView.SizeInBytes = static_cast<UINT>(vertexDataSize);
        _vertexView.StrideInBytes = static_cast<UINT>(_layout.getStride());

        _indexBuffer->SetName(L"IndexBuffer");
        _indexView.BufferLocation = _indexBuffer->GetGPUVirtualAddress();
        _indexView.SizeInBytes = static_cast<UINT>(indexDataSize);
        _indexView.Format = DXGI_FORMAT_R16_UINT;

        return true;
    }

    bool Mesh::update(const MeshBuilder::MeshRawData& raw)
    {
        if (_usage == Usage::Static)
        {
            log::warning("Mesh: attempt to update a static mesh. Operation ignored.");
            return false;
        }
        if (!_vertexBuffer || !_indexBuffer) return false;

        D3D12_RANGE readRange{ 0, 0 };
        void* mappedData = nullptr;

        // Обновляем вершинный буфер
        if (SUCCEEDED(_vertexBuffer->Map(0, &readRange, &mappedData)))
        {
            memcpy(mappedData, raw.vertexBuffer.data(), raw.vertexBuffer.size());
            _vertexBuffer->Unmap(0, nullptr);
        }
        else return false;

        // Обновляем индексный буфер
        if (SUCCEEDED(_indexBuffer->Map(0, &readRange, &mappedData)))
        {
            memcpy(mappedData, raw.indices.data(), raw.indices.size()*sizeof(vertex_meta::index_type));
            _indexBuffer->Unmap(0, nullptr);
        }
        else return false;

        _indexCount = static_cast<UINT>(raw.indices.size());
        _vertexView.SizeInBytes = static_cast<UINT>(raw.vertexBuffer.size());
        _indexView.SizeInBytes = static_cast<UINT>(raw.indices.size()*sizeof(vertex_meta::index_type));

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
