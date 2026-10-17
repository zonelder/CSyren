#pragma once

#include <vector>
#include <algorithm>
#include <memory>
#include <type_traits>
#include <functional>
#include <optional>
#include <unordered_map>
#include "cstdmf/assert_helpler.h"
#include "cstdmf/log.h"
#include "family_generator.h"
#include "task_description.h"

namespace csyren::core::reflection
{
    class ServiceFamilyID {};
    using ServiceFamily = Family<ServiceFamilyID>;
}

namespace csyren::core
{
    // Forward declaration
    namespace details { class ServiceRegistry; }
    class Scene;
    class EventBus2;

    // ============================================================
    // ServiceLocator - non-intrusive access to services
    // ============================================================
    class Services
    {
        friend details::ServiceRegistry;
    public:
        class SceneScope {
        public:
            SceneScope(Scene* scene, EventBus2* bus) : previousScene(_scene), previousBus(_bus) { _scene = scene; _bus = bus; }
            ~SceneScope() { _scene = previousScene; _bus = previousBus; }
            SceneScope(const SceneScope&) = delete;
        private:
            Scene* previousScene;
            EventBus2* previousBus;
        };
        class TaskScope {
        public:
            TaskScope() : _previous(_activeTask) { _activeTask = nullptr; }
            explicit TaskScope(const TaskDescription& description) : _previous(_activeTask) { _activeTask = &description; }
            ~TaskScope() { _activeTask = _previous; }
            TaskScope(const TaskScope&) = delete;
        private:
            const TaskDescription* _previous;
        };
        template<typename T>
        static T* get()
        {
            requireAccess<T>();
            if constexpr (std::is_same_v<std::remove_const_t<T>, Scene>) { if (_scene) return _scene; }
            if constexpr (std::is_same_v<std::remove_const_t<T>, EventBus2>) { if (_bus) return _bus; }
            auto it = _services.find(reflection::ServiceFamily::getID<std::remove_const_t<T>>());
            if (it == _services.end()) throw std::logic_error("Service not registered: " + std::string(typeid(T).name()));
            return reinterpret_cast<T*>(it->second);
        }

        template<typename T>
        static bool has()
        {
            return _services.find(reflection::ServiceFamily::getID<std::remove_const_t<T>>()) != _services.end();
        }

    private:
        template<class T> static void requireAccess() {
            if (!_activeTask || _activeTask->dynamicResources || std::is_same_v<std::remove_const_t<T>, Scene>) return;
            const auto type = std::type_index(typeid(std::remove_const_t<T>));
            if (std::find(_activeTask->localResources.begin(), _activeTask->localResources.end(), type) != _activeTask->localResources.end()) return;
            for (const auto& access : _activeTask->accesses)
                if (!access.component && access.type == type && (std::is_const_v<T> || access.mode == AccessMode::Write)) return;
            throw std::logic_error("Undeclared resource access in system: " + std::string(type.name()));
        }
        inline static thread_local Scene* _scene = nullptr;
        inline static thread_local EventBus2* _bus = nullptr;
        inline static thread_local const TaskDescription* _activeTask = nullptr;
        template<typename T>
        static void add(T* service)
        {
            auto type_id = reflection::ServiceFamily::getID<std::remove_const_t<T>>();
            CS_DEBUG_ASSERT(_services.find(type_id) == _services.end() && "Attempt to register service but it's already registered.");
            _services[type_id] = const_cast<std::remove_const_t<T>*>(service);
        }

        static void remove(size_t type_id)
        {
            _services.erase(type_id);
        }

        static void clear()
        {
            _services.clear();
        }

        inline static std::unordered_map<size_t, void*> _services{};
    };

    // ============================================================
    // Concepts for lifecycle detection
    // ============================================================
    namespace concepts
    {
        template<typename T>
        concept HasInit = requires(T & t) { t.init(); };

        template<typename T>
        concept HasEarlyInit = requires(T & t) { t.earlyInit(); };

        template<typename T>
        concept HasShutdown = requires(T & t) { t.shutdown(); };
    }

    // ============================================================
    // ServiceWrapper - wraps ANY type, provides lifecycle hooks
    // ============================================================
    namespace details
    {
        class IServiceWrapper
        {
        public:
            virtual ~IServiceWrapper() = default;
            virtual void earlyInit() = 0;
            virtual void init() = 0;
            virtual void shutdown() = 0;
            size_t getTypeID() const noexcept
            {
                return _typeID;
            }
        protected:
            size_t _typeID;
        };

        template<typename T>
        class ServiceWrapper : public IServiceWrapper
        {
        public:
            // Variadic constructor - creates T in-place
            template<typename... Args>
            explicit ServiceWrapper(Args&&... args)
                : instance_(std::in_place, std::forward < Args > (args)...)
            {
                _typeID = reflection::ServiceFamily::getID<T>();
            }
            void earlyInit() override
            {
                if constexpr (concepts::HasEarlyInit<T>)
                {
                    get().earlyInit();
                }
            }
            void init() override
            {
                if constexpr (concepts::HasInit<T>)
                {
                    try { get().init(); }
                    catch (const std::exception& error)
                    { log::exception(std::format("service '{}' init", typeid(T).name()), error); throw; }
                    catch (...) { log::error("Non-standard exception initializing service '{}'", typeid(T).name()); throw; }
                }
            }

            void shutdown() override
            {
                if constexpr (concepts::HasShutdown<T>)
                {
                    get().shutdown();
                }
            }

            T& get()
            {
                return instance_.value();
            }

        private:
            std::optional<T> instance_;
        };

        class ServiceRegistry
        {
        public:
            static void initializeAll()
            {
                for (auto& wrapper : wrappers_)
                {
                    wrapper->init();
                }
            }

            static void shutdownAll()
            {
                // Shutdown in reverse order (LIFO)
                for (auto it = wrappers_.rbegin(); it != wrappers_.rend(); ++it)
                {
                    (*it)->shutdown();
                }
                Services::clear();
                wrappers_.clear();
            }

            // Create and own the service (variadic constructor)
            template<class T, class... Args>
            static T& create(Args&&... args)
            {
                static_assert(!std::is_pointer_v<T>, "Use create<T>(args...) for value types, not pointers");

                auto wrapper = std::make_unique<ServiceWrapper<T>>(std::forward<Args>(args)...);
                T& ref = wrapper->get();

                Services::add(&ref);
                wrapper->earlyInit();
                wrappers_.push_back(std::move(wrapper));
                return ref;
            }

        private:
            inline static std::vector<std::unique_ptr<IServiceWrapper>> wrappers_;
        };
    }
}
