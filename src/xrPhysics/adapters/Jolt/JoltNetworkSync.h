#pragma once

#ifdef XRPHYSICS_JOLT

#include "../IPhysicsAdapter.h"
#include "xrCommon/xr_vector.h"

class NET_Packet;
class IReader;
class IWriter;

namespace JoltIntegration
{
struct RagdollNetState
{
    xr_vector<u8> buffer;

    void Capture(IPhysicsRagdoll& ragdoll);
    void Apply(IPhysicsRagdoll& ragdoll) const;

    void Write(NET_Packet& packet) const;
    void Read(NET_Packet& packet);

    void Save(IWriter& writer) const;
    void Load(IReader& reader);
};
} // namespace JoltIntegration

#endif // XRPHYSICS_JOLT

