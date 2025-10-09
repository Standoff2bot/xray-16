# Particle System ECS Migration Plan

## Overview
This document outlines the plan to migrate the X-Ray particle system to use ENTT (Entity Component System) while maintaining backward compatibility with the vanilla implementation through a runtime toggle.

## Current Architecture Analysis

### Core Components
1. **CParticleManager** - Manages particle effects and action lists
   - Maintains vectors of `ParticleEffect*` and `ParticleActions*`
   - Thread-safe with locks
   - Interface: IParticleManager

2. **ParticleEffect** - Container for particles
   - Array-based storage (`Particle* particles`)
   - Properties: `p_count`, `max_particles`, `particles_allocated`
   - Callbacks: birth/death events
   - Operations: Add, Remove, Resize

3. **Particle** - Individual particle data (64 bytes)
   ```cpp
   struct Particle {
       Rotation rot;      // 4 bytes
       pVector pos;       // 12 bytes (position)
       pVector posB;      // 12 bytes (secondary position)
       pVector vel;       // 12 bytes (velocity)
       pVector size;      // 12 bytes
       u32 color;         // 4 bytes
       float age;         // 4 bytes
       u16 frame;         // 2 bytes
       Flags16 flags;     // 2 bytes (ANIMATE_CCW)
   };
   ```

4. **ParticleActions** - Collection of actions/behaviors
   - Vector of `ParticleAction*`
   - Each action executes on entire particle effect
   - Thread-safe locking mechanism
   - 26 action types (gravity, damping, bounce, turbulence, etc.)

5. **ParticleAction** - Base class for particle behaviors
   - `Execute(ParticleEffect* pe, float dt, float& m_max)` - updates particles
   - `Transform(const Fmatrix& m)` - transforms action parameters
   - Derived classes: PAGravity, PADamping, PABounce, etc.

### Data Flow
1. Create effect → Allocate particle array
2. Create action list → Store list of behaviors
3. Play effect → Execute all actions on particle array each frame
4. Update → Each action iterates particles and modifies them
5. Render → Render all particles in effect

## ECS Architecture Design

### Component Design

Each particle will be an ENTT entity with the following components:

```cpp
namespace ParticleECS {

// Core particle components
struct PositionComponent {
    pVector pos;      // Current position
    pVector posB;     // Secondary position (for trails/ribbons)
};

struct VelocityComponent {
    pVector vel;
};

struct VisualComponent {
    pVector size;
    Rotation rot;
    u32 color;
    u16 frame;
    Flags16 flags;  // ANIMATE_CCW, etc.
};

struct LifetimeComponent {
    float age;           // Current age
    float maxAge;        // Max lifetime (optional, 0 = infinite)
};

// Effect grouping - which effect does this particle belong to?
struct EffectComponent {
    int effectId;        // Reference to effect
    void* owner;         // Owner object
    u32 param;          // User parameter
};

// Optional components for specific behaviors
struct GravityComponent {
    pVector gravity;     // Gravity vector
};

struct DampingComponent {
    pVector damping;     // Damping coefficients per axis
    float scale;         // Overall damping scale
};

struct RotationVelocityComponent {
    float rotVel;        // Rotation speed
};

// Tag components for particle states
struct MarkedForDeath {};  // Particle will be removed next frame
struct NewlyBorn {};       // Particle just created (for callbacks)

} // namespace ParticleECS
```

### System Design

Systems will replace the current ParticleAction classes:

```cpp
namespace ParticleECS {

class IParticleSystem {
public:
    virtual ~IParticleSystem() = default;
    virtual void Update(entt::registry& registry, float dt) = 0;
    virtual const char* GetName() const = 0;
};

// Core systems
class MovementSystem : public IParticleSystem {
    // Updates position based on velocity
    void Update(entt::registry& registry, float dt) override;
};

class LifetimeSystem : public IParticleSystem {
    // Ages particles, marks old ones for death
    void Update(entt::registry& registry, float dt) override;
};

class GravitySystem : public IParticleSystem {
    // Applies gravity to particles with GravityComponent
    void Update(entt::registry& registry, float dt) override;
};

class DampingSystem : public IParticleSystem {
    // Dampens velocity
    void Update(entt::registry& registry, float dt) override;
};

// ... more systems for each action type

class DeathSystem : public IParticleSystem {
    // Handles callbacks and removes dead particles
    void Update(entt::registry& registry, float dt) override;
};

} // namespace ParticleECS
```

### Manager Design

