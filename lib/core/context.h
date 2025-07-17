#ifndef __CSYREN_CONTEXT__
#define __CSYREN_CONTEXT__




namespace csyren::core
{
	namespace events
	{
		class EventBus2;
	}
	class Scene;
	class ResourceManager;
	class Time;

}

namespace csyren::core::events
{

	struct ContextualEvent
	{
		Scene& scene;
		ResourceManager& resources;
		EventBus2& bus;
	};

	struct UpdateEvent : ContextualEvent
	{
		Time& time;
	};

	struct DrawEvent : ContextualEvent
	{
		render::Renderer& render;
	};
}

#endif
