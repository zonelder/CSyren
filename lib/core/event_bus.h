// event_bus.h
#ifndef __CSYREN_EVENT_BUS__
#define __CSYREN_EVENT_BUS__
// Event bus: deferred notifications about what has already happened in a scene.
// Events are published anywhere during the frame and delivered at one point,
// dispatch() (SceneEventsSystem; between frames the scene's settle), on one
// thread, in publication order. Not for requests that need an answer, immediate
// calls or data that changes every frame (components and resources are for that).
//
//   struct PlayerDied { Entity::ID player; static constexpr auto key = &PlayerDied::player; };
//
//   auto died = bus.createEvent<PlayerDied>();   // every caller gets the same channel
//   died.publish({ id });                         // any thread, no locks
//   auto all  = died.subscribe([](const PlayerDied& e) { ... });      // every event
//   auto mine = died.subscribe(id, [](const PlayerDied& e) { ... });  // events with this key
//
// API: EventBus::createEvent<E>() and dispatch(); Broadcast<E>::publish,
// subscribe, subscribe(key) and reserve; Subscription (unsubscribes when reset or
// destroyed).
//
// Guarantees:
// 1. Events are delivered in publication order, whatever their type and thread.
// 2. An event published to a channel without subscribers is not kept.
// 3. Per event, subscribers of every event run first, then those of its key, each
//    in subscription order. A key is a data member or a const member function
//    (`static constexpr auto key = &E::member`); finding its subscribers is one
//    hash lookup.
// 4. A handler may publish (delivered later in the same dispatch(), at most
//    kMaxRounds chains deep; the rest waits for the next dispatch()), subscribe
//    (from the next event on) and reset subscriptions, its own included (takes
//    effect at once). dispatch() called from a handler does nothing.
// 5. A handler that throws is logged; the other handlers still run.
// 6. A Subscription may outlive the bus; a Broadcast may not.
//
// Threads: publish() runs on any thread, but not while dispatch() runs on another
// one. createEvent, subscribe, Subscription::reset and dispatch() run one at a
// time, on any thread: in the engine in onAttach/onDetach, structural commits and
// SceneEventsSystem. Debug builds log a call made on another thread while
// dispatch() runs.
#include "family_generator.h"
#include "type_key.h"
#include "cstdmf/log.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace csyren::core::reflection
{
	class EventFamilyID {};
	using EventFamily = Family<EventFamilyID>;
}

namespace csyren::core
{
    class EventBus;

    // Internals (defined in event_bus.inl).
    namespace details
    {
        template<class E> concept KeyedEvent = requires(const E& event) { std::invoke(E::key, event); };
        struct NoKey { bool operator==(const NoKey&) const = default; };
        template<class E> struct EventKey { using type = NoKey; };
        template<KeyedEvent E> struct EventKey<E>
        {
            using type = std::remove_cvref_t<std::invoke_result_t<decltype(E::key), const E&>>;
        };

        class ChannelBase;
        struct ListenerBase { ChannelBase* channel = nullptr; bool removed = false; };
        class ChannelBase
        {
        public:
            ChannelBase(EventBus& bus, std::string_view name, size_t family) : bus(bus), name(name), family(family) {}
            virtual ~ChannelBase() = default;
            virtual void remove(ListenerBase& listener) noexcept = 0;
            bool listened() const noexcept { return listeners.load(std::memory_order_relaxed) != 0; }

            EventBus& bus;
            const std::string_view name;
            const size_t family;
            std::atomic<uint32_t> listeners{ 0 }; // read by publishers: no subscribers, nothing kept
        };
        template<class E> class Channel;
        struct BufferBase;
        struct ThreadQueue;
        struct Record { uint64_t number; BufferBase* buffer; size_t index; }; // a published event
    }

    // Removes its subscription when reset or destroyed. Move-only.
    class Subscription
    {
    public:
        Subscription() = default;
        Subscription(Subscription&&) noexcept = default;
        Subscription& operator=(Subscription&& other) noexcept
        {
            if (this != &other) { reset(); _listener = std::move(other._listener); }
            return *this;
        }
        ~Subscription() { reset(); }

        void reset() noexcept
        {
            if (auto listener = _listener.lock()) listener->channel->remove(*listener);
            _listener.reset();
        }
        explicit operator bool() const noexcept { return !_listener.expired(); }

    private:
        template<class> friend class details::Channel;
        explicit Subscription(std::weak_ptr<details::ListenerBase> listener) noexcept : _listener(std::move(listener)) {}
        std::weak_ptr<details::ListenerBase> _listener;
    };

    // Handle of the channel of E on one bus (EventBus::createEvent). Cheap to copy.
    template<class E> class Broadcast
    {
        using Channel = details::Channel<E>;
    public:
        using Handler = std::function<void(const E&)>;
        using Key = typename details::EventKey<E>::type;
        static constexpr bool keyed = details::KeyedEvent<E>;

        Broadcast() = default;
        explicit operator bool() const noexcept { return _channel != nullptr; }

        // Any thread. Not kept when nobody is subscribed.
        void publish(E event) const
        {
            if (_channel && _channel->listened()) _channel->bus.record(*_channel, std::move(event));
        }
        [[nodiscard]] Subscription subscribe(Handler handler) const { return _channel->subscribe(std::nullopt, std::move(handler)); }
        [[nodiscard]] Subscription subscribe(const Key& key, Handler handler) const requires keyed
        {
            return _channel->subscribe(key, std::move(handler));
        }
        // The next `count` publications on this thread allocate nothing (a commit
        // that must not throw reserves before it starts).
        void reserve(size_t count) const
        {
            if (_channel && _channel->listened()) _channel->bus.reserve(*_channel, count);
        }

    private:
        friend class EventBus;
        explicit Broadcast(Channel* channel) noexcept : _channel(channel) {}
        Channel* _channel = nullptr;
    };

    class EventBus
    {
    public:
        // Handler chains deeper than this many rounds continue in the next dispatch().
        static constexpr int kMaxRounds = 16;

        EventBus();
        EventBus(const EventBus&) = delete;
        EventBus& operator=(const EventBus&) = delete;
        ~EventBus();

        // The channel of E on this bus, created on the first call.
        template<class E> Broadcast<E> createEvent();
        // Delivers the events published so far, in publication order, on the calling thread.
        void dispatch();

    private:
        template<class> friend class Broadcast;
        template<class> friend class details::Channel;
        details::ThreadQueue& localQueue();
        template<class E> void record(details::Channel<E>& channel, E&& event);
        template<class E> void reserve(details::Channel<E>& channel, size_t count);
        bool pending() const noexcept;
        void check(std::string_view what, std::string_view event) const;

        std::vector<std::unique_ptr<details::ChannelBase>> _channels; // by EventFamily id
        std::atomic<details::ThreadQueue*> _queues{ nullptr };         // one per publishing thread
        std::atomic<uint64_t> _published{ 0 };                         // numbers events: the delivery order
        std::atomic<bool> _dispatching{ false };
        std::atomic<std::thread::id> _dispatcher{};
        std::vector<details::Record> _round;                           // dispatch() scratch
        const uint64_t _serial;                                        // tells buses apart in thread_local caches
    };
}

#include "event_bus.inl"

#endif
