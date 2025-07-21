#include "pch.h"
#include "mesh.h"
#include "material.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace csyren::render
{
    bool Mesh::init(Renderer& renderer,
        const std::vector<Vertex>& vertices,
        const std::vector<uint16_t>& indices)
    {
        auto* device = renderer.device();
        _indexCount = static_cast<UINT>(indices.size());

        D3D12_HEAP_PROPERTIES heapProp = {};
        heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width = sizeof(Vertex) * vertices.size();
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (DX_FAILED(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE,
            &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&_vertexBuffer))))
            return false;

        void* data = nullptr;
        D3D12_RANGE readRange{ 0,0 };
        if (DX_SUCCEEDED(_vertexBuffer->Map(0, &readRange, &data)))
        {
            memcpy(data, vertices.data(), vertices.size() * sizeof(Vertex));
            _vertexBuffer->Unmap(0, nullptr);
        }

        _vertexView.BufferLocation = _vertexBuffer->GetGPUVirtualAddress();
        _vertexView.SizeInBytes = static_cast<UINT>(resDesc.Width);
        _vertexView.StrideInBytes = sizeof(Vertex);

        resDesc.Width = sizeof(uint16_t) * indices.size();
        if (DX_FAILED(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE,
            &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&_indexBuffer))))
            return false;

        if (DX_SUCCEEDED(_indexBuffer->Map(0, &readRange, &data)))
        {
            memcpy(data, indices.data(), indices.size() * sizeof(uint16_t));
            _indexBuffer->Unmap(0, nullptr);
        }

        _indexView.BufferLocation = _indexBuffer->GetGPUVirtualAddress();
        _indexView.SizeInBytes = static_cast<UINT>(resDesc.Width);
        _indexView.Format = DXGI_FORMAT_R16_UINT;

        return true;
    }

    void Mesh::draw(Renderer& renderer, const Material* material)
    {
        ID3D12GraphicsCommandList* cmd = renderer.commandList();
        cmd->SetPipelineState(material->pso());
        cmd->SetGraphicsRootSignature(material->rootSig());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0, 1, &_vertexView);
        cmd->IASetIndexBuffer(&_indexView);
        cmd->DrawIndexedInstanced(_indexCount, 1, 0, 0, 0);
    }
}
