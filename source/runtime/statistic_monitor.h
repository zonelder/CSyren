#pragma once
#include "core/time.h"

#include <array>

namespace csyren
{
    class StatisticMonitor
    {
    public:
        static constexpr size_t RING_BUFFER_SIZE = 600;
        using FrameRingBuffer_t = std::array<float, RING_BUFFER_SIZE>;
        void update(const core::Time& time)
        {
            const float dt = time.deltaTimeUnscaled();

            _frameTimeHistory[_historyOffset] = dt * 1000.0f;
            _historyOffset = (_historyOffset + 1) % _frameTimeHistory.size();

            _accumulatedTime += dt;
            _framesLastSecond++;

            if (_accumulatedTime >= 1.0f)
            {
                _currentFps = static_cast<float>(_framesLastSecond) / _accumulatedTime;
                _framesLastSecond = 0;
                _accumulatedTime = 0.0f;

                _minuteAccumulator += 1.0f;

                if (_minuteAccumulator >= 60.0f)
                {
                    _minFps = _currentFps;
                    _maxFps = _currentFps;
                    _minuteAccumulator = 0.0f;
                }
                else
                {
                    _minFps = std::min(_minFps, _currentFps);
                    _maxFps = std::max(_maxFps, _currentFps);
                }
            }
        }
        constexpr  size_t getHistorySize() const noexcept { return RING_BUFFER_SIZE; }
        size_t getHistoryOffset() const noexcept { return _historyOffset; }
        float getRawFrameTime(size_t index) const noexcept { return _frameTimeHistory[index]; }


        float getCurrentFps() const noexcept { return _currentFps; }
        float getMinFpsLastMinute() const noexcept { return _minFps; }
        float getMaxFpsLastMinute() const noexcept { return _maxFps; }
        void addDrawCalls(uint64_t newCalls) { _drawCalls += newCalls; }
        uint32_t getDrawCalls() const noexcept { return _drawCalls; }
    private:
        float _accumulatedTime{ 0.0f };
        uint32_t _framesLastSecond{ 0 };
        float _currentFps{ 0.0f };

        float _minuteAccumulator{ 0.0f };
        float _minFps{ 9999.0f };
        float _maxFps{ 0.0f };
        FrameRingBuffer_t _frameTimeHistory{};
        size_t _historyOffset{ 0 };
        uint64_t _drawCalls;
    };

    inline float StatisticMonitorFrameTimeGetter(void* data, int idx)
    {
        const auto* monitor = static_cast<const StatisticMonitor*>(data);
        return monitor->getRawFrameTime(idx);
    }
}

