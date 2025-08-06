#pragma once
#include "core/context.h"

namespace csyren::core
{
	class System
	{
	public:
		virtual void init(events::SystemEvent& event) {}

		virtual void update(events::UpdateEvent& event){}
		virtual void draw(events::DrawEvent& event) {}

		virtual void shutdown(events::SystemEvent& event){}

	private:

	};
}