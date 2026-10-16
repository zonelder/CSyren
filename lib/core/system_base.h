#pragma once

#include <string_view>
#include <functional>
#include <memory>
#include <unordered_map>
#include "cstdmf/log.h"
#include "task_description.h"

namespace csyren::core
{
	class System
	{
	public:
        virtual ~System() = default;
        virtual void init() {}
        virtual TaskDescription describe() const = 0;
        virtual void execute() {}
        virtual void shutdown() {}

	private:

	};

	namespace details
	{

		using SystemCreator = std::function<std::shared_ptr<System>()>;

		class SystemRegistry
		{
		public:
			static SystemRegistry& instance()
			{
				static SystemRegistry reg;
				return reg;
			}

			void registerSystem(std::string_view name, SystemCreator creator) 
			{
				_creators[std::string(name)] = std::move(creator);
			}

			std::shared_ptr<System> create(std::string_view name) {
				auto it = _creators.find(std::string(name));
				if (it != _creators.end()) {
					return it->second();
				}
				// Логирование ошибки: система не найдена
				log::error("attempt to create system {} but its not defined.\n", name);
				return nullptr;
			}

		private:
			std::unordered_map<std::string, SystemCreator> _creators;
		};
	}
}

#define REGISTER_SYSTEM(SystemClass) \
    namespace { \
        struct SystemClass##_Registrar { \
            SystemClass##_Registrar() { \
                ::csyren::core::details::SystemRegistry::instance().registerSystem( \
                    #SystemClass, \
                    []() -> std::shared_ptr<::csyren::core::System> { \
                        return std::make_shared<SystemClass>(); \
                    } \
                ); \
            } \
        }; \
        static SystemClass##_Registrar g_##SystemClass##_registrar; \
    }