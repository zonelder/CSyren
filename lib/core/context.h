#ifndef __CSYREN_CONTEXT__
#define __CSYREN_CONTEXT__

#include <unordered_map>
#include <cassert>
#include "family_generator.h"

namespace csyren::core::reflection
{
	class ServiceFamilyID {};
	using ServiceFamily = Family<ServiceFamilyID>;
}

namespace csyren::core
{
	class ServiceContext
	{
	public:
		template<typename T> void registerService(T* service)
		{
			auto type_id = reflection::ServiceFamily::getID<std::remove_const_t<T>>();
			assert(_services.find(type_id) == _services.end() && "Attempt to register service but its already registered.");
			_services[type_id] = const_cast<std::remove_const_t<T>*>(service);
		}	

		template<typename T>const T* get() const
		{
			auto it = _services.find(reflection::ServiceFamily::getID<std::remove_const_t<T>>());
			assert(it != _services.end() && "Attempt to get service but none it has not registered.");

			return reinterpret_cast<const T*>(it->second);
		}

		template<typename T>
		T* get()
		{
			auto it = _services.find(reflection::ServiceFamily::getID<std::remove_const_t<T>>());
			assert(it != _services.end() && "Service not registered.");
			return reinterpret_cast<T*>(it->second);
		}

	private:
		std::unordered_map<size_t, void*> _services;
	};


}

#endif
