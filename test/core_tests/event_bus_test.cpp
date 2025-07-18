#include "pch.h"
#include "core/event_bus.h" // Заголовочный файл с EventBus2
#include <memory>
#include <vector>
#include <atomic>
#include <string>

using namespace csyren::core::events;

struct TestEvent { int value; };
struct AnotherEvent { float data; };
struct MarkedEvent { std::string info; };

class EventBusTest : public ::testing::Test {
protected:
    std::unique_ptr<EventBus2> bus = std::make_unique<EventBus2>();
};

class EventTracker {
public:
    std::atomic<int> testEventCount = 0;
    std::atomic<int> markedEventCount = 0;
    std::atomic<int> anotherEventCount = 0;
    std::vector<int> receivedValues;
    std::mutex vector_mutex;

    void handleTestEvent(TestEvent& event) {
        testEventCount++;
        std::lock_guard lock(vector_mutex);
        receivedValues.push_back(event.value);
    }
    void handleMarkedEvent(MarkedEvent& /*event*/) { markedEventCount++; }
    void handleAnotherEvent(AnotherEvent& /*event*/) { anotherEventCount++; }
};


// =================================================================================
// БАЗОВЫЕ ТЕСТЫ (В основном без изменений)
// =================================================================================

TEST_F(EventBusTest, BasicPublishSubscribe) {
    EventTracker tracker;
    auto pub_token = bus->register_publisher<TestEvent>();
    ASSERT_TRUE(pub_token.valid());
    auto sub_token = bus->subscribe<TestEvent>([&](TestEvent& e) { tracker.handleTestEvent(e); });
    ASSERT_TRUE(sub_token.valid());

    TestEvent event{ 42 };
    bus->publish(pub_token, event);
    bus->commit_batch();

    EXPECT_EQ(tracker.testEventCount, 1);
    ASSERT_EQ(tracker.receivedValues.size(), 1);
    EXPECT_EQ(tracker.receivedValues[0], 42);
}

TEST_F(EventBusTest, MultipleSubscribers) {
    EventTracker tracker1, tracker2;
    auto pub_token = bus->register_publisher<TestEvent>();
    bus->subscribe<TestEvent>([&](TestEvent& e) { tracker1.handleTestEvent(e); });
    bus->subscribe<TestEvent>([&](TestEvent& e) { tracker2.handleTestEvent(e); });

    TestEvent event{ 100 };
    bus->publish(pub_token, event);
    bus->commit_batch();

    EXPECT_EQ(tracker1.testEventCount, 1);
    EXPECT_EQ(tracker2.testEventCount, 1);
}

TEST_F(EventBusTest, Unsubscribe) {
    EventTracker tracker;
    auto pub_token = bus->register_publisher<TestEvent>();
    auto sub_token = bus->subscribe<TestEvent>([&](TestEvent& e) { tracker.handleTestEvent(e); });

    bus->publish(pub_token, TestEvent{ 1 });
    bus->commit_batch();
    EXPECT_EQ(tracker.testEventCount, 1);

    bus->unsubscribe(sub_token);

    bus->publish(pub_token, TestEvent{ 2 });
    bus->commit_batch();
    EXPECT_EQ(tracker.testEventCount, 1);
}

TEST_F(EventBusTest, MultipleEventTypes) {
    EventTracker tracker;
    auto pub_test = bus->register_publisher<TestEvent>();
    auto pub_another = bus->register_publisher<AnotherEvent>();
    bus->subscribe<TestEvent>([&](auto&) { tracker.testEventCount++; });
    bus->subscribe<AnotherEvent>([&](auto&) { tracker.anotherEventCount++; });

    bus->publish(pub_test, TestEvent{});
    bus->publish(pub_another, AnotherEvent{});
    bus->commit_batch();

    EXPECT_EQ(tracker.testEventCount, 1);
    EXPECT_EQ(tracker.anotherEventCount, 1);
}

