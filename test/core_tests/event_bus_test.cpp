#include "pch.h"
#include "core/event_bus.h"
#include <bitset>
#include <unordered_set>

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

TEST(EventIDTest, HashAvalancheEffect2) 
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

TEST(EventIDTest, HashAvalancheEffect) {
    // Test small input changes create significant hash changes
    auto testPair = [](auto a, auto b) {
        EventID idA(a);
        EventID idB(b);
        uint32_t hashA = idA.getHash();
        uint32_t hashB = idB.getHash();

        // Calculate bit difference
        uint32_t diffBits = hashA ^ hashB;
        int changedBits = std::bitset<32>(diffBits).count();

        // We expect at least 40% of bits to change (13/32 bits)
        EXPECT_GE(changedBits, 13)
            << "Values: '" << a << "' vs '" << b << "'\n"
            << "HashA: 0x" << std::hex << hashA << "\n"
            << "HashB: 0x" << std::hex << hashB << "\n"
            << "Changed bits: " << std::dec << changedBits;
        };

    // Test various data types
    testPair("apple", "applf");  // Change last character
    testPair("apple", "bpple");  // Change first character
    testPair(12345, 12346);      // Consecutive integers
    testPair(0.1f, 0.1001f);     // Similar floats
    testPair(100u, 101u);        // Consecutive unsigned
    testPair("", " ");            // Empty vs single space
    testPair('a', 'b');           // Consecutive chars
    testPair(true, false);        // Boolean values
}

TEST(EventIDTest, HashDistribution) 
{
    // Verify hashes are well-distributed for different types
    EventID intId(42);
    EventID floatId(42.0f);
    EventID strId(std::string("42"));
    EventID charId('4');
    EventID boolId(true);

    // All should have different hashes
    EXPECT_NE(intId.getHash(), floatId.getHash());
    EXPECT_NE(intId.getHash(), strId.getHash());
    EXPECT_NE(floatId.getHash(), strId.getHash());
    EXPECT_NE(charId.getHash(), boolId.getHash());

    // Same values, different types
    EventID int100(100);
    EventID double100(100.0);
    EXPECT_NE(int100.getHash(), double100.getHash());
}

TEST(SubscriptionHandlerTest, CounterWrapWithCollision) {
    EventBus::HandlerMaker manager;

    // Create collision scenario
    EventID collision1 = EventID::fromHash(0xCAFEBABE);
    EventID collision2 = EventID::fromHash(0xCAFEBABE);  // Same hash

    // Set counter near wrap point
    manager._nextHandlerCounter = (0xFFFFFFFE);

    SubscriptionHandler h1 = manager.makeHandler(collision1);  // Counter: 0xFFFFFFFE
    SubscriptionHandler h2 = manager.makeHandler(collision2);  // Counter: 0xFFFFFFFF
    SubscriptionHandler h3 = manager.makeHandler(collision1);  // Counter: 0x00000000

    // Force same counter value for different events
    manager._nextHandlerCounter=(0xFFFFFFFE);
    SubscriptionHandler h4 = manager.makeHandler(collision2);  // Same counter as h1

    // Verify collision occurs
    ASSERT_EQ(h1, h4) << "Handler collision not detected!";
    ASSERT_EQ(manager.getEventID(h1), manager.getEventID(h4))
        << "Collision causes incorrect event ID mapping";
}

TEST(SubscriptionHandlerTest, HighVolumeUniqueness) {
    EventBus::HandlerMaker manager;
    std::unordered_set<SubscriptionHandler> handlers;
    const int NUM_EVENTS = 1000;
    const int SUBSCRIPTIONS_PER_EVENT = 10000;

    // Create distinct events
    std::vector<EventID> events;
    for (int i = 0; i < NUM_EVENTS; i++) {
        events.push_back(EventID(i));
    }

    // Generate massive number of handlers
    for (int i = 0; i < SUBSCRIPTIONS_PER_EVENT; i++) {
        for (const auto& event : events) {
            SubscriptionHandler h = manager.makeHandler(event);

            // Verify uniqueness
            auto result = handlers.insert(h);
            ASSERT_TRUE(result.second)
                << "Handler collision at event=" << event.getHash()
                << " counter=" << (h & 0xFFFFFFFF);
        }
    }

    // Verify counter advanced correctly
    ASSERT_EQ(manager._nextHandlerCounter,
        static_cast<uint32_t>(NUM_EVENTS * SUBSCRIPTIONS_PER_EVENT));
}

TEST(SubscriptionHandlerTest, ZeroValueHandling) {
    EventBus::HandlerMaker manager;

    // Create event with hash 0
    EventID zeroHashEvent = EventID::fromHash(0);

    // Create handlers at counter 0
    manager._nextHandlerCounter = (0);
    SubscriptionHandler h1 = manager.makeHandler(zeroHashEvent);
    SubscriptionHandler h2 = manager.makeHandler(zeroHashEvent);

    // Verify handlers are distinct
    ASSERT_NE(h1, h2);

    // Verify round trip
    ASSERT_EQ(manager.getEventID(h1), zeroHashEvent);
    ASSERT_EQ(manager.getEventID(h2), zeroHashEvent);

    // Verify handler composition
    ASSERT_EQ(h1 & 0xFFFFFFFF, 0u);
    ASSERT_EQ(h2 & 0xFFFFFFFF, 1u);
    ASSERT_EQ(h1 >> 32, 0u);
}

TEST(EventIDTest, TypeDistinction) {
    // Different types with same value
    EventID intEvent(42);
    EventID floatEvent(42.0f);
    EventID stringEvent(std::string("42"));

    // Same type, different values
    EventID int42(42);
    EventID int43(43);

    // Verify same value same type
    ASSERT_EQ(intEvent, EventID(42));

    // Verify different types don't match
    ASSERT_NE(intEvent, floatEvent);
    ASSERT_NE(intEvent, stringEvent);
    ASSERT_NE(floatEvent, stringEvent);

    // Verify different values don't match
    ASSERT_NE(int42, int43);
}