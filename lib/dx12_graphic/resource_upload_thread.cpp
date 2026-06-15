#include "pch.h"
#include "renderer.h"
#include "resource_upload_thread.h"
#include <chrono>

namespace csyren::render
{

    /**
     * @brief Constructs a resource upload thread for asynchronously uploading resources.
     *
     * This constructor initializes the thread and the command queue used for resource uploads.
     * A new thread is created that will process resource upload tasks.
     *
     * @param renderer The renderer that owns the resources and manages the device.
     * @throws std::invalid_argument If the device is null.
     * @throws std::runtime_error If creating the command queue fails.
     */
	ResourceUploadThread::ResourceUploadThread()
		: _context(core::Services::get<Renderer>())
	{
        auto device = core::Services::get<Renderer>()->device();
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


    /**
     * @brief Destructor that shuts down the upload thread and ensures that all tasks are completed.
     *
     * The destructor stops the upload thread and waits for all tasks to finish before joining the thread.
     *
     * @note This method ensures that the thread is properly shut down and cleaned up.
     */
	ResourceUploadThread::~ResourceUploadThread()
	{
		_running = false;
		_taskCondition.notify_one();
		if (_workerThread.joinable())
		{
			_workerThread.join();
		}
	}

    /**
     * @brief Adds a new upload task to the task queue.
     *
     * This method adds a task for uploading resources to the queue. The task is then processed by the upload thread.
     * The task is moved into the queue, and the upload thread is notified to process it.
     *
     * @param task The upload task to be added.
     * @note The task must be moved into the queue to avoid keeping a reference to resources that might be used in the upload thread.
     * @throws std::invalid_argument If the task is invalid (nullptr).
     *
     * @warning This method must never take the task by reference because it can lead to resource access conflicts in the main thread.
     * @warning The resource data must not remain accessible in the main thread after the task is added, as it may be modified by the upload thread.
     */
	void ResourceUploadThread::addTask(UploadTaskBase::Ptr&& task)
	{
		{
			std::lock_guard<std::mutex> lg(_taskMutex);
			_awaitUploadTasks.emplace_back(std::move(task));
		}
		_taskCondition.notify_one();
	}
	
    /**
     * @brief Synchronizes the upload tasks with the resource manager.
     *
     * This method is called by the main thread to synchronize resources after they have been uploaded.
     * It processes the completed tasks and updates the resource manager accordingly.
     *
     * @param rm The resource manager that will be updated with the uploaded resources.
     * @throws std::exception If an error occurs during synchronization.
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

    /**
     * @brief Main execution loop for the upload thread.
     *
     * This method processes the queued tasks by uploading resources and synchronizing the completion of each task.
     * Tasks are processed in batches for better performance. The thread continues to run until it is stopped.
     *
     * @note This method will periodically wake up to process tasks, and ensure that the thread exits gracefully when stopped.
     */
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

    /**
     * @brief Waits for all in-flight tasks to complete before shutting down the thread.
     *
     * This method blocks until all in-flight tasks have been processed and completed. It ensures that no tasks
     * are left unfinished before the thread shuts down.
     */
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
