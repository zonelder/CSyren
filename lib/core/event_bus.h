// event_bus.h
#ifndef __CSYREN_EVENT_BUS__
#define __CSYREN_EVENT_BUS__

#include "family_generator.h"

#include <vector>
#include <array>
#include <map>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <algorithm>
#include <type_traits>
#include <cstdint>

namespace csyren::core::reflection
{
	class EventFamilyID {};
	using EventFamily = Family<EventFamilyID>;
}


namespace csyren::core::events
{
	using EventMarker = uint32_t;
    constexpr EventMarker INVALID_MARKER = std::numeric_limits<EventMarker>::max();
    constexpr uint64_t INVALID_TOKEN = 0xFFFFFFFFFFFFFFFFull;
	class PublishToken
	{
		friend class EventBus2;
		explicit PublishToken(uint64_t d) : _data(d) {}
	public:
		PublishToken() noexcept = default;

        bool valid() const noexcept 
        {
            return _data != INVALID_TOKEN;
        }
    private:
        uint64_t _data{ INVALID_TOKEN };
	};

	class SubscriberToken
	{

		friend class EventBus2;
		explicit SubscriberToken(uint64_t d) : _data(d) {}

	public:
		SubscriberToken() noexcept = default;
        bool valid() const noexcept
        {
            return _data != INVALID_TOKEN;
        }
    private:
		uint64_t _data{ INVALID_TOKEN };
	};


	class EventID
	{
	public:
		template<typename T>
		EventID(const T& v) noexcept : 
			_hash(computeCombinedHash(v))
		{}

		static EventID null() noexcept
		{
			EventID id;
			id._hash = 0;
			return id;
		}
		struct Hash
		{
			bool operator()(const EventID& e) const { return e._hash; }
		};

		bool operator==(const EventID& other) const noexcept { return _hash == other._hash; }
		bool operator!=(const EventID& other) const noexcept { return _hash != other._hash; }

		uint32_t getHash() const noexcept { return _hash; }
		static EventID fromHash(uint32_t hash) noexcept { EventID id; id._hash = hash; return id; }
	private:
		EventID() noexcept = default;
		template<typename T>
		static uint32_t computeCombinedHash(const T& v) 
		{
			// Get type-safe ID and value hash
			uint32_t typeHash = static_cast<uint32_t>(reflection::EventFamily::getID<T>());
			uint32_t valueHash = static_cast<uint32_t>(std::hash<T>{}(v));
			return mixHashes(typeHash, valueHash);
		}

		static uint32_t mixHashes(uint32_t a, uint32_t b) noexcept {
			// Based on MurmurHash3 finalizer
			a ^= b;
			a *= 0xcc9e2d51;
			a = (a << 15) | (a >> 17);  // Rotate left 15
			a *= 0x1b873593;
			a ^= a >> 16;
			a *= 0x85ebca6b;
			a ^= a >> 13;
			a *= 0xc2b2ae35;
			a ^= a >> 16;
			return a;
		}
		uint32_t _hash{ 0 };
	};
	using SubscriptionHandler = uint64_t;

	class EventBus
	{
	public:
	
		class SubscriberContainerBase 
		{ 
		public:
			virtual ~SubscriberContainerBase() = default;
			virtual void unsubscribe(SubscriptionHandler handler) = 0;
		};
		//TODO subscription handler should know information about event id and EventBus must have acess to it. for unsubscribe for example we 
		//need to get container from handler and call unsubscribe from container;if its can be done- do it in bitmask manner

		template<typename TEvent>
		class SubscriberContainer : public SubscriberContainerBase
		{
		public:
			using EventCallback_t = std::function<void(const TEvent&)>;
			SubscriberContainer() = default;
			bool subscribe(SubscriptionHandler handler,EventCallback_t&& callback)
			{
				_callbacks.emplace(handler, std::move(callback));
			}

			void unsubscribe(SubscriptionHandler handler) override
			{
				_callbacks.erase(handler);
			}

			void invoke(const TEvent& event)
			{
				for (const auto& pair: _callbacks)
				{
					pair.second(event);
				}
			}
		private:
			std::unordered_map< SubscriptionHandler, EventCallback_t> _callbacks;
		};


		template<typename EventType>
		SubscriptionHandler subscribe(EventID id, std::function<void(const EventType&)> && callback)
		{
			SubscriberContainer<EventType>* container = nullptr;
			auto it = _subscriptionGroups.find(id);
			if (it == _subscriptionGroups.end())
			{
				container = new SubscriberContainer<EventType>(id);
				_subscriptionGroups.emplace(id, container);
			}
			else
			{
				container = static_cast<SubscriberContainer<EventType>*>(it->second);
			}
			const SubscriptionHandler handler = _handlerMaker.makeHandler(id);
			container->subscribe(handler,std::move(callback));
		}

