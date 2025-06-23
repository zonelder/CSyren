#ifndef __CSYREN_COMPONENT_BASE__
#define __CSYREN_COMPONENT_BASE__

#include <stdint.h>
#include "family_generator.h"


namespace csyren::core
{
	class Time
	{
	public:
		float deltaTime;
	};

	class Scene;

	class Component
	{
	public:
		using ID = size_t;
		virtual ~Component() = default;
	};
}

namespace csyren::core::reflection
{
	class ComponentFamilyID {};
	using ComponentFamily = Family<ComponentFamilyID>;

	template<typename T>
	concept HasOnCreate = requires(T t, Scene & s) { t.onCreate(s); };

	template<typename T>
	concept HasOnDestroy = requires(T t, Scene & s) { t.onDestroy(s); };

	template<typename T>
	concept HasUpdate = requires(T t, Scene & s, Time & tm) { t.update(s, tm); };
}

namespace csyren::core::reflection::order
{
	struct natural_order_t {};
	struct hierarchy_order_t {};

	template<typename T>	struct is_valid_order : std::false_type {};
	template<>				struct is_valid_order<natural_order_t> : std::true_type {};
	template<>				struct is_valid_order< hierarchy_order_t> : std::true_type {};

	template<typename T, typename = void>
	struct order_type_or_default
	{
		using type = natural_order_t;
	};

	template<typename T>
	struct order_type_or_default<T, std::void_t<typename T::order_type>>
	{
		static_assert(is_valid_order<typename T::order_type>::value, "T::order_type must be valid order tag.");
		using type = typename T::order_type;
	};

	template<typename T>
	using default_order = typename order_type_or_default<T>::type;
}



#endif;