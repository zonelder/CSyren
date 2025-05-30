#ifndef __CSYREN_INPUT__
#define __CSYREN_INPUT__
#include <windows.h>
#include <queue>
#include <bitset>

namespace csyren::core
{

	class Input
	{
	public:
		using KeyCode_t = unsigned char;
	private:
		friend class Window;
		class KeyEvent
		{
		public:
			enum class Type { PRESS, RELEASE, INVALID, };
			KeyEvent() noexcept : _type(Type::INVALID), _code(0u) {};
			KeyEvent(Type type, KeyCode_t code)noexcept : _type(type), _code(code) {};

			bool isPressed() const noexcept { return _type == Type::PRESS; }
			bool isReleased() const noexcept { return _type == Type::RELEASE; }
			KeyCode_t code() const noexcept { return _code; }
		private:
			Type _type;
			KeyCode_t _code;
		};
	public:

		Input() = delete;
		Input(const Input&) = delete;
		Input& operator=(const Input&) = delete;

		bool enableAutorepeat() const noexcept { return  _enableAutorepeat; }
		void enableAutorepeat(bool v) noexcept { _enableAutorepeat = true; }

		//key events

		bool isPressed(KeyCode_t key) const noexcept { return _keyStates[key]; }
		bool isReleased(KeyCode_t key) const noexcept { return !_keyStates[key]; }
	private:
		void onKeyPressed(KeyCode_t keycode) noexcept;
		void onKeyReleased(KeyCode_t keycode) noexcept;
		void onChar(KeyCode_t code) noexcept;

		void flush() noexcept;
		void flushKey() noexcept;
		void flushChar() noexcept;
		template<class T>
		static void trimBuffer(std::queue<T>& q);

	private:
		static constexpr unsigned int _nKeys = 256u;
		static constexpr unsigned int _bufferSize = 16u;
		bool _enableAutorepeat = false;
		std::bitset<_nKeys> _keyStates;
		std::queue<KeyEvent> _keyBuffer;
		std::queue<char> _charBuffer;
	};

}



#endif