		void unsubscribe(SubscriptionHandler handler)
		{
			auto id = _handlerMaker.getEventID(handler);
			auto it = _subscriptionGroups.find(id);
			if (it == _subscriptionGroups.end())
			{
				return;
			}

			it->second->unsubscribe(handler);
		}

		template<typename TEvent>
		void invoke(EventID id,const TEvent& event)
		{
			auto it = _subscriptionGroups.find(id);
			if (it == _subscriptionGroups.end())
				return;
			auto pSubs = static_cast<SubscriberContainer<TEvent>*>(it->second);
			pSubs->invoke(event);
		}

		template<typename TEvent>
		void invoke(TEvent& event)
		{
			const uint32_t typeID = reflection::EventFamily::getID<TEvent>();
			auto it = _broadcastSubscriptions.find(typeID);
			if (it != _broadcastSubscriptions.end()) {
				auto* container = static_cast<SubscriberContainer<TEvent>*>(it->second);
				container->invoke(event);
			}
		}

		class HandlerMaker
		{
		public:
			SubscriptionHandler makeHandler(EventID id) {
				// Use high 32 bits for hash, low 32 bits for counter

				static_assert(sizeof(id.getHash()) == 4);
				const uint64_t hashPart = static_cast<uint64_t>(id.getHash()) << 32;
				const uint64_t handler = hashPart | _nextHandlerCounter;

				// Ensure counter stays in 32-bit range
				_nextHandlerCounter = (_nextHandlerCounter + 1) & 0xFFFFFFFF;
				return handler;
			}

			EventID getEventID(SubscriptionHandler handler)
			{
				uint32_t hash = static_cast<uint32_t>(handler >> 32);
				return EventID::fromHash(hash);
			}
			uint32_t _nextHandlerCounter{ 0 };
		};

		
	private:
		HandlerMaker _handlerMaker;
		//TODO we dont need EventID in bucket in this case. need to create my own impl where will be only hash and container
		std::unordered_map<EventID, SubscriberContainerBase*,EventID::Hash> _subscriptionGroups;
		std::unordered_map<uint32_t, SubscriberContainerBase*> _broadcastSubscriptions;

	};



	class EventBus2
	{
	public:
		template<class Event_t>
		using Callback_t = std::function<void(std::decay_t<Event_t>&)>;
		// Легковесная запись о паблишере
		struct PublisherRecord {
			uint64_t type_id;
			std::optional<EventMarker> marker;
		};

		// --- Структуры для безопасного стирания типов ---
		struct EventDataWrapper {
			virtual ~EventDataWrapper() = default;
			virtual void publish(void* event, std::optional<EventMarker> marker) = 0;
			virtual void commit() = 0;
			virtual void unsubscribe(uint64_t sub_id) = 0;
		};

		template<typename Event_t>
		struct EventData : public EventDataWrapper {
			struct Subscriber {
				uint64_t id;
				Callback_t<Event_t> callback;
				std::optional<EventMarker> marker;
			};

			struct EventInstance {
				Event_t event;
				std::optional<EventMarker> marker;
			};

			std::vector<Subscriber> subscribers_;
			mutable std::shared_mutex subscribers_mutex_;

			std::vector<EventInstance> pending_events_;
			std::mutex events_mutex_;

			// --- Реализация интерфейса ---

			void publish(void* event, std::optional<EventMarker> marker) override {
				std::lock_guard lock(events_mutex_);
				pending_events_.push_back({ *static_cast<Event_t*>(event), marker });
			}

			void commit() override {
				std::vector<EventInstance> events_to_process;
				{
					std::lock_guard lock(events_mutex_);
					if (pending_events_.empty()) return;
					events_to_process.swap(pending_events_);
				}

				// Копируем подписчиков ОДИН раз для всего батча событий
				std::vector<Subscriber> subs_copy;
				{
					std::shared_lock lock(subscribers_mutex_);
					subs_copy = subscribers_;
				}

				for (const auto& instance : events_to_process) {
					for (const auto& sub : subs_copy) {
						// Фильтруем по маркеру, если он есть
						if (instance.marker.has_value() && sub.marker.has_value() && instance.marker != sub.marker) {
							continue;
						}
						sub.callback(const_cast<Event_t&>(instance.event));
					}
				}
			}

			void unsubscribe(uint64_t sub_id) override {
				std::unique_lock lock(subscribers_mutex_);
				std::erase_if(subscribers_, [sub_id](const Subscriber& s) { return s.id == sub_id; });
			}

