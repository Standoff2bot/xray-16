#include "JoltPhysicsConstraint.h"

#ifdef XRPHYSICS_JOLT

// Jolt includes
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>

#include "xrCore/xrCore.h"
#include "JoltPhysicsWorld.h"

JoltPhysicsConstraint::JoltPhysicsConstraint(JoltPhysicsWorld* world,
                                              JPH::TwoBodyConstraint* constraint,
                                              PhysicsConstraintType type,
                                              IPhysicsBody* body1,
                                              IPhysicsBody* body2)
    : m_world(world)
    , m_constraint(constraint)
    , m_type(type)
    , m_body1(body1)
    , m_body2(body2)
    , m_enabled(true)
    , m_break_force(FLT_MAX)
    , m_break_torque(FLT_MAX)
{
    m_anchor.set(0, 0, 0);
    for (int i = 0; i < 3; i++)
    {
        m_axis[i].set(0, 0, 0);
        m_limits_low[i] = 0.0f;
        m_limits_high[i] = 0.0f;
    }
}

JoltPhysicsConstraint::~JoltPhysicsConstraint()
{
    // Jolt constraints are reference counted and managed by the physics system
    // We don't need to manually delete them
}

PhysicsConstraintType JoltPhysicsConstraint::GetType() const
{
    return m_type;
}

IPhysicsBody* JoltPhysicsConstraint::GetBody1() const
{
    return m_body1;
}

IPhysicsBody* JoltPhysicsConstraint::GetBody2() const
{
    return m_body2;
}

void JoltPhysicsConstraint::SetAnchor(const Fvector& anchor)
{
    m_anchor = anchor;
    // Note: Jolt constraints have immutable anchor points set at creation
    Msg("! JoltPhysicsConstraint::SetAnchor: Anchor points are immutable after constraint creation");
}

void JoltPhysicsConstraint::GetAnchor(Fvector& anchor) const
{
    anchor = m_anchor;
}

void JoltPhysicsConstraint::SetAxis(const Fvector& axis, u32 axis_index)
{
    if (axis_index >= 3)
        return;

    m_axis[axis_index] = axis;
    // Note: Jolt constraint axes are immutable after creation
    Msg("! JoltPhysicsConstraint::SetAxis: Axes are immutable after constraint creation");
}

void JoltPhysicsConstraint::GetAxis(Fvector& axis, u32 axis_index) const
{
    if (axis_index >= 3)
    {
        axis.set(0, 1, 0);
        return;
    }

    axis = m_axis[axis_index];
}

void JoltPhysicsConstraint::SetLimits(float low, float high, u32 axis_index)
{
    if (axis_index >= 3)
        return;

    m_limits_low[axis_index] = low;
    m_limits_high[axis_index] = high;

    if (!m_constraint)
        return;

    // Apply limits based on constraint type
    switch (m_type)
    {
    case PhysicsConstraintType::Hinge:
    {
        JPH::HingeConstraint* hinge = static_cast<JPH::HingeConstraint*>(m_constraint);
        hinge->SetLimits(low, high);
        break;
    }
    case PhysicsConstraintType::Slider:
    {
        JPH::SliderConstraint* slider = static_cast<JPH::SliderConstraint*>(m_constraint);
        slider->SetLimits(low, high);
        break;
    }
    case PhysicsConstraintType::Generic:
    {
        JPH::SixDOFConstraint* sixdof = static_cast<JPH::SixDOFConstraint*>(m_constraint);
        // 6DOF limits need specific axis handling
        Msg("! JoltPhysicsConstraint::SetLimits: 6DOF constraints require specialized limit setup");
        break;
    }
    default:
        break;
    }
}

void JoltPhysicsConstraint::GetLimits(float& low, float& high, u32 axis_index) const
{
    if (axis_index >= 3)
    {
        low = 0.0f;
        high = 0.0f;
        return;
    }

    low = m_limits_low[axis_index];
    high = m_limits_high[axis_index];
}

