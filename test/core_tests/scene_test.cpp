#include "pch.h"
#include "core/scene.h"

#include <random>
#include <algorithm>

using namespace csyren::core;

struct TestComponent { int value; };
struct Position { float x, y; };
struct Velocity { float dx, dy; };
struct Health { int value; };
struct DummyComponent {};

class SceneTest : public ::testing::Test {
protected:
    events::EventBus2 bus;
    Scene scene{ bus };


    Entity::ID createEntityWithTestComponent(Entity::ID parent = Entity::invalidID) {
        auto id = scene.createEntity(parent);
        scene.addComponent<TestComponent>(id, 42);
        return id;
    }

    void flush()
    {
        scene.flush();
    };
};


TEST_F(SceneTest, EntityCreation) {
    auto id = scene.createEntity();
    EXPECT_NE(id, Entity::invalidID);
    EXPECT_TRUE(scene.entities().contains(id));
}

TEST_F(SceneTest, EntityDestruction) {
    auto id = scene.createEntity();
    scene.destroyEntity(id);
    flush();

    EXPECT_FALSE(scene.entities().contains(id));
}


TEST_F(SceneTest, ComponentOperations) {
    auto id = scene.createEntity();


    auto* comp = scene.addComponent<TestComponent>(id, 42);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->value, 42);


    auto* sameComp = scene.getComponent<TestComponent>(id);
    EXPECT_EQ(sameComp, comp);


    scene.removeComponent<TestComponent>(id);
    flush();
    EXPECT_EQ(scene.getComponent<TestComponent>(id), nullptr);
}

TEST_F(SceneTest, DuplicateComponent) {
    auto id = scene.createEntity();
    scene.addComponent<TestComponent>(id, 1);
    EXPECT_THROW(
        scene.addComponent<TestComponent>(id, 2),
        std::runtime_error
    );
}

TEST_F(SceneTest, EntityHierarchy) {
    auto parent = createEntityWithTestComponent();
    auto child1 = createEntityWithTestComponent(parent);
    auto child2 = createEntityWithTestComponent(parent);

    auto* parentEnt = scene.entities().try_get(parent);
    ASSERT_NE(parentEnt, nullptr);
    EXPECT_EQ(parentEnt->childrens.size(), 2);

    auto* childEnt = scene.entities().try_get(child1);
    ASSERT_NE(childEnt, nullptr);
    EXPECT_EQ(childEnt->parent, parent);

 
    scene.destroyEntity(child1);
    flush();

    parentEnt = scene.entities().try_get(parent);
    ASSERT_NE(parentEnt, nullptr);
    EXPECT_EQ(parentEnt->childrens.size(), 1);
    EXPECT_EQ(parentEnt->childrens[0], child2);

    scene.destroyEntity(parent);
    flush();
    EXPECT_FALSE(scene.entities().contains(parent));
    EXPECT_FALSE(scene.entities().contains(child2));
}

TEST_F(SceneTest, DeferredCommands) {
    auto id = createEntityWithTestComponent();

    scene.destroyEntity(id);
    EXPECT_TRUE(scene.entities().contains(id));

    flush();
    EXPECT_FALSE(scene.entities().contains(id));
}


TEST_F(SceneTest, EventDelivery) {
    int createCount = 0;
    int destroyCount = 0;

    auto createToken = bus.subscribe<events::EntityCreateEvent>(
        [&](const auto&) { createCount++; });

    auto destroyToken = bus.subscribe<events::EntityDestroyEvent>(
        [&](const auto&) { destroyCount++; });


    auto id = scene.createEntity();
    bus.commit_batch();
    EXPECT_EQ(createCount, 1);

    scene.destroyEntity(id);
    flush();
    bus.commit_batch();
    EXPECT_EQ(destroyCount, 1);
    bus.unsubscribe(createToken);
    bus.unsubscribe(destroyToken);
}

