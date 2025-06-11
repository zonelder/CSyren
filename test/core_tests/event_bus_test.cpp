#include "pch.h"
#include "core/event_bus.h"
#include <bitset>

using namespace csyren::core::events;

TEST(SubscriptionHandlerTest, HandlerCollisionAfterCounterWrap) 
{

    EventID event1 = EventID::fromHash(0xDEADBEEF);
    EventID event2 = EventID::fromHash(0xDEADBEEF);
    EventBus::HandlerMaker maker;
    maker._nextHandlerCounter = 0xFFFFFFFF;

    SubscriptionHandler h1 = maker.makeHandler(event1);


    maker._nextHandlerCounter = 0xFFFFFFFF;


    SubscriptionHandler h2 = maker.makeHandler(event2);

    EXPECT_EQ(h1, h2);

    EXPECT_EQ(maker.getEventID(h1), maker.getEventID(h2));
}

TEST(EventIDTest, HashCollision) 
{
    EventID a(123), b(456);  // Two different events
    // Force hash collision (mock computeCombinedHash)
    EXPECT_TRUE(a.getHash() != b.getHash());  // Simulate collision
    EXPECT_TRUE(a != b);  // Should still be different events!
}

TEST(SubscriptionHandlerTest, CounterWrap) {
    EventBus::HandlerMaker  manager;
    manager._nextHandlerCounter = 0xFFFFFFFE;

    auto h1 = manager.makeHandler(EventID{ 1 });
    auto h2 = manager.makeHandler(EventID{ 2 });
    auto h3 = manager.makeHandler(EventID{ 3 });

    // All handlers must be unique despite wrap
    ASSERT_NE(h1, h2);
    ASSERT_NE(h1, h3);
    ASSERT_NE(h2, h3);
}

TEST(SubscriptionHandlerTest, HandlerRoundTrip1) {
    EventID original(42);
    EventBus::HandlerMaker manager;
    SubscriptionHandler h = manager.makeHandler(original);
    EventID recovered = manager.getEventID(h);
    ASSERT_EQ(original, recovered);
}


TEST(SubscriptionHandlerTest, CounterBoundaryConditions) {
    EventBus::HandlerMaker manager;
    EventID event(42);

    manager._nextHandlerCounter = (0xFFFFFFFF - 1);

    SubscriptionHandler h1 = manager.makeHandler(event);  // Counter: 0xFFFFFFFE
    SubscriptionHandler h2 = manager.makeHandler(event);  // Counter: 0xFFFFFFFF
    SubscriptionHandler h3 = manager.makeHandler(event);  // Counter: 0x00000000
    SubscriptionHandler h4 = manager.makeHandler(event);  // Counter: 0x00000001

    // Verify all handlers are unique
    ASSERT_NE(h1, h2);
    ASSERT_NE(h1, h3);
    ASSERT_NE(h1, h4);
    ASSERT_NE(h2, h3);
    ASSERT_NE(h2, h4);
    ASSERT_NE(h3, h4);

    // Verify counter wrapped properly
    EXPECT_EQ(manager._nextHandlerCounter, 2);  // Test helper needed
}

TEST(EventIDTest, HashUniqueness) 
{
    // Test different types with same value
    EventID intEvent(42);
    EventID floatEvent(42.0f);

    // Test same type with different values
    EventID str1(std::string("hello"));
    EventID str2(std::string("world"));

    // Test empty values
    EventID emptyStr(std::string(""));
    EventID zeroInt(0);

    // Verify all hashes are unique (might fail in reality!)
    ASSERT_NE(intEvent.getHash(), floatEvent.getHash());
    ASSERT_NE(str1.getHash(), str2.getHash());
    ASSERT_NE(emptyStr.getHash(), zeroInt.getHash());

    // Test equivalent values
    EventID sameInt1(100);
    EventID sameInt2(100);
    ASSERT_EQ(sameInt1, sameInt2);
}

TEST(SubscriptionHandlerTest, HandlerRoundTrip) 
{
    EventBus::HandlerMaker manager;

    // Test with full 32-bit range values
    const uint32_t testHashes[] = { 0, 1, 0xFFFFFFFF, 0xDEADBEEF };

    for (uint32_t hash : testHashes) {
        EventID original = EventID::fromHash(hash);
        SubscriptionHandler handler = manager.makeHandler(original);
        EventID recovered = manager.getEventID(handler);

        ASSERT_EQ(original, recovered);
        ASSERT_EQ(hash, recovered.getHash());
    }
}

TEST(EventIDTest, HashAvalancheEffect) 
{
    // Test small input changes create big hash changes
    EventID base(std::string("apple"));
    EventID changed1(std::string("applf"));
    EventID changed2(std::string("appld"));
    EventID changed3(std::string("bpple"));

    uint32_t baseHash = base.getHash();

    // Hamming distance check
    auto hamming = [](uint32_t a, uint32_t b) {
        return std::bitset<32>(a ^ b).count();
        };

    // Verify significant hash changes
    ASSERT_GT(hamming(baseHash, changed1.getHash()), 10);
    ASSERT_GT(hamming(baseHash, changed2.getHash()), 10);
    ASSERT_GT(hamming(baseHash, changed3.getHash()), 10);
}