TEST_F(EventBusTest, UnregisterPublisher)
{
    EventTracker tracker;
    auto pub_token = bus->register_publisher<TestEvent>();
    bus->subscribe<TestEvent>([&](auto&) { tracker.testEventCount++; });

    bus->publish(pub_token, TestEvent{});
    bus->commit_batch();
    EXPECT_EQ(tracker.testEventCount, 1);

    bus->unregister_publisher(pub_token);

    bus->publish(pub_token, TestEvent{}); // Эта публикация не должна пройти
    bus->commit_batch();
    EXPECT_EQ(tracker.testEventCount, 1);
}

// =================================================================================
// ТЕСТЫ НА ОТПИСКУ И СИНХРОНИЗАЦИЮ (Проверяют ключевые исправления)
// =================================================================================

TEST_F(EventBusTest, UnsubscribeDuringCommit) {
    // Этот тест теперь проходит благодаря copy-on-write
    EventTracker tracker;
    SubscriberToken sub_token;
    auto pub_token = bus->register_publisher<TestEvent>();
    sub_token = bus->subscribe<TestEvent>([&](TestEvent&) {
        tracker.testEventCount++;
        bus->unsubscribe(sub_token);
        });

    bus->publish(pub_token, TestEvent{});
    bus->publish(pub_token, TestEvent{});
    bus->commit_batch();

    EXPECT_EQ(tracker.testEventCount, 1);
}

TEST_F(EventBusTest, MassiveUnsubscribeDuringProcessing) {
    // Этот тест теперь проходит благодаря copy-on-write
    constexpr int NUM_SUBSCRIBERS = 1000;
    std::atomic<int> active_count{ 0 };
    std::vector<SubscriberToken> tokens;
    tokens.reserve(NUM_SUBSCRIBERS);
    auto pub_token = bus->register_publisher<TestEvent>();

    for (int i = 0; i < NUM_SUBSCRIBERS; i++) {
        tokens.push_back(bus->subscribe<TestEvent>([this, &active_count, &tokens, i](TestEvent&) {
            active_count++;
            bus->unsubscribe(tokens[i]);
            }));
    }

    bus->publish(pub_token, TestEvent{});
    bus->commit_batch();
    EXPECT_EQ(active_count, NUM_SUBSCRIBERS);

    // Вторая публикация - никто не должен получить
    bus->publish(pub_token, TestEvent{});
    bus->commit_batch();
    EXPECT_EQ(active_count, NUM_SUBSCRIBERS);
}

// =================================================================================
// ТЕСТЫ ЛОГИКИ МАРКЕРОВ (Адаптированы под новую логику)
// =================================================================================

TEST_F(EventBusTest, MarkedEvents) {
    // Проверяет базовую фильтрацию: подписчик с маркером А получает событие с маркером А, но не Б.
    EventTracker tracker;
    const EventMarker MARK_A = 1, MARK_B = 2;
    auto pub_a = bus->register_publisher<MarkedEvent>(MARK_A);
    auto pub_b = bus->register_publisher<MarkedEvent>(MARK_B);
    bus->subscribe<MarkedEvent>(MARK_A, [&](auto& event) { tracker.handleMarkedEvent(event); });

    bus->publish(pub_a, MarkedEvent{});
    bus->publish(pub_b, MarkedEvent{});
    bus->commit_batch();

    EXPECT_EQ(tracker.markedEventCount, 1);
}

TEST_F(EventBusTest, UniversalSubscriberReceivesAll) {
    // ИЗМЕНЕНО: Этот тест заменяет старый UniversalPublisher.
    // Он проверяет, что универсальный подписчик получает ВСЕ события своего типа.
    EventTracker tracker;
    const EventMarker MARK_A = 123;
    auto universal_pub = bus->register_publisher<TestEvent>();
    auto marked_pub = bus->register_publisher<TestEvent>(MARK_A);

    // Универсальный подписчик
    bus->subscribe<TestEvent>([&](auto&) { tracker.testEventCount++; });

    // 1. Публикация от универсального издателя
    bus->publish(universal_pub, TestEvent{});
    bus->commit_batch();
    EXPECT_EQ(tracker.testEventCount, 1); // Получил

    // 2. Публикация от маркированного издателя
    bus->publish(marked_pub, TestEvent{});
    bus->commit_batch();
    EXPECT_EQ(tracker.testEventCount, 2); // Тоже получил
}


