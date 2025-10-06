#pragma once

#include <cstddef>

#include "Include/xrRender/Kinematics.h"
#include "Include/xrRender/KinematicsAnimated.h"
#include "Include/xrRender/animation_motion.h"
#include "Layers/xrRender/Animation.h"
#include "xrCore/_fbox.h"
#include "xrCommon/xr_smart_pointers.h"
#include "xrCommon/xr_unordered_map.h"

#include "OzzAnimationController.h"
#include "ExtendedBoneMetadata.h"
#include <filesystem>
#include <vector>

#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/span.h"

class CMotion;

namespace ozz
{
namespace io
{
class Stream;
} // namespace io
} // namespace ozz

namespace XRay
{
namespace Animation
{
class OzzKinematics : public IKinematics, public IKinematicsAnimated
{
public:
    OzzKinematics();
    ~OzzKinematics() override;

    // Bootstrap from converted `.ozz` assets.
    bool InitializeFromOzz(pcstr skeletonPath, const xr_vector<xr_string>& motionRefs = xr_vector<xr_string>());
    bool InitializeFromOzzBuffer(ozz::span<const std::byte> skeletonData, const xr_vector<xr_string>& motionRefs = xr_vector<xr_string>());
    bool ApplyExtendedBoneMetadata(const ExtendedBoneMetadataCollection& metadata);
    bool LoadUserDataFromBuffer(const std::vector<std::uint8_t>& buffer);

    // Pose management helpers.
    bool SetPoseLocals(ozz::span<const ozz::math::SoaTransform> locals);
    void ClearPose();
    const ozz::animation::Skeleton& Skeleton() const;
    ozz::animation::SamplingJob::Context& SamplingContext();

    // Builds a skinning palette from cached bone instances. When render_space is
    // true the output contains mRenderTransform (model-space skinning matrices),
    // otherwise the raw local-to-model transforms prior to render-space offsets.
    void BuildSkinningPalette(xr_vector<Fmatrix>& out_matrices, bool render_space) const;

    // Returns whether the current skeleton has any joints.
    bool HasBones() const { return initialized && !boneInstances.empty(); }
    bool IsInitialized() const { return initialized; }

    // Animation/runtime helpers mirrored from the legacy facade.
    const xr_vector<xr_string>& LegacyMotionReferences() const { return motionReferences; }
    xr_vector<xr_string> LegacyMotionNames();
    bool PlayLegacyMotion(const xr_string& motion_name);
    bool LoadAnimationFromFile(const std::filesystem::path& path);
    void StopAnimation();
    bool AdvanceAnimation(float dt);
    bool HasActiveAnimation() const { return animationApplied; }
    void SetEmbeddedAnimationData(const std::vector<std::uint8_t>& data);

    bool HasLoadedAnimation() const;

    // IKinematicsAnimated implementation
    void OnCalculateBones() override;
#ifdef DEBUG
    std::pair<LPCSTR, LPCSTR> LL_MotionDefName_dbg(MotionID ID) override;
    void LL_DumpBlends_dbg() override;
#endif
    u32 LL_PartBlendsCount(u32 bone_part_id) override;
    CBlend* LL_PartBlend(u32 bone_part_id, u32 n) override;
    void LL_IterateBlends(IterateBlendsCallback& callback) override;
    u16 LL_MotionsSlotCount() override;
    const shared_motions& LL_MotionsSlot(u16 idx) override;
    CMotionDef* LL_GetMotionDef(MotionID id) override;
    CMotion* LL_GetRootMotion(MotionID id) override;
    CMotion* LL_GetMotion(MotionID id, u16 bone_id) override;
    void LL_BuldBoneMatrixDequatize(const CBoneData* bd, u8 channel_mask, SKeyTable& keys) override;
    void LL_BoneMatrixBuild(CBoneInstance& bi, const Fmatrix* parent, const SKeyTable& keys) override;
    IBlendDestroyCallback* GetBlendDestroyCallback() override;
    void SetBlendDestroyCallback(IBlendDestroyCallback* cb) override;
    void SetUpdateTracksCalback(IUpdateTracksCallback* callback) override;
    IUpdateTracksCallback* GetUpdateTracksCalback() override;
    MotionID LL_MotionID(LPCSTR B) override;
    u16 LL_PartID(LPCSTR B) override;
    CBlend* LL_PlayCycle(u16 partition, MotionID motion, BOOL bMixing, float blendAccrue, float blendFalloff, float Speed,
        BOOL noloop, PlayCallback Callback, LPVOID CallbackParam, u8 channel = 0) override;
    CBlend* LL_PlayCycle(
        u16 partition, MotionID motion, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam, u8 channel = 0) override;
    void LL_CloseCycle(u16 partition, u8 mask_channel = (1 << 0)) override;
    void LL_SetChannelFactor(u16 channel, float factor) override;
    void UpdateTracks() override;
    void LL_UpdateTracks(float dt, bool b_force, bool leave_blends) override;
    MotionID ID_Cycle(LPCSTR N) override;
    MotionID ID_Cycle_Safe(LPCSTR N) override;
    MotionID ID_Cycle(shared_str N) override;
    MotionID ID_Cycle_Safe(shared_str N) override;
    CBlend* PlayCycle(LPCSTR N, BOOL bMixIn = TRUE, PlayCallback Callback = nullptr, LPVOID CallbackParam = nullptr,
        u8 channel = 0) override;
    CBlend* PlayCycle(MotionID M, BOOL bMixIn = TRUE, PlayCallback Callback = nullptr, LPVOID CallbackParam = nullptr,
        u8 channel = 0) override;
    CBlend* PlayCycle(u16 partition, MotionID M, BOOL bMixIn = TRUE, PlayCallback Callback = nullptr,
        LPVOID CallbackParam = nullptr, u8 channel = 0) override;
    MotionID ID_FX(LPCSTR N) override;
    MotionID ID_FX_Safe(LPCSTR N) override;
    CBlend* PlayFX(LPCSTR N, float power_scale) override;
    CBlend* PlayFX(MotionID M, float power_scale) override;
    CBlend* PlayFX_Safe(cpcstr N, float power_scale) override;
    const CPartition& partitions() const override;
    float get_animation_length(MotionID motion_ID) override;

