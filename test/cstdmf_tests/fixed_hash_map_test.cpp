#include "pch.h"
#include "cstdmf/fixed_hash_map.h"
#include "math/vector3.h"
#include <chrono>
#include <algorithm>

using namespace csyren::cstdmf;
using namespace csyren::math;


TEST(FixedHashMapTest, MinCapacity)
{
    FixedHashMap<int, int, 1> map;

    EXPECT_TRUE(map.insertOrAssign(10, 100));
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(*map.find(10), 100);


    EXPECT_FALSE(map.insertOrAssign(20, 200));
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(*map.find(10), 100);
}

TEST(FixedHashMapTest, MaxCapacity)
{
    constexpr uint32_t MAX_CAP = 128;
    FixedHashMap<int, int, MAX_CAP> map;

    for (int i = 0; i < MAX_CAP; ++i)
    {
        EXPECT_TRUE(map.insertOrAssign(i, i * 10));
    }
    EXPECT_EQ(map.size(), MAX_CAP);

    EXPECT_FALSE(map.insertOrAssign(999, 9990));
    EXPECT_EQ(map.size(), MAX_CAP);
}

TEST(FixedHashMapTest, ClearAndReuse)
{
    FixedHashMap<int, int, 16> map;
    map.insertOrAssign(1, 100);
    map.insertOrAssign(2, 200);
    map.clear();

    EXPECT_EQ(map.size(), 0);
    EXPECT_TRUE(map.empty());

    EXPECT_TRUE(map.insertOrAssign(3, 300));
    EXPECT_EQ(*map.find(3), 300);
}

struct ZeroHash
{
    size_t operator()(int) const noexcept { return 0; }
};

TEST(FixedHashMapTest, ExtremeCollisions)
{
    FixedHashMap<int, int, 16, ZeroHash> map;

    for (int i = 1; i <= 8; ++i)
    {
        EXPECT_TRUE(map.insertOrAssign(i, i * 100));
    }

    for (int i = 1; i <= 8; ++i)
    {
        EXPECT_TRUE(map.contains(i));
        EXPECT_EQ(*map.find(i), i * 100);
    }

    EXPECT_EQ(map.size(), 8);
}

// ============================================================================
// 3. ИТЕРАЦИЯ И ПЛОТНОСТЬ ДАННЫХ (DENSE PACKING)
// ============================================================================

TEST(FixedHashMapTest, DenseIterationOrder)
{
    FixedHashMap<int, float, 32> map;

    // Вставляем в "случайном" порядке
    map.insertOrAssign(5, 5.5f);
    map.insertOrAssign(1, 1.1f);
    map.insertOrAssign(9, 9.9f);

    int count = 0;
    std::vector<std::pair<int, float>> iterated_values;

    for (const auto& [key, value] : map)
    {
        count++;
        iterated_values.emplace_back(key, value);
    }

    EXPECT_EQ(count, 3);
    EXPECT_EQ(map.size(), 3);

    EXPECT_EQ(iterated_values[0].first, 5);
    EXPECT_EQ(iterated_values[1].first, 1);
    EXPECT_EQ(iterated_values[2].first, 9);
}

TEST(FixedHashMapTest, EmptyIteration)
{
    FixedHashMap<int, int, 16> map;
    int count = 0;
    for (auto& [k, v] : map) { count++; }
    EXPECT_EQ(count, 0);
}

struct PodStruct
{
    int id;
    float weight;
    Vector3 position;
};
static_assert(std::is_trivially_copyable_v<PodStruct>, "PodStruct must be trivially copyable");

TEST(FixedHashMapTest, ComplexTriviallyCopyableType)
{
    FixedHashMap<uint32_t, PodStruct, 16> map;

    PodStruct p1{ 1, 1.5f, Vector3(10.0f, 20.0f, 30.0f) };
    map.insertOrAssign(100, p1);

    const PodStruct* found = map.find(100);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, 1);
    EXPECT_FLOAT_EQ(found->weight, 1.5f);
    EXPECT_FLOAT_EQ(found->position.x, 10.0f);
}


TEST(FixedHashMapTest, CopyAndMoveSemantics)
{
    FixedHashMap<int, int, 16> map1;
    map1.insertOrAssign(1, 10);
    map1.insertOrAssign(2, 20);

    FixedHashMap<int, int, 16> map2 = map1;
    EXPECT_EQ(*map2.find(1), 10);

    map1.insertOrAssign(3, 30);
    EXPECT_FALSE(map2.contains(3));
    EXPECT_EQ(map1.size(), 3);
    EXPECT_EQ(map2.size(), 2);

    FixedHashMap<int, int, 16> map3 = std::move(map1);
    EXPECT_EQ(*map3.find(2), 20);
}

TEST(FixedHashMapTest, PerformanceStressTest)
{
    constexpr uint32_t CAP = 128;
    FixedHashMap<int, int, CAP> map;
    constexpr int ITERATIONS = 100000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i)
    {
        map.insertOrAssign(i % CAP, i);
    }

    int sum = 0;
    for (int i = 0; i < ITERATIONS; ++i)
    {
        const int* val = map.find(i % CAP);
        if (val) sum += *val;
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
#ifndef _DEBUG
    constexpr float expected = 5.0f;
#else
    constexpr float expected = 10.0f;
#endif
    EXPECT_LT(elapsed.count(), expected) << "Performance regression detected! Time: " << elapsed.count() << " ms";
    EXPECT_EQ(map.size(), CAP); 
}
