#pragma once

#include "resource_handle.h"
#include "upload_task.h"
#include "upload_context.h"

#include <d3d12.h>
#include <ResourceUploadBatch.h>

#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <future>

namespace csyren::render
{
	class ResourceManager;

	class ResourceUploadThread
	{
	public:
		ResourceUploadThread(Renderer* renderer);
		~ResourceUploadThread();

		ResourceUploadThread(const ResourceUploadThread&) = delete;
		ResourceUploadThread& operator=(const ResourceUploadThread&) = delete;

		void sync(ResourceManager& rm);

		void addTask(UploadTaskBase::Ptr&& task);

	private:
		void run();
		void flushAndShutdown(); // helper used during shutdown

		std::atomic<bool> _running{ false };
		std::thread _workerThread;

		// Context for the worker thread
		UploadContext _context;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> _commandQueue;

		// --- Queues for Inter-Thread Communication ---

		// 1. Pending Tasks (Main Thread -> Upload Thread)
		std::mutex _taskMutex;
		std::condition_variable _taskCondition; // To wake up the worker thread
		std::vector<UploadTaskBase::Ptr> _awaitUploadTasks;

		// 2. Completed Tasks (Upload Thread -> Main Thread)
		std::mutex _completeTaskMutex;
		std::vector <UploadTaskBase::Ptr> _completedTasks;

		struct Inflight
		{
			std::future<void> future;
			std::vector<UploadTaskBase::Ptr> tasks;
		};
		std::deque<Inflight> _inflight;
		std::mutex _inflightMutex;
	};
}