    // IKinematics implementation (stubbed for initial integration pass)
    void Bone_Calculate(CBoneData* bd, Fmatrix* parent) override;
    void Bone_GetAnimPos(Fmatrix& pos, u16 id, u8 channel_mask, bool ignore_callbacks) override;

    bool PickBone(const Fmatrix& parent_xform, pick_result& r, float dist, const Fvector& start, const Fvector& dir, u16 bone_id) override;
    void EnumBoneVertices(SEnumVerticesCallback& C, u16 bone_id) override;

    u16 LL_BoneID(LPCSTR B) override;
    u16 LL_BoneID(const shared_str& B) override;
    LPCSTR LL_BoneName_dbg(u16 ID) override;

    CInifile* LL_UserData() override;
    accel* LL_Bones() override;

    CBoneInstance& LL_GetBoneInstance(u16 bone_id) override;
    CBoneData& LL_GetData(u16 bone_id) override;
    const IBoneData& GetBoneData(u16 bone_id) const override;

    u16 LL_BoneCount() const override;
    u16 LL_VisibleBoneCount() override;

    Fmatrix& LL_GetTransform(u16 bone_id) override;
    const Fmatrix& LL_GetTransform(u16 bone_id) const override;

    Fmatrix& LL_GetTransform_R(u16 bone_id) override;
    Fobb& LL_GetBox(u16 bone_id) override;
    const Fbox& GetBox() const override;
    void LL_GetBindTransform(xr_vector<Fmatrix>& matrices) override;
    int LL_GetBoneGroups(xr_vector<xr_vector<u16>>& groups) override;

    u16 LL_GetBoneRoot() override;
    void LL_SetBoneRoot(u16 bone_id) override;

    BOOL LL_GetBoneVisible(u16 bone_id) override;
    void LL_SetBoneVisible(u16 bone_id, BOOL val, BOOL bRecursive) override;
    u64 LL_GetBonesVisible() override;
    void LL_SetBonesVisible(u64 mask) override;

    void LL_AddTransformToBone(KinematicsABT::additional_bone_transform& offset) override;
    void LL_ClearAdditionalTransform(u16 bone_id) override;

    void CalculateBones(BOOL bForceExact = FALSE) override;
    void CalculateBones_Invalidate() override;
    void Callback(UpdateCallback C, void* Param) override;

    void SetUpdateCallback(UpdateCallback pCallback) override;
    void SetUpdateCallbackParam(void* pCallbackParam) override;
    UpdateCallback GetUpdateCallback() override;
    void* GetUpdateCallbackParam() override;

    IRenderVisual* dcast_RenderVisual() override;
    IKinematics* dcast_PKinematics() override;
    IKinematicsAnimated* dcast_PKinematicsAnimated() override;

#ifdef DEBUG
    void DebugRender(Fmatrix& XFORM) override;
    shared_str getDebugName() override;
#endif

    struct MotionRecord
    {
        xr_string name;
        std::shared_ptr<ozz::animation::Animation> animation;
        CMotionDef definition;
        MotionID id;
        u32 frameCount = 0;
        xr_vector<xr_unique_ptr<CMotion>> boneMotions;
    };

