#ifndef __CSYREN_FIXED_SPARSE_SET__
#define __CSYREN_FIXED_SPARSE_SET__

#include <limits>
#include <stdexcept>

namespace csyren::cstdmf
{
	template<typename T,size_t Capacity>
	class FixedSparceSet
	{
	public:
		using ID = size_t;
		using iterator = T*;
		using const_iterator = const T*;
		static constexpr ID invalidID = std::numeric_limits<ID>::max();

		FixedSparceSet() :
			size_(0)
		{
			for (size_t i = 0; i < Capacity; ++i)
			{
				_sparse[i] = invalidID;
			}
		}

		template<typename... Args>
		bool emplace(ID id, Args&&... args)
		{
			if (_size >= Capacity || contains(id)) return false;
			_sparse[id] = _size;
			_dense[_size] = id;
			new (&_data[_size]) T(std::forward<Args>(args)...);
		}

		bool erase(ID id)
		{
			if (!contains(id)) return false;
			size_t index = _sparse[id];
			size_t last = _size - 1;
			_data[index].~T();
			if (index != last)
			{
				_data[index] = std::remove(_data[last]);
				_dense[index] = _dense[last];
				_sparse[_dense[index]] = index;
			}
			_sparce[id] = invalidID;
			--_size;
			return true;
		}

		bool contains(ID id) const
		{
			return _sparse[id] != invalidID;
		}

		size_t size() const noexcept { return _size; }

		iterator begin()	noexcept { return _data; }
		iterator end()		noexcept { return _data + _size; }

		const_iterator begin()	const noexcept { return _data; }
		const_iterator end()	const { return _data + _size; }

		T* get(ID id) const noexcept
		{
			if (!contains(id)) return nullptr;
			return &_data[_sparse[id]];
		}

		T& operator[](ID id) const
		{
			if (!contains(id))
				throw std::out_of_range("FixedSparceSet::operator[] out of range");
			return const_cast<T&>(_data[_sparse[id]]);
		}

	private:

		ID _sparse[Capacity];
		ID _dense[Capacity];
		T _data[Capacity];
		size_t _size;
	};
}


#endif
