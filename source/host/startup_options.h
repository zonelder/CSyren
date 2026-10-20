#pragma once
#include "core/project_paths.h"
#include <span>
#include <string_view>
#include <stdexcept>

namespace csyren
{
    struct StartupOptions
    {
        std::filesystem::path project = "CSyren.csyrenproj";
        bool console = false; // --console: attach a console window that mirrors the log

        // Arguments exclude argv[0]. Only named options select a project.
        static StartupOptions parse(std::span<const std::wstring_view> arguments)
        {
            StartupOptions options;
            bool projectSpecified = false;
            for (size_t i = 0; i < arguments.size(); ++i)
            {
                const auto argument = arguments[i];
                if (argument == L"--console")
                {
                    options.console = true;
                    continue;
                }
                std::wstring_view value;
                if (argument == L"--project")
                {
                    if (++i == arguments.size() || arguments[i].starts_with(L"--"))
                        throw std::invalid_argument("Missing value for --project");
                    value = arguments[i];
                }
                else if (argument.starts_with(L"--project=")) value = argument.substr(10);
                else throw std::invalid_argument("Unknown argument: " + core::pathToUtf8(std::filesystem::path(argument)));
                if (projectSpecified) throw std::invalid_argument("Duplicate --project argument");
                if (value.empty()) throw std::invalid_argument("Empty value for --project");
                options.project = std::filesystem::path(value);
                projectSpecified = true;
            }
            return options;
        }
    };
}
