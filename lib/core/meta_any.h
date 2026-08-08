#pragma once

#include "meta.h"
#include <type_traits>

namespace
{
	using namespace csyren::core::reflection;

	inline const MetaData* findField(const MetaType* type, LiteralID id) noexcept
	{
		if (!type) return nullptr;

		for (const auto& field : type->data)
			if (field.id.id == id.id)
				return &field;

		return nullptr;
	}
}

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
			using CleanType = std::remove_cvref_t<T>;
			CS_ASSERT_MSG(_type && _type->id == resolve<CleanType>()->id,
				"MetaAny::get<T>(): type mismatch");
			return *static_cast<T*>(_instance);
		}

		template<typename T>
		void set(const T& value)
		{
			using CleanType = std::remove_cvref_t<T>;
			CS_ASSERT_MSG(_type && _type->id == resolve<CleanType>()->id,
				"MetaAny::get<T>(): type mismatch");
			*static_cast<T*>(_instance) = value;
		}
		MetaAny field(LiteralID id) const
		{
			if (!_type || !_instance) return MetaAny(nullptr, nullptr);

			auto fieldMeta = findField(_type, id);
			if (!fieldMeta) return MetaAny(nullptr, nullptr);

			void* field_ptr = static_cast<char*>(_instance) + fieldMeta->offset;
			MetaType* field_type = fieldMeta->type;

			return MetaAny(field_ptr, field_type);
		}
		template<class Fn>
		void forEachField(Fn&& fn)
		{
			for (auto& data : _type->data)
			{
				void* field_ptr = static_cast<char*>(_instance) + data.offset;
				fn(data.id, MetaAny(field_ptr, data.type));
			}
		}

		bool hasField(LiteralID id) const
		{
			return _type && findField(_type,id);
		}

	private:
		void* _instance{ nullptr };
		MetaType* _type{ nullptr };
		bool _owns{ false };
	};
}