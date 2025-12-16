#include "pch.h"
#include "renderer.h"
#include "resource_upload_thread.h"
#include <chrono>

namespace csyren::render
{

	ResourceUploadThread::ResourceUploadThread(Renderer* renderer)
		: _context(renderer)
	{
        auto device = renderer->device();
		if (!device) throw std::invalid_argument("device is null");
		D3D12_COMMAND_QUEUE_DESC qdesc = {};
		qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		qdesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		qdesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		qdesc.NodeMask = 0;

		HRESULT hr = device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&_commandQueue));
		if (FAILED(hr)) {
			throw std::runtime_error("Failed to create copy command queue for upload thread");
		}
		_running = true;
		_workerThread = std::thread(&ResourceUploadThread::run, this);
	}
	ResourceUploadThread::~ResourceUploadThread()
	{
		_running = false;
		_taskCondition.notify_one();
		if (_workerThread.joinable())
		{
			_workerThread.join();
		}
	}

	void ResourceUploadThread::addTask(UploadTaskBase::Ptr&& task)
	{
		{
			std::lock_guard<std::mutex> lg(_taskMutex);
			_awaitUploadTasks.emplace_back(std::move(task));
		}
		_taskCondition.notify_one();
	}
	
	/**
	 * @brief Method is called by main thread to sync states if upload thread has something we already can use.
	 * 
	 * \param rm
	 */
	void ResourceUploadThread::sync(ResourceManager& rm)
	{
     
		std::vector<UploadTaskBase::Ptr> completedCopy;
		{
			std::lock_guard<std::mutex> lg(_completeTaskMutex);
			completedCopy = std::move(_completedTasks);
			_completedTasks.clear();
		}

		if (completedCopy.empty())
			return;

		for (auto& task : completedCopy)
		{
			try
			{
				task->onSync(rm);
			}
			catch (const std::exception& e)
			{
				log::error("ResourceUploadThread: error on sync resource {}", e.what());
			}
		}
	}

    void ResourceUploadThread::run()
    {
        const std::chrono::milliseconds pollInterval(5);
        const std::chrono::milliseconds wakeInterval(50);
        const std::chrono::milliseconds zeroWait(0);

        while (_running)
        {
            std::vector<UploadTaskBase::Ptr> tasksToProcess;
            {
                std::unique_lock<std::mutex> lock(_taskMutex);
                // wait until there are tasks or shutdown
                if (_awaitUploadTasks.empty() && _running)
                {
                    // wake up either on new tasks or periodically to check inflight futures
                    _taskCondition.wait_for(lock, wakeInterval, [this] { return !_awaitUploadTasks.empty() || !_running; });
                }

                if (!_awaitUploadTasks.empty())
                {
                    tasksToProcess = std::move(_awaitUploadTasks);
                    _awaitUploadTasks.clear();
                }
            } // release _taskMutex

            // If we have tasks, create a batch, issue uploads and keep the future + tasks pair
            if (!tasksToProcess.empty())
            {
                auto& batcher = _context.batcher();
                batcher.Begin();

                for (auto& task : tasksToProcess)
                {
                    try
                    {
                        task->onUpload(_context);
                    }
                    catch (const std::exception& e)
                    {
                        log::error("ResourceUploadThread: error occure while trying to upload resource.{}", e.what());
                    }
                }

                std::future<void> fut = batcher.End(_commandQueue.Get());
                fut.wait();
                log::debug("ResourceUploadThread: batch completed, {} tasks", tasksToProcess.size());

                std::lock_guard<std::mutex> lg(_completeTaskMutex);
                _completedTasks.insert(
                    _completedTasks.end(),
                    std::make_move_iterator(tasksToProcess.begin()),
                    std::make_move_iterator(tasksToProcess.end())
                );
            }
            std::this_thread::sleep_for(pollInterval);
        }
        flushAndShutdown();
    }

    void ResourceUploadThread::flushAndShutdown()
    {
        while (true)
        {
            {
                std::lock_guard<std::mutex> lg(_inflightMutex);
                if (_inflight.empty()) break;
            }

            // Wait on front future (blocking) to speed up shutdown
            Inflight infl;
            {
                std::lock_guard<std::mutex> lg(_inflightMutex);
                infl = std::move(_inflight.front());
                _inflight.pop_front();
            }

            try
            {
                infl.future.get();
            }
            catch (const std::exception& e)
            {
                log::error("upload future threw during shutdown: {}", e.what());
            }

            {
                std::lock_guard<std::mutex> lg2(_completeTaskMutex);
                _completedTasks.insert(_completedTasks.end(),
                    std::make_move_iterator(infl.tasks.begin()),
                    std::make_move_iterator(infl.tasks.end()));
            }
        }
    }
}
