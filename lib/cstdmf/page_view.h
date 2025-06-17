#ifndef __CSYREN_PAGE_VIEW__
#define __CSYREN_PAGE_VIEW__

#include "fixed_sparse_set.h"

namespace csyren::cstdmf
{
	template<typename T,size_t PageSize =  64>
	class PageView
	{
	public:
		using ID = size_t;
		using Page = FixedSparseSet<T, PageSize>;
		PageView() noexcept = default;
		~PageView() { clear(); }

		PageView(const PageView&) = delete;
		PageView& operator=(const PageView&) = delete;

	private:
		
		static ID encode_id(size_t page, uint16_t local)
		{
			return static_cast<ID>(page) << 32 | local;
		}

		static std::pair<size_t,uint16_t> decode_id(ID id)
		{
			return { id >> 32,static_cast<uint16_t>(id) };
		
		std::vector<Page*> _pages;
	};
}

#endif
