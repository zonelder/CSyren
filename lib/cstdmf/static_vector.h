#pragma once

#include <cstddef>
#include <memory>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace csyren::cstdmf
{
    // Inline, contiguous storage. Only [0, size()) contains live T objects.
    // Mutations require exclusive access. Assignment has the basic exception
    // guarantee; a failed constructor destroys its successfully built prefix.
    template<typename T, std::size_t Capacity>
    class StaticVector
    {
        static_assert(Capacity > 0);
        static_assert(Capacity <= std::numeric_limits<std::size_t>::max() / sizeof(T));
        static_assert(std::is_nothrow_destructible_v<T>, "T must have a noexcept destructor");
        static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>);

        struct ConstructionGuard
        {
            StaticVector* owner;
            ~ConstructionGuard() { if (owner) owner->clear(); }
        };

    public:
        StaticVector() noexcept = default;

        StaticVector(const StaticVector& other) noexcept(std::is_nothrow_copy_constructible_v<T>)
            requires std::is_copy_constructible_v<T>
        {
            ConstructionGuard guard{this};
            for (const auto& item : other) append_unchecked(item);
            guard.owner = nullptr;
        }

        StaticVector(StaticVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
            requires std::is_move_constructible_v<T>
        {
            ConstructionGuard guard{this};
            for (auto& item : other) append_unchecked(std::move(item));
            other.clear();
            guard.owner = nullptr;
        }

        StaticVector& operator=(const StaticVector& other)
            noexcept(std::is_nothrow_copy_constructible_v<T>)
            requires std::is_copy_constructible_v<T>
        {
            if (this != &other)
            {
                clear();
                for (const auto& item : other) append_unchecked(item);
            }
            return *this;
        }

        StaticVector& operator=(StaticVector&& other)
            noexcept(std::is_nothrow_move_constructible_v<T>)
            requires std::is_move_constructible_v<T>
        {
            if (this != &other)
            {
                clear();
                for (auto& item : other) append_unchecked(std::move(item));
                other.clear();
            }
            return *this;
        }

        ~StaticVector() { clear(); }

        void push_back(const T& value) { emplace_back(value); }
        void push_back(T&& value) { emplace_back(std::move(value)); }

        template<typename... Args>
        T& emplace_back(Args&&... args)
        {
            if (full()) throw std::length_error("StaticVector capacity exceeded");
            return append_unchecked(std::forward<Args>(args)...);
        }

        // Capacity exhaustion is expected control flow for fixed-size buffers.
        // Construction of T can still throw; in that case size is unchanged.
        template<typename... Args>
        T* try_emplace_back(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            if (full()) return nullptr;
            return &append_unchecked(std::forward<Args>(args)...);
        }

        void clear() noexcept
        {
            if constexpr (std::is_trivially_destructible_v<T>) _size = 0;
            else while (_size) std::destroy_at(live(--_size));
        }

        void pop_back()
        {
            if (empty()) throw std::out_of_range("StaticVector is empty");
            std::destroy_at(live(--_size));
        }

        T& at(std::size_t i)
        {
            if (i >= _size) throw std::out_of_range("StaticVector index out of range");
            return *live(i);
        }
        const T& at(std::size_t i) const
        {
            if (i >= _size) throw std::out_of_range("StaticVector index out of range");
            return *live(i);
        }
        T& operator[](std::size_t i) { return at(i); }
        const T& operator[](std::size_t i) const { return at(i); }
        T& front() { return at(0); }
        const T& front() const { return at(0); }
        T& back() { return at(_size - 1); }
        const T& back() const { return at(_size - 1); }

        T* data() noexcept { return empty() ? reinterpret_cast<T*>(_storage) : live(0); }
        const T* data() const noexcept { return empty() ? reinterpret_cast<const T*>(_storage) : live(0); }
        T* begin() noexcept { return data(); }
        const T* begin() const noexcept { return data(); }
        const T* cbegin() const noexcept { return begin(); }
        T* end() noexcept { return data() + _size; }
        const T* end() const noexcept { return data() + _size; }
        const T* cend() const noexcept { return end(); }

        std::size_t size() const noexcept { return _size; }
        static constexpr std::size_t capacity() noexcept { return Capacity; }
        bool empty() const noexcept { return _size == 0; }
        bool full() const noexcept { return _size == Capacity; }

    private:
        template<typename... Args>
        T& append_unchecked(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            // Byte addressing works before the element's lifetime has begun.
            T* p = ::new (static_cast<void*>(_storage + sizeof(T) * _size))
                T(std::forward<Args>(args)...);
            ++_size;
            return *p;
        }
        T* live(std::size_t i) noexcept
        { return std::launder(reinterpret_cast<T*>(_storage + sizeof(T) * i)); }
        const T* live(std::size_t i) const noexcept
        { return std::launder(reinterpret_cast<const T*>(_storage + sizeof(T) * i)); }

        // C++20 byte-array storage; explicitly construct non-implicit-lifetime T.
        alignas(T) std::byte _storage[sizeof(T) * Capacity];
        std::size_t _size = 0;
    };
}
