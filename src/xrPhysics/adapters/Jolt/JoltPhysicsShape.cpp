#include "JoltPhysicsShape.h"

#ifdef XRPHYSICS_JOLT

JoltPhysicsShape::JoltPhysicsShape()
    : m_type(PhysicsShapeType::Box)
    , m_user_data(nullptr)
{
}

JoltPhysicsShape::~JoltPhysicsShape()
{
}

PhysicsShapeType JoltPhysicsShape::GetType() const
{
    return m_type;
}

void JoltPhysicsShape::SetUserData(void* data)
{
    m_user_data = data;
}

void* JoltPhysicsShape::GetUserData() const
{
    return m_user_data;
}

void JoltPhysicsShape::GetAABB(Fvector& min_out, Fvector& max_out) const
{
    min_out.set(-1, -1, -1);
    max_out.set(1, 1, 1);
}

float JoltPhysicsShape::GetVolume() const
{
    return 1.0f;
}

#endif // XRPHYSICS_JOLT
