#pragma once

#include "../IPhysicsAdapter.h"
#include "xrCommon/xr_vector.h"

// Forward declarations for Jolt types
namespace JPH
{
    class PhysicsSystem;
    class JobSystem;
    class TempAllocator;
    class BroadPhaseLayerInterface;
    class ObjectVsBroadPhaseLayerFilter;
    class ObjectLayerPairFilter;
    class ContactListener;
}

class JoltContactListener;

class JoltPhysicsWorld : public IPhysicsWorld
{
    friend class JoltContactListener;

public:
    JoltPhysicsWorld();
    virtual ~JoltPhysicsWorld();

    // IPhysicsWorld implementation
    PhysicsEngineType GetEngineType() const override { return PhysicsEngineType::Jolt; }

    bool Initialize() override;
    void Shutdown() override;

    void Step(float dt) override;
    void SetGravity(const Fvector& gravity) override;
    void GetGravity(Fvector& gravity) const override;
    float GetGravityMagnitude() const override;

    IPhysicsBody* CreateBody(PhysicsBodyType type) override;
    void DestroyBody(IPhysicsBody* body) override;

    IPhysicsShape* CreateSphere(float radius) override;
    IPhysicsShape* CreateBox(const Fvector& half_extents) override;
    IPhysicsShape* CreateCylinder(float radius, float height) override;
    IPhysicsShape* CreateCapsule(float radius, float height) override;
    IPhysicsShape* CreateMesh(const Fvector* vertices, u32 vertex_count,
                              const u32* indices, u32 index_count) override;
    void DestroyShape(IPhysicsShape* shape) override;

    IPhysicsConstraint* CreateConstraint(PhysicsConstraintType type,
                                          IPhysicsBody* body1,
                                          IPhysicsBody* body2) override;
    void DestroyConstraint(IPhysicsConstraint* constraint) override;

    bool RayCast(const Fvector& origin, const Fvector& direction,
                float max_distance, PhysicsRayHit& hit_out) override;

    void SetDebugDrawEnabled(bool enabled) override;
    bool IsDebugDrawEnabled() const override;

    u32 GetBodyCount() const override;
    u32 GetConstraintCount() const override;
    u32 GetActiveBodyCount() const override;

    void SetThreadCount(u32 count) override;
    u32 GetThreadCount() const override;

    // Internal accessors
    JPH::PhysicsSystem* GetPhysicsSystem() { return m_physics_system; }

private:
    bool InitializeJolt();
    void ShutdownJolt();

    JPH::PhysicsSystem* m_physics_system;
    JPH::JobSystem* m_job_system;
    JPH::TempAllocator* m_temp_allocator;
    JPH::BroadPhaseLayerInterface* m_broad_phase_layer;
    JPH::ObjectVsBroadPhaseLayerFilter* m_object_vs_broad_phase_filter;
    JPH::ObjectLayerPairFilter* m_object_layer_pair_filter;
    JPH::ContactListener* m_contact_listener;

    Fvector m_gravity;
    bool m_initialized;
    bool m_debug_draw_enabled;
    u32 m_thread_count;

    xr_vector<IPhysicsBody*> m_bodies;
    xr_vector<IPhysicsShape*> m_shapes;
    xr_vector<IPhysicsConstraint*> m_constraints;
};

// Factory function
IPhysicsWorld* CreateJoltPhysicsWorld();
