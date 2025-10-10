#include "stdafx.h"
#include "AnimationECS_IK.h"
#include "AnimationECS_Components.h"
#include "ozz/animation/runtime/ik_two_bone_job.h"
#include "ozz/base/maths/math_ex.h"
#include "ozz/base/maths/simd_quaternion.h"
#include "../Externals/ozz-animation/samples/framework/utils.h"
#include <algorithm>
#include <cstring>

// NOTE: Debug visualization removed from this low-level library to avoid dependencies on xrGame.
// Debug drawing should be implemented at the application level (e.g., in xrGame or viewer tools)
// where DBG_Draw functions are available. See IK_CURRENT_LIMITATIONS.md for details.

namespace AnimationECS {

//=============================================================================
// IKInitializationSystem Implementation
//=============================================================================

int IKInitializationSystem::FindJointIndexByNames(
    ozz::span<const char* const> joint_names,
    std::initializer_list<const char*> candidates)
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

void IKInitializationSystem::ResolveChainAxes(
    LimbIKChain& chain,
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
            if (chain.role == LimbIKChain::Role::Leg)
            {
                chain.mid_axis = -chain.mid_axis;
            }
        }

        // Calculate stable pole vector from bind pose Y-axis
        // This prevents IK flipping/snapping issues, especially for first-person arm rigs
        ozz::math::SimdFloat4 pole_candidate = bind_pose_models[chain.mid].cols[1];
        const ozz::math::SimdFloat4 pole_len_sq = ozz::math::Length3Sqr(pole_candidate);
        if (ozz::math::AreAllTrue1(ozz::math::CmpGt(pole_len_sq, min_len)))
        {
            chain.pole_vector = ozz::math::Normalize3(pole_candidate);
        }
    }
}

void IKInitializationSystem::PopulateChain(
    LimbIKChain& chain,
    LimbIKChain::Role role,
    ozz::span<const char* const> joint_names,
    ozz::span<const ozz::math::Float4x4> bind_pose_models,
    std::initializer_list<const char*> start_candidates,
    std::initializer_list<const char*> mid_candidates,
    std::initializer_list<const char*> end_candidates)
{
    chain.role = role;
    chain.start = static_cast<s16>(FindJointIndexByNames(joint_names, start_candidates));
    chain.mid = static_cast<s16>(FindJointIndexByNames(joint_names, mid_candidates));
    chain.end = static_cast<s16>(FindJointIndexByNames(joint_names, end_candidates));
    chain.start_name = chain.start >= 0 ? joint_names[chain.start] : std::string();
    chain.mid_name = chain.mid >= 0 ? joint_names[chain.mid] : std::string();
    chain.end_name = chain.end >= 0 ? joint_names[chain.end] : std::string();
    chain.target_offset = { 0.f, 0.f, 0.f };
    chain.reached = false;
    ResolveChainAxes(chain, bind_pose_models);
}

