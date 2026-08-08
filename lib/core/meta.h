#pragma once
#include "component_base.h"
#include "cstdmf/hash_literal.h"
#include "cstdmf/static_vector.h"

#include <unordered_map>

using namespace literal;

namespace csyren::core::reflection
{
	using LiteralID = cstdmf::LiteralID;

	struct MetaType;
	struct MetaData
	{
		LiteralID id{ 0 };
		size_t offset{ 0 };
		size_t size{ 0 };
		MetaType* type{ 0 };
	};

	struct MetaType
	{
		static constexpr size_t MAX_FIELD_COUNT = 32;
		LiteralID id{ 0 };
		size_t size{ 0 };
		cstdmf::StaticVector< MetaData, MAX_FIELD_COUNT> data;
	};

}

namespace csyren::core::reflection::details
{

	template<class Type>
	class MetaLink
	{
	public:
		static inline MetaType* type{ new MetaType };
	};

	class MetaContext
	{
		using MetaMap = std::unordered_map<ComponentFamily::typeID, MetaType*>;
	public:
		static MetaContext& instance()
		{
			static MetaContext m;
			return m;
		}

		MetaType* get(cstdmf::LiteralID id) const {
			auto it = _types.find(id);
			return it != _types.end() ? it->second : nullptr;
		}
		MetaType* get(ComponentFamily::typeID cid) const {
			auto it = _typesByCompID.find(cid);
			return it != _typesByCompID.end() ? it->second : nullptr;
		}


		MetaType* insert(cstdmf::LiteralID id, ComponentFamily::typeID compID, size_t size,MetaType* raw)
		{
			if (auto it = _types.find(id); it != _types.end()) return it->second;
			raw->id = id;
			raw->size = size;
			_types[id] = raw;
			_typesByCompID[compID] = raw;
			//_storage.push_back(raw);
			return raw;
		}
		MetaType* insert(cstdmf::LiteralID id, size_t size, MetaType* raw)
		{
			if (auto it = _types.find(id); it != _types.end()) return it->second;

			raw->id = id;
			raw->size = size;
			_types[id] = raw;
			//_storage.push_back(raw);
			return raw;
		}

		struct MetaView
		{
			using iterator = MetaMap::const_iterator;
			MetaView(iterator begin, iterator end) noexcept : _begin(begin),_end(end){}

			iterator begin() const noexcept
			{
				return _begin;
			}
			iterator end() const noexcept
			{
				return _end;
			}
		private:
			iterator _begin;
			iterator _end;
		};

		MetaView view() const
		{
			return MetaView{ _typesByCompID.begin(),_typesByCompID.end() };
		}

	private:
		std::unordered_map<cstdmf::LiteralID, MetaType*, cstdmf::LiteralIDHash> _types;
		MetaMap _typesByCompID;
	};

	template<typename T, typename M>
	size_t getOffset(M T::* member)
	{
		T* ptr = nullptr;
		M* member_ptr = &(ptr->*member);
		return reinterpret_cast<char*>(member_ptr) - reinterpret_cast<char*>(ptr);
	}

	template<typename> struct MemberType;
	template<typename T, typename M> struct MemberType<M T::*> { using type = M; };
	template<typename T> using MemberType_t = typename MemberType<T>::type;
}


namespace csyren::core::reflection
{

	template<class Type>
	class MetaFactory
	{
	public:
		MetaFactory() : _pType(nullptr) {}

		MetaFactory& type(cstdmf::LiteralID id) {
			_pType = details::MetaContext::instance().insert(id,ComponentFamily::getID<Type>(), sizeof(Type), details::MetaLink<Type>::type);
			return *this;
		}

		MetaFactory& primitiveType(cstdmf::LiteralID id)
		{

			_pType = details::MetaContext::instance().insert(id, sizeof(Type), details::MetaLink<Type>::type);
			return *this;
		}

		template<auto Member>
		MetaFactory& data(cstdmf::LiteralID id) 
		{
			using MemberType = details::MemberType_t<decltype(Member)>;

			MetaData md;
			md.id = id;
			md.offset = details::getOffset(Member);
			md.size = sizeof(MemberType);
			md.type = details::MetaLink<MemberType>::type;

			_pType->data.emplace_back(std::move(md));
			return *this;
		}

	private:
		MetaType* _pType{ nullptr };
	};
	inline MetaType* resolve(cstdmf::LiteralID id)
	{
		return details::MetaContext::instance().get(id);
	}

	inline  MetaType* resolve(ComponentFamily::typeID compID)
	{
		return details::MetaContext::instance().get(compID);
	}

	template<class Type>
	inline MetaType* resolve() noexcept
	{
		return details::MetaLink<std::remove_cvref_t<Type>>::type;
	}
	inline auto resolveAll()
	{
		return details::MetaContext::instance().view();
	}



}

namespace csyren::core::reflection
{
	class MetaRegistry
	{
		static constexpr size_t MAX_ENTRIES = 256;
		static inline void (*s_entries[MAX_ENTRIES])() = {};
		static inline size_t s_count = 0;

	public:
		static void add(void(*fn)())
		{
			if (s_count < MAX_ENTRIES)
				s_entries[s_count++] = fn;
		}

		static void init_all()
		{
			for (size_t i = 0; i < s_count; ++i)
				s_entries[i]();
		}
	};
}

#define REFLECT(Type)                                                       \
    static void _meta_init_##Type() { Type::describe(); }                   \
    static struct _MetaRegistrar_##Type {                                   \
        _MetaRegistrar_##Type() {                                           \
            csyren::core::reflection::MetaRegistry::add(&_meta_init_##Type);\
        }                                                                   \
    } _meta_registrar_##Type##_instance;