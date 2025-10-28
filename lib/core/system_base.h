#pragma once
#include "core/context.h"

namespace csyren::core
{
	class System
	{
	public:
		virtual void init(ServiceContext& event) {}

		virtual void update(ServiceContext& event){}
		virtual void draw(ServiceContext& event) {}

		virtual void shutdown(ServiceContext& event){}

	private:

	};
}