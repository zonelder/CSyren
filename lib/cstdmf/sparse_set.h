#ifndef __CSYREN_SPARSE_SET__
#define __CSYREN_SPARSE_SET__

#include <cstdint>
#include <limits>
#include <vector>
#include <memory>
#include <stdexcept>

namespace
{
    static constexpr size_t kPageBits = 12;          // 4 KiB
    static constexpr size_t kPageSize = 1u << kPageBits;
    static constexpr size_t kPageMask = kPageSize - 1;
    static constexpr size_t kMaxPages = (std::numeric_limits<uint32_t>::max() >> kPageBits) + 1;

 
}

namespace csyren::cstdmf
{
    template<typename T,typename EntityID = uint32_t>
    class SparseSet
    {
        static_assert(std::is_integral_v<EntityID>, "EntityID should be integer type.");
        using index_type = EntityID;
        static constexpr EntityID       kInvalidEntity = static_cast<EntityID>(-1);
        static constexpr index_type     kInvalidIndex = static_cast<index_type>(-1);
    public:

        SparseSet() = default;
        ~SparseSet() { clear(); }

        SparseSet(const SparseSet&) = delete;
        SparseSet& operator=(const SparseSet&) = delete;

        SparseSet(SparseSet&& other) noexcept
            : _sparsePages(std::move(other._sparsePages)),
            _dense(std::move(other._dense)),
            _items(std::move(other._items)) {
        }
        SparseSet& operator=(SparseSet&& other) noexcept
        {
            if (this != &other)
            {
                clear();
                _sparsePages = std::move(other._sparsePages);
                _dense = std::move(other._dense);
                _items = std::move(other._items);
            }
            return *this;
        }

        template<typename... Args>
        T* emplace(EntityID entity, Args&&... args)
        {
            index_type& cell = sparseRef(entity);
            if (cell != kInvalidIndex)
                throw std::runtime_error("SparseSet::emplace: entity already present");

            cell = static_cast<index_type>(_dense.size());
            _dense.push_back(entity);
            _items.emplace_back(std::forward<Args>(args)...);
            return &_items.back();
        }

        bool erase(EntityID entity) noexcept
        {
            index_type& cell = sparseRef(entity);
            if (cell == kInvalidIndex)
                return false;

            const index_type idx = cell;
            const index_type last = static_cast<index_type>(_dense.size() - 1);

            if (idx != last)
            {
                const EntityID movedEntity = _dense[last];
                _dense[idx] = movedEntity;
                _items[idx] = std::move(_items[last]);
                sparseRef(movedEntity) = idx;
            }

            _dense.pop_back();
            _items.pop_back();
            cell = kInvalidIndex;
            return true;
        }

        [[nodiscard]] bool contains(EntityID entity) const noexcept {
            const auto cell = sparsePtr(entity);
            return cell && *cell != kInvalidIndex;
        }

        [[nodiscard]] T* try_get(EntityID entity) noexcept
        {
            const index_type* cell = sparsePtr(entity);
            return cell && *cell != kInvalidIndex ? &_items[*cell] : nullptr;
        }
        [[nodiscard]] const T* try_get(EntityID entity) const noexcept
        {
            const index_type* cell = sparsePtr(entity);
            return cell && *cell != kInvalidIndex ? &_items[*cell] : nullptr;
        }

        [[nodiscard]] T& operator[](EntityID entity)
        {
            T* ptr = try_get(entity);
            if (!ptr) throw std::out_of_range("SparseSet::[]");
            return *ptr;
        }
        [[nodiscard]] const T& operator[](EntityID entity) const
        {
            const T* ptr = try_get(entity);
            if (!ptr) throw std::out_of_range("SparseSet::[]");
            return *ptr;
        }

        using iterator = T*;
        using const_iterator = const T*;

        [[nodiscard]] size_t        size() const noexcept { return _items.size(); }
        [[nodiscard]] bool          empty() const noexcept { return _items.empty(); }

        [[nodiscard]] iterator       begin() noexcept { return _items.data(); }
        [[nodiscard]] iterator       end()   noexcept { return _items.data() + _items.size(); }
        [[nodiscard]] const_iterator begin() const noexcept { return _items.data(); }
        [[nodiscard]] const_iterator end()   const noexcept { return _items.data() + _items.size(); }

        void clear() noexcept
        {
            _dense.clear();
            _items.clear();
            for (auto& page : _sparsePages)
                page.reset();
        }

        void reserve(size_t capacity) 
        {
            _dense.reserve(capacity);
            _items.reserve(capacity);
        }

        T* data() noexcept { return _dense.data(); };
        const T* data() const noexcept { return _dense.data(); };
    private:
        [[nodiscard]] uint32_t* sparsePtr(EntityID entity) noexcept
        {
            const size_t page = entity >> kPageBits;
            if (page >= _sparsePages.size() || !_sparsePages[page])
                return nullptr;
            return &_sparsePages[page][entity & kPageMask];
        }
        [[nodiscard]] const uint32_t* sparsePtr(EntityID entity) const noexcept
        {
            const size_t page = entity >> kPageBits;
            if (page >= _sparsePages.size() || !_sparsePages[page])
                return nullptr;
            return &_sparsePages[page][entity & kPageMask];
        }
        index_type& sparseRef(EntityID entity)
        {
            const size_t page = entity >> kPageBits;
            if (page >= _sparsePages.size())
                _sparsePages.resize(page + 1);
            if (!_sparsePages[page])
            {
                _sparsePages[page] = std::make_unique<index_type[]>(kPageSize);
                std::fill_n(_sparsePages[page].get(), kPageSize, kInvalidIndex);
            }
            return _sparsePages[page][entity & kPageMask];
        }

        std::vector<std::unique_ptr<index_type[]>> _sparsePages;
        std::vector<EntityID>                    _dense;
        std::vector<T>                           _items;
    };
}


#endif

