#include "pch.h"
#include "core/event_bus.h"
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace csyren::core;

// One test (or a few) per guarantee in the header of core/event_bus.h.
namespace
{
    struct Ping { int value = 0; };
    struct Pong { int value = 0; };
    struct Died
    {
        uint64_t player = 0;
        static constexpr auto key = &Died::player;
    };
    struct ByMethod
    {
        int raw = 0;
        int id() const noexcept { return raw / 10; }
        static constexpr auto key = &ByMethod::id;
    };
    struct Counted
    {
        static inline int alive = 0;
        Counted() { ++alive; }
        Counted(const Counted&) { ++alive; }
        Counted(Counted&&) noexcept { ++alive; }
        ~Counted() { --alive; }
    };
    struct FromThread { int thread = 0; int index = 0; };

    // Allocations made on this thread while `counting` is set (see ReserveMakesPublishingAllocationFree).
    thread_local bool counting = false;
    thread_local int allocations = 0;
}

void* operator new(std::size_t size)
{
    if (counting) ++allocations;
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

TEST(EventBus, SameChannelForTheSameType)
{
    EventBus bus;
    int seen = 0;
    auto subscription = bus.createEvent<Ping>().subscribe([&](const Ping&) { ++seen; });
    bus.createEvent<Ping>().publish({});
    bus.dispatch();
    EXPECT_EQ(seen, 1);
}

TEST(EventBus, SubscriptionReceivesUntilReset)
{
    EventBus bus;
    auto ping = bus.createEvent<Ping>();
    std::vector<int> seen;
    auto subscription = ping.subscribe([&](const Ping& e) { seen.push_back(e.value); });
    EXPECT_TRUE(subscription);
    ping.publish({ 1 });
    bus.dispatch();
    subscription.reset();
    EXPECT_FALSE(subscription);
    ping.publish({ 2 });
    bus.dispatch();
    EXPECT_EQ(seen, std::vector<int>{ 1 });
}

// 1. Publication order.
TEST(EventBus, DeliveryFollowsPublicationOrderAcrossTypes)
{
    EventBus bus;
    auto ping = bus.createEvent<Ping>();
    auto pong = bus.createEvent<Pong>();
    std::string order;
    auto a = ping.subscribe([&](const Ping& e) { order += "i" + std::to_string(e.value); });
    auto b = pong.subscribe([&](const Pong& e) { order += "o" + std::to_string(e.value); });
    ping.publish({ 1 }); pong.publish({ 2 }); ping.publish({ 3 }); pong.publish({ 4 });
    bus.dispatch();
    EXPECT_EQ(order, "i1o2i3o4");
}

TEST(EventBus, ThreadsPublishWithoutLocksInPublicationOrder)
{
    EventBus bus;
    auto channel = bus.createEvent<FromThread>();
    std::vector<FromThread> seen;
    auto subscription = channel.subscribe([&](const FromThread& e) { seen.push_back(e); });
    constexpr int threads = 8, perThread = 2000;
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&, t] { for (int i = 0; i < perThread; ++i) channel.publish({ t, i }); });
    for (auto& worker : workers) worker.join();
    channel.publish({ -1, 0 }); // published after the joins: delivered after everything above
    bus.dispatch();
    ASSERT_EQ(seen.size(), size_t(threads * perThread + 1));
    std::vector<int> next(threads, 0);
    for (size_t i = 0; i + 1 < seen.size(); ++i)
    {
        const auto& e = seen[i];
        ASSERT_GE(e.thread, 0);
        EXPECT_EQ(e.index, next[size_t(e.thread)]++); // each thread's events in its order
    }
    EXPECT_EQ(seen.back().thread, -1);
}

// 2. Nothing is kept without subscribers.
TEST(EventBus, EventsWithoutSubscribersAreNotKept)
{
    EventBus bus;
    Counted::alive = 0;
    auto channel = bus.createEvent<Counted>();
    channel.publish({});
    EXPECT_EQ(Counted::alive, 0);
    auto subscription = channel.subscribe([](const Counted&) {});
    subscription.reset();
    channel.publish({});
    EXPECT_EQ(Counted::alive, 0); // the last subscription is gone
}

// 3. Subscribers of every event, then those of the key.
TEST(EventBus, SubscribersOfEveryEventRunFirstThenKeyed)
{
    EventBus bus;
    auto died = bus.createEvent<Died>();
    std::string order;
    auto keyed = died.subscribe(7, [&](const Died&) { order += "k"; });
    auto first = died.subscribe([&](const Died&) { order += "1"; });
    auto second = died.subscribe([&](const Died&) { order += "2"; });
    died.publish({ 7 });
    bus.dispatch();
    EXPECT_EQ(order, "12k");
}

