#include "IPhysicsAdapter.h"
#include "xrCore/xrCore.h"

xr_map<PhysicsEngineType, PhysicsWorldFactory>& PhysicsEngineRegistry::GetFactories()
{
    static xr_map<PhysicsEngineType, PhysicsWorldFactory> s_factories;
    return s_factories;
}

void PhysicsEngineRegistry::RegisterEngine(PhysicsEngineType type, PhysicsWorldFactory factory)
{
    auto& factories = GetFactories();
    if (factories.find(type) != factories.end())
    {
        Msg("! Warning: Physics engine type %d already registered, overwriting", static_cast<int>(type));
    }
    factories[type] = factory;
    Msg("* Registered physics engine: %d", static_cast<int>(type));
}

IPhysicsWorld* PhysicsEngineRegistry::CreateWorld(PhysicsEngineType type)
{
    auto& factories = GetFactories();
    auto it = factories.find(type);
    if (it == factories.end())
    {
        Msg("! Error: Physics engine type %d not registered", static_cast<int>(type));
        return nullptr;
    }

    IPhysicsWorld* world = it->second();
    if (!world)
    {
        Msg("! Error: Failed to create physics world for engine type %d", static_cast<int>(type));
        return nullptr;
    }

    Msg("* Created physics world for engine type: %d", static_cast<int>(type));
    return world;
}

bool PhysicsEngineRegistry::IsEngineAvailable(PhysicsEngineType type)
{
    auto& factories = GetFactories();
    return factories.find(type) != factories.end();
}
