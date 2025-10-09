#pragma once

#include "xrCore/xrCore.h"
#include "xrCore/Animation/SkeletonMotions.hpp"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/blending_job.h"
#include "ozz/base/containers/vector.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/maths/simd_math.h"

namespace AnimationECS {

//-----------------------------------------------------------------------------
// AnimationState Component
// Stores current playback state for an animation
//-----------------------------------------------------------------------------
struct AnimationState
{
    float current_time{0.0f};       // Current playback time in seconds
    float time_ratio{0.0f};         // Normalized time [0.0, 1.0]
    u16 current_partition{0};       // Current body partition (0 = full body)
    bool is_playing{false};         // Is animation currently playing
    bool is_looping{false};         // Should animation loop

    // Blending
    float blend_amount{1.0f};       // Current blend weight [0.0, 1.0]
    float blend_time{0.0f};         // Current blend transition time
    float blend_duration{0.2f};     // Target blend duration
};

//-----------------------------------------------------------------------------
// AnimationController Component
// References to shared animation resources (read-only, thread-safe)
//-----------------------------------------------------------------------------
struct AnimationController
{
    // Shared resources (read-only, never modified)
    const ozz::animation::Skeleton* skeleton{nullptr};
    const ozz::animation::Animation* animation{nullptr};

    // Resource names for debugging/reload
    shared_str skeleton_name;
    shared_str animation_name;

    // Playback settings
    float playback_speed{1.0f};

    void SetSkeleton(const ozz::animation::Skeleton* skel, const shared_str& name) {
        skeleton = skel;
        skeleton_name = name;
    }

    void SetAnimation(const ozz::animation::Animation* anim, const shared_str& name) {
        animation = anim;
        animation_name = name;
    }
};

//-----------------------------------------------------------------------------
// AnimationBuffers Component
// Per-character mutable animation data (thread-local)
//-----------------------------------------------------------------------------
struct AnimationBuffers
{
    // Sampling context (required for ozz sampling)
    ozz::animation::SamplingJob::Context context;

    // Local-space transforms (SoA layout for SIMD)
    ozz::vector<ozz::math::SoaTransform> locals;

    // Model-space matrices (AoS layout for rendering)
    ozz::vector<ozz::math::Float4x4> models;

    // Initialize buffers based on skeleton size
    void Initialize(const ozz::animation::Skeleton* skeleton)
    {
        if (!skeleton)
            return;

        const int num_joints = skeleton->num_joints();
        const int num_soa_joints = skeleton->num_soa_joints();

        // Resize buffers
        locals.resize(num_soa_joints);
        models.resize(num_joints);

        // Allocate context
        context.Resize(num_joints);
    }

    void Clear()
    {
        locals.clear();
        models.clear();
        context.Resize(0);
    }

    bool IsInitialized() const
    {
        return !locals.empty() && !models.empty();
    }
};

//-----------------------------------------------------------------------------
// BlendState Component
// Multi-animation blending state
//-----------------------------------------------------------------------------
struct BlendState
{
    // Blending layers
    ozz::vector<ozz::animation::BlendingJob::Layer> layers;

    // Blend transition tracking
    float transition_time{0.0f};
    float transition_duration{0.0f};

    void AddLayer(const ozz::animation::BlendingJob::Layer& layer)
    {
        layers.push_back(layer);
    }

    void ClearLayers()
    {
        layers.clear();
    }

    bool HasLayers() const
    {
        return !layers.empty();
    }
};

//-----------------------------------------------------------------------------
// PartitionMasks Component
// Per-partition joint weight masks (for body part animation)
//-----------------------------------------------------------------------------
struct PartitionMasks
{
    // Per-partition weight masks (SoA format)
    xr_map<u16, ozz::vector<ozz::math::SimdFloat4>> masks;

    void SetMask(u16 partition_id, const ozz::vector<ozz::math::SimdFloat4>& mask)
    {
        masks[partition_id] = mask;
    }

    const ozz::vector<ozz::math::SimdFloat4>* GetMask(u16 partition_id) const
    {
        auto it = masks.find(partition_id);
        return (it != masks.end()) ? &it->second : nullptr;
    }

    void Clear()
    {
        masks.clear();
    }
};

//-----------------------------------------------------------------------------
// AnimationCallbacks Component
// X-Ray compatibility callbacks
//-----------------------------------------------------------------------------
struct AnimationCallbacks
{
    PlayCallback on_play{nullptr};
    PlayCallback on_end{nullptr};
    PlayCallback on_stop{nullptr};
    CBlend* callback_param{nullptr};

    void InvokeOnPlay()
    {
        if (on_play && callback_param)
            on_play(callback_param);
    }

    void InvokeOnEnd()
    {
        if (on_end && callback_param)
            on_end(callback_param);
    }

    void InvokeOnStop()
    {
        if (on_stop && callback_param)
            on_stop(callback_param);
    }
};

//-----------------------------------------------------------------------------
// XRayCompatibility Component
// Stores X-Ray specific data for backward compatibility
//-----------------------------------------------------------------------------
struct XRayCompatibility
{
    // CBlend compatibility handle
    CBlend* blend_handle{nullptr};

    // Motion ID (X-Ray motion system)
    MotionID motion_id;

    // Channel and priority
    u8 channel{0};
    u8 priority{0};

    // Flags
    bool accrue{false};
    bool falloff{true};
};

} // namespace AnimationECS
