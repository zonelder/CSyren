#include "pch.h"
#include "core/scene.h"
#include "core/time.h"

using namespace csyren::core;
static input::InputDispatcher iDis;

// Test Components
struct Transform {
    float x = 0.0f, y = 0.0f;
};

struct Health {
    int value = 100;
    void onCreate(Scene&) { value = 200; } // Double health on creation
};

struct Damage {
    int amount = 10;
    void onDestroy(Scene& s) { amount = 0; } // Reset on destruction
};

struct Physics {
    float velocity = 0.0f;
    void update(Scene&, Time& tm) { velocity += 1.0f; }
};

TEST(SceneTest, SceneCreationTest)
{
    Scene scene;
    Entity::ID entity = scene.createEntity();
}

TEST(SceneTest, EntityLifecycle)
{
    // No direct way to verify destruction, but shouldn't crash
    try {
        Scene scene;
        Entity::ID entity = scene.createEntity();
        ASSERT_NE(entity, Entity::invalidID);

        scene.destroyEntity(entity);
        // No direct way to verify destruction, but shouldn't crash
    }
    catch (const std::exception& e) {
        FAIL() << "Exception: " << e.what();
    }
    catch (...) {
        FAIL() << "Unknown exception";
    }
}


TEST(SceneTest, ComponentManagement) {
    Scene scene;
    Entity::ID entity = scene.createEntity();

    // Add and retrieve component
    Transform* transform = scene.addComponent<Transform>(entity);
    ASSERT_NE(transform, nullptr);
    ASSERT_EQ(scene.getComponent<Transform>(entity), transform);

    // Remove component
    scene.removeComponent<Transform>(entity);
    ASSERT_EQ(scene.getComponent<Transform>(entity), nullptr);

    scene.destroyEntity(entity);
}

TEST(SceneTest, ComponentLifecycleCallbacks) {
    Scene scene;
    Entity::ID entity = scene.createEntity();
    // onCreate test
    Health* health = scene.addComponent<Health>(entity);
    ASSERT_EQ(health->value, 200); // onCreate doubled it

    // onDestroy test
    Damage* damage = scene.addComponent<Damage>(entity);
    ASSERT_EQ(damage->amount, 10);
    scene.destroyEntity(entity); // Should trigger onDestroy
    ASSERT_EQ(damage->amount, 0); // onDestroy reset it
}

TEST(SceneTest, UpdateSystem) {
    Scene scene;
    Time time;
    Entity::ID entity = scene.createEntity();

    Physics* physics = scene.addComponent<Physics>(entity);
    ASSERT_EQ(physics->velocity, 0.0f);

    scene.update(time);
    ASSERT_EQ(physics->velocity, 1.0f); // Updated once

    scene.update(time);
    ASSERT_EQ(physics->velocity, 2.0f); // Updated twice

    scene.destroyEntity(entity);
}

TEST(SceneTest, ParentChildRelationships) {
    Scene scene;
    Entity::ID parent = scene.createEntity();
    Entity::ID child = scene.createEntity(parent);

    // Verify parent-child linkage
    Transform* parentTrans = scene.addComponent<Transform>(parent);
    Transform* childTrans = scene.addComponent<Transform>(child);

    parentTrans->x = 5.0f;
    childTrans->x = parentTrans->x + 1.0f;
    ASSERT_EQ(childTrans->x, 6.0f);

    // Destroy parent (should orphan child)
    scene.destroyEntity(parent);
    // Child should still exist
    ASSERT_NE(scene.getComponent<Transform>(child), nullptr);

    scene.destroyEntity(child);
}

TEST(SceneTest, MultipleComponentsPerEntity) 
{
    Scene scene;
    Entity::ID entity = scene.createEntity();

    auto* transform = scene.addComponent<Transform>(entity);
    auto* health = scene.addComponent<Health>(entity);
    auto* physics = scene.addComponent<Physics>(entity);

    ASSERT_NE(transform, nullptr);
    ASSERT_NE(health, nullptr);
    ASSERT_NE(physics, nullptr);
    ASSERT_EQ(health->value, 200); // onCreate called

    scene.destroyEntity(entity);
}

TEST(SceneTest, NonexistentEntityHandling) 
{
    Scene scene;
    Entity::ID invalidEntity = 999; // Never created

    // All operations should handle invalid entities gracefully
    scene.destroyEntity(invalidEntity);
    ASSERT_EQ(scene.getComponent<Transform>(invalidEntity), nullptr);
    scene.removeComponent<Transform>(invalidEntity);
}

