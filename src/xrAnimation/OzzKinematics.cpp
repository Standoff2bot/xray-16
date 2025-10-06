#include "stdafx.h"

#include "OzzKinematics.h"

#include "OzzConversion.h"

#include "xrEngine/device.h"
#include "xrCore/FS.h"
#include "xrCore/FS_impl.h"
#include "xrCore/Animation/Motion.hpp"
#include "xrCore/_std_extensions.h"
#include "xrMaterialSystem/GameMtlLib.h"

#include "ozz/animation/runtime/skeleton_utils.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/io/stream.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/span.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace XRay
{
namespace Animation
{
namespace
{
struct BoneInstanceStub
{
    CBoneInstance instance;

    BoneInstanceStub()
    {
        instance.construct();
    }
};

CBoneInstance& StubBoneInstance()
{
    static BoneInstanceStub stub;
    return stub.instance;
}

CBoneData& StubBoneData()
{
    static CBoneData stub{ 0 };
    return stub;
}

Fmatrix& StubMatrix()
{
    static Fmatrix stub = Fidentity;
    return stub;
}

Fobb& StubObb()
{
    static Fobb stub;
    return stub;
}

Fobb BuildFallbackObbFromShape(const SBoneShape& shape)
{
    constexpr float kMinHalfExtent = 0.001f;
    Fobb result;
    result.invalidate();

    switch (shape.type)
    {
    case SBoneShape::stBox:
    {
        result.m_rotate = shape.box.m_rotate;
        result.m_translate = shape.box.m_translate;
        result.m_halfsize = shape.box.m_halfsize;
        break;
    }
    case SBoneShape::stSphere:
    {
        result.identity();
        result.m_translate = shape.sphere.P;
        const float radius = std::max(shape.sphere.R, kMinHalfExtent);
        result.m_halfsize.set(radius, radius, radius);
        break;
    }
    case SBoneShape::stCylinder:
    {
        Fvector axis = shape.cylinder.m_direction;
        if (axis.square_magnitude() <= EPS_L)
            axis.set(0.f, 1.f, 0.f);
        axis.normalize_safe();

        Fvector up;
        up.set(0.f, 1.f, 0.f);
        if (std::fabs(axis.dotproduct(up)) > 0.99f)
            up.set(1.f, 0.f, 0.f);

        Fvector right;
        right.crossproduct(up, axis);
        if (right.square_magnitude() <= EPS_L)
        {
            right.set(1.f, 0.f, 0.f);
            right.crossproduct(up, axis);
        }
        right.normalize_safe();

        Fvector forward;
        forward.crossproduct(axis, right);
        forward.normalize_safe();

        result.m_rotate.i = right;
        result.m_rotate.j = axis;
        result.m_rotate.k = forward;
        result.m_translate = shape.cylinder.m_center;

        const float radius = std::max(shape.cylinder.m_radius, kMinHalfExtent);
        const float half_height = std::max(shape.cylinder.m_height * 0.5f, kMinHalfExtent);
        result.m_halfsize.set(radius, half_height, radius);
        break;
    }
    default:
        result.identity();
        result.m_halfsize.set(kMinHalfExtent, kMinHalfExtent, kMinHalfExtent);
        break;
    }

    result.m_halfsize.x = std::max(result.m_halfsize.x, kMinHalfExtent);
    result.m_halfsize.y = std::max(result.m_halfsize.y, kMinHalfExtent);
    result.m_halfsize.z = std::max(result.m_halfsize.z, kMinHalfExtent);
    return result;
}

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
    const MotionMetadata& metadata, OzzKinematics::MotionRecord& record, u32 joint_count)
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

OzzKinematics::OzzKinematics()
    : userData(nullptr), rootBone(BI_NONE), visibleMask(0), updateCallback(nullptr), updateCallbackParam(nullptr), lastUpdateTime(0),
      visibilityCounter(0), animationController(xr_make_unique<OzzAnimationController>()), animationControllerReady(false)
{
    cachedBox.invalidate();
}

OzzKinematics::~OzzKinematics() = default;

bool OzzKinematics::InitializeFromOzz(pcstr skeletonPath, const xr_vector<xr_string>& motionRefs)
{
    if (!skeletonPath || !skeletonPath[0])
    {
        Msg("[OzzKinematics] InitializeFromOzz received empty skeleton path");
        return false;
    }

    ResetRuntimeState();
    motionReferences = motionRefs;
    motionLibraryBuilt = false;

    ozz::io::File file(skeletonPath, "rb");
    if (!file.opened())
    {
        Msg("[OzzKinematics] Failed to open skeleton file: %s", skeletonPath);
        return false;
    }

    return LoadSkeletonFromStream(&file, skeletonPath);
}

bool OzzKinematics::InitializeFromOzzBuffer(ozz::span<const std::byte> skeletonData, const xr_vector<xr_string>& motionRefs)
{
    ResetRuntimeState();
    motionReferences = motionRefs;
    motionLibraryBuilt = false;

    if (skeletonData.empty())
    {
        Msg("[OzzKinematics] InitializeFromOzzBuffer received empty skeleton buffer");
        return false;
    }

    ozz::io::MemoryStream memoryStream;
    if (!memoryStream.Write(skeletonData.data(), skeletonData.size()))
    {
        Msg("[OzzKinematics] Failed to copy skeleton buffer into memory stream");
        ResetRuntimeState();
        return false;
    }

    memoryStream.Seek(0, ozz::io::Stream::kSet);

    if (!LoadSkeletonFromStream(&memoryStream, "memory buffer"))
    {
        ResetRuntimeState();
        return false;
    }

    return true;
}

bool OzzKinematics::LoadUserDataFromBuffer(const std::vector<std::uint8_t>& buffer)
{
    userDataOwner.reset();
    userData = nullptr;

    if (buffer.empty())
        return true;

    IReader reader(const_cast<std::uint8_t*>(buffer.data()), buffer.size());

    pcstr config_path = "";
    if (FS.path_exist("$game_config$"))
    {
        const FS_Path* path = FS.get_path("$game_config$");
        if (path)
            config_path = path->m_Path;
    }

    userDataOwner = xr_make_unique<CInifile>(&reader, config_path);
    userData = userDataOwner.get();
    return userData != nullptr;
}

void OzzKinematics::NotImplemented(pcstr function_name) const
{
    Msg("[OzzKinematics] %s is not implemented yet", function_name);
}

void OzzKinematics::ResetRuntimeState()
{
    boneInstances.clear();
    boneBoxes.clear();
    bones.clear();
    boneStorage.clear();
    boneMapByName.clear();
    boneMapByPtr.clear();
    cachedTransformsPreCallbacks.clear();
    boneOffsets.clear();
    sampledLocals.clear();
    modelTransforms.clear();
    samplingContext.Resize(0);
    userDataOwner.reset();
    userData = nullptr;
    rootBone = BI_NONE;
    visibleMask = 0;
    cachedBox.invalidate();
    lastUpdateTime = 0;
    visibilityCounter = 0;
    skeleton = ozz::animation::Skeleton();

    ResetAnimationState();
    initialized = false;
}

void OzzKinematics::ResetAnimationState()
{
    motionReferences.clear();
    motionLibrary.Reset();
    motionLibraryBuilt = false;
    blendDestroyCallback = nullptr;
    updateTracksCallback = nullptr;
    animationApplied = false;
    animationControllerReady = false;
    embeddedAnimationData.clear();
}

void OzzKinematics::EnsureMotionLibraryLoaded()
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
                Msg("[OzzKinematics] failed to load embedded animations");
#endif
            }
        }
        embeddedAnimationData.clear();
    }

