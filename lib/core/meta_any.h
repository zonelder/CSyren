#pragma once

#include "meta.h"

namespace csyren::core::reflection
{
	class MetaAny
	{
	public:
		MetaAny(void* instance, MetaType* type)
			: _instance(instance), _type(type), _owns(false)
		{
		}
		explicit MetaAny(MetaType* type)
			: _instance(nullptr), _type(type), _owns(true)
		{
		}

		template<typename T>
		explicit MetaAny(T& value)
			: _instance(&value), _type(resolve<T>()), _owns(false)
		{
		}

		~MetaAny()
		{
		}

		MetaAny(const MetaAny&) = delete;
		MetaAny& operator=(const MetaAny&) = delete;

		// Move semantics
		MetaAny(MetaAny&& other) noexcept
			: _instance(other._instance), _type(other._type), _owns(other._owns)
		{
			other._instance = nullptr;
			other._type = nullptr;
			other._owns = false;
		}

		MetaAny& operator=(MetaAny&& other) noexcept
		{
			if (this != &other)
			{
				_instance = other._instance;
				_type = other._type;
				_owns = other._owns;
				other._instance = nullptr;
				other._type = nullptr;
				other._owns = false;
			}
			return *this;
		}

		// Проверка валидности
		explicit operator bool() const
		{
			return _instance != nullptr && _type != nullptr;
		}

		MetaType* type() const { return _type; }

		void* raw() const { return _instance; }

		template<typename T>
		T& get() const
		{
			CS_ASSERT_MSG(_type && _type->id == resolve<T>()->id,
				"MetaAny::get<T>(): type mismatch\n");
			return *static_cast<T*>(_instance);
		}

		template<typename T>
		void set(const T& value)
		{
			CS_ASSERT_MSG(_type && _type->id == resolve<T>()->id,
				"MetaAny::set<T>(): type mismatch\n");
			*static_cast<T*>(_instance) = value;
		}
		MetaAny field(LiteralID id) const
		{
			if (!_type || !_instance) return MetaAny(nullptr, nullptr);

			auto it = _type->data.find(id);
			if (it == _type->data.end()) return MetaAny(nullptr, nullptr);

			void* field_ptr = static_cast<char*>(_instance) + it->second.offset;
			MetaType* field_type = it->second.type;

			return MetaAny(field_ptr, field_type);
		}
		bool hasField(LiteralID id) const
		{
			return _type && _type->data.find(id) != _type->data.end();
		}

	private:
		void* _instance{ nullptr };
		MetaType* _type{ nullptr };
		bool _owns{ false };
	};
}