#pragma once

#include "static_vector.h"
#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace csyren::cstdmf
{
    // Dense, inline pool. ID slots are reused (IDs are NOT generation handles).
    // Insertion does not move existing T. Erase moves the last T into the hole.
    // No allocations in construction, insertion, erase or clear (excluding T).
    template<typename T, std::size_t Capacity, typename ID = std::size_t>
    class FixedSparseSet
    {
        static_assert(std::is_integral_v<ID> && std::is_unsigned_v<ID> && !std::is_same_v<ID, bool>);
        static_assert(Capacity > 0 && Capacity < std::numeric_limits<ID>::max());
        static constexpr bool relocatable = std::is_nothrow_move_constructible_v<T> || std::is_nothrow_copy_constructible_v<T>;

    public:
        using size_type = ID;
        using iterator = T*;
        using const_iterator = const T*;
        static constexpr ID invalidID = std::numeric_limits<ID>::max();

        FixedSparseSet() noexcept
        {
            for (std::size_t i = 0; i + 1 < Capacity; ++i) _sparse[i] = static_cast<ID>(i + 1);
            _sparse[Capacity - 1] = invalidID;
        }
        // Bytewise copy/move of a pool is unsafe. PageView moves owning pointers.
        FixedSparseSet(const FixedSparseSet&) = delete;
        FixedSparseSet& operator=(const FixedSparseSet&) = delete;
        FixedSparseSet(FixedSparseSet&&) = delete;
        FixedSparseSet& operator=(FixedSparseSet&&) = delete;

        template<typename... Args>
        ID emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            if (_freeHead == invalidID) return invalidID;
            const ID id = _freeHead;
            const ID next = _sparse[id];
            const auto index = _items.size();
            _items.try_emplace_back(std::forward<Args>(args)...);
            // Commit only after successful construction.
            _sparse[id] = static_cast<ID>(index);
            _dense[index] = id;
            _freeHead = next;
            return id;
        }

        // Throwing assignment keeps the mapping valid (basic guarantee), while
        // nothrow relocation avoids both assignment and exception recovery.
        bool erase(ID id) noexcept(relocatable || std::is_nothrow_move_assignable_v<T>)
            requires (relocatable || std::is_move_assignable_v<T>)
        {
            if (!contains(id)) return false;
            const auto index = _sparse[id];
            const auto last = _items.size() - 1;
            if (index != last)
            {
                T* data = _items.data();
                if constexpr (relocatable)
                {
                    std::destroy_at(data + index);
                    std::construct_at(data + index, std::move_if_noexcept(data[last]));
                }
                else data[index] = std::move(data[last]);
                const ID moved = _dense[last];
                _dense[index] = moved;
                _sparse[moved] = index;
            }
            _items.pop_back(); // Also destroys the moved-from last element.
            _sparse[id] = _freeHead;
            _freeHead = id;
            return true;
        }

        void clear() noexcept
        {
            for (std::size_t i = 0; i < _items.size(); ++i)
            {
                const auto id = _dense[i];
                _sparse[id] = _freeHead;
                _freeHead = id;
            }
            _items.clear();
        }

        bool contains(ID id) const noexcept
        {
            // Free cells store links; the reverse mapping distinguishes them
            // from live dense indices without a separate allocation/bitset.
            return id < Capacity && _sparse[id] < _items.size() && _dense[_sparse[id]] == id;
        }
        std::size_t size() const noexcept { return _items.size(); }
        bool empty() const noexcept { return _items.empty(); }
        static constexpr std::size_t capacity() noexcept { return Capacity; }

        iterator begin() noexcept { return _items.begin(); }
        iterator end() noexcept { return _items.end(); }
        const_iterator begin() const noexcept { return _items.begin(); }
        const_iterator end() const noexcept { return _items.end(); }
        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }

        T* get(ID id) noexcept { return contains(id) ? _items.data() + _sparse[id] : nullptr; }
        const T* get(ID id) const noexcept { return contains(id) ? _items.data() + _sparse[id] : nullptr; }
        T& operator[](ID id)
        {
            if (auto p = get(id)) return *p;
            throw std::out_of_range("FixedSparseSet invalid ID");
        }
        const T& operator[](ID id) const
        {
            if (auto p = get(id)) return *p;
            throw std::out_of_range("FixedSparseSet invalid ID");
        }

    private:
        std::array<ID, Capacity> _sparse;
        std::array<ID, Capacity> _dense;
        StaticVector<T, Capacity> _items;
        ID _freeHead = 0;
    };
}
