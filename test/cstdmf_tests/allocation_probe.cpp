#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <malloc.h>
#include "cstdmf/static_vector.h"
#include "cstdmf/fixed_sparse_set.h"
#include "cstdmf/sparse_set.h"
#include "cstdmf/page_view.h"

static long failAfter = -1;
static long allocations = 0;
static long outstanding = 0;
static void beforeAllocate() {
    if (failAfter == 0) throw std::bad_alloc();
    if (failAfter > 0) --failAfter;
}
void* operator new(std::size_t n) {
    beforeAllocate();
    if (void* p = std::malloc(n ? n : 1)) { ++allocations; ++outstanding; return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { if (p) { --outstanding; std::free(p); } }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new(std::size_t n, std::align_val_t a) {
    beforeAllocate();
    if (void* p = _aligned_malloc(n ? n : 1, static_cast<std::size_t>(a))) { ++allocations; ++outstanding; return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p, std::align_val_t) noexcept { if (p) { --outstanding; _aligned_free(p); } }
void operator delete[](void* p, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }

#define CHECK(e) do { if (!(e)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); std::abort(); } } while (false)
using namespace csyren::cstdmf;
int main() {
    const auto initial = outstanding;
    {
        const auto before = allocations;
        failAfter = 0;
        StaticVector<int, 64> a; FixedSparseSet<int, 64> b;
        for (int i = 0; i < 64; ++i) { a.emplace_back(i); b.emplace(i); }
        for (int i = 0; i < 64; ++i) CHECK(b.erase(i));
        b.clear(); a.clear();
        for (int i = 0; i < 64; ++i) CHECK(b.emplace(i) != b.invalidID);
        SparseSet<int> s;
        CHECK(!s.erase(0xFFFFFFFFu));
        failAfter = -1;
        CHECK(allocations == before);
    }
    {
        PageView<int, 2> p; p.reserve(8);
        const auto before = allocations;
        failAfter = 0;
        for (int i = 0; i < 8; ++i) p.emplace(i);
        for (std::uint32_t page = 0; page < 4; ++page) CHECK(p.erase(p.encode_id(page, 0)));
        for (int i = 0; i < 4; ++i) p.emplace(i);
        failAfter = -1;
        CHECK(allocations == before); CHECK(p.size() == 8);
    }
    int sparseFailures = 0, pageFailures = 0, reserveFailures = 0;
    for (int budget = 0; budget < 10; ++budget) {
        {
            SparseSet<int> s; s.emplace(1, 10);
            failAfter = budget;
            bool failed = false;
            try { s.emplace(4096, 20); } catch (const std::bad_alloc&) { failed = true; ++sparseFailures; }
            failAfter = -1;
            CHECK(s[1] == 10);
            if (failed) {
                CHECK(s.size() == 1); CHECK(!s.contains(4096)); CHECK(s.key_end() - s.key_begin() == 1);
                s.emplace(4096, 20);
            }
            CHECK(s[4096] == 20); CHECK(s.size() == 2);
        }
        {
            PageView<int, 2> p; auto a = p.emplace(1); p.emplace(2);
            failAfter = budget;
            bool failed = false;
            try { p.emplace(3); } catch (const std::bad_alloc&) { failed = true; ++pageFailures; }
            failAfter = -1;
            CHECK(p[a] == 1);
            if (failed) { CHECK(p.size() == 2); CHECK(p.capacity() == 2); p.emplace(3); }
            CHECK(p.size() == 3);
            p.erase(a); p.emplace(4); CHECK(p.size() == 3);
        }
        {
            PageView<int, 2> p;
            failAfter = budget;
            try { p.reserve(8); } catch (const std::bad_alloc&) { ++reserveFailures; }
            failAfter = -1;
            CHECK(p.empty()); CHECK(p.begin() == p.end());
            const auto capacity = p.capacity();
            failAfter = 0;
            for (std::size_t i = 0; i < capacity; ++i) p.emplace(static_cast<int>(i));
            failAfter = -1;
            CHECK(p.size() == capacity);
            p.clear(); p.emplace(4); CHECK(p.size() == 1);
        }
    }
    CHECK(sparseFailures >= 3); CHECK(pageFailures >= 2); CHECK(reserveFailures >= 4);
    CHECK(outstanding == initial);
    std::printf("PASS: zero-allocation paths; injected bad_alloc: sparse=%d, page=%d, reserve=%d; no outstanding allocations\n",
        sparseFailures, pageFailures, reserveFailures);
}
