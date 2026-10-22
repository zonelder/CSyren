#include "pch.h"
#include "cstdmf/profiler.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

using namespace csyren;

namespace
{
    // The collector is process-wide: start every test from an empty state.
    profiler::Collector& freshCollector()
    {
        profiler::setEnabled(true);
        auto& collector = profiler::Collector::instance();
        collector.collect();
        collector.reset();
        return collector;
    }
    size_t countSpans(const profiler::Collector& collector, uint32_t mark)
    {
        size_t count = 0;
        for (const auto& frame : collector.frames())
            for (const auto& span : frame.spans) count += span.name == mark;
        return count;
    }
}

TEST(Profiler, NestedScopesKeepDepthAndThreadName)
{
    auto& collector = freshCollector();
    const auto outer = profiler::name("test.outer"), inner = profiler::name("test.inner");
    collector.beginFrame();
    std::thread([&] {
        profiler::setThreadName("Test thread");
        const profiler::Scope a(outer);
        { const profiler::Scope b(inner); }
    }).join();
    collector.collect();
    collector.beginFrame();
    const auto* frame = collector.latest();
    ASSERT_NE(frame, nullptr);
    ASSERT_EQ(frame->spans.size(), 2u);
    const auto& first = frame->spans[0].name == inner ? frame->spans[0] : frame->spans[1];
    const auto& second = &first == &frame->spans[0] ? frame->spans[1] : frame->spans[0];
    EXPECT_EQ(first.name, inner);  EXPECT_EQ(first.depth, 1u);
    EXPECT_EQ(second.name, outer); EXPECT_EQ(second.depth, 0u);
    EXPECT_LE(second.start, first.start);
    EXPECT_GE(second.end, first.end);
    EXPECT_EQ(collector.threadName(first.thread), "Test thread");
    EXPECT_EQ(collector.nameOf(outer), "test.outer");
}

TEST(Profiler, InterningIsStable)
{
    const auto a = profiler::name("test.same");
    EXPECT_EQ(profiler::name("test.same"), a);
    EXPECT_EQ(profiler::find("test.same"), a);
    EXPECT_EQ(profiler::find("test.never-used"), 0u);
    EXPECT_NE(profiler::name("test.other"), a);
}

TEST(Profiler, ConcurrentWritersLoseNothingWhileTheCollectorDrains)
{
    auto& collector = freshCollector();
    collector.beginFrame();
    const auto mark = profiler::name("test.concurrent");
    const auto droppedBefore = collector.dropped(); // cumulative over the process
    constexpr int threads = 8, marks = 40000; // above the ring capacity: drains must interleave
    std::atomic<int> running{ threads };
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&] {
            for (int i = 0; i < marks; ++i) {
                const profiler::Scope scope(mark);
                if ((i & 1023) == 0) std::this_thread::yield();
            }
            --running;
        });
    while (running.load() > 0) collector.collect();
    for (auto& worker : workers) worker.join();
    collector.collect();
    const auto collected = countSpans(collector, mark);
    EXPECT_EQ(collected + (collector.dropped() - droppedBefore), size_t(threads) * marks);
    EXPECT_GT(collected, 0u);
}

TEST(Profiler, DisabledRecordsNothing)
{
    auto& collector = freshCollector();
    collector.beginFrame();
    const auto mark = profiler::name("test.disabled");
    profiler::setEnabled(false);
    { const profiler::Scope scope(mark); }
    profiler::value(mark, 1.0);
    profiler::setEnabled(true);
    collector.collect();
    EXPECT_EQ(countSpans(collector, mark), 0u);
    EXPECT_TRUE(collector.frames().back().samples.empty());
}

TEST(Profiler, MarksBelongToTheFrameTheyStartedIn)
{
    auto& collector = freshCollector();
    const auto mark = profiler::name("test.frame");
    collector.beginFrame();
    { const profiler::Scope scope(mark); }
    collector.beginFrame();
    { const profiler::Scope scope(mark); }
    { const profiler::Scope scope(mark); }
    collector.collect();
    collector.beginFrame();
    const auto& frames = collector.frames();
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_EQ(frames[0].spans.size(), 1u);
    EXPECT_EQ(frames[1].spans.size(), 2u);
    EXPECT_TRUE(frames[2].spans.empty());
    EXPECT_GT(frames[0].end, frames[0].start);
    EXPECT_EQ(collector.latest(), &frames[1]);
}

TEST(Profiler, StatsCountFramesTwoFramesAfterTheyClose)
{
    auto& collector = freshCollector();
    const auto mark = profiler::name("test.stat");
    const auto counter = profiler::name("test.counter");
    for (int frame = 0; frame < 6; ++frame) {
        collector.beginFrame();
        for (int i = 0; i < 3; ++i) { const profiler::Scope scope(mark); }
        profiler::value(counter, frame);
        collector.collect();
    }
    // Frames 1..6 opened; frames 1..4 are counted (the last two are still open to late marks).
    const auto stat = collector.stat(mark);
    EXPECT_DOUBLE_EQ(stat.callsPerFrame, 3.0);
    EXPECT_GT(stat.maxMs, 0.0);
    bool sawCounter = false;
    for (const auto& s : collector.stats())
        if (s.name == counter) {
            sawCounter = true;
            EXPECT_TRUE(s.isValue);
            EXPECT_DOUBLE_EQ(s.lastValue, 3.0);
            EXPECT_DOUBLE_EQ(s.avgValue, 1.5);
        }
    EXPECT_TRUE(sawCounter);
    const auto threads = collector.threads();
    ASSERT_FALSE(threads.empty());
    EXPECT_GE(threads.front().busy, 0.0);
    EXPECT_LE(threads.front().busy, 1.0);
}

