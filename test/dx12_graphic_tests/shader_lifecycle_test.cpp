#include "shader_test_fixture.h"
#include "dx12_graphic/shader.h"
#include "dx12_graphic/compute_shader.h"
#include "dx12_graphic/resource_handle.h"
#include "dx12_graphic/shader_file.h"
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <type_traits>

namespace
{
using namespace csyren::render;
static_assert(!std::is_same_v<ShaderHandle, ComputeShaderHandle>);
static_assert(std::is_nothrow_move_assignable_v<GraphicShader>);
static_assert(std::is_nothrow_move_assignable_v<ComputeShader>);

const std::string computeSource = "RWStructuredBuffer<uint> output : register(u0);\n"
    "[numthreads(8, 4, 1)] void CSMain(uint3 id : SV_DispatchThreadID) { output[id.x] = id.y; }\n";
const std::string geometrySource = "struct Output { float4 position : SV_POSITION; };\n"
    "Output VSMain(float3 p : POSITION) { Output o; o.position = float4(p,1); return o; }\n"
    "float4 PSMain(Output input) : SV_TARGET { return 1; }\n"
    "[maxvertexcount(1)] void GSMain(point Output input[1], inout PointStream<Output> output) { output.Append(input[0]); }\n";

class ShaderLifecycleTest : public csyren::test::ShaderFilesFixture
{
protected:
    void SetUp() override
    {
        ShaderFilesFixture::SetUp();
        if (HasFatalFailure()) return;
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        ASSERT_TRUE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
        ASSERT_TRUE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
        ASSERT_TRUE(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device;
};

TEST_F(ShaderLifecycleTest, BuiltInGraphicsShadersInitializeAndRoundTrip)
{
    for (const auto* name : {"default", "rainbow", "texture"})
    {
        SCOPED_TRACE(name);
        GraphicShader shader;
        ShaderBase& base = shader;
        EXPECT_EQ(base.getType(), ShaderType::Graphics);
        ASSERT_TRUE(base.init(device.Get(), std::string("shaders/") + name + ".shader"));
        EXPECT_NE(base.getRootSignature(), nullptr);
        EXPECT_NE(shader.getVSBytecode().pShaderBytecode, nullptr);
        EXPECT_NE(shader.getPSBytecode().pShaderBytecode, nullptr);
        EXPECT_EQ(shader.getGSBytecode().pShaderBytecode, nullptr);
        EXPECT_EQ(base.getBytecode(ShaderStage::Compute).pShaderBytecode, nullptr);
        const auto path = std::string("fixture:/") + name + ".shader";
        ASSERT_TRUE(base.save(path));
        GraphicShader restored;
        ASSERT_TRUE(restored.init(device.Get(), from_asset_path, path));
        EXPECT_EQ(restored.getInputLayout().size(), shader.getInputLayout().size());
        EXPECT_FALSE(base.save("fixture:/wrong.coshader"));
    }
}

TEST_F(ShaderLifecycleTest, ComputeShaderPreservesReflectionAndSource)
{
    ComputeShader compute;
    ShaderBase& base = compute;
    EXPECT_EQ(base.getType(), ShaderType::Compute);
    ASSERT_TRUE(base.init(device.Get(), from_source_code, computeSource));
    EXPECT_NE(base.getRootSignature(), nullptr);
    EXPECT_NE(compute.getCSBytecode().pShaderBytecode, nullptr);
    EXPECT_EQ(base.getBytecode(ShaderStage::Vertex).pShaderBytecode, nullptr);
    EXPECT_EQ(compute.getThreadGroupCountX(), 8u);
    EXPECT_EQ(compute.getThreadGroupCountY(), 4u);
    EXPECT_EQ(compute.getThreadGroupCountZ(), 1u);
    EXPECT_NE(base.getRootParameterIndex("output"), UINT_MAX);
    ASSERT_TRUE(base.save("fixture:/compute.coshader"));
    ComputeShader restored;
    ASSERT_TRUE(restored.init(device.Get(), "fixture:/compute.coshader"));
    EXPECT_EQ(restored.getThreadGroupCountX(), 8u);
    EXPECT_EQ(restored.getThreadGroupCountY(), 4u);
    EXPECT_FALSE(restored.init(device.Get(), "shaders/default.shader"));
    EXPECT_FALSE(restored.save("fixture:/wrong.shader"));
}

TEST_F(ShaderLifecycleTest, CompilationFailurePreservesPreviousShader)
{
    ComputeShader compute;
    ASSERT_TRUE(compute.init(device.Get(), from_source_code, computeSource));
    EXPECT_FALSE(compute.init(device.Get(), from_source_code, "void WrongEntry() {}"));
    EXPECT_NE(compute.getCSBytecode().pShaderBytecode, nullptr);
    ASSERT_TRUE(compute.save("fixture:/compute.coshader"));
    std::string saved;
    ASSERT_TRUE(details::loadShaderSource("fixture:/compute.coshader", ".coshader", saved));
    EXPECT_EQ(saved, computeSource);
}

TEST_F(ShaderLifecycleTest, RejectsInvalidSourceDeviceAndShaderType)
{
    GraphicShader invalid;
    EXPECT_FALSE(invalid.init(device.Get(), from_source_code, computeSource));
    EXPECT_FALSE(invalid.init(static_cast<ID3D12Device*>(nullptr), from_source_code, computeSource));
    EXPECT_FALSE(invalid.save("fixture:/uninitialized.shader"));
    ASSERT_TRUE(details::saveShaderSource("fixture:/compute.coshader", ".coshader", computeSource));
    EXPECT_FALSE(invalid.init(device.Get(), "fixture:/compute.coshader"));
}

TEST_F(ShaderLifecycleTest, InvalidOptionalStageFailsInitialization)
{
    const std::string badGeometry = "float4 VSMain(float3 p : POSITION) : SV_POSITION { return float4(p,1); }\n"
        "float4 PSMain() : SV_TARGET { return 1; }\n"
        "float4 GSMain(float4 p : POSITION) : SV_POSITION { return p; }\n";
    GraphicShader invalid;
    EXPECT_FALSE(invalid.init(device.Get(), from_source_code, badGeometry));
}

TEST_F(ShaderLifecycleTest, ReloadClearsRemovedOptionalStages)
{
    GraphicShader shader;
    ASSERT_TRUE(shader.init(device.Get(), from_source_code, geometrySource));
    EXPECT_NE(shader.getGSBytecode().pShaderBytecode, nullptr);
    ASSERT_TRUE(shader.init(device.Get(), "shaders/texture.shader"));
    EXPECT_EQ(shader.getGSBytecode().pShaderBytecode, nullptr);
}

TEST_F(ShaderLifecycleTest, LegacyCacheSupportsGeometryStage)
{
    ASSERT_TRUE(details::saveShaderSource("fixture:/legacy.hlsl", ".hlsl", geometrySource));
#if !defined(_DEBUG)
    // Release reads cache; prepare its own fixture without relying on a Debug run.
    const auto meta = ShaderMetaBuilder::build(geometrySource);
    ASSERT_TRUE(meta);
    ASSERT_TRUE(ShaderMetaBuilder::save(*meta, "cache:/fixture_/legacy.json"));
    struct Stage { const char* entry; const char* target; const char* suffix; };
    for (const auto stage : {Stage{"VSMain", "vs_5_1", "_vs.cso"}, Stage{"PSMain", "ps_5_1", "_ps.cso"}, Stage{"GSMain", "gs_5_1", "_gs.cso"}})
    {
        Microsoft::WRL::ComPtr<ID3DBlob> blob;
        ASSERT_TRUE(SUCCEEDED(D3DCompile(geometrySource.data(), geometrySource.size(), nullptr,
            nullptr, nullptr, stage.entry, stage.target, 0, 0, &blob, nullptr)));
        auto path = std::filesystem::path("cache:/fixture_/legacy.hlsl");
        path.replace_extension(stage.suffix);
        ASSERT_TRUE(files.writeFile(path, std::span(static_cast<const std::byte*>(blob->GetBufferPointer()), blob->GetBufferSize())));
    }
#endif
    GraphicShader legacy;
    ASSERT_TRUE(legacy.init(device.Get(), "fixture:/legacy.hlsl"));
    EXPECT_NE(legacy.getGSBytecode().pShaderBytecode, nullptr);
#if defined(_DEBUG)
    EXPECT_TRUE(legacy.save("fixture:/legacy_export.shader"));
    EXPECT_TRUE(files.exists("cache:/fixture_/legacy.json"));
#else
    EXPECT_FALSE(legacy.save("fixture:/legacy_export.shader"));
#endif
}
}
