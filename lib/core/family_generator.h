#pragma once
#include <atomic>

namespace csyren::core::reflection
{
	template<class FamilyIdentifierT>
	class Family
	{
	public:
		using typeID = uint64_t;
		template<class T>
		static typeID getID()
		{
			static typeID i = next();
			return i;
		}
	private:
		static typeID next()
		{
			static std::atomic<typeID> s_nextID = 0;
			return s_nextID++;
		}
	};
}
