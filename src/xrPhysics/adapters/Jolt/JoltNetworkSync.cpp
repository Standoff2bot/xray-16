#ifdef XRPHYSICS_JOLT

#include "JoltNetworkSync.h"

#include "xrCore/NET_utils.h"
#include "xrCore/FS.h"
#include "xrCore/_vector3d.h"
#include "xrCore/_quaternion.h"
#include "xrCore/xrDebug.h"

namespace
{
constexpr u32 kRagdollHeaderSize = sizeof(u16);
constexpr u32 kBoneRecordSize =
    sizeof(u16) + sizeof(Fvector) * 3 + sizeof(Fquaternion);

u32 ComputeBufferCapacity(const IPhysicsRagdoll& ragdoll)
{
    const u16 count = ragdoll.GetElementCount();
    return kRagdollHeaderSize + static_cast<u32>(count) * kBoneRecordSize;
}
} // namespace

namespace JoltIntegration
{
void RagdollNetState::Capture(IPhysicsRagdoll& ragdoll)
{
    const u32 required_capacity = ComputeBufferCapacity(ragdoll);
    buffer.resize(required_capacity);

    u32 written = required_capacity;
    ragdoll.Serialize(buffer.data(), written);
    buffer.resize(written);
}

void RagdollNetState::Apply(IPhysicsRagdoll& ragdoll) const
{
    if (buffer.empty())
        return;

    ragdoll.Deserialize(buffer.data(), static_cast<u32>(buffer.size()));
}

void RagdollNetState::Write(NET_Packet& packet) const
{
    const u32 blob_size = static_cast<u32>(buffer.size());
    R_ASSERT2(blob_size < NET_PacketSizeLimit, "Ragdoll blob exceeds NET_Packet limit");
    packet.w_u32(blob_size);
    if (blob_size)
        packet.w(buffer.data(), blob_size);
}

void RagdollNetState::Read(NET_Packet& packet)
{
    const u32 blob_size = packet.r_u32();
    buffer.resize(blob_size);
    if (blob_size)
        packet.r(buffer.data(), blob_size);
}

void RagdollNetState::Save(IWriter& writer) const
{
    const u32 blob_size = static_cast<u32>(buffer.size());
    writer.w_u32(blob_size);
    if (blob_size)
        writer.w(buffer.data(), blob_size);
}

void RagdollNetState::Load(IReader& reader)
{
    const u32 blob_size = reader.r_u32();
    buffer.resize(blob_size);
    if (blob_size)
        reader.r(buffer.data(), blob_size);
}
} // namespace JoltIntegration

#endif // XRPHYSICS_JOLT