#ifdef DEBUG
    Msg("[OzzKinematics] building motion library (%zu refs)", static_cast<size_t>(motionReferences.size()));
#endif
    for (const auto& reference : motionReferences)
    {
#ifdef DEBUG
        Msg("[OzzKinematics]   loading ref '%s'", reference.c_str());
#endif
        LoadMotionReference(reference);
    }

    motionLibraryBuilt = true;
}

int OzzKinematics::FindActiveBlendIndex(u16 partition, u8 channel) const
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

void OzzKinematics::RemoveActiveBlend(size_t index, bool notifyDestroy)
{
    if (index >= activeBlends.size())
        return;

    ActiveBlendEntry& entry = activeBlends[index];
    if (notifyDestroy && blendDestroyCallback && entry.blend)
        blendDestroyCallback->BlendDestroy(*entry.blend);

    activeBlends.erase(activeBlends.begin() + index);

    if (activeBlends.empty())
        ResetAnimationControllerState();
}

void OzzKinematics::ClearActiveBlends(bool notifyDestroy)
{
    if (activeBlends.empty())
    {
        ResetAnimationControllerState();
        return;
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
    ResetAnimationControllerState();
}

void OzzKinematics::ResetAnimationControllerState()
{
    controllerMotion.invalidate();
    if (animationController)
        animationController->ClearAnimation();
    animationApplied = false;
}

xr_vector<xr_string> OzzKinematics::LegacyMotionNames()
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

bool OzzKinematics::LoadMotionReference(const xr_string& reference)
{
    if (reference.empty())
        return false;

    xr_string candidate = reference;
    if (!EndsWithIgnoreCase(candidate, ".ozz"))
        candidate += ".ozz";

    return LoadOzzAnimationsFromFile(candidate);
}

bool OzzKinematics::LoadLegacyMotion(const xr_string& motion_name)
{
    if (!animationControllerReady || !animationController)
        return false;

    EnsureMotionLibraryLoaded();

    const MotionRecord* record = motionLibrary.Find(motion_name);
    if (!record)
    {
        Msg("[OzzKinematics] Motion '%s' not available", motion_name.c_str());
        return false;
    }

    if (!animationController->LoadAnimation(record->animation))
    {
        Msg("[OzzKinematics] Failed to bind motion '%s'", motion_name.c_str());
        return false;
    }

    controllerMotion = record->id;

    animationApplied = false;
    CalculateBones_Invalidate();
    return true;
}

bool OzzKinematics::PlayLegacyMotion(const xr_string& motion_name)
{
    return LoadLegacyMotion(motion_name);
}

bool OzzKinematics::LoadAnimationFromFile(const std::filesystem::path& path)
{
    if (!animationControllerReady || !animationController)
        return false;

    if (!animationController->LoadAnimation(path))
        return false;

    controllerMotion.invalidate();
    animationApplied = false;
    CalculateBones_Invalidate();
    return true;
}

void OzzKinematics::StopAnimation()
{
    ClearActiveBlends(true);
    ClearPose();
    CalculateBones_Invalidate();
}

void OzzKinematics::SetEmbeddedAnimationData(const std::vector<std::uint8_t>& data)
{
    embeddedAnimationData = data;
    motionLibraryBuilt = false;
}

MotionID OzzKinematics::ResolveLegacyMotionId(const xr_string& motion_name)
{
    if (motion_name.empty())
        return MotionID();

    EnsureMotionLibraryLoaded();

    const MotionRecord* record = motionLibrary.Find(motion_name);
    return record ? record->id : MotionID();
}

bool OzzKinematics::AdvanceAnimation(float dt)
{
    if (!animationControllerReady || !animationController)
        return false;

    if (!animationController->HasAnimation())
    {
        if (animationApplied)
        {
            ClearPose();
            animationApplied = false;
            return true;
        }
        return false;
    }

    if (!animationController->Update(dt))
        return false;

    const auto locals = animationController->SampledLocals();
    if (locals.empty())
        return false;

    if (!SetPoseLocals(locals))
        return false;

    animationApplied = true;
    return true;
}

bool OzzKinematics::HasLoadedAnimation() const
{
    return animationControllerReady && animationController && animationController->HasAnimation();
}

bool OzzKinematics::LoadOzzAnimationsFromFile(const xr_string& relative_path)
{
    string_path resolved;
    FileStatus status = FS.exist(resolved, "$game_meshes$", relative_path.c_str());
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
            file.Read(payload.data(), payload.size());
    }
    else
    {
        IReader* reader = FS.r_open("$game_meshes$", relative_path.c_str());
        if (!reader)
            return false;

        const size_t length = static_cast<size_t>(reader->length());
        payload.resize(length);
        if (length > 0)
            std::memcpy(payload.data(), reader->pointer(), length);
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

            if (!initialized || animation->num_tracks() != skeleton.num_joints())
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

            if (!PopulateMotionRecordFromMetadata(metadata, record, static_cast<u32>(skeleton.num_joints())))
                return false;

            Msg("[OzzKinematics]   added motion '%s' from '%s'", motion_name.c_str(), relative_path.c_str());
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

bool OzzKinematics::LoadOzzAnimationsFromArchive(ozz::io::IArchive& archive, const xr_string& source_label)
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

        if (!initialized || animation->num_tracks() != skeleton.num_joints())
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

        if (!PopulateMotionRecordFromMetadata(metadata, record, static_cast<u32>(skeleton.num_joints())))
            continue;

        Msg("[OzzKinematics]   added motion '%s' from '%s'", motion_name.c_str(), source_label.c_str());
        motionLibrary.Add(std::move(record));
        loaded_any = true;
    }

    return loaded_any;
}

