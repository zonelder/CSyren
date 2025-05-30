#include "pch.h"
#include "core/input_buffer.h"


using namespace csyren::core;

class InputBufferTest : public ::testing::Test
{
protected:
	void SetUp() override {};
	void TearDown() override {};

};



TEST_F(InputBufferTest, DefaultConstruction)
{
	InputBuffer<int> buffer;
	EXPECT_TRUE(buffer.empty());
	EXPECT_EQ(buffer.size(), 0);
}


// Test pushing and popping items
TEST_F(InputBufferTest, PushAndPop) {
    InputBuffer<int> buffer;

    buffer.push(1);
    buffer.push(2);
    buffer.push(3);

    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(buffer.size(), 3);

    EXPECT_EQ(buffer.pop(), 1);
    EXPECT_EQ(buffer.pop(), 2);
    EXPECT_EQ(buffer.pop(), 3);

    EXPECT_TRUE(buffer.empty());
}

// Test buffer overflow with DiscardOldest policy
TEST_F(InputBufferTest, OverflowDiscardOldest) {
    InputBuffer<int> buffer(3, BufferOverflowPolicy::DiscardOldest);

    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    buffer.push(4); // This should discard 1

    EXPECT_EQ(buffer.size(), 3);
    EXPECT_EQ(buffer.pop(), 2);
    EXPECT_EQ(buffer.pop(), 3);
    EXPECT_EQ(buffer.pop(), 4);
}

// Test buffer overflow with DiscardNewest policy
TEST_F(InputBufferTest, OverflowDiscardNewest) {
    InputBuffer<int> buffer(3, BufferOverflowPolicy::DiscardNewest);

    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    buffer.push(4); // This should be discarded

    EXPECT_EQ(buffer.size(), 3);
    EXPECT_EQ(buffer.pop(), 1);
    EXPECT_EQ(buffer.pop(), 2);
    EXPECT_EQ(buffer.pop(), 3);
}

// Test buffer overflow with Resize policy
TEST_F(InputBufferTest, OverflowResize) {
    InputBuffer<int> buffer(3, BufferOverflowPolicy::Resize);

    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    buffer.push(4); // This should cause a resize

    EXPECT_EQ(buffer.size(), 4);
    EXPECT_EQ(buffer.pop(), 1);
    EXPECT_EQ(buffer.pop(), 2);
    EXPECT_EQ(buffer.pop(), 3);
    EXPECT_EQ(buffer.pop(), 4);
}

// Test buffer overflow with ThrowException policy
TEST_F(InputBufferTest, OverflowThrowException) {
    InputBuffer<int> buffer(3, BufferOverflowPolicy::ThrowException);

    buffer.push(1);
    buffer.push(2);
    buffer.push(3);

    EXPECT_THROW(buffer.push(4), std::overflow_error);
}

// Test clear method
TEST_F(InputBufferTest, Clear) {
    InputBuffer<int> buffer;

    buffer.push(1);
    buffer.push(2);
    buffer.push(3);

    buffer.clear();

    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0);
}

// Test setMaxSize method
TEST_F(InputBufferTest, SetMaxSize) {
    InputBuffer<int> buffer(5);

    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    buffer.push(4);
    buffer.push(5);

    buffer.maxSize(3); // This should discard 1 and 2

    EXPECT_EQ(buffer.size(), 3);
    EXPECT_EQ(buffer.pop(), 3);
    EXPECT_EQ(buffer.pop(), 4);
    EXPECT_EQ(buffer.pop(), 5);
}

// Test underflow exception
TEST_F(InputBufferTest, Underflow) {
    InputBuffer<int> buffer;

    EXPECT_THROW(buffer.pop(), std::underflow_error);
}

// Test setOverflowPolicy method
TEST_F(InputBufferTest, SetOverflowPolicy) {
    InputBuffer<int> buffer(3, BufferOverflowPolicy::DiscardOldest);

    buffer.push(1);
    buffer.push(2);
    buffer.push(3);

    buffer.overflowPolicy(BufferOverflowPolicy::ThrowException);

    EXPECT_THROW(buffer.push(4), std::overflow_error);
}

