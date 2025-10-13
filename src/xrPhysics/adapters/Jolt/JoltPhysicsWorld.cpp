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
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/ManifoldBetweenTwoFaces.h>

#include "xrCore/xrCore.h"
#include "JoltPhysicsShape.h"
#include "JoltPhysicsBody.h"
#include "JoltPhysicsConstraint.h"
#include "JoltPhysicsCharacter.h"
#include "JoltPhysicsRagdoll.h"
#include "xrMaterialSystem/GameMtlLib.h"

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

// Contact listener implementation
class JoltContactListener : public JPH::ContactListener
{
public:
    JoltContactListener(JoltPhysicsWorld* world)
        : m_world(world)
    {
    }

    virtual JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                                   JPH::RVec3Arg inBaseOffset,
                                                   const JPH::CollideShapeResult& inCollisionResult) override
    {
        // Find wrapper bodies to get material IDs
        IPhysicsBody* physics_body1 = FindBodyWrapper(inBody1.GetID());
        IPhysicsBody* physics_body2 = FindBodyWrapper(inBody2.GetID());

        if (!physics_body1 || !physics_body2)
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;

        // Cast to Jolt bodies to access material IDs
        JoltPhysicsBody* jolt_body1 = static_cast<JoltPhysicsBody*>(physics_body1);
        JoltPhysicsBody* jolt_body2 = static_cast<JoltPhysicsBody*>(physics_body2);

        int mat_id1 = jolt_body1->GetMaterialID();
        int mat_id2 = jolt_body2->GetMaterialID();

        // If either body has a valid material, check material flags
        if (mat_id1 >= 0 || mat_id2 >= 0)
        {
            SGameMtl* mtl1 = (mat_id1 >= 0) ? GMLib.GetMaterialByID(mat_id1) : nullptr;
            SGameMtl* mtl2 = (mat_id2 >= 0) ? GMLib.GetMaterialByID(mat_id2) : nullptr;

            // Check if either material is non-passable (blocks collision)
            if (mtl1 && !mtl1->Flags.test(SGameMtl::flPassable))
            {
                // Material 1 is solid, allow collision
            }
            else if (mtl2 && !mtl2->Flags.test(SGameMtl::flPassable))
            {
                // Material 2 is solid, allow collision
            }
            else if (mtl1 && mtl1->Flags.test(SGameMtl::flPassable) &&
                     mtl2 && mtl2->Flags.test(SGameMtl::flPassable))
            {
                // Both materials are passable, reject collision
                return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
            }
        }

        // Allow contact by default
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                               const JPH::ContactManifold& inManifold,
                               JPH::ContactSettings& ioSettings) override
    {
        // Find wrapper bodies
        IPhysicsBody* physics_body1 = FindBodyWrapper(inBody1.GetID());
        IPhysicsBody* physics_body2 = FindBodyWrapper(inBody2.GetID());

        if (!physics_body1 || !physics_body2)
            return;

        // Get collision callbacks
        ICollisionCallback* callback1 = physics_body1->GetCollisionCallback();
        ICollisionCallback* callback2 = physics_body2->GetCollisionCallback();

        if (!callback1 && !callback2)
            return;

        // Get first contact point from manifold
        if (inManifold.mRelativeContactPointsOn1.size() == 0)
            return;

        JPH::Vec3 contact_point1 = inManifold.mRelativeContactPointsOn1[0];
        JPH::Vec3 world_contact_point = inManifold.mBaseOffset + contact_point1;

        // Cast to Jolt bodies to access material IDs
        JoltPhysicsBody* jolt_body1 = static_cast<JoltPhysicsBody*>(physics_body1);
        JoltPhysicsBody* jolt_body2 = static_cast<JoltPhysicsBody*>(physics_body2);

        // Look up materials from material IDs
        int mat_id1 = jolt_body1->GetMaterialID();
        int mat_id2 = jolt_body2->GetMaterialID();

        SGameMtl* mtl1 = (mat_id1 >= 0) ? GMLib.GetMaterialByID(mat_id1) : nullptr;
        SGameMtl* mtl2 = (mat_id2 >= 0) ? GMLib.GetMaterialByID(mat_id2) : nullptr;

        // Apply material-based friction and restitution
        if (mtl1 || mtl2)
        {
            // Calculate combined friction and restitution from materials
            float friction1 = mtl1 ? mtl1->fPHFriction : 0.5f;
            float friction2 = mtl2 ? mtl2->fPHFriction : 0.5f;
            float restitution1 = mtl1 ? mtl1->fPHBouncing : 0.0f;
            float restitution2 = mtl2 ? mtl2->fPHBouncing : 0.0f;

            // Use geometric mean for friction (standard physics practice)
            ioSettings.mCombinedFriction = sqrtf(friction1 * friction2);

            // Use maximum for restitution (bouncy materials dominate)
            ioSettings.mCombinedRestitution = _max(restitution1, restitution2);
        }

        // Build PhysicsContact structure
        PhysicsContact contact;
        contact.position.set(world_contact_point.GetX(), world_contact_point.GetY(), world_contact_point.GetZ());
        contact.normal.set(inManifold.mWorldSpaceNormal.GetX(),
                          inManifold.mWorldSpaceNormal.GetY(),
                          inManifold.mWorldSpaceNormal.GetZ());
        contact.depth = inManifold.mPenetrationDepth;
        contact.body1 = physics_body1;
        contact.body2 = physics_body2;
        contact.material1 = mtl1;
        contact.material2 = mtl2;
        contact.user_data1 = physics_body1->GetUserData();
        contact.user_data2 = physics_body2->GetUserData();

        // Call callbacks
        bool allow_collision = true;

        if (callback1)
        {
            bool result = callback1->OnCollision(contact);
            allow_collision = allow_collision && result;
        }

        if (callback2)
        {
            // Flip contact for body2's callback
            PhysicsContact contact2 = contact;
            contact2.body1 = physics_body2;
            contact2.body2 = physics_body1;
            contact2.normal = contact2.normal.mul(-1.0f);
            contact2.material1 = contact.material2;
            contact2.material2 = contact.material1;
            contact2.user_data1 = contact.user_data2;
            contact2.user_data2 = contact.user_data1;

            bool result = callback2->OnCollision(contact2);
            allow_collision = allow_collision && result;
        }

        // If callback returned false, disable collision
        if (!allow_collision)
        {
            ioSettings.mCombinedRestitution = 0.0f;
            ioSettings.mCombinedFriction = 0.0f;
        }
    }

    virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                    const JPH::ContactManifold& inManifold,
                                    JPH::ContactSettings& ioSettings) override
    {
        // Similar to OnContactAdded, but for persistent contacts
        // For now, we'll just call the same logic as OnContactAdded
        OnContactAdded(inBody1, inBody2, inManifold, ioSettings);
    }

    virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
    {
        // Contact removed - could notify game code here if needed
        // For now, we don't track contact removal
    }

