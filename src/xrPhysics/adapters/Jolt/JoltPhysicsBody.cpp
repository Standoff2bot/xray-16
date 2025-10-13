#include "JoltPhysicsBody.h"

#ifdef XRPHYSICS_JOLT

// Jolt includes
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionType.h>

#include "xrCore/xrCore.h"
#include "JoltPhysicsWorld.h"
#include "JoltPhysicsShape.h"

JoltPhysicsBody::JoltPhysicsBody(JoltPhysicsWorld* world, const JPH::BodyID& body_id, PhysicsBodyType type)
    : m_world(world)
    , m_body_id(body_id)
    , m_type(type)
    , m_user_data(nullptr)
    , m_collision_callback(nullptr)
    , m_friction(0.5f)
    , m_restitution(0.0f)
{
}

JoltPhysicsBody::~JoltPhysicsBody()
{
}

PhysicsBodyType JoltPhysicsBody::GetType() const
{
    return m_type;
}

void JoltPhysicsBody::SetType(PhysicsBodyType type)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    m_type = type;
    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();

    // Convert to Jolt motion type
    JPH::EMotionType motion_type;
    switch (type)
    {
    case PhysicsBodyType::Static:
        motion_type = JPH::EMotionType::Static;
        break;
    case PhysicsBodyType::Dynamic:
        motion_type = JPH::EMotionType::Dynamic;
        break;
    case PhysicsBodyType::Kinematic:
        motion_type = JPH::EMotionType::Kinematic;
        break;
    default:
        motion_type = JPH::EMotionType::Dynamic;
        break;
    }

    body_interface.SetMotionType(m_body_id, motion_type, JPH::EActivation::Activate);
}

void JoltPhysicsBody::GetPosition(Fvector& pos) const
{
    if (!m_world || !m_world->GetPhysicsSystem())
    {
        pos.set(0, 0, 0);
        return;
    }

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    JPH::Vec3 jolt_pos = body_interface.GetPosition(m_body_id);
    pos.set(jolt_pos.GetX(), jolt_pos.GetY(), jolt_pos.GetZ());
}

void JoltPhysicsBody::SetPosition(const Fvector& pos)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.SetPosition(m_body_id, JPH::Vec3(pos.x, pos.y, pos.z), JPH::EActivation::Activate);
}

void JoltPhysicsBody::GetRotation(Fmatrix& rot) const
{
    if (!m_world || !m_world->GetPhysicsSystem())
    {
        rot.identity();
        return;
    }

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    JPH::Quat jolt_rot = body_interface.GetRotation(m_body_id);

    // Convert quaternion to rotation matrix
    JPH::Mat44 jolt_mat = JPH::Mat44::sRotation(jolt_rot);

    rot._11 = jolt_mat.GetColumn4(0).GetX();
    rot._12 = jolt_mat.GetColumn4(0).GetY();
    rot._13 = jolt_mat.GetColumn4(0).GetZ();

    rot._21 = jolt_mat.GetColumn4(1).GetX();
    rot._22 = jolt_mat.GetColumn4(1).GetY();
    rot._23 = jolt_mat.GetColumn4(1).GetZ();

    rot._31 = jolt_mat.GetColumn4(2).GetX();
    rot._32 = jolt_mat.GetColumn4(2).GetY();
    rot._33 = jolt_mat.GetColumn4(2).GetZ();

    rot._41 = 0.0f;
    rot._42 = 0.0f;
    rot._43 = 0.0f;
}

void JoltPhysicsBody::SetRotation(const Fmatrix& rot)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    // Convert rotation matrix to quaternion
    // Using Jolt's Mat44 and converting
    JPH::Mat44 jolt_mat(
        JPH::Vec4(rot._11, rot._12, rot._13, 0),
        JPH::Vec4(rot._21, rot._22, rot._23, 0),
        JPH::Vec4(rot._31, rot._32, rot._33, 0),
        JPH::Vec4(0, 0, 0, 1)
    );

    JPH::Quat jolt_quat = jolt_mat.GetQuaternion();

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.SetRotation(m_body_id, jolt_quat, JPH::EActivation::Activate);
}

