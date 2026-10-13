#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace csyren::cstdmf
{
    // Dense values with a paged key->index map. Keys are immutable through the
    // public API. Insertion/reserve can invalidate pointers; erase moves the last
    // value into the hole. No synchronization or generation checking is implied.
    template<typename T, typename EntityID = std::uint32_t>
    class SparseSet
    {
        static_assert(std::is_integral_v<EntityID> && std::is_unsigned_v<EntityID> && !std::is_same_v<EntityID, bool>);
        static_assert(std::is_nothrow_destructible_v<T>);
        static_assert(!std::is_same_v<T, bool>, "vector<bool> does not provide contiguous bool objects");
        using index_type = EntityID;
        static constexpr index_type kInvalidIndex = std::numeric_limits<index_type>::max();
        static constexpr std::size_t kPageBits = 12;
        static constexpr std::size_t kPageSize = std::size_t{1} << kPageBits;
        static constexpr std::size_t kPageMask = kPageSize - 1;
        static constexpr bool safe_growth = std::is_nothrow_move_constructible_v<T> || std::is_copy_constructible_v<T>;
        static constexpr bool relocatable = std::is_nothrow_move_constructible_v<T> || std::is_nothrow_copy_constructible_v<T>;

    public:
        SparseSet() = default;
        SparseSet(const SparseSet&) = delete;
        SparseSet& operator=(const SparseSet&) = delete;
        SparseSet(SparseSet&& other) noexcept { swap(other); }
        SparseSet& operator=(SparseSet&& other) noexcept
        {
            if (this != &other) { clear(); swap(other); }
            return *this;
        }
        void swap(SparseSet& other) noexcept
        {
            _sparsePages.swap(other._sparsePages);
            _dense.swap(other._dense);
            _items.swap(other._items);
        }

        template<typename... Args>
        T* emplace(EntityID entity, Args&&... args) requires safe_growth
        {
            index_type& cell = sparseRef(entity);
            if (cell != kInvalidIndex) throw std::runtime_error("SparseSet duplicate key");
            if (_dense.size() >= kInvalidIndex) throw std::length_error("SparseSet index capacity exceeded");
            const auto index = static_cast<index_type>(_dense.size());
            _items.emplace_back(std::forward<Args>(args)...);
            if (_dense.size() < _dense.capacity()) _dense.push_back(entity);
            else
            {
                try { _dense.push_back(entity); }
                catch (...) { _items.pop_back(); throw; }
            }
            cell = index;
            return &_items.back();
        }

        // Throwing assignment is supported with a basic guarantee: all objects
        // and key mappings stay valid, but the two affected T values may change.
        bool erase(EntityID entity) noexcept(std::is_nothrow_move_assignable_v<T> || relocatable)
            requires (std::is_move_assignable_v<T> || relocatable)
        {
            auto* cell = sparsePtr(entity); // Read-only lookup: never allocate.
            if (!cell || *cell == kInvalidIndex) return false;
            const auto idx = *cell;
            const auto last = _dense.size() - 1;
            if (idx != last)
            {
                if constexpr (std::is_nothrow_move_assignable_v<T> || !relocatable)
                    _items[idx] = std::move(_items[last]);
                else
                {
                    std::destroy_at(_items.data() + idx);
                    std::construct_at(_items.data() + idx, std::move_if_noexcept(_items[last]));
                }
                const auto moved = _dense[last];
                _dense[idx] = moved;
                // moved is already live, so its sparse page necessarily exists.
                const auto movedPage = static_cast<std::size_t>(static_cast<std::uintmax_t>(moved) >> kPageBits);
                _sparsePages[movedPage][moved & kPageMask] = idx;
            }
            _items.pop_back();
            _dense.pop_back();
            *cell = kInvalidIndex;
            return true;
        }

        bool contains(EntityID entity) const noexcept
        { const auto* cell = sparsePtr(entity); return cell && *cell != kInvalidIndex; }
        T* try_get(EntityID entity) noexcept
        { const auto* cell = sparsePtr(entity); return cell && *cell != kInvalidIndex ? &_items[*cell] : nullptr; }
        const T* try_get(EntityID entity) const noexcept
        { const auto* cell = sparsePtr(entity); return cell && *cell != kInvalidIndex ? &_items[*cell] : nullptr; }
        T& operator[](EntityID entity)
        {
            if (auto* p = try_get(entity)) return *p;
            throw std::out_of_range("SparseSet invalid key");
        }
        const T& operator[](EntityID entity) const
        {
            if (auto* p = try_get(entity)) return *p;
            throw std::out_of_range("SparseSet invalid key");
        }

        using iterator = T*;
        using const_iterator = const T*;
        using key_iterator = typename std::vector<EntityID>::const_iterator;
        using const_key_iterator = key_iterator;
        std::size_t size() const noexcept { return _items.size(); }
        bool empty() const noexcept { return _items.empty(); }
        iterator begin() noexcept { return _items.data(); }
        iterator end() noexcept { return empty() ? begin() : begin() + size(); }
        const_iterator begin() const noexcept { return _items.data(); }
        const_iterator end() const noexcept { return empty() ? begin() : begin() + size(); }
        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }
        key_iterator key_begin() noexcept { return _dense.cbegin(); }
        key_iterator key_end() noexcept { return _dense.cend(); }
        const_key_iterator key_begin() const noexcept { return _dense.cbegin(); }
        const_key_iterator key_end() const noexcept { return _dense.cend(); }

        void clear() noexcept { _items.clear(); _dense.clear(); _sparsePages.clear(); }
        void reserve(std::size_t capacity) requires safe_growth
        {
            if (capacity > kInvalidIndex) throw std::length_error("SparseSet index capacity exceeded");
            _dense.reserve(capacity);
            _items.reserve(capacity);
        }
        T* data() noexcept { return _items.data(); }
        const T* data() const noexcept { return _items.data(); }
        const EntityID* key_data() noexcept { return _dense.data(); }
        const EntityID* key_data() const noexcept { return _dense.data(); }

    private:
        index_type* sparsePtr(EntityID entity) noexcept
        {
            const auto page = static_cast<std::uintmax_t>(entity) >> kPageBits;
            if (std::cmp_greater_equal(page, _sparsePages.size()) || !_sparsePages[static_cast<std::size_t>(page)]) return nullptr;
            return &_sparsePages[static_cast<std::size_t>(page)][entity & kPageMask];
        }
        const index_type* sparsePtr(EntityID entity) const noexcept
        {
            const auto page = static_cast<std::uintmax_t>(entity) >> kPageBits;
            if (std::cmp_greater_equal(page, _sparsePages.size()) || !_sparsePages[static_cast<std::size_t>(page)]) return nullptr;
            return &_sparsePages[static_cast<std::size_t>(page)][entity & kPageMask];
        }
        index_type& sparseRef(EntityID entity)
        {
            const auto widePage = static_cast<std::uintmax_t>(entity) >> kPageBits;
            if (std::cmp_greater_equal(widePage, _sparsePages.max_size())) throw std::length_error("SparseSet key too large");
            const auto page = static_cast<std::size_t>(widePage);
            if (page >= _sparsePages.size()) _sparsePages.resize(page + 1);
            if (!_sparsePages[page])
            {
                auto allocated = std::make_unique_for_overwrite<index_type[]>(kPageSize);
                std::fill_n(allocated.get(), kPageSize, kInvalidIndex);
                _sparsePages[page] = std::move(allocated);
            }
            return _sparsePages[page][entity & kPageMask];
        }

        std::vector<std::unique_ptr<index_type[]>> _sparsePages;
        std::vector<EntityID> _dense;
        std::vector<T> _items;
    };
}
