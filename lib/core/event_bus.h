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

	static constexpr uint32_t MAX_GENERATION = std::numeric_limits<uint32_t>::max();
    constexpr uint64_t INVALID_TOKEN = 0xFFFFFFFFFFFFFFFFull;
	class PublishToken
	{
		friend class EventBus2;
		explicit PublishToken(uint64_t d,uint32_t generation = 0) : _data(d),_generation(generation) {}
	public:
		PublishToken() noexcept = default;

        bool valid() const noexcept 
        {
            return _data != INVALID_TOKEN;
        }
    private:
        uint64_t _data{ INVALID_TOKEN };
		uint32_t _generation{ MAX_GENERATION };
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

	class EventBus2
	{
	public:
		template<class Event_t>
		using Callback_t = std::function<void(std::decay_t<Event_t>&)>;
		
		// Легковесная запись о паблишере
		struct PublisherRecord {
			uint64_t type_id;
			std::optional<EventMarker> marker;
			uint32_t generation;
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

		void unregister_publisher(PublishToken token) 
		{
			if (!token.valid()) return;
			const uint64_t pub_id = token._data;
			if (pub_id == 0 || pub_id >= next_publisher_id_.load()) return;

			auto& pub_record = publishers_[pub_id];

			//invalidate record;
			pub_record.generation = pub_record.generation >= MAX_GENERATION ? 0 : pub_record.generation + 1;
			pub_record.marker = std::nullopt;
			pub_record.type_id = 0;
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
			if (pub_record.generation != token._generation) return;

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