void IKInitializationSystem::Initialize(
    entt::registry& registry,
    entt::entity entity,
    ozz::span<const char* const> joint_names,
    ozz::span<const ozz::math::Float4x4> bind_pose_models)
{
    // Add or get IK configuration component
    auto& ik_config = registry.emplace_or_replace<IKConfiguration>(entity);

    // Reset chains
    ik_config.left_leg = LimbIKChain{};
    ik_config.right_leg = LimbIKChain{};
    ik_config.left_arm = LimbIKChain{};
    ik_config.right_arm = LimbIKChain{};

    // Populate leg chains
    PopulateChain(ik_config.left_leg, LimbIKChain::Role::Leg,
        joint_names, bind_pose_models,
        { "bip01_l_thigh", "l_thigh", "left_thigh" },
        { "bip01_l_calf", "l_calf", "left_calf", "bip01_l_knee" },
        { "bip01_l_foot", "l_foot", "left_foot", "bip01_l_ankle" });

    PopulateChain(ik_config.right_leg, LimbIKChain::Role::Leg,
        joint_names, bind_pose_models,
        { "bip01_r_thigh", "r_thigh", "right_thigh" },
        { "bip01_r_calf", "r_calf", "right_calf", "bip01_r_knee" },
        { "bip01_r_foot", "r_foot", "right_foot", "bip01_r_ankle" });

    // Populate arm chains
    PopulateChain(ik_config.left_arm, LimbIKChain::Role::Arm,
        joint_names, bind_pose_models,
        { "bip01_l_upperarm", "bip01_l_shoulder", "l_upperarm", "left_shoulder" },
        { "bip01_l_forearm", "l_forearm", "left_forearm", "bip01_l_elbow" },
        { "bip01_l_hand", "l_hand", "left_hand", "bip01_l_wrist" });

    PopulateChain(ik_config.right_arm, LimbIKChain::Role::Arm,
        joint_names, bind_pose_models,
        { "bip01_r_upperarm", "bip01_r_shoulder", "r_upperarm", "right_shoulder" },
        { "bip01_r_forearm", "r_forearm", "right_forearm", "bip01_r_elbow" },
        { "bip01_r_hand", "r_hand", "right_hand", "bip01_r_wrist" });

    // Auto-detect left/right swap based on X position of hands in bind pose
    if (ik_config.left_arm.Valid() && ik_config.right_arm.Valid() && !bind_pose_models.empty())
    {
        const float left_x = ozz::math::GetX(bind_pose_models[ik_config.left_arm.end].cols[3]);
        const float right_x = ozz::math::GetX(bind_pose_models[ik_config.right_arm.end].cols[3]);
        if (left_x > right_x)
        {
            std::swap(ik_config.left_arm, ik_config.right_arm);
        }
    }

    // Set labels
    ik_config.left_leg.label = "Left leg";
    ik_config.right_leg.label = "Right leg";
    ik_config.left_arm.label = "Left arm";
    ik_config.right_arm.label = "Right arm";

    // Set availability
    ik_config.leg_ik_available = ik_config.left_leg.Valid() || ik_config.right_leg.Valid();
    ik_config.arm_ik_available = ik_config.left_arm.Valid() || ik_config.right_arm.Valid();

    // Disable chains by default - application must explicitly enable them
    // Enabling by default causes numerical instability when solving to trivial targets
    ik_config.left_leg.enabled = false;
    ik_config.right_leg.enabled = false;
    ik_config.left_arm.enabled = false;
    ik_config.right_arm.enabled = false;

    // Set default parameters
    ik_config.leg_params.weight = 1.0f;
    ik_config.leg_params.soften = 0.97f;
    ik_config.leg_params.twist_angle = 0.f;

    ik_config.arm_params.weight = 1.0f;
    ik_config.arm_params.soften = 0.85f;
    ik_config.arm_params.twist_angle = 0.f;

    ik_config.initialized = true;

    Msg("[AnimationECS IK] Initialized IK for entity. Legs: %d, Arms: %d",
        ik_config.leg_ik_available, ik_config.arm_ik_available);
}

//=============================================================================
// IKSolverSystem Implementation
//=============================================================================

ozz::math::Float3 IKSolverSystem::ExtractTranslation(const ozz::math::Float4x4& mat)
{
    return ozz::math::Float3{
        ozz::math::GetX(mat.cols[3]),
        ozz::math::GetY(mat.cols[3]),
        ozz::math::GetZ(mat.cols[3])
    };
}

bool IKSolverSystem::SolveLimbIK(
    LimbIKChain& chain,
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

    // Store debug info for external visualization
    chain.debug_target = target_model;
    chain.debug_target_valid = true;

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
        *min_dirty_joint = std::min<int>(*min_dirty_joint, static_cast<int>(chain.start));
    }

    return true;
}

