#pragma once

#include "../IPhysicsAdapter.h"

#ifdef XRPHYSICS_JOLT

namespace JPH
{
    class Body;
    class BodyID;
}

class JoltPhysicsBody : public IPhysicsBody
{
public:
    JoltPhysicsBody();
    virtual ~JoltPhysicsBody();

    // IPhysicsBody implementation - stubs for now
    PhysicsBodyType GetType() const override;
    void SetType(PhysicsBodyType type) override;

    void GetPosition(Fvector& pos) const override;
    void SetPosition(const Fvector& pos) override;
    void GetRotation(Fmatrix& rot) const override;
    void SetRotation(const Fmatrix& rot) override;
    void GetTransform(Fmatrix& transform) const override;
    void SetTransform(const Fmatrix& transform) override;

    void GetLinearVelocity(Fvector& vel) const override;
    void SetLinearVelocity(const Fvector& vel) override;
    void GetAngularVelocity(Fvector& vel) const override;
    void SetAngularVelocity(const Fvector& vel) override;

    void AddForce(const Fvector& force) override;
    void AddForceAtPosition(const Fvector& force, const Fvector& pos) override;
    void AddImpulse(const Fvector& impulse) override;
    void AddImpulseAtPosition(const Fvector& impulse, const Fvector& pos) override;
    void AddTorque(const Fvector& torque) override;

    float GetMass() const override;
    void SetMass(float mass) override;
    void GetMassCenter(Fvector& center) const override;
    void SetMassCenter(const Fvector& center) override;

    void SetFriction(float friction) override;
    void SetRestitution(float restitution) override;
    float GetFriction() const override;
    float GetRestitution() const override;

    void Activate() override;
    void Deactivate() override;
    bool IsActive() const override;
    void Enable() override;
    void Disable() override;
    bool IsEnabled() const override;

    void AddShape(IPhysicsShape* shape) override;
    void RemoveShape(IPhysicsShape* shape) override;
    u32 GetShapeCount() const override;
    IPhysicsShape* GetShape(u32 index) override;

    void SetUserData(void* data) override;
    void* GetUserData() const override;

    void SetCollisionCallback(ICollisionCallback* callback) override;
    ICollisionCallback* GetCollisionCallback() const override;

    void SetGravityEnabled(bool enabled) override;
    bool IsGravityEnabled() const override;

private:
    JPH::Body* m_body;
    void* m_user_data;
    ICollisionCallback* m_collision_callback;
};

#endif // XRPHYSICS_JOLT
