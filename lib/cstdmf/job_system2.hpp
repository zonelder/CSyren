// Ported from E:/src/lib/cstdmf/job_system2.hpp.
// CSyren wrapper: borrowed jobs, pinned tasks, and frame/persistent storage.
// Dependencies and execution remain owned by the unchanged enkiTS scheduler.
#ifndef CSYREN_JOB_SYSTEM2_HPP
#define CSYREN_JOB_SYSTEM2_HPP

#include "log.h"
#include "profiler.h"
#include "third_party/enkiTS/src/TaskScheduler.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace csyren::cstdmf::multicore {
class IJob {
public:
    virtual ~IJob() = default;
    virtual void execute(uint32_t start, uint32_t end, uint32_t threadNum) = 0;
};

template<class Fn> requires std::is_invocable_v<Fn, uint32_t, uint32_t, uint32_t>
class LambdaJob : public IJob {
public:
    explicit LambdaJob(Fn&& fn) noexcept : _fn(std::move(fn)) {}
    void execute(uint32_t start, uint32_t end, uint32_t thread) override { _fn(start, end, thread); }
private:
    Fn _fn;
};

enum class JobStorage : uint8_t { Frame, Async, Count };
struct JobHandle {
    uint32_t id = UINT32_MAX;
    uint16_t thread = UINT16_MAX;
    JobStorage storage = JobStorage::Frame;
    uint64_t generation = 0;
    bool operator==(const JobHandle&) const = default;
    [[nodiscard]] bool isValid() const noexcept { return id != UINT32_MAX && thread != UINT16_MAX; }
};

class JobSystem {
    friend class JobGraph;
public:
    static JobSystem& instance() { static JobSystem system; return system; }
    void init(uint32_t numThreads = 0) {
        log::info("init JobSystem with {} threads", numThreads);
        enki::TaskSchedulerConfig config;
        config.numTaskThreadsToCreate = (numThreads ? numThreads : std::max(1u, std::thread::hardware_concurrency())) - 1;
        // Profiler tracks: "Worker N" (thread 0 is the thread that initializes, i.e. the main thread).
        config.profilerCallbacks.threadStart = [](uint32_t thread) { profiler::setThreadName("Worker " + std::to_string(thread)); };
        _scheduler.Initialize(config);
        _numThreads = _scheduler.GetNumTaskThreads();
        for (size_t storage = 0; storage < _entries.size(); ++storage) {
            _entries[storage].resize(_numThreads);
            _freeHeads[storage].assign(_numThreads, UINT32_MAX);
        }
    }
    void fini() {
        for (auto& thread : _entries[static_cast<size_t>(JobStorage::Async)])
            for (auto& entry : thread) if (entry.wrapper) _scheduler.WaitforTask(entry.wrapper.get());
        clear(); // The caller must have completed its frame graph before shutdown.
        for (auto& storage : _entries) storage.clear();
        for (auto& heads : _freeHeads) heads.clear();
        _scheduler.ShutdownNow();
        _numThreads = 0;
    }

    // Both methods borrow IJob until completion; submitRoot starts the graph.
    [[nodiscard]] JobHandle schedule(IJob* job, uint32_t setSize = 1, uint32_t minRange = 1)
    { return scheduleIn(JobStorage::Frame, job, setSize, minRange); }
    [[nodiscard]] JobHandle scheduleAsync(IJob* job, uint32_t setSize = 1, uint32_t minRange = 1)
    { return scheduleIn(JobStorage::Async, job, setSize, minRange); }
    [[nodiscard]] JobHandle schedulePinned(IJob* job, uint32_t targetThread = 0) {
        if (targetThread >= _numThreads) throw std::invalid_argument("Invalid pinned job thread");
        return scheduleIn(JobStorage::Frame, job, 1, 1, targetThread);
    }
    void addDependency(JobHandle child, JobHandle parent) {
        auto* c = find(child); auto* p = find(parent);
        if (!c || !p) throw std::invalid_argument("Invalid job dependency handle");
        c->wrapper->SetDependency(c->deps.emplace_back(), p->wrapper.get());
    }
    void submitRoot(JobHandle handle) {
        auto* entry = find(handle);
        if (!entry) return;
        if (!entry->deps.empty()) throw std::invalid_argument("Only root jobs can be submitted");
        if (entry->pinned) _scheduler.AddPinnedTask(static_cast<enki::IPinnedTask*>(entry->wrapper.get()));
        else _scheduler.AddTaskSetToPipe(static_cast<enki::ITaskSet*>(entry->wrapper.get()));
    }
    [[nodiscard]] bool isComplete(JobHandle handle) const {
        const auto* entry = find(handle);
        return entry && entry->wrapper->GetIsComplete();
    }
    // Wait may execute other jobs on this thread; do not hold thread-local scratch across it.
    void wait(JobHandle handle) {
        auto* entry = find(handle);
        if (!entry) return;
        auto* task = entry->wrapper.get();
        const bool pinned = entry->pinned;
        _scheduler.WaitforTask(task);
        if (pinned) static_cast<PinnedAdapter*>(task)->rethrow();
        else static_cast<EnkiAdapter*>(task)->rethrow();
    }

