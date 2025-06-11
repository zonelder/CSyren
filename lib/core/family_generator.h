#pragma once

namespace csyren::core::reflection
{
	template<class FamilyIdentifierT>
	class Family
	{
	public:
		template<class T>
		static size_t getID()
		{
			static size_t i = next();
			return i;
		}
	private:
		static size_t next()
		{
			static size_t s_nextID = 0;
			return s_nextID++;
		}
	};
}