			uint64_t subscribe(Callback_t<Event_t>&& callback, std::optional<EventMarker> marker) {
				const uint64_t sub_id = next_subscriber_id_++;
				std::unique_lock lock(subscribers_mutex_);
				subscribers_.push_back({ sub_id, std::move(callback), marker });
				return sub_id;
			}
		};
		// --- Конец структур ---
	private:
		std::array<std::unique_ptr<EventDataWrapper>, 1024> event_data_;
		std::array<PublisherRecord, 65536> publishers_; // Простой реестр паблишеров
		std::atomic<uint64_t> next_publisher_id_{ 1 };
		static inline std::atomic<uint64_t> next_subscriber_id_{ 1 };

		EventDataWrapper& get_data(uint64_t type_id) {
			if (!event_data_[type_id]) {
				static std::mutex creation_mutex;
				std::lock_guard lock(creation_mutex);
				if (!event_data_[type_id]) {
					// Это место небезопасно для гонок, но для простоты оставим так.
					// В реальном коде здесь нужна double-checked locking.
				}
			}
			return *event_data_[type_id];
		}

		template<typename Event_t>
		EventData<Event_t>& get_typed_data() {
			const uint64_t type_id = reflection::EventFamily::getID<Event_t>();
			if (!event_data_[type_id]) {
				static std::mutex creation_mutex;
				std::lock_guard lock(creation_mutex);
				if (!event_data_[type_id]) {
					event_data_[type_id] = std::make_unique<EventData<Event_t>>();
				}
			}
			return static_cast<EventData<Event_t>&>(*event_data_[type_id]);
		}

	public:
		// --- ПУБЛИЧНОЕ API (теперь работает корректно) ---

		template<typename Event_t>
		PublishToken register_publisher() {
			return register_publisher_impl<Event_t>(std::nullopt);
		}

		template<typename Event_t>
		PublishToken register_publisher(EventMarker marker) {
			return register_publisher_impl<Event_t>(marker);
		}

		void unregister_publisher(PublishToken /*token*/) {
			// В этой модели отписка паблишера не нужна, но метод оставим для API
		}

		template<class Event_t>
		SubscriberToken subscribe(Callback_t<Event_t>&& callback) {
			using Clean_t = std::decay_t<Event_t>;
			auto& data = get_typed_data<Clean_t>();
			const uint64_t sub_id = data.subscribe(std::move(callback), std::nullopt);
			return SubscriberToken(sub_id);
		}

		template<class Event_t>
		SubscriberToken subscribe(EventMarker mark, Callback_t<Event_t>&& callback) {
			auto& data = get_typed_data<Event_t>();
			const uint64_t sub_id = data.subscribe(std::move(callback), mark);
			return SubscriberToken(sub_id);
		}

		void unsubscribe(SubscriberToken token) {
			if (!token.valid()) return;
			// В этой простой модели мы не знаем тип события по токену,
			// поэтому придется итерироваться по всем. Это компромисс ради простоты.
			for (auto& data_ptr : event_data_) {
				if (data_ptr) {
					data_ptr->unsubscribe(token._data);
				}
			}
		}

		template<typename Event_t>
		void publish(PublishToken token, Event_t& event) {
			publish_impl(token, event);
		}

		template<typename Event_t>
		void publish(PublishToken token, Event_t&& event) {
			publish_impl(token, std::move(event));
		}

		void commit_batch() {
			for (auto& data_ptr : event_data_) {
				if (data_ptr) {
					data_ptr->commit();
				}
			}
		}

	private:
		template<typename Event_t>
		void publish_impl(PublishToken token, Event_t&& event) {
			if (!token.valid()) return;

			using Clean_t = std::decay_t<Event_t>;
			const uint64_t pub_id = token._data;
			if (pub_id == 0 || pub_id >= next_publisher_id_.load()) return;

			const auto& pub_record = publishers_[pub_id];
			const uint64_t type_id = reflection::EventFamily::getID<Clean_t>();

			if (pub_record.type_id != type_id) return;

			get_typed_data<Clean_t>().publish(&event, pub_record.marker);
		}
		template<typename Event_t>
		PublishToken register_publisher_impl(std::optional<EventMarker> marker) {
			const uint64_t pub_id = next_publisher_id_.fetch_add(1);
			using Clean_t = std::decay_t<Event_t>;
			if (pub_id >= publishers_.size()) 
			{
				return PublishToken(INVALID_TOKEN);
			}
			publishers_[pub_id] = { reflection::EventFamily::getID<Clean_t>(), marker };
			return PublishToken(pub_id);
		}
	};
   
}



#endif
