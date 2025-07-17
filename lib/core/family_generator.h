#pragma once
#include <atomic>

namespace csyren::core::reflection
{
	template<class FamilyIdentifierT>
	class Family
	{
	public:
		template<class T>
		static uint64_t getID()
		{
			static uint64_t i = next();
			return i;
		}
	private:
		static uint64_t next()
		{
			static std::atomic<uint64_t> s_nextID = 0;
			return s_nextID++;
		}
	};
}
