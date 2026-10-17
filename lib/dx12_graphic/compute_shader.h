#pragma once
#include "shader_base.h"

namespace csyren::render
{
    class ComputeShader final : public ShaderBase
    {
    public:
        ComputeShader() : ShaderBase(ShaderType::Compute) {}
        D3D12_SHADER_BYTECODE getCSBytecode() const noexcept { return getBytecode(ShaderStage::Compute); }
        UINT getThreadGroupCountX() const noexcept { return _threadGroupX; }
        UINT getThreadGroupCountY() const noexcept { return _threadGroupY; }
        UINT getThreadGroupCountZ() const noexcept { return _threadGroupZ; }

    private:
        bool finalizeInit(ID3D12Device* device) override;
        UINT _threadGroupX = 0;
        UINT _threadGroupY = 0;
        UINT _threadGroupZ = 0;
    };
}
