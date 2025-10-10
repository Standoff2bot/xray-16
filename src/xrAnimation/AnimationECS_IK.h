#pragma once

#include "xrCore/xrCore.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/maths/vec_float.h"
#include "ozz/base/containers/vector.h"
#include "ozz/base/span.h"
#include "entt/entt.hpp"
#include <string>

namespace AnimationECS {

//=============================================================================
// IK COMPONENTS (Data-only, ECS compatible)
//=============================================================================

//-----------------------------------------------------------------------------
// LimbIKChain Component
// Represents a two-bone IK chain (shoulder-elbow-wrist or hip-knee-ankle)
//-----------------------------------------------------------------------------
struct LimbIKChain
{
    enum class Role : u8
    {
        Leg = 0,
        Arm = 1,
    };

    // Chain configuration
    Role role{Role::Leg};                       // Chain type (determines bend direction)

    // Joint indices in skeleton
    s16 start{-1};                              // Shoulder/Hip joint index
    s16 mid{-1};                                // Elbow/Knee joint index
    s16 end{-1};                                // Wrist/Ankle joint index

    // IK solving parameters (calculated from bind pose)
    ozz::math::SimdFloat4 mid_axis;             // Bend axis (which way elbow/knee bends)
    ozz::math::SimdFloat4 pole_vector;          // Stable pole direction from bind pose

    // Target control
    ozz::math::Float3 target_offset{0.f, 0.f, 0.f};  // Offset from animated position
    bool enabled{true};                         // Whether to apply IK to this chain
    bool reached{false};                        // Whether IK reached target last frame

    // Debug visualization
    ozz::math::Float3 debug_target{0.f, 0.f, 0.f};
    bool debug_target_valid{false};

    // Cached names for debugging
    std::string start_name;
    std::string mid_name;
    std::string end_name;
    std::string label;                          // Display name (e.g., "Left arm")

    bool Valid() const { return start >= 0 && mid >= 0 && end >= 0; }
};

//-----------------------------------------------------------------------------
// IKConfiguration Component
// Container for all IK chains on an entity
//-----------------------------------------------------------------------------
struct IKConfiguration
{
    // IK chains (optional - only exist if skeleton has appropriate bones)
    LimbIKChain left_leg;
    LimbIKChain right_leg;
    LimbIKChain left_arm;
    LimbIKChain right_arm;

    // Availability flags
    bool leg_ik_available{false};
    bool arm_ik_available{false};
    bool initialized{false};

    // IK solving parameters
    struct IKParams
    {
        float weight{1.0f};                     // IK weight (0-1)
        float soften{0.97f};                    // Joint softness (0-0.999)
        float twist_angle{0.f};                 // Twist angle in radians
    };

    IKParams leg_params;
    IKParams arm_params;

    // Ground/crouch parameters
    float foot_ground_height{0.f};
    float crouch_offset{0.f};
    bool crouch_affects_arms{false};

    bool IsInitialized() const { return initialized; }
    bool HasLegIK() const { return leg_ik_available; }
    bool HasArmIK() const { return arm_ik_available; }
};

//=============================================================================
// IK SYSTEMS (Logic operating on components)
//=============================================================================

//-----------------------------------------------------------------------------
// IKInitializationSystem
// Initializes IK chains by finding appropriate bones in skeleton
//-----------------------------------------------------------------------------
class IKInitializationSystem
{
public:
    /// <summary>
    /// Initialize IK configuration for an entity
    /// </summary>
    /// <param name="registry">ECS registry</param>
    /// <param name="entity">Entity to initialize IK for</param>
    /// <param name="joint_names">Skeleton joint names</param>
    /// <param name="bind_pose_models">Bind pose model-space transforms</param>
    static void Initialize(
        entt::registry& registry,
        entt::entity entity,
        ozz::span<const char* const> joint_names,
        ozz::span<const ozz::math::Float4x4> bind_pose_models);

private:
    static int FindJointIndexByNames(
        ozz::span<const char* const> joint_names,
        std::initializer_list<const char*> candidates);

    static void ResolveChainAxes(
        LimbIKChain& chain,
        ozz::span<const ozz::math::Float4x4> bind_pose_models);

    static void PopulateChain(
        LimbIKChain& chain,
        LimbIKChain::Role role,
        ozz::span<const char* const> joint_names,
        ozz::span<const ozz::math::Float4x4> bind_pose_models,
        std::initializer_list<const char*> start_candidates,
        std::initializer_list<const char*> mid_candidates,
        std::initializer_list<const char*> end_candidates);
};

//-----------------------------------------------------------------------------
// IKSolverSystem
// Solves IK for all chains on entities with IKConfiguration
//-----------------------------------------------------------------------------
class IKSolverSystem
{
public:
    /// <summary>
    /// Solve IK for all entities with IK configuration
    /// Called after animation sampling but before local-to-model conversion
    /// </summary>
    /// <param name="registry">ECS registry</param>
    static void Update(entt::registry& registry);

    /// <summary>
    /// Solve IK for a specific limb chain
    /// </summary>
    static bool SolveLimbIK(
        LimbIKChain& chain,
        const ozz::math::Float3& target_model,
        float weight,
        float soften,
        float twist_angle,
        ozz::span<const ozz::math::Float4x4> models,
        ozz::span<ozz::math::SoaTransform> locals,
        int* min_dirty_joint = nullptr);

    /// <summary>
    /// Compute target position for a chain based on its offset and role
    /// </summary>
    static ozz::math::Float3 ComputeChainTarget(
        const LimbIKChain& chain,
        ozz::span<const ozz::math::Float4x4> models,
        float foot_ground_height = 0.f,
        float crouch_offset = 0.f,
        bool crouch_affects_arms = false);

    /// <summary>
    /// Apply dragged target to a chain (for interactive control)
    /// </summary>
    static void ApplyDraggedTarget(
        LimbIKChain& chain,
        const ozz::math::Float3& target_world,
        ozz::span<const ozz::math::Float4x4> models);

private:
    static ozz::math::Float3 ExtractTranslation(const ozz::math::Float4x4& mat);
};

//-----------------------------------------------------------------------------
// IKDebugSystem
// Visualization and debugging for IK chains
//-----------------------------------------------------------------------------
class IKDebugSystem
{
public:
    /// <summary>
    /// Update debug visualization for IK chains
    /// </summary>
    static void UpdateDebugVisualization(entt::registry& registry);

    /// <summary>
    /// Render IK chain debug info (targets, poles, etc.)
    /// </summary>
    static void RenderDebugInfo(const IKConfiguration& ik_config,
                                ozz::span<const ozz::math::Float4x4> models);
};

} // namespace AnimationECS
