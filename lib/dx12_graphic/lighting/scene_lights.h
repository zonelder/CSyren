#pragma once
// CPU snapshot of the scene lights, rebuilt every frame by LightCollectSystem
// and uploaded by the GPU passes that need it (cluster assignment, light pass,
// forward pass). The layout mirrors `Light` in assets/shaders/lighting.hlsli.
#include <DirectXMath.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "dx12_graphic/renderer.h"

namespace csyren::render
{
    struct alignas(16) GpuLight
    {
        DirectX::XMFLOAT3 position{};   // world, point lights
        float radius = 0.0f;            // point lights: window radius
        DirectX::XMFLOAT3 color{};      // radiance (linear, intensity applied)
        uint32_t type = 0;              // 0 directional, 1 point
        DirectX::XMFLOAT3 direction{};  // directional: direction the light travels (normalized)
        float invRadiusSq = 0.0f;
        DirectX::XMFLOAT4 extra{};      // reserved (spot cone, shadow index)
    };
    static_assert(sizeof(GpuLight) == 64, "GpuLight must match the HLSL Light struct (64 bytes)");

    struct SceneLights
    {
        static constexpr uint32_t MaxDirectional = 4;
        static constexpr uint32_t MaxLights = 1024;
        // Directional lights first, then point lights.
        std::vector<GpuLight> lights;
        uint32_t directionalCount = 0;
        uint32_t pointCount = 0;
        uint64_t revision = 0;

        // Copies the list into the recording pass's transient upload memory and
        // returns its GPU address (always at least one element so the root SRV is valid).
        D3D12_GPU_VIRTUAL_ADDRESS upload(Renderer& renderer) const
        {
            const size_t count = (std::max)(size_t(1), lights.size());
            void* cpu = nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
            if (!renderer.allocateUpload(count * sizeof(GpuLight), &cpu, &gpu)) return 0;
            if (lights.empty()) std::memset(cpu, 0, sizeof(GpuLight));
            else std::memcpy(cpu, lights.data(), lights.size() * sizeof(GpuLight));
            return gpu;
        }
    };
}
