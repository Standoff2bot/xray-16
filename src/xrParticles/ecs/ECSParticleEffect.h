#pragma once

#include "Components.h"
#include "Systems/IParticleSystem.h"
#include "Systems/MovementSystem.h"
#include "Systems/LifetimeSystem.h"
#include "Systems/DeathSystem.h"
#include "xrParticles/psystem.h"
#include <entt/entt.hpp>
#include <vector>
#include <algorithm>

namespace ParticleECS
{

/**
 * @brief ECSParticleEffect - ECS-based implementation of particle effects
 *
 * This class manages a particle effect using the ECS architecture:
 * - Owns an entt::registry for all particles in this effect
 * - Owns and manages particle systems (MovementSystem, LifetimeSystem, etc.)
 * - Provides methods compatible with vanilla ParticleEffect interface
 * - Handles particle lifecycle (spawn, update, death)
 */
class PARTICLES_API ECSParticleEffect
{
public:
    explicit ECSParticleEffect(u32 max_particles);
    ~ECSParticleEffect();

    // Particle management
    bool AddParticle(const PAPI::pVector& pos, const PAPI::pVector& posB,
                     const PAPI::pVector& size, const PAPI::pVector& rot,
                     const PAPI::pVector& vel, u32 color, float age = 0.0f,
                     u16 frame = 0, u16 flags = 0);

    void RemoveParticle(entt::entity entity);
    void Resize(u32 max_particles);

    // Update all systems
    void Update(float dt);

    // Callbacks
    void SetCallbacks(PAPI::OnBirthParticleCB birth_cb, PAPI::OnDeadParticleCB death_cb,
                      void* owner, u32 param);

    // Getters
    u32 GetParticleCount() const {
        // Count entities with ActiveParticle component
        auto view = m_registry.view<ActiveParticle>();
        return static_cast<u32>(std::distance(view.begin(), view.end()));
    }
    u32 GetMaxParticles() const { return m_maxParticles; }
    entt::registry& GetRegistry() { return m_registry; }
    const entt::registry& GetRegistry() const { return m_registry; }

    // Get particles as vanilla array (for compatibility/rendering)
    void GetParticles(PAPI::Particle*& particles, u32& count);

private:
    // Entity component system
    entt::registry m_registry;

    // Particle limits
    u32 m_maxParticles;

    // Core systems (always present)
    MovementSystem m_movementSystem;
    LifetimeSystem m_lifetimeSystem;
    DeathSystem m_deathSystem;

    // All systems sorted by execution order
    std::vector<IParticleSystem*> m_systems;

    // Callbacks
    PAPI::OnBirthParticleCB m_birthCallback = nullptr;
    PAPI::OnDeadParticleCB m_deathCallback = nullptr;
    void* m_owner = nullptr;
    u32 m_param = 0;

    // Temporary buffer for GetParticles compatibility
    std::vector<PAPI::Particle> m_particleBuffer;

    // Helper methods
    void InitializeSystems();
    void SortSystems();
};

} // namespace ParticleECS
