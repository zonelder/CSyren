#include "pch.h"
#include "core/scene.h"

#include <random>
#include <algorithm>

using namespace csyren::core;

struct TestComponent { int value; };


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
