#include "pch.h"
#include "core/input_context.h"


using namespace csyren::core;

// Mock input device for testing
class MockInputDevice : public InputDevice {
public:
    void update() override {}
    bool isConnected() const override { return true; }
    std::string name() const override { return "MockDevice"; }
    DeviceType type() const override { return DeviceType::Keyboard; }
};

class InputContextTest : public ::testing::Test {
protected:
    MockInputDevice mockDevice;

    void SetUp() override {
        // Setup code if needed
    }

    void TearDown() override {
        // Teardown code if needed
    }
};

// Test InputAction creation and comparison
TEST_F(InputContextTest, InputAction) {
    InputAction action1("Jump", "Player jumps");
    InputAction action2("Jump", "Player jumps differently");
    InputAction action3("Run", "Player runs");

    EXPECT_EQ(action1.name(), "Jump");
    EXPECT_EQ(action1.description(), "Player jumps");

    EXPECT_TRUE(action1 == action2);
    EXPECT_FALSE(action1 == action3);
}


// Test InputBinding creation and matching
TEST_F(InputContextTest, InputBinding) {
    InputBinding binding(DeviceType::Keyboard,' ', false, false, false); // Space key
    InputEvent matchingEvent(InputEvent::Type::KeyDown, &mockDevice, ' ');
    matchingEvent.data.keyboard.shift = false;
    matchingEvent.data.keyboard.ctrl = false;
    matchingEvent.data.keyboard.alt = false;

    InputEvent nonMatchingEvent(InputEvent::Type::KeyDown, &mockDevice, 'A'); // 'A' key
    nonMatchingEvent.data.keyboard.shift = false;
    nonMatchingEvent.data.keyboard.ctrl = false;
    nonMatchingEvent.data.keyboard.alt = false;

    InputEvent modifierEvent(InputEvent::Type::KeyDown, &mockDevice, ' ');
    modifierEvent.data.keyboard.shift = true;
    modifierEvent.data.keyboard.ctrl = false;
    modifierEvent.data.keyboard.alt = false;

    EXPECT_TRUE(binding.matches(matchingEvent));
    EXPECT_FALSE(binding.matches(nonMatchingEvent));
    EXPECT_FALSE(binding.matches(modifierEvent));
}

// Test InputBinding with modifiers
TEST_F(InputContextTest, InputBindingWithModifiers) {
    InputBinding binding(DeviceType::Keyboard, 'A', true, true, false); // Ctrl+Shift+A

    InputEvent matchingEvent(InputEvent::Type::KeyDown, &mockDevice, 'A');
    matchingEvent.data.keyboard.shift = true;
    matchingEvent.data.keyboard.ctrl = true;
    matchingEvent.data.keyboard.alt = false;

    InputEvent nonMatchingEvent1(InputEvent::Type::KeyDown, &mockDevice, 'A');
    nonMatchingEvent1.data.keyboard.shift = true;
    nonMatchingEvent1.data.keyboard.ctrl = false;
    nonMatchingEvent1.data.keyboard.alt = false;

    InputEvent nonMatchingEvent2(InputEvent::Type::KeyDown, &mockDevice, 'A');
    nonMatchingEvent2.data.keyboard.shift = true;
    nonMatchingEvent2.data.keyboard.ctrl = true;
    nonMatchingEvent2.data.keyboard.alt = true;

    EXPECT_TRUE(binding.matches(matchingEvent));
    EXPECT_FALSE(binding.matches(nonMatchingEvent1));
    EXPECT_FALSE(binding.matches(nonMatchingEvent2));
}

// Test InputContext activation and binding
TEST_F(InputContextTest, InputContextActivation) 
{
    InputContext context("Gameplay", 10);

    EXPECT_FALSE(context.active());
    EXPECT_EQ(context.name(), "Gameplay");
    EXPECT_EQ(context.priority(), 10);

    context.active(true);
    EXPECT_TRUE(context.active());

    context.active(false);
    EXPECT_FALSE(context.active());
}

