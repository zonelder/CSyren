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
	InputEvent event(InputEvent::Type::KeyDown, &mockDevice, 'A');
	EXPECT_EQ(event.type, InputEvent::Type::KeyDown);
	EXPECT_EQ(event.source, &mockDevice);
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

	InputEvent event(InputEvent::Type::KeyDown, &mockDevice, 'A');

	auto handle = dispatcher.dispatch(event);
	EXPECT_TRUE(handle);
	EXPECT_TRUE(callbackCalled);
}
