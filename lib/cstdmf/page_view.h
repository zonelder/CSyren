#pragma once

#include "fixed_sparse_set.h"
#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace csyren::cstdmf
{
    // Pages never move their T objects during growth. Erase can move the last
    // object WITHIN the affected page. IDs are reused slots, not generations.
    // Structural mutations invalidate iterators; callers must synchronize access.
    template<typename T, std::size_t PageSize = 64>
    class PageView
    {
        static_assert(PageSize > 0);
    public:
        using ID = std::uint64_t;
        using LocalID = std::uint32_t;
        using Page = FixedSparseSet<T, PageSize, LocalID>;
        static constexpr ID invalidID = static_cast<ID>(Page::invalidID);

        template<bool IsConst>
        class iterator_impl
        {
            friend class PageView;
            template<bool> friend class iterator_impl;
            using ViewPtr = std::conditional_t<IsConst, const PageView*, PageView*>;
            using PageIterator = std::conditional_t<IsConst, typename Page::const_iterator, typename Page::iterator>;
            ViewPtr _view = nullptr;
            std::size_t _page = 0;
            PageIterator _it = nullptr;

            iterator_impl(ViewPtr view, std::size_t page, PageIterator it) noexcept
                : _view(view), _page(page), _it(it) { advance_to_valid(); }
            void advance_to_valid() noexcept
            {
                while (_view && _it == _view->_pages[_page]->end())
                {
                    if (++_page == _view->_pages.size())
                    { _view = nullptr; _page = 0; _it = nullptr; break; }
                    _it = _view->_pages[_page]->begin();
                }
            }

        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = std::conditional_t<IsConst, const T*, T*>;
            using reference = std::conditional_t<IsConst, const T&, T&>;
            iterator_impl() noexcept = default;
            template<bool OtherConst> requires (IsConst && !OtherConst)
            iterator_impl(const iterator_impl<OtherConst>& other) noexcept
                : _view(other._view), _page(other._page), _it(other._it) {}
            reference operator*() const noexcept { return *_it; }
            pointer operator->() const noexcept { return _it; }
            iterator_impl& operator++() noexcept { ++_it; advance_to_valid(); return *this; }
            iterator_impl operator++(int) noexcept { auto old = *this; ++*this; return old; }
            template<bool OtherConst>
            bool operator==(const iterator_impl<OtherConst>& other) const noexcept
            { return _view == other._view && _page == other._page && _it == other._it; }
            template<bool OtherConst>
            bool operator!=(const iterator_impl<OtherConst>& other) const noexcept { return !(*this == other); }
        };

        using iterator = iterator_impl<false>;
        using const_iterator = iterator_impl<true>;
        PageView() noexcept = default;
        PageView(const PageView&) = delete;
        PageView& operator=(const PageView&) = delete;
        PageView(PageView&& other) noexcept { swap(other); }
        PageView& operator=(PageView&& other) noexcept
        {
            if (this != &other) { clear(); swap(other); }
            return *this;
        }
        void swap(PageView& other) noexcept
        {
            _pages.swap(other._pages);
            _nonFullPages.swap(other._nonFullPages);
            std::swap(_size, other._size);
        }

        template<typename... Args>
        ID emplace(Args&&... args)
        {
            if (!_nonFullPages.empty())
            {
                const auto pageIdx = _nonFullPages.front();
                auto& page = *_pages[pageIdx];
                const auto local = page.emplace(std::forward<Args>(args)...);
                if (page.size() == PageSize)
                {
                    std::pop_heap(_nonFullPages.begin(), _nonFullPages.end(), std::greater<LocalID>{});
                    _nonFullPages.pop_back();
                }
                ++_size;
                return encode_id(pageIdx, local);
            }
            // Build before publishing a page: a throwing T leaves no ghost page.
            auto page = std::make_unique<Page>();
            const auto local = page->emplace(std::forward<Args>(args)...);
            const auto pageIdx = append_page(std::move(page));
            ++_size;
            return encode_id(pageIdx, local);
        }

        bool erase(ID id) noexcept(noexcept(std::declval<Page&>().erase(LocalID{})))
            requires requires(Page& p, LocalID local) { p.erase(local); }
        {
            const auto [pageIdx, local] = decode_id(id);
            if (!is_valid(pageIdx)) return false;
            auto& page = *_pages[pageIdx];
            const bool wasFull = page.size() == PageSize;
            if (!page.erase(local)) return false;
            --_size;
            if (wasFull) add_non_full(pageIdx); // Capacity is reserved for every page.
            return true;
        }
        bool contains(ID id) const noexcept { return get(id) != nullptr; }
        T* get(ID id) noexcept
        {
            const auto [page, local] = decode_id(id);
            return is_valid(page) ? _pages[page]->get(local) : nullptr;
        }
        const T* get(ID id) const noexcept
        {
            const auto [page, local] = decode_id(id);
            return is_valid(page) ? std::as_const(*_pages[page]).get(local) : nullptr;
        }
        T& at(ID id)
        {
            if (auto* p = get(id)) return *p;
            throw std::out_of_range("PageView invalid ID");
        }
        const T& at(ID id) const
        {
            if (auto* p = get(id)) return *p;
            throw std::out_of_range("PageView invalid ID");
        }
        T& operator[](ID id) { return at(id); }
        const T& operator[](ID id) const { return at(id); }

        std::size_t size() const noexcept { return _size; }
        bool empty() const noexcept { return _size == 0; }
        std::size_t capacity() const noexcept { return _pages.size() * PageSize; }
        void clear() noexcept { _pages.clear(); _nonFullPages.clear(); _size = 0; }
        void reserve(std::size_t capacity)
        {
            const auto count = capacity / PageSize + (capacity % PageSize != 0);
            if (count <= _pages.size()) return;
            check_page_count(count);
            _pages.reserve(count);
            _nonFullPages.reserve(count);
            // On allocation failure, any pages already reserved remain usable.
            while (_pages.size() < count) append_page(std::make_unique<Page>());
        }

        iterator begin() noexcept
        { return _pages.empty() ? iterator{} : iterator(this, 0, _pages[0]->begin()); }
        iterator end() noexcept { return {}; }
        const_iterator begin() const noexcept
        { return _pages.empty() ? const_iterator{} : const_iterator(this, 0, _pages[0]->begin()); }
        const_iterator end() const noexcept { return {}; }
        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }

        static constexpr ID encode_id(std::uint32_t page, LocalID local) noexcept
        { return (static_cast<ID>(page) << 32) | local; }
        static constexpr std::pair<std::uint32_t, LocalID> decode_id(ID id) noexcept
        { return {static_cast<std::uint32_t>(id >> 32), static_cast<LocalID>(id)}; }

    private:
        bool is_valid(std::uint32_t page) const noexcept { return page < _pages.size(); }
        void check_page_count(std::size_t count) const
        {
            if (count > _pages.max_size() || count > _nonFullPages.max_size() ||
                count > std::numeric_limits<std::size_t>::max() / PageSize ||
                (count && count - 1 > std::numeric_limits<std::uint32_t>::max()))
                throw std::length_error("PageView capacity exceeded");
        }
        void add_non_full(LocalID page) noexcept
        {
            _nonFullPages.push_back(page);
            std::push_heap(_nonFullPages.begin(), _nonFullPages.end(), std::greater<LocalID>{});
        }
        LocalID append_page(std::unique_ptr<Page> page)
        {
            if (_pages.size() == _pages.max_size()) throw std::length_error("PageView capacity exceeded");
            const auto count = _pages.size() + 1;
            check_page_count(count);
            if (_nonFullPages.capacity() < count)
            {
                const auto max = _nonFullPages.max_size();
                const auto old = _nonFullPages.capacity();
                const auto grown = old > max / 2 ? max : old * 2;
                _nonFullPages.reserve(std::max(count, grown));
            }
            const auto idx = static_cast<LocalID>(_pages.size());
            const bool nonFull = page->size() < PageSize;
            _pages.push_back(std::move(page));
            if (nonFull) add_non_full(idx);
            return idx;
        }

        std::vector<std::unique_ptr<Page>> _pages;
        // Min-heap preserves lowest-page reuse without std::set node allocations.
        // capacity >= _pages.size(): erase of a full page cannot allocate.
        std::vector<LocalID> _nonFullPages;
        std::size_t _size = 0;
    };
}