// Test InputContext binding and matching
TEST_F(InputContextTest, InputContextBinding) 
{
    InputContext context("Gameplay", 10);
    context.active(true);

    InputAction jumpAction("Jump", "Player jumps");
    InputBinding spaceBinding(DeviceType::Keyboard, ' '); // Space key

    context.bind(jumpAction, spaceBinding);

    InputEvent matchingEvent(InputEvent::Type::KeyDown, &mockDevice, ' ');
    InputEvent nonMatchingEvent(InputEvent::Type::KeyDown, &mockDevice, 'A'); // 'A' key

    InputAction outAction("", "");

    EXPECT_TRUE(context.hasBinding(matchingEvent, outAction));
    EXPECT_EQ(outAction.name(), "Jump");

    outAction = InputAction("", "");
    EXPECT_FALSE(context.hasBinding(nonMatchingEvent, outAction));

    // Test inactive context
    context.active(false);
    outAction = InputAction("", "");
    EXPECT_FALSE(context.hasBinding(matchingEvent, outAction));
}


class InputContextManagerTest : public ::testing::Test {
protected:
    InputContextManager manager;
    MockInputDevice mockDevice;
    InputContext gameplayContext{ "Gameplay", 10 };
    InputContext menuContext{ "Menu", 20 };
    InputContext dialogContext{ "Dialog", 30 };

    void SetUp() override {
        // Setup contexts with bindings
        gameplayContext.active(true);
        menuContext.active(true);
        dialogContext.active(true);

        InputAction jumpAction("Jump", "Player jumps");
        InputBinding spaceBinding(DeviceType::Keyboard, ' '); // Space key
        gameplayContext.bind(jumpAction, spaceBinding);

        InputAction selectAction("Select", "Select menu item");
        InputBinding enterBinding(DeviceType::Keyboard, 13); // Enter key
        menuContext.bind(selectAction, enterBinding);

        InputAction confirmAction("Confirm", "Confirm dialog");
        InputBinding yBinding(DeviceType::Keyboard, 'Y'); // 'Y' key
        dialogContext.bind(confirmAction, yBinding);

        // Register contexts with manager
        manager.registerContext(&dialogContext);  // Highest priority
        manager.registerContext(&menuContext);    // Medium priority
        manager.registerContext(&gameplayContext); // Lowest priority
    }
};

// Test context registration and priority ordering
TEST_F(InputContextManagerTest, ContextRegistration) {
    InputAction outAction("", "");

    // Dialog context should handle 'Y' key
    InputEvent yEvent(InputEvent::Type::KeyDown, &mockDevice, 'Y');
    InputContext* context = manager.activeContext(yEvent, outAction);
    EXPECT_EQ(context, &dialogContext);
    EXPECT_EQ(outAction.name(), "Confirm");

    // Menu context should handle Enter key
    InputEvent enterEvent(InputEvent::Type::KeyDown, &mockDevice, 13);
    context = manager.activeContext(enterEvent, outAction);
    EXPECT_EQ(context, &menuContext);
    EXPECT_EQ(outAction.name(), "Select");

    // Gameplay context should handle Space key
    InputEvent spaceEvent(InputEvent::Type::KeyDown, &mockDevice, ' ');
    context = manager.activeContext(spaceEvent, outAction);
    EXPECT_EQ(context, &gameplayContext);
    EXPECT_EQ(outAction.name(), "Jump");
}

// Test context activation and deactivation
TEST_F(InputContextManagerTest, ContextActivation) {
    // Deactivate menu context
    manager.active("Menu",false);

    InputAction outAction("", "");

    // Dialog context should still handle 'Y' key
    InputEvent yEvent(InputEvent::Type::KeyDown, &mockDevice, 89);
    InputContext* context = manager.activeContext(yEvent, outAction);
    EXPECT_EQ(context, &dialogContext);

    // Menu context should no longer handle Enter key
    InputEvent enterEvent(InputEvent::Type::KeyDown, &mockDevice, 13);
    context = manager.activeContext(enterEvent, outAction);
    EXPECT_EQ(context, nullptr);

    // Reactivate menu context
    manager.active("Menu",true);
    context = manager.activeContext(enterEvent, outAction);
    EXPECT_EQ(context, &menuContext);
}


// Test unregistering contexts
TEST_F(InputContextManagerTest, UnregisterContext) 
{
    // Unregister dialog context
    manager.unregisterContext(&dialogContext);

    InputAction outAction("", "");

    // Dialog context should no longer handle 'Y' key
    InputEvent yEvent(InputEvent::Type::KeyDown, &mockDevice, 'Y');
    InputContext* context = manager.activeContext(yEvent, outAction);
    EXPECT_EQ(context, nullptr);

    // Menu context should still handle Enter key
    InputEvent enterEvent(InputEvent::Type::KeyDown, &mockDevice, 13);
    context = manager.activeContext(enterEvent, outAction);
    EXPECT_EQ(context, &menuContext);

    //return to base state
    manager.registerContext(&dialogContext);
}

