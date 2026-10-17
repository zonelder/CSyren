#pragma once

#include <gtest/gtest.h>
#include "cstdmf/file_system.h"
#include "../test_directory.h"

namespace csyren::test
{
    class ShaderFilesFixture : public testing::Test
    {
    protected:
        void SetUp() override
        {
            savedMounts = files.mounts();
            for (const auto& mount : savedMounts)
                if (mount.name != "engine" && mount.name != "cache") files.unmount(mount.name);
#ifdef CSYREN_ENGINE_ASSETS
            const std::filesystem::path assets(CSYREN_ENGINE_ASSETS);
#else
            const auto assets = std::filesystem::absolute(std::filesystem::path(__FILE__).parent_path() / "../../assets");
#endif
            ASSERT_TRUE(files.mount("engine", assets, false));
            ASSERT_TRUE(files.mount("cache", directory.path() / "cache", true, false));
            ASSERT_TRUE(files.mount("fixture", directory.path() / "fixture", true, false));
        }

        void TearDown() override
        {
            for (const auto& mount : files.mounts())
                if (mount.name != "engine" && mount.name != "cache") files.unmount(mount.name);
            for (const auto& mount : savedMounts)
                EXPECT_TRUE(files.mount(mount.name, mount.root, mount.writable, mount.searchable));
        }

        TestDirectory directory;
        cstdmf::FileSystem& files = cstdmf::FileSystem::instance();
        std::vector<cstdmf::FileSystem::Mount> savedMounts;
    };
}
