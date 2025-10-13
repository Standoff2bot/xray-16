#include "JoltPhysicsWorld.h"

#ifdef XRPHYSICS_JOLT

// Jolt includes
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include "xrCore/xrCore.h"

// Layer that objects can be in, determines which other objects it can collide with
namespace Layers
{
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};

// Broadphase layers
namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS(2);
};

// BroadPhaseLayerInterface implementation
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        // Create a mapping table from object to broad phase layer
        m_ObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        m_ObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }

    virtual uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return m_ObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
    {
        switch ((JPH::BroadPhaseLayer::Type)inLayer)
        {
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:  return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:      return "MOVING";
        default: JPH_ASSERT(false); return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer m_ObjectToBroadPhase[Layers::NUM_LAYERS];
};

// ObjectVsBroadPhaseLayerFilter implementation
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
    {
        switch (inLayer1)
        {
        case Layers::NON_MOVING:
            return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};

// ObjectLayerPairFilter implementation
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
    {
        switch (inObject1)
        {
        case Layers::NON_MOVING:
            return inObject2 == Layers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};

// Custom temp allocator
class TempAllocatorImpl : public JPH::TempAllocator
{
public:
    TempAllocatorImpl()
    {
        // Allocate 10 MB for temporary allocations
        m_buffer_size = 10 * 1024 * 1024;
        m_buffer = static_cast<uint8*>(JPH::AlignedAllocate(m_buffer_size, JPH_CACHE_LINE_SIZE));
        m_top = 0;
    }

    virtual ~TempAllocatorImpl()
    {
        JPH::AlignedFree(m_buffer);
    }

    virtual void* Allocate(uint inSize) override
    {
        inSize = JPH::AlignUp(inSize, JPH_CACHE_LINE_SIZE);
        uint offset = m_top.fetch_add(inSize, std::memory_order_relaxed);
        if (offset + inSize > m_buffer_size)
        {
            // Out of memory
            return nullptr;
        }
        return m_buffer + offset;
    }

    virtual void Free(void* inAddress, uint inSize) override
    {
        // We don't free individual allocations
    }

private:
    uint8* m_buffer;
    uint m_buffer_size;
    std::atomic<uint> m_top;
};

JoltPhysicsWorld::JoltPhysicsWorld()
    : m_physics_system(nullptr)
    , m_job_system(nullptr)
    , m_temp_allocator(nullptr)
    , m_broad_phase_layer(nullptr)
    , m_object_vs_broad_phase_filter(nullptr)
    , m_object_layer_pair_filter(nullptr)
    , m_gravity(0.0f, -9.81f, 0.0f)
    , m_initialized(false)
    , m_debug_draw_enabled(false)
    , m_thread_count(std::thread::hardware_concurrency())
{
}

JoltPhysicsWorld::~JoltPhysicsWorld()
{
    if (m_initialized)
    {
        Shutdown();
    }
}

bool JoltPhysicsWorld::Initialize()
{
    if (m_initialized)
    {
        Msg("! JoltPhysicsWorld::Initialize: Already initialized");
        return true;
    }

    Msg("* JoltPhysicsWorld: Initializing Jolt Physics...");

    // Register allocation hook
    JPH::RegisterDefaultAllocator();

    // Install callbacks
    JPH::Trace = [](const char* inFMT, ...)
    {
        va_list list;
        va_start(list, inFMT);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), inFMT, list);
        va_end(list);
        Msg("Jolt: %s", buffer);
    };

    JPH::AssertFailed = [](const char* inExpression, const char* inMessage, const char* inFile, uint inLine)
    {
        Msg("! Jolt Assertion Failed: %s at %s:%u - %s", inExpression, inFile, inLine, inMessage ? inMessage : "");
        return true; // Return true to trigger breakpoint
    };

    // Create factory
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all Jolt physics types
    JPH::RegisterTypes();

    // Create temp allocator
    m_temp_allocator = new TempAllocatorImpl();

    // Create job system
    m_job_system = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, m_thread_count - 1);

    // Create collision layers
    m_broad_phase_layer = new BPLayerInterfaceImpl();
    m_object_vs_broad_phase_filter = new ObjectVsBroadPhaseLayerFilterImpl();
    m_object_layer_pair_filter = new ObjectLayerPairFilterImpl();

    // Create physics system
    const uint cMaxBodies = 65536;
    const uint cNumBodyMutexes = 0; // Auto-detect
    const uint cMaxBodyPairs = 65536;
    const uint cMaxContactConstraints = 10240;

    m_physics_system = new JPH::PhysicsSystem();
    m_physics_system->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
                          *m_broad_phase_layer, *m_object_vs_broad_phase_filter, *m_object_layer_pair_filter);

    // Set gravity
    m_physics_system->SetGravity(JPH::Vec3(m_gravity.x, m_gravity.y, m_gravity.z));

    m_initialized = true;
    Msg("* JoltPhysicsWorld: Initialized successfully");
    Msg("  - Max bodies: %u", cMaxBodies);
    Msg("  - Max body pairs: %u", cMaxBodyPairs);
    Msg("  - Max contact constraints: %u", cMaxContactConstraints);
    Msg("  - Thread count: %u", m_thread_count);

    return true;
}