bool OzzKinematics::LoadSkeletonFromStream(ozz::io::Stream* stream, pcstr debug_source)
{
    if (!stream)
    {
        Msg("[OzzKinematics] LoadSkeletonFromStream received null stream for %s", debug_source ? debug_source : "<unknown>");
        return false;
    }

    ozz::io::IArchive archive(stream);
    if (!archive.TestTag<ozz::animation::Skeleton>())
    {
        Msg("[OzzKinematics] Stream does not contain a skeleton: %s", debug_source ? debug_source : "<unknown>");
        return false;
    }

    archive >> skeleton;

    if (!FinalizeSkeletonInitialization(debug_source))
    {
        ResetRuntimeState();
        return false;
    }

    return true;
}

bool OzzKinematics::FinalizeSkeletonInitialization(pcstr debug_source)
{
    const int joint_count = skeleton.num_joints();
    if (joint_count <= 0)
    {
        Msg("[OzzKinematics] Skeleton contains no joints: %s", debug_source ? debug_source : "<unknown>");
        return false;
    }

    boneInstances.resize(joint_count);
    for (CBoneInstance& instance : boneInstances)
        instance.construct();

    boneBoxes.resize(joint_count);
    for (Fobb& box : boneBoxes)
        box.invalidate();

    bones.assign(joint_count, nullptr);
    boneStorage.clear();
    boneStorage.reserve(joint_count);

    modelTransforms.clear();
    modelTransforms.shrink_to_fit();
    modelTransforms.resize(static_cast<size_t>(joint_count));

    cachedTransformsPreCallbacks.clear();
    cachedTransformsPreCallbacks.resize(static_cast<size_t>(joint_count));

    boneMapByName.clear();
    boneMapByPtr.clear();
    const auto joint_names = skeleton.joint_names();
    boneMapByName.reserve(static_cast<size_t>(joint_count));
    boneMapByPtr.reserve(static_cast<size_t>(joint_count));
    for (int joint = 0; joint < joint_count; ++joint)
    {
        const size_t joint_index = static_cast<size_t>(joint);
        const char* joint_name = (joint_index < joint_names.size() && joint_names[joint_index]) ? joint_names[joint_index] : "";
        shared_str shared_name(joint_name ? joint_name : "");
        const u16 bone_id = static_cast<u16>(joint);
        boneMapByName.emplace_back(shared_name, bone_id);
        boneMapByPtr.emplace_back(shared_name, bone_id);
    }

    std::sort(boneMapByName.begin(), boneMapByName.end(),
        [](const auto& lhs, const auto& rhs)
        {
            return xr_strcmp(lhs.first.c_str(), rhs.first.c_str()) < 0;
        });

    std::sort(boneMapByPtr.begin(), boneMapByPtr.end(),
        [](const auto& lhs, const auto& rhs)
        {
            return lhs.first._get() < rhs.first._get();
        });

    rootBone = joint_count > 0 ? 0 : BI_NONE;

    if (joint_count >= 64)
        visibleMask = u64(-1);
    else if (joint_count == 0)
        visibleMask = 0;
    else
        visibleMask = (u64(1) << joint_count) - 1;

    if (!BuildBoneMetadata())
        return false;

    boneOffsets.clear();
    sampledLocals.clear();
    samplingContext.Resize(0);

    cachedBox.invalidate();
    lastUpdateTime = 0;
    visibilityCounter = 0;

    initialized = true;

    if (!animationController)
        animationController = xr_make_unique<OzzAnimationController>();

    animationApplied = false;
    animationControllerReady = animationController && animationController->Initialize(skeleton);
    if (!animationControllerReady)
    {
        Msg("[OzzKinematics] Failed to initialize animation controller for skeleton");
        return false;
    }

    return true;
}

