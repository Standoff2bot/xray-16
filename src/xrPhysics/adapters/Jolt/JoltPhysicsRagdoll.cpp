#include "StdAfx.h"
#include "JoltPhysicsRagdoll.h"
#include "JoltPhysicsWorld.h"
#include "JoltPhysicsBody.h"
#include "JoltPhysicsShape.h"
#include "JoltPhysicsConstraint.h"

#ifdef XRPHYSICS_JOLT

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include "xrCore/Animation/Bone.hpp"
#include "xrCore/Animation/Motion.hpp"

// Constructor
JoltPhysicsRagdoll::JoltPhysicsRagdoll(JoltPhysicsWorld* world)
    : m_world(world)
    , m_root_element(nullptr)
    , m_is_active(false)
    , m_is_enabled(false)
    , m_collision_group(0)
    , m_ragdoll_collision_mode(true)
    , m_material_id(0)
    , m_friction(0.5f)
    , m_restitution(0.0f)
    , m_user_data(nullptr)
    , m_debug_draw(false)
{
}

// Destructor
JoltPhysicsRagdoll::~JoltPhysicsRagdoll()
{
    // Clean up all elements
    for (auto* element : m_elements)
    {
        DestroyElementInternal(element);
    }
    m_elements.clear();
    m_bone_to_element.clear();
    m_root_element = nullptr;
}

// Build ragdoll from skeleton
bool JoltPhysicsRagdoll::BuildFromKinematics(IKinematics* kinematics, bool create_all_bones)
{
    if (!kinematics)
        return false;

    // Clear existing ragdoll
    for (auto* element : m_elements)
    {
        DestroyElementInternal(element);
    }
    m_elements.clear();
    m_bone_to_element.clear();
    m_root_element = nullptr;

    // Build ragdoll hierarchy from skeleton
    // Start with root bone (typically bone 0)
    u16 root_bone_id = kinematics->LL_GetBoneRoot();

    if (create_all_bones)
    {
        // Create physics elements for all bones
        RecursiveBuildFromKinematics(kinematics, root_bone_id, nullptr);
    }
    else
    {
        // Only create elements for bones with physics shapes defined
        // This requires checking bone shapes in the skeleton
        // For now, we'll create for all bones - specific filtering can be added later
        RecursiveBuildFromKinematics(kinematics, root_bone_id, nullptr);
    }

    // Find and cache root element
    m_root_element = FindRootElement();

    return m_root_element != nullptr;
}

// Recursively build ragdoll from skeleton
void JoltPhysicsRagdoll::RecursiveBuildFromKinematics(IKinematics* kinematics, u16 bone_id, RagdollElement* parent)
{
    // Get bone data
    CBoneData& bone_data = kinematics->LL_GetData(bone_id);

    // Only create elements for bones with physics shapes
    if (!bone_data.shape.Valid())
    {
        // No physics shape, but still recurse to children
        for (u16 i = 0; i < kinematics->LL_BoneCount(); i++)
        {
            CBoneData& child_data = kinematics->LL_GetData(i);
            if (child_data.GetParentID() == bone_id)
            {
                RecursiveBuildFromKinematics(kinematics, i, parent);
            }
        }
        return;
    }

    // Create element for this bone
    u16 parent_bone_id = parent ? parent->bone_id : u16(-1);
    RagdollElement* element = AddElement(bone_id, parent_bone_id);

    if (!element)
        return;

    // Add physics shapes based on bone shape data
    const SBoneShape& shape = bone_data.shape;

    switch (shape.type)
    {
        case SBoneShape::stSphere:
        {
            Fvector center;
            center.set(shape.sphere.P);
            float radius = shape.sphere.R;
            AddSphereToElement(bone_id, center, radius);
            break;
        }

        case SBoneShape::stBox:
        {
            Fvector center = shape.box.m_translate;
            Fvector half_extents = shape.box.m_halfsize;
            Fmatrix rotation = shape.box.m_rotate;
            AddBoxToElement(bone_id, center, half_extents, rotation);
            break;
        }

        case SBoneShape::stCylinder:
        {
            Fvector center = shape.cylinder.m_center;
            float radius = shape.cylinder.m_radius;
            float height = shape.cylinder.m_height;
            Fmatrix rotation = shape.cylinder.m_direction;  // Cylinder orientation
            AddCapsuleToElement(bone_id, center, radius, height, rotation);
            break;
        }

        default:
            // Unknown shape type, create a default small sphere
            AddSphereToElement(bone_id, Fvector().set(0, 0, 0), 0.05f);
            break;
    }

    // Set element mass from bone mass
    if (bone_data.shape.Valid())
    {
        float mass = bone_data.shape.getMass();
        if (mass > 0.0f)
        {
            SetElementMass(bone_id, mass);
        }
    }

    // Configure joint limits if this is not the root
    if (parent)
    {
        // Get joint limits from bone data
        // X-Ray stores these in the bone IK data
        // For now, use reasonable defaults - can be refined with actual bone data later
        float limit_low = -PI / 4.0f;  // -45 degrees
        float limit_high = PI / 4.0f;   // +45 degrees

        SetJointLimits(bone_id, limit_low, limit_high, 0);
        SetJointLimits(bone_id, limit_low, limit_high, 1);
        SetJointLimits(bone_id, limit_low, limit_high, 2);

        // Set spring/damping for natural ragdoll movement
        SetJointSpringDamping(bone_id, 0.0f, 0.1f, 0);
    }

    // Recurse to child bones
    for (u16 i = 0; i < kinematics->LL_BoneCount(); i++)
    {
        CBoneData& child_data = kinematics->LL_GetData(i);
        if (child_data.GetParentID() == bone_id)
        {
            RecursiveBuildFromKinematics(kinematics, i, element);
        }
    }
}

