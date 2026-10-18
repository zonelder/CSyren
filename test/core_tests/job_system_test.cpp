#include "pch.h"
#include "cstdmf/job_system2.hpp"
#include "cstdmf/job_graph.hpp"
#include <atomic>
#include <chrono>
#include <thread>

namespace {
using namespace csyren::cstdmf::multicore;

class JobSystemTest : public ::testing::Test {
protected:
    JobSystem jobs;
    void SetUp() override { jobs.init(3); }
    void TearDown() override { jobs.fini(); }
};

TEST_F(JobSystemTest, TerminalWaitIncludesPinnedDependencyAndAllBranches) {
    const auto mainThread = std::this_thread::get_id();
    for (int frame = 0; frame < 2000; ++frame) {
        std::atomic<unsigned> bits{0};
        bool correctThread = false;
        bool parentsFinished = false;
        auto aFn = [&](uint32_t, uint32_t, uint32_t) { bits.fetch_or(1); };
        auto bFn = [&](uint32_t, uint32_t, uint32_t) { bits.fetch_or(2); };
        auto pinnedFn = [&](uint32_t, uint32_t, uint32_t thread) {
            correctThread = thread == 0 && std::this_thread::get_id() == mainThread;
            parentsFinished = bits.load() == 3;
            bits.fetch_or(4);
        };
        auto finishFn = [](uint32_t, uint32_t, uint32_t) {};
        LambdaJob a(std::move(aFn)); LambdaJob b(std::move(bFn));
        LambdaJob pinned(std::move(pinnedFn)); LambdaJob finish(std::move(finishFn));
        const auto ah = jobs.schedule(&a), bh = jobs.schedule(&b);
        const auto ph = jobs.schedulePinned(&pinned), fh = jobs.schedule(&finish);
        jobs.addDependency(ph, ah);
        jobs.addDependency(ph, bh);
        jobs.addDependency(fh, ph);
        jobs.submitRoot(ah);
        jobs.submitRoot(bh);
        jobs.wait(fh);
        jobs.clear();
        ASSERT_TRUE(correctThread);
        ASSERT_TRUE(parentsFinished);
        ASSERT_EQ(bits.load(), 7u);
    }
}

TEST_F(JobSystemTest, IndependentRootsActuallyOverlap) {
    std::atomic<unsigned> running{0};
    std::atomic<bool> overlapped{true};
    auto work = [&](uint32_t, uint32_t, uint32_t) {
        running.fetch_add(1);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (running.load() != 2) {
            if (std::chrono::steady_clock::now() > deadline) { overlapped = false; break; }
            std::this_thread::yield();
        }
    };
    auto finishFn = [](uint32_t, uint32_t, uint32_t) {};
    LambdaJob a(std::move(work));
    auto second = [&](uint32_t s, uint32_t e, uint32_t t) { a.execute(s, e, t); };
    LambdaJob b(std::move(second)); LambdaJob finish(std::move(finishFn));
    const auto ah = jobs.schedule(&a), bh = jobs.schedule(&b), fh = jobs.schedule(&finish);
    jobs.addDependency(fh, ah);
    jobs.addDependency(fh, bh);
    jobs.submitRoot(ah);
    jobs.submitRoot(bh);
    jobs.wait(fh);
    jobs.clear();
    EXPECT_TRUE(overlapped.load());
}

TEST_F(JobSystemTest, SingleThreadAndPinnedRootDoNotDeadlock) {
    jobs.fini();
    jobs.init(1);
    unsigned count = 0;
    auto pinnedFn = [&](uint32_t, uint32_t, uint32_t thread) { if (thread == 0) ++count; };
    auto finishFn = [](uint32_t, uint32_t, uint32_t) {};
    LambdaJob pinned(std::move(pinnedFn)); LambdaJob finish(std::move(finishFn));
    const auto ph = jobs.schedulePinned(&pinned), fh = jobs.schedule(&finish);
    jobs.addDependency(fh, ph);
    jobs.submitRoot(ph);
    jobs.wait(fh);
    jobs.clear();
    EXPECT_EQ(count, 1u);
}

TEST_F(JobSystemTest, AsyncRangeAndDependenciesUseTheSameAPI) {
    std::atomic<uint32_t> sum{0};
    bool dependentSawAllRanges = false;
    auto rangeFn = [&](uint32_t start, uint32_t end, uint32_t) {
        for (auto i = start; i < end; ++i) sum.fetch_add(i, std::memory_order_relaxed);
    };
    auto dependentFn = [&](uint32_t, uint32_t, uint32_t) { dependentSawAllRanges = sum == 499500u; };
    LambdaJob range(std::move(rangeFn)); LambdaJob dependent(std::move(dependentFn));
    const auto root = jobs.scheduleAsync(&range, 1000, 13);
    const auto terminal = jobs.scheduleAsync(&dependent);
    jobs.addDependency(terminal, root);
    jobs.submitRoot(root); jobs.wait(terminal);
    EXPECT_TRUE(dependentSawAllRanges);
    EXPECT_TRUE(jobs.isComplete(root)); EXPECT_TRUE(jobs.isComplete(terminal));
    jobs.clear();
    EXPECT_TRUE(jobs.isComplete(terminal));
    EXPECT_TRUE(jobs.clear(terminal)); EXPECT_TRUE(jobs.clear(root));
}

TEST_F(JobSystemTest, AsyncAndFrameJobsCanShareDependencies) {
    std::atomic<unsigned> stage{0};
    bool ordered = false;
    auto firstFn = [&](uint32_t, uint32_t, uint32_t) { stage = 1; };
    auto middleFn = [&](uint32_t, uint32_t, uint32_t) { if (stage == 1) stage = 2; };
    auto lastFn = [&](uint32_t, uint32_t, uint32_t) { ordered = stage == 2; };
    LambdaJob first(std::move(firstFn)); LambdaJob middle(std::move(middleFn)); LambdaJob last(std::move(lastFn));
    const auto a = jobs.scheduleAsync(&first), b = jobs.schedulePinned(&middle), c = jobs.scheduleAsync(&last);
    jobs.addDependency(b, a); jobs.addDependency(c, b);
    jobs.submitRoot(a); jobs.wait(c);
    jobs.clear(); // Frame dependencies detach without destroying either async entry.
    EXPECT_TRUE(ordered); EXPECT_TRUE(jobs.isComplete(a)); EXPECT_TRUE(jobs.isComplete(c));
    EXPECT_TRUE(jobs.clear(a)); EXPECT_TRUE(jobs.clear(c));
}

TEST_F(JobSystemTest, AsyncSlotReuseRejectsStaleHandles) {
    unsigned executed = 0;
    auto fn = [&](uint32_t, uint32_t, uint32_t) { ++executed; };
    LambdaJob task(std::move(fn));
    auto stale = jobs.scheduleAsync(&task);
    jobs.submitRoot(stale); jobs.wait(stale); EXPECT_TRUE(jobs.clear(stale));
    for (unsigned i = 0; i < 100; ++i) {
        const auto next = jobs.scheduleAsync(&task);
        EXPECT_EQ(stale.id, next.id); EXPECT_NE(stale.generation, next.generation);
        EXPECT_FALSE(jobs.isComplete(stale)); EXPECT_FALSE(jobs.clear(stale));
        EXPECT_THROW(jobs.addDependency(next, stale), std::invalid_argument);
        jobs.submitRoot(stale); // Does not submit the replacement entry.
        jobs.submitRoot(next); jobs.wait(next); EXPECT_TRUE(jobs.clear(next));
        stale = next;
    }
    EXPECT_EQ(executed, 101u);
}

TEST_F(JobSystemTest, AsyncCleanupRejectsAnIncompleteJob) {
    std::atomic<bool> release{false}, started{false};
    auto firstFn = [](uint32_t, uint32_t, uint32_t) {};
    auto blockedFn = [&](uint32_t, uint32_t, uint32_t) {
        started = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!release && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    };
    LambdaJob first(std::move(firstFn)); LambdaJob blocked(std::move(blockedFn));
    const auto a = jobs.schedule(&first), b = jobs.scheduleAsync(&blocked);
    jobs.addDependency(b, a); jobs.submitRoot(a);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!started && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    EXPECT_TRUE(started);
    jobs.wait(a);
    EXPECT_THROW(jobs.clear(b), std::logic_error);
    release = true; jobs.wait(b); jobs.clear(); EXPECT_TRUE(jobs.clear(b));
}

TEST_F(JobSystemTest, BothStoresPropagateFailuresAndReleaseCompletedEntries) {
    auto fn = [](uint32_t, uint32_t, uint32_t) { throw std::runtime_error("job failure"); };
    LambdaJob failed(std::move(fn));
    const auto frame = jobs.schedule(&failed), async = jobs.scheduleAsync(&failed);
    jobs.submitRoot(frame); jobs.submitRoot(async);
    EXPECT_THROW(jobs.wait(frame), std::runtime_error);
    EXPECT_THROW(jobs.wait(async), std::runtime_error);
    EXPECT_TRUE(jobs.isComplete(frame)); EXPECT_TRUE(jobs.isComplete(async));
    EXPECT_TRUE(jobs.clear(frame)); EXPECT_TRUE(jobs.clear(async));
    jobs.clear();
}


TEST_F(JobSystemTest, ClearingAReusedFrameSlotDoesNotRetargetAsyncDependencies) {
    unsigned rootRuns = 0, childRuns = 0;
    auto rootFn = [&](uint32_t, uint32_t, uint32_t) { ++rootRuns; };
    auto childFn = [&](uint32_t, uint32_t, uint32_t) { ++childRuns; };
    LambdaJob root(std::move(rootFn)); LambdaJob child(std::move(childFn));
    const auto parent = jobs.scheduleAsync(&root);
    JobHandle previous;
    for (unsigned frame = 0; frame < 100; ++frame) {
        const auto dependent = jobs.schedule(&child);
        if (previous.isValid()) {
            EXPECT_EQ(previous.id, dependent.id);
            EXPECT_FALSE(jobs.isComplete(previous));
        }
        jobs.addDependency(dependent, parent);
        jobs.submitRoot(parent); jobs.wait(dependent);
        if (frame % 2) jobs.clear();
        else EXPECT_TRUE(jobs.clear(dependent));
        previous = dependent;
    }
    EXPECT_EQ(rootRuns, 100u); EXPECT_EQ(childRuns, 100u);
    EXPECT_TRUE(jobs.clear(parent));
}


TEST_F(JobSystemTest, AsyncBurstsSurviveOutOfOrderCleanupAndSlotReuse) {
    std::atomic<unsigned> executed{0};
    auto fn = [&](uint32_t, uint32_t, uint32_t) { ++executed; };
    LambdaJob job(std::move(fn));
    std::vector<JobHandle> previous;
    for (unsigned burst = 0; burst < 2; ++burst) {
        std::vector<JobHandle> handles;
        for (unsigned i = 0; i < 128; ++i) handles.push_back(jobs.scheduleAsync(&job));
        for (auto old : previous) EXPECT_FALSE(jobs.isComplete(old));
        for (auto handle : handles) jobs.submitRoot(handle);
        for (auto handle : handles) jobs.wait(handle);
        jobs.clear(); // Async entries still belong to the caller.
        for (unsigned parity = 0; parity < 2; ++parity)
            for (size_t i = parity; i < handles.size(); i += 2) EXPECT_TRUE(jobs.clear(handles[i]));
        previous = std::move(handles);
    }
    EXPECT_EQ(executed, 256u);
}

TEST_F(JobSystemTest, CompiledGraphReusesFanInAndPinnedTasksAcrossFailures) {
    int frame=0; std::atomic<unsigned> bits{0}; unsigned completed=0;
    const auto main=std::this_thread::get_id();
    auto af=[&](uint32_t,uint32_t,uint32_t) { bits.fetch_or(1); if(frame==123) throw std::runtime_error("compiled failure"); };
    auto bf=[&](uint32_t,uint32_t,uint32_t) { bits.fetch_or(2); };
    auto cf=[&](uint32_t,uint32_t,uint32_t) { EXPECT_EQ(bits.load(),3u); EXPECT_EQ(std::this_thread::get_id(),main); ++completed; };
    LambdaJob a(std::move(af)); LambdaJob b(std::move(bf)); LambdaJob c(std::move(cf));
    JobGraph graph({{&a,{}},{&b,{}},{&c,{0,1},0}});
    for(;frame<3000;++frame) {
        bits=0;
        if(frame==123) EXPECT_THROW(graph.execute(jobs),std::runtime_error);
        else EXPECT_NO_THROW(graph.execute(jobs));
        jobs.clear(); // compiled tasks are independent of transient frame storage
    }
    EXPECT_EQ(completed,3000u);
    JobGraph empty({}); EXPECT_NO_THROW(empty.execute(jobs));
}
TEST_F(JobSystemTest, CompiledGraphValidatesBeforeSubmittingAnyWork) {
    auto fn=[](uint32_t,uint32_t,uint32_t) {};
    LambdaJob job(std::move(fn));
    EXPECT_THROW((JobGraph({{&job,{1}}})),std::invalid_argument);
    EXPECT_THROW((JobGraph({{&job,{0}}})),std::invalid_argument);
    JobGraph invalidThread({{&job,{},30000}});
    EXPECT_THROW(invalidThread.execute(jobs),std::invalid_argument);
    JobGraph graph({{&job,{}}}); EXPECT_NO_THROW(graph.execute(jobs));
}

}