private:
    IPhysicsBody* FindBodyWrapper(const JPH::BodyID& body_id)
    {
        JPH::PhysicsSystem* physics_system = m_world->GetPhysicsSystem();
        if (!physics_system)
            return nullptr;

        // Get body count and iterate through bodies
        u32 body_count = m_world->GetBodyCount();
        for (u32 i = 0; i < body_count; ++i)
        {
            IPhysicsBody* body = m_world->m_bodies[i];
            JoltPhysicsBody* jolt_body = static_cast<JoltPhysicsBody*>(body);
            if (jolt_body->GetBodyID() == body_id)
            {
                return body;
            }
        }

        return nullptr;
    }

    JoltPhysicsWorld* m_world;
};

JoltPhysicsWorld::JoltPhysicsWorld()
    : m_physics_system(nullptr)
    , m_job_system(nullptr)
    , m_temp_allocator(nullptr)
    , m_broad_phase_layer(nullptr)
    , m_object_vs_broad_phase_filter(nullptr)
    , m_object_layer_pair_filter(nullptr)
    , m_contact_listener(nullptr)
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

    // Create and register contact listener
    m_contact_listener = new JoltContactListener(this);
    m_physics_system->SetContactListener(m_contact_listener);

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

    // Clean up characters
    for (auto character : m_characters)
    {
        xr_delete(character);
    }
    m_characters.clear();

    // Clean up ragdolls
    for (auto ragdoll : m_ragdolls)
    {
        xr_delete(ragdoll);
    }
    m_ragdolls.clear();

    // Clean up Jolt
    xr_delete(m_contact_listener);
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

