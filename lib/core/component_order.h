#ifndef __CSYREN_COMPONENT_ORDER__
#define __CSYREN_COMPONENT_ORDER__

#include "component_base.h"
#include "entity.h"

#include <algorithm>
#include <vector>
#include <type_traits>

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



#endif
