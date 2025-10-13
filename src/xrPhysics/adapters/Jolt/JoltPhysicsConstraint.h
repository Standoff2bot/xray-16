#pragma once

#include "../IPhysicsAdapter.h"

#ifdef XRPHYSICS_JOLT

namespace JPH
{
    class Constraint;
    class TwoBodyConstraint;
}

class JoltPhysicsWorld;

class JoltPhysicsConstraint : public IPhysicsConstraint
{
public:
    JoltPhysicsConstraint(JoltPhysicsWorld* world,
                          JPH::TwoBodyConstraint* constraint,
                          PhysicsConstraintType type,
                          IPhysicsBody* body1,
                          IPhysicsBody* body2);
    virtual ~JoltPhysicsConstraint();

    PhysicsConstraintType GetType() const override;

    IPhysicsBody* GetBody1() const override;
    IPhysicsBody* GetBody2() const override;

    void SetAnchor(const Fvector& anchor) override;
    void GetAnchor(Fvector& anchor) const override;

    void SetAxis(const Fvector& axis, u32 axis_index = 0) override;
    void GetAxis(Fvector& axis, u32 axis_index = 0) const override;

    void SetLimits(float low, float high, u32 axis_index = 0) override;
    void GetLimits(float& low, float& high, u32 axis_index = 0) const override;

    void SetMotor(float target_velocity, float max_force, u32 axis_index = 0) override;
    void EnableMotor(bool enabled, u32 axis_index = 0) override;

    void SetSpringDamping(float spring, float damping, u32 axis_index = 0) override;

    void SetBreakForce(float force, float torque) override;
    bool IsBroken() const override;

    void Enable() override;
    void Disable() override;
    bool IsEnabled() const override;

    // Internal accessors
    JPH::TwoBodyConstraint* GetJoltConstraint() const { return m_constraint; }

private:
    JoltPhysicsWorld* m_world;
    JPH::TwoBodyConstraint* m_constraint;
    PhysicsConstraintType m_type;
    IPhysicsBody* m_body1;
    IPhysicsBody* m_body2;
    bool m_enabled;
    float m_break_force;
    float m_break_torque;

    // Cached parameters (Jolt constraints are immutable after creation for many properties)
    Fvector m_anchor;
    Fvector m_axis[3];
    float m_limits_low[3];
    float m_limits_high[3];
};

#endif // XRPHYSICS_JOLT