void JoltPhysicsBody::GetTransform(Fmatrix& transform) const
{
    Fvector pos;
    Fmatrix rot;
    GetPosition(pos);
    GetRotation(rot);

    transform = rot;
    transform.c = pos;
}

void JoltPhysicsBody::SetTransform(const Fmatrix& transform)
{
    Fvector pos = transform.c;
    Fmatrix rot = transform;
    rot.c.set(0, 0, 0);

    SetPosition(pos);
    SetRotation(rot);
}

void JoltPhysicsBody::GetLinearVelocity(Fvector& vel) const
{
    if (!m_world || !m_world->GetPhysicsSystem())
    {
        vel.set(0, 0, 0);
        return;
    }

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    JPH::Vec3 jolt_vel = body_interface.GetLinearVelocity(m_body_id);
    vel.set(jolt_vel.GetX(), jolt_vel.GetY(), jolt_vel.GetZ());
}

void JoltPhysicsBody::SetLinearVelocity(const Fvector& vel)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.SetLinearVelocity(m_body_id, JPH::Vec3(vel.x, vel.y, vel.z));
}

void JoltPhysicsBody::GetAngularVelocity(Fvector& vel) const
{
    if (!m_world || !m_world->GetPhysicsSystem())
    {
        vel.set(0, 0, 0);
        return;
    }

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    JPH::Vec3 jolt_vel = body_interface.GetAngularVelocity(m_body_id);
    vel.set(jolt_vel.GetX(), jolt_vel.GetY(), jolt_vel.GetZ());
}

void JoltPhysicsBody::SetAngularVelocity(const Fvector& vel)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.SetAngularVelocity(m_body_id, JPH::Vec3(vel.x, vel.y, vel.z));
}

void JoltPhysicsBody::AddForce(const Fvector& force)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.AddForce(m_body_id, JPH::Vec3(force.x, force.y, force.z));
}

void JoltPhysicsBody::AddForceAtPosition(const Fvector& force, const Fvector& pos)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.AddForce(m_body_id, JPH::Vec3(force.x, force.y, force.z), JPH::Vec3(pos.x, pos.y, pos.z));
}

void JoltPhysicsBody::AddImpulse(const Fvector& impulse)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.AddImpulse(m_body_id, JPH::Vec3(impulse.x, impulse.y, impulse.z));
}

void JoltPhysicsBody::AddImpulseAtPosition(const Fvector& impulse, const Fvector& pos)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.AddImpulse(m_body_id, JPH::Vec3(impulse.x, impulse.y, impulse.z), JPH::Vec3(pos.x, pos.y, pos.z));
}

void JoltPhysicsBody::AddTorque(const Fvector& torque)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.AddTorque(m_body_id, JPH::Vec3(torque.x, torque.y, torque.z));
}

float JoltPhysicsBody::GetMass() const
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return 0.0f;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();

    // Get motion properties to access mass
    JPH::BodyLockRead lock(m_world->GetPhysicsSystem()->GetBodyLockInterface(), m_body_id);
    if (lock.Succeeded())
    {
        const JPH::Body& body = lock.GetBody();
        if (!body.IsStatic())
        {
            return 1.0f / body.GetMotionProperties()->GetInverseMass();
        }
    }

    return 0.0f;
}

void JoltPhysicsBody::SetMass(float mass)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    // Note: In Jolt, mass is typically set when creating the body
    // Changing it after creation requires recreating the body or using MotionProperties
    JPH::BodyLockWrite lock(m_world->GetPhysicsSystem()->GetBodyLockInterface(), m_body_id);
    if (lock.Succeeded())
    {
        JPH::Body& body = lock.GetBody();
        if (!body.IsStatic() && mass > 0.0f)
        {
            body.GetMotionProperties()->SetInverseMass(1.0f / mass);
        }
    }
}

void JoltPhysicsBody::GetMassCenter(Fvector& center) const
{
    if (!m_world || !m_world->GetPhysicsSystem())
    {
        center.set(0, 0, 0);
        return;
    }

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    JPH::Vec3 com = body_interface.GetCenterOfMassPosition(m_body_id);
    center.set(com.GetX(), com.GetY(), com.GetZ());
}

