#include "../../stdafx.h"
#include "LifetimeSystem.h"

namespace ParticleECS
{

void LifetimeSystem::Update(entt::registry& registry, float dt)
{
    // Process all active particles with lifetime component
    auto view = registry.view<LifetimeComponent, ActiveParticle>();

    for (auto entity : view)
    {
        auto& lifetime = view.get<LifetimeComponent>(entity);

        // Age the particle
        lifetime.age += dt;

        // Check if particle has exceeded its maximum age
        if (lifetime.age >= lifetime.maxAge)
        {
            // Mark for death - DeathSystem will handle removal
            registry.emplace_or_replace<MarkedForDeath>(entity);
        }
    }
}

} // namespace ParticleECS
