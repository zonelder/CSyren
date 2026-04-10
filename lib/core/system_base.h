#pragma once
#include "core/context.h"

namespace csyren::core
{
	class System
	{
	public:
		virtual void init(ServiceContext& event) {}

		virtual void update(ServiceContext& event){}
		virtual void startFrame(ServiceContext& event) {};
		virtual void onFrame(ServiceContext& event) {}
		virtual void endFrame(ServiceContext& event) {};

		virtual void shutdown(ServiceContext& event){}

	private:

	};
}