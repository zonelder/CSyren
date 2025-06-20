#include "pch.h"
#include "cstdmf/page_view.h"
#include <algorithm>
#include <random>
#include <vector>

using namespace csyren::cstdmf;


TEST(PageViewTest, IDCoding)
{
    using PageClass = PageView<int>;
    constexpr size_t N = 100000;
    constexpr size_t PageN = 100;
    for (size_t i = 0; i < N; ++i)
    {
        for (size_t j = 0; j < PageN; ++j)
        {
            auto id = PageClass::encode_id(j, i);
            auto [decode_j, decode_i] = PageClass::decode_id(id);
            EXPECT_EQ(decode_j, j);
            EXPECT_EQ(decode_i, i);
        }
    }
}

TEST(PageViewTest, BasicOperations) {
    PageView<int> pv;

    auto id1 = pv.emplace(1);
    auto id2 = pv.emplace(2);
    auto id3 = pv.emplace(3);

    EXPECT_EQ(pv.size(), 3);
    EXPECT_TRUE(pv.contains(id1));
    EXPECT_TRUE(pv.contains(id2));
    EXPECT_TRUE(pv.contains(id3));

    EXPECT_EQ(*pv.get(id1), 1);
    EXPECT_EQ(pv.at(id2), 2);
    EXPECT_EQ(pv[id3], 3);


    EXPECT_TRUE(pv.erase(id2));
    EXPECT_EQ(pv.size(), 2);
    EXPECT_FALSE(pv.contains(id2));
    EXPECT_EQ(pv.get(id2), nullptr);
}


TEST(PageViewTest, Clear) {
    PageView<std::string> pv;

    auto id1 = pv.emplace("A");
    auto id2 = pv.emplace("B");
    pv.reserve(100);

    EXPECT_EQ(pv.size(), 2);
    EXPECT_GE(pv.capacity(), 100);

    pv.clear();

    EXPECT_EQ(pv.size(), 0);
    EXPECT_EQ(pv.capacity(), 0);
    EXPECT_FALSE(pv.contains(id1));
}

TEST(PageViewTest, Iterators) {
    PageView<int> pv;
    std::vector<int> values;

    for (int i = 0; i < 100; ++i) {
        pv.emplace(i);
        values.push_back(i);
    }

    std::vector<int> result;
    for (auto& item : pv) {
        result.push_back(item);
    }

    EXPECT_EQ(result, values);

    PageView<int> empty;
    EXPECT_EQ(empty.begin(), empty.end());
}

TEST(PageViewMoveTest, MoveSemantics) 
{
    PageView<std::unique_ptr<int>, 2> pv;

    auto id1 = pv.emplace(std::make_unique<int>(42));
    auto id2 = pv.emplace(std::make_unique<int>(100));

    EXPECT_TRUE(pv.erase(id1));

    auto id3 = pv.emplace(std::make_unique<int>(200));

    EXPECT_EQ(**pv.get(id2), 100);
    EXPECT_EQ(**pv.get(id3), 200);

    auto [page2, slot2] = pv.decode_id(id2);
    auto [page3, slot3] = pv.decode_id(id3);

    EXPECT_EQ(page2, 0);
    EXPECT_EQ(page3, 0);

    // Проверка, что элементы разные (даже если в одном слоте)
    EXPECT_NE(pv.get(id2), pv.get(id3));

    // Дополнительная проверка через итераторы
    std::vector<int> values;
    for (auto& ptr : pv) {
        values.push_back(*ptr);
    }
    std::sort(values.begin(), values.end());
    ASSERT_EQ(values.size(), 2);
    EXPECT_EQ(values[0], 100);
    EXPECT_EQ(values[1], 200);
}
// Тест на перемещение контейнера с move-only типами
TEST(PageViewMoveTest, ContainerMove) {
    PageView<std::unique_ptr<int>> pv1;

    auto id1 = pv1.emplace(std::make_unique<int>(42));
    auto id2 = pv1.emplace(std::make_unique<int>(100));

    // Перемещаем контейнер
    PageView<std::unique_ptr<int>> pv2 = std::move(pv1);

    EXPECT_EQ(pv1.size(), 0);
    EXPECT_EQ(pv2.size(), 2);

    // Проверяем целостность перемещённых элементов
    EXPECT_EQ(**pv2.get(id1), 42);
    EXPECT_EQ(**pv2.get(id2), 100);

    // Добавляем новый элемент в перемещённый контейнер
    auto id3 = pv2.emplace(std::make_unique<int>(200));
    EXPECT_EQ(**pv2.get(id3), 200);
}