// Add element to ragdoll
RagdollElement* JoltPhysicsRagdoll::AddElement(u16 bone_id, u16 parent_bone_id)
{
    // Check if element already exists
    if (m_bone_to_element.find(bone_id) != m_bone_to_element.end())
    {
        Msg("! JoltPhysicsRagdoll: Element for bone %d already exists", bone_id);
        return m_bone_to_element[bone_id];
    }

    // Create new element
    RagdollElement* element = xr_new<RagdollElement>();
    element->bone_id = bone_id;
    element->body = m_world->CreateBody(PhysicsBodyType::Dynamic);
    element->joint = nullptr;
    element->parent = nullptr;
    element->mass = 1.0f;
    element->is_breakable = false;
    element->break_force = FLT_MAX;
    element->break_torque = FLT_MAX;

    // Find parent element
    if (parent_bone_id != u16(-1))
    {
        auto parent_it = m_bone_to_element.find(parent_bone_id);
        if (parent_it != m_bone_to_element.end())
        {
            element->parent = parent_it->second;
            element->parent->children.push_back(element);

            // Create joint connecting to parent
            CreateJoint(element, element->parent);
        }
    }

    // Store element
    m_elements.push_back(element);
    m_bone_to_element[bone_id] = element;

    // Apply current material settings to new body
    if (element->body)
    {
        element->body->SetFriction(m_friction);
        element->body->SetRestitution(m_restitution);
    }

    return element;
}

// Create joint between element and parent
void JoltPhysicsRagdoll::CreateJoint(RagdollElement* element, RagdollElement* parent)
{
    if (!element || !parent || !element->body || !parent->body)
        return;

    // Create a cone constraint (ragdoll joint)
    // This provides 3-DOF rotation typical for ragdoll joints
    element->joint = m_world->CreateConstraint(
        PhysicsConstraintType::Cone,
        parent->body,
        element->body
    );

    if (element->joint)
    {
        // Set anchor point at element position (will be refined when activated)
        Fvector anchor;
        element->body->GetPosition(anchor);
        element->joint->SetAnchor(anchor);

        // Set reasonable default cone angle
        element->joint->SetLimits(-PI / 4.0f, PI / 4.0f, 0);  // Swing limits
        element->joint->SetLimits(-PI / 4.0f, PI / 4.0f, 1);  // Twist limits
    }
}

// Get element by bone ID
RagdollElement* JoltPhysicsRagdoll::GetElement(u16 bone_id)
{
    auto it = m_bone_to_element.find(bone_id);
    return (it != m_bone_to_element.end()) ? it->second : nullptr;
}