TEST_F(SceneTest, HighLoadOperations) {
    const int N = 10000;
    std::vector<Entity::ID> ids;


    for (int i = 0; i < N; i++) {
        ids.push_back(createEntityWithTestComponent());
    }

  
    for (auto id : ids) {
        auto* comp = scene.getComponent<TestComponent>(id);
        ASSERT_NE(comp, nullptr);
        EXPECT_EQ(comp->value, 42);
    }

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(ids.begin(), ids.end(), g);

    for (auto id : ids) {
        scene.destroyEntity(id);
    }
    flush();

    for (auto id : ids) {
        EXPECT_FALSE(scene.entities().contains(id));
    }
    EXPECT_EQ(scene.entities().size(), 0);
}

TEST_F(SceneTest, NonTrivialAccess) {
    auto root = scene.createEntity();
    struct SecondComponent {};
    // Создание иерархии
    std::vector<Entity::ID> children;
    for (int i = 0; i < 10; i++) {
        auto child = createEntityWithTestComponent(root);
        children.push_back(child);

        // Добавление второго компонента

        scene.addComponent<SecondComponent>(child);
    }

    // Удаление промежуточного компонента
    for (auto id : children) {
        scene.removeComponent<TestComponent>(id);
    }
    flush();

    // Проверка состояния
    for (auto id : children) {
        EXPECT_EQ(scene.getComponent<TestComponent>(id), nullptr);
        EXPECT_NE(scene.getComponent<SecondComponent>(id), nullptr);
    }

    // Удаление корня
    scene.destroyEntity(root);
    flush();
    EXPECT_FALSE(scene.entities().contains(root));
}



class SceneViewTest : public SceneTest {
protected:

    void SetUp() override {
        // Создаем тестовые данные
        auto e1 = scene.createEntity();
        scene.addComponent<Position>(e1, 1.0f, 2.0f);
        scene.addComponent<Velocity>(e1, 0.1f, 0.2f);

        auto e2 = scene.createEntity();
        scene.addComponent<Position>(e2, 3.0f, 4.0f);
        scene.addComponent<Health>(e2, 100);

        auto e3 = scene.createEntity();
        scene.addComponent<Position>(e3, 5.0f, 6.0f);
        scene.addComponent<Velocity>(e3, 0.3f, 0.4f);
        scene.addComponent<Health>(e3, 200);

        flush();
    }
};

TEST_F(SceneViewTest, SingleComponentView) {
    int count = 0;
    scene.view<Position>().each([&](Entity::ID id, Position& pos) {
        count++;
        EXPECT_TRUE(pos.x > 0 && pos.y > 0);
        });
    EXPECT_EQ(count, 3);
}

TEST_F(SceneViewTest, TwoComponentView) {
    std::vector<Entity::ID> entities;
    scene.view<Position, Velocity>().each([&](Entity::ID id, Position& pos, Velocity& vel) {
        entities.push_back(id);
        EXPECT_NE(pos.x, 0.0f);
        EXPECT_NE(vel.dx, 0.0f);
        });
    EXPECT_EQ(entities.size(), 2);
}

TEST_F(SceneViewTest, ThreeComponentView) {
    int count = 0;
    scene.view<Position, Velocity, Health>().each([&](auto id, auto&...) {
        count++;
        });
    EXPECT_EQ(count, 1);
}

TEST_F(SceneViewTest, EmptyView) {


    int count = 0;
    scene.view<DummyComponent>().each([&](auto...) {
        count++;
        });
    EXPECT_EQ(count, 0);
}

TEST_F(SceneViewTest, IteratorAccess) {
    auto view = scene.view<Position, Health>();
    size_t count = 0;
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [id, pos, health] = *it;
        EXPECT_NE(pos.x, 0.0f);
        EXPECT_GT(health.value, 0);
        count++;
    }
    EXPECT_EQ(count, 2);
}

