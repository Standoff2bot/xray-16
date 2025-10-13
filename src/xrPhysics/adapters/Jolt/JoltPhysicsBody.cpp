#include "JoltPhysicsBody.h"

#ifdef XRPHYSICS_JOLT

#include "xrCore/xrCore.h"

JoltPhysicsBody::JoltPhysicsBody()
    : m_body(nullptr)
    , m_user_data(nullptr)
    , m_collision_callback(nullptr)
{
}

JoltPhysicsBody::~JoltPhysicsBody()
{
}

// Stub implementations
PhysicsBodyType JoltPhysicsBody::GetType() const { return PhysicsBodyType::Dynamic; }
void JoltPhysicsBody::SetType(PhysicsBodyType type) {}

void JoltPhysicsBody::GetPosition(Fvector& pos) const { pos.set(0, 0, 0); }
void JoltPhysicsBody::SetPosition(const Fvector& pos) {}
void JoltPhysicsBody::GetRotation(Fmatrix& rot) const { rot.identity(); }
void JoltPhysicsBody::SetRotation(const Fmatrix& rot) {}
void JoltPhysicsBody::GetTransform(Fmatrix& transform) const { transform.identity(); }
void JoltPhysicsBody::SetTransform(const Fmatrix& transform) {}

void JoltPhysicsBody::GetLinearVelocity(Fvector& vel) const { vel.set(0, 0, 0); }
void JoltPhysicsBody::SetLinearVelocity(const Fvector& vel) {}
void JoltPhysicsBody::GetAngularVelocity(Fvector& vel) const { vel.set(0, 0, 0); }
void JoltPhysicsBody::SetAngularVelocity(const Fvector& vel) {}

void JoltPhysicsBody::AddForce(const Fvector& force) {}
void JoltPhysicsBody::AddForceAtPosition(const Fvector& force, const Fvector& pos) {}
void JoltPhysicsBody::AddImpulse(const Fvector& impulse) {}
void JoltPhysicsBody::AddImpulseAtPosition(const Fvector& impulse, const Fvector& pos) {}
void JoltPhysicsBody::AddTorque(const Fvector& torque) {}

float JoltPhysicsBody::GetMass() const { return 0.0f; }
void JoltPhysicsBody::SetMass(float mass) {}
void JoltPhysicsBody::GetMassCenter(Fvector& center) const { center.set(0, 0, 0); }
void JoltPhysicsBody::SetMassCenter(const Fvector& center) {}

void JoltPhysicsBody::SetFriction(float friction) {}
void JoltPhysicsBody::SetRestitution(float restitution) {}
float JoltPhysicsBody::GetFriction() const { return 0.5f; }
float JoltPhysicsBody::GetRestitution() const { return 0.0f; }

void JoltPhysicsBody::Activate() {}
void JoltPhysicsBody::Deactivate() {}
bool JoltPhysicsBody::IsActive() const { return false; }
void JoltPhysicsBody::Enable() {}
void JoltPhysicsBody::Disable() {}
bool JoltPhysicsBody::IsEnabled() const { return false; }

void JoltPhysicsBody::AddShape(IPhysicsShape* shape) {}
void JoltPhysicsBody::RemoveShape(IPhysicsShape* shape) {}
u32 JoltPhysicsBody::GetShapeCount() const { return 0; }
IPhysicsShape* JoltPhysicsBody::GetShape(u32 index) { return nullptr; }

void JoltPhysicsBody::SetUserData(void* data) { m_user_data = data; }
void* JoltPhysicsBody::GetUserData() const { return m_user_data; }

void JoltPhysicsBody::SetCollisionCallback(ICollisionCallback* callback) { m_collision_callback = callback; }
ICollisionCallback* JoltPhysicsBody::GetCollisionCallback() const { return m_collision_callback; }

void JoltPhysicsBody::SetGravityEnabled(bool enabled) {}
bool JoltPhysicsBody::IsGravityEnabled() const { return true; }

#endif // XRPHYSICS_JOLT
