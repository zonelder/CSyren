// event_bus.h
#ifndef __CSYREN_EVENT_BUS__
#define __CSYREN_EVENT_BUS__

#include "family_generator.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm> // For std::sort
#include <memory>

namespace csyren::core::reflection
{
	class EventFamilyID {};
	using EventFamily = Family<EventFamilyID>;
}


namespace csyren::core::events
{

	class EventID
	{
	public:
		template<typename T>
		EventID(const T& v) noexcept : 
			_hash(computeCombinedHash(v))
		{}

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

	};
}


#endif
