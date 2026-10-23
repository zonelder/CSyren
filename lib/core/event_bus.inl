// event_bus.inl: internals of event_bus.h (included at its end).
//
// Subscribers: a channel per event type keeps its listeners (shared nodes; a
// Subscription holds a weak reference) in one list for every event and a list per
// key. A listener reset while its channel delivers is only marked and erased after
// the delivery; otherwise it is erased at once.
//
// Published events: each publishing thread has its own queue (found through a
// thread_local cache, created once and pushed into a lock-free list): a record
// {number, buffer, index} per event and a vector per event type for the values.
// dispatch() takes the records of all queues, sorts them by number and delivers
// them; events published by handlers make the next round.
namespace csyren::core
{
    namespace details
    {
        template<class E> class Channel final : public ChannelBase
        {
        public:
            using Handler = std::function<void(const E&)>;
            using Key = typename EventKey<E>::type;
            static constexpr bool keyed = KeyedEvent<E>;

            Channel(EventBus& bus, size_t family) : ChannelBase(bus, typeName<E>(), family) {}

            Subscription subscribe(std::optional<Key> key, Handler handler)
            {
                bus.check("subscribe to", name);
                auto listener = std::make_shared<Listener>();
                listener->channel = this;
                listener->handler = std::move(handler);
                listener->key = std::move(key);
                listFor(listener->key).push_back(listener);
                listeners.fetch_add(1, std::memory_order_relaxed);
                return Subscription(listener);
            }

            void deliver(const E& event)
            {
                // Sizes taken first: a subscription made by a handler starts with the next event.
                std::vector<ListenerPtr>* byKey = nullptr;
                if constexpr (keyed)
                {
                    const auto it = _keyed.find(std::invoke(E::key, event));
                    if (it != _keyed.end()) byKey = &it->second;
                }
                const size_t all = _all.size(), keyedCount = byKey ? byKey->size() : 0;
                _busy = true;
                for (size_t i = 0; i < all; ++i) call(*_all[i], event);
                for (size_t i = 0; i < keyedCount; ++i) call(*(*byKey)[i], event);
                _busy = false;
                if (_dirty) compact();
            }

            void remove(ListenerBase& base) noexcept override
            {
                bus.check("unsubscribe from", name);
                auto& listener = static_cast<Listener&>(base);
                listener.removed = true;
                listeners.fetch_sub(1, std::memory_order_relaxed);
                if (_busy) { _dirty = true; return; }
                if constexpr (keyed)
                {
                    if (listener.key)
                    {
                        const auto it = _keyed.find(*listener.key);
                        if (it == _keyed.end()) return;
                        eraseRemoved(it->second);
                        if (it->second.empty()) _keyed.erase(it);
                        return;
                    }
                }
                eraseRemoved(_all);
            }

        private:
            struct Listener : ListenerBase
            {
                Handler handler;
                std::optional<Key> key;
            };
            using ListenerPtr = std::shared_ptr<Listener>;

            std::vector<ListenerPtr>& listFor(const std::optional<Key>& key)
            {
                if constexpr (keyed) { if (key) return _keyed[*key]; }
                return _all;
            }
            void call(Listener& listener, const E& event)
            {
                if (listener.removed) return;
                try { listener.handler(event); }
                catch (const std::exception& error) { log::error("EventBus: a handler of '{}' failed: {}", name, error.what()); }
                catch (...) { log::error("EventBus: a handler of '{}' failed", name); }
            }
            static void eraseRemoved(std::vector<ListenerPtr>& list) noexcept
            {
                std::erase_if(list, [](const ListenerPtr& listener) { return listener->removed; });
            }
            void compact() noexcept
            {
                // A listener destroyed here may reset another one: that one is only marked.
                _busy = true;
                while (_dirty)
                {
                    _dirty = false;
                    eraseRemoved(_all);
                    if constexpr (keyed)
                        for (auto it = _keyed.begin(); it != _keyed.end();)
                        {
                            eraseRemoved(it->second);
                            it = it->second.empty() ? _keyed.erase(it) : std::next(it);
                        }
                }
                _busy = false;
            }

            std::vector<ListenerPtr> _all;
            // Keyed listeners. Unkeyed events never use it (int: NoKey has no hash).
            std::unordered_map<std::conditional_t<keyed, Key, int>, std::vector<ListenerPtr>> _keyed;
            bool _busy = false;  // delivering or compacting: a reset listener is only marked
            bool _dirty = false; // marked listeners wait for compact()
        };

        // The values of one event type published by one thread.
        struct BufferBase
        {
            virtual ~BufferBase() = default;
            virtual void deliver(size_t index) = 0;
            virtual void clear() noexcept = 0;
        };
        template<class E> struct Buffer final : BufferBase
        {
            explicit Buffer(Channel<E>& channel) : channel(channel) {}
            void deliver(size_t index) override
            {
                // Moved out first: a handler may publish into this buffer.
                const E event = std::move(events[index]);
                channel.deliver(event);
            }
            void clear() noexcept override { events.clear(); }

            Channel<E>& channel;
            std::vector<E> events;
        };

