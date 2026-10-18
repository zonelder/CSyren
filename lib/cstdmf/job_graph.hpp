#pragma once
#include "job_system2.hpp"

namespace csyren::cstdmf::multicore {
// Immutable, borrowed job DAG. Native adapters and dependency links are built
// once; enki resets completion counters when roots are resubmitted. Execute is
// synchronous on the scheduler owner thread, so destruction never races work.
class JobGraph {
public:
    struct Task {
        IJob* job;
        std::vector<size_t> parents;
        uint32_t pinnedThread = UINT32_MAX;
    };
    explicit JobGraph(const std::vector<Task>& tasks) {
        // Validate before installing links, including unordered task declarations.
        std::vector<bool> done(tasks.size());
        for (size_t remaining = tasks.size(); remaining;) {
            bool progress = false;
            for (size_t i = 0; i < tasks.size(); ++i) if (!done[i]) {
                if (!tasks[i].job) throw std::invalid_argument("Null compiled job");
                for (auto p : tasks[i].parents)
                    if (p >= tasks.size()) throw std::invalid_argument("Invalid compiled job dependency");
                if (std::all_of(tasks[i].parents.begin(), tasks[i].parents.end(), [&](auto p) { return done[p]; })) {
                    done[i] = true; --remaining; progress = true;
                }
            }
            if (!progress) throw std::invalid_argument("Compiled job dependency cycle");
        }
        _jobs.reserve(tasks.size()+1);
        for (const auto& task : tasks) add(task.job, task.pinnedThread);
        add(&_finish, UINT32_MAX);
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (tasks[i].parents.empty()) _roots.push_back(i);
            for (auto parent : tasks[i].parents) link(i, parent);
            link(tasks.size(), i);
        }
        if (tasks.empty()) _roots.push_back(0);
    }
    JobGraph(const JobGraph&) = delete;
    JobGraph& operator=(const JobGraph&) = delete;
    void execute(JobSystem& scheduler) {
        if (_executing || !scheduler.getNumThreads() || scheduler.currentThreadIndex() != 0)
            throw std::logic_error("Compiled graph requires an idle scheduler owner thread");
        for (auto& job : _jobs) {
            if (!job.task->GetIsComplete()) throw std::logic_error("Compiled graph is still running");
            if (job.pinned != UINT32_MAX && job.pinned >= scheduler.getNumThreads())
                throw std::invalid_argument("Invalid compiled pinned thread");
            job.failure->reset();
        }
        _executing = true;
        for (auto i : _roots) {
            auto& job = _jobs[i];
            if (job.pinned == UINT32_MAX) scheduler._scheduler.AddTaskSetToPipe(static_cast<enki::ITaskSet*>(job.task.get()));
            else scheduler._scheduler.AddPinnedTask(static_cast<enki::IPinnedTask*>(job.task.get()));
        }
        scheduler._scheduler.WaitforTask(_jobs.back().task.get());
        _executing = false;
        for (const auto& job : _jobs) job.failure->rethrow();
    }
private:
    struct Finish : IJob { void execute(uint32_t,uint32_t,uint32_t) override {} } _finish;
    struct Entry {
        std::unique_ptr<enki::ICompletable> task;
        JobSystem::JobFailure* failure;
        uint32_t pinned;
    };
    void add(IJob* job, uint32_t pinned) {
        if (pinned == UINT32_MAX) {
            auto task = std::make_unique<JobSystem::EnkiAdapter>(job,1,1);
            auto* failure = task.get(); _jobs.push_back({std::move(task),failure,pinned});
        } else {
            auto task = std::make_unique<JobSystem::PinnedAdapter>(job,pinned);
            auto* failure = task.get(); _jobs.push_back({std::move(task),failure,pinned});
        }
    }
    void link(size_t child, size_t parent) { _jobs[child].task->SetDependency(_dependencies.emplace_back(),_jobs[parent].task.get()); }
    std::vector<Entry> _jobs;
    // Links must be destroyed before either endpoint, and never relocate live.
    std::deque<enki::Dependency> _dependencies;
    std::vector<size_t> _roots;
    bool _executing = false;
};
}
