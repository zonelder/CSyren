#pragma once

#include "forward_decl.h"
#include "shader_meta.h"

#include <d3d12.h>
#include <wrl.h>
#include <d3d12shader.h>

#include <array>
#include <filesystem>
#include <span>
#include <unordered_map>
#include <vector>
#include <string>

namespace csyren::render
{
	struct from_asset_path_t {};
	inline constexpr from_asset_path_t from_asset_path{};

	struct from_source_code_t {};
	inline constexpr from_source_code_t from_source_code{};

	struct ShaderResourceInfo
	{
		std::string name;
		UINT shaderRegister;
		UINT rootParameterIndex;
		UINT registerSpace;
	};

	struct ConstantBufferVariableInfo
	{
		std::string bufferName;
		UINT offset;
		UINT size;
		std::vector<uint8_t> defaultValue;
		bool needsTranspose;
	};

    enum class ShaderType { Graphics, Compute };
    enum class ShaderStage { Vertex, Pixel, Geometry, Hull, Domain, Compute, Count };

    class ShaderBase
    {
    public:
        virtual ~ShaderBase() = default;
        ShaderType getType() const noexcept { return _type; }

        bool init(from_source_code_t, const std::string& code);
        bool init(from_asset_path_t, const std::string& filepath);
        bool init(const std::string& filepath);
        bool init(Renderer& renderer, from_source_code_t, const std::string& code);
        bool init(Renderer& renderer, from_asset_path_t, const std::string& filepath);
        bool init(Renderer& renderer, const std::string& filepath);
        bool init(ID3D12Device* device, from_source_code_t, const std::string& code);
        bool init(ID3D12Device* device, from_asset_path_t, const std::string& filepath);
        bool init(ID3D12Device* device, const std::string& filepath);
        bool save(const std::string& filepath) const;

        ID3D12RootSignature* getRootSignature() const { return _rootSignature.Get(); }
        UINT getRootParameterIndex(const std::string& resourceName) const;
        D3D12_SHADER_BYTECODE getBytecode(ShaderStage stage) const noexcept;

        bool buildRootSignatureFromReflection(ID3D12Device* device,
            const std::vector<std::pair<D3D12_SHADER_VISIBILITY, ID3DBlob*>>& shaderBlobs);

        static Microsoft::WRL::ComPtr<ID3DBlob> compileShader(
            const std::string& source, const char* target,
            const std::string& entryPoint, bool ignoreMissing = false);

    protected:
        ShaderBase(ShaderBase&&) noexcept = default;
        ShaderBase& operator=(ShaderBase&&) noexcept = default;
        ShaderBase(const ShaderBase&) = delete;
        ShaderBase& operator=(const ShaderBase&) = delete;
        explicit ShaderBase(ShaderType type) : _type(type) {}
        virtual bool finalizeInit(ID3D12Device* device) = 0;
        ID3DBlob* stageBlob(ShaderStage stage) const noexcept;

        Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
        std::unordered_map<std::string, ShaderResourceInfo> _resourceMap;
        std::unordered_map<std::string, ConstantBufferVariableInfo> _variableInfoMap;
        std::unordered_map<std::string, UINT> _constantBufferSizes;
        ShaderMetaPtr _meta;

    private:
        struct StageDefinition
        {
            ShaderStage stage;
            const char* target;
            const char* entryPoint;
            const char* cacheSuffix;
            D3D12_SHADER_VISIBILITY visibility;
            bool optional;
        };
        struct CompileResult
        {
            Microsoft::WRL::ComPtr<ID3DBlob> blob;
            bool success;
        };
        static CompileResult compileStage(const std::string& source, const char* target,
            const std::string& entryPoint, bool ignoreMissing);
        std::span<const StageDefinition> stageDefinitions() const noexcept;
        const char* sourceExtension() const noexcept;
        bool compileAndInit(ID3D12Device* device, const std::string& code);
        bool finishInit(ID3D12Device* device);
        bool savePrecompiled(const std::filesystem::path& relativePath) const;
        bool loadPrecompiledAndInit(ID3D12Device* device, const std::filesystem::path& relativePath);

        ShaderType _type;
        std::array<Microsoft::WRL::ComPtr<ID3DBlob>, static_cast<size_t>(ShaderStage::Count)> _stageBlobs;
        std::string _sourceCode;
    };
}
