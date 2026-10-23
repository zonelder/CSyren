#include "pch.h"
#include "core/input_event.h"

using namespace csyren::core;

class MockInputDevice : public InputDevice
{
public:
	void update() override {};
	bool isConnected() const override { return true; }
	std::string name() const override { return "MockDevice"; }
	DeviceType type() const override { return DeviceType::Keyboard; }
};


class InputEventTest : public ::testing::Test
{
protected:
    InputEventDispatcher dispatcher;
    MockInputDevice mockDevice;

    void SetUp() override {}

    void TearDown() override {}
};


TEST_F(InputEventTest, EventCreation)
{
	InputEvent event(InputEvent::Type::KeyDown, mockDevice.type(), 'A');
	EXPECT_EQ(event.type, InputEvent::Type::KeyDown);
	EXPECT_EQ(event.deviceType, mockDevice.type());
	EXPECT_EQ(event.code, 'A');
	EXPECT_GT(event.timestamp, 0.0);
}

TEST_F(InputEventTest, SubscribeAndDispatch)
{
	bool callbackCalled = false;

	dispatcher.subscribe(InputEvent::Type::KeyDown, [&callbackCalled](const InputEvent& event) {
		callbackCalled = true;
		return true;
	});

	InputEvent event(InputEvent::Type::KeyDown, mockDevice.type(), 'A');

	auto handle = dispatcher.dispatch(event);
	EXPECT_TRUE(handle);
	EXPECT_TRUE(callbackCalled);
}

// ---- window -> InputDispatchSystem queue and device snapshots

#include "core/dispatch_handle.h"
#include "core/mouse_device.h"

namespace
{
	struct QueueProbe : csyren::core::details::DispatchHandle
	{
		size_t size() const { return _eventBuffer.size(); }
		InputEvent pop() { return _eventBuffer.pop(); }
	};

	InputEvent mouseMove(int x, int y)
	{
		InputEvent event(InputEvent::Type::MouseMove, DeviceType::Mouse, 0);
		event.data.mouse.x = x; event.data.mouse.y = y;
		return event;
	}
}

TEST(InputQueue, ConsecutiveMouseMovesAreMergedAndKeysSurvive)
{
	QueueProbe queue;
	queue.dispatch(InputEvent(InputEvent::Type::KeyDown, DeviceType::Keyboard, 'W'));
	for (int i = 0; i < 1000; ++i) queue.dispatch(mouseMove(i, 2 * i));
	queue.dispatch(InputEvent(InputEvent::Type::KeyUp, DeviceType::Keyboard, 'W'));
	queue.dispatch(mouseMove(5, 6));
	ASSERT_EQ(queue.size(), 4u); // key down, one merged move, key up, move
	EXPECT_EQ(queue.pop().type, InputEvent::Type::KeyDown);
	const auto merged = queue.pop();
	EXPECT_EQ(merged.type, InputEvent::Type::MouseMove);
	EXPECT_EQ(merged.data.mouse.x, 999); EXPECT_EQ(merged.data.mouse.y, 1998);
	EXPECT_EQ(queue.pop().type, InputEvent::Type::KeyUp);
	EXPECT_EQ(queue.pop().data.mouse.x, 5);
}

TEST(InputQueue, KeysAreKeptUnderABurstOfOtherEvents)
{
	QueueProbe queue;
	queue.dispatch(InputEvent(InputEvent::Type::KeyDown, DeviceType::Keyboard, 'A'));
	for (int i = 0; i < 200; ++i) // moves interleaved with wheel ticks are not merged
	{
		queue.dispatch(mouseMove(i, i));
		queue.dispatch(InputEvent(InputEvent::Type::MouseWheel, DeviceType::Mouse, 0));
	}
	EXPECT_LE(queue.size(), 256u);
	// A frame's worth of events (well below the capacity) never loses the first key.
	QueueProbe small;
	small.dispatch(InputEvent(InputEvent::Type::KeyDown, DeviceType::Keyboard, 'A'));
	for (int i = 0; i < 100; ++i) { small.dispatch(mouseMove(i, i)); small.dispatch(InputEvent(InputEvent::Type::MouseWheel, DeviceType::Mouse, 0)); }
	EXPECT_EQ(small.pop().type, InputEvent::Type::KeyDown);
}

TEST(MouseDeviceSnapshot, DeltaAndWheelAccumulateOverTheFrame)
{
	MouseDevice mouse;
	mouse.setMousePosition(100, 100); // first position: no jump from (0, 0)
	EXPECT_EQ(mouse.deltaPosition().x, 0); EXPECT_EQ(mouse.deltaPosition().y, 0);
	mouse.update();
	mouse.setMousePosition(110, 95);
	mouse.setMousePosition(130, 90);
	mouse.setScrollDelta(120); mouse.setScrollDelta(120);
	EXPECT_EQ(mouse.deltaPosition().x, 30); EXPECT_EQ(mouse.deltaPosition().y, -10);
	EXPECT_EQ(mouse.scrollDelta(), 240);
	EXPECT_EQ(mouse.x(), 130); EXPECT_EQ(mouse.y(), 90);
	mouse.update(); // next frame
	EXPECT_EQ(mouse.deltaPosition().x, 0); EXPECT_EQ(mouse.scrollDelta(), 0);
}
