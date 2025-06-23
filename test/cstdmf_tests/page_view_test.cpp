#include "pch.h"
#include "cstdmf/page_view.h"
#include <algorithm>
#include <random>
#include <vector>

using namespace csyren::cstdmf;

TEST(PageViewTest, SinglePageOperations) 
{
    PageView<int> view;
    auto id1 = view.emplace(10);
    auto id2 = view.emplace(20);

    EXPECT_TRUE(view.contains(id1));
    EXPECT_EQ(*view.get(id1), 10);
    EXPECT_EQ(view.size(), 2);

    view.erase(id1);
    EXPECT_FALSE(view.contains(id1));
    EXPECT_EQ(view.size(), 1);
}

TEST(PageViewTest, MultiPageAllocation) 
{
    PageView<int, 2> view;  // Small pages
    auto id1 = view.emplace(1);
    auto id2 = view.emplace(2);
    auto id3 = view.emplace(3);  // Should create new page

    auto [page1, idx1] = PageView<int, 2>::decode_id(id1);
    auto [page2, idx2] = PageView<int, 2>::decode_id(id2);
    auto [page3, idx3] = PageView<int, 2>::decode_id(id3);

    EXPECT_EQ(page1, page2);   // Same page
    EXPECT_NE(page1, page3);   // Different page
    EXPECT_EQ(view.size(), 3);
}

TEST(PageViewTest, IDReuseAcrossPages) {
    PageView<int, 2> view;
    auto id1 = view.emplace(1);
    auto id2 = view.emplace(2);
    view.erase(id1);

    auto id3 = view.emplace(3);  // Should reuse id1's slot

    auto [page1, idx1] = PageView<int, 2>::decode_id(id1);
    auto [page3, idx3] = PageView<int, 2>::decode_id(id3);

    EXPECT_EQ(page1, page3);
    EXPECT_EQ(idx1, idx3);
}

TEST(PageViewTest, FullPageManagement) 
{
    PageView<int, 2> view;
    auto id1 = view.emplace(1);
    auto id2 = view.emplace(2);
    auto id3 = view.emplace(3);  // New page

    view.erase(id1);  // Create space in first page
    auto id4 = view.emplace(4);

    auto [page4, idx4] = PageView<int, 2>::decode_id(id4);
    auto [page1, idx1] = PageView<int, 2>::decode_id(id1);

    EXPECT_EQ(page4, page1);  // Should use freed space
    EXPECT_EQ(view.size(), 3);
}

TEST(PageViewTest, ReservePreallocatesPages) 
{
    using PV = PageView<int, 2>;
    PV view;
    view.reserve(5);  // Requires 3 pages (ceil(5/2))

    EXPECT_GE(view.capacity(), 6);
    EXPECT_EQ(view.size(), 0);

    // Should populate preallocated pages
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_NE(view.emplace(i), PV::invalidID);
    }
}

TEST(PageViewTest, InvalidAccessHandling) {
    PageView<int> view;
    auto valid_id = view.emplace(10);
    auto invalid_id = valid_id + 1000;  // Invalid ID

    EXPECT_FALSE(view.contains(invalid_id));
    EXPECT_EQ(view.get(invalid_id), nullptr);
    EXPECT_THROW({ view.at(invalid_id); }, std::out_of_range);
}
struct Tracked
{
    static int count;
    Tracked() { ++count; }
    ~Tracked() { --count; }
};
int Tracked::count = 0;

TEST(PageViewTest, ObjectLifetime)
{

    PageView<Tracked, 2> view;
    auto id1 = view.emplace();
    auto id2 = view.emplace();
    auto id3 = view.emplace();  // New page

    EXPECT_EQ(Tracked::count, 3);

    view.erase(id2);
    EXPECT_EQ(Tracked::count, 2);

    view.clear();
    EXPECT_EQ(Tracked::count, 0);

}

TEST(PageViewTest, StableReferences) 
{
    PageView<std::string> view;
    auto id1 = view.emplace("Hello");
    auto id2 = view.emplace("World");

    // Store raw pointers
    const auto* ptr1 = view.get(id1);
    const auto* ptr2 = view.get(id2);

    // Force reallocations
    for (int i = 0; i < 100; ++i)
        view.emplace("Temp" + std::to_string(i));

    // Pointers should remain valid
    EXPECT_EQ(*ptr1, "Hello");
    EXPECT_EQ(*ptr2, "World");

    // Cleanup
    view.clear();
}