#pragma once

#include <filesystem>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include "cstdmf/file_system.h"
#include "cstdmf/log.h"

namespace csyren::render::details
{
    // Compatibility helper; logical paths are resolved by the shared filesystem.
    inline std::filesystem::path shaderFilePath(const std::string& filepath)
    {
        const auto path = cstdmf::FileSystem::instance().resolveRead(filepath);
        return path.value_or(std::filesystem::path{});
    }

    // Textual '#include "file"' support (D3DCompile gets no include handler).
    // Paths resolve relative to the including file inside the asset filesystem.
    inline std::string expandShaderIncludes(const std::string& source, const std::filesystem::path& directory, int depth = 0)
    {
        if (depth > 8)
        {
            log::error("Shader include depth exceeded under {}", directory.generic_string());
            return source;
        }
        static const std::regex includeRegex(R"re(^\s*#include\s+"([^"]+)")re");
        std::string result;
        result.reserve(source.size());
        std::istringstream stream(source);
        std::string line;
        while (std::getline(stream, line))
        {
            std::smatch match;
            if (std::regex_search(line, match, includeRegex))
            {
                const auto path = (directory / match[1].str()).lexically_normal();
                std::string included;
                if (cstdmf::FileSystem::instance().readFile(path, included))
                {
                    result += "// --- begin include " + path.generic_string() + "\n";
                    result += expandShaderIncludes(included, path.parent_path(), depth + 1);
                    result += "\n// --- end include " + path.generic_string() + "\n";
                }
                else
                {
                    log::error("Shader include not found: {}", path.generic_string());
                    result += "#error missing include " + path.generic_string() + "\n";
                }
            }
            else
            {
                result += line;
                result += '\n';
            }
        }
        return result;
    }

    inline bool loadShaderSource(const std::string& filepath, const char* extension, std::string& source)
    {
        const std::filesystem::path path(filepath);
        if (path.extension() != extension)
            return false;
        std::string contents;
        if (!cstdmf::FileSystem::instance().readFile(path, contents) || contents.empty())
            return false;
        source = expandShaderIncludes(contents, path.parent_path());
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
