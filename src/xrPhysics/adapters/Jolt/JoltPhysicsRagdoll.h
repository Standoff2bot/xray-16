#pragma once

#include "../IPhysicsAdapter.h"
#include "xrCommon/xr_vector.h"
#include "xrCommon/xr_map.h"

#ifdef XRPHYSICS_JOLT

class JoltPhysicsWorld;
class IKinematics;

class JoltPhysicsRagdoll : public IPhysicsRagdoll
{
public:
    JoltPhysicsRagdoll(JoltPhysicsWorld* world);
    virtual ~JoltPhysicsRagdoll();

    // IPhysicsRagdoll implementation

    // Build from skeleton
    bool BuildFromKinematics(IKinematics* kinematics, bool create_all_bones = false) override;

    // Element management
    RagdollElement* AddElement(u16 bone_id, u16 parent_bone_id) override;
    RagdollElement* GetElement(u16 bone_id) override;
    const RagdollElement* GetElement(u16 bone_id) const override;
    RagdollElement* GetElementByIndex(u16 index) override;
    u16 GetElementCount() const override;
    void RemoveElement(u16 bone_id) override;

    // Shape addition to elements
    void AddSphereToElement(u16 bone_id, const Fvector& center, float radius) override;
    void AddBoxToElement(u16 bone_id, const Fvector& center, const Fvector& half_extents, const Fmatrix& rotation) override;
    void AddCapsuleToElement(u16 bone_id, const Fvector& center, float radius, float height, const Fmatrix& rotation) override;

    // Joint configuration
    void SetJointLimits(u16 bone_id, float low, float high, u32 axis_index = 0) override;
    void SetJointSpringDamping(u16 bone_id, float spring, float damping, u32 axis_index = 0) override;
    void SetJointBreakable(u16 bone_id, bool breakable, float break_force, float break_torque) override;
    bool IsJointBroken(u16 bone_id) const override;

    // Mass properties
    void SetElementMass(u16 bone_id, float mass) override;
    float GetElementMass(u16 bone_id) const override;
    void SetTotalMass(float mass) override;
    float GetTotalMass() const override;

    // Activation/Deactivation
    void Activate(const Fmatrix& transform) override;
    void Activate(const Fmatrix& transform, const Fvector& linear_vel, const Fvector& angular_vel) override;
    void Deactivate() override;
    bool IsActive() const override;

    // Enable/Disable
    void Enable() override;
    void Disable() override;
    bool IsEnabled() const override;

    // Transform
    void GetRootTransform(Fmatrix& transform) const override;
    void SetRootTransform(const Fmatrix& transform) override;

    // Velocity
    void GetRootLinearVelocity(Fvector& vel) const override;
    void GetRootAngularVelocity(Fvector& vel) const override;
    void SetRootLinearVelocity(const Fvector& vel) override;
    void SetRootAngularVelocity(const Fvector& vel) override;

    // Forces and impulses
    void AddForce(const Fvector& force) override;
    void AddForceAtBone(u16 bone_id, const Fvector& force, const Fvector& position) override;
    void AddImpulse(const Fvector& impulse) override;
    void AddImpulseAtBone(u16 bone_id, const Fvector& impulse, const Fvector& position) override;

    // Hit system (for damage application)
    void ApplyHit(u16 bone_id, const Fvector& position, const Fvector& direction,
                 float impulse_magnitude) override;

    // Collision control
    void SetCollisionGroup(u32 group) override;
    u32 GetCollisionGroup() const override;
    void SetRagdollCollisionMode(bool enabled) override;

    // Material
    void SetMaterial(u16 material_id) override;
    void SetFriction(float friction) override;
    void SetRestitution(float restitution) override;

    // Update bone transforms from physics
    void UpdateBoneTransforms(IKinematics* kinematics) override;

    // Network synchronization
    void Serialize(void* packet_data, u32& size) override;
    void Deserialize(const void* packet_data, u32 size) override;

    // Debugging
    void SetDebugDraw(bool enabled) override;

    // User data
    void SetUserData(void* data) override;
    void* GetUserData() const override;

private:
    // Helper methods
    void CreateJoint(RagdollElement* element, RagdollElement* parent);
    void UpdateBrokenJoints();
    RagdollElement* FindRootElement();
    void RecursiveBuildFromKinematics(IKinematics* kinematics, u16 bone_id, RagdollElement* parent);
    void DestroyElementInternal(RagdollElement* element);

    JoltPhysicsWorld* m_world;

    // Element storage
    xr_vector<RagdollElement*> m_elements;              // Flat array for fast iteration
    xr_map<u16, RagdollElement*> m_bone_to_element;     // Fast lookup by bone ID

    RagdollElement* m_root_element;                     // Root of hierarchy

    // State
    bool m_is_active;
    bool m_is_enabled;
    u32 m_collision_group;
    bool m_ragdoll_collision_mode;

    // Material properties
    u16 m_material_id;
    float m_friction;
    float m_restitution;

    // User data
    void* m_user_data;

    // Debug
    bool m_debug_draw;
};

#endif // XRPHYSICS_JOLT