const RagdollElement* JoltPhysicsRagdoll::GetElement(u16 bone_id) const
{
    auto it = m_bone_to_element.find(bone_id);
    return (it != m_bone_to_element.end()) ? it->second : nullptr;
}

// Get element by index
RagdollElement* JoltPhysicsRagdoll::GetElementByIndex(u16 index)
{
    return (index < m_elements.size()) ? m_elements[index] : nullptr;
}

// Get element count
u16 JoltPhysicsRagdoll::GetElementCount() const
{
    return static_cast<u16>(m_elements.size());
}

// Remove element
void JoltPhysicsRagdoll::RemoveElement(u16 bone_id)
{
    auto it = m_bone_to_element.find(bone_id);
    if (it == m_bone_to_element.end())
        return;

    RagdollElement* element = it->second;

    // Remove from parent's children list
    if (element->parent)
    {
        auto& children = element->parent->children;
        children.erase(std::remove(children.begin(), children.end(), element), children.end());
    }

    // Reparent children to this element's parent
    for (auto* child : element->children)
    {
        child->parent = element->parent;
        if (element->parent)
        {
            element->parent->children.push_back(child);
        }

        // Recreate joint to new parent
        if (child->joint)
        {
            m_world->DestroyConstraint(child->joint);
            child->joint = nullptr;
        }
        if (child->parent)
        {
            CreateJoint(child, child->parent);
        }
    }

    // Destroy element
    DestroyElementInternal(element);

    // Remove from storage
    m_elements.erase(std::remove(m_elements.begin(), m_elements.end(), element), m_elements.end());
    m_bone_to_element.erase(it);

    xr_delete(element);

    // Update root element if needed
    if (element == m_root_element)
    {
        m_root_element = FindRootElement();
    }
}

// Destroy element internal resources
void JoltPhysicsRagdoll::DestroyElementInternal(RagdollElement* element)
{
    if (!element)
        return;

    // Destroy joint
    if (element->joint)
    {
        m_world->DestroyConstraint(element->joint);
        element->joint = nullptr;
    }

    // Destroy body (and its shapes)
    if (element->body)
    {
        m_world->DestroyBody(element->body);
        element->body = nullptr;
    }
}

// Find root element (element with no parent)
RagdollElement* JoltPhysicsRagdoll::FindRootElement()
{
    for (auto* element : m_elements)
    {
        if (!element->parent)
            return element;
    }
    return m_elements.empty() ? nullptr : m_elements[0];
}

// Add shapes to elements
void JoltPhysicsRagdoll::AddSphereToElement(u16 bone_id, const Fvector& center, float radius)
{
    RagdollElement* element = GetElement(bone_id);
    if (!element || !element->body)
        return;

    IPhysicsShape* shape = m_world->CreateSphere(radius);
    if (shape)
    {
        element->body->AddShape(shape);
        // Note: Center offset would need to be applied via body transform or compound shape
    }
}

void JoltPhysicsRagdoll::AddBoxToElement(u16 bone_id, const Fvector& center, const Fvector& half_extents, const Fmatrix& rotation)
{
    RagdollElement* element = GetElement(bone_id);
    if (!element || !element->body)
        return;

    IPhysicsShape* shape = m_world->CreateBox(half_extents);
    if (shape)
    {
        element->body->AddShape(shape);
        // Note: Rotation and center offset would need to be applied via compound shape or body transform
    }
}

void JoltPhysicsRagdoll::AddCapsuleToElement(u16 bone_id, const Fvector& center, float radius, float height, const Fmatrix& rotation)
{
    RagdollElement* element = GetElement(bone_id);
    if (!element || !element->body)
        return;

    IPhysicsShape* shape = m_world->CreateCapsule(radius, height);
    if (shape)
    {
        element->body->AddShape(shape);
        // Note: Rotation and center offset would need to be applied
    }
}

// Joint configuration
void JoltPhysicsRagdoll::SetJointLimits(u16 bone_id, float low, float high, u32 axis_index)
{
    RagdollElement* element = GetElement(bone_id);
    if (element && element->joint)
    {
        element->joint->SetLimits(low, high, axis_index);
    }
}

void JoltPhysicsRagdoll::SetJointSpringDamping(u16 bone_id, float spring, float damping, u32 axis_index)
{
    RagdollElement* element = GetElement(bone_id);
    if (element && element->joint)
    {
        element->joint->SetSpringDamping(spring, damping, axis_index);
    }
}

