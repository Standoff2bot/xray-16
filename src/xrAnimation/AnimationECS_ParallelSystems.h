#pragma once

#include "AnimationECS_Components.h"
#include "AnimationECS_Systems.h"
#include "AnimationECS_IK.h"
#include "AnimationECS_Performance.h"
#include "entt/entt.hpp"
#include "xrCore/Threading/ParallelForEach.hpp"

// C++17 parallel algorithms
#include <execution>
#include <algorithm>

namespace AnimationECS {

// Global toggle for parallel implementation
enum class ParallelImplementation
{
    XRayTaskSystem,     // Use X-Ray's xr_parallel_for_each
    StdExecution        // Use std::for_each with std::execution::par
};

inline ParallelImplementation g_parallel_implementation = ParallelImplementation::XRayTaskSystem;

//-----------------------------------------------------------------------------
// ParallelAnimationProcessor
// Processes multiple animated entities in parallel using X-Ray's task system
// Singleton with reusable buffers to avoid per-frame allocations
//-----------------------------------------------------------------------------
class ParallelAnimationProcessor
{
private:
    // Reusable buffers to avoid per-frame allocations
    xr_vector<entt::entity> m_entity_cache;
    xr_vector<entt::entity> m_blend_entity_cache;

    // Singleton
    ParallelAnimationProcessor() = default;
    ~ParallelAnimationProcessor() = default;
    ParallelAnimationProcessor(const ParallelAnimationProcessor&) = delete;
    ParallelAnimationProcessor& operator=(const ParallelAnimationProcessor&) = delete;

public:
    static ParallelAnimationProcessor& Instance()
    {
        static ParallelAnimationProcessor instance;
        return instance;
    }

