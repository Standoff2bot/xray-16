#pragma once

#include "ozz/animation/runtime/ik_two_bone_job.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/containers/vector.h"
#include "ozz/base/span.h"
#include <string>

namespace xray
{
namespace animation
{

/// <summary>
/// Represents a two-bone IK chain (shoulder-elbow-wrist or hip-knee-ankle)
/// </summary>
struct LimbIkChain
{
    enum class Role
    {
        Leg,
        Arm,
    };

    std::string label;                          // Display name (e.g., "Left arm")
    Role role = Role::Leg;                      // Chain type determines bend direction

    // Joint indices in skeleton
    int start = -1;                             // Shoulder/Hip joint index
    int mid = -1;                               // Elbow/Knee joint index
    int end = -1;                               // Wrist/Ankle joint index

    // Cached joint names for debugging
    std::string start_name;
    std::string mid_name;
    std::string end_name;

    // IK configuration (calculated from bind pose)
    ozz::math::SimdFloat4 mid_axis;            // Bend axis (which way elbow/knee bends)
    ozz::math::SimdFloat4 pole_vector;         // Stable pole direction from bind pose

    // IK target control
    ozz::math::Float3 target_offset = { 0.f, 0.f, 0.f };  // Offset from animated position
    bool enabled = true;                       // Whether to apply IK to this chain
    bool reached = false;                      // Whether IK reached target last frame

    // Debug visualization
    ozz::math::Float3 debug_target = { 0.f, 0.f, 0.f };
    bool debug_target_valid = false;

    bool Valid() const { return start >= 0 && mid >= 0 && end >= 0; }
};

/// <summary>
/// Manages IK chains for a skeleton and provides IK solving
/// </summary>
class OzzIKSystem
{
public:
    OzzIKSystem() = default;
    ~OzzIKSystem() = default;

    /// <summary>
    /// Initialize IK chains by finding appropriate bones in the skeleton
    /// </summary>
    /// <param name="joint_names">Skeleton joint names</param>
    /// <param name="bind_pose_models">Bind pose model-space transforms</param>
    void Initialize(
        ozz::span<const char* const> joint_names,
        ozz::span<const ozz::math::Float4x4> bind_pose_models);

    /// <summary>
    /// Solve IK for a specific limb chain
    /// </summary>
    /// <param name="chain">The IK chain to solve</param>
    /// <param name="target_model">Target position in model space</param>
    /// <param name="weight">IK weight (0-1)</param>
    /// <param name="soften">Joint softness (0-0.999)</param>
    /// <param name="twist_angle">Twist angle in radians</param>
    /// <param name="models">Current model-space transforms</param>
    /// <param name="locals">Local transforms to modify (SoA format)</param>
    /// <param name="min_dirty_joint">Optional output: minimum joint index modified</param>
    /// <returns>True if IK was successfully applied</returns>
    bool SolveLimbIk(
        LimbIkChain& chain,
        const ozz::math::Float3& target_model,
        float weight,
        float soften,
        float twist_angle,
        ozz::span<const ozz::math::Float4x4> models,
        ozz::span<ozz::math::SoaTransform> locals,
        int* min_dirty_joint = nullptr);

    /// <summary>
    /// Compute the target position for a chain based on its offset and role
    /// </summary>
    ozz::math::Float3 ComputeChainTarget(
        const LimbIkChain& chain,
        ozz::span<const ozz::math::Float4x4> models,
        float foot_ground_height = 0.f,
        float crouch_offset = 0.f,
        bool crouch_affects_arms = false) const;

    /// <summary>
    /// Apply target offset to a chain (used for interactive dragging)
    /// </summary>
    void ApplyDraggedTarget(
        LimbIkChain& chain,
        const ozz::math::Float3& target_world,
        ozz::span<const ozz::math::Float4x4> models);

    // Accessors for IK chains
    LimbIkChain& GetLeftLegChain() { return left_leg_chain_; }
    LimbIkChain& GetRightLegChain() { return right_leg_chain_; }
    LimbIkChain& GetLeftArmChain() { return left_arm_chain_; }
    LimbIkChain& GetRightArmChain() { return right_arm_chain_; }

    const LimbIkChain& GetLeftLegChain() const { return left_leg_chain_; }
    const LimbIkChain& GetRightLegChain() const { return right_leg_chain_; }
    const LimbIkChain& GetLeftArmChain() const { return left_arm_chain_; }
    const LimbIkChain& GetRightArmChain() const { return right_arm_chain_; }

    bool IsLegIkAvailable() const { return leg_ik_available_; }
    bool IsArmIkAvailable() const { return arm_ik_available_; }
    bool IsInitialized() const { return initialized_; }

private:
    /// <summary>
    /// Find a joint index by searching for one of several candidate names
    /// </summary>
    int FindJointIndexByNames(
        ozz::span<const char* const> joint_names,
        std::initializer_list<const char*> candidates) const;

    /// <summary>
    /// Resolve the mid_axis and pole_vector for a chain from bind pose
    /// </summary>
    void ResolveChainAxes(
        LimbIkChain& chain,
        ozz::span<const ozz::math::Float4x4> bind_pose_models);

    /// <summary>
    /// Populate a chain by searching for bones and resolving axes
    /// </summary>
    void PopulateChain(
        LimbIkChain& chain,
        LimbIkChain::Role role,
        ozz::span<const char* const> joint_names,
        ozz::span<const ozz::math::Float4x4> bind_pose_models,
        std::initializer_list<const char*> start_candidates,
        std::initializer_list<const char*> mid_candidates,
        std::initializer_list<const char*> end_candidates);

    // IK chains
    LimbIkChain left_leg_chain_;
    LimbIkChain right_leg_chain_;
    LimbIkChain left_arm_chain_;
    LimbIkChain right_arm_chain_;

    // Availability flags
    bool leg_ik_available_ = false;
    bool arm_ik_available_ = false;
    bool initialized_ = false;
};

} // namespace animation
} // namespace xray