void JoltPhysicsRagdoll::SetJointBreakable(u16 bone_id, bool breakable, float break_force, float break_torque)
{
    RagdollElement* element = GetElement(bone_id);
    if (element)
    {
        element->is_breakable = breakable;
        element->break_force = break_force;
        element->break_torque = break_torque;

        if (element->joint)
        {
            element->joint->SetBreakForce(break_force, break_torque);
        }
    }
}

bool JoltPhysicsRagdoll::IsJointBroken(u16 bone_id) const
{
    const RagdollElement* element = GetElement(bone_id);
    return element && element->joint && element->joint->IsBroken();
}

// Mass properties
void JoltPhysicsRagdoll::SetElementMass(u16 bone_id, float mass)
{
    RagdollElement* element = GetElement(bone_id);
    if (element)
    {
        element->mass = mass;
        if (element->body)
        {
            element->body->SetMass(mass);
        }
    }
}

float JoltPhysicsRagdoll::GetElementMass(u16 bone_id) const
{
    const RagdollElement* element = GetElement(bone_id);
    return element ? element->mass : 0.0f;
}

void JoltPhysicsRagdoll::SetTotalMass(float mass)
{
    float current_total = GetTotalMass();
    if (current_total <= 0.0f)
        return;

    float scale = mass / current_total;

    for (auto* element : m_elements)
    {
        element->mass *= scale;
        if (element->body)
        {
            element->body->SetMass(element->mass);
        }
    }
}

float JoltPhysicsRagdoll::GetTotalMass() const
{
    float total = 0.0f;
    for (const auto* element : m_elements)
    {
        total += element->mass;
    }
    return total;
}

// Activation/Deactivation
void JoltPhysicsRagdoll::Activate(const Fmatrix& transform)
{
    if (!m_root_element)
        return;

    // Set root transform
    SetRootTransform(transform);

    // Activate all bodies
    for (auto* element : m_elements)
    {
        if (element->body)
        {
            element->body->Enable();
            element->body->Activate();
        }
    }

    m_is_active = true;
    m_is_enabled = true;
}

void JoltPhysicsRagdoll::Activate(const Fmatrix& transform, const Fvector& linear_vel, const Fvector& angular_vel)
{
    Activate(transform);

    // Set velocities
    SetRootLinearVelocity(linear_vel);
    SetRootAngularVelocity(angular_vel);
}

void JoltPhysicsRagdoll::Deactivate()
{
    // Deactivate all bodies
    for (auto* element : m_elements)
    {
        if (element->body)
        {
            element->body->Deactivate();
        }
    }

    m_is_active = false;
}

bool JoltPhysicsRagdoll::IsActive() const
{
    return m_is_active;
}

// Enable/Disable
void JoltPhysicsRagdoll::Enable()
{
    for (auto* element : m_elements)
    {
        if (element->body)
        {
            element->body->Enable();
        }
    }
    m_is_enabled = true;
}

void JoltPhysicsRagdoll::Disable()
{
    for (auto* element : m_elements)
    {
        if (element->body)
        {
            element->body->Disable();
        }
    }
    m_is_enabled = false;
}

bool JoltPhysicsRagdoll::IsEnabled() const
{
    return m_is_enabled;
}

// Transform
void JoltPhysicsRagdoll::GetRootTransform(Fmatrix& transform) const
{
    if (m_root_element && m_root_element->body)
    {
        m_root_element->body->GetTransform(transform);
    }
    else
    {
        transform.identity();
    }
}

void JoltPhysicsRagdoll::SetRootTransform(const Fmatrix& transform)
{
    if (m_root_element && m_root_element->body)
    {
        m_root_element->body->SetTransform(transform);
    }
}

// Velocity
void JoltPhysicsRagdoll::GetRootLinearVelocity(Fvector& vel) const
{
    if (m_root_element && m_root_element->body)
    {
        m_root_element->body->GetLinearVelocity(vel);
    }
    else
    {
        vel.set(0, 0, 0);
    }
}

