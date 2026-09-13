#pragma once

#include <vector>
#include <memory>
#include <type_traits>
#include <functional>
#include <optional>
#include <unordered_map>
#include "cstdmf/assert_helpler.h"
#include "family_generator.h"

namespace csyren::core::reflection
{
    class ServiceFamilyID {};
    using ServiceFamily = Family<ServiceFamilyID>;
}

namespace csyren::core
{
    // Forward declaration
    namespace details { class ServiceRegistry; }

    // ============================================================
    // ServiceLocator - non-intrusive access to services
    // ============================================================
    class Services
    {
        friend details::ServiceRegistry;
    public:
        template<typename T>
        static T* get()
        {
            auto it = _services.find(reflection::ServiceFamily::getID<std::remove_const_t<T>>());
            CS_DEBUG_ASSERT(it != _services.end() && "Service not registered.");
            return reinterpret_cast<T*>(it->second);
        }

        template<typename T>
        static bool has()
        {
            return _services.find(reflection::ServiceFamily::getID<std::remove_const_t<T>>()) != _services.end();
        }

    private:
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
                    get().init();
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