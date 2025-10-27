#pragma once
#include "Jolt/jolt.h"
#include "Jolt/Core/Factory.h"
#include "Jolt/core/TempAllocator.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"


namespace JPH {
    class JobSystem;
    class TempAllocator;
    class BroadPhaseLayerInterface;
    class ObjectVsBroadPhaseLayerFilter;
    class ObjectLayerPairFilter;
}


class PhysicsEngine
{
public:
    PhysicsEngine();
    ~PhysicsEngine();

    void initialize();
    void shutdown();

    void TestCreateObject();

private:
    JPH::PhysicsSystem* m_physicsSystem = nullptr;
    JPH::JobSystem* m_jobSystem = nullptr;
    JPH::TempAllocator* m_tempAllocator = nullptr;

    static void* sAllocate(size_t inSize);
    static void sFree(void* inBlock);
    static void* sAlignedAllocate(size_t inSize, size_t inAlignment);
    static void sAlignedFree(void* inBlock);
};

