#include "pch.h"
#include "physic_system.h"

#include <iostream>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/JobSystemThreadPool.h>

void* PhysicsEngine::sAllocate(size_t inSize) { return malloc(inSize); }
void PhysicsEngine::sFree(void* inBlock) { free(inBlock); }
void* PhysicsEngine::sAlignedAllocate(size_t inSize, size_t inAlignment) { return _aligned_malloc(inSize, inAlignment); }
void PhysicsEngine::sAlignedFree(void* inBlock) { _aligned_free(inBlock); }

namespace Layers
{
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer COUNT = 2;
};

namespace BroadPhaseLayers
{
    // Каждый слой объектов должен быть отнесен к своему "широкому" слою.
    // Мы можем создать простое соответствие.
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint COUNT(2);
};

// 1. ИНТЕРФЕЙС ДЛЯ BROAD PHASE СЛОЕВ (BPLayerInterface)
// Определяет, к какому "широкому" слою относится каждый "объектный" слой.
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        // Создаем соответствие между слоями объектов и широкими слоями
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }

    virtual JPH::uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::COUNT;
    }

    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        JPH_ASSERT(inLayer < Layers::COUNT);
        return mObjectToBroadPhase[inLayer];
    }

private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::COUNT];
};


// 2. ФИЛЬТР СТОЛКНОВЕНИЙ ОБЪЕКТОВ И BROAD PHASE СЛОЕВ (ObjectVsBroadPhaseLayerFilter)
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    // Возвращает true, если объект 'inLayer1' должен сталкиваться с широким слоем 'inLayer2'
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
    {
        switch (inLayer1)
        {
        case Layers::NON_MOVING:
            // Статические объекты сталкиваются только с динамическими
            return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING:
            // Динамические объекты сталкиваются со всеми
            return true;
        default:
            return false;
        }
    }
};

// 3. ФИЛЬТР СТОЛКНОВЕНИЙ МЕЖДУ ДВУМЯ СЛОЯМИ ОБЪЕКТОВ (ObjectLayerPairFilter)
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
    // Возвращает true, если два слоя объектов должны сталкиваться
    virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
    {
        switch (inObject1)
        {
        case Layers::NON_MOVING:
            // Статические объекты сталкиваются только с динамическими
            return inObject2 == Layers::MOVING;
        case Layers::MOVING:
            // Динамические объекты сталкиваются со всеми
            return true;
        default:
            return false;
        }
    }
};


PhysicsEngine::PhysicsEngine()
{
    std::cout << "PhysicsEngine created." << std::endl;
}

PhysicsEngine::~PhysicsEngine()
{
    if (m_physicsSystem)
    {
        shutdown();
    }
}

void PhysicsEngine::initialize()
{
    // ... регистрация аллокаторов, типов, создание TempAllocator и JobSystem без изменений ...
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    m_tempAllocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    m_jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

    // Создаем экземпляры наших интерфейсов.
    // Они должны существовать все время, пока работает физическая система,
    // поэтому делаем их статическими или членами класса.
    static BPLayerInterfaceImpl sBroadPhaseLayerInterface;
    static ObjectVsBroadPhaseLayerFilterImpl sObjectVsBroadPhaseLayerFilter;
    static ObjectLayerPairFilterImpl sObjectLayerPairFilter;

    // Создание самой физической системы
    m_physicsSystem = new JPH::PhysicsSystem();

    // ПРАВИЛЬНЫЙ ВЫЗОВ INIT
    m_physicsSystem->Init(
        1024, // Максимальное количество тел
        0,    // Максимальное количество мьютексов тел (0 - хорошее значение по умолчанию)
        1024, // Максимальное количество пар "тело-тело"
        1024, // Максимальное количество пар "тело-триггер"
        sBroadPhaseLayerInterface,
        sObjectVsBroadPhaseLayerFilter,
        sObjectLayerPairFilter
    );

    csyren::log::debug("Jolt Physics System Initialized CORRECTLY.");
}


void PhysicsEngine::shutdown()
{
    // 1. Очищаем ресурсы в обратном порядке
    delete m_physicsSystem;
    m_physicsSystem = nullptr;

    delete m_jobSystem;
    m_jobSystem = nullptr;

    delete m_tempAllocator;
    m_tempAllocator = nullptr;

    // 2. Дерегистрация типов
    JPH::UnregisterTypes();

    csyren::log::debug("Jolt Physics System shutdown.");
}

void PhysicsEngine::TestCreateObject()
{
    if (!m_physicsSystem) return;

    // Просто создаем форму коробки, чтобы проверить, что классы Jolt доступны
    JPH::BoxShapeSettings box_settings(JPH::Vec3(0.5f, 1.0f, 2.0f));
    JPH::Shape::ShapeResult result;
    JPH::ShapeRefC shape = box_settings.Create().Get(); // Get() вернет результат

    if (result.HasError())
    {
        csyren::log::debug("Test failed: Could not create shape. Error: {} ", result.GetError());
    }
    else
    {
        csyren::log::debug("Test successful: Jolt shape created!");
    }
}
