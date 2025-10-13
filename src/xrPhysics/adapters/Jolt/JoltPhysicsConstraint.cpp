#include "JoltPhysicsConstraint.h"

#ifdef XRPHYSICS_JOLT

JoltPhysicsConstraint::JoltPhysicsConstraint()
    : m_type(PhysicsConstraintType::Point)
    , m_body1(nullptr)
    , m_body2(nullptr)
    , m_enabled(true)
    , m_broken(false)
{
}

JoltPhysicsConstraint::~JoltPhysicsConstraint()
{
}

PhysicsConstraintType JoltPhysicsConstraint::GetType() const { return m_type; }

IPhysicsBody* JoltPhysicsConstraint::GetBody1() const { return m_body1; }
IPhysicsBody* JoltPhysicsConstraint::GetBody2() const { return m_body2; }

void JoltPhysicsConstraint::SetAnchor(const Fvector& anchor) {}
void JoltPhysicsConstraint::GetAnchor(Fvector& anchor) const { anchor.set(0, 0, 0); }

void JoltPhysicsConstraint::SetAxis(const Fvector& axis, u32 axis_index) {}
void JoltPhysicsConstraint::GetAxis(Fvector& axis, u32 axis_index) const { axis.set(0, 1, 0); }

void JoltPhysicsConstraint::SetLimits(float low, float high, u32 axis_index) {}
void JoltPhysicsConstraint::GetLimits(float& low, float& high, u32 axis_index) const { low = 0; high = 0; }

void JoltPhysicsConstraint::SetMotor(float target_velocity, float max_force, u32 axis_index) {}
void JoltPhysicsConstraint::EnableMotor(bool enabled, u32 axis_index) {}

void JoltPhysicsConstraint::SetSpringDamping(float spring, float damping, u32 axis_index) {}

void JoltPhysicsConstraint::SetBreakForce(float force, float torque) {}
bool JoltPhysicsConstraint::IsBroken() const { return m_broken; }

void JoltPhysicsConstraint::Enable() { m_enabled = true; }
void JoltPhysicsConstraint::Disable() { m_enabled = false; }
bool JoltPhysicsConstraint::IsEnabled() const { return m_enabled; }

#endif // XRPHYSICS_JOLT
