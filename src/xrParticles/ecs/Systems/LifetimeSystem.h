#pragma once

#include "IParticleSystem.h"
#include "../Components.h"
#include "xrParticles/psystem.h"
#include <entt/entt.hpp>

namespace ParticleECS
{

/**
 * @brief LifetimeSystem - Ages particles and marks old ones for death
 *
 * This system increments each particle's age by dt and checks if it has exceeded
 * its maximum lifetime. Particles that are too old are tagged with MarkedForDeath
 * for the DeathSystem to remove.
 *
 * Execution order: SystemOrder::Lifetime (250) - Runs before DeathSystem
 */
class PARTICLES_API LifetimeSystem : public IParticleSystem
{
public:
    LifetimeSystem() = default;
    ~LifetimeSystem() override = default;

    void Update(entt::registry& registry, float dt) override;
    const char* GetName() const override { return "LifetimeSystem"; }
    int GetExecutionOrder() const override { return SystemOrder::Lifetime; }
};

} // namespace ParticleECS