ozz::math::Float3 IKSolverSystem::ComputeChainTarget(
    const LimbIKChain& chain,
    ozz::span<const ozz::math::Float4x4> models,
    float foot_ground_height,
    float crouch_offset,
    bool crouch_affects_arms)
{
    if (!chain.Valid() || static_cast<size_t>(chain.end) >= models.size())
    {
        return { 0.f, 0.f, 0.f };
    }

    ozz::math::Float3 target = ExtractTranslation(models[chain.end]);
    if (chain.role == LimbIKChain::Role::Leg)
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

void IKSolverSystem::ApplyDraggedTarget(
    LimbIKChain& chain,
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

void IKSolverSystem::Update(entt::registry& registry)
{
    Msg("[IKSolverSystem::Update] Called");

    // NOTE: Currently this IK system does NOT calculate ground collision targets like the legacy IKLimb system!
    // The legacy system (see xrGame/ik/IKLimb.cpp):
    //   - Uses m_foot.Collide() to raycast for ground surfaces
    //   - Calculates proper foot placement on terrain/geometry
    //   - Blends between animation and IK based on foot contact state
    //
    // This new system currently just uses animated positions + user offsets as targets.
    // To get visible foot IK like the legacy system, we need to:
    //   1. Implement ground collision detection (raycasting)
    //   2. Calculate proper foot placement matrices
    //   3. Handle blending and state transitions
    //
    // For now, this serves as a foundation and can be extended with proper target calculation.

    // Process all entities with IK configuration
    auto view = registry.view<IKConfiguration, AnimationBuffers>();

    const size_t entity_count = view.size_hint();
    Msg("[IKSolverSystem::Update] Found %zu entities with IKConfiguration + AnimationBuffers", entity_count);

    if (entity_count == 0)
    {
        Msg("[IKSolverSystem::Update] No entities to process - returning early");
        return;
    }

    for (auto entity : view)
    {
        auto& ik_config = view.get<IKConfiguration>(entity);
        auto& buffers = view.get<AnimationBuffers>(entity);

        if (!ik_config.IsInitialized() || !buffers.IsInitialized())
            continue;

        // Build local-to-model transforms first (IK works in model space)
        // This should be done by the LocalToModelSystem before calling IK
        // For now, we assume models[] is already up-to-date

        int min_dirty_joint = static_cast<int>(buffers.models.size());

        // Solve leg IK
        if (ik_config.leg_ik_available)
        {
            auto solve_leg = [&](LimbIKChain& chain)
            {
                if (!chain.Valid() || !chain.enabled)
                    return;

                const ozz::math::Float3 target = ComputeChainTarget(
                    chain,
                    ozz::make_span(buffers.models),
                    ik_config.foot_ground_height,
                    ik_config.crouch_offset,
                    ik_config.crouch_affects_arms);

                SolveLimbIK(
                    chain,
                    target,
                    ik_config.leg_params.weight,
                    ik_config.leg_params.soften,
                    ik_config.leg_params.twist_angle,
                    ozz::make_span(buffers.models),
                    ozz::make_span(buffers.locals),
                    &min_dirty_joint);
            };

            solve_leg(ik_config.left_leg);
            solve_leg(ik_config.right_leg);
        }

        // Solve arm IK
        if (ik_config.arm_ik_available)
        {
            auto solve_arm = [&](LimbIKChain& chain)
            {
                if (!chain.Valid() || !chain.enabled)
                    return;

                const ozz::math::Float3 target = ComputeChainTarget(
                    chain,
                    ozz::make_span(buffers.models),
                    ik_config.foot_ground_height,
                    ik_config.crouch_offset,
                    ik_config.crouch_affects_arms);

                SolveLimbIK(
                    chain,
                    target,
                    ik_config.arm_params.weight,
                    ik_config.arm_params.soften,
                    ik_config.arm_params.twist_angle,
                    ozz::make_span(buffers.models),
                    ozz::make_span(buffers.locals),
                    &min_dirty_joint);
            };

            solve_arm(ik_config.left_arm);
            solve_arm(ik_config.right_arm);
        }

        // Rebuild model transforms after IK modified locals
        // IK solver modifies local-space transforms, so we must rebuild model-space matrices
        // from the affected joints onwards
        if (min_dirty_joint < static_cast<int>(buffers.models.size()))
        {
            // Get controller for skeleton access
            auto* controller = registry.try_get<AnimationController>(entity);
            if (controller && controller->skeleton && buffers.IsInitialized())
            {
                // Rebuild local-to-model from the first affected joint onwards
                ozz::animation::LocalToModelJob ltm_job;
                ltm_job.skeleton = controller->skeleton;
                ltm_job.from = min_dirty_joint;  // Start from first IK-modified joint
                ltm_job.input = ozz::make_span(buffers.locals);
                ltm_job.output = ozz::make_span(buffers.models);

                if (!ltm_job.Run())
                {
                    Msg("! [IKSolverSystem] Failed to rebuild model transforms after IK for entity %u", static_cast<u32>(entity));
                }
            }
        }
    }
}

//=============================================================================
// IKDebugSystem Implementation
//=============================================================================

void IKDebugSystem::UpdateDebugVisualization(entt::registry& registry)
{
    // TODO: Implement debug visualization
    // This would draw IK targets, pole vectors, chain bones, etc.
}

void IKDebugSystem::RenderDebugInfo(const IKConfiguration& ik_config,
                                   ozz::span<const ozz::math::Float4x4> models)
{
    // TODO: Implement debug rendering
    // This would draw gizmos for IK targets, chains, etc.
}

} // namespace AnimationECS
