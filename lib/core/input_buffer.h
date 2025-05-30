#ifndef __CSYREN_INPUT_BUFFER__
#define __CSYREN_INPUT_BUFFER__
#include  <queue>
#include <stdexcept>


namespace csyren::core
{
	/**
	 * @enum BufferOverflowPolicy
	 * .@brief Defines how buffer overflow situations are handled.
	 */
	enum class BufferOverflowPolicy
	{
		DiscardOldest,		///< Remove oldest events when buffer if full
		DiscardNewest,		///< Ignor new events when buffer is full
		Resize,				///<Dynamically resize the buffer
		ThrowException,		/// Throw exception on overflow(for debugging)
	};

	/**
	 * @class InputBuffer
	 * @brief Buffer system for input events
	 * 
	 * This template class provides a flexible and configurable buffer for storing
	 * input events. It support different overflow policies.
	 * @tparam T the type of item stores in the buffer.
	 */
	template<typename T>
	class InputBuffer
	{
	public:
		/**
		 *.
		 * 
		 * @param size Initial maximum size of the buffer
		 * @param policy Overflow policy to use
		 * 
		 */
		InputBuffer(size_t size = 16,BufferOverflowPolicy policy = BufferOverflowPolicy::DiscardOldest) noexcept : 
			_maxSize(size),
			_overflowPolicy(policy)
		{ }
		/**
		 * @brief Push an item into the buffer.
		 * Add an item to the buffer. handling overflow according to current policy.
		 * param item The item to add
		 * @throw std::overflow_error  if the buffer is full and policy is ThrowException
		 */
		void push(const T& item)
		{
			if (_buffer.size() < _maxSize)
			{
				_buffer.push(item);
				return;
			}
			switch (_overflowPolicy)
			{
			case csyren::core::BufferOverflowPolicy::DiscardOldest:
				_buffer.pop();
				_buffer.push(item);
				break;
			case csyren::core::BufferOverflowPolicy::DiscardNewest:
				//do nothing. just ignor the new item
				break;
			case csyren::core::BufferOverflowPolicy::Resize:
				_maxSize *= 2;
				_buffer.push(item);
				break;
			case csyren::core::BufferOverflowPolicy::ThrowException:
				throw std::overflow_error("InputBuffer overflow");
				break;
			default:
				break;
			}
		}

		/**
		 * @brief Pop an item from the buffer
		 *
		 * Removes and returns the oldest item from the buffer.
		 *
		 * @return The oldest item in the buffer
		 * @throws std::underflow_error if the buffer is empty
		 */
		T pop()
		{
			if (_buffer.empty())
				throw std::underflow_error("Input buffer underflow");
			T item = _buffer.front();
			_buffer.pop();
			return item;
		}
		/**
		 * @brief Check if the buffer is emty.
		 * 
		 * \return 
		 */
		bool empty() const noexcept(noexcept(_buffer.empty())) { return _buffer.empty(); }

		/**
		 * @brief clear the buffer
		 * Remove all items trom the buffer.
		 */
		void clear()
		{
			while (!_buffer.empty())
			{
				_buffer.pop();
			}
		}

		size_t size() const noexcept(noexcept(_buffer.size()))
		{
			return _buffer.size();
		}
		/**
		 * @brief. Set the maximum size of the buffer
		 * 
		 * \param size The new maximum size
		 */
		void maxSize(size_t size)
		{
			_maxSize = size;
			if (_overflowPolicy == BufferOverflowPolicy::DiscardOldest)
			{
				while (_buffer.size() > _maxSize)
				{
					_buffer.pop();
				}
			}
		}

		size_t maxSize() const noexcept { return _maxSize; };
		void overflowPolicy(BufferOverflowPolicy policy) noexcept { _overflowPolicy = policy; }
		BufferOverflowPolicy overflowPolicy() const noexcept { return _overflowPolicy; }
	private:
		std::queue<T>	_buffer;
		size_t			_maxSize;
		BufferOverflowPolicy _overflowPolicy;
	};
}

#endif
