#include "shader_test_fixture.h"
#include "dx12_graphic/shader_file.h"
#include <d3dcompiler.h>
#include <wrl.h>

namespace
{
using namespace csyren::render::details;
using ShaderFileTest = csyren::test::ShaderFilesFixture;

void expectCompiles(const std::string& source, const char* entry, const char* target)
{
    Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
    const auto result = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
        entry, target, 0, 0, &blob, &errors);
    ASSERT_TRUE(SUCCEEDED(result)) << (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "No compiler diagnostics");
    EXPECT_NE(blob.Get(), nullptr);
}

TEST_F(ShaderFileTest, BuiltInGraphicsShadersCompile)
{
    for (const auto* name : {"default", "rainbow", "texture"})
    {
        SCOPED_TRACE(name);
        std::string source;
        ASSERT_TRUE(loadShaderSource(std::string("shaders/") + name + ".shader", ".shader", source));
        expectCompiles(source, "VSMain", "vs_5_1");
        expectCompiles(source, "PSMain", "ps_5_1");
    }
}

TEST_F(ShaderFileTest, GraphicsSourceRoundTripsThroughNestedFolders)
{
    std::string source, restored;
    ASSERT_TRUE(loadShaderSource("shaders/default.shader", ".shader", source));
    ASSERT_TRUE(saveShaderSource("fixture:/nested/roundtrip.shader", ".shader", source));
    ASSERT_TRUE(loadShaderSource("fixture:/nested/roundtrip.shader", ".shader", restored));
    EXPECT_EQ(restored, source);
}

TEST_F(ShaderFileTest, ComputeSourceRoundTripsAndAppendsMissingExtension)
{
    const std::string source = "RWStructuredBuffer<uint> output : register(u0);\n"
        "[numthreads(8, 4, 1)] void CSMain(uint3 id : SV_DispatchThreadID) { output[id.x] = id.y; }\n";
    ASSERT_TRUE(saveShaderSource("fixture:/compute", ".coshader", source));
    std::string restored;
    ASSERT_TRUE(loadShaderSource("fixture:/compute.coshader", ".coshader", restored));
    EXPECT_EQ(restored, source);
    expectCompiles(restored, "CSMain", "cs_5_1");
}

TEST_F(ShaderFileTest, RejectsMismatchedExtensionsMissingFilesAndEmptySource)
{
    std::string source, restored;
    ASSERT_TRUE(loadShaderSource("shaders/default.shader", ".shader", source));
    EXPECT_FALSE(loadShaderSource("shaders/default.shader", ".coshader", restored));
    EXPECT_FALSE(saveShaderSource("fixture:/wrong.coshader", ".shader", source));
    EXPECT_FALSE(loadShaderSource("fixture:/missing.shader", ".shader", restored));
    EXPECT_FALSE(saveShaderSource("fixture:/empty.shader", ".shader", ""));
    EXPECT_FALSE(saveShaderSource("", ".shader", source));
}

TEST_F(ShaderFileTest, GameOverridesEngineAndExplicitPathsBypassOverrides)
{
    const auto engine = shaderFilePath("engine:/shaders/default.shader");
    ASSERT_FALSE(engine.empty());
    EXPECT_EQ(shaderFilePath("assets/shaders/default.shader"), engine);
    ASSERT_TRUE(files.mount("project", directory.path() / "project", true));
    // loadShaderSource ends every line with '\n' (include expansion), so the saved source does too.
    const std::string source = "project override\n";
    ASSERT_TRUE(saveShaderSource("shaders/default.shader", ".shader", source));
    EXPECT_EQ(shaderFilePath("shaders/default.shader"), std::filesystem::weakly_canonical(directory.path() / "project/shaders/default.shader"));
    EXPECT_EQ(shaderFilePath("engine:/shaders/default.shader"), engine);
    std::string restored;
    ASSERT_TRUE(loadShaderSource("shaders/default.shader", ".shader", restored));
    EXPECT_EQ(restored, source);
}
}
