#include "stdafx.h"
#include "OzzIKSystem.h"
#include "ozz/base/maths/math_ex.h"
#include "../Externals/ozz-animation/samples/framework/utils.h"
#include <algorithm>
#include <cstring>

namespace xray
{
namespace animation
{

static ozz::math::Float3 ExtractTranslation(const ozz::math::Float4x4& mat)
{
    return ozz::math::Float3{
        ozz::math::GetX(mat.cols[3]),
        ozz::math::GetY(mat.cols[3]),
        ozz::math::GetZ(mat.cols[3])
    };
}

int OzzIKSystem::FindJointIndexByNames(
    ozz::span<const char* const> joint_names,
    std::initializer_list<const char*> candidates) const
{
    for (const char* candidate : candidates)
    {
        for (int i = 0; i < static_cast<int>(joint_names.size()); ++i)
        {
            if (std::strcmp(joint_names[i], candidate) == 0)
            {
                return i;
            }
        }
    }
    return -1;
}

void OzzIKSystem::ResolveChainAxes(
    LimbIkChain& chain,
    ozz::span<const ozz::math::Float4x4> bind_pose_models)
{
    chain.mid_axis = ozz::math::simd_float4::z_axis();
    chain.pole_vector = ozz::math::simd_float4::y_axis();  // Default pole vector

    if (chain.Valid() && chain.mid >= 0 && static_cast<size_t>(chain.mid) < bind_pose_models.size())
    {
        // Calculate mid_axis (bend axis) from bind pose Z-axis
        ozz::math::SimdFloat4 axis_candidate = bind_pose_models[chain.mid].cols[2];
        const ozz::math::SimdFloat4 axis_len_sq = ozz::math::Length3Sqr(axis_candidate);
        const ozz::math::SimdFloat4 min_len = ozz::math::simd_float4::Load1(1e-6f);
        if (ozz::math::AreAllTrue1(ozz::math::CmpGt(axis_len_sq, min_len)))
        {
            chain.mid_axis = ozz::math::Normalize3(axis_candidate);
            // Legs bend backwards (knees bend opposite to elbows)
            // Negate the axis for leg chains
            if (chain.role == LimbIkChain::Role::Leg)
            {
                chain.mid_axis = -chain.mid_axis;
            }
        }

        // Calculate stable pole vector from bind pose Y-axis
        ozz::math::SimdFloat4 pole_candidate = bind_pose_models[chain.mid].cols[1];
        const ozz::math::SimdFloat4 pole_len_sq = ozz::math::Length3Sqr(pole_candidate);
        if (ozz::math::AreAllTrue1(ozz::math::CmpGt(pole_len_sq, min_len)))
        {
            chain.pole_vector = ozz::math::Normalize3(pole_candidate);
        }
    }
}

void OzzIKSystem::PopulateChain(
    LimbIkChain& chain,
    LimbIkChain::Role role,
    ozz::span<const char* const> joint_names,
    ozz::span<const ozz::math::Float4x4> bind_pose_models,
    std::initializer_list<const char*> start_candidates,
    std::initializer_list<const char*> mid_candidates,
    std::initializer_list<const char*> end_candidates)
{
    chain.role = role;
    chain.start = FindJointIndexByNames(joint_names, start_candidates);
    chain.mid = FindJointIndexByNames(joint_names, mid_candidates);
    chain.end = FindJointIndexByNames(joint_names, end_candidates);
    chain.start_name = chain.start >= 0 ? joint_names[chain.start] : std::string();
    chain.mid_name = chain.mid >= 0 ? joint_names[chain.mid] : std::string();
    chain.end_name = chain.end >= 0 ? joint_names[chain.end] : std::string();
    chain.target_offset = { 0.f, 0.f, 0.f };
    chain.reached = false;
    ResolveChainAxes(chain, bind_pose_models);
}

void OzzIKSystem::Initialize(
    ozz::span<const char* const> joint_names,
    ozz::span<const ozz::math::Float4x4> bind_pose_models)
{
    // Reset chains
    left_leg_chain_ = LimbIkChain{};
    right_leg_chain_ = LimbIkChain{};
    left_arm_chain_ = LimbIkChain{};
    right_arm_chain_ = LimbIkChain{};

    // Populate leg chains
    PopulateChain(left_leg_chain_, LimbIkChain::Role::Leg,
        joint_names, bind_pose_models,
        { "bip01_l_thigh", "l_thigh", "left_thigh" },
        { "bip01_l_calf", "l_calf", "left_calf", "bip01_l_knee" },
        { "bip01_l_foot", "l_foot", "left_foot", "bip01_l_ankle" });

    PopulateChain(right_leg_chain_, LimbIkChain::Role::Leg,
        joint_names, bind_pose_models,
        { "bip01_r_thigh", "r_thigh", "right_thigh" },
        { "bip01_r_calf", "r_calf", "right_calf", "bip01_r_knee" },
        { "bip01_r_foot", "r_foot", "right_foot", "bip01_r_ankle" });

    // Populate arm chains
    PopulateChain(left_arm_chain_, LimbIkChain::Role::Arm,
        joint_names, bind_pose_models,
        { "bip01_l_upperarm", "bip01_l_shoulder", "l_upperarm", "left_shoulder" },
        { "bip01_l_forearm", "l_forearm", "left_forearm", "bip01_l_elbow" },
        { "bip01_l_hand", "l_hand", "left_hand", "bip01_l_wrist" });

    PopulateChain(right_arm_chain_, LimbIkChain::Role::Arm,
        joint_names, bind_pose_models,
        { "bip01_r_upperarm", "bip01_r_shoulder", "r_upperarm", "right_shoulder" },
        { "bip01_r_forearm", "r_forearm", "right_forearm", "bip01_r_elbow" },
        { "bip01_r_hand", "r_hand", "right_hand", "bip01_r_wrist" });

    // Auto-detect left/right swap based on X position of hands in bind pose
    if (left_arm_chain_.Valid() && right_arm_chain_.Valid() && !bind_pose_models.empty())
    {
        const float left_x = ozz::math::GetX(bind_pose_models[left_arm_chain_.end].cols[3]);
        const float right_x = ozz::math::GetX(bind_pose_models[right_arm_chain_.end].cols[3]);
        if (left_x > right_x)
        {
            std::swap(left_arm_chain_, right_arm_chain_);
        }
    }

    // Set labels
    left_leg_chain_.label = "Left leg";
    left_leg_chain_.role = LimbIkChain::Role::Leg;
    right_leg_chain_.label = "Right leg";
    right_leg_chain_.role = LimbIkChain::Role::Leg;
    left_arm_chain_.label = "Left arm";
    left_arm_chain_.role = LimbIkChain::Role::Arm;
    right_arm_chain_.label = "Right arm";
    right_arm_chain_.role = LimbIkChain::Role::Arm;

    // Set availability
    leg_ik_available_ = left_leg_chain_.Valid() || right_leg_chain_.Valid();
    arm_ik_available_ = left_arm_chain_.Valid() || right_arm_chain_.Valid();

    // Enable chains by default if valid
    left_leg_chain_.enabled = left_leg_chain_.Valid();
    right_leg_chain_.enabled = right_leg_chain_.Valid();
    left_arm_chain_.enabled = left_arm_chain_.Valid();
    right_arm_chain_.enabled = right_arm_chain_.Valid();

    initialized_ = true;
}

bool OzzIKSystem::SolveLimbIk(
    LimbIkChain& chain,
    const ozz::math::Float3& target_model,
    float weight,
    float soften,
    float twist_angle,
    ozz::span<const ozz::math::Float4x4> models,
    ozz::span<ozz::math::SoaTransform> locals,
    int* min_dirty_joint)
{
    chain.reached = false;
    if (!chain.Valid() || !chain.enabled)
    {
        chain.debug_target_valid = false;
        return false;
    }

    const size_t model_count = models.size();
    if (chain.start < 0 || chain.mid < 0 || chain.end < 0 ||
        static_cast<size_t>(chain.start) >= model_count ||
        static_cast<size_t>(chain.mid) >= model_count ||
        static_cast<size_t>(chain.end) >= model_count)
    {
        chain.debug_target_valid = false;
        return false;
    }

    const ozz::math::SimdFloat4 target_ms = ozz::math::simd_float4::Load3PtrU(&target_model.x);

    // Use the stable pole vector from bind pose instead of recalculating from current frame
    // This prevents IK flipping/snapping issues, especially for first-person arm rigs
    ozz::math::SimdFloat4 pole_vector_ms = chain.pole_vector;

    ozz::animation::IKTwoBoneJob ik_job;
    ik_job.target = target_ms;
    ik_job.pole_vector = pole_vector_ms;
    ik_job.mid_axis = chain.mid_axis;
    ik_job.weight = std::clamp(weight, 0.f, 1.f);
    ik_job.soften = std::clamp(soften, 0.f, 0.999f);
    ik_job.twist_angle = twist_angle;
    ik_job.start_joint = &models[chain.start];
    ik_job.mid_joint = &models[chain.mid];
    ik_job.end_joint = &models[chain.end];
    ozz::math::SimdQuaternion start_correction;
    ozz::math::SimdQuaternion mid_correction;
    ik_job.start_joint_correction = &start_correction;
    ik_job.mid_joint_correction = &mid_correction;
    ik_job.reached = &chain.reached;

    if (!ik_job.Run())
    {
        chain.reached = false;
        return false;
    }

    ozz::sample::MultiplySoATransformQuaternion(chain.start, start_correction, locals);
    ozz::sample::MultiplySoATransformQuaternion(chain.mid, mid_correction, locals);

    if (min_dirty_joint)
    {
        *min_dirty_joint = std::min(*min_dirty_joint, chain.start);
    }

    return true;
}

ozz::math::Float3 OzzIKSystem::ComputeChainTarget(
    const LimbIkChain& chain,
    ozz::span<const ozz::math::Float4x4> models,
    float foot_ground_height,
    float crouch_offset,
    bool crouch_affects_arms) const
{
    if (!chain.Valid() || static_cast<size_t>(chain.end) >= models.size())
    {
        return { 0.f, 0.f, 0.f };
    }

    ozz::math::Float3 target = ExtractTranslation(models[chain.end]);
    if (chain.role == LimbIkChain::Role::Leg)
    {
        target.x += chain.target_offset.x;
        target.z += chain.target_offset.z;
        target.y = foot_ground_height + chain.target_offset.y;
    }
    else
    {
        target.x += chain.target_offset.x;
        target.y += chain.target_offset.y;
        target.z += chain.target_offset.z;
        if (crouch_affects_arms)
        {
            target.y -= crouch_offset;
        }
    }
    return target;
}

void OzzIKSystem::ApplyDraggedTarget(
    LimbIkChain& chain,
    const ozz::math::Float3& target_world,
    ozz::span<const ozz::math::Float4x4> models)
{
    if (!chain.Valid() || static_cast<size_t>(chain.end) >= models.size())
    {
        return;
    }

    const ozz::math::Float3 current_pos = ExtractTranslation(models[chain.end]);
    chain.target_offset.x = target_world.x - current_pos.x + chain.target_offset.x;
    chain.target_offset.y = target_world.y - current_pos.y + chain.target_offset.y;
    chain.target_offset.z = target_world.z - current_pos.z + chain.target_offset.z;
}

} // namespace animation
} // namespace xray
