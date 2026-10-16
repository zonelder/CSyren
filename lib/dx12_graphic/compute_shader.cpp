#include "pch.h"
#include "compute_shader.h"

#include <d3dcompiler.h>

namespace csyren::render
{
    bool ComputeShader::finalizeInit(ID3D12Device*)
    {
        auto* blob = stageBlob(ShaderStage::Compute);
        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
        if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&reflection))))
            return false;
        reflection->GetThreadGroupSize(&_threadGroupX, &_threadGroupY, &_threadGroupZ);
        return true;
    }
}