    // Process all animation systems in parallel
    void UpdateParallel(entt::registry& registry, float dt)
    {
        auto& profiler = GetPerformanceProfiler();
        PerformanceTimer total_timer;

        // Collect all entities that need animation updates
        auto view = registry.view<AnimationState, AnimationController, AnimationBuffers>();

        // Reuse cached vector instead of allocating new one
        m_entity_cache.clear();
        m_entity_cache.reserve(view.size_hint());

        for (auto entity : view)
        {
            m_entity_cache.push_back(entity);
        }

        if (profiler.IsEnabled())
        {
            profiler.GetStats().total_entities += m_entity_cache.size();
            profiler.GetStats().parallel_mode = true;
        }

        if (m_entity_cache.empty())
        {
            if (profiler.IsEnabled())
            {
                profiler.GetStats().total_update_time += total_timer.ElapsedMs();
            }
            return;
        }

        // Phase 1: Sample animations in parallel (SIMD-friendly, no shared state)
        PerformanceTimer sampling_timer;

        auto sampling_lambda = [&](entt::entity entity)
        {
            auto& state = view.get<AnimationState>(entity);
            auto& controller = view.get<AnimationController>(entity);
            auto& buffers = view.get<AnimationBuffers>(entity);

            // Skip if not playing or no animation
            if (!state.is_playing || !controller.animation || !buffers.IsInitialized())
                return;

            // Update time
            state.current_time += dt * controller.playback_speed;

            // Calculate time ratio
            const float duration = controller.animation->duration();
            if (duration > 0.0f)
            {
                state.time_ratio = state.current_time / duration;

                // Handle looping
                if (state.time_ratio > 1.0f)
                {
                    if (state.is_looping)
                    {
                        state.current_time = fmodf(state.current_time, duration);
                        state.time_ratio = fmodf(state.time_ratio, 1.0f);
                    }
                    else
                    {
                        state.current_time = duration;
                        state.time_ratio = 1.0f;
                        state.is_playing = false;
                    }
                }
            }

            // Sample animation
            ozz::animation::SamplingJob sampling_job;
            sampling_job.animation = controller.animation;
            sampling_job.context = &buffers.context;
            sampling_job.ratio = state.time_ratio;
            sampling_job.output = ozz::make_span(buffers.locals);

            // Note: Sampling failures removed from hot path - check profiler stats instead
            sampling_job.Run();
        };

        // Execute based on selected parallel implementation
        if (g_parallel_implementation == ParallelImplementation::StdExecution)
        {
            std::for_each(std::execution::par, m_entity_cache.begin(), m_entity_cache.end(), sampling_lambda);
        }
        else
        {
            xr_parallel_for_each(m_entity_cache, sampling_lambda);
        }

        if (profiler.IsEnabled())
        {
            profiler.GetStats().sampling_time += sampling_timer.ElapsedMs();
        }

        // Phase 2: Blending (if needed, process entities with BlendState)
        PerformanceTimer blending_timer;
        auto blend_view = registry.view<AnimationBuffers, BlendState, AnimationController>();

        // Reuse cached vector for blend entities
        m_blend_entity_cache.clear();
        m_blend_entity_cache.reserve(blend_view.size_hint());

        for (auto entity : blend_view)
        {
            m_blend_entity_cache.push_back(entity);
        }

        if (profiler.IsEnabled())
        {
            profiler.GetStats().blending_entities += m_blend_entity_cache.size();
        }

        if (!m_blend_entity_cache.empty())
        {
            auto blending_lambda = [&](entt::entity entity)
            {
                auto& buffers = blend_view.get<AnimationBuffers>(entity);
                auto& blend_state = blend_view.get<BlendState>(entity);
                auto& controller = blend_view.get<AnimationController>(entity);

                // Skip if no layers to blend
                if (!blend_state.HasLayers() || !controller.skeleton)
                    return;

                // Setup blending job
                ozz::animation::BlendingJob blend_job;
                blend_job.layers = ozz::make_span(blend_state.layers);
                blend_job.rest_pose = controller.skeleton->joint_rest_poses();
                blend_job.output = ozz::make_span(buffers.locals);

                // Note: Blending failures removed from hot path
                blend_job.Run();
            };

            // Execute based on selected parallel implementation
            if (g_parallel_implementation == ParallelImplementation::StdExecution)
            {
                std::for_each(std::execution::par, m_blend_entity_cache.begin(), m_blend_entity_cache.end(), blending_lambda);
            }
            else
            {
                xr_parallel_for_each(m_blend_entity_cache, blending_lambda);
            }
        }

        if (profiler.IsEnabled())
        {
            profiler.GetStats().blending_time += blending_timer.ElapsedMs();
        }

        // Phase 3: Local-to-Model transformation in parallel
        PerformanceTimer ltm_timer;

        auto ltm_lambda = [&](entt::entity entity)
        {
            auto& controller = view.get<AnimationController>(entity);
            auto& buffers = view.get<AnimationBuffers>(entity);

            // Skip if no skeleton or uninitialized
            if (!controller.skeleton || !buffers.IsInitialized())
                return;

            // Setup local-to-model job
            ozz::animation::LocalToModelJob ltm_job;
            ltm_job.skeleton = controller.skeleton;
            ltm_job.input = ozz::make_span(buffers.locals);
            ltm_job.output = ozz::make_span(buffers.models);

            // Note: LocalToModel failures removed from hot path
            ltm_job.Run();
        };

        // Execute based on selected parallel implementation
        if (g_parallel_implementation == ParallelImplementation::StdExecution)
        {
            std::for_each(std::execution::par, m_entity_cache.begin(), m_entity_cache.end(), ltm_lambda);
        }
        else
        {
            xr_parallel_for_each(m_entity_cache, ltm_lambda);
        }

        if (profiler.IsEnabled())
        {
            profiler.GetStats().local_to_model_time += ltm_timer.ElapsedMs();
        }

        // Phase 4: IK Solving (after LocalToModel, before callbacks)
        // IK works in model space, so must run after local-to-model conversion
        // Can potentially be parallelized per-entity, but sequential for now
        PerformanceTimer ik_timer;

        IKSolverSystem::Update(registry);

        if (profiler.IsEnabled())
        {
            profiler.GetStats().local_to_model_time += ik_timer.ElapsedMs(); // IK time added to LTM for now
        }

        // Phase 5: Callbacks (MUST run on main thread sequentially)
        // This is intentionally NOT parallelized due to potential side effects
        PerformanceTimer callback_timer;

        AnimationCallbackSystem::Update(registry);

        if (profiler.IsEnabled())
        {
            profiler.GetStats().callback_time += callback_timer.ElapsedMs();
            profiler.GetStats().total_update_time += total_timer.ElapsedMs();
        }
    }
};

//-----------------------------------------------------------------------------
// Parallel-aware AnimationUpdateOrchestrator
// Chooses between sequential and parallel execution based on entity count
//-----------------------------------------------------------------------------
class ParallelAnimationOrchestrator
{
public:
    // Threshold for switching to parallel execution
    static constexpr size_t PARALLEL_THRESHOLD = 4;

    static void Update(entt::registry& registry, float dt, bool force_parallel = false)
    {
        auto view = registry.view<AnimationState, AnimationController, AnimationBuffers>();
        const size_t entity_count = view.size_hint();

        // Use parallel processing if we have enough entities to benefit
        if (force_parallel || entity_count >= PARALLEL_THRESHOLD)
        {
            ParallelAnimationProcessor::Instance().UpdateParallel(registry, dt);
        }
        else
        {
            // Sequential path for small entity counts (less overhead)
            AnimationUpdateOrchestrator::Update(registry, dt);
        }
    }
};

} // namespace AnimationECS