TEST(Profiler, ThreadLoadAfterResetCoversOnlyNewFrames)
{
    auto& collector = freshCollector();
    const auto mark = profiler::name("test.busy");
    // Busy for `busy` ms of a ~4 ms frame.
    const auto frame = [&](int busy) {
        collector.beginFrame();
        { const profiler::Scope scope(mark); std::this_thread::sleep_for(std::chrono::milliseconds(busy)); }
        std::this_thread::sleep_for(std::chrono::milliseconds(4 - busy));
        collector.collect();
    };
    for (size_t i = 0; i < profiler::Collector::StatFrames + 5; ++i) frame(4);
    collector.reset();
    for (size_t i = 0; i < profiler::Collector::StatFrames + 60; ++i) frame(1); // the window turns over
    const auto threads = collector.threads();
    ASSERT_EQ(threads.size(), 1u);
    EXPECT_GT(threads.front().busy, 0.1);  // about a quarter: frames from before the reset
    EXPECT_LT(threads.front().busy, 0.6);  // are neither counted nor subtracted
}

TEST(Profiler, StatWindowForgetsOldFrames)
{
    auto& collector = freshCollector();
    const auto mark = profiler::name("test.window");
    collector.beginFrame();
    { const profiler::Scope scope(mark); }
    collector.collect();
    for (size_t i = 0; i < profiler::Collector::StatFrames + 4; ++i) { collector.beginFrame(); collector.collect(); }
    EXPECT_EQ(collector.stat(mark).callsPerFrame, 0.0);
    EXPECT_EQ(collector.stat(mark).maxMs, 0.0);
}

TEST(Profiler, StatMaxFollowsTheWindow)
{
    auto& collector = freshCollector();
    const auto mark = profiler::name("test.peak");
    const auto frame = [&](int sleepMs) {
        collector.beginFrame();
        {
            const profiler::Scope scope(mark);
            if (sleepMs) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
        collector.collect();
    };
    frame(5);
    for (int i = 0; i < 4; ++i) frame(0);
    EXPECT_GE(collector.stat(mark).maxMs, 4.0); // the slow frame is counted
    for (size_t i = 0; i < profiler::Collector::StatFrames; ++i) frame(0);
    const auto& stat = collector.stat(mark);
    EXPECT_GT(stat.callsPerFrame, 0.0);         // the mark is still in the window,
    EXPECT_LT(stat.maxMs, 4.0);                 // its slow call is not
    // stat() is the stats() entry, found without a search.
    const auto& stats = collector.stats();
    const auto entry = std::find_if(stats.begin(), stats.end(), [&](const profiler::Stat& s) { return s.name == mark; });
    ASSERT_NE(entry, stats.end());
    EXPECT_EQ(&*entry, &stat);
    EXPECT_EQ(collector.stat(profiler::name("test.peak.never")).name, 0u);
}

TEST(Profiler, ExitedThreadsAreReleasedAfterTheirMarksAreCollected)
{
    auto& collector = freshCollector();
    collector.beginFrame();
    const auto mark = profiler::name("test.exited");
    size_t before = 0;
    {
        auto& registry = profiler::details::registry();
        std::lock_guard lock(registry.threadsMutex);
        before = registry.threads.size();
    }
    std::thread([&] { const profiler::Scope scope(mark); }).join();
    collector.collect();
    EXPECT_EQ(countSpans(collector, mark), 1u);
    auto& registry = profiler::details::registry();
    std::lock_guard lock(registry.threadsMutex);
    EXPECT_LE(registry.threads.size(), before);
}

TEST(Profiler, ChromeTraceExport)
{
    auto& collector = freshCollector();
    const auto mark = profiler::name("test \"quoted\" mark");
    collector.beginFrame();
    { const profiler::Scope scope(mark); }
    profiler::value(profiler::name("test.export.value"), 7);
    collector.collect();
    collector.beginFrame();
    const auto path = std::filesystem::temp_directory_path() / "csyren_profiler_test.json";
    ASSERT_TRUE(collector.exportChromeTrace(path));
    std::ifstream in(path);
    std::stringstream text; text << in.rdbuf();
    const auto json = text.str();
    std::filesystem::remove(path);
    EXPECT_EQ(json.front(), '{');
    EXPECT_NE(json.find("\"traceEvents\""), std::string::npos);
    EXPECT_NE(json.find("\"ph\":\"X\""), std::string::npos);
    EXPECT_NE(json.find("\"ph\":\"C\""), std::string::npos);
    EXPECT_NE(json.find("thread_name"), std::string::npos);
    EXPECT_NE(json.find("test \\\"quoted\\\" mark"), std::string::npos);
}

TEST(Profiler, MacrosCompileAndRecord)
{
    auto& collector = freshCollector();
    collector.beginFrame();
    {
        CS_PROFILE_SCOPE("test.macro");
        CS_PROFILE_VALUE("test.macro.value", 3);
    }
    collector.collect();
    EXPECT_EQ(countSpans(collector, profiler::find("test.macro")), 1u);
}
