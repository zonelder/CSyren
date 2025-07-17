#ifndef __CSYREN_TIME__
#define __CSYREN_TIME__

#include <chrono>
#include <ratio>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace csyren::core::details
{
	class TimeHandler;
}


namespace csyren::core
{
	class Time
	{
		friend class details::TimeHandler;
		using Clock_t = std::chrono::high_resolution_clock;
	public:
		float totalTime() const noexcept { return _time; }
		float deltaTime() const noexcept { return _deltaTime; }
		
		float timeScale() const noexcept { return _timeScale; }
		void timeScale(const float scale) { _timeScale = scale; }

		float totalTimeUnscaled() const noexcept { return _unscaledTime; };
		float deltaTimeUnscaled() const noexcept { return _unscaledDeltaTime; }

		size_t frameCount() const noexcept { return _frameCount; }

	private:
		float _time{ 0.0f };
		float  _deltaTime{ 0.0f };
		float _timeScale{ 1.0f };

		float _unscaledDeltaTime{ 0.0f };
		float _unscaledTime{ 0.0f };
		size_t _frameCount{ 0 };
	};
}

namespace csyren::core::details
{
	class TimeHandler
	{
	public:
		TimeHandler() :
			_startTime(Time::Clock_t::now()),
			_lastFrameTime(_startTime)
		{
			
		}
		void update(Time& time)
		{
			const auto now = Time::Clock_t::now();
			time._unscaledDeltaTime = std::chrono::duration<float>(now - _lastFrameTime).count();
			time._unscaledTime = std::chrono::duration<float>(now - _startTime).count();
			_lastFrameTime = now;

			time._deltaTime = time._unscaledDeltaTime * time._timeScale;
			time._time += time._deltaTime;
			++time._frameCount;

		}
	private:
		Time::Clock_t::time_point _startTime;
		Time::Clock_t::time_point _lastFrameTime;
	};
}

#endif