using XRay::Animation::ConvertOzzMatrixToXRay;
bool OzzKinematics::BuildBoneMetadata()
{
    const int joint_count = skeleton.num_joints();
    if (joint_count <= 0)
        return false;

    const ozz::span<const int16_t> parents = skeleton.joint_parents();
    const auto joint_names = skeleton.joint_names();

    boneStorage.clear();
    boneStorage.reserve(static_cast<size_t>(joint_count));

    for (int joint = 0; joint < joint_count; ++joint)
    {
        const u16 bone_id = static_cast<u16>(joint);
        auto bone = xr_make_unique<CBoneData>(bone_id);

    const int16_t parent_index = (static_cast<size_t>(joint) < parents.size()) ? parents[joint] : static_cast<int16_t>(-1);
        bone->SetParentID(parent_index >= 0 ? static_cast<u16>(parent_index) : BI_NONE);

    const char* joint_name = (static_cast<size_t>(joint) < joint_names.size()) ? joint_names[joint] : nullptr;
        bone->name = joint_name && joint_name[0] ? shared_str(joint_name) : shared_str();

        bone->shape.Reset();
        bone->obb.invalidate();
        bone->game_mtl_name = shared_str();
        bone->game_mtl_idx = 0;
        bone->mass = 0.f;
        bone->center_of_mass.set(0.f, 0.f, 0.f);
        bone->IK_data.Reset();

        const ozz::math::Transform rest_pose = ozz::animation::GetJointLocalRestPose(skeleton, joint);
        const ozz::math::Float4x4 rest_matrix = ozz::math::Float4x4::FromAffine(rest_pose);
        bone->bind_transform = ConvertOzzMatrixToXRay(rest_matrix);
        bone->m2b_transform.identity();

        boneStorage.push_back(std::move(bone));
    }

    for (size_t idx = 0; idx < boneStorage.size(); ++idx)
        bones[idx] = boneStorage[idx].get();

    // Build children relationships.
    for (int joint = 0; joint < joint_count; ++joint)
    {
        const int16_t parent_index = (static_cast<size_t>(joint) < parents.size()) ? parents[joint] : static_cast<int16_t>(-1);
        if (parent_index >= 0 && parent_index < joint_count)
            bones[parent_index]->children.push_back(bones[joint]);
    }

    if (!bones.empty() && rootBone != BI_NONE && rootBone < bones.size())
        bones[rootBone]->CalculateM2B(Fidentity);

    return true;
}

bool OzzKinematics::ApplyExtendedBoneMetadata(const ExtendedBoneMetadataCollection& metadata)
{
    if (metadata.empty())
        return true;

    if (metadata.size() != bones.size())
    {
        Msg("[OzzKinematics] Bone metadata count mismatch (metadata=%zu, bones=%zu)", metadata.size(), bones.size());
        return false;
    }

    for (size_t idx = 0; idx < metadata.size(); ++idx)
    {
        CBoneData* bone = bones[idx];
        if (!bone)
            continue;

        const ExtendedBoneMetadata& source = metadata[idx];

        bone->shape = source.shape;
        if (_valid(source.obb))
        {
            bone->obb = source.obb;
        }
        else
        {
            bone->obb = BuildFallbackObbFromShape(source.shape);
        }
        bone->IK_data = source.joint;
        bone->mass = source.mass;
        bone->center_of_mass = source.center_of_mass;
        bone->rest_length = source.rest_length;
        bone->dominant_axis = source.dominant_axis;
        bone->local_aabb_min = source.local_aabb_min;
        bone->local_aabb_max = source.local_aabb_max;
        bone->inverse_global_transform = source.inverse_global_transform;
        bone->inertia_tensor = source.inertia_tensor;
        bone->volume = source.volume;
        bone->collision_layers.assign(source.collision_layers);
        bone->ground_contact_candidate = source.ground_contact_candidate;
        bone->weapon_anchor_candidate = source.weapon_anchor_candidate;

        bone->game_mtl_name = shared_str();
        bone->game_mtl_idx = 0;
        if (!source.game_material.empty())
        {
            const char* material_name = source.game_material.c_str();
            bone->game_mtl_name = shared_str(material_name);

            if (GMLib.GetMaterial(material_name))
                bone->game_mtl_idx = GMLib.GetMaterialIdx(material_name);
            else
                Msg("[OzzKinematics] Unknown material '%s' for bone '%s'", material_name, bone->name.c_str());
        }
    }

    return true;
}

bool OzzKinematics::IsBoneVisible(size_t index) const
{
    if (index >= boneInstances.size())
        return false;
    if (index >= 64)
        return true;
    const u64 mask = u64(1) << index;
    return (visibleMask & mask) != 0;
}

void OzzKinematics::ApplyAdditionalBoneTransforms(u16 bone_id, Fmatrix& transform) const
{
    for (const auto& offset : boneOffsets)
    {
        if (offset.m_bone_id != bone_id)
            continue;

        const Fvector original_position = transform.c;
        transform.mulB_43(offset.m_transform);
        transform.c.add(original_position, offset.m_transform.c);
    }
}

bool OzzKinematics::SetPoseLocals(ozz::span<const ozz::math::SoaTransform> locals)
{
    if (!initialized)
        return false;

    if (locals.empty())
    {
        sampledLocals.clear();
        CalculateBones_Invalidate();
        return true;
    }

    if (static_cast<int>(locals.size()) != skeleton.num_soa_joints())
    {
        Msg("[OzzKinematics] SetPoseLocals received %zu soa joints, expected %d", locals.size(), skeleton.num_soa_joints());
        return false;
    }

    sampledLocals.assign(locals.begin(), locals.end());
    CalculateBones_Invalidate();
    return true;
}

void OzzKinematics::ClearPose()
{
    if (!initialized)
        return;

    sampledLocals.clear();
    CalculateBones_Invalidate();
}

const ozz::animation::Skeleton& OzzKinematics::Skeleton() const
{
    return skeleton;
}

ozz::animation::SamplingJob::Context& OzzKinematics::SamplingContext()
{
    return samplingContext;
}