TEST_F(SceneViewTest, PartialComponentRemoval) {
    // Удаляем компонент у одной сущности
    auto view = scene.view<Position>();
    Entity::ID target = Entity::invalidID;

    view.each([&](Entity::ID id, Position&) {
        if (id == 2) {
            scene.removeComponent<Position>(id);
            target = id;
        }
        });
    flush(); // Применяем изменения

    // Проверяем обновленное состояние
    int count = 0;
    scene.view<Position>().each([&](auto...) { count++; });
    EXPECT_EQ(count, 2);

    // Должны получить nullptr для удаленного компонента
    EXPECT_EQ(scene.getComponent<Position>(target), nullptr);
}

class SceneViewPerformanceTest : public SceneTest 
{
};

TEST_F(SceneViewPerformanceTest, PerformanceTest) 
{
    for (int i = 0; i < 10000; i++) {
        auto id = scene.createEntity();
        scene.addComponent<Position>(id, static_cast<float>(i), 0.0f);
        if (i % 5 == 0) {
            scene.addComponent<Health>(id, i);
        }
    }

    auto start = std::chrono::high_resolution_clock::now();

    int count = 0;
    scene.view<Position, Health>().each([&](Entity::ID id, Position& pos, Health& health) {
        EXPECT_EQ(static_cast<int>(pos.x), health.value);
        count++;
        });

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_EQ(count, 2000); 
    std::cout << "View with 2000 entities took: " << duration.count() << "ms\n";
}

class SceneViewHardcoreTest : public SceneTest {
protected:
    // Вспомогательная функция для создания сущностей с разным набором компонентов
    void createEntityWithComponents(const std::vector<int>& componentTypes) {
        auto id = scene.createEntity();
        for (int type : componentTypes) {
            switch (type) {
            case 1: scene.addComponent<Position>(id); break;
            case 2: scene.addComponent<Velocity>(id); break;
            case 3: scene.addComponent<Health>(id); break;
            case 4: scene.addComponent<DummyComponent>(id); break;
            }
        }
    }
};

TEST_F(SceneViewHardcoreTest, IterationWithConcurrentModification) 
{
    for (int i = 0; i < 1000; ++i) {
        auto id = scene.createEntity();
        scene.addComponent<Position>(id, (float)i, (float)i);
        scene.addComponent<Velocity>(id, 1.0f, 1.0f);
    }
    flush();

    auto view = scene.view<Position, Velocity>();
    int count = 0;

 
    ASSERT_NO_THROW({
        for (auto [id, pos, vel] : view) {

            if (static_cast<int>(pos.x) % 2 == 0) {
                scene.addComponent<Health>(id, 100);
            }
            if (static_cast<int>(pos.x) % 3 == 0) {
                 scene.removeComponent<Velocity>(id);
            }
            count++;
        }
        });


    EXPECT_EQ(count, 1000);

    flush();

    int healthCount = 0;
    scene.view<Health>().each([&](auto...) { healthCount++; });
    EXPECT_EQ(healthCount, 500);

    int velocityCount = 0;
    scene.view<Velocity>().each([&](auto...) { velocityCount++; });
    EXPECT_EQ(velocityCount, 1000 - 334);
}

TEST_F(SceneViewHardcoreTest, NeedleInAHaystack) 
{

    for (int i = 0; i < 100000; ++i) {
        scene.addComponent<Position>(scene.createEntity());
    }


    std::vector<Entity::ID> needles;
    for (int i = 0; i < 5; ++i) {
        auto id = scene.createEntity();
        scene.addComponent<Position>(id);
        scene.addComponent<Velocity>(id);
        scene.addComponent<Health>(id, i);
        needles.push_back(id);
    }
    flush();

    auto start = std::chrono::high_resolution_clock::now();

    int count = 0;
    std::vector<int> foundValues;
    scene.view<Position, Velocity, Health>().each([&](Entity::ID id,Position& pos,Velocity& vel, Health& health) {
        foundValues.push_back(health.value);
        count++;
        });

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    EXPECT_EQ(count, 5);
    std::sort(foundValues.begin(), foundValues.end());
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(foundValues[i], i);
    }

    std::cout << "Needle in a haystack search took: " << duration.count() << "us\n";
    EXPECT_LT(duration.count(), 1000);
}