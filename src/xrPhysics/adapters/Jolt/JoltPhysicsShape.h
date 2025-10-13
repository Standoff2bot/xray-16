#pragma once

#include "../IPhysicsAdapter.h"

#ifdef XRPHYSICS_JOLT

// Forward declarations for Jolt types
namespace JPH
{
    class Shape;
    class RefConst;
    template<class T> class Ref;
}

class JoltPhysicsShape : public IPhysicsShape
{
public:
    JoltPhysicsShape(PhysicsShapeType type, const JPH::Ref<const JPH::Shape>& shape);
    virtual ~JoltPhysicsShape();

    PhysicsShapeType GetType() const override;
    void SetUserData(void* data) override;
    void* GetUserData() const override;

    void GetAABB(Fvector& min_out, Fvector& max_out) const override;
    float GetVolume() const override;

    // Internal accessors
    const JPH::Ref<const JPH::Shape>& GetJoltShape() const { return m_jolt_shape; }

private:
    PhysicsShapeType m_type;
    void* m_user_data;
    JPH::Ref<const JPH::Shape> m_jolt_shape;
};

#endif // XRPHYSICS_JOLT
