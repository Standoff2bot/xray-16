#pragma once

#include <cstddef>

#include "Include/xrRender/Kinematics.h"
#include "xrCore/_fbox.h"
#include "xrCommon/xr_smart_pointers.h"

#include "ExtendedBoneMetadata.h"

#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/span.h"

namespace ozz::io { class Stream; }

namespace XRay::Animation
{
/**
 * Core skeleton and bone state management.
 * This class contains all shared functionality needed by both static and animated kinematics.
 * It does not implement any interfaces directly.
 */
class OzzKinematicsCore
{
public:
    OzzKinematicsCore();
    virtual ~OzzKinematicsCore();

    // Initialization
    bool InitializeFromOzz(pcstr skeletonPath);
    bool InitializeFromOzzBuffer(ozz::span<const std::byte> skeletonData);
    bool ApplyExtendedBoneMetadata(const ExtendedBoneMetadataCollection& metadata);
    bool LoadUserDataFromBuffer(const std::vector<std::uint8_t>& buffer);

    // Skeleton access
    const ozz::animation::Skeleton& Skeleton() const { return skeleton; }
    bool HasBones() const { return initialized && !boneInstances.empty(); }
    bool IsInitialized() const { return initialized; }

    // Pose management
    bool SetPoseLocals(ozz::span<const ozz::math::SoaTransform> locals);
    void ClearPose();
    ozz::animation::SamplingJob::Context& SamplingContext() { return samplingContext; }

    // Transform building
    void BuildSkinningPalette(xr_vector<Fmatrix>& out_matrices, bool render_space) const;

    // Bone queries
    u16 GetBoneCount() const;
    u16 GetRootBone() const { return rootBone; }
    void SetRootBone(u16 bone_id);

    // Visibility
    bool IsBoneVisible(u16 bone_id) const;
    void SetBoneVisible(u16 bone_id, bool visible, bool recursive);
    u64 GetVisibilityMask() const { return visibleMask; }
    void SetVisibilityMask(u64 mask);

    // User data
    CInifile* GetUserData() { return userData; }

    // Bone access
    struct BoneInfo
    {
        shared_str name;
        u16 id;
        u16 parent_id;
        CBoneData* data;
        CBoneInstance* instance;
    };

    bool GetBoneInfo(u16 bone_id, BoneInfo& info) const;
    u16 FindBoneID(pcstr name) const;
    u16 FindBoneID(const shared_str& name) const;
    pcstr GetBoneName(u16 bone_id) const;

    // Transform access
    const Fmatrix& GetBoneTransform(u16 bone_id) const;
    const Fmatrix& GetBoneRenderTransform(u16 bone_id) const;
    void GetBindTransforms(xr_vector<Fmatrix>& matrices) const;
    const Fbox& GetBoundingBox() const;
    Fobb& GetBoneBox(u16 bone_id);

    // Calculate transforms from current pose
    void CalculateTransforms(bool force_exact);
    void InvalidateCache();

    // Callbacks
    using UpdateCallback = void(*)(IKinematics*);
    void SetUpdateCallback(UpdateCallback callback, void* param);
    UpdateCallback GetUpdateCallback() const { return updateCallback; }
    void* GetUpdateCallbackParam() const { return updateCallbackParam; }

    // Additional transforms
    void AddBoneTransform(const KinematicsABT::additional_bone_transform& transform);
    void ClearBoneTransform(u16 bone_id);

    // Bone lookup accelerators
    IKinematics::accel* GetBoneMapByName() { return &boneMapByName; }
    IKinematics::accel* GetBoneMapByPtr() { return &boneMapByPtr; }

protected:

protected:
    // Virtual hooks for derived classes
    virtual void OnSkeletonLoaded() {}
    virtual void OnResetRuntime() {}
    virtual void OnCalculateBones() {}

private:
    bool LoadSkeletonFromStream(ozz::io::Stream* stream, pcstr debug_source);
    bool FinalizeSkeletonInitialization(pcstr debug_source);
    bool BuildBoneMetadata();
    void ResetRuntimeState();
    void ApplyAdditionalBoneTransforms(u16 bone_id, Fmatrix& transform) const;
    void UpdateBoundingBox();

protected:
    // Skeleton data
    ozz::animation::Skeleton skeleton;
    xr_vector<ozz::math::SoaTransform> sampledLocals;
    ozz::animation::SamplingJob::Context samplingContext;

    // Bone instances and metadata
    xr_vector<CBoneInstance> boneInstances;
    xr_vector<CBoneData*> bones;
    xr_vector<xr_unique_ptr<CBoneData>> boneStorage;
    xr_vector<Fobb> boneBoxes;

    // Transform caches
    xr_vector<ozz::math::Float4x4> modelTransforms;
    xr_vector<Fmatrix> cachedTransformsPreCallbacks;

    // Bone lookup
    IKinematics::accel boneMapByName;
    IKinematics::accel boneMapByPtr;

    // Additional transforms
    xr_vector<KinematicsABT::additional_bone_transform> boneOffsets;

    // State
    u16 rootBone;
    u64 visibleMask;
    u32 lastUpdateTime;
    s32 visibilityCounter;
    Fbox cachedBox;
    bool initialized;

    // User data
    CInifile* userData;
    xr_unique_ptr<CInifile> userDataOwner;

    // Callbacks
    UpdateCallback updateCallback;
    void* updateCallbackParam;
};

} // namespace XRay::Animation