void JoltPhysicsConstraint::SetMotor(float target_velocity, float max_force, u32 axis_index)
{
    if (!m_constraint)
        return;

    switch (m_type)
    {
    case PhysicsConstraintType::Hinge:
    {
        JPH::HingeConstraint* hinge = static_cast<JPH::HingeConstraint*>(m_constraint);
        JPH::MotorSettings motor_settings;
        motor_settings.mMinForceLimit = -max_force;
        motor_settings.mMaxForceLimit = max_force;
        hinge->SetMotorState(JPH::EMotorState::Velocity);
        hinge->SetTargetAngularVelocity(target_velocity);
        break;
    }
    case PhysicsConstraintType::Slider:
    {
        JPH::SliderConstraint* slider = static_cast<JPH::SliderConstraint*>(m_constraint);
        JPH::MotorSettings motor_settings;
        motor_settings.mMinForceLimit = -max_force;
        motor_settings.mMaxForceLimit = max_force;
        slider->SetMotorState(JPH::EMotorState::Velocity);
        slider->SetTargetVelocity(target_velocity);
        break;
    }
    default:
        Msg("! JoltPhysicsConstraint::SetMotor: Motor not supported for this constraint type");
        break;
    }
}

void JoltPhysicsConstraint::EnableMotor(bool enabled, u32 axis_index)
{
    if (!m_constraint)
        return;

    JPH::EMotorState motor_state = enabled ? JPH::EMotorState::Velocity : JPH::EMotorState::Off;

    switch (m_type)
    {
    case PhysicsConstraintType::Hinge:
    {
        JPH::HingeConstraint* hinge = static_cast<JPH::HingeConstraint*>(m_constraint);
        hinge->SetMotorState(motor_state);
        break;
    }
    case PhysicsConstraintType::Slider:
    {
        JPH::SliderConstraint* slider = static_cast<JPH::SliderConstraint*>(m_constraint);
        slider->SetMotorState(motor_state);
        break;
    }
    default:
        break;
    }
}

void JoltPhysicsConstraint::SetSpringDamping(float spring, float damping, u32 axis_index)
{
    if (!m_constraint)
        return;

    // Jolt uses spring settings for various constraints
    JPH::SpringSettings spring_settings;
    spring_settings.mFrequency = spring;
    spring_settings.mDamping = damping;

    switch (m_type)
    {
    case PhysicsConstraintType::Distance:
    {
        JPH::DistanceConstraint* distance = static_cast<JPH::DistanceConstraint*>(m_constraint);
        distance->SetLimitsSpringSettings(spring_settings);
        break;
    }
    case PhysicsConstraintType::Hinge:
    {
        JPH::HingeConstraint* hinge = static_cast<JPH::HingeConstraint*>(m_constraint);
        hinge->SetLimitsSpringSettings(spring_settings);
        break;
    }
    case PhysicsConstraintType::Slider:
    {
        JPH::SliderConstraint* slider = static_cast<JPH::SliderConstraint*>(m_constraint);
        slider->SetLimitsSpringSettings(spring_settings);
        break;
    }
    default:
        break;
    }
}

void JoltPhysicsConstraint::SetBreakForce(float force, float torque)
{
    m_break_force = force;
    m_break_torque = torque;

    // Note: Jolt doesn't have built-in constraint breaking
    // We would need to manually check forces in contact callbacks
    Msg("! JoltPhysicsConstraint::SetBreakForce: Constraint breaking requires manual force monitoring");
}

bool JoltPhysicsConstraint::IsBroken() const
{
    // Would need to implement force monitoring to detect breakage
    return false;
}

void JoltPhysicsConstraint::Enable()
{
    m_enabled = true;

    if (!m_constraint || !m_world || !m_world->GetPhysicsSystem())
        return;

    m_constraint->SetEnabled(true);
}

void JoltPhysicsConstraint::Disable()
{
    m_enabled = false;

    if (!m_constraint || !m_world || !m_world->GetPhysicsSystem())
        return;

    m_constraint->SetEnabled(false);
}

bool JoltPhysicsConstraint::IsEnabled() const
{
    if (!m_constraint)
        return m_enabled;

    return m_constraint->GetEnabled();
}

#endif // XRPHYSICS_JOLT
