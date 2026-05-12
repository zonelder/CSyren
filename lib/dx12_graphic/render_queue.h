#ifndef __RENDER_QUEUE_HPP__
#define __RENDER_QUEUE_HPP__

#include "dx_main.h"
#include "command_list.h"
#include "dx_log.h"


namespace csyren::render
{
	/**
	 * @class RenderQueue
	 * @brief Low-level wrapper around ID3D12CommandQueue with fence management.
	 *
	 * RenderQueue represents a single GPU command queue. It manages:
	 * - Submitting command lists
	 * - Signaling a fence for GPU completion tracking
	 * - Waiting for other queues or previous work to complete
	 *
	 * This is intended to be used by higher-level constructs such as QueueContext
	 * which aggregate multiple FrameContext instances.
	 *
	 */
	class RenderQueue
	{
	public:
		RenderQueue() = default;
#ifndef _DEBUG
		using Statistic = CommandList::Statistic;
#endif
		bool init(ID3D12Device* device, const D3D12_COMMAND_QUEUE_DESC& desc)
		{
			if (DX_FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_pQueue))))
			{
				return false;
			}

			if (DX_FAILED(device->CreateFence(EMPTY_CMD_QUEUE, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_))))
			{
				return false;
			}
			fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			type_ = desc.Type;
			return fenceEvent_ != nullptr;
		}

		~RenderQueue()
		{
			if (fenceEvent_)
				CloseHandle(fenceEvent_);
		}

		/**
		* @brief Waits on this queue until another RenderQueue has finished work.
		*
		* Uses GPU-side wait;
		*
		* @param other The other RenderQueue to wait for
		*/
		void awaitComplete(RenderQueue& other)
		{
			UINT64 fenceValueToWait = other.fenceValue_;
			if (fenceValueToWait == EMPTY_CMD_QUEUE)
				return;

			// ∆дЄм на своей очереди, пока друга€ очередь не закончит
			_pQueue->Wait(other.fence_.Get(), fenceValueToWait);
		}

		/**
		* @brief Convenience method to signal and then immediately wait for completion.
		*
		* Signals the current fence value and waits until GPU completes.
		*/
		void syncAwaitLastComplete()
		{
			syncAwaitComplete(signal());
		}

		/**
		 * @brief Waits on this queue for a specific fence value to complete.
		 *
		 * Blocks CPU execution until the fence reaches or exceeds fenceValue.
		 *
		 * @param fenceValue The fence value to wait for
		 */
		void syncAwaitComplete(UINT64  fenceValue)
		{
			if (fenceValue != EMPTY_CMD_QUEUE && fence_->GetCompletedValue() < fenceValue)
			{
				fence_->SetEventOnCompletion(fenceValue, fenceEvent_);
				WaitForSingleObject(fenceEvent_, INFINITE);
			}
		}

		/**
		 * @brief Returns the last completed fence value.
		 *
		 * @return Fence value that GPU has finished executing
		 */
		UINT64 getCompleted()
		{
			return fence_->GetCompletedValue();
		}

		bool isComplete(UINT64 fenceValue) const
		{
			return fence_->GetCompletedValue() >= fenceValue;
		}

		/**
		* @brief Signals the internal fence and increments its value.
		*
		* Used to mark GPU work completion. Other queues or CPU can wait on this fence value.
		*
		* @return The new fence value after signal
		*/
		UINT64 signal()
		{
			++fenceValue_;
			_pQueue->Signal(fence_.Get(), fenceValue_);
			return fenceValue_;
		}

		/**
		 * @brief Adds a FrameContext to this queue for execution.
		 *
		 * @param ptr Pointer to a FrameContext. Must be fully recorded and cmdList closed.
		 */
		void addCommands(CommandList* ptr)
		{
			CS_ASSERT(ptr->type() == type_);
			frameContexts_.push_back(ptr);
		}

		/**
		 * @brief Executes all collected FrameContext instances as a single batch.
		 *
		 * - Calls ExecuteCommandLists() on the CommandQueue
		 * - Cleans up retired GPU resources that are safe to delete.
		 *
		 * @note FrameContext cmdList must be closed before execution.
		 */
		void execute();

		/**
		* @brief Access the underlying ID3D12CommandQueue pointer.
		*
		* @return ID3D12CommandQueue*
		*/
		ID3D12CommandQueue* raw() const noexcept
		{
			return _pQueue.Get();
		}
		D3D12_COMMAND_LIST_TYPE type() const noexcept
		{
			return type_;
		}
#ifndef _DEBUG
		const Statistic& getStatistic() const noexcept
		{
			return statistic_;
		};
#endif

	private:
#ifndef _DEBUG
		Statistic							statistic_;
#endif
		static constexpr UINT64 EMPTY_CMD_QUEUE = 0u;

		details::ComPtr< ID3D12CommandQueue >	_pQueue;
		D3D12_COMMAND_LIST_TYPE				type_;
		UINT64								fenceValue_{ 0 };
		HANDLE								fenceEvent_{ nullptr };
		details::ComPtr< ID3D12Fence >			fence_;
		std::vector< CommandList* >			frameContexts_;
	};
}

#endif