void JoltPhysicsRagdoll::GetRootAngularVelocity(Fvector& vel) const
{
    if (m_root_element && m_root_element->body)
    {
        m_root_element->body->GetAngularVelocity(vel);
    }
    else
    {
        vel.set(0, 0, 0);
    }
}

void JoltPhysicsRagdoll::SetRootLinearVelocity(const Fvector& vel)
{
    if (m_root_element && m_root_element->body)
    {
        m_root_element->body->SetLinearVelocity(vel);
    }
}

void JoltPhysicsRagdoll::SetRootAngularVelocity(const Fvector& vel)
{
    if (m_root_element && m_root_element->body)
    {
        m_root_element->body->SetAngularVelocity(vel);
    }
}

// Forces and impulses
void JoltPhysicsRagdoll::AddForce(const Fvector& force)
{
    // Apply force to all elements
    for (auto* element : m_elements)
    {
        if (element->body)
        {
            element->body->AddForce(force);
        }
    }
}

void JoltPhysicsRagdoll::AddForceAtBone(u16 bone_id, const Fvector& force, const Fvector& position)
{
    RagdollElement* element = GetElement(bone_id);
    if (element && element->body)
    {
        element->body->AddForceAtPosition(force, position);
    }
}

void JoltPhysicsRagdoll::AddImpulse(const Fvector& impulse)
{
    // Apply impulse to all elements
    for (auto* element : m_elements)
    {
        if (element->body)
        {
            element->body->AddImpulse(impulse);
        }
    }
}

void JoltPhysicsRagdoll::AddImpulseAtBone(u16 bone_id, const Fvector& impulse, const Fvector& position)
{
    RagdollElement* element = GetElement(bone_id);
    if (element && element->body)
    {
        element->body->AddImpulseAtPosition(impulse, position);
    }
}

// Hit system
void JoltPhysicsRagdoll::ApplyHit(u16 bone_id, const Fvector& position, const Fvector& direction,
                                  float impulse_magnitude)
{
    RagdollElement* element = GetElement(bone_id);
    if (!element || !element->body)
        return;

    // Calculate impulse vector
    Fvector impulse;
    impulse.set(direction);
    impulse.normalize();
    impulse.mul(impulse_magnitude);

    // Apply impulse at hit position
    element->body->AddImpulseAtPosition(impulse, position);

    // Check for joint breaking
    if (element->is_breakable && element->joint)
    {
        // Jolt will handle the breaking internally based on force thresholds
        // We just need to check if it broke in the next update
        UpdateBrokenJoints();
    }
}

// Update broken joints
void JoltPhysicsRagdoll::UpdateBrokenJoints()
{
    for (auto* element : m_elements)
    {
        if (element->joint && element->joint->IsBroken())
        {
            // Joint has broken - detach from parent
            if (element->parent)
            {
                auto& siblings = element->parent->children;
                siblings.erase(std::remove(siblings.begin(), siblings.end(), element), siblings.end());
            }

            element->parent = nullptr;

            // Destroy the broken joint
            m_world->DestroyConstraint(element->joint);
            element->joint = nullptr;
        }
    }
}

// Collision control
void JoltPhysicsRagdoll::SetCollisionGroup(u32 group)
{
    m_collision_group = group;
    // TODO: Apply collision group to all bodies via Jolt collision layers
}

u32 JoltPhysicsRagdoll::GetCollisionGroup() const
{
    return m_collision_group;
}

void JoltPhysicsRagdoll::SetRagdollCollisionMode(bool enabled)
{
    m_ragdoll_collision_mode = enabled;
    // TODO: Configure ragdoll-specific collision filtering
}

// Material
void JoltPhysicsRagdoll::SetMaterial(u16 material_id)
{
    m_material_id = material_id;
    // TODO: Apply material properties to all bodies
}

void JoltPhysicsRagdoll::SetFriction(float friction)
{
    m_friction = friction;
    for (auto* element : m_elements)
    {
        if (element->body)
        {
            element->body->SetFriction(friction);
        }
    }
}

void JoltPhysicsRagdoll::SetRestitution(float restitution)
{
    m_restitution = restitution;
    for (auto* element : m_elements)
    {
        if (element->body)
        {
            element->body->SetRestitution(restitution);
        }
    }
}

