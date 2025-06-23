#ifndef __CSYREN_FIXED_SPARSE_SET__
#define __CSYREN_FIXED_SPARSE_SET__

#include <queue>
#include <limits>
#include <stdexcept>

namespace csyren::cstdmf
{
	template<typename T,size_t Capacity,typename ID = size_t>
	class FixedSparseSet
	{
		static_assert(std::is_integral_v<ID>,"FixedSparseSet:: ID is not integer type");
		static_assert(Capacity < std::numeric_limits<ID>::max(), "FixedSparseSet:: capacity is bigger that ID type can handle. reduse Capacity or change ID type.");
	public:
		using size_type = ID;
		using iterator = T*;
		using const_iterator = const T*;
		static constexpr ID invalidID = std::numeric_limits<ID>::max();

		FixedSparseSet() noexcept :
			_size(0)
		{
			_freeList.reserve(Capacity);
			for (size_t i = 0; i < Capacity; ++i)
			{
				_freeList.push_back(static_cast<ID>(Capacity - 1 - i)); // Reverse order
				_sparse[i] = invalidID;
			}
		}

		~FixedSparseSet()
		{
			clear();
		}

		template<typename... Args>
		ID emplace(Args&&... args)
		{
			
			if (_size >= Capacity || _freeList.empty()) return invalidID;

			ID id = _freeList.back();
			_freeList.pop_back();
			T* data = data_ptr();
			_sparse[id] = _size;
			_dense[_size] = id;
			new (&data[_size]) T(std::forward<Args>(args)...);
			++_size;
			return id;
		}

		bool erase(ID id)
		{
			if (!contains(id)) return false;

			T* data = data_ptr();
			ID index = _sparse[id];
			ID last = _size - 1;

			data[index].~T();
			if (index != last)
			{
				const ID last_id = _dense[last];
				new(&data[index]) T(std::move(data[last]));
				_dense[index] = last_id;
				_sparse[last_id] = index;
			}
			_sparse[id] = invalidID;
			--_size;
			_freeList.push_back(id);
			return true;
		}

		void clear()
		{
			T* data = data_ptr();
			for (size_t i = 0; i < _size; ++i)
			{
				data[i].~T();
				_sparse[_dense[i]] = invalidID;
				//compact clean up. it may occure to be faster just iterate over all _sparce and clean instead of random access;
			}
			_size = 0;
		}

		bool contains(ID id) const noexcept
		{
			return id < Capacity && _sparse[id] != invalidID;
		}

		size_t size() const noexcept { return _size; }

		iterator begin()	noexcept { return data_ptr(); }
		iterator end()		noexcept { return data_ptr() + _size; }

		const_iterator begin()	const noexcept { return data_ptr(); }
		const_iterator end()	const { return data_ptr() + _size; }

		T* get(ID id) const noexcept
		{
			return contains(id) ? data_ptr() + _sparse[id] : nullptr;
		}

		T* get(ID id) noexcept
		{
			return contains(id) ?data_ptr() + _sparse[id] : nullptr;
		}

		T& operator[](ID id)
		{
			if (!contains(id))
				throw std::out_of_range("FixedSparseSet::operator[] out of range");
			return data_ptr()[_sparse[id]];
		}
		const T& operator[](ID id) const
		{
			if (!contains(id))
				throw std::out_of_range("FixedSparseSet::operator[] out of range");
			return data_ptr()[_sparse[id]];
		}

	private:
		T* data_ptr() noexcept {
			return std::launder(reinterpret_cast<T*>(_data));
		}

		const T* data_ptr() const noexcept {
			return std::launder(reinterpret_cast<const T*>(_data));
		}

		ID _sparse[Capacity];
		ID _dense[Capacity];
		alignas(alignof(T)) std::byte _data[Capacity * sizeof(T)]{};
		std::vector<ID>  _freeList;
		size_type _size{ 0 };
	};
}


#endif
