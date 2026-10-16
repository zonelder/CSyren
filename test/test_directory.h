#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace csyren::test
{
    class TestDirectory
    {
    public:
        TestDirectory() : _root(std::filesystem::temp_directory_path())
        {
            static std::atomic<unsigned> sequence{0};
            for (unsigned attempt = 0; attempt != 100; ++attempt)
            {
                _path = _root / ("csyren-test-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count())
                    + "-" + std::to_string(sequence.fetch_add(1)));
                if (std::filesystem::create_directory(_path)) return;
            }
            throw std::runtime_error("Could not create a unique test directory");
        }

        ~TestDirectory()
        {
            std::error_code error;
            // Delete only the unique directory created by this instance.
            if (_path.parent_path() == _root && _path.filename().string().starts_with("csyren-test-"))
                std::filesystem::remove_all(_path, error);
        }

        TestDirectory(const TestDirectory&) = delete;
        TestDirectory& operator=(const TestDirectory&) = delete;
        const std::filesystem::path& path() const { return _path; }

    private:
        std::filesystem::path _root;
        std::filesystem::path _path;
    };
}
