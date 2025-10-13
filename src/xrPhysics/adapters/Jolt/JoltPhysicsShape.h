#pragma once

#include "../IPhysicsAdapter.h"

#ifdef XRPHYSICS_JOLT

class JoltPhysicsShape : public IPhysicsShape
{
public:
    JoltPhysicsShape();
    virtual ~JoltPhysicsShape();

    PhysicsShapeType GetType() const override;
    void SetUserData(void* data) override;
    void* GetUserData() const override;

    void GetAABB(Fvector& min_out, Fvector& max_out) const override;
    float GetVolume() const override;

private:
    PhysicsShapeType m_type;
    void* m_user_data;
};

#endif // XRPHYSICS_JOLT