void JoltPhysicsBody::SetMassCenter(const Fvector& center)
{
    // Note: Jolt doesn't directly support changing the center of mass after body creation
    // This would require recreating the body with a different shape offset
    Msg("! JoltPhysicsBody::SetMassCenter: Not supported - requires body recreation");
}

void JoltPhysicsBody::SetFriction(float friction)
{
    m_friction = friction;

    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyLockWrite lock(m_world->GetPhysicsSystem()->GetBodyLockInterface(), m_body_id);
    if (lock.Succeeded())
    {
        JPH::Body& body = lock.GetBody();
        body.SetFriction(friction);
    }
}

void JoltPhysicsBody::SetRestitution(float restitution)
{
    m_restitution = restitution;

    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyLockWrite lock(m_world->GetPhysicsSystem()->GetBodyLockInterface(), m_body_id);
    if (lock.Succeeded())
    {
        JPH::Body& body = lock.GetBody();
        body.SetRestitution(restitution);
    }
}

float JoltPhysicsBody::GetFriction() const
{
    return m_friction;
}

float JoltPhysicsBody::GetRestitution() const
{
    return m_restitution;
}

void JoltPhysicsBody::Activate()
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.ActivateBody(m_body_id);
}

void JoltPhysicsBody::Deactivate()
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.DeactivateBody(m_body_id);
}

bool JoltPhysicsBody::IsActive() const
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return false;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    return body_interface.IsActive(m_body_id);
}

void JoltPhysicsBody::Enable()
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.AddBody(m_body_id, JPH::EActivation::Activate);
}

void JoltPhysicsBody::Disable()
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    body_interface.RemoveBody(m_body_id);
}

bool JoltPhysicsBody::IsEnabled() const
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return false;

    JPH::BodyInterface& body_interface = m_world->GetPhysicsSystem()->GetBodyInterface();
    return body_interface.IsAdded(m_body_id);
}

void JoltPhysicsBody::AddShape(IPhysicsShape* shape)
{
    if (!shape)
        return;

    m_shapes.push_back(shape);

    // Note: Jolt doesn't easily support adding shapes to existing bodies
    // This would typically require recreating the body with a compound shape
    Msg("! JoltPhysicsBody::AddShape: Dynamic shape addition requires body recreation");
}

void JoltPhysicsBody::RemoveShape(IPhysicsShape* shape)
{
    auto it = std::find(m_shapes.begin(), m_shapes.end(), shape);
    if (it != m_shapes.end())
    {
        m_shapes.erase(it);
    }
}

u32 JoltPhysicsBody::GetShapeCount() const
{
    return static_cast<u32>(m_shapes.size());
}

IPhysicsShape* JoltPhysicsBody::GetShape(u32 index)
{
    if (index >= m_shapes.size())
        return nullptr;

    return m_shapes[index];
}

void JoltPhysicsBody::SetUserData(void* data)
{
    m_user_data = data;
}

void* JoltPhysicsBody::GetUserData() const
{
    return m_user_data;
}

void JoltPhysicsBody::SetCollisionCallback(ICollisionCallback* callback)
{
    m_collision_callback = callback;
}

ICollisionCallback* JoltPhysicsBody::GetCollisionCallback() const
{
    return m_collision_callback;
}

void JoltPhysicsBody::SetGravityEnabled(bool enabled)
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return;

    JPH::BodyLockWrite lock(m_world->GetPhysicsSystem()->GetBodyLockInterface(), m_body_id);
    if (lock.Succeeded())
    {
        JPH::Body& body = lock.GetBody();
        if (!body.IsStatic())
        {
            body.GetMotionProperties()->SetGravityFactor(enabled ? 1.0f : 0.0f);
        }
    }
}

bool JoltPhysicsBody::IsGravityEnabled() const
{
    if (!m_world || !m_world->GetPhysicsSystem())
        return true;

    JPH::BodyLockRead lock(m_world->GetPhysicsSystem()->GetBodyLockInterface(), m_body_id);
    if (lock.Succeeded())
    {
        const JPH::Body& body = lock.GetBody();
        if (!body.IsStatic())
        {
            return body.GetMotionProperties()->GetGravityFactor() > 0.0f;
        }
    }

    return true;
}

#endif // XRPHYSICS_JOLT
