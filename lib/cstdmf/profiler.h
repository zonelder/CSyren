#pragma once
// CPU profiler: named marks recorded per thread and collected into frames.
//
//   CS_PROFILE_SCOPE("Physics step");      // from here to the end of the block
//   CS_PROFILE_VALUE("Spawned", count);    // one sample of a counter
//
// Code that marks knows nothing about threads. Each thread appends to its own
// ring buffer, registered on its first mark: marking takes no lock and never
// waits, and threads never write to shared memory. A full ring drops the mark
// and counts the drop. Once a frame the collector (main thread) drains every
// ring and assembles the frames piece by piece: which mark, on which thread,
// when and for how long. Threads outside the job system (physics workers, the
// presentation thread) are just more tracks.
//
// Frames: the main loop calls beginFrame() at the start of each frame and
// collect() at its end. A mark belongs to the frame in which it started. Stats
// (per mark: time per frame, longest call, calls per frame, threads) cover the
// last StatFrames frames; a frame is counted only when the frame after it has
// closed too, so marks of other threads that end up to a frame late are included.
// The stats are kept as running totals and assembled at most once per counted
// frame, when first asked for: windows may query them every frame.
//
// Cost while enabled: two clock reads per scope; while disabled: one relaxed
// load. CSYREN_PROFILER=0 compiles the marks out (the collector stays, empty).
#ifndef CSYREN_PROFILER
#define CSYREN_PROFILER 1
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace csyren::profiler
{
    using Ticks = int64_t; // nanoseconds of a steady clock

    inline Ticks now() noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    namespace details
    {
        enum class Kind : uint8_t { Span, Value };
        struct Event
        {
            Ticks start = 0;
            Ticks end = 0;
            double value = 0.0;
            uint32_t name = 0;
            uint16_t depth = 0;
            Kind kind = Kind::Span;
        };

        // Single writer (the owning thread), single reader (the collector).
        class ThreadLog
        {
        public:
            static constexpr uint64_t Capacity = 1u << 14;
            explicit ThreadLog(uint16_t index) : index(index), name("Thread " + std::to_string(index)),
                _events(std::make_unique<Event[]>(Capacity)) {}

            bool push(const Event& event) noexcept
            {
                const auto head = _head.load(std::memory_order_relaxed);
                if (head - _tail.load(std::memory_order_acquire) >= Capacity) {
                    dropped.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
                _events[head & (Capacity - 1)] = event;
                _head.store(head + 1, std::memory_order_release);
                return true;
            }
            template<class Fn> void drain(Fn&& fn)
            {
                auto tail = _tail.load(std::memory_order_relaxed);
                const auto head = _head.load(std::memory_order_acquire);
                for (; tail != head; ++tail) fn(_events[tail & (Capacity - 1)]);
                _tail.store(tail, std::memory_order_release);
            }
            bool empty() const noexcept
            {
                return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_relaxed);
            }

            const uint16_t index;
            std::string name;                    // guarded by the registry mutex
            std::atomic<bool> alive{ true };     // false once the thread has exited
            std::atomic<uint64_t> dropped{ 0 };
            uint16_t depth = 0;                  // owner thread only
        private:
            std::unique_ptr<Event[]> _events;
            alignas(64) std::atomic<uint64_t> _head{ 0 };
            alignas(64) std::atomic<uint64_t> _tail{ 0 };
        };

        struct Registry
        {
            std::atomic<bool> enabled{ true };
            std::mutex threadsMutex;
            std::vector<std::shared_ptr<ThreadLog>> threads; // live, and exited but not yet drained
            uint16_t nextThread = 0;
            std::mutex namesMutex;
            std::deque<std::string> names{ "" };              // id 0: unnamed
            std::unordered_map<std::string, uint32_t> ids;
        };
        inline Registry& registry()
        {
            static Registry instance;
            return instance;
        }

        // Owned by the thread; marks the log as exited when the thread ends.
        struct ThreadSlot
        {
            std::shared_ptr<ThreadLog> log;
            ~ThreadSlot() { if (log) log->alive.store(false, std::memory_order_release); }
        };
        inline ThreadLog& threadLog()
        {
            thread_local ThreadSlot slot;
            if (!slot.log) {
                auto& r = registry();
                std::lock_guard lock(r.threadsMutex);
                slot.log = std::make_shared<ThreadLog>(r.nextThread++);
                r.threads.push_back(slot.log);
            }
            return *slot.log;
        }
    }

    inline bool enabled() noexcept
    {
#if CSYREN_PROFILER
        return details::registry().enabled.load(std::memory_order_relaxed);
#else
        return false;
#endif
    }
    inline void setEnabled(bool value) noexcept { details::registry().enabled.store(value, std::memory_order_relaxed); }

    // Interns a mark name (once per call site or per graph build, not per mark).
    inline uint32_t name(std::string_view text)
    {
        auto& r = details::registry();
        std::lock_guard lock(r.namesMutex);
        const auto found = r.ids.find(std::string(text));
        if (found != r.ids.end()) return found->second;
        const auto id = static_cast<uint32_t>(r.names.size());
        r.names.emplace_back(text);
        r.ids.emplace(r.names.back(), id);
        return id;
    }
    // Id of an interned name, 0 when the name was never used.
    inline uint32_t find(std::string_view text)
    {
        auto& r = details::registry();
        std::lock_guard lock(r.namesMutex);
        const auto found = r.ids.find(std::string(text));
        return found == r.ids.end() ? 0 : found->second;
    }

    // Names the calling thread's track ("Main", "Worker 2", "Jolt 1", ...).
    inline void setThreadName(std::string_view text)
    {
        auto& log = details::threadLog();
        auto& r = details::registry();
        std::lock_guard lock(r.threadsMutex);
        log.name = std::string(text);
    }

    class Scope
    {
    public:
        // Mark 0 (the unnamed id) records nothing.
        explicit Scope(uint32_t mark) noexcept
        {
            if (!mark || !enabled()) return;
            _log = &details::threadLog();
            _mark = mark;
            _depth = _log->depth++;
            _start = now();
        }
        ~Scope()
        {
            if (!_log) return;
            details::Event event;
            event.start = _start; event.end = now(); event.name = _mark; event.depth = _depth;
            --_log->depth;
            _log->push(event);
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        details::ThreadLog* _log = nullptr;
        Ticks _start = 0;
        uint32_t _mark = 0;
        uint16_t _depth = 0;
    };

    inline void value(uint32_t mark, double sample) noexcept
    {
        if (!enabled()) return;
        auto& log = details::threadLog();
        details::Event event;
        event.start = event.end = now(); event.value = sample; event.name = mark;
        event.depth = log.depth; event.kind = details::Kind::Value;
        log.push(event);
    }

    // ------------------------------------------------------------------ collected data
    struct Span { Ticks start; Ticks end; uint32_t name; uint16_t depth; uint16_t thread; };
    struct Sample { Ticks time; double value; uint32_t name; uint16_t thread; };
    struct Frame
    {
        uint64_t index = 0;
        Ticks start = 0;
        Ticks end = 0;          // 0 while the frame is open
        std::vector<Span> spans;
        std::vector<Sample> samples;
        double ms() const noexcept { return end > start ? double(end - start) * 1e-6 : 0.0; }
    };
    struct Stat
    {
        uint32_t name = 0;
        double msPerFrame = 0;  // total time per frame, averaged over the window
        double maxMs = 0;       // longest single call in the window
        double lastMs = 0;      // total in the newest counted frame
        double callsPerFrame = 0;
        std::vector<uint16_t> threads;
        bool isValue = false;   // CS_PROFILE_VALUE: lastValue/avgValue instead of times
        double lastValue = 0;
        double avgValue = 0;
    };
    struct ThreadInfo { uint16_t index; std::string name; bool alive; double busy; }; // busy: share of the frame (depth 0 marks)

    // Main-thread only (beginFrame/collect from the main loop, queries from the UI).
    class Collector
    {
    public:
        static constexpr size_t FramesKept = 300;
        static constexpr size_t StatFrames = 120;

        static Collector& instance()
        {
            static Collector collector;
            return collector;
        }

        void beginFrame()
        {
            const auto t = now();
            if (!_frames.empty()) _frames.back().end = t;
            Frame frame; frame.index = ++_frameIndex; frame.start = t;
            _frames.push_back(std::move(frame));
            while (_frames.size() > FramesKept) _frames.pop_front();
            // Count the frame before the one that just closed (late marks are in by now).
            if (_frames.size() >= 3) count(_frames[_frames.size() - 3]);
        }

        void collect()
        {
            std::vector<std::shared_ptr<details::ThreadLog>> logs;
            {
                auto& r = details::registry();
                std::lock_guard lock(r.threadsMutex);
                logs = r.threads;
                for (const auto& log : logs) {
                    if (log->index >= _threads.size()) _threads.resize(log->index + 1);
                    _threads[log->index].index = log->index;
                    _threads[log->index].name = log->name;
                    _threads[log->index].alive = log->alive.load(std::memory_order_acquire);
                }
            }
            uint64_t dropped = 0;
            for (const auto& log : logs) {
                log->drain([&](const details::Event& event) { place(event, log->index); });
                dropped += log->dropped.load(std::memory_order_relaxed);
            }
            _dropped = dropped + _retiredDropped;
            // Exited threads are released once their last marks are collected.
            auto& r = details::registry();
            std::lock_guard lock(r.threadsMutex);
            std::erase_if(r.threads, [&](const std::shared_ptr<details::ThreadLog>& log) {
                if (log->alive.load(std::memory_order_acquire) || !log->empty()) return false;
                _retiredDropped += log->dropped.load(std::memory_order_relaxed);
                return true;
            });
        }

        const std::deque<Frame>& frames() const noexcept { return _frames; }
        // The newest closed frame (the one before the frame in progress).
        const Frame* latest() const noexcept
        {
            for (auto it = _frames.rbegin(); it != _frames.rend(); ++it) if (it->end) return &*it;
            return nullptr;
        }
        uint64_t dropped() const noexcept { return _dropped; }
        uint64_t late() const noexcept { return _late; }

        std::string nameOf(uint32_t id) const
        {
            auto& r = details::registry();
            std::lock_guard lock(r.namesMutex);
            return id < r.names.size() ? r.names[id] : std::string();
        }
        std::string threadName(uint16_t index) const { return index < _threads.size() ? _threads[index].name : std::string(); }

        // Per-thread load over the stat window, for the threads seen in it.
        std::vector<ThreadInfo> threads() const
        {
            std::vector<ThreadInfo> result;
            for (const auto& [index, busy] : _threadBusy) {
                ThreadInfo info{ index, threadName(index), index < _threads.size() && _threads[index].alive, 0.0 };
                info.busy = _countedTime > 0 ? busy / _countedTime : 0.0;
                result.push_back(std::move(info));
            }
            std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
            return result;
        }

        // Every mark seen in the window. The reference stays valid until the next
        // beginFrame() or reset().
        const std::vector<Stat>& stats() const
        {
            refreshStats();
            return _stats;
        }
        // One mark's stat (by interned id), without a search; an empty Stat (name 0)
        // when the mark was not seen in the window. Valid as long as stats().
        const Stat& stat(uint32_t id) const
        {
            static const Stat none;
            refreshStats();
            const auto it = _statIndex.find(id);
            return it == _statIndex.end() ? none : _stats[it->second];
        }

        // Chrome trace JSON of the last `frameCount` closed frames; opens in
        // https://ui.perfetto.dev and chrome://tracing.
        bool exportChromeTrace(const std::filesystem::path& path, size_t frameCount = FramesKept) const
        {
            std::vector<const Frame*> frames;
            for (auto it = _frames.rbegin(); it != _frames.rend() && frames.size() < frameCount; ++it)
                if (it->end) frames.push_back(&*it);
            std::reverse(frames.begin(), frames.end());
            return exportChromeTrace(path, frames);
        }
        // The same for given frames (e.g. frames copied when the view was paused).
        bool exportChromeTrace(const std::filesystem::path& path, const std::vector<const Frame*>& frames) const
        {
            std::ofstream out(path, std::ios::binary);
            if (!out) return false;
            const Ticks origin = frames.empty() ? 0 : frames.front()->start;
            const auto us = [&](Ticks t) { return double(t - origin) * 1e-3; };
            std::unordered_map<uint32_t, std::string> names;
            const auto escaped = [&](uint32_t id) -> const std::string& {
                auto [it, inserted] = names.try_emplace(id);
                if (inserted) it->second = escape(nameOf(id));
                return it->second;
            };
            bool first = true;
            const auto separator = [&] { if (!first) out << ",\n"; first = false; };
            char number[64];
            out << "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n";
            std::vector<bool> seen(_threads.size());
            for (const auto* frame : frames)
                for (const auto& span : frame->spans) if (span.thread < seen.size()) seen[span.thread] = true;
            for (size_t i = 0; i < seen.size(); ++i) {
                if (!seen[i]) continue;
                separator();
                out << "{\"ph\":\"M\",\"pid\":1,\"tid\":" << i << ",\"name\":\"thread_name\",\"args\":{\"name\":\""
                    << escape(_threads[i].name) << "\"}}";
            }
            for (const auto* frame : frames) {
                separator();
                std::snprintf(number, sizeof(number), "%.3f", us(frame->start));
                out << "{\"ph\":\"i\",\"s\":\"g\",\"pid\":1,\"tid\":0,\"name\":\"Frame " << frame->index << "\",\"ts\":" << number << "}";
                for (const auto& span : frame->spans) {
                    separator();
                    std::snprintf(number, sizeof(number), "%.3f,\"dur\":%.3f", us(span.start), double(span.end - span.start) * 1e-3);
                    out << "{\"ph\":\"X\",\"pid\":1,\"tid\":" << span.thread << ",\"name\":\"" << escaped(span.name) << "\",\"ts\":" << number << "}";
                }
                for (const auto& sample : frame->samples) {
                    separator();
                    std::snprintf(number, sizeof(number), "%.3f,\"args\":{\"value\":%.6g}", us(sample.time), sample.value);
                    out << "{\"ph\":\"C\",\"pid\":1,\"tid\":" << sample.thread << ",\"name\":\"" << escaped(sample.name) << "\",\"ts\":" << number << "}";
                }
            }
            out << "\n]}\n";
            return bool(out);
        }

        // Drops every frame and statistic (the thread buffers are kept).
        void reset()
        {
            _frames.clear(); _window.clear(); _totals.clear(); _threadBusy.clear();
            _windowBusy.clear(); _windowTime.clear(); _countedTime = 0; _late = 0;
            _stats.clear(); _statIndex.clear(); _statsValid = false;
        }

    private:
        struct Accumulated
        {
            double ms = 0;
            double maxMs = 0;
            uint32_t calls = 0;
            double lastValue = 0;
            double valueSum = 0;
            bool isValue = false;
            std::vector<uint16_t> threads;
        };
        using FrameTotals = std::unordered_map<uint32_t, Accumulated>;
        // One mark over the stat window.
        struct Running
        {
            double ms = 0;
            uint32_t calls = 0;
            double valueSum = 0;
            bool isValue = false;
            std::vector<uint16_t> threads;
            // Longest call of the window: (counted frame, ms), the ms decreasing, so
            // the front is the maximum and leaves with its frame.
            std::deque<std::pair<uint64_t, double>> peaks;
            uint64_t lastCounted = 0; // the counted frame of lastMs
            double lastMs = 0;
            double lastValue = 0;     // the newest sample in the window
        };

        static void addThread(std::vector<uint16_t>& threads, uint16_t thread)
        {
            if (std::find(threads.begin(), threads.end(), thread) == threads.end()) threads.push_back(thread);
        }
        static std::string escape(const std::string& text)
        {
            std::string result;
            result.reserve(text.size());
            for (const char c : text) {
                if (c == '"' || c == '\\') { result += '\\'; result += c; }
                else if (static_cast<unsigned char>(c) < 0x20) result += ' ';
                else result += c;
            }
            return result;
        }

        void place(const details::Event& event, uint16_t thread)
        {
            Frame* frame = nullptr;
            for (auto it = _frames.rbegin(); it != _frames.rend(); ++it)
                if (event.start >= it->start) { frame = &*it; break; }
            if (!frame) { ++_late; return; }
            if (event.kind == details::Kind::Value)
                frame->samples.push_back({ event.start, event.value, event.name, thread });
            else
                frame->spans.push_back({ event.start, event.end, event.name, event.depth, thread });
        }

        void count(const Frame& frame)
        {
            if (!frame.end) return;
            FrameTotals totals;
            std::unordered_map<uint16_t, double> busy;
            for (const auto& span : frame.spans) {
                const double ms = double(span.end - span.start) * 1e-6;
                auto& total = totals[span.name];
                total.ms += ms; total.maxMs = (std::max)(total.maxMs, ms); ++total.calls;
                addThread(total.threads, span.thread);
                if (span.depth == 0) busy[span.thread] += ms;
            }
            for (const auto& sample : frame.samples) {
                auto& total = totals[sample.name];
                total.isValue = true; total.lastValue = sample.value; total.valueSum += sample.value; ++total.calls;
                addThread(total.threads, sample.thread);
            }
            // Add the frame to the running totals, retire the oldest one.
            ++_counted;
            _statsValid = false;
            for (const auto& [id, value] : totals) {
                auto& total = _totals[id];
                total.ms += value.ms; total.calls += value.calls; total.valueSum += value.valueSum;
                total.isValue = total.isValue || value.isValue;
                for (auto thread : value.threads) addThread(total.threads, thread);
                while (!total.peaks.empty() && total.peaks.back().second <= value.maxMs) total.peaks.pop_back();
                total.peaks.emplace_back(_counted, value.maxMs);
                total.lastCounted = _counted; total.lastMs = value.ms;
                if (value.isValue) total.lastValue = value.lastValue;
            }
            for (const auto& [thread, ms] : busy) _threadBusy[thread] += ms;
            _countedTime += frame.ms();
            _window.push_back(std::move(totals));
            _windowBusy.push_back(std::move(busy));
            _windowTime.push_back(frame.ms());
            if (_window.size() > StatFrames) {
                const uint64_t retired = _counted - _window.size() + 1;
                for (const auto& [id, value] : _window.front()) {
                    auto it = _totals.find(id);
                    if (it == _totals.end()) continue;
                    it->second.ms -= value.ms; it->second.calls -= value.calls; it->second.valueSum -= value.valueSum;
                    auto& peaks = it->second.peaks;
                    if (!peaks.empty() && peaks.front().first <= retired) peaks.pop_front();
                    if (it->second.calls == 0) _totals.erase(it);
                }
                for (const auto& [thread, ms] : _windowBusy.front()) {
                    auto it = _threadBusy.find(thread);
                    if (it == _threadBusy.end()) continue;
                    it->second -= ms;
                    if (it->second <= 1e-9) _threadBusy.erase(it);
                }
                _countedTime -= _windowTime.front();
                _window.pop_front(); _windowBusy.pop_front(); _windowTime.pop_front();
            }
        }

        std::deque<Frame> _frames;
        uint64_t _frameIndex = 0;
        std::deque<FrameTotals> _window;
        std::deque<std::unordered_map<uint16_t, double>> _windowBusy;
        std::deque<double> _windowTime;
        std::unordered_map<uint32_t, Running> _totals;
        uint64_t _counted = 0; // frames counted so far
        std::unordered_map<uint16_t, double> _threadBusy;
        double _countedTime = 0;
        std::vector<ThreadInfo> _threads;
        uint64_t _dropped = 0, _retiredDropped = 0, _late = 0;
        // stats(): assembled from _totals once per counted frame, on demand.
        mutable std::vector<Stat> _stats;
        mutable std::unordered_map<uint32_t, size_t> _statIndex;
        mutable bool _statsValid = false;

        void refreshStats() const
        {
            if (_statsValid) return;
            _stats.clear(); _statIndex.clear();
            _stats.reserve(_totals.size());
            const double frames = double((std::max<size_t>)(_window.size(), 1));
            for (const auto& [id, total] : _totals) {
                Stat stat;
                stat.name = id;
                stat.isValue = total.isValue;
                stat.msPerFrame = total.ms / frames;
                stat.maxMs = total.peaks.empty() ? 0.0 : total.peaks.front().second;
                stat.lastMs = total.lastCounted == _counted ? total.lastMs : 0.0;
                stat.callsPerFrame = double(total.calls) / frames;
                stat.threads = total.threads;
                stat.lastValue = total.lastValue;
                stat.avgValue = total.calls ? total.valueSum / double(total.calls) : 0.0;
                _statIndex.emplace(id, _stats.size());
                _stats.push_back(std::move(stat));
            }
            _statsValid = true;
        }
    };

    // Main loop helpers.
    inline void beginFrame() { Collector::instance().beginFrame(); }
    inline void collect() { Collector::instance().collect(); }
}

#define CS_PROFILE_CONCAT_INNER(a, b) a##b
#define CS_PROFILE_CONCAT(a, b) CS_PROFILE_CONCAT_INNER(a, b)
#if CSYREN_PROFILER
#define CS_PROFILE_SCOPE(literal) \
    static const uint32_t CS_PROFILE_CONCAT(csProfileMark_, __LINE__) = ::csyren::profiler::name(literal); \
    const ::csyren::profiler::Scope CS_PROFILE_CONCAT(csProfileScope_, __LINE__)(CS_PROFILE_CONCAT(csProfileMark_, __LINE__))
#define CS_PROFILE_VALUE(literal, sample) \
    do { static const uint32_t csProfileMark = ::csyren::profiler::name(literal); \
         ::csyren::profiler::value(csProfileMark, double(sample)); } while (false)
#else
#define CS_PROFILE_SCOPE(literal) ((void)0)
#define CS_PROFILE_VALUE(literal, sample) ((void)0)
#endif
