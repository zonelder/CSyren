#include <iostream>
#include "core/application.h"
#include "core/event_bus.h"

using namespace csyren::core::events;

#define CONSOLE_ENABLE

struct TestEvent {
    int value;
};

struct AnotherEvent {
    float data;
};

struct MarkedEvent {
    std::string info;
};


// Вспомогательный класс для отслеживания вызовов
class EventTracker {
public:
    int testEventCount = 0;
    int markedEventCount = 0;
    int anotherEventCount = 0;
    std::vector<int> receivedValues;

    void handleTestEvent(TestEvent& event) {
        testEventCount++;
        receivedValues.push_back(event.value);
    }

    void handleMarkedEvent(MarkedEvent& event) {
        markedEventCount++;
    }

    void handleAnotherEvent(AnotherEvent& event) {
        anotherEventCount++;
    }
};


int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE pPrevInstance, LPSTR plCmdLine, INT nCmdShow)
{

#ifdef CONSOLE_ENABLE
	FILE* conout = stdout;
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	freopen_s(&conout, "CON", "w", stdout);
#endif
    {
        std::unique_ptr<EventBus2> bus = std::make_unique<EventBus2>();
        EventTracker tracker;

        auto pub_token = bus->register_publisher<TestEvent>();
        auto sub_token = bus->subscribe<TestEvent>([&](auto&) { tracker.testEventCount++; });

        const int BUFFER_SIZE = EventBus2::EventData<TestEvent>::BUFFER_SIZE;

        // Публикация больше событий, чем размер буфера
        TestEvent event;
        for (int i = 0; i < BUFFER_SIZE + 10; i++) {
            bus->publish(pub_token, event);
        }

        bus->commit_batch();

        // Должно обработаться только BUFFER_SIZE событий
       
       // EXPECT_EQ(tracker.testEventCount, BUFFER_SIZE);
    }
	csyren::core::Application app{};
	if (!app.init())
	{
		return -1;
	}
	return app.run();
}
