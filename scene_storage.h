#pragma once
#include "core/scene.h"
#include <string>

namespace csyren
{
    // Session logic is independent of archive format and renderer services.
    class SceneStorage
    {
    public:
        virtual ~SceneStorage() = default;
        virtual bool read(const std::string& path, core::Scene&) = 0; // append/import
        virtual bool write(const std::string& path, core::Scene&) = 0;
        virtual bool snapshot(core::Scene&, std::string&) = 0;
        virtual bool restore(const std::string&, core::Scene&) = 0;
    };
}