    // Owner-thread cleanup: wait for the entire affected graph's terminal first,
    // including cross-storage dependents. These methods do not own or delete IJob.
    // Do not use WaitforAll as a lifetime barrier (observed upstream idle-scan race).
    // https://github.com/dougbinks/enkiTS/issues/86#issuecomment-1487335696
    bool clear(JobHandle handle) {
        auto* entry = find(handle);
        if (!entry) return false;
        requireComplete(*entry);
        entry->deps.clear(); entry->wrapper.reset();
        auto& head = _freeHeads[static_cast<size_t>(handle.storage)][handle.thread];
        entry->nextFree = head; head = handle.id;
        return true;
    }
    void clear() {
        auto& frame = _entries[static_cast<size_t>(JobStorage::Frame)];
        for (auto& thread : frame) for (auto& entry : thread) if (entry.wrapper) requireComplete(entry);
        for (auto& thread : frame) thread.clear();
        std::fill(_freeHeads[static_cast<size_t>(JobStorage::Frame)].begin(),
            _freeHeads[static_cast<size_t>(JobStorage::Frame)].end(), UINT32_MAX);
    }
    [[nodiscard]] uint32_t getNumThreads() const noexcept { return _numThreads; }
    [[nodiscard]] uint32_t currentThreadIndex() const noexcept { return _scheduler.GetThreadNum(); }

private:
    class JobFailure {
    public:
        void rethrow() const { if (_failure) std::rethrow_exception(_failure); }
        void reset() { _failure = nullptr; }
    protected:
        void invoke(IJob* job, uint32_t start, uint32_t end, uint32_t thread) noexcept {
            try { job->execute(start, end, thread); }
            catch (...) { std::lock_guard lock(_mutex); if (!_failure) _failure = std::current_exception(); }
        }
    private:
        std::mutex _mutex;
        std::exception_ptr _failure;
    };
    class EnkiAdapter final : public enki::ITaskSet, public JobFailure {
    public:
        EnkiAdapter(IJob* job, uint32_t size, uint32_t range) : enki::ITaskSet(size, range), _job(job) {}
        void ExecuteRange(enki::TaskSetPartition range, uint32_t thread) override { invoke(_job, range.start, range.end, thread); }
    private:
        IJob* _job;
    };
    class PinnedAdapter final : public enki::IPinnedTask, public JobFailure {
    public:
        PinnedAdapter(IJob* job, uint32_t thread) : enki::IPinnedTask(thread), _job(job) {}
        void Execute() override { invoke(_job, 0, 1, threadNum); }
    private:
        IJob* _job;
    };
    struct JobEntry {
        uint64_t generation = 0;
        uint32_t nextFree = UINT32_MAX;
        bool pinned = false;
        std::unique_ptr<enki::ICompletable> wrapper;
        std::deque<enki::Dependency> deps;
    };
    JobHandle scheduleIn(JobStorage storage, IJob* job, uint32_t size, uint32_t range, uint32_t pinned = UINT32_MAX) {
        if (!job || !_numThreads) throw std::invalid_argument("Scheduling requires a job and initialized scheduler");
        const auto thread = _scheduler.GetThreadNum();
        if (thread >= _numThreads) throw std::logic_error("Scheduling requires a scheduler thread");
        const auto index = static_cast<size_t>(storage);
        auto& entries = _entries[index][thread]; auto& head = _freeHeads[index][thread];
        const auto id = head == UINT32_MAX ? static_cast<uint32_t>(entries.size()) : head;
        JobEntry entry; entry.generation = ++_nextGeneration; entry.pinned = pinned != UINT32_MAX;
        if (entry.pinned) entry.wrapper = std::make_unique<PinnedAdapter>(job, pinned);
        else entry.wrapper = std::make_unique<EnkiAdapter>(job, size, range);
        const JobHandle handle{id, static_cast<uint16_t>(thread), storage, entry.generation};
        if (head == UINT32_MAX) entries.emplace_back(std::move(entry));
        else { const auto next = entries[id].nextFree; entries[id] = std::move(entry); head = next; }
        return handle;
    }
    const JobEntry* find(JobHandle handle) const {
        if (!handle.isValid() || handle.storage >= JobStorage::Count) return nullptr;
        const auto& storage = _entries[static_cast<size_t>(handle.storage)];
        if (handle.thread >= storage.size() || handle.id >= storage[handle.thread].size()) return nullptr;
        const auto& entry = storage[handle.thread][handle.id];
        return entry.wrapper && entry.generation == handle.generation ? &entry : nullptr;
    }
    JobEntry* find(JobHandle handle) { return const_cast<JobEntry*>(std::as_const(*this).find(handle)); }
    static void requireComplete(JobEntry& entry) {
        if (!entry.wrapper->GetIsComplete()) throw std::logic_error("Cannot clear an incomplete job; wait for graph terminal");
        for (auto& dep : entry.deps)
            if (auto* parent = dep.GetDependencyTask(); parent && !parent->GetIsComplete())
                throw std::logic_error("Cannot clear a job with an incomplete predecessor");
    }

    enki::TaskScheduler _scheduler;
    std::atomic<uint64_t> _nextGeneration{0};
    std::array<std::vector<std::vector<JobEntry>>, static_cast<size_t>(JobStorage::Count)> _entries;
    std::array<std::vector<uint32_t>, static_cast<size_t>(JobStorage::Count)> _freeHeads;
    uint32_t _numThreads = 0;
};
} // namespace csyren::cstdmf::multicore
#endif
