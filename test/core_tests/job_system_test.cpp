#include "pch.h"
#include "cstdmf/job_system2.hpp"
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
}