        // Events published by one thread. Only that thread appends; dispatch() reads and clears.
        struct ThreadQueue
        {
            template<class E> Buffer<E>& buffer(Channel<E>& channel)
            {
                if (channel.family >= buffers.size()) buffers.resize(channel.family + 1);
                auto& slot = buffers[channel.family];
                if (!slot) slot = std::make_unique<Buffer<E>>(channel);
                return static_cast<Buffer<E>&>(*slot);
            }
            void clear() noexcept
            {
                records.clear();
                delivered = 0;
                reserved = 0;
                for (auto& buffer : buffers) if (buffer) buffer->clear();
            }

            std::thread::id thread;
            ThreadQueue* next = nullptr;
            std::vector<Record> records;
            size_t delivered = 0;
            size_t reserved = 0; // records promised by reserve()
            std::vector<std::unique_ptr<BufferBase>> buffers; // by EventFamily id
        };
    }

    inline EventBus::EventBus() : _serial([] {
        static std::atomic<uint64_t> serial{ 0 };
        return ++serial;
    }()) {}

    inline EventBus::~EventBus()
    {
        for (auto* queue = _queues.load(std::memory_order_acquire); queue;)
        {
            auto* next = queue->next;
            delete queue;
            queue = next;
        }
    }

    template<class E> Broadcast<E> EventBus::createEvent()
    {
        static_assert(std::is_same_v<E, std::remove_cvref_t<E>>, "createEvent<E>: E is a plain event type");
        static_assert(std::is_move_constructible_v<E> && std::is_nothrow_destructible_v<E>, "createEvent<E>: E must be movable");
        check("createEvent", typeName<E>());
        const auto family = static_cast<size_t>(reflection::EventFamily::getID<E>());
        if (family >= _channels.size()) _channels.resize(family + 1);
        auto& slot = _channels[family];
        if (!slot) slot = std::make_unique<details::Channel<E>>(*this, family);
        return Broadcast<E>(static_cast<details::Channel<E>*>(slot.get()));
    }

    inline void EventBus::dispatch()
    {
        if (_dispatching.exchange(true))
        {
            check("dispatch", "");
            return; // called from a handler: the outer dispatch() continues
        }
        _dispatcher.store(std::this_thread::get_id(), std::memory_order_relaxed);
        struct Done
        {
            EventBus& bus;
            ~Done() { bus._dispatcher.store({}, std::memory_order_relaxed); bus._dispatching.store(false); }
        } done{ *this };

        for (int round = 0; pending(); ++round)
        {
            if (round == kMaxRounds)
            {
                log::warning("EventBus: events published by event handlers went {} rounds deep; the rest is delivered next time", kMaxRounds);
                return;
            }
            _round.clear();
            for (auto* queue = _queues.load(std::memory_order_acquire); queue; queue = queue->next)
            {
                _round.insert(_round.end(), queue->records.begin() + static_cast<std::ptrdiff_t>(queue->delivered), queue->records.end());
                queue->delivered = queue->records.size();
            }
            std::sort(_round.begin(), _round.end(), [](const details::Record& a, const details::Record& b) { return a.number < b.number; });
            for (const auto& record : _round) record.buffer->deliver(record.index);
        }
        for (auto* queue = _queues.load(std::memory_order_acquire); queue; queue = queue->next) queue->clear();
    }

    inline bool EventBus::pending() const noexcept
    {
        for (auto* queue = _queues.load(std::memory_order_acquire); queue; queue = queue->next)
            if (queue->delivered < queue->records.size()) return true;
        return false;
    }

    inline details::ThreadQueue& EventBus::localQueue()
    {
        struct Last { uint64_t bus = 0; details::ThreadQueue* queue = nullptr; };
        static thread_local Last last;
        if (last.bus == _serial) return *last.queue;
        const auto self = std::this_thread::get_id();
        details::ThreadQueue* queue = nullptr;
        for (auto* q = _queues.load(std::memory_order_acquire); q && !queue; q = q->next)
            if (q->thread == self) queue = q;
        if (!queue)
        {
            auto created = std::make_unique<details::ThreadQueue>();
            created->thread = self;
            created->next = _queues.load(std::memory_order_relaxed);
            while (!_queues.compare_exchange_weak(created->next, created.get(), std::memory_order_release, std::memory_order_relaxed)) {}
            queue = created.release();
        }
        last = { _serial, queue };
        return *queue;
    }

    template<class E> void EventBus::record(details::Channel<E>& channel, E&& event)
    {
        check("publish", channel.name);
        auto& queue = localQueue();
        auto& buffer = queue.buffer(channel);
        buffer.events.push_back(std::move(event));
        try { queue.records.push_back({ _published.fetch_add(1, std::memory_order_relaxed), &buffer, buffer.events.size() - 1 }); }
        catch (...) { buffer.events.pop_back(); throw; }
        if (queue.reserved) --queue.reserved;
    }

    template<class E> void EventBus::reserve(details::Channel<E>& channel, size_t count)
    {
        auto& queue = localQueue();
        auto& buffer = queue.buffer(channel);
        buffer.events.reserve(buffer.events.size() + count);
        queue.reserved += count;
        queue.records.reserve(queue.records.size() + queue.reserved);
    }

    inline void EventBus::check([[maybe_unused]] std::string_view what, [[maybe_unused]] std::string_view event) const
    {
#ifdef _DEBUG
        if (_dispatching.load(std::memory_order_relaxed) && _dispatcher.load(std::memory_order_relaxed) != std::this_thread::get_id())
            log::error("EventBus: {} '{}' on another thread while dispatch() runs", what, event);
#endif
    }
}
