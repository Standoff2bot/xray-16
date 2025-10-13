#include "JoltPhysicsShape.h"

#ifdef XRPHYSICS_JOLT

// Jolt includes
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "xrCore/xrCore.h"

JoltPhysicsShape::JoltPhysicsShape(PhysicsShapeType type, const JPH::Ref<const JPH::Shape>& shape)
    : m_type(type)
    , m_user_data(nullptr)
    , m_jolt_shape(shape)
{
}

JoltPhysicsShape::~JoltPhysicsShape()
{
    // JPH::Ref handles cleanup automatically
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
    if (!m_jolt_shape)
    {
        min_out.set(0, 0, 0);
        max_out.set(0, 0, 0);
        return;
    }

    // Get the local bounds of the shape
    JPH::AABox bounds = m_jolt_shape->GetLocalBounds();

    // Convert to X-Ray coordinate system
    min_out.set(bounds.mMin.GetX(), bounds.mMin.GetY(), bounds.mMin.GetZ());
    max_out.set(bounds.mMax.GetX(), bounds.mMax.GetY(), bounds.mMax.GetZ());
}

float JoltPhysicsShape::GetVolume() const
{
    if (!m_jolt_shape)
    {
        return 0.0f;
    }

    // Get volume from Jolt shape
    return m_jolt_shape->GetVolume();
}

#endif // XRPHYSICS_JOLT
