#include "pch.h"
#include "cstdmf/fixed_sparse_set.h"

using namespace csyren::cstdmf;

TEST(FixedSparseSetTest, BasicInsertErase)
{
    FixedSparseSet<int, 4> set;
    EXPECT_TRUE(set.emplace(0, 10));
    EXPECT_TRUE(set.contains(0));
    EXPECT_EQ(set.size(), 1u);
    EXPECT_FALSE(set.emplace(0, 20));
    EXPECT_EQ(*set.get(0), 10);
    EXPECT_TRUE(set.erase(0));
    EXPECT_FALSE(set.contains(0));
    EXPECT_EQ(set.size(), 0u);
}

TEST(FixedSparseSetTest, CapacityAndClear)
{
    FixedSparseSet<int, 2> set;
    EXPECT_TRUE(set.emplace(0, 10));
    EXPECT_TRUE(set.emplace(1, 20));
    EXPECT_FALSE(set.emplace(2, 30));
    EXPECT_EQ(set.size(), 2u);

    set.clear();
    EXPECT_EQ(set.size(), 0u);
    EXPECT_FALSE(set.contains(0));
    EXPECT_TRUE(set.emplace(0, 40));
    EXPECT_EQ(*set.get(0), 40);
}

TEST(FixedSparseSetTest, InvalidAccess)
{
    FixedSparseSet<int, 1> set;
    EXPECT_EQ(set.get(0), nullptr);
    EXPECT_FALSE(set.erase(0));
    EXPECT_THROW((void)set[0], std::out_of_range);
}

TEST(FixedSparseSetTest, CopySemantics)
{
    FixedSparseSet<int, 4> set;
    set.emplace(0, 1);
    set.emplace(1, 2);

    FixedSparseSet<int, 4> copy = set;
    EXPECT_EQ(copy.size(), set.size());
    EXPECT_EQ(*copy.get(0), 1);
    EXPECT_EQ(*copy.get(1), 2);

    copy.erase(0);
    EXPECT_TRUE(set.contains(0));
    EXPECT_FALSE(copy.contains(0));
}