IPhysicsBody* JoltPhysicsWorld::CreateBody(PhysicsBodyType type)
{
    if (!m_initialized || !m_physics_system)
    {
        Msg("! JoltPhysicsWorld::CreateBody: World not initialized");
        return nullptr;
    }

    // Convert body type to Jolt motion type
    JPH::EMotionType motion_type;
    JPH::ObjectLayer object_layer;

    switch (type)
    {
    case PhysicsBodyType::Static:
        motion_type = JPH::EMotionType::Static;
        object_layer = Layers::NON_MOVING;
        break;
    case PhysicsBodyType::Dynamic:
        motion_type = JPH::EMotionType::Dynamic;
        object_layer = Layers::MOVING;
        break;
    case PhysicsBodyType::Kinematic:
        motion_type = JPH::EMotionType::Kinematic;
        object_layer = Layers::MOVING;
        break;
    default:
        motion_type = JPH::EMotionType::Dynamic;
        object_layer = Layers::MOVING;
        break;
    }

    // Create a default box shape for the body (1m cube)
    // In practice, the user should call AddShape to set the actual shape
    JPH::BoxShapeSettings box_settings(JPH::Vec3(0.5f, 0.5f, 0.5f));
    JPH::ShapeSettings::ShapeResult shape_result = box_settings.Create();

    if (shape_result.HasError())
    {
        Msg("! JoltPhysicsWorld::CreateBody: Failed to create default shape - %s", shape_result.GetError().c_str());
        return nullptr;
    }

    // Create body creation settings
    JPH::BodyCreationSettings body_settings(
        shape_result.Get(),
        JPH::Vec3(0, 0, 0),
        JPH::Quat::sIdentity(),
        motion_type,
        object_layer
    );

    // Set default properties
    body_settings.mFriction = 0.5f;
    body_settings.mRestitution = 0.0f;
    body_settings.mGravityFactor = 1.0f;

    // Create the body
    JPH::BodyInterface& body_interface = m_physics_system->GetBodyInterface();
    JPH::Body* jolt_body = body_interface.CreateBody(body_settings);

    if (!jolt_body)
    {
        Msg("! JoltPhysicsWorld::CreateBody: Failed to create Jolt body");
        return nullptr;
    }

    JPH::BodyID body_id = jolt_body->GetID();

    // Add body to physics system
    body_interface.AddBody(body_id, JPH::EActivation::Activate);

    // Create wrapper
    JoltPhysicsBody* physics_body = new JoltPhysicsBody(this, body_id, type);
    m_bodies.push_back(physics_body);

    return physics_body;
}

void JoltPhysicsWorld::DestroyBody(IPhysicsBody* body)
{
    if (!body || !m_physics_system)
    {
        return;
    }

    // Cast to Jolt body
    JoltPhysicsBody* jolt_body = static_cast<JoltPhysicsBody*>(body);

    // Remove from Jolt physics system
    JPH::BodyInterface& body_interface = m_physics_system->GetBodyInterface();
    body_interface.RemoveBody(jolt_body->GetBodyID());
    body_interface.DestroyBody(jolt_body->GetBodyID());

    // Remove from tracking vector
    auto it = std::find(m_bodies.begin(), m_bodies.end(), body);
    if (it != m_bodies.end())
    {
        m_bodies.erase(it);
    }

    // Delete the wrapper
    xr_delete(body);
}

IPhysicsShape* JoltPhysicsWorld::CreateSphere(float radius)
{
    if (!m_initialized)
    {
        Msg("! JoltPhysicsWorld::CreateSphere: World not initialized");
        return nullptr;
    }

    // Create Jolt sphere shape
    JPH::SphereShapeSettings sphere_settings(radius);
    JPH::ShapeSettings::ShapeResult result = sphere_settings.Create();

    if (result.HasError())
    {
        Msg("! JoltPhysicsWorld::CreateSphere: Failed to create sphere - %s", result.GetError().c_str());
        return nullptr;
    }

    // Create wrapper
    JoltPhysicsShape* shape = new JoltPhysicsShape(PhysicsShapeType::Sphere, result.Get());
    m_shapes.push_back(shape);

    return shape;
}

