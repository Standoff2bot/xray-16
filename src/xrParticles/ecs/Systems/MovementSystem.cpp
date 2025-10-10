#include "../../stdafx.h"
#include "MovementSystem.h"

namespace ParticleECS
{

void MovementSystem::Update(entt::registry& registry, float dt)
{
    // Only process active particles with position and velocity
    auto view = registry.view<PositionComponent, VelocityComponent, ActiveParticle>();

    for (auto entity : view)
    {
        auto& pos = view.get<PositionComponent>(entity);
        const auto& vel = view.get<VelocityComponent>(entity);

        // Euler integration: position += velocity * dt
        pos.pos.x += vel.vel.x * dt;
        pos.pos.y += vel.vel.y * dt;
        pos.pos.z += vel.vel.z * dt;

        // Also update secondary position (used for rendering trails/motion blur)
        pos.posB.x += vel.vel.x * dt;
        pos.posB.y += vel.vel.y * dt;
        pos.posB.z += vel.vel.z * dt;
    }
}

} // namespace ParticleECS
