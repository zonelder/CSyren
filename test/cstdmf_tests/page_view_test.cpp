#include "pch.h"
#include "pch.h"
#include "cstdmf/page_view.h"
#include <algorithm>
#include <vector>

using namespace csyren::cstdmf;

TEST(PageViewTest, EmplaceAndGet)
{
    PageView<int, 2> view;
    auto id1 = view.emplace(10);
    auto id2 = view.emplace(20);
    EXPECT_EQ(*view.get(id1), 10);
    EXPECT_EQ(*view.get(id2), 20);
    EXPECT_TRUE(view.erase(id1));
    EXPECT_EQ(view.get(id1), nullptr);
}

TEST(PageViewTest, MultiPageOperations)
{
    PageView<int, 2> view;
    auto ids = std::vector<PageView<int, 2>::ID>{};
    for (int i = 0; i < 5; ++i)
        ids.push_back(view.emplace(i));

    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(*view.get(ids[i]), i);

    EXPECT_TRUE(view.erase(ids[1]));
    EXPECT_EQ(view.get(ids[1]), nullptr);

    auto newId = view.emplace(42);
    EXPECT_EQ(*view.get(newId), 42);

    view.clear();
    EXPECT_EQ(view.get(ids[0]), nullptr);
}

TEST(PageViewTest, SizeAndIteration)
{
    PageView<int, 2> view;
    for (int i = 0; i < 5; ++i)
        view.emplace(i);

    EXPECT_EQ(view.size(), 5u);

    std::vector<int> collected;
    for (int v : view)
        collected.push_back(v);

    std::sort(collected.begin(), collected.end());

    EXPECT_EQ(collected.size(), 5u);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(collected[i], i);
}

TEST(PageViewTest, ConstIteration)
{
    PageView<int, 2> view;
    for (int i = 0; i < 5; ++i)
        view.emplace(i);

    const PageView<int, 2>& constView = view;
    int sum = 0;
    for (const int& v : constView)
        sum += v;

    EXPECT_EQ(sum, 10); // 0+1+2+3+4
}