TEST(EventBus, KeyedSubscriptionsGetOnlyTheirKey)
{
    EventBus bus;
    auto died = bus.createEvent<Died>();
    std::vector<uint64_t> seven, all;
    auto mine = died.subscribe(7, [&](const Died& e) { seven.push_back(e.player); });
    auto every = died.subscribe([&](const Died& e) { all.push_back(e.player); });
    died.publish({ 5 }); died.publish({ 7 }); died.publish({ 9 }); died.publish({ 7 });
    bus.dispatch();
    EXPECT_EQ(seven, (std::vector<uint64_t>{ 7, 7 }));
    EXPECT_EQ(all, (std::vector<uint64_t>{ 5, 7, 9, 7 }));
    static_assert(Broadcast<Died>::keyed && !Broadcast<Ping>::keyed);
}

TEST(EventBus, KeyFromAMemberFunction)
{
    EventBus bus;
    auto channel = bus.createEvent<ByMethod>();
    std::vector<int> seen;
    auto subscription = channel.subscribe(3, [&](const ByMethod& e) { seen.push_back(e.raw); });
    channel.publish({ 31 }); channel.publish({ 42 }); channel.publish({ 35 });
    bus.dispatch();
    EXPECT_EQ(seen, (std::vector<int>{ 31, 35 }));
}

// 4. What a handler may do.
TEST(EventBus, EventsPublishedByHandlersComeLaterInTheSameDispatch)
{
    EventBus bus;
    auto ping = bus.createEvent<Ping>();
    auto pong = bus.createEvent<Pong>();
    std::string order;
    auto a = ping.subscribe([&](const Ping& e) { order += "i" + std::to_string(e.value); if (e.value == 1) pong.publish({ 10 }); });
    auto b = pong.subscribe([&](const Pong& e) { order += "o" + std::to_string(e.value); });
    ping.publish({ 1 }); pong.publish({ 2 });
    bus.dispatch();
    EXPECT_EQ(order, "i1o2o10");
    bus.dispatch();
    EXPECT_EQ(order, "i1o2o10"); // nothing left
}

TEST(EventBus, EndlessChainsStopAfterTheRoundLimit)
{
    EventBus bus;
    auto ping = bus.createEvent<Ping>();
    int seen = 0;
    auto loop = ping.subscribe([&](const Ping& e) { ++seen; ping.publish({ e.value + 1 }); });
    ping.publish({ 0 });
    bus.dispatch();
    EXPECT_EQ(seen, EventBus::kMaxRounds);
    bus.dispatch(); // the rest continues
    EXPECT_EQ(seen, 2 * EventBus::kMaxRounds);
    loop.reset();
    bus.dispatch();
}

TEST(EventBus, SubscriptionsMadeByHandlersStartWithTheNextEvent)
{
    EventBus bus;
    auto died = bus.createEvent<Died>();
    std::vector<uint64_t> all, keyed;
    std::vector<Subscription> made;
    auto first = died.subscribe([&](const Died& e) {
        if (!made.empty()) return;
        made.push_back(died.subscribe([&](const Died& later) { all.push_back(later.player); }));
        made.push_back(died.subscribe(e.player, [&](const Died& later) { keyed.push_back(later.player); }));
    });
    died.publish({ 1 }); died.publish({ 1 }); died.publish({ 2 });
    bus.dispatch();
    EXPECT_EQ(all, (std::vector<uint64_t>{ 1, 2 }));
    EXPECT_EQ(keyed, (std::vector<uint64_t>{ 1 }));
}

TEST(EventBus, ResetDuringDeliveryTakesEffectAtOnce)
{
    EventBus bus;
    auto ping = bus.createEvent<Ping>();
    int second = 0;
    Subscription removed;
    auto first = ping.subscribe([&](const Ping&) { removed.reset(); });
    removed = ping.subscribe([&](const Ping&) { ++second; });
    ping.publish({}); ping.publish({});
    bus.dispatch();
    EXPECT_EQ(second, 0);
}