IPhysicsShape* JoltPhysicsWorld::CreateBox(const Fvector& half_extents)
{
    if (!m_initialized)
    {
        Msg("! JoltPhysicsWorld::CreateBox: World not initialized");
        return nullptr;
    }

    // Create Jolt box shape
    JPH::Vec3 jolt_half_extents(half_extents.x, half_extents.y, half_extents.z);
    JPH::BoxShapeSettings box_settings(jolt_half_extents);
    JPH::ShapeSettings::ShapeResult result = box_settings.Create();

    if (result.HasError())
    {
        Msg("! JoltPhysicsWorld::CreateBox: Failed to create box - %s", result.GetError().c_str());
        return nullptr;
    }

    // Create wrapper
    JoltPhysicsShape* shape = new JoltPhysicsShape(PhysicsShapeType::Box, result.Get());
    m_shapes.push_back(shape);

    return shape;
}

IPhysicsShape* JoltPhysicsWorld::CreateCylinder(float radius, float height)
{
    if (!m_initialized)
    {
        Msg("! JoltPhysicsWorld::CreateCylinder: World not initialized");
        return nullptr;
    }

    // Create Jolt cylinder shape (height is half-height in Jolt)
    JPH::CylinderShapeSettings cylinder_settings(height * 0.5f, radius);
    JPH::ShapeSettings::ShapeResult result = cylinder_settings.Create();

    if (result.HasError())
    {
        Msg("! JoltPhysicsWorld::CreateCylinder: Failed to create cylinder - %s", result.GetError().c_str());
        return nullptr;
    }

    // Create wrapper
    JoltPhysicsShape* shape = new JoltPhysicsShape(PhysicsShapeType::Cylinder, result.Get());
    m_shapes.push_back(shape);

    return shape;
}

IPhysicsShape* JoltPhysicsWorld::CreateCapsule(float radius, float height)
{
    if (!m_initialized)
    {
        Msg("! JoltPhysicsWorld::CreateCapsule: World not initialized");
        return nullptr;
    }

    // Create Jolt capsule shape (height is half-height of cylindrical portion in Jolt)
    JPH::CapsuleShapeSettings capsule_settings(height * 0.5f, radius);
    JPH::ShapeSettings::ShapeResult result = capsule_settings.Create();

    if (result.HasError())
    {
        Msg("! JoltPhysicsWorld::CreateCapsule: Failed to create capsule - %s", result.GetError().c_str());
        return nullptr;
    }

    // Create wrapper
    JoltPhysicsShape* shape = new JoltPhysicsShape(PhysicsShapeType::Capsule, result.Get());
    m_shapes.push_back(shape);

    return shape;
}

IPhysicsShape* JoltPhysicsWorld::CreateMesh(const Fvector* vertices, u32 vertex_count,
                                             const u32* indices, u32 index_count)
{
    if (!m_initialized)
    {
        Msg("! JoltPhysicsWorld::CreateMesh: World not initialized");
        return nullptr;
    }

    if (!vertices || vertex_count == 0 || !indices || index_count == 0)
    {
        Msg("! JoltPhysicsWorld::CreateMesh: Invalid mesh data");
        return nullptr;
    }

    // Convert vertex data to Jolt format
    JPH::TriangleList triangles;
    triangles.reserve(index_count / 3);

    for (u32 i = 0; i < index_count; i += 3)
    {
        u32 i0 = indices[i];
        u32 i1 = indices[i + 1];
        u32 i2 = indices[i + 2];

        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
        {
            Msg("! JoltPhysicsWorld::CreateMesh: Index out of bounds");
            return nullptr;
        }

        const Fvector& v0 = vertices[i0];
        const Fvector& v1 = vertices[i1];
        const Fvector& v2 = vertices[i2];

        JPH::Triangle tri;
        tri.mV[0] = JPH::Float3(v0.x, v0.y, v0.z);
        tri.mV[1] = JPH::Float3(v1.x, v1.y, v1.z);
        tri.mV[2] = JPH::Float3(v2.x, v2.y, v2.z);

        triangles.push_back(tri);
    }

    // Create Jolt mesh shape
    JPH::MeshShapeSettings mesh_settings(triangles);
    JPH::ShapeSettings::ShapeResult result = mesh_settings.Create();

    if (result.HasError())
    {
        Msg("! JoltPhysicsWorld::CreateMesh: Failed to create mesh - %s", result.GetError().c_str());
        return nullptr;
    }

    // Create wrapper
    JoltPhysicsShape* shape = new JoltPhysicsShape(PhysicsShapeType::Mesh, result.Get());
    m_shapes.push_back(shape);

    return shape;
}

