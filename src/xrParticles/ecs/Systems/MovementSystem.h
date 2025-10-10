#pragma once

#include "IParticleSystem.h"
#include "../Components.h"
#include "xrParticles/psystem.h"
#include <entt/entt.hpp>

namespace ParticleECS
{

/**
 * @brief MovementSystem - Integrates velocity into position each frame
 *
 * This system performs basic Euler integration: position += velocity * dt
 * It operates on all entities with Position, Velocity, and ActiveParticle components.
 *
 * Execution order: SystemOrder::Movement (150) - Runs after physics/behavior systems
 */
class PARTICLES_API MovementSystem : public IParticleSystem
{
public:
    MovementSystem() = default;
    ~MovementSystem() override = default;

    void Update(entt::registry& registry, float dt) override;
    const char* GetName() const override { return "MovementSystem"; }
    int GetExecutionOrder() const override { return SystemOrder::Movement; }
};

} // namespace ParticleECS
