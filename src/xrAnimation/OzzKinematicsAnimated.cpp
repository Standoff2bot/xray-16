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

bool PopulateMotionRecordFromMetadata(
    const MotionMetadata& metadata, OzzKinematicsAnimated::MotionRecord& record, u32 joint_count)
{
    if (joint_count == 0)
        return false;

    const u32 frame_count = std::max<u32>(1u, DetermineFrameCount(metadata, record.animation.get()));
    record.frameCount = frame_count;

    record.boneMotions.clear();
    record.boneMotions.resize(joint_count);

    for (const MotionBoneData& bone : metadata.bone_motions)
    {
        if (bone.bone_id == BI_NONE || bone.bone_id >= joint_count)
            continue;

        xr_unique_ptr<CMotion> motion = xr_make_unique<CMotion>();
        motion->set_flags(bone.flags);
        motion->set_count(frame_count);

        if (!bone.rotation_keys.empty())
        {
            const u32 crc = ComputeOrDefaultCrc(
                bone.rotation_crc, bone.rotation_keys.data(), bone.rotation_keys.size() * sizeof(CKeyQR));
            motion->_keysR.create(crc, static_cast<u32>(bone.rotation_keys.size()),
                const_cast<CKeyQR*>(bone.rotation_keys.data()));
        }

        switch (bone.translation_format)
        {
        case 1:
            if (!bone.translation_keys8.empty())
            {
                const u32 crc = ComputeOrDefaultCrc(bone.translation_crc, bone.translation_keys8.data(),
                    bone.translation_keys8.size() * sizeof(CKeyQT8));
                motion->_keysT8.create(crc, static_cast<u32>(bone.translation_keys8.size()),
                    const_cast<CKeyQT8*>(bone.translation_keys8.data()));
            }
            break;
        case 2:
            if (!bone.translation_keys16.empty())
            {
                const u32 crc = ComputeOrDefaultCrc(bone.translation_crc, bone.translation_keys16.data(),
                    bone.translation_keys16.size() * sizeof(CKeyQT16));
                motion->_keysT16.create(crc, static_cast<u32>(bone.translation_keys16.size()),
                    const_cast<CKeyQT16*>(bone.translation_keys16.data()));
            }
            break;
        default:
            break;
        }

        motion->_sizeT = bone.translation_size;
        motion->_initT = bone.translation_init;

        record.boneMotions[bone.bone_id] = std::move(motion);
    }

    for (u32 bone_idx = 0; bone_idx < joint_count; ++bone_idx)
    {
        if (record.boneMotions[bone_idx])
            continue;

        xr_unique_ptr<CMotion> fallback = xr_make_unique<CMotion>();
        fallback->set_flags(flRKeyAbsent);
        fallback->set_count(frame_count);

        CKeyQR identity{};
        identity.x = 0;
        identity.y = 0;
        identity.z = 0;
        identity.w = static_cast<s16>(KEY_Quant);

        const u32 crc = ComputeOrDefaultCrc(0, &identity, sizeof(identity));
        fallback->_keysR.create(crc, 1, const_cast<CKeyQR*>(&identity));
        fallback->_sizeT.set(0.f, 0.f, 0.f);
        fallback->_initT.set(0.f, 0.f, 0.f);

        record.boneMotions[bone_idx] = std::move(fallback);
    }

    return true;
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

bool OzzKinematicsAnimated::InitializeFromOzz(pcstr skeletonPath, const xr_vector<xr_string>& motionRefs)
{
    if (!skeletonPath || !skeletonPath[0])
    {
        Msg("[OzzKinematicsAnimated] InitializeFromOzz received empty skeleton path");
        return false;
    }

    // Initialize skeleton using core
    if (!core.InitializeFromOzz(skeletonPath))
        return false;

    InitializeSamplingState();
    ResetPlaybackState();

    // Store motion references and initialize channels
    motionReferences = motionRefs;
    motionLibraryBuilt = false;
    InitializeChannelState();

    return true;
}

bool OzzKinematicsAnimated::InitializeFromOzzBuffer(ozz::span<const std::byte> skeletonData, const xr_vector<xr_string>& motionRefs)
{
    // Initialize skeleton using core
    if (!core.InitializeFromOzzBuffer(skeletonData))
        return false;

    InitializeSamplingState();
    ResetPlaybackState();

    // Store motion references and initialize channels
    motionReferences = motionRefs;
    motionLibraryBuilt = false;
    InitializeChannelState();

    return true;
}

void OzzKinematicsAnimated::SetEmbeddedAnimationData(const std::vector<std::uint8_t>& data)
{
    embeddedAnimationData = data;
    motionLibraryBuilt = false;
}

void OzzKinematicsAnimated::ResetAnimationState()
{
    InitializeChannelState();
    motionReferences.clear();
    motionLibrary.Reset();
   motionLibraryBuilt = false;
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
    if (motionLibraryBuilt)
        return;

    motionLibrary.Reset();

    if (!embeddedAnimationData.empty())
    {
        ozz::io::MemoryStream embedded_stream;
        if (embedded_stream.Write(embeddedAnimationData.data(), embeddedAnimationData.size()))
        {
            embedded_stream.Seek(0, ozz::io::Stream::kSet);
            ozz::io::IArchive archive(&embedded_stream);
            if (!LoadOzzAnimationsFromArchive(archive, xr_string("<embedded>")))
            {
#ifdef DEBUG
                Msg("[OzzKinematicsAnimated] failed to load embedded animations");
#endif
            }
        }
        embeddedAnimationData.clear();
    }

#ifdef DEBUG
    Msg("[OzzKinematicsAnimated] building motion library (%zu refs)", static_cast<size_t>(motionReferences.size()));
#endif
    for (const auto& reference : motionReferences)
    {
#ifdef DEBUG
        Msg("[OzzKinematicsAnimated]   loading ref '%s'", reference.c_str());
#endif
        LoadMotionReference(reference);
    }

    motionLibraryBuilt = true;
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

xr_vector<xr_string> OzzKinematicsAnimated::LegacyMotionNames()
{
    EnsureMotionLibraryLoaded();

    xr_vector<xr_string> names;
    names.reserve(motionLibrary.records.size());
    for (const auto& motion : motionLibrary.records)
        names.push_back(motion.name);

    std::sort(names.begin(), names.end(),
        [](const xr_string& lhs, const xr_string& rhs)
        {
            return xr_stricmp(lhs.c_str(), rhs.c_str()) < 0;
        });
    return names;
}

bool OzzKinematicsAnimated::LoadMotionReference(const xr_string& reference)
{
    if (reference.empty())
        return false;

    xr_string candidate = reference;
    if (!EndsWithIgnoreCase(candidate, ".ozz"))
        candidate += ".ozz";

    return LoadOzzAnimationsFromFile(candidate);
}

bool OzzKinematicsAnimated::LoadLegacyMotion(const xr_string& motion_name)
{
    EnsureMotionLibraryLoaded();

    const MotionRecord* record = motionLibrary.Find(motion_name);
    if (!record)
    {
        Msg("[OzzKinematicsAnimated] Motion '%s' not available", motion_name.c_str());
        return false;
    }

    if (!record->animation)
    {
        Msg("[OzzKinematicsAnimated] Motion '%s' missing animation payload", motion_name.c_str());
        return false;
    }

    if (!LoadAnimationClip(record->animation))
    {
        Msg("[OzzKinematicsAnimated] Failed to bind motion '%s'", motion_name.c_str());
        return false;
    }

    controllerMotion = record->id;
    animationApplied = false;
    CalculateBones_Invalidate();
    return true;
}

bool OzzKinematicsAnimated::PlayLegacyMotion(const xr_string& motion_name)
{
    return LoadLegacyMotion(motion_name);
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

MotionID OzzKinematicsAnimated::ResolveLegacyMotionId(const xr_string& motion_name)
{
    if (motion_name.empty())
        return MotionID();

    EnsureMotionLibraryLoaded();

    const MotionRecord* record = motionLibrary.Find(motion_name);
    return record ? record->id : MotionID();
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

bool OzzKinematicsAnimated::LoadOzzAnimationsFromFile(const xr_string& relative_path)
{
    string_path resolved;
    FileStatus status(false, false);
    xr_string source_alias;

    constexpr pcstr kSearchAliases[] = { "$game_meshes$", "$levels$" };
    for (pcstr alias : kSearchAliases)
    {
        status = FS.exist(resolved, alias, relative_path.c_str());
        if (status)
        {
            source_alias = alias;
            break;
        }
    }

    if (!status)
        return false;

    std::vector<std::byte> payload;
    if (status.External)
    {
        std::error_code ec;
        fs::path absolute = fs::weakly_canonical(fs::path(resolved), ec);
        if (ec || !fs::exists(absolute, ec))
            return false;

        ozz::io::File file(absolute.string().c_str(), "rb");
        if (!file.opened())
        {
            Msg("[ozz] failed to open animation file '%s'", absolute.string().c_str());
            return false;
        }

        const int64_t length = file.Size();
        if (length < 0)
            return false;

        payload.resize(static_cast<size_t>(length));
        if (!payload.empty())
        {
            const size_t bytes_read = file.Read(payload.data(), payload.size());
            if (bytes_read != payload.size())
            {
                Msg("[ozz] failed to read %zu bytes from '%s' (got %zu)", payload.size(), absolute.string().c_str(),
                    bytes_read);
                return false;
            }
        }
    }
    else
    {
        pcstr alias_cstr = source_alias.c_str();
        IReader* reader = FS.r_open(alias_cstr, relative_path.c_str());
        if (!reader)
            return false;

        const size_t length = static_cast<size_t>(reader->length());
        payload.resize(length);
        if (length > 0)
        {
            if (!reader->pointer())
            {
                Msg("[ozz] reader for '%s' returned null pointer", relative_path.c_str());
                FS.r_close(reader);
                return false;
            }
            std::memcpy(payload.data(), reader->pointer(), length);
        }
        FS.r_close(reader);
    }

    ozz::io::MemoryStream stream;
    if (payload.empty())
        return false;

    if (!stream.Write(payload.data(), payload.size()))
        return false;
    stream.Seek(0, ozz::io::Stream::kSet);

    bool loaded_any = false;

    {
        ozz::io::IArchive archive(&stream);
        if (archive.TestTag<ozz::animation::Animation>())
        {
            auto animation = std::make_shared<ozz::animation::Animation>();
            archive >> *animation;

            if (!core.IsInitialized() || animation->num_tracks() != core.Skeleton().num_joints())
            {
                Msg("[ozz] Ignoring animation from '%s' due to track mismatch", relative_path.c_str());
                return false;
            }

            MotionMetadata metadata = ReadMotionMetadataFromArchive(archive);

            xr_string motion_name = metadata.name;
            if (motion_name.empty())
                motion_name = BuildMotionNameFromPath(relative_path);
            if (motion_name.empty())
            {
                motion_name = "motion_";
                motion_name += xr_string(std::to_string(motionLibrary.NextIndex()).c_str());
            }

            if (motionLibrary.Contains(motion_name))
            {
                Msg("[ozz] Duplicate motion '%s' ignored while loading '%s'", motion_name.c_str(), relative_path.c_str());
                return false;
            }

            MotionRecord record;
            record.name = motion_name;
            record.animation = std::move(animation);
            record.id.set(0, motionLibrary.NextIndex());

            CMotionDef def;
            def.bone_or_part = metadata.bone_or_part;
            def.motion = metadata.motion_id;
            const float speed = std::max(metadata.speed, 0.f);
            const float power = std::max(metadata.power, 0.f);
            const float accrue = std::max(metadata.accrue, 0.f);
            const float falloff = std::max(metadata.falloff, 0.f);
            def.speed = def.Quantize(speed);
            def.power = def.Quantize(power);
            def.accrue = def.Quantize(accrue);
            def.falloff = def.Quantize(falloff);
            def.flags = static_cast<u16>(metadata.flags);
            if (!(def.flags & esmFX) && def.falloff >= def.accrue && def.accrue > 0)
                def.falloff = static_cast<u16>(def.accrue - 1);
            def.marks.clear();

            record.definition = def;

            if (!PopulateMotionRecordFromMetadata(metadata, record, static_cast<u32>(core.Skeleton().num_joints())))
                return false;

            Msg("[OzzKinematicsAnimated]   added motion '%s' from '%s'", motion_name.c_str(), relative_path.c_str());
            motionLibrary.Add(std::move(record));
            loaded_any = true;
        }
    }

    if (loaded_any)
        return true;

    stream.Seek(0, ozz::io::Stream::kSet);
    ozz::io::IArchive aggregate(&stream);
    return LoadOzzAnimationsFromArchive(aggregate, relative_path);
}

bool OzzKinematicsAnimated::LoadOzzAnimationsFromArchive(ozz::io::IArchive& archive, const xr_string& source_label)
{
    uint32_t animation_count = 0;
    archive >> animation_count;
    if (animation_count == 0)
        return false;

    bool loaded_any = false;

    for (uint32_t idx = 0; idx < animation_count; ++idx)
    {
        std::shared_ptr<ozz::animation::Animation> animation = std::make_shared<ozz::animation::Animation>();
        archive >> *animation;

        MotionMetadata metadata = ReadMotionMetadataFromArchive(archive);
        xr_string motion_name = metadata.name;
        if (motion_name.empty())
        {
            motion_name = BuildMotionNameFromPath(source_label);
            if (animation_count > 1)
            {
                motion_name += "_";
                motion_name += xr_string(std::to_string(idx).c_str());
            }
        }

        if (!core.IsInitialized() || animation->num_tracks() != core.Skeleton().num_joints())
        {
            continue;
        }

        if (motionLibrary.Contains(motion_name))
        {
            continue;
        }

        MotionRecord record;
        record.name = motion_name;
        record.animation = std::move(animation);
        record.id.set(0, motionLibrary.NextIndex());

        CMotionDef def;
        def.bone_or_part = metadata.bone_or_part;
        def.motion = metadata.motion_id;
        const float speed = std::max(metadata.speed, 0.f);
        const float power = std::max(metadata.power, 0.f);
        const float accrue = std::max(metadata.accrue, 0.f);
        const float falloff = std::max(metadata.falloff, 0.f);
        def.speed = def.Quantize(speed);
        def.power = def.Quantize(power);
        def.accrue = def.Quantize(accrue);
        def.falloff = def.Quantize(falloff);
        def.flags = static_cast<u16>(metadata.flags);
        if (!(def.flags & esmFX) && def.falloff >= def.accrue && def.accrue > 0)
            def.falloff = static_cast<u16>(def.accrue - 1);
        def.marks.clear();

        record.definition = def;

        if (!PopulateMotionRecordFromMetadata(metadata, record, static_cast<u32>(core.Skeleton().num_joints())))
            continue;

        Msg("[OzzKinematicsAnimated]   added motion '%s' from '%s'", motion_name.c_str(), source_label.c_str());
        motionLibrary.Add(std::move(record));
        loaded_any = true;
    }

    return loaded_any;
}

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
    if (!id.valid() || id.slot != 0)
        return nullptr;

    const u16 index = id.idx;
    EnsureMotionLibraryLoaded();

    MotionRecord* record = motionLibrary.Find(index);
    return record ? &record->definition : nullptr;
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
    if (!id.valid() || id.slot != 0)
        return nullptr;

    if (!core.IsInitialized())
        return nullptr;

    if (bone_id == BI_NONE)
        return nullptr;

    const u32 joint_count = static_cast<u32>(core.Skeleton().num_joints());
    if (joint_count == 0 || bone_id >= joint_count)
        return nullptr;

    EnsureMotionLibraryLoaded();

    MotionRecord* record = motionLibrary.Find(id.idx);
    if (!record)
        return nullptr;

    if (record->boneMotions.size() != joint_count)
        return nullptr;

    const xr_unique_ptr<CMotion>& motion = record->boneMotions[bone_id];
    return motion.get();
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

    EnsureMotionLibraryLoaded();

    const MotionRecord* record = motionLibrary.Find(xr_string(B));
    return record ? record->id : MotionID();
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

    if (motion.slot != 0)
    {
        Msg("[OzzKinematicsAnimated] LL_PlayCycle received motion with unsupported slot %u", motion.slot);
        return nullptr;
    }

    EnsureMotionLibraryLoaded();

    const u16 motionIndex = motion.idx;
    MotionRecord* record = motionLibrary.Find(motionIndex);
    if (!record)
    {
        Msg("[OzzKinematicsAnimated] LL_PlayCycle motion index %u out of range", motionIndex);
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

    if (!controllerMotion.valid() || controllerMotion != record->id)
    {
        if (!LoadAnimationClip(record->animation))
            return nullptr;

        controllerMotion = record->id;
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
    entry.recordIndex = motionIndex;
    entry.motionId = record->id;

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

    blend.motionID = record->id;
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
    R_ASSERT3(id.valid(), "[OzzKinematicsAnimated] can't find cycle: ", N ? N : "<null>");
    return id;
}

MotionID OzzKinematicsAnimated::ID_Cycle_Safe(LPCSTR N)
{
    if (!N || !N[0])
        return MotionID();

    return ResolveLegacyMotionId(xr_string(N));
}

MotionID OzzKinematicsAnimated::ID_Cycle(shared_str N)
{
    MotionID id = ID_Cycle_Safe(N);
    R_ASSERT3(id.valid(), "[OzzKinematicsAnimated] can't find cycle: ", N.c_str());
    return id;
}

MotionID OzzKinematicsAnimated::ID_Cycle_Safe(shared_str N)
{
    if (!N || !N.c_str() || !N.c_str()[0])
        return MotionID();

    return ResolveLegacyMotionId(xr_string(N.c_str()));
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

// MotionLibrary implementation
void OzzKinematicsAnimated::MotionLibrary::Reset()
{
    records.clear();
    lookup.clear();
}

bool OzzKinematicsAnimated::MotionLibrary::Contains(const xr_string& name) const
{
    return lookup.find(name) != lookup.end();
}

OzzKinematicsAnimated::MotionRecord* OzzKinematicsAnimated::MotionLibrary::Find(u16 index)
{
    if (index >= records.size())
        return nullptr;
    return &records[index];
}

const OzzKinematicsAnimated::MotionRecord* OzzKinematicsAnimated::MotionLibrary::Find(u16 index) const
{
    if (index >= records.size())
        return nullptr;
    return &records[index];
}

OzzKinematicsAnimated::MotionRecord* OzzKinematicsAnimated::MotionLibrary::Find(const xr_string& name)
{
    auto it = lookup.find(name);
    if (it == lookup.end())
        return nullptr;
    return Find(it->second);
}

const OzzKinematicsAnimated::MotionRecord* OzzKinematicsAnimated::MotionLibrary::Find(const xr_string& name) const
{
    auto it = lookup.find(name);
    if (it == lookup.end())
        return nullptr;
    return Find(it->second);
}

u16 OzzKinematicsAnimated::MotionLibrary::NextIndex() const
{
    return static_cast<u16>(records.size());
}

void OzzKinematicsAnimated::MotionLibrary::Add(MotionRecord&& record)
{
    const u16 index = static_cast<u16>(records.size());
    lookup[record.name] = index;
    records.push_back(std::move(record));
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