void JoltPhysicsWorld::DestroyShape(IPhysicsShape* shape)
{
    if (!shape)
    {
        return;
    }

    // Remove from tracking vector
    auto it = std::find(m_shapes.begin(), m_shapes.end(), shape);
    if (it != m_shapes.end())
    {
        m_shapes.erase(it);
    }

    // Delete the shape (JPH::Ref will handle Jolt cleanup)
    xr_delete(shape);
}

IPhysicsConstraint* JoltPhysicsWorld::CreateConstraint(PhysicsConstraintType type,
                                                        IPhysicsBody* body1,
                                                        IPhysicsBody* body2)
{
    if (!m_initialized || !m_physics_system)
    {
        Msg("! JoltPhysicsWorld::CreateConstraint: World not initialized");
        return nullptr;
    }

    if (!body1 || !body2)
    {
        Msg("! JoltPhysicsWorld::CreateConstraint: Invalid bodies");
        return nullptr;
    }

    // Cast to Jolt bodies
    JoltPhysicsBody* jolt_body1 = static_cast<JoltPhysicsBody*>(body1);
    JoltPhysicsBody* jolt_body2 = static_cast<JoltPhysicsBody*>(body2);

    JPH::BodyID body_id1 = jolt_body1->GetBodyID();
    JPH::BodyID body_id2 = jolt_body2->GetBodyID();

    // Default anchor at world origin, axis along Y
    JPH::Vec3 anchor(0, 0, 0);
    JPH::Vec3 axis(0, 1, 0);

    JPH::TwoBodyConstraint* jolt_constraint = nullptr;

    switch (type)
    {
    case PhysicsConstraintType::Fixed:
    {
        JPH::FixedConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = anchor;
        jolt_constraint = static_cast<JPH::TwoBodyConstraint*>(
            settings.Create(*m_physics_system->GetBodyLockInterface().TryGetBody(body_id1),
                           *m_physics_system->GetBodyLockInterface().TryGetBody(body_id2)));
        break;
    }

    case PhysicsConstraintType::Point:
    {
        JPH::PointConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = anchor;
        jolt_constraint = static_cast<JPH::TwoBodyConstraint*>(
            settings.Create(*m_physics_system->GetBodyLockInterface().TryGetBody(body_id1),
                           *m_physics_system->GetBodyLockInterface().TryGetBody(body_id2)));
        break;
    }

    case PhysicsConstraintType::Hinge:
    {
        JPH::HingeConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = anchor;
        settings.mHingeAxis1 = settings.mHingeAxis2 = axis;
        settings.mNormalAxis1 = settings.mNormalAxis2 = JPH::Vec3(1, 0, 0);
        jolt_constraint = static_cast<JPH::TwoBodyConstraint*>(
            settings.Create(*m_physics_system->GetBodyLockInterface().TryGetBody(body_id1),
                           *m_physics_system->GetBodyLockInterface().TryGetBody(body_id2)));
        break;
    }

    case PhysicsConstraintType::Slider:
    {
        JPH::SliderConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = anchor;
        settings.mSliderAxis1 = settings.mSliderAxis2 = axis;
        settings.mNormalAxis1 = settings.mNormalAxis2 = JPH::Vec3(1, 0, 0);
        jolt_constraint = static_cast<JPH::TwoBodyConstraint*>(
            settings.Create(*m_physics_system->GetBodyLockInterface().TryGetBody(body_id1),
                           *m_physics_system->GetBodyLockInterface().TryGetBody(body_id2)));
        break;
    }

    case PhysicsConstraintType::Distance:
    {
        JPH::DistanceConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = anchor;
        settings.mMinDistance = 0.0f;
        settings.mMaxDistance = 10.0f;
        jolt_constraint = static_cast<JPH::TwoBodyConstraint*>(
            settings.Create(*m_physics_system->GetBodyLockInterface().TryGetBody(body_id1),
                           *m_physics_system->GetBodyLockInterface().TryGetBody(body_id2)));
        break;
    }

    case PhysicsConstraintType::Cone:
    {
        JPH::ConeConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = anchor;
        settings.mTwistAxis1 = settings.mTwistAxis2 = axis;
        settings.mHalfConeAngle = JPH::DegreesToRadians(45.0f);
        jolt_constraint = static_cast<JPH::TwoBodyConstraint*>(
            settings.Create(*m_physics_system->GetBodyLockInterface().TryGetBody(body_id1),
                           *m_physics_system->GetBodyLockInterface().TryGetBody(body_id2)));
        break;
    }

    case PhysicsConstraintType::Generic:
    {
        JPH::SixDOFConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPosition1 = settings.mPosition2 = anchor;
        jolt_constraint = static_cast<JPH::TwoBodyConstraint*>(
            settings.Create(*m_physics_system->GetBodyLockInterface().TryGetBody(body_id1),
                           *m_physics_system->GetBodyLockInterface().TryGetBody(body_id2)));
        break;
    }

    default:
        Msg("! JoltPhysicsWorld::CreateConstraint: Unsupported constraint type");
        return nullptr;
    }

    if (!jolt_constraint)
    {
        Msg("! JoltPhysicsWorld::CreateConstraint: Failed to create Jolt constraint");
        return nullptr;
    }

    // Add constraint to physics system
    m_physics_system->AddConstraint(jolt_constraint);

    // Create wrapper
    JoltPhysicsConstraint* constraint = new JoltPhysicsConstraint(this, jolt_constraint, type, body1, body2);
    m_constraints.push_back(constraint);

    return constraint;
}

