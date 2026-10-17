#include "pch.h"
#include "cstdmf/file_system.h"
#include "../test_directory.h"
#include <array>

namespace
{
using csyren::cstdmf::FileSystem;
namespace fs = std::filesystem;

class FileSystemTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(files.writeFile("engine:/shared.txt", "engine"));
        ASSERT_TRUE(files.writeFile("engine:/fallback.txt", "fallback"));
        ASSERT_TRUE(files.mount("project", directory.path() / "project", true));
        ASSERT_TRUE(files.writeFile("shared.txt", "project"));
    }

    csyren::test::TestDirectory directory;
    FileSystem files{directory.path() / "engine", directory.path() / "cache"};
};

TEST_F(FileSystemTest, SearchesProjectBeforeEngineAndSupportsLegacyPrefix)
{
    std::string source;
    ASSERT_TRUE(files.readFile("shared.txt", source));
    EXPECT_EQ(source, "project");
    ASSERT_TRUE(files.readFile("assets/shared.txt", source));
    EXPECT_EQ(source, "project");
    ASSERT_TRUE(files.readFile("engine:/shared.txt", source));
    EXPECT_EQ(source, "engine");
    ASSERT_TRUE(files.readFile("fallback.txt", source));
    EXPECT_EQ(source, "fallback");
}

TEST_F(FileSystemTest, RemountingEnginePreservesProjectPriority)
{
    ASSERT_TRUE(files.mount("engine", directory.path() / "engine", false));
    std::string source;
    ASSERT_TRUE(files.readFile("shared.txt", source));
    EXPECT_EQ(source, "project");
    EXPECT_FALSE(files.writeFile("engine:/shared.txt", "bad"));
}

TEST_F(FileSystemTest, CreatesParentDirectoriesAndWritesIntoProject)
{
    ASSERT_TRUE(files.writeFile("nested/new.txt", "new"));
    EXPECT_TRUE(files.exists("project:/nested/new.txt"));
    EXPECT_FALSE(files.exists("engine:/nested/new.txt"));
}

TEST_F(FileSystemTest, CacheAndNamedOnlyMountsDoNotOverrideAssetSearch)
{
    ASSERT_TRUE(files.writeFile("cache:/shared.txt", "cached"));
    ASSERT_TRUE(files.mount("named", directory.path() / "cache", true, false));
    std::string source;
    ASSERT_TRUE(files.readFile("named:/shared.txt", source));
    EXPECT_EQ(source, "cached");
    ASSERT_TRUE(files.readFile("shared.txt", source));
    EXPECT_EQ(source, "project");
}

TEST_F(FileSystemTest, RejectsWritesToReadOnlyMountsAndUnknownRoots)
{
    ASSERT_TRUE(files.mount("readonly", directory.path() / "project", false));
    EXPECT_FALSE(files.writeFile("readonly:/shared.txt", "bad"));
    EXPECT_FALSE(files.resolveRead("missing:/shared.txt"));
    EXPECT_FALSE(files.resolveWrite("missing:/shared.txt"));
    EXPECT_FALSE(files.mount("x", directory.path()));
}

TEST_F(FileSystemTest, RejectsEmptyPathsRootDirectoriesAndEscapes)
{
    for (const auto* path : {"", "../escape.txt", "project:/../../escape.txt", "project:/", "project://escape.txt", "engine:/../escape.txt"})
    {
        SCOPED_TRACE(path);
        EXPECT_FALSE(files.resolveRead(path));
        EXPECT_FALSE(files.resolveWrite(path));
    }
}

TEST_F(FileSystemTest, FailedReadPreservesOutput)
{
    std::string source = "unchanged";
    EXPECT_FALSE(files.readFile("does_not_exist.txt", source));
    EXPECT_EQ(source, "unchanged");
}

TEST_F(FileSystemTest, RoundTripsRawBytesAndEmptyFiles)
{
    const std::array<std::byte, 4> bytes{std::byte{0}, std::byte{255}, std::byte{13}, std::byte{10}};
    ASSERT_TRUE(files.writeFile("binary/data.bin", bytes));
    std::vector<std::byte> restored;
    ASSERT_TRUE(files.readFile("binary/data.bin", restored));
    EXPECT_EQ(restored, (std::vector<std::byte>(bytes.begin(), bytes.end())));
    ASSERT_TRUE(files.writeFile("empty.txt", ""));
    std::string source = "old";
    ASSERT_TRUE(files.readFile("empty.txt", source));
    EXPECT_TRUE(source.empty());
}

TEST_F(FileSystemTest, SupportsUnicodeAndAbsolutePaths)
{
    const fs::path unicode(L"project:/unicode/\u0442\u0435\u0441\u0442.txt");
    ASSERT_TRUE(files.writeFile(unicode, "unicode"));
    std::string source;
    ASSERT_TRUE(files.readFile(unicode, source));
    EXPECT_EQ(source, "unicode");
    const auto absolute = directory.path() / "external.txt";
    ASSERT_TRUE(files.writeFile(absolute, "external"));
    ASSERT_TRUE(files.readFile(absolute, source));
    EXPECT_EQ(source, "external");
}

TEST_F(FileSystemTest, RootsRemainStableAfterWorkingDirectoryChanges)
{
    struct RestoreDirectory
    {
        fs::path original = fs::current_path();
        ~RestoreDirectory() { std::error_code error; fs::current_path(original, error); }
    } restore;
    fs::current_path(directory.path());
    std::string source;
    ASSERT_TRUE(files.readFile("engine:/shared.txt", source));
    EXPECT_EQ(source, "engine");
}

TEST_F(FileSystemTest, UnmountRestoresFallbackAndReservedRootsRemainMounted)
{
    ASSERT_TRUE(files.unmount("project"));
    std::string source;
    ASSERT_TRUE(files.readFile("shared.txt", source));
    EXPECT_EQ(source, "engine");
    EXPECT_FALSE(files.unmount("engine"));
    EXPECT_FALSE(files.unmount("cache"));
    ASSERT_TRUE(files.writeFile("cache:/remove.txt", "cached"));
    ASSERT_TRUE(files.remove("cache:/remove.txt"));
    EXPECT_FALSE(files.exists("cache:/remove.txt"));
}

TEST_F(FileSystemTest, RejectsEscapesThroughDirectorySymlinks)
{
    std::error_code error;
    fs::create_directory_symlink(directory.path() / "project", directory.path() / "engine/link", error);
    if (error)
    {
        // Google Test 1.8 has no GTEST_SKIP; report the OS restriction explicitly.
        SUCCEED() << "Symlink check unavailable: " << error.message();
        return;
    }
    EXPECT_FALSE(files.resolveRead("engine:/link/shared.txt"));
    EXPECT_FALSE(files.resolveWrite("engine:/link/escape.txt"));
}
}
