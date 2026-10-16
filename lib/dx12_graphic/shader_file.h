#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include "cstdmf/file_system.h"

namespace csyren::render::details
{
    // Compatibility helper; logical paths are resolved by the shared filesystem.
    inline std::filesystem::path shaderFilePath(const std::string& filepath)
    {
        const auto path = cstdmf::FileSystem::instance().resolveRead(filepath);
        return path.value_or(std::filesystem::path{});
    }

    inline bool loadShaderSource(const std::string& filepath, const char* extension, std::string& source)
    {
        const std::filesystem::path path(filepath);
        if (path.extension() != extension)
            return false;
        std::string contents;
        if (!cstdmf::FileSystem::instance().readFile(path, contents) || contents.empty())
            return false;
        source = std::move(contents);
        return true;
    }

    inline bool saveShaderSource(const std::string& filepath, const char* extension, const std::string& source)
    {
        std::filesystem::path path(filepath);
        if (filepath.empty() || source.empty())
            return false;
        if (!path.has_extension())
            path += extension;
        if (path.extension() != extension)
            return false;
        return cstdmf::FileSystem::instance().writeFile(path, source);
    }
}