// The first matching event only: the handler resets its own subscription.
TEST(EventBus, AHandlerMayResetItsOwnSubscription)
{
    EventBus bus;
    auto died = bus.createEvent<Died>();
    int seen = 0;
    Subscription once;
    once = died.subscribe(7, [&](const Died&) { ++seen; once.reset(); });
    died.publish({ 7 }); died.publish({ 7 });
    bus.dispatch();
    EXPECT_EQ(seen, 1);
    EXPECT_FALSE(once);
}

TEST(EventBus, DispatchFromAHandlerDoesNothing)
{
    EventBus bus;
    auto ping = bus.createEvent<Ping>();
    std::vector<int> seen;
    auto subscription = ping.subscribe([&](const Ping& e) { seen.push_back(e.value); bus.dispatch(); });
    ping.publish({ 1 }); ping.publish({ 2 });
    bus.dispatch();
    EXPECT_EQ(seen, (std::vector<int>{ 1, 2 }));
}

// 5. Exceptions.
TEST(EventBus, AThrowingHandlerDoesNotStopTheOthers)
{
    EventBus bus;
    auto ping = bus.createEvent<Ping>();
    int after = 0;
    auto failing = ping.subscribe([](const Ping&) { throw std::runtime_error("handler failed"); });
    auto next = ping.subscribe([&](const Ping&) { ++after; });
    ping.publish({}); ping.publish({});
    EXPECT_NO_THROW(bus.dispatch());
    EXPECT_EQ(after, 2);
}

// 6. Lifetimes.
TEST(EventBus, EventsAreDestroyedAfterDeliveryOrWithTheBus)
{
    Counted::alive = 0;
    {
        EventBus bus;
        auto channel = bus.createEvent<Counted>();
        auto subscription = channel.subscribe([](const Counted&) {});
        channel.publish({}); channel.publish({});
        EXPECT_EQ(Counted::alive, 2);
        bus.dispatch();
        EXPECT_EQ(Counted::alive, 0);
        channel.publish({});
        EXPECT_EQ(Counted::alive, 1); // never dispatched
    }
    EXPECT_EQ(Counted::alive, 0);
}

TEST(EventBus, SubscriptionMayOutliveTheBus)
{
    Subscription subscription;
    {
        EventBus bus;
        subscription = bus.createEvent<Ping>().subscribe([](const Ping&) {});
        EXPECT_TRUE(subscription);
    }
    EXPECT_FALSE(subscription);
    subscription.reset();
}

// Threads: SceneEventsSystem dispatches inside a frame graph task, on whatever worker runs it.
TEST(EventBus, DispatchRunsOnAnyThread)
{
    EventBus bus;
    auto ping = bus.createEvent<Ping>();
    auto pong = bus.createEvent<Pong>();
    std::string order;
    auto a = ping.subscribe([&](const Ping& e) { order += "i" + std::to_string(e.value); pong.publish({ e.value + 1 }); });
    auto b = pong.subscribe([&](const Pong& e) { order += "o" + std::to_string(e.value); });
    ping.publish({ 1 });
    testing::internal::CaptureStdout();
    std::thread worker([&] { bus.dispatch(); });
    worker.join();
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "");
    EXPECT_EQ(order, "i1o2");
}

#ifdef _DEBUG
TEST(EventBus, PublishingOnAnotherThreadDuringDispatchIsReported)
{
    EventBus bus;
    auto ping = bus.createEvent<Ping>();
    int seen = 0;
    auto a = ping.subscribe([&](const Ping& e) {
        ++seen;
        if (e.value == 1) std::thread([&] { ping.publish({ 2 }); }).join();
    });
    ping.publish({ 1 });
    testing::internal::CaptureStdout();
    bus.dispatch();
    EXPECT_NE(testing::internal::GetCapturedStdout().find("another thread while dispatch() runs"), std::string::npos);
    EXPECT_EQ(seen, 2); // still delivered
}
#endif

TEST(EventBus, ReserveMakesPublishingAllocationFree)
{
    EventBus bus;
    auto ping = bus.createEvent<Ping>();
    auto pong = bus.createEvent<Pong>();
    int seen = 0;
    auto a = ping.subscribe([&](const Ping&) { ++seen; });
    auto b = pong.subscribe([&](const Pong&) { ++seen; });
    ping.reserve(100);
    pong.reserve(50);
    counting = true; allocations = 0;
    for (int i = 0; i < 100; ++i) ping.publish({ i });
    for (int i = 0; i < 50; ++i) pong.publish({ i });
    counting = false;
    EXPECT_EQ(allocations, 0);
    bus.dispatch();
    EXPECT_EQ(seen, 150);
}