TEST(PageViewMoveTest, ElementRelocation) 
{
    PageView<std::string, 3> pv;

    auto id1 = pv.emplace("A");
    auto id2 = pv.emplace("B");
    auto id3 = pv.emplace("C");

    // Удаляем элемент в середине страницы
    EXPECT_TRUE(pv.erase(id2));

    // Проверяем что последний элемент переместился
    EXPECT_EQ(pv.size(), 2);
    EXPECT_EQ(*pv.get(id1), "A");
    EXPECT_EQ(*pv.get(id3), "C");

    // Добавляем новый элемент
    auto id4 = pv.emplace("D");
    EXPECT_EQ(*pv.get(id4), "D");
}


TEST(PageViewMoveTest, NonCopyableTypes) {
    struct Resource {
        int handle;
        Resource(int h) : handle(h) {}
        Resource(Resource&&) = default;
        Resource& operator=(Resource&&) = default;
        Resource(const Resource&) = delete;
    };

    PageView<Resource> pv;
    auto id1 = pv.emplace(1);
    auto id2 = pv.emplace(2);

    EXPECT_TRUE(pv.erase(id1));
    auto id3 = pv.emplace(3);

    EXPECT_EQ(pv.get(id2)->handle, 2);
    EXPECT_EQ(pv.get(id3)->handle, 3);
}

TEST(PageViewMoveTest, FullPageRelocation) {
    PageView<std::unique_ptr<int>, 2> pv;

    // Страница 0: [0:42, 1:100]
    auto id1 = pv.emplace(std::make_unique<int>(42));
    auto id2 = pv.emplace(std::make_unique<int>(100));

    // Страница 1: [0:200, 1:300]
    auto id3 = pv.emplace(std::make_unique<int>(200));
    auto id4 = pv.emplace(std::make_unique<int>(300));

    // Удаляем элемент из середины страницы 0
    EXPECT_TRUE(pv.erase(id1));

    // Страница 0 должна схлопнуться: [1:100] -> слот 0
    EXPECT_EQ(**pv.get(id2), 100);

    // Добавляем новый элемент - должен попасть в дыру в странице 0
    auto id5 = pv.emplace(std::make_unique<int>(500));

    auto [page5, slot5] = pv.decode_id(id5);
    EXPECT_EQ(page5, 0);
}

TEST(PageViewMoveTest, PerformanceWithMoveOnly) 
{
    constexpr int N = 10'000;
    PageView<std::unique_ptr<int>> pv;

    for (int i = 0; i < N; ++i) {
        pv.emplace(std::make_unique<int>(i));
    }

    for (int i = 0; i < N; i += 2) 
    {
        pv.erase(i);
    }

    for (int i = 0; i < N / 2; ++i) {
        pv.emplace(std::make_unique<int>(i + N));
    }

    EXPECT_EQ(pv.size(), N);
}

// Тест на корректность деструктора
TEST(PageViewMoveTest, Destructor) {
    static int counter = 0;
    struct Tracked {
        int value;
        Tracked(int v) : value(v) { counter++; }
        Tracked(Tracked&&) = default;
        ~Tracked() { counter--; }
    };

    {
        PageView<Tracked> pv;
        auto id1 = pv.emplace(1);
        auto id2 = pv.emplace(2);
        pv.erase(id1);
        auto id3 = pv.emplace(3);
    }

    EXPECT_EQ(counter, 0);
}
TEST(PageViewMoveTest, DestructorClear) {
    static int counter = 0;
    struct Tracked {
        int value;
        Tracked(int v) : value(v) { counter++; }
        Tracked(Tracked&&) = default;
        ~Tracked() { counter--; }
    };

    PageView<Tracked> pv;
    auto id1 = pv.emplace(1);
    auto id2 = pv.emplace(2);
    pv.erase(id1);
    auto id3 = pv.emplace(3);
    pv.clear();

    EXPECT_EQ(counter, 0);
}