void JoltPhysicsWorld::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    Msg("* JoltPhysicsWorld: Shutting down...");

    // Clean up bodies
    for (auto body : m_bodies)
    {
        xr_delete(body);
    }
    m_bodies.clear();

    // Clean up shapes
    for (auto shape : m_shapes)
    {
        xr_delete(shape);
    }
    m_shapes.clear();

    // Clean up constraints
    for (auto constraint : m_constraints)
    {
        xr_delete(constraint);
    }
    m_constraints.clear();

    // Clean up Jolt
    xr_delete(m_physics_system);
    xr_delete(m_job_system);
    xr_delete(m_temp_allocator);
    xr_delete(m_broad_phase_layer);
    xr_delete(m_object_vs_broad_phase_filter);
    xr_delete(m_object_layer_pair_filter);

    // Unregister factory
    xr_delete(JPH::Factory::sInstance);
    JPH::Factory::sInstance = nullptr;

    m_initialized = false;
    Msg("* JoltPhysicsWorld: Shutdown complete");
}

void JoltPhysicsWorld::Step(float dt)
{
    if (!m_initialized || !m_physics_system)
    {
        return;
    }

    // Update the physics world
    const int cCollisionSteps = 1;
    m_physics_system->Update(dt, cCollisionSteps, m_temp_allocator, m_job_system);
}

void JoltPhysicsWorld::SetGravity(const Fvector& gravity)
{
    m_gravity = gravity;
    if (m_physics_system)
    {
        m_physics_system->SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }
}

void JoltPhysicsWorld::GetGravity(Fvector& gravity) const
{
    gravity = m_gravity;
}

float JoltPhysicsWorld::GetGravityMagnitude() const
{
    return m_gravity.magnitude();
}

// Stub implementations - to be filled in later
IPhysicsBody* JoltPhysicsWorld::CreateBody(PhysicsBodyType type)
{
    Msg("! JoltPhysicsWorld::CreateBody: Not implemented yet");
    return nullptr;
}

void JoltPhysicsWorld::DestroyBody(IPhysicsBody* body)
{
    Msg("! JoltPhysicsWorld::DestroyBody: Not implemented yet");
}

IPhysicsShape* JoltPhysicsWorld::CreateSphere(float radius)
{
    Msg("! JoltPhysicsWorld::CreateSphere: Not implemented yet");
    return nullptr;
}

IPhysicsShape* JoltPhysicsWorld::CreateBox(const Fvector& half_extents)
{
    Msg("! JoltPhysicsWorld::CreateBox: Not implemented yet");
    return nullptr;
}

IPhysicsShape* JoltPhysicsWorld::CreateCylinder(float radius, float height)
{
    Msg("! JoltPhysicsWorld::CreateCylinder: Not implemented yet");
    return nullptr;
}

IPhysicsShape* JoltPhysicsWorld::CreateCapsule(float radius, float height)
{
    Msg("! JoltPhysicsWorld::CreateCapsule: Not implemented yet");
    return nullptr;
}

IPhysicsShape* JoltPhysicsWorld::CreateMesh(const Fvector* vertices, u32 vertex_count,
                                             const u32* indices, u32 index_count)
{
    Msg("! JoltPhysicsWorld::CreateMesh: Not implemented yet");
    return nullptr;
}

void JoltPhysicsWorld::DestroyShape(IPhysicsShape* shape)
{
    Msg("! JoltPhysicsWorld::DestroyShape: Not implemented yet");
}

IPhysicsConstraint* JoltPhysicsWorld::CreateConstraint(PhysicsConstraintType type,
                                                        IPhysicsBody* body1,
                                                        IPhysicsBody* body2)
{
    Msg("! JoltPhysicsWorld::CreateConstraint: Not implemented yet");
    return nullptr;
}

void JoltPhysicsWorld::DestroyConstraint(IPhysicsConstraint* constraint)
{
    Msg("! JoltPhysicsWorld::DestroyConstraint: Not implemented yet");
}

bool JoltPhysicsWorld::RayCast(const Fvector& origin, const Fvector& direction,
                               float max_distance, PhysicsRayHit& hit_out)
{
    Msg("! JoltPhysicsWorld::RayCast: Not implemented yet");
    return false;
}

void JoltPhysicsWorld::SetDebugDrawEnabled(bool enabled)
{
    m_debug_draw_enabled = enabled;
}

bool JoltPhysicsWorld::IsDebugDrawEnabled() const
{
    return m_debug_draw_enabled;
}

u32 JoltPhysicsWorld::GetBodyCount() const
{
    return static_cast<u32>(m_bodies.size());
}

u32 JoltPhysicsWorld::GetConstraintCount() const
{
    return static_cast<u32>(m_constraints.size());
}

u32 JoltPhysicsWorld::GetActiveBodyCount() const
{
    if (!m_physics_system)
        return 0;

    return m_physics_system->GetNumActiveBodies();
}

void JoltPhysicsWorld::SetThreadCount(u32 count)
{
    m_thread_count = count;
    // Note: Would need to recreate job system to apply this
    Msg("! JoltPhysicsWorld::SetThreadCount: Thread count change requires restart");
}

u32 JoltPhysicsWorld::GetThreadCount() const
{
    return m_thread_count;
}

// Factory function
IPhysicsWorld* CreateJoltPhysicsWorld()
{
    return new JoltPhysicsWorld();
}

// Register this engine
REGISTER_PHYSICS_ENGINE(Jolt, CreateJoltPhysicsWorld)

#endif // XRPHYSICS_JOLT