void OzzKinematics::BuildSkinningPalette(xr_vector<Fmatrix>& out_matrices, bool render_space) const
{
    if (!initialized)
    {
        out_matrices.clear();
        return;
    }

    const size_t expected = static_cast<size_t>(skeleton.num_joints());
    if (expected == 0)
    {
        out_matrices.clear();
        return;
    }

    const size_t instance_count = boneInstances.size();
    const size_t model_count = modelTransforms.size();
    const size_t available = std::min(instance_count, std::min(model_count, expected));

    out_matrices.resize(expected);

    if (available != expected)
    {
        Msg("[OzzKinematics] BuildSkinningPalette requested before bone buffers ready (instances=%zu, model=%zu, expected=%zu)",
            instance_count, model_count, expected);

        for (size_t idx = 0; idx < expected; ++idx)
            out_matrices[idx] = Fidentity;
        return;
    }

    if (render_space)
    {
        for (size_t idx = 0; idx < expected; ++idx)
            out_matrices[idx] = boneInstances[idx].mRenderTransform;
    }
    else
    {
        for (size_t idx = 0; idx < expected; ++idx)
            out_matrices[idx] = boneInstances[idx].mTransform;
    }
}

void OzzKinematics::Bone_Calculate(CBoneData* /*bd*/, Fmatrix* /*parent*/)
{
    CalculateBones(TRUE);
}

void OzzKinematics::OnCalculateBones()
{
    CalculateBones(TRUE);
}

void OzzKinematics::Bone_GetAnimPos(Fmatrix& pos, u16 id, u8 /*channel_mask*/, bool ignore_callbacks)
{
    if (id >= boneInstances.size())
    {
        pos.identity();
        return;
    }

    CalculateBones(TRUE);

    if (ignore_callbacks && id < cachedTransformsPreCallbacks.size())
    {
        pos = cachedTransformsPreCallbacks[id];
    }
    else
    {
        pos = LL_GetTransform(id);
    }
}

bool OzzKinematics::PickBone(const Fmatrix& /*parent_xform*/, pick_result& /*r*/, float /*dist*/, const Fvector& /*start*/, const Fvector& /*dir*/, u16 /*bone_id*/)
{
    return false;
}

void OzzKinematics::EnumBoneVertices(SEnumVerticesCallback& /*C*/, u16 /*bone_id*/)
{
}

u16 OzzKinematics::LL_BoneID(LPCSTR B)
{
    if (!B)
        return BI_NONE;

    const auto it = std::lower_bound(boneMapByName.begin(), boneMapByName.end(), B,
        [](const accel::value_type& entry, LPCSTR value)
        {
            return xr_strcmp(entry.first.c_str(), value) < 0;
        });

    if (it == boneMapByName.end())
        return BI_NONE;

    return xr_strcmp(it->first.c_str(), B) == 0 ? it->second : BI_NONE;
}

u16 OzzKinematics::LL_BoneID(const shared_str& B)
{
    if (!B._get())
        return BI_NONE;

    const auto it = std::lower_bound(boneMapByPtr.begin(), boneMapByPtr.end(), B,
        [](const accel::value_type& entry, const shared_str& value)
        {
            return entry.first._get() < value._get();
        });

    if (it == boneMapByPtr.end())
        return BI_NONE;

    return it->first._get() == B._get() ? it->second : BI_NONE;
}

LPCSTR OzzKinematics::LL_BoneName_dbg(u16 ID)
{
    if (ID >= boneInstances.size())
        return nullptr;

    const auto it = std::find_if(boneMapByName.begin(), boneMapByName.end(),
        [ID](const accel::value_type& entry)
        {
            return entry.second == ID;
        });

    return it != boneMapByName.end() ? it->first.c_str() : nullptr;
}

CInifile* OzzKinematics::LL_UserData()
{
    return userData;
}

IKinematics::accel* OzzKinematics::LL_Bones()
{
    return &boneMapByName;
}

CBoneInstance& OzzKinematics::LL_GetBoneInstance(u16 bone_id)
{
    if (bone_id < boneInstances.size())
        return boneInstances[bone_id];

    NotImplemented(__FUNCTION__);
    return StubBoneInstance();
}

CBoneData& OzzKinematics::LL_GetData(u16 bone_id)
{
    if (bone_id < bones.size() && bones[bone_id])
        return *bones[bone_id];
    return StubBoneData();
}

const IBoneData& OzzKinematics::GetBoneData(u16 bone_id) const
{
    if (bone_id < bones.size() && bones[bone_id])
        return *bones[bone_id];
    return StubBoneData();
}

u16 OzzKinematics::LL_BoneCount() const
{
    return static_cast<u16>(boneInstances.size());
}

u16 OzzKinematics::LL_VisibleBoneCount()
{
    u16 count = 0;
    const u16 total = LL_BoneCount();
    for (u16 i = 0; i < total; ++i)
        if (LL_GetBoneVisible(i))
            ++count;
    return count;
}

Fmatrix& OzzKinematics::LL_GetTransform(u16 bone_id)
{
    if (bone_id < boneInstances.size())
        return boneInstances[bone_id].mTransform;

    NotImplemented(__FUNCTION__);
    return StubMatrix();
}

const Fmatrix& OzzKinematics::LL_GetTransform(u16 bone_id) const
{
    if (bone_id < boneInstances.size())
        return boneInstances[bone_id].mTransform;

    const_cast<OzzKinematics*>(this)->NotImplemented(__FUNCTION__);
    return StubMatrix();
}

Fmatrix& OzzKinematics::LL_GetTransform_R(u16 bone_id)
{
    if (bone_id < boneInstances.size())
        return boneInstances[bone_id].mRenderTransform;

    NotImplemented(__FUNCTION__);
    return StubMatrix();
}

Fobb& OzzKinematics::LL_GetBox(u16 bone_id)
{
    if (bone_id < boneBoxes.size())
        return boneBoxes[bone_id];

    NotImplemented(__FUNCTION__);
    return StubObb();
}

