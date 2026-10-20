#pragma once
// Helper for screen-space passes: a graphics shader drawn as one fullscreen
// triangle (VSMain(uint id : SV_VertexID)), no vertex buffer, no depth test.
// Render target formats are taken from the recording pass.
#include <string>
#include "dx12_graphic/renderer.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/material.h"
#include "dx12_graphic/vertex_layout.h"

namespace csyren::render
{
    class FullscreenPass
    {
    public:
        static MaterialStateDesc states()
        {
            MaterialStateDesc states{};
            states.depthStencilState.DepthEnable = FALSE;
            states.depthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            states.rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            return states;
        }
        static const VertexLayout& emptyLayout()
        {
            static const VertexLayout layout(std::vector<VertexAttribute>{});
            return layout;
        }
        // Resolve the shader resource (call outside GPU tasks, e.g. onAttach).
        void load(const std::string& path)
        {
            _path = path;
            if (!_shader) _shader = core::Services::get<ResourceManager>()->get<GraphicShader>(path);
        }
        // Binds pipeline + frame constants; returns the shader for resource binding.
        const GraphicShader* bind(Renderer& renderer, const ResourceManager& resources)
        {
            const auto* shader = _shader ? resources.getShader(_shader) : nullptr;
            if (!shader)
            {
                if (!_warned) { _warned = true; log::error("FullscreenPass: shader '{}' is not available", _path); }
                return nullptr;
            }
            if (!renderer.bindShader(resources, _shader, states(), emptyLayout())) return nullptr;
            return shader;
        }
        ShaderHandle handle() const { return _shader; }
    private:
        std::string _path;
        ShaderHandle _shader;
        bool _warned = false;
    };
}
