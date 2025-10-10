#include "../../stdafx.h"
#include "DeathSystem.h"

namespace ParticleECS
{

void DeathSystem::Update(entt::registry& registry, float dt)
{
    // Find all particles marked for death
    auto view = registry.view<MarkedForDeath>();

    // Collect entities to destroy (can't destroy during iteration)
    std::vector<entt::entity> to_destroy;
    to_destroy.reserve(16); // Typical small batch

    for (auto entity : view)
    {
        // Call death callback if set
        if (m_deathCallback != nullptr)
        {
            // Construct a Particle from components to pass to callback
            PAPI::Particle p;

            // Get components (some may not exist)
            if (auto* pos = registry.try_get<PositionComponent>(entity))
            {
                p.pos = pos->pos;
                p.posB = pos->posB;
            }
            if (auto* vel = registry.try_get<VelocityComponent>(entity))
            {
                p.vel = vel->vel;
            }
            if (auto* visual = registry.try_get<VisualComponent>(entity))
            {
                p.size = visual->size;
                p.rot = visual->rot;
                p.color = visual->color;
                p.frame = visual->frame;
                p.flags = visual->flags;
            }
            if (auto* lifetime = registry.try_get<LifetimeComponent>(entity))
            {
                p.age = lifetime->age;
            }

            // Use entity ID as particle index (cast to u32)
            u32 idx = static_cast<u32>(entt::to_integral(entity));

            // Call the death callback
            m_deathCallback(m_owner, m_param, p, idx);
        }

        // Mark for destruction
        to_destroy.push_back(entity);
    }

    // Now destroy all marked entities
    for (auto entity : to_destroy)
    {
        registry.destroy(entity);
    }
}

} // namespace ParticleECS