const Fbox& OzzKinematics::GetBox() const
{
    auto* self = const_cast<OzzKinematics*>(this);

    if (!initialized || boneInstances.empty())
    {
        self->cachedBox.invalidate();
        return cachedBox;
    }

    Fbox computed;
    computed.invalidate();

    for (size_t idx = 0; idx < boneInstances.size(); ++idx)
    {
        if (!IsBoneVisible(idx))
            continue;

        const Fmatrix& transform = boneInstances[idx].mTransform;
        computed.modify(transform.c);
    }

    if (!computed.is_valid())
        computed.set_zero();

    self->cachedBox = computed;
    return cachedBox;
}

void OzzKinematics::LL_GetBindTransform(xr_vector<Fmatrix>& matrices)
{
    matrices.clear();
    matrices.reserve(bones.size());
    for (CBoneData* bone : bones)
    {
        if (bone)
            matrices.push_back(bone->bind_transform);
    }
}

int OzzKinematics::LL_GetBoneGroups(xr_vector<xr_vector<u16>>& groups)
{
    groups.clear();
    return 0;
}

u16 OzzKinematics::LL_GetBoneRoot()
{
    return rootBone;
}

void OzzKinematics::LL_SetBoneRoot(u16 bone_id)
{
    rootBone = bone_id;
}

BOOL OzzKinematics::LL_GetBoneVisible(u16 bone_id)
{
    if (bone_id >= 64)
        return TRUE;
    const u64 mask = u64(1) << bone_id;
    return (visibleMask & mask) ? TRUE : FALSE;
}

void OzzKinematics::LL_SetBoneVisible(u16 bone_id, BOOL val, BOOL /*bRecursive*/)
{
    if (bone_id < 64)
    {
        const u64 mask = u64(1) << bone_id;
        if (val)
            visibleMask |= mask;
        else
            visibleMask &= ~mask;
    }

    if (bone_id < boneInstances.size())
    {
        if (val)
        {
            CalculateBones_Invalidate();
        }
        else
        {
            boneInstances[bone_id].mTransform.scale(0.f, 0.f, 0.f);
            boneInstances[bone_id].mRenderTransform.scale(0.f, 0.f, 0.f);
            if (bone_id < cachedTransformsPreCallbacks.size())
                cachedTransformsPreCallbacks[bone_id].scale(0.f, 0.f, 0.f);
        }
    }
}

u64 OzzKinematics::LL_GetBonesVisible()
{
    return visibleMask;
}

void OzzKinematics::LL_SetBonesVisible(u64 mask)
{
    visibleMask = mask;

    const size_t count = boneInstances.size();
    for (size_t idx = 0; idx < count && idx < 64; ++idx)
    {
        const u64 bit = u64(1) << idx;
        if ((visibleMask & bit) == 0)
        {
            boneInstances[idx].mTransform.scale(0.f, 0.f, 0.f);
            boneInstances[idx].mRenderTransform.scale(0.f, 0.f, 0.f);
            if (idx < cachedTransformsPreCallbacks.size())
                cachedTransformsPreCallbacks[idx].scale(0.f, 0.f, 0.f);
        }
    }
    CalculateBones_Invalidate();
}

void OzzKinematics::LL_AddTransformToBone(KinematicsABT::additional_bone_transform& offset)
{
    boneOffsets.push_back(offset);
    CalculateBones_Invalidate();
}

void OzzKinematics::LL_ClearAdditionalTransform(u16 bone_id)
{
    if (bone_id == BI_NONE)
    {
        boneOffsets.clear();
    }
    else
    {
        boneOffsets.erase(
            std::remove_if(boneOffsets.begin(), boneOffsets.end(),
                [bone_id](const KinematicsABT::additional_bone_transform& entry)
                {
                    return entry.m_bone_id == bone_id;
                }),
            boneOffsets.end());
    }

    CalculateBones_Invalidate();
}

void OzzKinematics::CalculateBones(BOOL bForceExact)
{
    if (!initialized)
        return;

    if (skeleton.num_joints() == 0 || boneInstances.empty())
        return;

    const u32 current_time = Device.dwTimeGlobal;
    if (!bForceExact && current_time == lastUpdateTime)
        return;
    if (!bForceExact && lastUpdateTime != 0 && current_time < lastUpdateTime + UCalc_Interval)
        return;

    const size_t expected_joint_count = static_cast<size_t>(skeleton.num_joints());
    if (boneInstances.size() != expected_joint_count)
    {
        Msg("[OzzKinematics] Bone instance array size mismatch (have %zu, expected %zu). Reinitializing.",
            boneInstances.size(), expected_joint_count);
        boneInstances.resize(expected_joint_count);
        for (CBoneInstance& instance : boneInstances)
            instance.construct();
    }

    if (modelTransforms.size() != expected_joint_count)
    {
        modelTransforms.resize(expected_joint_count);
        std::fill(modelTransforms.begin(), modelTransforms.end(), ozz::math::Float4x4::identity());
    }
    if (cachedTransformsPreCallbacks.size() != expected_joint_count)
    {
        cachedTransformsPreCallbacks.resize(expected_joint_count);
        std::fill(cachedTransformsPreCallbacks.begin(), cachedTransformsPreCallbacks.end(), Fidentity);
    }

    ozz::animation::LocalToModelJob job;
    job.skeleton = &skeleton;
    if (!sampledLocals.empty() && sampledLocals.size() == static_cast<size_t>(skeleton.num_soa_joints()))
        job.input = ozz::span<const ozz::math::SoaTransform>(sampledLocals.data(), sampledLocals.size());
    else
        job.input = skeleton.joint_rest_poses();

    job.output = ozz::span<ozz::math::Float4x4>(modelTransforms.data(), modelTransforms.size());

    if (!job.Run())
    {
        Msg("[OzzKinematics] LocalToModelJob failed during CalculateBones");
        return;
    }

    const size_t bone_count = boneInstances.size();
    Fbox box;
    box.invalidate();

    for (size_t i = 0; i < bone_count; ++i)
    {
        CBoneInstance& instance = boneInstances[i];
        const u16 bone_id = static_cast<u16>(i);
        const bool visible = IsBoneVisible(i);

        Fmatrix transform = ConvertOzzMatrixToXRay(modelTransforms[i]);
        ApplyAdditionalBoneTransforms(bone_id, transform);

        if (!visible)
        {
            instance.mTransform.scale(0.f, 0.f, 0.f);
            instance.mRenderTransform.scale(0.f, 0.f, 0.f);
            if (i < cachedTransformsPreCallbacks.size())
                cachedTransformsPreCallbacks[i].scale(0.f, 0.f, 0.f);
            continue;
        }

        cachedTransformsPreCallbacks[i] = transform;

        const bool has_callback = instance.callback() != nullptr;
        if (!instance.callback_overwrite() || !has_callback)
            instance.mTransform = transform;

        if (has_callback)
            instance.callback()(&instance);
        else if (instance.callback_overwrite())
            instance.mTransform = transform;

        if (i < bones.size() && bones[i])
            instance.mRenderTransform.mul_43(instance.mTransform, bones[i]->m2b_transform);
        else
            instance.mRenderTransform = instance.mTransform;

        box.modify(instance.mTransform.c);
    }

    cachedBox = box;
    lastUpdateTime = current_time;

    if (updateCallback)
        updateCallback(this);
}

