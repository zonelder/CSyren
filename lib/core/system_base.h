#pragma once

#include <string_view>
#include <functional>
#include <memory>
#include <unordered_map>
#include "cstdmf/log.h"
#include "scene_context.h"
#include <optional>
#include "task_description.h"

namespace csyren::core
{
	class System
	{
	public:
        virtual ~System() = default;
        void attach(SceneContext context)
        {
            _context.emplace(context);
            try { onAttach(); }
            catch (...) { onDetach(); _context.reset(); throw; }
        }
        void detach() noexcept
        {
            if (_context) { onDetach(); _context.reset(); }
        }
        bool attached() const noexcept { return _context.has_value(); }
		virtual void init() {}

		virtual TaskDescription describe() const { TaskDescription d; d.phase = "simulation"; d.affinity = TaskAffinity::MainThread; d.exclusiveScene = true; d.dynamicResources = true; return d; }
        virtual void execute() { update(); onFrame(); }
        Scene* boundScene() const { return _context ? &_context->scene : nullptr; }
        virtual void update(){}
		virtual void startFrame() {};
		virtual void onFrame() {}
		virtual void endFrame() {};

		virtual void shutdown(){}

    protected:
        Scene& scene() const { return _context->scene; }
        EventBus2& sceneBus() const { return _context->bus; }
        virtual void onAttach() {}
        virtual void onDetach() noexcept {}
	private:
        std::optional<SceneContext> _context;

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

            bool contains(std::string_view name) const { return _creators.find(std::string(name)) != _creators.end(); }

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
