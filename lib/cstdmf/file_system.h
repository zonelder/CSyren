#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace csyren::cstdmf
{
    // Logical paths: shaders/default.shader, engine:/..., project:/..., cache:/...
    // Configure before loading resources. Roots are captured as absolute paths.
    class FileSystem
    {
    public:
        struct Mount
        {
            std::string name;
            std::filesystem::path root;
            bool writable = false;
            bool searchable = true;
        };

        explicit FileSystem(const std::filesystem::path& assets = "assets",
                            const std::filesystem::path& cache = "build")
        {
            mount("engine", assets, true);
            mount("cache", cache, true, false);
        }

        static FileSystem& instance()
        {
            static FileSystem files;
            return files;
        }

        // Last added searchable root wins; engine always remains the fallback.
        bool mount(std::string name, const std::filesystem::path& root,
                   bool writable = false, bool searchable = true)
        {
            if (name.size() < 2 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos || root.empty())
                return false;
            std::error_code error;
            auto absolute = std::filesystem::absolute(root, error).lexically_normal();
            if (error) return false;
            std::unique_lock lock(_mutex);
            std::erase_if(_mounts, [&](const Mount& m) { return m.name == name; });
            Mount entry{std::move(name), std::move(absolute), writable, searchable};
            if (entry.name == "engine") _mounts.insert(_mounts.begin(), std::move(entry));
            else _mounts.push_back(std::move(entry));
            return true;
        }

        bool unmount(std::string_view name)
        {
            if (name == "engine" || name == "cache") return false;
            std::unique_lock lock(_mutex);
            return std::erase_if(_mounts, [&](const Mount& m) { return m.name == name; }) != 0;
        }

        std::vector<Mount> mounts() const
        {
            std::shared_lock lock(_mutex);
            return _mounts;
        }

        // Physical paths are an explicit compatibility escape hatch for external tools.
        std::optional<std::filesystem::path> resolveRead(const std::filesystem::path& path) const
        {
            return resolve(path, false);
        }

        std::optional<std::filesystem::path> resolveWrite(const std::filesystem::path& path) const
        {
            return resolve(path, true);
        }

        bool exists(const std::filesystem::path& path) const { return resolveRead(path).has_value(); }

        // Read the entire file as raw bytes. No encoding or newline conversion.
        // On failure, output remains unchanged.
        bool readFile(const std::filesystem::path& path, std::string& output) const
        {
            const auto physical = resolveRead(path);
            if (!physical) return false;
            std::ifstream file(*physical, std::ios::binary);
            if (!file) return false;
            std::string contents((std::istreambuf_iterator<char>(file)), {});
            if (file.bad()) return false;
            output = std::move(contents);
            return true;
        }

        bool readFile(const std::filesystem::path& path, std::vector<std::byte>& output) const
        {
            std::string bytes;
            if (!readFile(path, bytes)) return false;
            const auto data = std::as_bytes(std::span(bytes.data(), bytes.size()));
            output.assign(data.begin(), data.end());
            return true;
        }

        // Create or overwrite the entire file, creating parent directories as needed.
        bool writeFile(const std::filesystem::path& path, std::string_view contents) const
        {
            const auto physical = resolveWrite(path);
            if (!physical) return false;
            std::error_code error;
            std::filesystem::create_directories(physical->parent_path(), error);
            if (error) return false;
            std::ofstream file(*physical, std::ios::binary | std::ios::trunc);
            if (!file) return false;
            file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            file.close();
            return !file.fail();
        }

        bool writeFile(const std::filesystem::path& path, std::span<const std::byte> bytes) const
        {
            return writeFile(path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        }

        bool remove(const std::filesystem::path& path) const
        {
            const auto physical = resolveWrite(path);
            if (!physical) return false;
            std::error_code error;
            std::filesystem::remove(*physical, error);
            return !error;
        }

    private:
        static std::optional<std::filesystem::path> inside(const Mount& mount, const std::filesystem::path& relative)
        {
            if (relative.empty() || relative.has_root_path()) return {};
            auto normalized = relative.lexically_normal();
            for (const auto& part : normalized)
                if (part == "..") return {};
            // Also reject escapes through directory symlinks/junctions.
            std::error_code error;
            auto root = std::filesystem::weakly_canonical(mount.root, error);
            if (error) return {};
            auto candidate = std::filesystem::weakly_canonical(root / normalized, error);
            if (error) return {};
            auto r = root.begin(), p = candidate.begin();
            for (; r != root.end(); ++r, ++p)
                if (p == candidate.end() || *r != *p) return {};
            if (p == candidate.end()) return {}; // A root itself is not a file.
            return candidate;
        }

        std::optional<std::filesystem::path> resolve(const std::filesystem::path& input, bool writing) const
        {
            if (input.empty()) return {};
            auto path = input;
            auto text = path.generic_wstring();
            std::string name;
            const auto separator = text.find(L":/");
            // One-letter prefixes remain Windows drive paths.
            if (separator != std::wstring::npos && separator > 1)
            {
                const auto prefix = text.substr(0, separator);
                if (prefix.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::wstring::npos)
                    return {};
                for (const auto character : prefix) name.push_back(static_cast<char>(character));
                path = text.substr(separator + 2);
            }
            else if (path.is_absolute())
            {
                std::error_code error;
                if (writing || std::filesystem::is_regular_file(path, error)) return path.lexically_normal();
                return {};
            }
            else if (path.has_root_path()) return {};
            else if (*path.begin() == "assets")
            {
                // Legacy assets/ spelling is equivalent to an unqualified asset path.
                path = path.lexically_relative("assets");
            }
            std::shared_lock lock(_mutex);
            for (auto it = _mounts.rbegin(); it != _mounts.rend(); ++it)
            {
                if ((!name.empty() && it->name != name) || (name.empty() && !it->searchable)) continue;
                if (writing && !it->writable) continue;
                auto candidate = inside(*it, path);
                if (!candidate) continue;
                std::error_code error;
                if (writing || std::filesystem::is_regular_file(*candidate, error)) return candidate;
            }
            return {};
        }

        mutable std::shared_mutex _mutex;
        std::vector<Mount> _mounts;
    };
}