// Update bone transforms from physics
void JoltPhysicsRagdoll::UpdateBoneTransforms(IKinematics* kinematics)
{
    if (!kinematics)
        return;

    for (auto* element : m_elements)
    {
        if (!element->body)
            continue;

        // Get physics transform
        Fmatrix physics_transform;
        element->body->GetTransform(physics_transform);

        // Update skeleton bone transform
        CBoneInstance& bone_instance = kinematics->LL_GetBoneInstance(element->bone_id);
        bone_instance.mTransform.set(physics_transform);
    }
}

// Network synchronization
void JoltPhysicsRagdoll::Serialize(void* packet_data, u32& size)
{
    // Simple serialization format:
    // u16: element_count
    // For each element:
    //   u16: bone_id
    //   Fvector: position
    //   Fquaternion: rotation
    //   Fvector: linear_velocity
    //   Fvector: angular_velocity

    u8* data = static_cast<u8*>(packet_data);
    u32 offset = 0;

    // Write element count
    u16 count = GetElementCount();
    *reinterpret_cast<u16*>(data + offset) = count;
    offset += sizeof(u16);

    // Write each element's state
    for (auto* element : m_elements)
    {
        if (!element->body)
            continue;

        // Bone ID
        *reinterpret_cast<u16*>(data + offset) = element->bone_id;
        offset += sizeof(u16);

        // Position
        Fvector pos;
        element->body->GetPosition(pos);
        *reinterpret_cast<Fvector*>(data + offset) = pos;
        offset += sizeof(Fvector);

        // Rotation (as quaternion)
        Fmatrix rot;
        element->body->GetRotation(rot);
        Fquaternion quat;
        quat.set(rot);
        *reinterpret_cast<Fquaternion*>(data + offset) = quat;
        offset += sizeof(Fquaternion);

        // Linear velocity
        Fvector lin_vel;
        element->body->GetLinearVelocity(lin_vel);
        *reinterpret_cast<Fvector*>(data + offset) = lin_vel;
        offset += sizeof(Fvector);

        // Angular velocity
        Fvector ang_vel;
        element->body->GetAngularVelocity(ang_vel);
        *reinterpret_cast<Fvector*>(data + offset) = ang_vel;
        offset += sizeof(Fvector);
    }

    size = offset;
}

void JoltPhysicsRagdoll::Deserialize(const void* packet_data, u32 size)
{
    const u8* data = static_cast<const u8*>(packet_data);
    u32 offset = 0;

    // Read element count
    u16 count = *reinterpret_cast<const u16*>(data + offset);
    offset += sizeof(u16);

    // Read each element's state
    for (u16 i = 0; i < count; ++i)
    {
        // Bone ID
        u16 bone_id = *reinterpret_cast<const u16*>(data + offset);
        offset += sizeof(u16);

        RagdollElement* element = GetElement(bone_id);
        if (!element || !element->body)
        {
            // Skip this element's data
            offset += sizeof(Fvector) + sizeof(Fquaternion) + sizeof(Fvector) + sizeof(Fvector);
            continue;
        }

        // Position
        Fvector pos = *reinterpret_cast<const Fvector*>(data + offset);
        offset += sizeof(Fvector);
        element->body->SetPosition(pos);

        // Rotation
        Fquaternion quat = *reinterpret_cast<const Fquaternion*>(data + offset);
        offset += sizeof(Fquaternion);
        Fmatrix rot;
        rot.rotation(quat);
        element->body->SetRotation(rot);

        // Linear velocity
        Fvector lin_vel = *reinterpret_cast<const Fvector*>(data + offset);
        offset += sizeof(Fvector);
        element->body->SetLinearVelocity(lin_vel);

        // Angular velocity
        Fvector ang_vel = *reinterpret_cast<const Fvector*>(data + offset);
        offset += sizeof(Fvector);
        element->body->SetAngularVelocity(ang_vel);
    }
}

// Debugging
void JoltPhysicsRagdoll::SetDebugDraw(bool enabled)
{
    m_debug_draw = enabled;
    // TODO: Integrate with Jolt debug rendering
}

// User data
void JoltPhysicsRagdoll::SetUserData(void* data)
{
    m_user_data = data;
}

void* JoltPhysicsRagdoll::GetUserData() const
{
    return m_user_data;
}

#endif // XRPHYSICS_JOLT
