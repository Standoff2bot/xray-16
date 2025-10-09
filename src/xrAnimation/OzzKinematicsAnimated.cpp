#include "stdafx.h"

#include "OzzKinematicsAnimated.h"

#include "OzzConversion.h"
#include "Layers/xrRender/AnimationKeyCalculate.h"

#include "xrEngine/device.h"
#include "xrCore/FS.h"
#include "xrCore/FS_impl.h"
#include "xrCore/Animation/Motion.hpp"
#include "xrCore/_std_extensions.h"

#include "ozz/animation/runtime/skeleton_utils.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/io/stream.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>

namespace fs = std::filesystem;
namespace Render = xray::render::RENDER_NAMESPACE;

namespace XRay
{
namespace Animation
{
namespace
{
constexpr Render::animation::channal_rule kDefaultChannelRules[MAX_CHANNELS] = {
    { Render::animation::lerp, Render::animation::lerp },
    { Render::animation::lerp, Render::animation::lerp },
    { Render::animation::lerp, Render::animation::add },
    { Render::animation::lerp, Render::animation::add },
};

bool EndsWithIgnoreCase(const xr_string& value, pcstr suffix)
{
    if (!suffix)
        return false;
    const size_t suffix_length = xr_strlen(suffix);
    if (value.size() < suffix_length)
        return false;
    return xr_stricmp(value.c_str() + value.size() - suffix_length, suffix) == 0;
}

std::string ReadOzzString(ozz::io::IArchive& archive)
{
    uint32_t length = 0;
    archive >> length;
    std::string result;
    if (length == 0)
        return result;

    result.resize(length);
    archive >> ozz::io::MakeArray(result.data(), length);
    return result;
}

void SkipOzzAnimationMetadata(ozz::io::IArchive& archive)
{
    ReadOzzString(archive);

    uint32_t flags = 0;
    uint16_t bone_or_part = 0;
    uint16_t motion_id = 0;
    float speed = 0.f;
    float power = 0.f;
    float accrue = 0.f;
    float falloff = 0.f;

    archive >> flags;
    archive >> bone_or_part;
    archive >> motion_id;
    archive >> speed;
    archive >> power;
    archive >> accrue;
    archive >> falloff;

    uint32_t mark_count = 0;
    archive >> mark_count;
    for (uint32_t mark_index = 0; mark_index < mark_count; ++mark_index)
    {
        ReadOzzString(archive);

        uint32_t interval_count = 0;
        archive >> interval_count;
        for (uint32_t interval_index = 0; interval_index < interval_count; ++interval_index)
        {
            float first = 0.f;
            float second = 0.f;
            archive >> first;
            archive >> second;
        }
    }
}

struct MotionBoneData
{
    u16 bone_id = BI_NONE;
    u8 flags = 0;
    u8 translation_format = 0;
    u32 rotation_crc = 0;
    u32 translation_crc = 0;
    xr_vector<CKeyQR> rotation_keys;
    xr_vector<CKeyQT8> translation_keys8;
    xr_vector<CKeyQT16> translation_keys16;
    Fvector translation_size{};
    Fvector translation_init{};
};

struct MotionMetadata
{
    xr_string name;
    uint32_t flags = 0;
    uint16_t bone_or_part = 0;
    uint16_t motion_id = 0;
    float speed = 1.f;
    float power = 1.f;
    float accrue = 0.f;
    float falloff = 0.f;
    uint32_t frame_count = 0;
    xr_vector<MotionBoneData> bone_motions;
};

MotionMetadata ReadMotionMetadataFromArchive(ozz::io::IArchive& archive)
{
    MotionMetadata metadata;

    std::string name = ReadOzzString(archive);
    metadata.name = name.c_str();

    archive >> metadata.flags;
    archive >> metadata.bone_or_part;
    archive >> metadata.motion_id;
    archive >> metadata.speed;
    archive >> metadata.power;
    archive >> metadata.accrue;
    archive >> metadata.falloff;

    uint32_t mark_count = 0;
    archive >> mark_count;

    for (uint32_t mark_index = 0; mark_index < mark_count; ++mark_index)
    {
        ReadOzzString(archive); // mark name

        uint32_t interval_count = 0;
        archive >> interval_count;
        for (uint32_t interval_index = 0; interval_index < interval_count; ++interval_index)
        {
            float start = 0.f;
            float end = 0.f;
            archive >> start;
            archive >> end;
        }
    }

    archive >> metadata.frame_count;

    uint32_t bone_motion_count = 0;
    archive >> bone_motion_count;
    metadata.bone_motions.resize(bone_motion_count);

    for (uint32_t bone_index = 0; bone_index < bone_motion_count; ++bone_index)
    {
        MotionBoneData& bone = metadata.bone_motions[bone_index];

        archive >> bone.bone_id;

        uint8_t flags = 0;
        archive >> flags;
        bone.flags = flags;

        uint8_t translation_format = 0;
        archive >> translation_format;
        bone.translation_format = translation_format;

        archive >> bone.rotation_crc;
        archive >> bone.translation_crc;

        uint32_t rotation_key_count = 0;
        archive >> rotation_key_count;
        bone.rotation_keys.clear();
        bone.rotation_keys.resize(rotation_key_count);
        for (uint32_t key_index = 0; key_index < rotation_key_count; ++key_index)
        {
            s16 x = 0;
            s16 y = 0;
            s16 z = 0;
            s16 w = 0;
            archive >> x;
            archive >> y;
            archive >> z;
            archive >> w;
            bone.rotation_keys[key_index].x = x;
            bone.rotation_keys[key_index].y = y;
            bone.rotation_keys[key_index].z = z;
            bone.rotation_keys[key_index].w = w;
        }

        uint32_t translation_key_count = 0;
        archive >> translation_key_count;

        switch (translation_format)
        {
        case 1:
            bone.translation_keys8.clear();
            bone.translation_keys8.resize(translation_key_count);
            bone.translation_keys16.clear();
            for (uint32_t key_index = 0; key_index < translation_key_count; ++key_index)
            {
                s8 x = 0;
                s8 y = 0;
                s8 z = 0;
                archive >> x;
                archive >> y;
                archive >> z;
                bone.translation_keys8[key_index].x1 = x;
                bone.translation_keys8[key_index].y1 = y;
                bone.translation_keys8[key_index].z1 = z;
            }
            break;
        case 2:
            bone.translation_keys16.clear();
            bone.translation_keys16.resize(translation_key_count);
            bone.translation_keys8.clear();
            for (uint32_t key_index = 0; key_index < translation_key_count; ++key_index)
            {
                s16 x = 0;
                s16 y = 0;
                s16 z = 0;
                archive >> x;
                archive >> y;
                archive >> z;
                bone.translation_keys16[key_index].x1 = x;
                bone.translation_keys16[key_index].y1 = y;
                bone.translation_keys16[key_index].z1 = z;
            }
            break;
        default:
            bone.translation_keys8.clear();
            bone.translation_keys16.clear();
            break;
        }

        archive >> bone.translation_size.x;
        archive >> bone.translation_size.y;
        archive >> bone.translation_size.z;
        archive >> bone.translation_init.x;
        archive >> bone.translation_init.y;
        archive >> bone.translation_init.z;
    }

    return metadata;
}

xr_string BuildMotionNameFromPath(const xr_string& path)
{
    xr_string name = path;
    const size_t slash = name.find_last_of("/\\");
    if (slash != xr_string::npos)
        name.erase(0, slash + 1);

    const size_t dot = name.find_last_of('.');
    if (dot != xr_string::npos)
        name.erase(dot);

    return name;
}

u32 CalculateFrameCountFromAnimation(const ozz::animation::Animation* animation)
{
    if (!animation)
        return 1;

    const float duration = animation->duration();
    if (!std::isfinite(duration) || duration <= 0.f)
        return 1;

    const double frames = static_cast<double>(duration) * static_cast<double>(SAMPLE_FPS);
    const double rounded = std::round(frames);
    if (!std::isfinite(rounded) || rounded <= 0.0)
        return 1;

    const double clamped = std::min(rounded, static_cast<double>(std::numeric_limits<u32>::max()));
    const u32 frame_count = static_cast<u32>(clamped);
    return frame_count > 0 ? frame_count : 1u;
}

u32 DetermineFrameCount(const MotionMetadata& metadata, const ozz::animation::Animation* animation)
{
    if (metadata.frame_count > 0)
        return metadata.frame_count;
    return CalculateFrameCountFromAnimation(animation);
}

u32 ComputeOrDefaultCrc(u32 provided_crc, const void* data, size_t byte_count)
{
    if (provided_crc != 0 || byte_count == 0 || data == nullptr)
        return provided_crc;
    return crc32(data, static_cast<u32>(byte_count));
}
} // namespace

OzzKinematicsAnimated::OzzKinematicsAnimated()
{
    InitializeChannelState();
    ResetPlaybackState();
}

OzzKinematicsAnimated::~OzzKinematicsAnimated()
{
    blendDestroyCallback = nullptr;
    updateTracksCallback = nullptr;

    for (ActiveBlendEntry& entry : activeBlends)
    {
        if (entry.blend)
        {
            entry.blend->Callback = nullptr;
            entry.blend->CallbackParam = nullptr;
        }
    }

    ClearActiveBlends(true);
}

void OzzKinematicsAnimated::OnSkeletonLoaded()
{
    EnsureMotionLibraryLoaded();
}

bool OzzKinematicsAnimated::InitializeFromOzz(pcstr skeletonPath, const xr_vector<xr_string>& motionRefs)
{
    if (!skeletonPath || !skeletonPath[0])
    {
        Msg("[OzzKinematicsAnimated] InitializeFromOzz received empty skeleton path");
        return false;
    }

    // Stash motion references before the skeleton triggers OnSkeletonLoaded().
    motionReferences = motionRefs;

    // Initialize skeleton using core
    if (!core.InitializeFromOzz(skeletonPath))
    {
        motionReferences.clear();
        return false;
    }

    InitializeSamplingState();
    ResetPlaybackState();

    InitializeChannelState();
    EnsureMotionLibraryLoaded();

    return true;
}

bool OzzKinematicsAnimated::InitializeFromOzzBuffer(ozz::span<const std::byte> skeletonData, const xr_vector<xr_string>& motionRefs)
{
    // Stash motion references before the skeleton triggers OnSkeletonLoaded().
    motionReferences = motionRefs;

    // Initialize skeleton using core
    if (!core.InitializeFromOzzBuffer(skeletonData))
    {
        motionReferences.clear();
        return false;
    }

    InitializeSamplingState();
    ResetPlaybackState();

    InitializeChannelState();
    EnsureMotionLibraryLoaded();

    return true;
}

void OzzKinematicsAnimated::SetEmbeddedAnimationData(const std::vector<std::uint8_t>& data)
{
    embeddedAnimationData = data;
    Msg("[OzzKinematicsAnimated::SetEmbeddedAnimationData] Received %zu bytes of embedded animation data",
        embeddedAnimationData.size());
}

void OzzKinematicsAnimated::ResetAnimationState()
{
    InitializeChannelState();
    motionReferences.clear();
    m_Motions.clear();  // Clear shared motion slots
    blendDestroyCallback = nullptr;
    updateTracksCallback = nullptr;
    animationApplied = false;
    embeddedAnimationData.clear();
    ResetPlaybackState();
}

void OzzKinematicsAnimated::InitializeChannelState()
{
    for (u32 channel = 0; channel < MAX_CHANNELS; ++channel)
    {
        channelRules[channel] = kDefaultChannelRules[channel];
        channelFactors[channel] = 1.f;
    }
}

void OzzKinematicsAnimated::EnsureMotionLibraryLoaded()
{
    // REFACTORED: Use shared motion container system
    if (!g_pOzzMotionsContainer)
    {
        Msg("[OzzKinematicsAnimated] ERROR: g_pOzzMotionsContainer not initialized!");
        return;
    }

    m_Motions.clear();

    if (!core.IsInitialized())
    {
        Msg("[OzzKinematicsAnimated] Skeleton not initialized, cannot load motion library");
        return;
    }

    // Compute skeleton fingerprint once
    SkeletonFingerprint skelFP = SkeletonFingerprint::Compute(core.Skeleton());

    // First, check if we have embedded animations
    if (!embeddedAnimationData.empty())
    {
        Msg("[OzzKinematicsAnimated] Loading embedded animations (%zu bytes)", embeddedAnimationData.size());

        // Create a unique key for embedded animations based on skeleton fingerprint
        xr_string embeddedKey = xr_string("<embedded_") +
                                xr_string(std::to_string(skelFP.hash).c_str()) +
                                xr_string(">");

        // Create motion slot for embedded animations
        m_Motions.push_back(SMotionsSlot());
        SMotionsSlot& slot = m_Motions.back();

        // Create load request with embedded data
        OzzMotionsContainer::LoadRequest request;
        request.key = shared_str(embeddedKey.c_str());
        request.skelFingerprint = skelFP;
        request.blocking = true;
        request.embeddedData = embeddedAnimationData;  // Pass the embedded data

        // Dock to global container (will load from memory)
        if (!slot.motions.Create(request, core.Skeleton()))
        {
            Msg("[OzzKinematicsAnimated] Failed to load embedded animations");
            m_Motions.pop_back();
        }
        else
        {
            // Build per-bone motion cache
            BuildBoneMotionCache(slot);
            Msg("[OzzKinematicsAnimated] Successfully loaded embedded animations");
        }

        // Clear embedded data after loading
        embeddedAnimationData.clear();
    }

#ifdef DEBUG
    Msg("[OzzKinematicsAnimated] building motion library with shared system (%zu refs)",
        static_cast<size_t>(motionReferences.size()));
    Msg("[OzzKinematicsAnimated]   skeleton fingerprint: hash=0x%08X, joints=%u",
        skelFP.hash, skelFP.jointCount);
#endif

    // Then load external motion references
    for (const auto& reference : motionReferences)
    {
#ifdef DEBUG
        Msg("[OzzKinematicsAnimated]   loading ref '%s'", reference.c_str());
#endif

        // Create motion slot
        m_Motions.push_back(SMotionsSlot());
        SMotionsSlot& slot = m_Motions.back();

        // Add .ozz extension if missing
        xr_string reference_path = reference;
        if (reference_path.find(".ozz") == xr_string::npos)
            reference_path += ".ozz";

        // Create load request with skeleton fingerprint
        OzzMotionsContainer::LoadRequest request;
        request.key = shared_str(reference_path.c_str());
        request.skelFingerprint = skelFP;
        request.blocking = true;  // Synchronous for now

        // Dock to global container (deduplicates automatically!)
        if (!slot.motions.Create(request, core.Skeleton()))
        {
            Msg("[OzzKinematicsAnimated] Failed to load motion ref '%s'", reference.c_str());
            m_Motions.pop_back();
            continue;
        }

        // Build per-bone motion cache
        BuildBoneMotionCache(slot);

#ifdef DEBUG
        Msg("[OzzKinematicsAnimated]   successfully docked '%s' (shared)", reference.c_str());
#endif
    }

}

void OzzKinematicsAnimated::BuildBoneMotionCache(SMotionsSlot& slot)
{
    if (!core.IsInitialized())
        return;

    const u32 bone_count = core.Skeleton().num_joints();
    slot.bone_motions.resize(bone_count);

    // Get MotionVec* for each bone from shared data
    auto joint_names = core.Skeleton().joint_names();  // returns ozz::span
    for (u32 bone_idx = 0; bone_idx < bone_count; ++bone_idx)
    {
        const char* bone_name = joint_names[bone_idx];

        // Get pointer to MotionVec for this bone
        slot.bone_motions[bone_idx] = slot.motions.GetBoneMotions(bone_name);
        //                             ^^^ Returns MotionVec* from shared container
        //                                 This is a POINTER to the shared vector!
    }

#ifdef DEBUG
    u32 valid_caches = 0;
    for (u32 i = 0; i < bone_count; ++i)
    {
        if (slot.bone_motions[i])
            valid_caches++;
    }
    Msg("[OzzKinematicsAnimated]   bone motion cache built: %u/%u valid", valid_caches, bone_count);
#endif
}

int OzzKinematicsAnimated::FindActiveBlendIndex(u16 partition, u8 channel) const
{
    const u16 resolvedPartition = (partition == BI_NONE) ? u16(0) : partition;
    for (size_t index = 0; index < activeBlends.size(); ++index)
    {
        const ActiveBlendEntry& entry = activeBlends[index];
        if (entry.partition == resolvedPartition && entry.channel == channel)
            return static_cast<int>(index);
    }
    return -1;
}

void OzzKinematicsAnimated::RemoveActiveBlend(size_t index, bool notifyDestroy)
{
    if (index >= activeBlends.size())
        return;

    ActiveBlendEntry& entry = activeBlends[index];

    if (entry.blend)
    {
        entry.blend->Callback = nullptr;
        entry.blend->CallbackParam = nullptr;
    }

    if (notifyDestroy && blendDestroyCallback && entry.blend)
        blendDestroyCallback->BlendDestroy(*entry.blend);

    activeBlends.erase(activeBlends.begin() + index);

    if (activeBlends.empty())
        ResetPlaybackState();
}

void OzzKinematicsAnimated::ClearActiveBlends(bool notifyDestroy)
{
    if (activeBlends.empty())
    {
        ResetPlaybackState();
        return;
    }

    for (ActiveBlendEntry& entry : activeBlends)
    {
        if (entry.blend)
        {
            entry.blend->Callback = nullptr;
            entry.blend->CallbackParam = nullptr;
        }
    }

    if (notifyDestroy && blendDestroyCallback)
    {
        for (ActiveBlendEntry& entry : activeBlends)
        {
            if (entry.blend)
                blendDestroyCallback->BlendDestroy(*entry.blend);
        }
    }

    activeBlends.clear();
    ResetPlaybackState();
}

void OzzKinematicsAnimated::ResetPlaybackState()
{
    controllerMotion.invalidate();
    activeAnimation.reset();
    animationLoaded = false;
    animationApplied = false;
    playbackTime = 0.f;
    playbackSpeed = 1.f;
    loopPlayback = true;
    samplingContext.Resize(0);
    ResetSamplingBuffers();
}




bool OzzKinematicsAnimated::LoadAnimationFromFile(const std::filesystem::path& path)
{
    if (!LoadAnimationClipFromFile(path))
        return false;

    controllerMotion.invalidate();
    animationApplied = false;
    CalculateBones_Invalidate();
    return true;
}

void OzzKinematicsAnimated::StopAnimation()
{
    ClearActiveBlends(true);
    ClearPose();
    CalculateBones_Invalidate();
}


bool OzzKinematicsAnimated::AdvanceAnimation(float dt)
{
    if (!core.IsInitialized())
        return false;

    if (!animationLoaded || !activeAnimation)
    {
        if (animationApplied)
        {
            ClearPose();
            animationApplied = false;
            return true;
        }
        return false;
    }

    const float duration = activeAnimation->duration();
    if (duration <= 0.f)
        return false;

    playbackTime += dt * playbackSpeed;

    if (loopPlayback && duration > 0.f)
    {
        while (playbackTime < 0.f)
            playbackTime += duration;
        while (playbackTime > duration)
            playbackTime -= duration;
    }
    else
    {
        if (playbackTime < 0.f)
            playbackTime = 0.f;
        if (playbackTime > duration)
            playbackTime = duration;
    }

    const float ratio = duration > 0.f ? playbackTime / duration : 0.f;

    if (sampledLocals.size() != static_cast<size_t>(core.Skeleton().num_soa_joints()))
        ResetSamplingBuffers();

    ozz::animation::SamplingJob job;
    job.animation = activeAnimation.get();
    job.context = &samplingContext;
    job.ratio = loopPlayback ? ratio : std::clamp(ratio, 0.f, 1.f);
    job.output = ozz::span<ozz::math::SoaTransform>(sampledLocals.data(), sampledLocals.size());

    if (!job.Run())
    {
        Msg("[OzzKinematicsAnimated] sampling job failed during animation update");
        return false;
    }

    if (!SetPoseLocals(ozz::span<const ozz::math::SoaTransform>(sampledLocals.data(), sampledLocals.size())))
        return false;

    animationApplied = true;
    return true;
}

bool OzzKinematicsAnimated::HasLoadedAnimation() const
{
    return animationLoaded && activeAnimation;
}

void OzzKinematicsAnimated::SetLooping(bool loop)
{
    loopPlayback = loop;
}

void OzzKinematicsAnimated::SetPlaybackSpeed(float speed)
{
    playbackSpeed = speed;
}

float OzzKinematicsAnimated::AnimationDuration() const
{
    return (animationLoaded && activeAnimation) ? activeAnimation->duration() : 0.f;
}

#ifdef DEBUG
ozz::span<const ozz::math::SoaTransform> OzzKinematicsAnimated::DebugSampledLocals() const
{
    if (sampledLocals.empty())
        return {};
    return ozz::span<const ozz::math::SoaTransform>(sampledLocals.data(), sampledLocals.size());
}
#endif

void OzzKinematicsAnimated::InitializeSamplingState()
{
    ResetSamplingBuffers();
}

void OzzKinematicsAnimated::ResetSamplingBuffers()
{
    if (!core.IsInitialized())
    {
        sampledLocals.clear();
        return;
    }

    const int soa_count = core.Skeleton().num_soa_joints();
    sampledLocals.resize(static_cast<size_t>(soa_count));
    for (ozz::math::SoaTransform& transform : sampledLocals)
        transform = ozz::math::SoaTransform::identity();
}

bool OzzKinematicsAnimated::LoadAnimationClip(const std::shared_ptr<ozz::animation::Animation>& animation)
{
    if (!core.IsInitialized() || !animation)
        return false;

    if (animation->num_tracks() != core.Skeleton().num_joints())
    {
        Msg("[OzzKinematicsAnimated] animation track mismatch (%d vs %d)",
            animation->num_tracks(), core.Skeleton().num_joints());
        return false;
    }

    activeAnimation = animation;
    samplingContext.Resize(animation->num_tracks());
    ResetSamplingBuffers();
    animationLoaded = true;
    playbackTime = 0.f;
    return true;
}

bool OzzKinematicsAnimated::LoadAnimationClipFromFile(const std::filesystem::path& path)
{
    if (!core.IsInitialized())
        return false;

    ozz::io::File file(path.u8string().c_str(), "rb");
    if (!file.opened())
    {
        Msg("[OzzKinematicsAnimated] failed to open animation %s", path.u8string().c_str());
        return false;
    }

    ozz::io::IArchive archive(&file);
    std::shared_ptr<ozz::animation::Animation> animation = std::make_shared<ozz::animation::Animation>();
    if (archive.TestTag<ozz::animation::Animation>())
    {
        archive >> *animation;
    }
    else
    {
        file.Seek(0, ozz::io::Stream::kSet);
        ozz::io::IArchive aggregate(&file);

        uint32_t animation_count = 0;
        aggregate >> animation_count;
        if (animation_count == 0)
        {
            Msg("[OzzKinematicsAnimated] file %s does not contain animations", path.u8string().c_str());
            return false;
        }

        aggregate >> *animation;
        SkipOzzAnimationMetadata(aggregate);

        for (uint32_t idx = 1; idx < animation_count; ++idx)
        {
            ozz::animation::Animation discarded;
            aggregate >> discarded;
            SkipOzzAnimationMetadata(aggregate);
        }
    }

    return LoadAnimationClip(animation);
}

// LoadOzzAnimationsFromFile and LoadOzzAnimationsFromArchive removed - using shared motion system

void OzzKinematicsAnimated::OnCalculateBones()
{
    UpdateTracks();
}

void OzzKinematicsAnimated::CalculateBones(BOOL bForceExact)
{
    core.CalculateTransforms(bForceExact);
}

#ifdef DEBUG
std::pair<LPCSTR, LPCSTR> OzzKinematicsAnimated::LL_MotionDefName_dbg(MotionID /*ID*/)
{
    static xr_string empty;
    return { empty.c_str(), empty.c_str() };
}

void OzzKinematicsAnimated::LL_DumpBlends_dbg()
{
    Msg("[OzzKinematicsAnimated] LL_DumpBlends_dbg not implemented");
}
#endif

u32 OzzKinematicsAnimated::LL_PartBlendsCount(u32 /*bone_part_id*/)
{
    return 0;
}

CBlend* OzzKinematicsAnimated::LL_PartBlend(u32 /*bone_part_id*/, u32 /*n*/)
{
    return nullptr;
}

void OzzKinematicsAnimated::LL_IterateBlends(IterateBlendsCallback& /*callback*/)
{
}

u16 OzzKinematicsAnimated::LL_MotionsSlotCount()
{
    return 0;
}

const shared_motions& OzzKinematicsAnimated::LL_MotionsSlot(u16 /*idx*/)
{
    static shared_motions dummy;
    return dummy;
}

CMotionDef* OzzKinematicsAnimated::LL_GetMotionDef(MotionID id)
{
    if (!id.valid())
        return nullptr;

    // Use shared motion system
    if (id.slot < m_Motions.size() && g_pOzzMotionsContainer)
    {
        OzzMotionsValue* value = g_pOzzMotionsContainer->Resolve(
            m_Motions[id.slot].motions.GetHandle()
        );

        if (value)
        {
            auto* record = value->FindMotion(id.idx);
            return record ? &record->definition : nullptr;
        }
    }

    return nullptr;
}

CMotion* OzzKinematicsAnimated::LL_GetRootMotion(MotionID id)
{
    if (!id.valid())
        return nullptr;

    const u16 root_bone = core.GetRootBone();
    const u16 resolved_root = (root_bone != BI_NONE) ? root_bone : u16(0);
    return LL_GetMotion(id, resolved_root);
}

CMotion* OzzKinematicsAnimated::LL_GetMotion(MotionID id, u16 bone_id)
{
    // Use bone-major cache from shared container
    if (!id.valid() || id.slot >= m_Motions.size())
        return nullptr;

    if (!core.IsInitialized() || bone_id == BI_NONE)
        return nullptr;

    const u32 joint_count = static_cast<u32>(core.Skeleton().num_joints());
    if (bone_id >= joint_count)
        return nullptr;

    // Get bone's motion vector from cache
    MotionVec* bone_motions = m_Motions[id.slot].bone_motions[bone_id];

    if (!bone_motions)
        return nullptr;

    // Index into the vector by motion ID
    if (id.idx >= bone_motions->size())
        return nullptr;

    return &bone_motions->at(id.idx);
}

void OzzKinematicsAnimated::LL_BuldBoneMatrixDequatize(const CBoneData* bd, u8 channel_mask, SKeyTable& keys)
{
    if (!core.IsInitialized() || !bd)
        return;

    const u16 self_id = bd->GetSelfID();
    if (self_id == BI_NONE)
        return;

    CKey base_keys[MAX_CHANNELS][MAX_BLENDED];

    if (activeBlends.empty())
        return;

    for (const ActiveBlendEntry& entry : activeBlends)
    {
        CBlend* blend = entry.blend.get();
        if (!blend)
            continue;

        const u8 channel = blend->channel;
        if (channel >= MAX_CHANNELS)
            continue;

        if ((channel_mask & (1 << channel)) == 0)
            continue;

        int& blend_count = keys.chanel_blend_conts[channel];
        if (blend_count >= static_cast<int>(MAX_BLENDED))
            continue;

        CMotion* motion = LL_GetMotion(blend->motionID, self_id);
        if (!motion)
            continue;

        CKey& destination = keys.keys[channel][blend_count];
        Render::key_identity(destination);
        Render::Dequantize(destination, *blend, *motion);

        CKey& base = base_keys[channel][blend_count];
        Render::key_identity(base);

        if (motion->_keysR.size())
            Render::QR2Quat(motion->_keysR[0], base.Q);

        if (motion->test_flag(flTKeyPresent))
        {
            if (motion->test_flag(flTKey16IsBit) && motion->_keysT16.size())
                Render::QT16_2T(motion->_keysT16[0], *motion, base.T);
            else if (motion->_keysT8.size())
                Render::QT8_2T(motion->_keysT8[0], *motion, base.T);
            else
                base.T.set(motion->_initT);
        }
        else
        {
            base.T.set(motion->_initT);
        }

        keys.blends[channel][blend_count] = blend;
        ++blend_count;
    }

    for (u16 channel = 0; channel < MAX_CHANNELS; ++channel)
    {
        if (channelRules[channel].extern_ == Render::animation::add)
            Render::keys_substruct(keys.keys[channel], base_keys[channel], keys.chanel_blend_conts[channel]);
    }
}

void OzzKinematicsAnimated::LL_BoneMatrixBuild(CBoneInstance& bi, const Fmatrix* parent, const SKeyTable& keys)
{
    CKey channel_keys[MAX_CHANNELS];
    Render::animation::channel_def channel_defs[MAX_CHANNELS];
    u16 channel_count = 0;

    for (u16 channel = 0; channel < MAX_CHANNELS; ++channel)
    {
        if (channel != 0 && keys.chanel_blend_conts[channel] == 0)
            continue;

        Render::animation::channel_def def{};
        def.rule = channelRules[channel];
        def.factor = channelFactors[channel];

        channel_defs[channel_count] = def;
        xray::render::RENDER_NAMESPACE::process_single_channel(channel_keys[channel_count], def, keys.keys[channel], keys.blends[channel],
            keys.chanel_blend_conts[channel]);
        ++channel_count;
    }

    if (channel_count == 0)
    {
        if (parent)
            bi.mTransform = *parent;
        else
            bi.mTransform.identity();
        return;
    }

    CKey mixed_key;
    xray::render::RENDER_NAMESPACE::MixChannels(mixed_key, channel_keys, channel_defs, channel_count);

    Fmatrix local_matrix;
    local_matrix.mk_xform(mixed_key.Q, mixed_key.T);

    if (parent)
        bi.mTransform.mul_43(*parent, local_matrix);
    else
        bi.mTransform = local_matrix;

#ifdef DEBUG
#ifndef _EDITOR
    if (!xray::render::RENDER_NAMESPACE::check_scale(local_matrix))
    {
        VERIFY(xray::render::RENDER_NAMESPACE::check_scale(bi.mTransform));
    }
    VERIFY(_valid(bi.mTransform));

    Fbox dbg_box;
    constexpr float kBoxExtent = 100000.f;
    dbg_box.set(-kBoxExtent, -kBoxExtent, -kBoxExtent, kBoxExtent, kBoxExtent, kBoxExtent);
    VERIFY2(dbg_box.contains(bi.mTransform.c),
        make_string("[OzzKinematicsAnimated] bone transform out of bounds: (%.3f, %.3f, %.3f)", bi.mTransform.c.x,
            bi.mTransform.c.y, bi.mTransform.c.z)
            .c_str());
#endif
#endif
}

IBlendDestroyCallback* OzzKinematicsAnimated::GetBlendDestroyCallback()
{
    return blendDestroyCallback;
}

void OzzKinematicsAnimated::SetBlendDestroyCallback(IBlendDestroyCallback* cb)
{
    blendDestroyCallback = cb;
}

void OzzKinematicsAnimated::SetUpdateTracksCalback(IUpdateTracksCallback* callback)
{
    updateTracksCallback = callback;
}

IUpdateTracksCallback* OzzKinematicsAnimated::GetUpdateTracksCalback()
{
    return updateTracksCallback;
}

MotionID OzzKinematicsAnimated::LL_MotionID(LPCSTR B)
{
    if (!B || !*B)
        return MotionID();

    if (!g_pOzzMotionsContainer || m_Motions.empty())
        return MotionID();

    const xr_string motion_name(B);

    // Search through all motion slots for a matching name
    for (u16 slot = 0; slot < m_Motions.size(); ++slot)
    {
        OzzMotionsValue* value = g_pOzzMotionsContainer->Resolve(
            m_Motions[slot].motions.GetHandle()
        );

        if (!value)
            continue;

        const auto lookup = value->lookup.find(motion_name);
        if (lookup == value->lookup.end())
            continue;

        return MotionID(slot, lookup->second);
    }

    return MotionID();
}

u16 OzzKinematicsAnimated::LL_PartID(LPCSTR /*B*/)
{
    return BI_NONE;
}

CBlend* OzzKinematicsAnimated::LL_PlayCycle(u16 partition, MotionID motion, BOOL bMixing, float blendAccrue,
    float blendFalloff, float Speed, BOOL noloop, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    if (!motion.valid())
        return nullptr;

    if (motion.slot >= m_Motions.size())
    {
        Msg("[OzzKinematicsAnimated] LL_PlayCycle received motion with invalid slot %u", motion.slot);
        return nullptr;
    }

    // Get motion record from shared system
    OzzMotionsValue::MotionRecord* record = nullptr;
    if (g_pOzzMotionsContainer)
    {
        OzzMotionsValue* value = g_pOzzMotionsContainer->Resolve(
            m_Motions[motion.slot].motions.GetHandle()
        );

        if (value)
            record = value->FindMotion(motion.idx);
    }

    if (!record)
    {
        Msg("[OzzKinematicsAnimated] LL_PlayCycle motion index %u not found", motion.idx);
        return nullptr;
    }

    if (!record->animation)
    {
        Msg("[OzzKinematicsAnimated] LL_PlayCycle motion '%s' is missing animation payload", record->name.c_str());
        return nullptr;
    }

    const u16 resolvedPartition = (partition == BI_NONE) ? u16(0) : partition;
    const bool mixing = !!bMixing;
    const bool stop_at_end = !!noloop;

    if (!mixing && !activeBlends.empty())
        ClearActiveBlends(true);

    const MotionID resolvedMotion(motion.slot, record->id.idx);

    if (!controllerMotion.valid() || controllerMotion != resolvedMotion)
    {
        if (!LoadAnimationClip(record->animation))
            return nullptr;

        controllerMotion = resolvedMotion;
        animationApplied = false;
        CalculateBones_Invalidate();
    }

    loopPlayback = !stop_at_end;

    const int existingIndex = FindActiveBlendIndex(resolvedPartition, channel);
    if (existingIndex >= 0)
        RemoveActiveBlend(static_cast<size_t>(existingIndex), true);

    activeBlends.emplace_back();
    ActiveBlendEntry& entry = activeBlends.back();
    entry.partition = resolvedPartition;
    entry.channel = channel;
    entry.recordIndex = resolvedMotion.idx;
    entry.motionId = resolvedMotion;

    entry.blend = xr_make_unique<CBlend>();
    if (!entry.blend)
    {
        activeBlends.pop_back();
        return nullptr;
    }

    CBlend& blend = *entry.blend;
    blend.set_accrue_state();
    blend.blendAmount = mixing ? EPS_S : 1.f;
    blend.blendAccrue = blendAccrue;
    blend.blendFalloff = blendFalloff;
    blend.blendPower = 1.f;

    const float def_speed = record->definition.Speed();
    float playback_speed_value = Speed;
    if (fis_zero(playback_speed_value))
        playback_speed_value = !fis_zero(def_speed) ? def_speed : 1.f;
    blend.speed = playback_speed_value;
    SetPlaybackSpeed(playback_speed_value);

    blend.motionID = resolvedMotion;
    blend.timeCurrent = 0.f;
    const float animationDuration = record->animation ? record->animation->duration() : 0.f;
    float motionLength = animationDuration;
    if (motionLength <= 0.f && record->frameCount > 0)
        motionLength = static_cast<float>(record->frameCount) * SAMPLE_SPF;
    if (motionLength <= 0.f)
        motionLength = SAMPLE_SPF;
    blend.timeTotal = motionLength;
    blend.bone_or_part = resolvedPartition;
    blend.stop_at_end = stop_at_end;
    blend.playing = true;
    blend.stop_at_end_callback = true;
    blend.Callback = Callback;
    blend.CallbackParam = CallbackParam;
    blend.channel = channel;
    blend.fall_at_end = blend.stop_at_end && (channel > 1);
    blend.dwFrame = Device.dwFrame ? Device.dwFrame - 1 : 0;

    animationApplied = false;

    return activeBlends.size() ? activeBlends.back().blend.get() : nullptr;
}

CBlend* OzzKinematicsAnimated::LL_PlayCycle(
    u16 partition, MotionID motion, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    CMotionDef* def = LL_GetMotionDef(motion);
    if (!def)
        return nullptr;

    return LL_PlayCycle(partition, motion, bMixIn, def->Accrue(), def->Falloff(), def->Speed(), def->StopAtEnd(), Callback,
        CallbackParam, channel);
}

void OzzKinematicsAnimated::LL_CloseCycle(u16 partition, u8 mask_channel)
{
    if (activeBlends.empty())
        return;

    const u16 resolvedPartition = (partition == BI_NONE) ? u16(0) : partition;
    size_t index = 0;
    while (index < activeBlends.size())
    {
        const ActiveBlendEntry& entry = activeBlends[index];
        const bool partitionMatch = (partition == BI_NONE) || (entry.partition == resolvedPartition);
        const bool channelMatch = (mask_channel & (1 << entry.channel)) != 0;
        if (partitionMatch && channelMatch)
        {
            RemoveActiveBlend(index, true);
            continue;
        }
        ++index;
    }
}

void OzzKinematicsAnimated::LL_SetChannelFactor(u16 channel, float factor)
{
    if (channel < MAX_CHANNELS)
        channelFactors[channel] = factor;
}

void OzzKinematicsAnimated::UpdateTracks()
{
    LL_UpdateTracks(Device.fTimeDelta, true, false);
}

void OzzKinematicsAnimated::LL_UpdateTracks(float dt, bool b_force, bool leave_blends)
{
    if (!activeBlends.empty())
    {
        const CBlend* primaryBlend = activeBlends.front().blend.get();
        if (primaryBlend)
        {
            loopPlayback = !primaryBlend->stop_at_end;
            SetPlaybackSpeed(primaryBlend->speed);
        }
    }

    const bool advanced = AdvanceAnimation(dt);

    if (!activeBlends.empty())
    {
        size_t index = 0;
        while (index < activeBlends.size())
        {
            ActiveBlendEntry& entry = activeBlends[index];
            CBlend& blend = *entry.blend;

            if (b_force || blend.dwFrame != Device.dwFrame)
            {
                blend.dwFrame = Device.dwFrame;
                const bool finished = blend.update(dt, blend.Callback);
                if (finished && !leave_blends)
                {
                    RemoveActiveBlend(index, true);
                    continue;
                }
            }

            ++index;
        }
    }

    if (updateTracksCallback)
        (*updateTracksCallback)(dt, *this);

    if (advanced && activeBlends.empty())
    {
        // When all blends are removed the controller state has already been reset.
    }
}

MotionID OzzKinematicsAnimated::ID_Cycle(LPCSTR N)
{
    MotionID id = ID_Cycle_Safe(N);
    if (!id.valid())
        return MotionID();

    return id;
}

MotionID OzzKinematicsAnimated::ID_Cycle_Safe(LPCSTR N)
{
    if (!N || !N[0])
    {
        Msg("[OzzKinematicsAnimated::ID_Cycle_Safe] Empty motion name provided");
        return MotionID();
    }

    MotionID id = LL_MotionID(N);
    if (!id.valid())
    {
        Msg("[OzzKinematicsAnimated::ID_Cycle_Safe] Motion '%s' NOT FOUND in library", N);

        // Log available motions for debugging
        if (g_pOzzMotionsContainer && !m_Motions.empty())
        {
            Msg("[OzzKinematicsAnimated] Available motion slots: %u", static_cast<u32>(m_Motions.size()));
            for (u16 slot = 0; slot < m_Motions.size(); ++slot)
            {
                OzzMotionsValue* value = g_pOzzMotionsContainer->Resolve(
                    m_Motions[slot].motions.GetHandle()
                );
                if (value)
                {
                    Msg("[OzzKinematicsAnimated]   Slot %u: %u motions", slot, value->GetMotionCount());
                    // List first few motion names for debugging
                    u16 count = std::min<u16>(5, value->GetMotionCount());
                    for (u16 i = 0; i < count; ++i)
                    {
                        auto* record = value->FindMotion(i);
                        if (record)
                            Msg("[OzzKinematicsAnimated]     - '%s'", record->name.c_str());
                    }
                    if (value->GetMotionCount() > count)
                        Msg("[OzzKinematicsAnimated]     ... and %u more", value->GetMotionCount() - count);
                }
            }
        }
    }
    else
    {
        Msg("[OzzKinematicsAnimated::ID_Cycle_Safe] Motion '%s' found: slot=%u, idx=%u",
            N, id.slot, id.idx);
    }

    return id;
}

MotionID OzzKinematicsAnimated::ID_Cycle(shared_str N)
{
    MotionID id = ID_Cycle_Safe(N);
    if (!id.valid())
        return MotionID();

    return id;
}

MotionID OzzKinematicsAnimated::ID_Cycle_Safe(shared_str N)
{
    if (!N || !N.c_str() || !N.c_str()[0])
        return MotionID();

    return LL_MotionID(N.c_str());
}

CBlend* OzzKinematicsAnimated::PlayCycle(LPCSTR N, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    MotionID id = ID_Cycle_Safe(N);
    if (!id.valid())
        return nullptr;

    return PlayCycle(id, bMixIn, Callback, CallbackParam, channel);
}

CBlend* OzzKinematicsAnimated::PlayCycle(MotionID M, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    CMotionDef* def = LL_GetMotionDef(M);
    if (!def)
        return nullptr;

    return LL_PlayCycle(def->bone_or_part, M, bMixIn, def->Accrue(), def->Falloff(), def->Speed(), def->StopAtEnd(),
        Callback, CallbackParam, channel);
}

CBlend* OzzKinematicsAnimated::PlayCycle(u16 partition, MotionID M, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam,
    u8 channel)
{
    return LL_PlayCycle(partition, M, bMixIn, Callback, CallbackParam, channel);
}

MotionID OzzKinematicsAnimated::ID_FX(LPCSTR /*N*/)
{
    return MotionID();
}

MotionID OzzKinematicsAnimated::ID_FX_Safe(LPCSTR /*N*/)
{
    return MotionID();
}

CBlend* OzzKinematicsAnimated::PlayFX(LPCSTR /*N*/, float /*power_scale*/)
{
    return nullptr;
}

CBlend* OzzKinematicsAnimated::PlayFX(MotionID /*M*/, float /*power_scale*/)
{
    return nullptr;
}

CBlend* OzzKinematicsAnimated::PlayFX_Safe(cpcstr /*N*/, float /*power_scale*/)
{
    return nullptr;
}

const CPartition& OzzKinematicsAnimated::partitions() const
{
    return defaultPartition;
}

float OzzKinematicsAnimated::get_animation_length(MotionID /*motion_ID*/)
{
    return AnimationDuration();
}


// Implementations for methods redeclared in IKinematicsAnimated
// These are already implemented in OzzKinematics, just need to be exposed here
void OzzKinematicsAnimated::LL_AddTransformToBone(KinematicsABT::additional_bone_transform& offset)
{
    OzzKinematics::LL_AddTransformToBone(offset);
}

void OzzKinematicsAnimated::LL_ClearAdditionalTransform(u16 bone_id)
{
    OzzKinematics::LL_ClearAdditionalTransform(bone_id);
}

} // namespace Animation
} // namespace XRay
