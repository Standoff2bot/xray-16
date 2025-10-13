#pragma once

#include "../IPhysicsAdapter.h"

#ifdef XRPHYSICS_JOLT

class JoltPhysicsConstraint : public IPhysicsConstraint
{
public:
    JoltPhysicsConstraint();
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

private:
    PhysicsConstraintType m_type;
    IPhysicsBody* m_body1;
    IPhysicsBody* m_body2;
    bool m_enabled;
    bool m_broken;
};

#endif // XRPHYSICS_JOLT
