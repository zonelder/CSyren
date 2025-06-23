#include "pch.h"
#include "cstdmf/fixed_sparse_set.h"

using namespace csyren::cstdmf;
struct TrackedObj {
    static int count;
    int value;
    explicit TrackedObj(int v = 0) : value(v) { ++count; }
    ~TrackedObj() { --count; }
};
int TrackedObj::count = 0;

TEST(FixedSparseSetTest, BasicOperations) {
    FixedSparseSet<int, 5> set;
    auto id1 = set.emplace(10);
    auto id2 = set.emplace(20);

    EXPECT_TRUE(set.contains(id1));
    EXPECT_TRUE(set.contains(id2));
    EXPECT_EQ(*set.get(id1), 10);
    EXPECT_EQ(set[id2], 20);
    EXPECT_EQ(set.size(), 2);
}

TEST(FixedSparseSetTest, EraseReusesIDs)
{
    FixedSparseSet<int, 3> set;
    auto id1 = set.emplace(1);  // Should be ID 0 (last in free list)
    auto id2 = set.emplace(2);  // Should be ID 1

    set.erase(id1);  // Free ID 0
    auto id3 = set.emplace(3);  // Should reuse ID 0 (last freed)

    EXPECT_EQ(id3, id1);  // Same ID reused
    EXPECT_TRUE(set.contains(id1));  // Now valid again
    EXPECT_EQ(set[id1], 3);  // New value
    EXPECT_EQ(set.size(), 2);
}

TEST(FixedSparseSetTest, LIFOReuseOrder)
{
    FixedSparseSet<int, 5> set;
    auto id1 = set.emplace(1);  // ID 0
    auto id2 = set.emplace(2);  // ID 1
    auto id3 = set.emplace(3);  // ID 2

    // Erase in order 1-2-3
    set.erase(id1);
    set.erase(id2);
    set.erase(id3);

    // Should reuse in reverse order 3-2-1
    auto id4 = set.emplace(4);
    auto id5 = set.emplace(5);
    auto id6 = set.emplace(6);

    EXPECT_EQ(id4, id3);  // Last erased first reused
    EXPECT_EQ(id5, id2);
    EXPECT_EQ(id6, id1);
}

TEST(FixedSparseSetTest, ObjectLifetime) {
    TrackedObj::count = 0;
    {
        FixedSparseSet<TrackedObj, 3> set;
        auto id1 = set.emplace(1);
        auto id2 = set.emplace(2);
        EXPECT_EQ(TrackedObj::count, 2);

        set.erase(id1);
        EXPECT_EQ(TrackedObj::count, 1);

        auto id3 = set.emplace(3);
        EXPECT_EQ(TrackedObj::count, 2);
    }
    EXPECT_EQ(TrackedObj::count, 0);  // All destroyed
}

TEST(FixedSparseSetTest, IteratorValidity) {
    FixedSparseSet<int, 5> set;
    std::vector<size_t> ids;
    for (int i = 0; i < 5; ++i)
        ids.push_back(set.emplace(i));

    // Erase middle element
    set.erase(ids[2]);

    std::vector<int> values;
    for (const auto& v : set)
        values.push_back(v);

    EXPECT_EQ(values, (std::vector{ 0, 1, 4, 3 }));  // Last element moved
}

TEST(FixedSparseSetTest, CapacityLimits) 
{
    using Set = FixedSparseSet<int, 2>;
    Set set;
    auto id1 = set.emplace(1);
    auto id2 = set.emplace(2);
    auto id3 = set.emplace(3);  // Should fail

    EXPECT_NE(id1, Set::invalidID);
    EXPECT_NE(id2, Set::invalidID);
    EXPECT_EQ(id3, Set::invalidID);
    EXPECT_EQ(set.size(), 2);
}

TEST(FixedSparseSetTest, ClearResetsState) 
{
    using Set = FixedSparseSet<int, 5>;
    Set set;
    set.emplace(1);
    set.emplace(2);
    set.clear();

    EXPECT_EQ(set.size(), 0);
    EXPECT_TRUE(set.emplace(3) != Set::invalidID);  // Should reuse
    EXPECT_EQ(set.size(), 1);
}

TEST(FixedSparseSetTest, AccessInvalidID) 
{
    FixedSparseSet<int, 3> set;
    auto id = set.emplace(10);
    set.erase(id);

    EXPECT_EQ(set.get(id), nullptr);
    EXPECT_THROW({ set[id]; }, std::out_of_range);
}