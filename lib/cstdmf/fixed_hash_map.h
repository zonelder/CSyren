#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "assert_helpler.h"


namespace csyren::cstdmf
{
	/**
	 * @brief High-performance, Zero-Allocation , fixed-capacity hash table.
	 *
	 * @details Implements a hash table using open addressing and linear probing.
	 * Specifically designed for hot-path tasks with small key numbers;
	 *
	 * @par Architectural Advantages:
	 * - @b Zero-Allocation: Memory is allocated strictly at initialization.
	 *   Absence of heap fragmentation guarantees deterministic execution
	 * - @b Optimal Cache Locality: During lookups, only the compact index array is loaded into the L1 cache.
	 * - @b No Pointer Chasing: All data resides in contiguous arrays, maximizing
	 *   CPU hardware prefetcher efficiency (unlike @c std::unordered_map).
	 * - @b Predictable O(1) Complexity: With a 50% load factor
	 *   (@c NumSlots = Capacity * 2), the expected number of probes on collision is ~1.5.
	 *
	 * @par Limitations & Trade-offs:
	 * - Does not support individual element removal (@c erase). Only full @c clear()
	 * - Capacity is fixed at compile-time and @b must be a power of two.
	 * - Maximum capacity is capped at 128 elements (due to @c uint8_t indices).
	 * - Keys and values must be trivially copyable (@c std::is_trivially_copyable_v).
	 *
	 * @tparam Key      The key type. Must support @c operator== and be trivially copyable.
	 * @tparam Value    The mapped value type. Must be trivially copyable.
	 * @tparam Capacity Maximum number of elements. @b Must be a power of two (1..128).
	 * @tparam Hash     The hash functor.@c std::hash.
	 *
	 * @note For maximum performance, it is highly recommended to use fast,
	 *       non-cryptographic hashes with a strong avalanche effect (e.g., xxHash).
	 */
	template < typename Key, typename Value, uint32_t Capacity, typename Hash = std::hash<Key>>
	class FixedHashMap
	{
		static_assert(Capacity > 0 && Capacity <= 128, "Capacity must be between 1 and 128 for uint8_t slots");
		static_assert((Capacity& (Capacity - 1)) == 0, "Capacity must be a power of 2");

		static_assert(std::is_trivially_copyable_v< Key >, "Key must be trivially copyable");
		static_assert(std::is_trivially_copyable_v< Value >, "Value must be trivially copyable");

		static constexpr uint32_t NumSlots = Capacity * 2;	// 50% max load in for collision minimization;
		static constexpr uint32_t SlotMask = NumSlots - 1;
		static constexpr uint8_t EmptySlot = 0xFFu;

	public:
		struct Entry
		{
			Key key;
			Value value;
		};
		using iterator = Entry*;
		using const_iterator = const Entry*;
		using value_type = Entry;
		FixedHashMap() noexcept
		{
			clear();
		}

		FixedHashMap(const FixedHashMap&) noexcept = default;
		FixedHashMap& operator=(const FixedHashMap&) noexcept = default;

		FixedHashMap(FixedHashMap&&) noexcept = default;
		FixedHashMap& operator=(FixedHashMap&&) noexcept = default;

		void clear() noexcept
		{
			size_ = 0;
			slots_.fill(EmptySlot);
		}

		[[nodiscard]] size_t size() const noexcept
		{
			return static_cast<size_t>(size_);
		}
		[[nodiscard]] bool empty() const noexcept
		{
			return size_ == 0;
		}
		[[nodiscard]] static constexpr uint32_t capacity() noexcept
		{
			return Capacity;
		}

		[[nodiscard]] Value* find(const Key& key) noexcept
		{
			return const_cast<Value*>(static_cast<const FixedHashMap*>(this)->find(key));
		}

		[[nodiscard]] const Value* find(const Key& key) const noexcept
		{
			uint32_t slot = Hash{}(key)&SlotMask;
			for (;; )
			{
				const uint8_t index = slots_[slot];
				if (index == EmptySlot) [[unlikely]]
					return nullptr;

				if (entries_[index].key == key)[[likely]]
					return &entries_[index].value;

				slot = (slot + 1) & SlotMask;
			}
		}

		[[nodiscard]] bool contains(const Key& key) const noexcept
		{
			return find(key) != nullptr;
		}
		bool insertOrAssign(const Key& key, const Value& value) noexcept
		{
			if (size_ >= Capacity) [[unlikely]]
				return false;
			const uint32_t hash = Hash{}(key);
			uint32_t slot = hash & SlotMask;
			uint32_t first_empty_slot = NumSlots; // Sentinel for "not found yet"


			// Single-pass: search for key AND remember the first empty slot
			while (true)
			{
				const uint8_t index = slots_[slot];

				if (index == EmptySlot)
				{
					first_empty_slot = slot;
					break; // Key cannot exist beyond the first empty slot in linear probing
				}

				if (entries_[index].key == key)
				{
					entries_[index].value = value;
					return true;
				}

				slot = (slot + 1) & SlotMask;
			}



			const uint8_t newIndex = size_;
			entries_[newIndex].key = key;
			entries_[newIndex].value = value;

			slots_[first_empty_slot] = newIndex;
			++size_;
			return true;
		}
		[[nodiscard]] Value& operator[](const Key& key) noexcept
		{
			Value* pValue = find(key);
			CS_ASSERT(pValue != nullptr && "FixedHashMap::operator[]: Key not found");
			return *pValue;
		}

		[[nodiscard]] const Value& operator[](const Key& key) const noexcept
		{
			const Value* pValue = find(key);
			CS_ASSERT(pValue != nullptr && "FixedHashMap::operator[]: Key not found");
			return *pValue;
		}

		[[nodiscard]] iterator begin() noexcept { return entries_.data(); }
		[[nodiscard]] iterator end() noexcept { return entries_.data() + size_; }

		[[nodiscard]] const_iterator begin() const noexcept { return entries_.data(); }
		[[nodiscard]] const_iterator end() const noexcept { return entries_.data() + size_; }

		[[nodiscard]] const_iterator cbegin() const noexcept { return entries_.data(); }
		[[nodiscard]] const_iterator cend() const noexcept { return entries_.data() + size_; }

		[[nodiscard]] Entry* data() noexcept { return entries_.data(); }
		[[nodiscard]] const Entry* data() const noexcept { return entries_.data(); }
	private:

		std::array< Entry, Capacity > entries_;
		std::array< uint8_t, NumSlots > slots_;
		uint8_t size_ = 0;
	};
}