void JoltPhysicsWorld::DestroyConstraint(IPhysicsConstraint* constraint)
{
    if (!constraint || !m_physics_system)
    {
        return;
    }

    // Cast to Jolt constraint
    JoltPhysicsConstraint* jolt_constraint = static_cast<JoltPhysicsConstraint*>(constraint);

    // Remove from Jolt physics system
    m_physics_system->RemoveConstraint(jolt_constraint->GetJoltConstraint());

    // Remove from tracking vector
    auto it = std::find(m_constraints.begin(), m_constraints.end(), constraint);
    if (it != m_constraints.end())
    {
        m_constraints.erase(it);
    }

    // Delete the wrapper
    xr_delete(constraint);
}

bool JoltPhysicsWorld::RayCast(const Fvector& origin, const Fvector& direction,
                               float max_distance, PhysicsRayHit& hit_out)
{
    if (!m_initialized || !m_physics_system)
    {
        hit_out.hit = false;
        return false;
    }

    // Convert X-Ray ray to Jolt format
    JPH::Vec3 jolt_origin(origin.x, origin.y, origin.z);
    JPH::Vec3 jolt_direction(direction.x, direction.y, direction.z);

    // Normalize direction and scale by max distance
    float dir_length = jolt_direction.Length();
    if (dir_length < 1e-6f)
    {
        // Invalid direction
        hit_out.hit = false;
        return false;
    }

    jolt_direction = jolt_direction / dir_length;

    // Create ray cast settings
    JPH::RRayCast ray;
    ray.mOrigin = jolt_origin;
    ray.mDirection = jolt_direction * max_distance;

    // Create a closest hit collector
    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;

    // Broad phase layer filter - test against all layers
    class AllBroadPhaseLayerFilter : public JPH::BroadPhaseLayerFilter
    {
    public:
        virtual bool ShouldCollide(JPH::BroadPhaseLayer inLayer) const override
        {
            return true; // Test against all broad phase layers
        }
    };

    AllBroadPhaseLayerFilter broad_phase_filter;

    // Object layer filter - test against all objects
    class AllObjectLayerFilter : public JPH::ObjectLayerFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer) const override
        {
            return true; // Test against all objects
        }
    };

    AllObjectLayerFilter object_layer_filter;

    // Body filter - test against all bodies
    class AllBodyFilter : public JPH::BodyFilter
    {
    public:
        virtual bool ShouldCollide(const JPH::BodyID& inBodyID) const override
        {
            return true; // Test against all bodies
        }

        virtual bool ShouldCollideLocked(const JPH::Body& inBody) const override
        {
            return true; // Test against all bodies
        }
    };

    AllBodyFilter body_filter;

    // Cast the ray
    m_physics_system->GetNarrowPhaseQuery().CastRay(
        ray,
        collector,
        broad_phase_filter,
        object_layer_filter,
        body_filter
    );

    // Check if we hit something
    if (collector.HadHit())
    {
        JPH::RayCastResult hit = collector.mHit;

        // Fill in hit result
        hit_out.hit = true;
        hit_out.fraction = hit.mFraction;

        // Calculate hit position
        JPH::Vec3 hit_pos = jolt_origin + jolt_direction * (max_distance * hit.mFraction);
        hit_out.position.set(hit_pos.GetX(), hit_pos.GetY(), hit_pos.GetZ());

        // Get hit normal from the surface
        JPH::BodyLockRead lock(m_physics_system->GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded())
        {
            const JPH::Body& body = lock.GetBody();

            // Get the surface normal at hit point
            JPH::Vec3 normal = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hit_pos);
            hit_out.normal.set(normal.GetX(), normal.GetY(), normal.GetZ());

            // Find the corresponding body wrapper
            hit_out.body = nullptr;
            for (auto physics_body : m_bodies)
            {
                JoltPhysicsBody* jolt_body = static_cast<JoltPhysicsBody*>(physics_body);
                if (jolt_body->GetBodyID() == hit.mBodyID)
                {
                    hit_out.body = physics_body;
                    hit_out.user_data = physics_body->GetUserData();
                    break;
                }
            }

            // Shape is not directly accessible from ray hit
            hit_out.shape = nullptr;
        }
        else
        {
            // Couldn't lock body, set default normal
            hit_out.normal.set(0, 1, 0);
            hit_out.body = nullptr;
            hit_out.shape = nullptr;
            hit_out.user_data = nullptr;
        }

        return true;
    }

    // No hit
    hit_out.hit = false;
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

