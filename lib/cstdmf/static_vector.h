#pragma once
#include "assert_helpler.h"

#include <type_traits>


namespace csyren::cstdmf
{
	template<typename T,size_t Capacity>
	class StaticVector
	{
	public:
		static_assert(Capacity > 0, "Capacity must be bigger than zero");

		StaticVector() noexcept = default;
		StaticVector(const StaticVector&) noexcept(std::is_nothrow_copy_constructible_v<T>) = default;
		StaticVector& operator=(const StaticVector&) noexcept(std::is_nothrow_copy_assignable_v<T>) = default;


		StaticVector(StaticVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
			: _size(other._size)
		{
			for (size_t i = 0; i < _size; ++i)
				new (ptr(i)) T(std::move(*other.ptr(i)));
			other._size = 0;
		}

		~StaticVector() noexcept(std::is_nothrow_destructible_v<T>) 
		{
			clear();
		}

		void push_back(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
		{
			CS_ASSERT_MSG(_size < Capacity, "StaticVector overflow");
			new (ptr(_size)) T(value);
			++_size;
		}

		void push_back(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
		{
			CS_ASSERT_MSG(_size < Capacity, "StaticVector overflow");
			new (ptr(_size)) T(std::move(value));
			++_size;
		}

		template<typename... Args>
		T& emplace_back(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
		{
			CS_ASSERT_MSG(_size < Capacity, "StaticVector overflow");
			T* p = new (ptr(_size)) T(std::forward<Args>(args)...);
			++_size;
			return *p;
		}

		void clear() noexcept(std::is_nothrow_destructible_v<T>)
		{
			if constexpr (!std::is_trivially_destructible_v<T>)
			{
				for (size_t i = 0; i < _size; ++i)
					ptr(i)->~T();
			}
			_size = 0;
		}

		void pop_back() noexcept(std::is_nothrow_destructible_v<T>)
		{
			CS_ASSERT_MSG(_size > 0, "StaticVector underflow");
			--_size;
			ptr(_size)->~T();
		}

		T& operator[](size_t i)       noexcept { return *ptr(i); }
		const T& operator[](size_t i) const noexcept { return *ptr(i); }

		T& front()       noexcept { return *ptr(0); }
		const T& front() const noexcept { return *ptr(0); }
		T& back()        noexcept { return *ptr(_size - 1); }
		const T& back()  const noexcept { return *ptr(_size - 1); }

		T* data()       noexcept { return ptr(0); }
		const T* data() const noexcept { return ptr(0); }

		T* begin()       noexcept { return ptr(0); }
		const T* begin() const noexcept { return ptr(0); }
		T* end()         noexcept { return ptr(_size); }
		const T* end()   const noexcept { return ptr(_size); }

		size_t size()     const noexcept { return _size; }
		constexpr size_t capacity() const noexcept { return Capacity; }
		bool   empty()    const noexcept { return _size == 0; }
		bool   full()     const noexcept { return _size == Capacity; }


	private:
		T* ptr(size_t i)       noexcept { return reinterpret_cast<T*>(_storage) + i; }
		const T* ptr(size_t i) const noexcept { return reinterpret_cast<const T*>(_storage) + i; }


		alignas(T) unsigned char _storage[sizeof(T) * Capacity];
		size_t _size{ 0 };
	};
}