void OzzKinematics::CalculateBones_Invalidate()
{
    lastUpdateTime = 0;
    visibilityCounter = 0;
    cachedBox.invalidate();
}

void OzzKinematics::Callback(UpdateCallback C, void* Param)
{
    updateCallback = C;
    updateCallbackParam = Param;
}

void OzzKinematics::SetUpdateCallback(UpdateCallback pCallback)
{
    updateCallback = pCallback;
}

void OzzKinematics::SetUpdateCallbackParam(void* pCallbackParam)
{
    updateCallbackParam = pCallbackParam;
}

UpdateCallback OzzKinematics::GetUpdateCallback()
{
    return updateCallback;
}

void* OzzKinematics::GetUpdateCallbackParam()
{
    return updateCallbackParam;
}

u32 OzzKinematics::LL_PartBlendsCount(u32 /*bone_part_id*/)
{
    return 0;
}

CBlend* OzzKinematics::LL_PartBlend(u32 /*bone_part_id*/, u32 /*n*/)
{
    return nullptr;
}

void OzzKinematics::LL_IterateBlends(IterateBlendsCallback& /*callback*/)
{
}

u16 OzzKinematics::LL_MotionsSlotCount()
{
    return 0;
}

const shared_motions& OzzKinematics::LL_MotionsSlot(u16 /*idx*/)
{
    static shared_motions dummy;
    return dummy;
}

CMotionDef* OzzKinematics::LL_GetMotionDef(MotionID id)
{
    if (!id.valid() || id.slot != 0)
        return nullptr;

    const u16 index = id.idx;
    EnsureMotionLibraryLoaded();

    MotionRecord* record = motionLibrary.Find(index);
    return record ? &record->definition : nullptr;
}

CMotion* OzzKinematics::LL_GetRootMotion(MotionID id)
{
    if (!id.valid())
        return nullptr;

    const u16 resolved_root = (rootBone != BI_NONE) ? rootBone : u16(0);
    return LL_GetMotion(id, resolved_root);
}

