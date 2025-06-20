#ifndef __CSYREN_PAGE_VIEW__
#define __CSYREN_PAGE_VIEW__

#include "fixed_sparse_set.h"

#include <memory>
#include <utility>
#include <vector>


namespace csyren::cstdmf
{
	template<typename T,size_t PageSize =  64>
	class PageView
	{
	public:
		using Page = FixedSparseSet<T, PageSize>;
		template<bool isConst>
		class iterator_impl
		{
			friend class PageView;
			using ViewPtr = std::conditional_t<isConst, const PageView*, PageView*>;
			using PageIterator = std::conditional_t < isConst,typename Page::const_iterator,typename Page::iterator>;
			ViewPtr _view{};
			size_t _page;
			PageIterator _it{};
			iterator_impl(ViewPtr view, size_t page, PageIterator it)
				: _view(view),
				_page(page),
				_it(it)
			{
				advance_to_valid();
			}

			void advance_to_valid()
			{
				while (_view && _page < _view->_pages.size() && _it == _view->_pages[_page]->end())
				{
					++_page;
					if (_page >= _view->_pages.size())
					{
						_view = nullptr;
						_page = 0;
						_it = PageIterator{};
						break;
					}
					_it = _view->_pages[_page]->begin();
				}
			}
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = T;
			using difference_type = std::ptrdiff_t;
			using pointer = std::conditional_t<isConst, const T*, T*>;
			using reference = std::conditional_t<isConst, const T&, T&>;
			iterator_impl() = default;
			reference operator*() const { return *_it; }
			pointer operator->() const { return &(*_it); }

			iterator_impl& operator++()
			{
				++_it;
				advance_to_valid();
				return *this;
			}

			iterator_impl& operator++(int)
			{
				iterator_impl tmp = *this;
				++(*this);
				return tmp;
			}

			bool operator==(const iterator_impl& other) const
			{
				if (_view == nullptr && other._view == nullptr)
					return true;
				return _view == other._view && _page == other._page && _it == other._it;
			}
			bool operator!=(const iterator_impl& other) const { return !(*this == other); }

		};

		using ID = size_t;
		using iterator = iterator_impl<false>;
		using const_iterator = iterator_impl<true>;

		PageView() noexcept = default;
		~PageView() { clear(); }

		PageView(const PageView&) = delete;
		PageView& operator=(const PageView&) = delete;

		PageView(PageView&&) noexcept = default;
		PageView& operator=(PageView&&) noexcept = default;

		template<typename... Args>
		ID emplace(Args&&... args)
		{
			for (size_t i = 0; i < _pages.size(); ++i)
			{
				if (_pages[i] && _pages[i]->size() < PageSize)
				{
					auto localID = static_cast<Page::ID>(_pages[i]->size());
					_pages[i]->emplace(localID, std::forward<Args>(args)...);
					return encode_id(static_cast<uint32_t>(i), static_cast<uint32_t>(localID));
				}
			}
			typename Page::ID localID = 0;
			_pages.push_back(std::make_unique<Page>());
			_pages.back()->emplace(localID, std::forward<Args>(args)...);
			return encode_id(static_cast<uint32_t>(_pages.size() - 1), static_cast<uint32_t>(localID));
		}

		bool erase(ID id)
		{
			auto [page, local] = decode_id(id);
			if (!is_valid(page)) return false;
			return _pages[page]->erase(local);
		}

		bool contains(ID id) const noexcept 
		{
			auto [page, local_id] = decode_id(id);
			return is_valid(page) && _pages[page]->contains(local_id);
		}

		void clear()
		{
			_pages.clear();
		}

		T* get(ID id) noexcept
		{
			const auto [page_idx, local_id] = decode_id(id);
			return is_valid(page_idx) ? _pages[page_idx]->get(local_id) : nullptr;
		}

		const T* get(ID id) const noexcept  // Add const overload
		{
			const auto [page_idx, local_id] = decode_id(id);
			return is_valid(page_idx) ? _pages[page_idx]->get(local_id) : nullptr;
		}

		T& at(ID id)
		{
			if (T* ptr = get(id)) return *ptr;
			throw std::out_of_range("PageView::at invalid id");
		}

		const T& at(ID id) const
		{
			if (const T* ptr = get(id)) return *ptr;
			throw std::out_of_range("PageView::at invalid id");
		}

		T& operator[](ID id) { return *get(id);}
		const T& operator[](ID id) const { return *get(id); }

		size_t size() const noexcept
		{
			size_t total = 0;
			for (const auto& p : _pages)
			{
				if (p) total += p->size();
			}
			return total;
		}
		size_t capacity() const noexcept
		{
			size_t existPages = 0;
			for (const auto& p : _pages)
			{
				if (p) ++existPages;
			}
			return existPages * PageSize;
		}
		void reserve(size_t capacity)
		{
			const size_t needed_pages = (capacity + PageSize - 1) / PageSize;
			if (needed_pages < _pages.size())
				return;
			_pages.reserve(needed_pages);
			size_t count = needed_pages - _pages.size();
			for(size_t i = 0; i < count; ++i)
			{
				_pages.push_back(std::make_unique<Page>());
			}
		}

		iterator begin()
		{
			size_t page = 0;
			while (page < _pages.size())
			{
				if (_pages[page]->size() != 0)
				{
					return iterator(this, page, _pages[page]->begin());
				}
				++page;
			}
			return iterator{};
		}

		iterator end() { return iterator{}; }

		const_iterator begin() const
		{
			size_t page = 0;
			while (page < _pages.size())
			{
				if (_pages[page]->size())
					return const_iterator(this, page, _pages[page]->begin());
				++page;
			}
			return const_iterator();
		}

		const_iterator end() const { return const_iterator(); }


		static ID encode_id(uint32_t page, uint32_t local)
		{
			return (static_cast<ID>(page) << 32) | local;
		}

		static std::pair<uint32_t, uint32_t> decode_id(ID id)
		{
			return {static_cast<uint32_t>(id >> 32), static_cast<uint32_t>(id) };
		}
	private:
		
		bool is_valid(uint32_t page_id) const noexcept
		{
			return page_id < _pages.size() && _pages[page_id];
		}

		std::vector<std::unique_ptr<Page>> _pages;
		std::vector<uint32_t> _nonFullPages;
	};
}

#endif
