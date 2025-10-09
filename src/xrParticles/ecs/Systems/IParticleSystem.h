#pragma once

#include <entt/entt.hpp>

namespace ParticleECS
{

// ============================================================================
// Base interface for all particle systems
// ============================================================================

class IParticleSystem
{
public:
    virtual ~IParticleSystem() = default;

    // Main update function - called every frame
    // @param registry: ENTT registry containing all particle entities
    // @param dt: Delta time in seconds
    virtual void Update(entt::registry& registry, float dt) = 0;

    // Get system name for debugging/profiling
    virtual const char* GetName() const = 0;

    // System priority/execution order (lower = earlier)
    // Default is 100. Core systems should run first, cleanup systems last.
    virtual int GetExecutionOrder() const { return 100; }

    // Whether this system should run (can be disabled for debugging)
    virtual bool IsEnabled() const { return m_enabled; }
    virtual void SetEnabled(bool enabled) { m_enabled = enabled; }

protected:
    bool m_enabled = true;
};

// ============================================================================
// System execution order constants
// ============================================================================

namespace SystemOrder
{
    constexpr int PreUpdate = 0;        // Systems that run before everything
    constexpr int Input = 10;           // Input/spawn systems
    constexpr int Source = 20;          // Particle source/emission
    constexpr int Physics = 50;         // Physics/forces (gravity, damping, etc.)
    constexpr int Behavior = 100;       // Behavior systems (default)
    constexpr int Movement = 150;       // Movement/integration
    constexpr int Constraints = 200;    // Constraints (speed limits, bounds, etc.)
    constexpr int Lifetime = 250;       // Lifetime/aging
    constexpr int Death = 300;          // Death/removal
    constexpr int PostUpdate = 400;     // Systems that run after everything
}

// ============================================================================
// Helper macros for system definition
// ============================================================================

#define DECLARE_PARTICLE_SYSTEM(ClassName, SystemName, ExecutionOrder) \
    class ClassName : public IParticleSystem \
    { \
    public: \
        void Update(entt::registry& registry, float dt) override; \
        const char* GetName() const override { return SystemName; } \
        int GetExecutionOrder() const override { return ExecutionOrder; } \
    };

// ============================================================================
// System registry/factory support
// ============================================================================

// System factory function type
using SystemFactory = IParticleSystem* (*)();

// System registry for automatic registration
class SystemRegistry
{
public:
    static SystemRegistry& Instance()
    {
        static SystemRegistry instance;
        return instance;
    }

    void RegisterSystem(const char* name, SystemFactory factory)
    {
        m_factories[name] = factory;
    }

    IParticleSystem* CreateSystem(const char* name)
    {
        auto it = m_factories.find(name);
        if (it != m_factories.end())
            return it->second();
        return nullptr;
    }

    const xr_map<const char*, SystemFactory>& GetFactories() const { return m_factories; }

private:
    xr_map<const char*, SystemFactory> m_factories;
};

// Helper for automatic system registration
template<typename T>
struct SystemRegistrar
{
    SystemRegistrar(const char* name)
    {
        SystemRegistry::Instance().RegisterSystem(name, []() -> IParticleSystem* {
            return xr_new<T>();
        });
    }
};

// Macro for easy system registration
#define REGISTER_PARTICLE_SYSTEM(ClassName, SystemName) \
    static ParticleECS::SystemRegistrar<ClassName> s_##ClassName##_registrar(SystemName);

} // namespace ParticleECS