TEST(PageViewTest, MultiPageOperations) 
{
    PageView<int, 2> pv;

    std::vector<PageView<int, 2>::ID> ids;
    for (int i = 0; i < 5; ++i) {
        ids.push_back(pv.emplace(i));
    }

    EXPECT_EQ(pv.size(), 5);
    EXPECT_EQ(pv.capacity(), 6);


    auto [page1, local1] = pv.decode_id(ids[0]);
    auto [page2, local2] = pv.decode_id(ids[2]);
    auto [page3, local3] = pv.decode_id(ids[4]);

    EXPECT_EQ(page1, 0);
    EXPECT_EQ(page2, 1);
    EXPECT_EQ(page3, 2);

    pv.erase(ids[1]);
    EXPECT_EQ(pv.size(), 4);


    auto new_id = pv.emplace(100);
    auto [new_page, new_local] = pv.decode_id(new_id);
    EXPECT_EQ(new_page, 0);
}

TEST(PageViewTest, RandomErase) {
    PageView<int> pv;
    std::vector<PageView<int>::ID> ids;

    for (int i = 0; i < 1000; ++i) {
        ids.push_back(pv.emplace(i));
    }

    std::mt19937 rng(42);
    std::shuffle(ids.begin(), ids.end(), rng);

    for (int i = 0; i < 500; ++i) {
        EXPECT_TRUE(pv.erase(ids.back()));
        ids.pop_back();
    }

    EXPECT_EQ(pv.size(), 500);

    for (auto id : ids) {
        EXPECT_TRUE(pv.contains(id));
        EXPECT_EQ(*pv.get(id), static_cast<int>(id & 0xFFFFFFFF));
    }

    for (int i = 0; i < 500; ++i) {
        auto new_id = pv.emplace(i + 1000);
        ids.push_back(new_id);
    }

    EXPECT_EQ(pv.size(), 1000);

    std::vector<int> values;
    for (auto id : ids) {
        values.push_back(*pv.get(id));
    }

    std::sort(values.begin(), values.end());
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(values[i], i);
    }
}

// Тест на исключения
TEST(PageViewTest, ExceptionSafety) 
{
    PageView<int> pv;

    EXPECT_THROW({ pv.at(0xDEADBEEF); }, std::out_of_range);

    auto id = pv.emplace(42);
    EXPECT_TRUE(pv.erase(id));
    EXPECT_FALSE(pv.erase(id));

    EXPECT_FALSE(pv.contains(0x12345678));
    EXPECT_EQ(pv.get(0x87654321), nullptr);
}

TEST(PageViewTest, MemoryLeakCheck) {
    static int counter = 0;

    struct LeakChecker {
        int value;
        LeakChecker(int v) : value(v) { counter++; }
        ~LeakChecker() { counter--; }
    };

    {
        PageView<LeakChecker> pv;

        auto id1 = pv.emplace(1);
        auto id2 = pv.emplace(2);
        auto id3 = pv.emplace(3);

        pv.erase(id2);
        pv.emplace(4);
    }

    EXPECT_EQ(counter, 0);
}


TEST(PageViewTest, ConstCorrectness) {
    PageView<int> pv;
    auto id = pv.emplace(42);

    const auto& const_pv = pv;

    std::vector<int> const_values;
    for (auto it = const_pv.begin(); it != const_pv.end(); ++it) {
        const_values.push_back(*it);
    }

    EXPECT_EQ(*const_pv.get(id), 42);
    EXPECT_EQ(const_pv.at(id), 42);
    EXPECT_EQ(const_pv[id], 42);
}


TEST(PageViewTest, BoundaryConditions) {
    // Страница размером 1
    PageView<int, 1> pv;

    auto id1 = pv.emplace(1);
    auto id2 = pv.emplace(2);  // Новая страница

    EXPECT_EQ(pv.size(), 2);
    EXPECT_EQ(pv.capacity(), 2);

    pv.erase(id1);
    auto id3 = pv.emplace(3);  // Должен заполнить первую страницу

    auto [page, local] = pv.decode_id(id3);
    EXPECT_EQ(page, 0);
}