#pragma once

namespace csyren::core
{
	class System
	{
	public:
		virtual void init() {}

		virtual void update(){}
		virtual void startFrame() {};
		virtual void onFrame() {}
		virtual void endFrame() {};

		virtual void shutdown(){}

	private:

	};
}