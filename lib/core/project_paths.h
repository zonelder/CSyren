#pragma once
#include "services.h"
#include <filesystem>
#include <string>

namespace csyren::core
{
    inline std::filesystem::path pathFromUtf8(const std::string& value)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
    }
    inline std::string pathToUtf8(const std::filesystem::path& value)
    {
        const auto text = value.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }
    struct ProjectPaths
    {
        std::filesystem::path root;
        std::filesystem::path assets;
        std::filesystem::path resolve(const std::filesystem::path& path) const { return (root / path).lexically_normal(); }
        std::filesystem::path asset(const std::filesystem::path& path) const { return (assets / path).lexically_normal(); }
        std::filesystem::path build(const std::filesystem::path& path) const { return (root / "build" / path).lexically_normal(); }
    };
    inline std::filesystem::path projectAssetPath(const std::filesystem::path& path)
    {
        if (Services::has<ProjectPaths>()) return Services::get<ProjectPaths>()->asset(path);
        return std::filesystem::path("assets") / path;
    }
    inline std::filesystem::path projectBuildPath(const std::filesystem::path& path)
    {
        if (Services::has<ProjectPaths>()) return Services::get<ProjectPaths>()->build(path);
        return std::filesystem::path("build") / path;
    }
}