// =================================================================================
// ПРОЧИЕ И СТРЕСС-ТЕСТЫ (В основном без изменений)
// =================================================================================

TEST_F(EventBusTest, HighLoadStressTest) {
    constexpr int NUM_EVENTS = 5000;
    constexpr int NUM_SUBSCRIBERS = 100;
    std::atomic<int> event_counter{ 0 };
    auto pub_token = bus->register_publisher<TestEvent>();

    for (int i = 0; i < NUM_SUBSCRIBERS; i++) {
        bus->subscribe<TestEvent>([&](TestEvent&) { event_counter++; });
    }

    TestEvent event;
    for (int i = 0; i < NUM_EVENTS; i++) {
        bus->publish(pub_token, event);
    }
    bus->commit_batch();

    EXPECT_EQ(event_counter.load(), NUM_EVENTS * NUM_SUBSCRIBERS);
}

TEST_F(EventBusTest, EventOrderPreservation) {
    std::vector<int> event_order;
    constexpr int NUM_EVENTS = 100;
    auto pub_token = bus->register_publisher<TestEvent>();
    bus->subscribe<TestEvent>([&](TestEvent& e) {
        event_order.push_back(e.value);
        });

    for (int i = 0; i < NUM_EVENTS; i++) {
        bus->publish(pub_token, TestEvent{ i });
    }
    bus->commit_batch();

    ASSERT_EQ(event_order.size(), NUM_EVENTS);
    for (int i = 0; i < NUM_EVENTS; i++) {
        EXPECT_EQ(event_order[i], i);
    }
}

TEST_F(EventBusTest, RapidSubscribeUnsubscribe) {
    constexpr int NUM_ITERATIONS = 1000;
    std::atomic<int> event_counter{ 0 };
    auto pub_token = bus->register_publisher<TestEvent>();

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        auto sub_token = bus->subscribe<TestEvent>([&](TestEvent&) { event_counter++; });
        bus->publish(pub_token, TestEvent{});
        bus->commit_batch();
        bus->unsubscribe(sub_token);
    }

    EXPECT_EQ(event_counter, NUM_ITERATIONS);
}

// =================================================================================
// НОВЫЕ ТЕСТЫ
// =================================================================================

TEST_F(EventBusTest, NOP_UnsubscribeInvalidToken) {
    // НОВЫЙ ТЕСТ: Проверяет, что отписка по невалидному токену ничего не ломает.
    EventTracker tracker;
    auto pub_token = bus->register_publisher<TestEvent>();
    auto sub_token = bus->subscribe<TestEvent>([&](auto&) { tracker.testEventCount++; });

    bus->unsubscribe(SubscriberToken{}); // Невалидный токен
    bus->unsubscribe(SubscriberToken{});      // Несуществующий токен

    bus->publish(pub_token, TestEvent{});
    bus->commit_batch();

    EXPECT_EQ(tracker.testEventCount, 1); // Подписка должна остаться активной
}

TEST_F(EventBusTest, NOP_PublishWithInvalidToken) {
    // НОВЫЙ ТЕСТ: Проверяет, что публикация с невалидным токеном ничего не делает.
    EventTracker tracker;
    bus->subscribe<TestEvent>([&](auto&) { tracker.testEventCount++; });

    bus->publish(PublishToken{}, TestEvent{});
    bus->commit_batch();

    EXPECT_EQ(tracker.testEventCount, 0);
}

TEST_F(EventBusTest, NoSubscribers) {
    // НОВЫЙ ТЕСТ: Проверяет, что система не падает, если нет подписчиков.
    auto pub_token = bus->register_publisher<TestEvent>();

    // Ожидаем, что эти вызовы просто ничего не сделают и не вызовут крэш
    ASSERT_NO_THROW({
        bus->publish(pub_token, TestEvent{});
        bus->commit_batch();
        });
}