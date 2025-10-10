#pragma once

#include "IParticleSystem.h"
#include "../Components.h"
#include "xrParticles/psystem.h"
#include <entt/entt.hpp>

namespace ParticleECS
{

/**
 * @brief DeathSystem - Handles particle death callbacks and entity removal
 *
 * This system processes all particles marked for death:
 * 1. Calls the OnDeadParticleCB callback if set
 * 2. Removes the entity from the registry
 *
 * The callbacks are set by the effect and stored in this system.
 *
 * Execution order: SystemOrder::Death (300) - Runs after LifetimeSystem
 */
class PARTICLES_API DeathSystem : public IParticleSystem
{
public:
    DeathSystem() = default;
    ~DeathSystem() override = default;

    void Update(entt::registry& registry, float dt) override;
    const char* GetName() const override { return "DeathSystem"; }
    int GetExecutionOrder() const override { return SystemOrder::Death; }

    // Set callbacks for particle death
    void SetCallbacks(PAPI::OnDeadParticleCB death_callback, void* owner, u32 param)
    {
        m_deathCallback = death_callback;
        m_owner = owner;
        m_param = param;
    }

private:
    PAPI::OnDeadParticleCB m_deathCallback = nullptr;
    void* m_owner = nullptr;
    u32 m_param = 0;
};

} // namespace ParticleECS
