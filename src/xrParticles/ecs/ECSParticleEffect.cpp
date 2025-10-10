#include "../stdafx.h"
#include "ECSParticleEffect.h"

namespace ParticleECS
{

ECSParticleEffect::ECSParticleEffect(u32 max_particles)
    : m_maxParticles(max_particles)
{
    // EnTT automatically manages capacity
    // No need to reserve in modern EnTT

    // Initialize systems
    InitializeSystems();
}

ECSParticleEffect::~ECSParticleEffect()
{
    // Registry cleans up entities automatically
    // Systems are stack-allocated, no manual cleanup needed
}

void ECSParticleEffect::InitializeSystems()
{
    // Add core systems to the list
    m_systems.push_back(&m_movementSystem);
    m_systems.push_back(&m_lifetimeSystem);
    m_systems.push_back(&m_deathSystem);

    // Sort systems by execution order
    SortSystems();
}

void ECSParticleEffect::SortSystems()
{
    // Sort systems by their execution order (lower = earlier)
    std::sort(m_systems.begin(), m_systems.end(),
        [](const IParticleSystem* a, const IParticleSystem* b) {
            return a->GetExecutionOrder() < b->GetExecutionOrder();
        });
}

bool ECSParticleEffect::AddParticle(
    const PAPI::pVector& pos, const PAPI::pVector& posB,
    const PAPI::pVector& size, const PAPI::pVector& rot,
    const PAPI::pVector& vel, u32 color, float age,
    u16 frame, u16 flags)
{
    // Check if we've reached max particles
    if (GetParticleCount() >= m_maxParticles)
        return false;

    // Create new entity
    entt::entity entity = m_registry.create();

    // Add core components
    m_registry.emplace<PositionComponent>(entity, pos, posB);
    m_registry.emplace<VelocityComponent>(entity, vel);

    // Visual component
    PAPI::Rotation rotation;
    rotation.x = rot.x;
    ::Flags16 particle_flags;
    particle_flags.assign(flags);
    m_registry.emplace<VisualComponent>(entity, size, rotation, color, frame, particle_flags);

    // Lifetime component (maxAge = 0 means infinite, controlled by actions)
    m_registry.emplace<LifetimeComponent>(entity, age, 0.0f);

    // Effect component (for callbacks)
    m_registry.emplace<EffectComponent>(entity, -1, m_owner, m_param);

    // Tag as active and newly born
    m_registry.emplace<ActiveParticle>(entity);
    m_registry.emplace<NewlyBorn>(entity);

    // Call birth callback if set
    if (m_birthCallback != nullptr)
    {
        PAPI::Particle p;
        p.pos = pos;
        p.posB = posB;
        p.vel = vel;
        p.size = size;
        p.rot = rotation;
        p.color = color;
        p.age = age;
        p.frame = frame;
        p.flags.assign(flags);

        u32 idx = static_cast<u32>(entt::to_integral(entity));
        m_birthCallback(m_owner, m_param, p, idx);
    }

    return true;
}

void ECSParticleEffect::RemoveParticle(entt::entity entity)
{
    if (m_registry.valid(entity))
    {
        // Mark for death rather than immediate removal
        m_registry.emplace_or_replace<MarkedForDeath>(entity);
    }
}

void ECSParticleEffect::Resize(u32 max_particles)
{
    m_maxParticles = max_particles;
    // Note: We don't shrink the registry if new size is smaller
    // Particles will naturally be culled when limit is reached
}

void ECSParticleEffect::Update(float dt)
{
    // Update all systems in execution order
    for (auto* system : m_systems)
    {
        if (system->IsEnabled())
        {
            system->Update(m_registry, dt);
        }
    }

    // Remove NewlyBorn tag from all particles that were born last frame
    auto newborn_view = m_registry.view<NewlyBorn>();
    for (auto entity : newborn_view)
    {
        m_registry.remove<NewlyBorn>(entity);
    }
}

void ECSParticleEffect::SetCallbacks(
    PAPI::OnBirthParticleCB birth_cb,
    PAPI::OnDeadParticleCB death_cb,
    void* owner, u32 param)
{
    m_birthCallback = birth_cb;
    m_deathCallback = death_cb;
    m_owner = owner;
    m_param = param;

    // Update death system with new callback
    m_deathSystem.SetCallbacks(death_cb, owner, param);
}

void ECSParticleEffect::GetParticles(PAPI::Particle*& particles, u32& count)
{
    // Convert ECS particles to vanilla array format for compatibility
    count = GetParticleCount();
    m_particleBuffer.resize(count);

    u32 idx = 0;
    auto view = m_registry.view<PositionComponent, VelocityComponent, VisualComponent, LifetimeComponent>();

    for (auto entity : view)
    {
        if (idx >= count)
            break;

        const auto& pos = view.get<PositionComponent>(entity);
        const auto& vel = view.get<VelocityComponent>(entity);
        const auto& visual = view.get<VisualComponent>(entity);
        const auto& lifetime = view.get<LifetimeComponent>(entity);

        PAPI::Particle& p = m_particleBuffer[idx];
        p.pos = pos.pos;
        p.posB = pos.posB;
        p.vel = vel.vel;
        p.size = visual.size;
        p.rot = visual.rot;
        p.color = visual.color;
        p.frame = visual.frame;
        p.flags = visual.flags;
        p.age = lifetime.age;

        idx++;
    }

    particles = m_particleBuffer.data();
    count = idx;
}

} // namespace ParticleECS