```cpp
namespace ParticleECS {

class ECSParticleEffect {
public:
    int effectId;
    u32 maxParticles;
    entt::registry registry;  // Local registry for this effect
    xr_vector<IParticleSystem*> systems;

    OnBirthParticleCB birthCallback;
    OnDeadParticleCB deathCallback;
    void* owner;
    u32 param;

    ECSParticleEffect(int id, u32 max_particles);
    ~ECSParticleEffect();

    void Update(float dt);
    void AddParticle(const pVector& pos, const pVector& posB,
                    const pVector& size, const pVector& rot,
                    const pVector& vel, u32 color,
                    float age, u16 frame, u16 flags);
    void RemoveParticle(entt::entity entity);
    u32 GetParticleCount() const;
};

class ECSParticleManager : public IParticleManager {
private:
    xr_vector<ECSParticleEffect*> effects;
    // Action lists map to system configurations
    xr_map<int, xr_vector<IParticleSystem*>> actionListSystems;
    Lock pm_lock;

public:
    // Implement IParticleManager interface
    int CreateEffect(u32 max_particles) override;
    void DestroyEffect(int effect_id) override;
    int CreateActionList() override;
    void DestroyActionList(int alist_id) override;

    void PlayEffect(int effect_id, int alist_id) override;
    void StopEffect(int effect_id, int alist_id, bool deferred) override;

    void Update(int effect_id, int alist_id, float dt) override;
    void Render(int effect_id) override;
    // ... etc
};

} // namespace ParticleECS
```

## Dual Implementation Strategy

### 1. Console Variable
Add a console command to toggle between implementations:

```cpp
// In console variables
xr_token particle_implementation_token[] = {
    { "vanilla", 0 },
    { "ecs",     1 },
    { nullptr,   0 }
};

// Console variable
extern PARTICLES_API int ps_particle_implementation; // 0 = vanilla, 1 = ECS
```

### 2. Factory Pattern
Create a factory that returns the appropriate manager:

```cpp
// In particle_manager.cpp
namespace PAPI {

IParticleManager* ParticleManager() {
    static IParticleManager* s_manager = nullptr;

    if (!s_manager) {
        if (ps_particle_implementation == 1) {
            s_manager = new ParticleECS::ECSParticleManager();
        } else {
            s_manager = new CParticleManager();
        }
    }

    return s_manager;
}

void RecreateParticleManager() {
    // For runtime switching (requires cleanup)
    IParticleManager* old_manager = ParticleManager();
    xr_delete(old_manager);
    // Create new one based on current setting
}

} // namespace PAPI
```

### 3. Abstraction Layer
Both implementations will conform to `IParticleManager` interface, ensuring:
- Same API surface
- Compatible behavior
- Transparent switching (with caveats about active particles)

## Implementation Phases

### Phase 1: Foundation
- [ ] Add console variable `ps_particle_implementation`
- [ ] Create `ParticleECS` namespace and directory structure
- [ ] Define all component structures
- [ ] Create base `IParticleSystem` interface

### Phase 2: Core Systems
- [ ] Implement `MovementSystem`
- [ ] Implement `LifetimeSystem`
- [ ] Implement `DeathSystem` (with callbacks)
- [ ] Create `ECSParticleEffect` class

### Phase 3: Action Systems
Map each ParticleAction to an ECS system:
- [ ] `PAGravity` → `GravitySystem`
- [ ] `PADamping` → `DampingSystem`
- [ ] `PABounce` → `BounceSystem`
- [ ] `PAKillOld` → Part of `LifetimeSystem`
- [ ] `PAMove` → `MovementSystem`
- [ ] `PASource` → `ParticleSpawnSystem`
- [ ] `PASink` → `ParticleSinkSystem`
- [ ] `PATargetColor` → `ColorInterpolationSystem`
- [ ] `PATargetSize` → `SizeInterpolationSystem`
- [ ] `PATargetVelocity` → `VelocityTargetSystem`
- [ ] `PAVortex` → `VortexSystem`
- [ ] `PATurbulence` → `TurbulenceSystem`
- [ ] ... (complete all 26 action types)

### Phase 4: Manager Implementation
- [ ] Implement `ECSParticleManager`
- [ ] Implement effect creation/destruction
- [ ] Implement action list → system mapping
- [ ] Add locking for thread safety

### Phase 5: Rendering Integration
- [ ] Create particle data extraction for rendering
- [ ] Ensure compatibility with existing render pipeline
- [ ] Handle particle sorting for transparency