IPhysicsCharacter* JoltPhysicsWorld::CreateCharacter(float radius, float height)
{
    if (!m_initialized || !m_physics_system)
    {
        Msg("! JoltPhysicsWorld::CreateCharacter: World not initialized");
        return nullptr;
    }

    // Create character with Quake-style movement physics
    JoltPhysicsCharacter* character = new JoltPhysicsCharacter(this, radius, height);
    m_characters.push_back(character);

    Msg("* JoltPhysicsWorld: Created character controller (radius=%.2f, height=%.2f)", radius, height);

    return character;
}

void JoltPhysicsWorld::DestroyCharacter(IPhysicsCharacter* character)
{
    if (!character)
    {
        return;
    }

    // Remove from tracking vector
    auto it = std::find(m_characters.begin(), m_characters.end(), character);
    if (it != m_characters.end())
    {
        m_characters.erase(it);
    }

    // Delete the character
    xr_delete(character);
}

IPhysicsRagdoll* JoltPhysicsWorld::CreateRagdoll()
{
    if (!m_initialized || !m_physics_system)
    {
        Msg("! JoltPhysicsWorld::CreateRagdoll: World not initialized");
        return nullptr;
    }

    // Create ragdoll
    JoltPhysicsRagdoll* ragdoll = new JoltPhysicsRagdoll(this);
    m_ragdolls.push_back(ragdoll);

    Msg("* JoltPhysicsWorld: Created ragdoll system");

    return ragdoll;
}

void JoltPhysicsWorld::DestroyRagdoll(IPhysicsRagdoll* ragdoll)
{
    if (!ragdoll)
    {
        return;
    }

    // Remove from tracking vector
    auto it = std::find(m_ragdolls.begin(), m_ragdolls.end(), ragdoll);
    if (it != m_ragdolls.end())
    {
        m_ragdolls.erase(it);
    }

    // Delete the ragdoll
    xr_delete(ragdoll);
}

// Factory function
IPhysicsWorld* CreateJoltPhysicsWorld()
{
    return new JoltPhysicsWorld();
}

// Register this engine
REGISTER_PHYSICS_ENGINE(Jolt, CreateJoltPhysicsWorld)

#endif // XRPHYSICS_JOLT
