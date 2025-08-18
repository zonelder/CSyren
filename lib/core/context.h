#ifndef __CSYREN_CONTEXT__
#define __CSYREN_CONTEXT__

namespace DirectX
{
	struct XMMATRIX;
}


namespace csyren::render
{
	class ResourceManager;
}

namespace csyren::core
{
	namespace events
	{
		class EventBus2;
	}
	class Scene;
	class Time;

}

namespace csyren::core::input
{
	class Devices;
}


namespace csyren::core::events
{

	//base class for generic app event
	struct ContextualEvent
	{
		const input::Devices& devices;
		Scene& scene;
		render::ResourceManager& resources;
		EventBus2& bus;
	};

	//class for Update call
	struct UpdateEvent : ContextualEvent
	{
		Time& time;
	};

	//class for draw call
	struct DrawEvent : ContextualEvent
	{
		render::Renderer& render;
	};

	//class for sustem init\shutdown
	struct SystemEvent : ContextualEvent
	{
		Time& time;
		render::Renderer& render;
	};
}

#endif
