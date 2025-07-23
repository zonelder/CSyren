#include "pch.h"
#include "cstdmf/sparse_set.h"


using namespace csyren::cstdmf;
using EntityID = uint32_t;
struct MoveOnly {
    explicit MoveOnly(int v = 0) noexcept : value(v) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&)            noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
    int value;
};

struct ThrowOnCtor {
    explicit ThrowOnCtor(int v = 0) {
        if (v < 0) throw std::runtime_error("negative");
        value = v;
    }
    int value;
};

struct RefCount {
    static inline int alive = 0;
    RefCount() { ++alive; }
    RefCount(RefCount&&) { ++alive; }  // Перемещение = новый объект
    ~RefCount() { --alive; }
    // Оператор присваивания не меняет счётчик
    RefCount& operator=(RefCount&&) { return *this; }
};

TEST(SparseSet, Empty) {
    SparseSet<int> s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
    EXPECT_FALSE(s.contains(0));
    EXPECT_EQ(s.try_get(0), nullptr);
}

TEST(SparseSet, EmplaceContainsGet) 
{
    SparseSet<int> s;
    EntityID e = 42;
    int* ptr = s.emplace(e, 123);
    EXPECT_TRUE(s.contains(e));
    EXPECT_EQ(s.try_get(e), ptr);
    EXPECT_EQ(*ptr, 123);
    EXPECT_EQ(s.size(), 1u);
}

TEST(SparseSet, Erase) {
    SparseSet<int> s;
    EntityID e = 5;
    s.emplace(e, 55);
    EXPECT_TRUE(s.erase(e));
    EXPECT_FALSE(s.contains(e));
    EXPECT_EQ(s.size(), 0u);
    EXPECT_FALSE(s.erase(e));   // второй раз
}

TEST(SparseSet, DoubleEmplaceThrows) {
    SparseSet<int> s;
    s.emplace(1, 1);
    EXPECT_THROW(s.emplace(1, 2), std::runtime_error);
}

TEST(SparseSet, AccessOperator) {
    SparseSet<int> s;
    s.emplace(7, 777);
    EXPECT_EQ(s[7], 777);
    EXPECT_THROW(s[8], std::out_of_range);
}

TEST(SparseSet, IterationEmpty) {
    SparseSet<int> s;
    EXPECT_EQ(s.begin(), s.end());
}

TEST(SparseSet, IterationOrder) {
    SparseSet<int> s;
    std::vector<EntityID> order{ 10, 3, 7, 100 };
    for (EntityID e : order) s.emplace(e, static_cast<int>(e));

    std::vector<int> collected;
    for (const auto& v : s) collected.push_back(v);
    EXPECT_EQ(collected, std::vector<int>({ 10, 3, 7, 100 }));
}

TEST(SparseSet, MoveCtor) {
    SparseSet<std::unique_ptr<int>> s;
    s.emplace(1, std::make_unique<int>(42));
    SparseSet<std::unique_ptr<int>> s2(std::move(s));
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(*s2[1], 42);
}

TEST(SparseSet, MoveAssignment) {
    SparseSet<std::string> s;
    s.emplace(1, "hello");
    SparseSet<std::string> s2;
    s2 = std::move(s);
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s2[1], "hello");
}

/* ---------- move-only компонент ---------- */
TEST(SparseSet, MoveOnlyComponent) {
    SparseSet<MoveOnly> s;
    s.emplace(1, MoveOnly{ 99 });
    EXPECT_EQ(s[1].value, 99);
}

/* ---------- throwing-ctor ---------- */
TEST(SparseSet, EmplaceThrowsStrongGuarantee) {
    SparseSet<ThrowOnCtor> s;
    EXPECT_NO_THROW(s.emplace(1, 1));
    EXPECT_EQ(s.size(), 1u);
    EXPECT_THROW(s.emplace(2, -1), std::runtime_error);
    EXPECT_EQ(s.size(), 1u);   // строгая гарантия
}

/* ---------- граничные значения ---------- */
TEST(SparseSet, MaxEntity) {
    SparseSet<int> s;
    EntityID e = std::numeric_limits<EntityID>::max();
    s.emplace(e, 1);
    EXPECT_TRUE(s.contains(e));
    EXPECT_EQ(s.size(), 1u);
}

/* ---------- большое количество элементов ---------- */
TEST(SparseSet, Stress) {
    constexpr std::size_t N = 100'000;
    SparseSet<int> s;
    for (EntityID i = 0; i < N; ++i) {
        s.emplace(i * 97, static_cast<int>(i)); // разреженный
    }
    EXPECT_EQ(s.size(), N);
    for (EntityID i = 0; i < N; ++i) {
        EntityID e = i * 97;
        EXPECT_EQ(*s.try_get(e), static_cast<int>(i));
    }
    // удалим каждый второй
    for (EntityID i = 0; i < N; i += 2) {
        EXPECT_TRUE(s.erase(i * 97));
    }
    EXPECT_EQ(s.size(), N / 2);
}

/* ---------- memory leak / RAII ---------- */
TEST(SparseSet, NoLeak) {
    {
        SparseSet<RefCount> s;
        s.emplace(1);
        s.emplace(2);
        s.emplace(3);
        EXPECT_EQ(RefCount::alive, 3);
    }
    EXPECT_EQ(RefCount::alive, 0);
}

/* ---------- итераторы после erase ---------- */
TEST(SparseSet, IteratorStability) {
    SparseSet<int> s;
    s.emplace(1, 1);
    s.emplace(2, 2);
    s.emplace(3, 3);

    auto it = s.begin();
    EXPECT_EQ(*it, 1);
    s.erase(1);
    it = s.begin();
    EXPECT_TRUE(it == s.begin());
    std::vector<int> vec{ s.begin(), s.end() };
    EXPECT_EQ(vec.size(), 2u);
}