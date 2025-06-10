#ifndef __CSYREN_INPUT_CONTEXT__
#define __CSYREN_INPUT_CONTEXT__

#include "input_device.h"
#include "input_event.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <format>

namespace csyren::core::input
{

	/**
	 * @brief Represents  a high-level game action trigger by input
	 * 
	 * This class defines action that can be perfomed in game,
	 * abstracting away from specific input device of keys/
	 */
	class InputAction
	{
	public:
		/**
		 * @param name The unique name of the action
		 * @param desc  A human-readable description of the action.
		 */
		InputAction(const std::string& name, const std::string& desc = "") :
			_name(name),
			_description(desc)
		{
		}

		const std::string& name() const noexcept { return _name; }
		const std::string description()const noexcept { return _description; }

		bool operator==(const InputAction& other) const { return _name == other._name; }

		struct Hash
		{
			std::hash<std::string> string_hash;
			size_t operator()(const InputAction& action) const { return string_hash(action._name); };
		};

	private:
		std::string _name;
		std::string _description;
		
	};

	/**
	 * @brief represent a binding between an input event and action.
	 * 
	 * This class defines how raw input events are mapped to high-level actions.
	 */
	class InputBinding
	{

	public:
		/**
		 * @brief Contstructor.
		 * 
		 * @param type The type of input device
		 * @param code The device-specific code(e.g.,key code)
		 * @param shift Whether the Shift modifier is required
		 * @param ctrl Whether the Ctrl modifier is required
		 * @param alt Whether the Alt modifier is required

		 */
		InputBinding(DeviceType type, int code, bool shift = false, bool ctrl = false, bool alt = false) :
			_deviceType(type),
			_code(code),
			_shiftModifier(shift),
			_ctrlModifier(ctrl),
			_altModifier(alt)
		{
		}

		// Equality comparison operator
		bool operator==(const InputBinding& other) const noexcept {
			return _deviceType == other._deviceType &&
				_code == other._code &&
				_shiftModifier == other._shiftModifier &&
				_ctrlModifier == other._ctrlModifier &&
				_altModifier == other._altModifier;
		}

		// Inequality operator for completeness
		bool operator!=(const InputBinding& other) const noexcept {
			return !(*this == other);
		}

		/**
		 * @brief Check if this binding matches an input event
		 *
		 * @param event The input event to check
		 * @return true if the event matches this binding, false otherwise
		 */
		bool matches(const InputEvent& event) const
		{
			if (event.source && event.source->type() != _deviceType)
			{
				return false;
			}
			if (event.code != _code)
			{
				return false;
			}
			
			if (_deviceType == DeviceType::Keyboard)
			{
				return _shiftModifier == event.data.keyboard.shift
					&& _ctrlModifier == event.data.keyboard.ctrl
					&& _altModifier == event.data.keyboard.alt;
			}
			return true;
		}

		std::string toString() const
		{
			std::string result;
			if (_ctrlModifier) result += "Ctrl+";
			if (_shiftModifier) result += "Shift+";
			if (_altModifier) result += "Alt+";

			switch(_deviceType)
			{
			case DeviceType::Keyboard:
				result += std::format("Key({})", _code);
			case DeviceType::Mouse:
				result += std::format("Mouse({})", _code);
				break;
			case DeviceType::Gamepad:
				result+= std::format("Gamepad({})", _code);
			default:
				result += std::format("Unknown({})", _code);
				break;
			}
			return result;
		}

	private:
		DeviceType _deviceType;
		int _code;
		bool _shiftModifier;
		bool _ctrlModifier;
		bool _altModifier;
	};

	/**
	 * @class InputContext
	 * @brief Manages input bindings for a specific game context
	 *
	 * This class defines a set of input bindings that are active in a specific
	 * game context, such as gameplay, menu navigation, or dialog interaction.
	 */
	class InputContext
	{
	public:
		/**
		 * @brief Constructor
		 *
		 * @param name The unique name of the context
		 * @param priority The priority of the context (higher = checked first)
		 */
		InputContext(const std::string& name, int priority = 0)
			:
			_name(name),
			_priority(priority),
			_active(false)
		{}

		/**
		 * @brief Get activity state of context.
		 * @return true if context active, false otherwise
		 */
		bool active() const noexcept { return _active; };
		/**
		 * @brief set activity state of context.
		 * 
		 * @param value The new activity state of context
		 */
		void active(bool value) noexcept { _active = value; }

		int priority() const noexcept { return _priority; }
		void prioprity(int value) noexcept { _priority = value; }

		const std::string& name() const noexcept { return _name;}
		
		/**
		 * @brief Bind an action to an input binding.
		 * 
		 * @param action the action to bind
		 * @param binding The input binding
		 */
		void bind(const InputAction& action, const InputBinding& binding)
		{
			_actionBindings[action].push_back(binding);
		}

		void unbind(const InputAction& action, const InputBinding& binding)
		{
			auto it = _actionBindings.find(action);
			if (it == _actionBindings.end())
				return;

			auto bind_it = std::find(it->second.begin(), it->second.end(), binding);
			if (bind_it == it->second.end())
				return;
			it->second.erase(bind_it);
		}

		bool hasBinding(const InputEvent& event, InputAction& outAction) const
		{
			if (!_active)
				return false;

			for (const auto& pair : _actionBindings)
			{
				for (const auto& binding : pair.second)
				{
					if (binding.matches(event))
					{
						outAction = pair.first;
						return true;
					}
				}
			}

			return false;
		}

	private:
		std::string _name;
		bool _active;
		int _priority;
		std::unordered_map<InputAction, std::vector<InputBinding>, InputAction::Hash> _actionBindings;
	};


	/**
 * @class InputContextManager
 * @brief Manages multiple input contexts
 *
 * This class handles the registration, activation, and prioritization
 * of input contexts, and determines which context should handle an input event.
 */
	class InputContextManager
	{
	public:

		/**
		 * @brief Register a context
		 *
		 * @param context The context to register
		 */
		void registerContext(InputContext* context)
		{
			_contexts.push_back(context);
			std::sort(_contexts.begin(), _contexts.end(), [](const InputContext* a, const InputContext* b) {return a->priority() > b->priority(); });
		}

		/**
		 * @brief Unregister a context
		 *
		 * @param context The context to unregister
		 */
		void unregisterContext(InputContext* context)
		{
			auto it = std::find(_contexts.begin(), _contexts.end(),context);
			if (it == _contexts.end())
				return;
			_contexts.erase(it);
		}

		void active(const std::string& name, bool value)
		{
			for (auto context : _contexts)
			{
				if (context->name() == name)
				{
					context->active(value);
					return;
				}
			}
		}

		bool active(const std::string& name) const
		{
			for (auto context : _contexts)
			{
				if (context->name() == name)
				{
					return context->active();
				}
			}
		}

		InputContext* activeContext(const InputEvent& event, InputAction& outAction)
		{
			for (auto context : _contexts)
			{
				if (context->hasBinding(event, outAction))
				{
					return context;
				}
			}

			return nullptr;
		}

	private:
		std::vector<InputContext*> _contexts;
	};
}

#endif
