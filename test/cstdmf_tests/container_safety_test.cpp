#include "pch.h"
#include "cstdmf/static_vector.h"
#include "cstdmf/fixed_sparse_set.h"
#include "cstdmf/sparse_set.h"
#include "cstdmf/page_view.h"
#include <concepts>
#include <iterator>
#include <map>
#include <random>
#include <string>

namespace {
using namespace csyren::cstdmf;
struct Life {
    static inline int live = 0;
    static inline int copiesLeft = -1;
    static inline int movesLeft = -1;
    int value;
    explicit Life(int n = 0) : value(n) { ++live; }
    Life(const Life& other) : value(other.value) {
        if (copiesLeft == 0) throw std::runtime_error("copy");
        if (copiesLeft > 0) --copiesLeft;
        ++live;
    }
    Life(Life&& other) : value(other.value) {
        if (movesLeft == 0) throw std::runtime_error("move");
        if (movesLeft > 0) --movesLeft;
        other.value = -1; ++live;
    }
    Life& operator=(Life&& other) {
        value = other.value;
        if (movesLeft == 0) throw std::runtime_error("assignment");
        other.value = -1;
        return *this;
    }
    ~Life() { --live; }
};
struct FailCtor {
    int value;
    explicit FailCtor(int v = 0) : value(v) { if (v < 0) throw std::runtime_error("ctor"); }
};
struct alignas(128) Aligned { int value; explicit Aligned(int n) noexcept : value(n) {} };
struct CopyFallback {
    static inline int moves = 0;
    int value;
    explicit CopyFallback(int v) noexcept : value(v) {}
    CopyFallback(const CopyFallback&) noexcept = default;
    CopyFallback(CopyFallback&&) { ++moves; throw std::runtime_error("move must not be used"); }
};
struct ThrowingMoveOnly {
    ThrowingMoveOnly() = default;
    ThrowingMoveOnly(const ThrowingMoveOnly&) = delete;
    ThrowingMoveOnly(ThrowingMoveOnly&&) noexcept(false) {}
};
template<class S> concept CanGrow = requires(S& s) { s.emplace(0); s.reserve(1); };
template<class S> concept CanErase = requires(S& s) { s.erase(0); };
static_assert(!CanGrow<SparseSet<ThrowingMoveOnly>>);
static_assert(!CanErase<FixedSparseSet<ThrowingMoveOnly, 4>>);
static_assert(!CanErase<PageView<ThrowingMoveOnly, 4>>);
static_assert(!std::is_copy_constructible_v<StaticVector<std::unique_ptr<int>, 4>>);
static_assert(!std::is_copy_constructible_v<FixedSparseSet<int, 4>>);
static_assert(!std::is_move_constructible_v<FixedSparseSet<int, 4>>);
static_assert(std::forward_iterator<PageView<int>::iterator>);
static_assert(std::forward_iterator<PageView<int>::const_iterator>);
static_assert(std::is_same_v<decltype(std::declval<const FixedSparseSet<int, 4>&>().get(0)), const int*>);
static_assert(std::is_same_v<decltype(*std::declval<SparseSet<int>&>().key_begin()), const std::uint32_t&>);
static_assert(std::is_same_v<decltype(std::declval<SparseSet<int>&>().key_data()), const std::uint32_t*>);

TEST(ContainerSafety, StaticBoundsRemainCheckedInRelease) {
    StaticVector<int, 2> s;
    EXPECT_THROW(s.pop_back(), std::out_of_range);
    EXPECT_THROW(s.front(), std::out_of_range);
    EXPECT_THROW(s.back(), std::out_of_range);
    s.push_back(1); s.emplace_back(2);
    EXPECT_THROW(s.emplace_back(3), std::length_error);
    EXPECT_EQ(s.try_emplace_back(4), nullptr);
    EXPECT_THROW(s[2], std::out_of_range);
    EXPECT_EQ(s.size(), 2u);
    s.pop_back(); EXPECT_EQ(s.back(), 1);
}
TEST(ContainerSafety, StaticCopyAndMoveOwnTheirElements) {
    Life::live = 0; Life::copiesLeft = Life::movesLeft = -1;
    {
        StaticVector<Life, 4> a; a.emplace_back(1); a.emplace_back(2);
        auto b = a; EXPECT_EQ(Life::live, 4);
        StaticVector<Life, 4> c(std::move(b)); EXPECT_TRUE(b.empty()); EXPECT_EQ(Life::live, 4);
        b = a; EXPECT_EQ(Life::live, 6);
        b = b; b = std::move(b); EXPECT_EQ(b.size(), 2u);
        c = std::move(b); EXPECT_TRUE(b.empty()); EXPECT_EQ(Life::live, 4);
        b.emplace_back(3); EXPECT_EQ(b[0].value, 3);
    }
    EXPECT_EQ(Life::live, 0);
}
TEST(ContainerSafety, StaticFailedCopyConstructorCleansPrefix) {
    Life::live = 0; Life::copiesLeft = Life::movesLeft = -1;
    {
        StaticVector<Life, 4> a; a.emplace_back(1); a.emplace_back(2);
        Life::copiesLeft = 1;
        EXPECT_THROW((StaticVector<Life, 4>(a)), std::runtime_error);
        EXPECT_EQ(Life::live, 2); EXPECT_EQ(a.size(), 2u);
        Life::copiesLeft = -1;
    }
    EXPECT_EQ(Life::live, 0);
}
TEST(ContainerSafety, StaticFailedMoveConstructorCleansPrefix) {
    Life::live = 0; Life::copiesLeft = Life::movesLeft = -1;
    {
        StaticVector<Life, 4> a; a.emplace_back(1); a.emplace_back(2);
        Life::movesLeft = 1;
        EXPECT_THROW((StaticVector<Life, 4>(std::move(a))), std::runtime_error);
        EXPECT_EQ(Life::live, 2); EXPECT_EQ(a.size(), 2u);
        Life::movesLeft = -1;
        a.clear(); a.emplace_back(4);
    }
    EXPECT_EQ(Life::live, 0);
}
TEST(ContainerSafety, StaticFailedAssignmentRetainsValidPrefix) {
    Life::live = 0; Life::copiesLeft = Life::movesLeft = -1;
    {
        StaticVector<Life, 4> a; a.emplace_back(1); a.emplace_back(2);
        StaticVector<Life, 4> b; b.emplace_back(3);
        Life::copiesLeft = 1;
        EXPECT_THROW(b = a, std::runtime_error);
        EXPECT_EQ(b.size(), 1u); EXPECT_EQ(Life::live, 3);
        Life::copiesLeft = -1; Life::movesLeft = 1;
        EXPECT_THROW(b = std::move(a), std::runtime_error);
        EXPECT_EQ(b.size(), 1u); EXPECT_EQ(a.size(), 2u); EXPECT_EQ(Life::live, 3);
        Life::movesLeft = -1;
    }
    EXPECT_EQ(Life::live, 0);
}
TEST(ContainerSafety, StaticThrowingEmplaceAndAliasing) {
    StaticVector<FailCtor, 3> a;
    a.emplace_back(1);
    EXPECT_THROW(a.try_emplace_back(-1), std::runtime_error);
    EXPECT_EQ(a.size(), 1u);
    a.push_back(a.front()); EXPECT_EQ(a.back().value, 1);
}
TEST(ContainerSafety, StaticRandomOperationsMatchVector) {
    StaticVector<std::string, 13> actual; std::vector<std::string> expected;
    std::mt19937 rng(912);
    for (int step = 0; step < 4000; ++step) {
        if (step % 101 == 0) { actual.clear(); expected.clear(); }
        else if (rng() % 2) {
            auto value = std::to_string(step);
            if (actual.full()) EXPECT_THROW(actual.push_back(value), std::length_error);
            else { actual.push_back(value); expected.push_back(value); }
        } else if (!expected.empty()) { actual.pop_back(); expected.pop_back(); }
        ASSERT_EQ(actual.size(), expected.size());
        EXPECT_TRUE(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
        auto copy = actual;
        EXPECT_TRUE(std::equal(copy.begin(), copy.end(), expected.begin(), expected.end()));
    }
}
TEST(ContainerSafety, InlineStorageHonorsOverAlignment) {
    StaticVector<Aligned, 3> a; a.emplace_back(1); a.emplace_back(2);
    FixedSparseSet<Aligned, 3> b; auto id = b.emplace(3);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(a.data()) % alignof(Aligned), 0u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(b.get(id)) % alignof(Aligned), 0u);
    EXPECT_EQ(a[1].value, 2);
}
TEST(ContainerSafety, FixedEraseDestroysOwnedResources) {
    auto p = std::make_shared<int>(7);
    FixedSparseSet<std::shared_ptr<int>, 4> s;
    auto a = s.emplace(p); auto b = s.emplace(p);
    EXPECT_EQ(p.use_count(), 3);
    s.erase(a); EXPECT_EQ(p.use_count(), 2); EXPECT_TRUE(s.contains(b));
    s.clear(); EXPECT_EQ(p.use_count(), 1);
    s.clear(); EXPECT_EQ(p.use_count(), 1);
}
TEST(ContainerSafety, FixedThrowDoesNotLoseSlotOrPublishObject) {
    FixedSparseSet<FailCtor, 2> s;
    auto a = s.emplace(1);
    EXPECT_THROW(s.emplace(-1), std::runtime_error);
    EXPECT_EQ(s.size(), 1u); EXPECT_EQ(s[a].value, 1);
    EXPECT_FALSE(s.contains(1));
    auto b = s.emplace(2); EXPECT_EQ(b, 1u);
    s.erase(a); EXPECT_EQ(s[b].value, 2);
    s.clear(); EXPECT_NE(s.emplace(3), decltype(s)::invalidID); EXPECT_NE(s.emplace(4), decltype(s)::invalidID);
}
TEST(ContainerSafety, FixedEraseUsesNothrowCopyFallback) {
    CopyFallback::moves = 0;
    FixedSparseSet<CopyFallback, 3> s;
    auto a = s.emplace(1); auto b = s.emplace(2);
    s.erase(a); EXPECT_EQ(s[b].value, 2); EXPECT_EQ(CopyFallback::moves, 0);
}
TEST(ContainerSafety, FixedAndPageThrowingEraseKeepTheirMappings) {
    Life::live = 0; Life::copiesLeft = Life::movesLeft = -1;
    {
        FixedSparseSet<Life, 3> s; auto a = s.emplace(1); auto b = s.emplace(2);
        PageView<Life, 3> p; auto x = p.emplace(3); auto y = p.emplace(4);
        Life::movesLeft = 0;
        EXPECT_THROW(s.erase(a), std::runtime_error);
        EXPECT_THROW(p.erase(x), std::runtime_error);
        EXPECT_EQ(s.size(), 2u); EXPECT_EQ(p.size(), 2u); EXPECT_EQ(Life::live, 4);
        EXPECT_TRUE(s.contains(a)); EXPECT_TRUE(s.contains(b));
        EXPECT_TRUE(p.contains(x)); EXPECT_TRUE(p.contains(y));
        Life::movesLeft = -1;
        s.erase(a); p.erase(x); EXPECT_EQ(Life::live, 2);
    }
    EXPECT_EQ(Life::live, 0);
}
TEST(ContainerSafety, FixedConstLookupAndInvalidIDs) {
    FixedSparseSet<int, 2, std::uint8_t> s;
    auto id = s.emplace(8); const auto& c = s;
    EXPECT_EQ(*c.get(id), 8); EXPECT_EQ(c.get(255), nullptr);
    EXPECT_FALSE(s.erase(255)); EXPECT_THROW(c[255], std::out_of_range);
}
TEST(ContainerSafety, FixedRandomOperationsMatchReference) {
    FixedSparseSet<int, 31> s;
    std::map<std::size_t, int> expected;
    std::mt19937 rng(0xC57D);
    for (int step = 0; step < 20000; ++step) {
        if (step % 317 == 0) { s.clear(); expected.clear(); }
        else if (rng() % 2) {
            auto id = s.emplace(step);
            if (id != decltype(s)::invalidID) { ASSERT_FALSE(expected.contains(id)); expected[id] = step; }
            else EXPECT_EQ(expected.size(), 31u);
        } else {
            const auto id = rng() % 40;
            EXPECT_EQ(s.erase(id), expected.erase(id) != 0);
        }
        ASSERT_EQ(s.size(), expected.size());
        for (std::size_t id = 0; id < 40; ++id) {
            ASSERT_EQ(s.contains(id), expected.contains(id));
            if (expected.contains(id)) EXPECT_EQ(s[id], expected.at(id));
        }
    }
}
TEST(ContainerSafety, SparseFailedCtorRollsBackEveryIndex) {
    SparseSet<FailCtor> s; s.emplace(4095, 3);
    EXPECT_THROW(s.emplace(4096, -1), std::runtime_error);
    EXPECT_FALSE(s.contains(4096)); EXPECT_EQ(s.try_get(4096), nullptr);
    EXPECT_EQ(std::distance(s.key_begin(), s.key_end()), 1);
    EXPECT_EQ(s[4095].value, 3); EXPECT_NO_THROW(s.emplace(4096, 4));
}
TEST(ContainerSafety, SparseFailedRelocationKeepsExistingKeys) {
    Life::live = 0; Life::copiesLeft = Life::movesLeft = -1;
    {
        SparseSet<Life> s; s.reserve(1); s.emplace(1, 1);
        Life::copiesLeft = 0;
        EXPECT_THROW(s.emplace(2, 2), std::runtime_error);
        EXPECT_TRUE(s.contains(1)); EXPECT_FALSE(s.contains(2));
        EXPECT_EQ(s[1].value, 1); EXPECT_EQ(s.size(), 1u); EXPECT_EQ(Life::live, 1);
        Life::copiesLeft = -1; s.emplace(2, 2);
    }
    EXPECT_EQ(Life::live, 0);
}
TEST(ContainerSafety, SparseThrowingEraseKeepsIndexMapping) {
    Life::live = 0; Life::copiesLeft = Life::movesLeft = -1;
    {
        SparseSet<Life> s; s.emplace(1, 10); s.emplace(2, 20);
        Life::movesLeft = 0;
        EXPECT_THROW(s.erase(1), std::runtime_error);
        EXPECT_EQ(s.size(), 2u); EXPECT_EQ(Life::live, 2);
        EXPECT_EQ(*s.key_begin(), 1u); EXPECT_EQ(*(s.key_begin() + 1), 2u);
        EXPECT_TRUE(s.contains(1)); EXPECT_TRUE(s.contains(2));
        Life::movesLeft = -1; EXPECT_TRUE(s.erase(1)); EXPECT_FALSE(s.contains(1));
    }
    EXPECT_EQ(Life::live, 0);
}
TEST(ContainerSafety, SparseSupportsNonAssignableNothrowMoveType) {
    struct Item {
        int value; explicit Item(int v) noexcept : value(v) {}
        Item(Item&&) noexcept = default;
        Item& operator=(Item&&) = delete;
    };
    SparseSet<Item> s; s.emplace(1, 7); s.emplace(2, 9); s.erase(1);
    EXPECT_EQ(s[2].value, 9);
}
TEST(ContainerSafety, SparseSmallIDExhaustionAndMaximumKey) {
    SparseSet<int, std::uint8_t> s;
    for (int i = 0; i < 255; ++i) s.emplace(static_cast<std::uint8_t>(i), i);
    EXPECT_THROW(s.emplace(255, 9), std::length_error);
    EXPECT_FALSE(s.contains(255)); EXPECT_EQ(s.size(), 255u);
    s.erase(0); s.emplace(255, 42); EXPECT_EQ(s[255], 42);
    EXPECT_THROW(s.reserve(256), std::length_error);
    SparseSet<int, std::uint16_t> mid; mid.emplace(65535, 1); EXPECT_EQ(std::as_const(mid)[65535], 1);
}
TEST(ContainerSafety, Sparse64BitKeysAreNotTruncated) {
    SparseSet<int, std::uint64_t> s;
    const auto large = (std::uint64_t{1} << 32) + 5;
    s.emplace(5, 1); s.emplace(large, 2);
    EXPECT_EQ(s[5], 1); EXPECT_EQ(std::as_const(s)[large], 2);
    EXPECT_FALSE(s.erase(std::numeric_limits<std::uint64_t>::max()));
    s.erase(5); EXPECT_EQ(s[large], 2);
}
TEST(ContainerSafety, SparseMoveLeavesSourceReusable) {
    SparseSet<std::string> a; a.emplace(1, "one");
    SparseSet<std::string> b(std::move(a));
    EXPECT_TRUE(a.empty()); EXPECT_FALSE(a.contains(1)); a.emplace(1, "new");
    b = std::move(a); EXPECT_TRUE(a.empty()); EXPECT_EQ(b[1], "new");
    b = std::move(b); EXPECT_EQ(b[1], "new"); a.emplace(8, "eight");
}
TEST(ContainerSafety, SparseRandomOperationsMatchReference) {
    SparseSet<int> s; std::map<std::uint32_t, int> expected; std::mt19937 rng(321);
    for (int step = 0; step < 12000; ++step) {
        auto id = static_cast<std::uint32_t>((rng() % 151) * 97);
        if (step % 499 == 0) { s.clear(); expected.clear(); }
        else if (rng() % 2) {
            if (expected.contains(id)) EXPECT_THROW(s.emplace(id, step), std::runtime_error);
            else { s.emplace(id, step); expected[id] = step; }
        } else EXPECT_EQ(s.erase(id), expected.erase(id) != 0);
        ASSERT_EQ(s.size(), expected.size());
        std::size_t index = 0;
        for (auto key = s.key_begin(); key != s.key_end(); ++key, ++index) {
            ASSERT_TRUE(expected.contains(*key)); EXPECT_EQ(s.data()[index], expected.at(*key));
            EXPECT_EQ(s.try_get(*key), s.data() + index);
        }
    }
}
TEST(ContainerSafety, PageClearReserveAndRefill) {
    PageView<int, 2> p; p.reserve(6);
    for (int i = 0; i < 6; ++i) p.emplace(i);
    EXPECT_EQ(p.capacity(), 6u); EXPECT_EQ(p.size(), 6u);
    p.clear(); EXPECT_TRUE(p.empty()); EXPECT_EQ(p.begin(), p.end());
    auto id = p.emplace(9); EXPECT_EQ(p[id], 9); EXPECT_EQ(p.size(), 1u);
    p.clear(); p.clear(); EXPECT_EQ(p.capacity(), 0u); p.reserve(4); EXPECT_EQ(p.begin(), p.end());
}
TEST(ContainerSafety, PageThrowingCtorDoesNotPublishPageOrID) {
    PageView<FailCtor, 2> p;
    EXPECT_THROW(p.emplace(-1), std::runtime_error); EXPECT_EQ(p.capacity(), 0u);
    p.reserve(2); EXPECT_THROW(p.emplace(-1), std::runtime_error);
    EXPECT_EQ(p.size(), 0u); EXPECT_EQ(p.capacity(), 2u);
    EXPECT_EQ(p.emplace(1), 0u); EXPECT_EQ(p.emplace(2), 1u);
    EXPECT_THROW(p.emplace(-1), std::runtime_error); EXPECT_EQ(p.capacity(), 2u);
}
TEST(ContainerSafety, PageIteratorSkipsEmptyPagesAndReturnsOldPosition) {
    PageView<int, 1> p;
    auto a = p.emplace(10); p.emplace(20); auto c = p.emplace(30); p.emplace(40);
    p.erase(a); p.erase(c); p.reserve(7);
    auto it = p.begin(); auto prev = it++;
    EXPECT_EQ(*prev, 20); EXPECT_EQ(*it, 40);
    PageView<int, 1>::const_iterator cit = it;
    EXPECT_TRUE(cit == it); EXPECT_TRUE(it == cit);
    EXPECT_EQ(*cit++, 40); EXPECT_EQ(cit, std::as_const(p).end());
    ++it; EXPECT_EQ(it, p.end());
    std::vector<int> values(p.cbegin(), p.cend()); EXPECT_EQ(values, (std::vector<int>{20, 40}));
}
TEST(ContainerSafety, PageMoveLeavesSourceReusableAndPointersStable) {
    PageView<std::string, 2> a; auto id = a.emplace("hello"); auto* ptr = a.get(id);
    PageView<std::string, 2> b(std::move(a)); EXPECT_TRUE(a.empty()); EXPECT_EQ(b.get(id), ptr);
    a.emplace("source"); a = std::move(b); EXPECT_TRUE(b.empty()); EXPECT_EQ(a.get(id), ptr);
    a = std::move(a); EXPECT_EQ(*ptr, "hello");
    b.emplace("reused"); a.reserve(12); EXPECT_EQ(a.get(id), ptr);
}
TEST(ContainerSafety, PageSmallestFreePageAndCheckedAccess) {
    PageView<int, 1> p; std::vector<PageView<int, 1>::ID> ids;
    for (int i = 0; i < 6; ++i) ids.push_back(p.emplace(i));
    p.erase(ids[4]); p.erase(ids[1]); p.erase(ids[3]);
    EXPECT_EQ(p.emplace(11), ids[1]); EXPECT_EQ(p.emplace(33), ids[3]); EXPECT_EQ(p.emplace(44), ids[4]);
    EXPECT_THROW(p[p.invalidID], std::out_of_range);
    EXPECT_THROW(std::as_const(p)[p.invalidID], std::out_of_range);
    EXPECT_EQ(std::as_const(p).get(p.invalidID), nullptr);
    EXPECT_THROW(p.reserve(std::numeric_limits<std::size_t>::max()), std::length_error);
}
TEST(ContainerSafety, PageRandomOperationsMatchReference) {
    PageView<int, 7> p; std::map<std::uint64_t, int> expected; std::mt19937 rng(882);
    for (int step = 0; step < 8000; ++step) {
        if (step % 263 == 0) { p.clear(); expected.clear(); p.reserve(21); }
        else if (rng() % 2 || expected.empty()) {
            auto id = p.emplace(step); ASSERT_FALSE(expected.contains(id)); expected[id] = step;
        } else {
            auto it = expected.begin(); std::advance(it, rng() % expected.size());
            ASSERT_TRUE(p.erase(it->first)); expected.erase(it);
        }
        ASSERT_EQ(p.size(), expected.size()); EXPECT_EQ(std::distance(p.begin(), p.end()), p.size());
        for (const auto& [id, value] : expected) { ASSERT_NE(p.get(id), nullptr); EXPECT_EQ(p[id], value); }
    }
}
} // namespace