### Phase 6: Testing & Optimization
- [ ] Unit tests for each system
- [ ] Integration tests with game
- [ ] Performance profiling (ECS vs vanilla)
- [ ] Memory usage analysis
- [ ] Multithreading opportunities (ENTT supports this well)

## Benefits of ECS Approach

### Performance
1. **Cache coherency**: Components stored contiguously in memory
2. **SIMD opportunities**: Can batch-process components
3. **Parallelization**: ENTT supports multithreaded system execution
4. **Sparse sets**: Only iterate particles that have specific components

### Flexibility
1. **Composition**: Easy to add new behaviors by adding components
2. **Runtime changes**: Can add/remove components dynamically
3. **Querying**: Fast queries for particles with specific component sets
4. **Serialization**: ENTT provides snapshot/serialization support

### Maintainability
1. **Separation of concerns**: Each system handles one behavior
2. **Testability**: Systems can be tested in isolation
3. **Extensibility**: New particle types = new component combinations
4. **Data-oriented**: Logic is separate from data

## Migration Risks & Mitigation

### Risk 1: Callback Compatibility
- **Issue**: Birth/death callbacks expect array indices
- **Solution**: Store entity handle in callback or provide wrapper

### Risk 2: Performance Regression
- **Issue**: ECS might be slower for small particle counts
- **Solution**: Profile both, optimize hot paths, consider batch size tuning

### Risk 3: Memory Overhead
- **Issue**: ENTT has some overhead per entity
- **Solution**: Profile memory usage, consider pooling

### Risk 4: Thread Safety
- **Issue**: Current system has explicit locks
- **Solution**: ENTT registries are not thread-safe by default, add locking or use per-thread registries

### Risk 5: Rendering Data Access
- **Issue**: Renderer expects contiguous particle array
- **Solution**: Create a temporary buffer in Render() or use ENTT views for direct iteration

## Testing Strategy

### Unit Tests
- Each system individually
- Component creation/destruction
- Callback invocations
- System ordering

### Integration Tests
- Full effect playback
- Multiple effects simultaneously
- Action list changes at runtime
- Thread safety under load

### Visual Tests
- Side-by-side comparison of vanilla vs ECS
- All particle effects in game
- Performance profiling in various scenarios

### Regression Tests
- Ensure vanilla implementation still works
- Toggle switch works correctly
- No crashes when switching (after cleanup)

## File Structure

```
src/xrParticles/
├── ecs/
│   ├── Components.h              // All component definitions
│   ├── Systems/
│   │   ├── IParticleSystem.h
│   │   ├── MovementSystem.h/.cpp
│   │   ├── LifetimeSystem.h/.cpp
│   │   ├── GravitySystem.h/.cpp
│   │   ├── DampingSystem.h/.cpp
│   │   └── ... (one file per system)
│   ├── ECSParticleEffect.h/.cpp
│   ├── ECSParticleManager.h/.cpp
│   └── ParticleSystemFactory.h/.cpp  // System creation based on actions
├── particle_manager.h/.cpp       // Factory for manager selection
├── particle_effect.h/.cpp        // Vanilla implementation
└── ECS_PARTICLE_MIGRATION_PLAN.md // This document
```

## Console Commands

```
// Set particle implementation (requires restart or manager recreation)
ps_particle_implementation [0|1]  // 0=vanilla, 1=ecs

// Runtime switching (with warnings about losing active particles)
ps_particle_switch_implementation

// Debug info
ps_particle_stats  // Show particle counts, memory usage, etc.
ps_particle_debug [0|1]  // Enable/disable debug visualization
```

## Performance Considerations

### Vanilla Approach
- **Pros**: Simple, proven, low overhead for small counts
- **Cons**: Poor cache usage, hard to parallelize, all particles always processed

### ECS Approach
- **Pros**: Excellent cache usage, easy to parallelize, sparse iteration
- **Cons**: Overhead per entity, complex for simple cases

### Hybrid Approach (Future)
Consider using vanilla for small effects (<100 particles) and ECS for large effects (>100 particles).

## Next Steps

1. Review and approve this design
2. Create feature branch `feature/particle-ecs`
3. Implement Phase 1 (foundation)
4. Iteratively implement remaining phases
5. Profile and optimize
6. Merge when both implementations are stable

## References

- ENTT documentation: https://github.com/skypjack/entt
- Current particle system: `src/xrParticles/`
- Similar ECS work: `src/xrAnimation/OzzKinematicsAnimated.cpp` (ENTT example in codebase)