    struct ActiveBlendEntry
    {
        ActiveBlendEntry() = default;
        ActiveBlendEntry(ActiveBlendEntry&&) = default;
        ActiveBlendEntry& operator=(ActiveBlendEntry&&) = default;
        ActiveBlendEntry(const ActiveBlendEntry&) = delete;
        ActiveBlendEntry& operator=(const ActiveBlendEntry&) = delete;

        xr_unique_ptr<CBlend> blend;
        MotionID motionId{};
        u16 partition = BI_NONE;
        u8 channel = 0;
        u16 recordIndex = u16(-1);
    };

private:
    void NotImplemented(pcstr function_name) const;
    bool BuildBoneMetadata();
    void ResetRuntimeState();
    void ResetAnimationState();
    void InitializeChannelState();
    bool EnsureAnimationController();
    bool LoadLegacyMotion(const xr_string& motion_name);
    MotionID ResolveLegacyMotionId(const xr_string& motion_name);
    void ApplyAdditionalBoneTransforms(u16 bone_id, Fmatrix& transform) const;
    bool IsBoneVisible(size_t index) const;
    bool LoadSkeletonFromStream(ozz::io::Stream* stream, pcstr debug_source);
    bool FinalizeSkeletonInitialization(pcstr debug_source);
    bool LoadOzzAnimationsFromFile(const xr_string& relative_path);
    bool LoadOzzAnimationsFromArchive(ozz::io::IArchive& archive, const xr_string& source_label);
    bool LoadMotionReference(const xr_string& reference);
    void EnsureMotionLibraryLoaded();
    int FindActiveBlendIndex(u16 partition, u8 channel) const;
    void RemoveActiveBlend(size_t index, bool notifyDestroy);
    void ClearActiveBlends(bool notifyDestroy);
    void ResetAnimationControllerState();

private:
    CInifile* userData;
    xr_unique_ptr<CInifile> userDataOwner;
    accel boneMapByName;
    accel boneMapByPtr;
    xr_vector<CBoneInstance> boneInstances;
    xr_vector<CBoneData*> bones;
    xr_vector<xr_unique_ptr<CBoneData>> boneStorage;
    xr_vector<Fobb> boneBoxes;
    u16 rootBone;
    u64 visibleMask;
    UpdateCallback updateCallback;
    void* updateCallbackParam;
    u32 lastUpdateTime;
    s32 visibilityCounter;
    Fbox cachedBox;
    ozz::animation::Skeleton skeleton;
    xr_vector<ozz::math::SoaTransform> sampledLocals;
    xr_vector<ozz::math::Float4x4> modelTransforms;
    xr_vector<Fmatrix> cachedTransformsPreCallbacks;
    xr_vector<KinematicsABT::additional_bone_transform> boneOffsets;
    ozz::animation::SamplingJob::Context samplingContext;
    xr_unique_ptr<OzzAnimationController> animationController;

    struct MotionLibrary
    {
        xr_vector<MotionRecord> records;
        xr_unordered_map<xr_string, u16> lookup;

        void Reset()
        {
            records.clear();
            lookup.clear();
        }

        bool Contains(const xr_string& name) const
        {
            return lookup.find(name) != lookup.end();
        }

        MotionRecord* Find(u16 index)
        {
            return index < records.size() ? &records[index] : nullptr;
        }

        const MotionRecord* Find(u16 index) const
        {
            return index < records.size() ? &records[index] : nullptr;
        }

        MotionRecord* Find(const xr_string& name)
        {
            auto it = lookup.find(name);
            return it != lookup.end() ? &records[it->second] : nullptr;
        }

        const MotionRecord* Find(const xr_string& name) const
        {
            auto it = lookup.find(name);
            return it != lookup.end() ? &records[it->second] : nullptr;
        }

        u16 NextIndex() const { return static_cast<u16>(records.size()); }

        void Add(MotionRecord&& record)
        {
            const u16 index = static_cast<u16>(records.size());
            lookup.emplace(record.name, index);
            records.push_back(std::move(record));
        }
    };

    MotionLibrary motionLibrary;
    xr_vector<xr_string> motionReferences;
    std::vector<std::uint8_t> embeddedAnimationData;
    bool motionLibraryBuilt = false;
    IBlendDestroyCallback* blendDestroyCallback = nullptr;
    IUpdateTracksCallback* updateTracksCallback = nullptr;
    bool animationApplied = false;
    CPartition defaultPartition{};
    bool initialized = false;
    xr_vector<ActiveBlendEntry> activeBlends;
    MotionID controllerMotion{};
    bool animationControllerReady = false;
    xray::render::RENDER_NAMESPACE::animation::channal_rule channelRules[MAX_CHANNELS]{};
    float channelFactors[MAX_CHANNELS]{};
};
} // namespace Animation
} // namespace XRay