CMotion* OzzKinematics::LL_GetMotion(MotionID id, u16 bone_id)
{
    if (!id.valid() || id.slot != 0)
        return nullptr;

    if (!initialized)
        return nullptr;

    if (bone_id == BI_NONE)
        return nullptr;

    const u32 joint_count = static_cast<u32>(skeleton.num_joints());
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

void OzzKinematics::LL_BuldBoneMatrixDequatize(const CBoneData* /*bd*/, u8 /*channel_mask*/, SKeyTable& /*keys*/)
{
    NotImplemented(__FUNCTION__);
}

void OzzKinematics::LL_BoneMatrixBuild(CBoneInstance& /*bi*/, const Fmatrix* /*parent*/, const SKeyTable& /*keys*/)
{
    NotImplemented(__FUNCTION__);
}

IBlendDestroyCallback* OzzKinematics::GetBlendDestroyCallback()
{
    return blendDestroyCallback;
}

void OzzKinematics::SetBlendDestroyCallback(IBlendDestroyCallback* cb)
{
    blendDestroyCallback = cb;
}

void OzzKinematics::SetUpdateTracksCalback(IUpdateTracksCallback* callback)
{
    updateTracksCallback = callback;
}

IUpdateTracksCallback* OzzKinematics::GetUpdateTracksCalback()
{
    return updateTracksCallback;
}

MotionID OzzKinematics::LL_MotionID(LPCSTR B)
{
    if (!B || !*B)
        return MotionID();

    EnsureMotionLibraryLoaded();

    const MotionRecord* record = motionLibrary.Find(xr_string(B));
    return record ? record->id : MotionID();
}

u16 OzzKinematics::LL_PartID(LPCSTR /*B*/)
{
    return BI_NONE;
}

CBlend* OzzKinematics::LL_PlayCycle(u16 partition, MotionID motion, BOOL bMixing, float blendAccrue,
    float blendFalloff, float Speed, BOOL noloop, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    if (!motion.valid())
        return nullptr;

    if (motion.slot != 0)
    {
        Msg("[OzzKinematics] LL_PlayCycle received motion with unsupported slot %u", motion.slot);
        return nullptr;
    }

    EnsureMotionLibraryLoaded();

    const u16 motionIndex = motion.idx;
    MotionRecord* record = motionLibrary.Find(motionIndex);
    if (!record)
    {
        Msg("[OzzKinematics] LL_PlayCycle motion index %u out of range", motionIndex);
        return nullptr;
    }

    if (!animationControllerReady || !animationController)
        return nullptr;

    if (!record->animation)
    {
        Msg("[OzzKinematics] LL_PlayCycle motion '%s' is missing animation payload", record->name.c_str());
        return nullptr;
    }

    const u16 resolvedPartition = (partition == BI_NONE) ? u16(0) : partition;
    const bool mixing = !!bMixing;
    const bool stop_at_end = !!noloop;

    if (!mixing && !activeBlends.empty())
        ClearActiveBlends(true);

    if (!controllerMotion.valid() || controllerMotion != record->id)
    {
        if (!animationController->LoadAnimation(record->animation))
            return nullptr;

        controllerMotion = record->id;
        animationApplied = false;
        CalculateBones_Invalidate();
    }

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
    float playback_speed = Speed;
    if (fis_zero(playback_speed))
        playback_speed = !fis_zero(def_speed) ? def_speed : 1.f;
    blend.speed = playback_speed;

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

    animationController->SetLooping(!stop_at_end);
    animationController->SetPlaybackSpeed(playback_speed);
    animationApplied = false;

    return activeBlends.size() ? activeBlends.back().blend.get() : nullptr;
}

CBlend* OzzKinematics::LL_PlayCycle(
    u16 partition, MotionID motion, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    CMotionDef* def = LL_GetMotionDef(motion);
    if (!def)
        return nullptr;

    return LL_PlayCycle(partition, motion, bMixIn, def->Accrue(), def->Falloff(), def->Speed(), def->StopAtEnd(), Callback,
        CallbackParam, channel);
}

void OzzKinematics::LL_CloseCycle(u16 partition, u8 mask_channel)
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

void OzzKinematics::LL_SetChannelFactor(u16 /*channel*/, float /*factor*/)
{
}

void OzzKinematics::UpdateTracks()
{
    LL_UpdateTracks(Device.fTimeDelta, true, false);
}

void OzzKinematics::LL_UpdateTracks(float dt, bool b_force, bool leave_blends)
{
    if (!activeBlends.empty() && animationController)
    {
        const CBlend* primaryBlend = activeBlends.front().blend.get();
        if (primaryBlend)
        {
            animationController->SetLooping(!primaryBlend->stop_at_end);
            animationController->SetPlaybackSpeed(primaryBlend->speed);
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

MotionID OzzKinematics::ID_Cycle(LPCSTR N)
{
    MotionID id = ID_Cycle_Safe(N);
    R_ASSERT3(id.valid(), "[OzzKinematics] can't find cycle: ", N ? N : "<null>");
    return id;
}

MotionID OzzKinematics::ID_Cycle_Safe(LPCSTR N)
{
    if (!N || !N[0])
        return MotionID();

    return ResolveLegacyMotionId(xr_string(N));
}

MotionID OzzKinematics::ID_Cycle(shared_str N)
{
    MotionID id = ID_Cycle_Safe(N);
    R_ASSERT3(id.valid(), "[OzzKinematics] can't find cycle: ", N.c_str());
    return id;
}

MotionID OzzKinematics::ID_Cycle_Safe(shared_str N)
{
    if (!N || !N.c_str() || !N.c_str()[0])
        return MotionID();

    return ResolveLegacyMotionId(xr_string(N.c_str()));
}

CBlend* OzzKinematics::PlayCycle(LPCSTR N, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    MotionID id = ID_Cycle_Safe(N);
    if (!id.valid())
        return nullptr;

    return PlayCycle(id, bMixIn, Callback, CallbackParam, channel);
}

CBlend* OzzKinematics::PlayCycle(MotionID M, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    CMotionDef* def = LL_GetMotionDef(M);
    if (!def)
        return nullptr;

    return LL_PlayCycle(def->bone_or_part, M, bMixIn, def->Accrue(), def->Falloff(), def->Speed(), def->StopAtEnd(),
        Callback, CallbackParam, channel);
}

CBlend* OzzKinematics::PlayCycle(u16 partition, MotionID M, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam,
    u8 channel)
{
    return LL_PlayCycle(partition, M, bMixIn, Callback, CallbackParam, channel);
}

MotionID OzzKinematics::ID_FX(LPCSTR /*N*/)
{
    return MotionID();
}

MotionID OzzKinematics::ID_FX_Safe(LPCSTR /*N*/)
{
    return MotionID();
}

CBlend* OzzKinematics::PlayFX(LPCSTR /*N*/, float /*power_scale*/)
{
    return nullptr;
}

CBlend* OzzKinematics::PlayFX(MotionID /*M*/, float /*power_scale*/)
{
    return nullptr;
}

CBlend* OzzKinematics::PlayFX_Safe(cpcstr /*N*/, float /*power_scale*/)
{
    return nullptr;
}

const CPartition& OzzKinematics::partitions() const
{
    return defaultPartition;
}

float OzzKinematics::get_animation_length(MotionID /*motion_ID*/)
{
    if (animationController)
        return animationController->Duration();
    return 0.f;
}

IRenderVisual* OzzKinematics::dcast_RenderVisual()
{
    return nullptr;
}

IKinematics* OzzKinematics::dcast_PKinematics()
{
    return this;
}

IKinematicsAnimated* OzzKinematics::dcast_PKinematicsAnimated()
{
    return this;
}

#ifdef DEBUG
std::pair<LPCSTR, LPCSTR> OzzKinematics::LL_MotionDefName_dbg(MotionID /*ID*/)
{
    static xr_string empty;
    return { empty.c_str(), empty.c_str() };
}

void OzzKinematics::LL_DumpBlends_dbg()
{
    Msg("[OzzKinematics] LL_DumpBlends_dbg not implemented");
}

void OzzKinematics::DebugRender(Fmatrix& /*XFORM*/)
{
    NotImplemented(__FUNCTION__);
}

shared_str OzzKinematics::getDebugName()
{
    NotImplemented(__FUNCTION__);
    return shared_str("OzzKinematics");
}
#endif
} // namespace Animation
} // namespace XRay
