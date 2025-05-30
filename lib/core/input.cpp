#include "pch.h"
#include "input.h"

void csyren::core::Input::onKeyPressed(KeyCode_t keycode) noexcept
{
	_keyStates[keycode] = true;
	_keyBuffer.emplace(KeyEvent::Type::PRESS, keycode);
	trimBuffer(_keyBuffer);
}

void csyren::core::Input::onKeyReleased(KeyCode_t keyCode) noexcept
{
	_keyStates[keyCode] = false;
	_keyBuffer.emplace(KeyEvent::Type::RELEASE, keyCode);
	trimBuffer(_keyBuffer);
}


void csyren::core::Input::onChar(KeyCode_t keyCode) noexcept
{
	_charBuffer.emplace(keyCode);
	trimBuffer(_charBuffer);
}

template<class T>
void csyren::core::Input::trimBuffer(std::queue<T>& q)
{
	while (q.size() > _bufferSize)
	{
		q.pop();
	}
}

//buffer block


void csyren::core::Input::flush() noexcept
{
	flushChar();
	flushKey();
}


void csyren::core::Input::flushKey() noexcept
{
	_keyBuffer = {};
}

void csyren::core::Input::flushChar() noexcept
{
	_charBuffer = {};
}


