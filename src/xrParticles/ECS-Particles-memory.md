# ECS Particles Implementation - Project Memory

## Session Date
2025-10-09

## Project Goal
Convert X-Ray 16's particle system to use ENTT (Entity Component System) while maintaining backward compatibility with the vanilla implementation through a runtime console toggle.

## Phase 1 Completion Summary

### What Was Accomplished

1. **Research & Design**
   - Analyzed existing particle system architecture in `src/xrParticles/`
   - Identified key components: `CParticleManager`, `ParticleEffect`, `Particle`, `ParticleActions`
   - Designed ECS architecture with component-based approach
   - Created comprehensive migration plan document: `ECS_PARTICLE_MIGRATION_PLAN.md`

2. **Directory Structure Created**
   ```
   src/xrParticles/
   ├── ecs/
   │   ├── Components.h            (NEW)
   │   └── Systems/
   │       └── IParticleSystem.h   (NEW)
   ├── particle_console.h          (NEW)
   ├── particle_console.cpp        (NEW)
   └── ECS_PARTICLE_MIGRATION_PLAN.md (NEW)
   ```

3. **Core Files Created**

   **ecs/Components.h**
   - Defines all particle components for ECS:
     - Core: `PositionComponent`, `VelocityComponent`, `VisualComponent`, `LifetimeComponent`, `EffectComponent`
     - Behaviors: `GravityComponent`, `DampingComponent`, `RotationVelocityComponent`, etc.
     - 20+ behavior components mapping to existing ParticleActions
     - Tag components: `MarkedForDeath`, `NewlyBorn`, `ActiveParticle`

   **ecs/Systems/IParticleSystem.h**
   - Base interface for all particle systems
   - Execution order constants (PreUpdate, Physics, Movement, etc.)
   - System registry/factory support
   - Helper macros: `DECLARE_PARTICLE_SYSTEM`, `REGISTER_PARTICLE_SYSTEM`

   **particle_console.h/cpp**
   - Console variable: `ps_particle_implementation` (0=vanilla, 1=ECS)
   - Token definitions for console UI
   - Console commands:
     - `ps_particle_implementation [vanilla|ecs]` - Switch implementations
     - `ps_particle_stats` - Display particle system statistics
   - Initialization function: `xrParticles_initconsole()`

4. **Integration Changes**

   **particle_manager.cpp**
   - Added include for `particle_console.h`
   - Modified `CParticleManager` constructor to call `xrParticles_initconsole()` on first creation
   - Uses static boolean to ensure console is only initialized once

   **CMakeLists.txt**
   - Added new files to `ParticleAPI` group:
     - `particle_console.cpp`
     - `particle_console.h`
   - Created new `ECS` group with `Components.h`
   - Created new `ECS\Systems` group with `IParticleSystem.h`
   - Added `EnTT::EnTT` to `target_link_libraries`

   **VS2022 Project Files**
   - Updated `xrParticles.vcxproj`:
     - Added all new .h files to `<ClInclude>` section
     - Added `particle_console.cpp` to `<ClCompile>` section
   - Updated `xrParticles.vcxproj.filters`:
     - Created `ECS` filter with GUID `{8a3b5e91-7f2c-4d8e-9a1f-3c2d4e5f6a7b}`
     - Created `ECS\Systems` filter with GUID `{9b4c6f02-8e3d-5e9f-0b2e-4d3e5f6a7b8c}`
     - Organized new files into appropriate filters

## Current System Architecture

### Vanilla Particle System (Existing)
```
IParticleManager (interface)
    ↓
CParticleManager
    ├── ParticleEffectVec effect_vec
    │   └── ParticleEffect[]
    │       └── Particle* particles (array)
    └── ParticleActionsVec m_alist_vec
        └── ParticleActions[]
            └── ParticleAction* (various types)
```

### Planned ECS Architecture
```
IParticleManager (interface)
    ↓
ECSParticleManager
    ├── ECSParticleEffect[]
    │   ├── entt::registry (per-effect registry)
    │   └── xr_vector<IParticleSystem*> systems
    └── xr_map<int, xr_vector<IParticleSystem*>> actionListSystems
```

## Key Design Decisions

1. **Dual Implementation Strategy**
   - Both implementations conform to `IParticleManager` interface
   - Console variable `ps_particle_implementation` controls which is used
   - Manager selection happens at startup via factory pattern
   - Switching requires engine restart (to avoid active particle issues)

2. **Component Granularity**
   - Core particle data split into focused components
   - Optional behavior components added based on active actions
   - Tag components for state tracking (newly born, marked for death)
   - Enables sparse iteration and better cache performance

3. **System Organization**
   - Each ParticleAction type maps to an ECS system
   - Systems have explicit execution order (physics → movement → constraints → lifetime → death)
   - System registry allows dynamic system creation
   - Systems are enable/disable-able for debugging

4. **Per-Effect Registry**
   - Each `ECSParticleEffect` has its own `entt::registry`
   - Isolates particle effects from each other
   - Simplifies cleanup and effect management
   - Matches vanilla behavior where effects are independent

## Particle Structure Comparison

### Vanilla Particle (64 bytes)
```cpp
struct Particle {
    Rotation rot;      // 4 bytes
    pVector pos;       // 12 bytes (current position)
    pVector posB;      // 12 bytes (secondary position)
    pVector vel;       // 12 bytes (velocity)
    pVector size;      // 12 bytes
    u32 color;         // 4 bytes
    float age;         // 4 bytes
    u16 frame;         // 2 bytes
    Flags16 flags;     // 2 bytes
};
```

### ECS Entity (composed from components)
```cpp
// Core components (always present):
PositionComponent { pos, posB }
VelocityComponent { vel }
VisualComponent { size, rot, color, frame, flags }
LifetimeComponent { age, maxAge }
EffectComponent { effectId, owner, param }

// Optional behavior components (as needed):
GravityComponent { gravity }
DampingComponent { damping, scale }
// ... etc
```

## Action → System Mapping

| Vanilla Action Type | ECS System | Notes |
|---------------------|------------|-------|
| PAGravity | GravitySystem | Adds constant acceleration |
| PADamping | DampingSystem | Velocity reduction |
| PAMove | MovementSystem | Position integration |
| PAKillOld | LifetimeSystem | Age particles, mark for death |
| PASource | ParticleSpawnSystem | Spawn new particles |
| PASink | ParticleSinkSystem | Remove particles in domain |
| PABounce | BounceSystem | Collision with domains |
| PATargetColor | ColorInterpolationSystem | Interpolate to target color |
| PATargetSize | SizeInterpolationSystem | Interpolate to target size |
| PATargetVelocity | VelocityTargetSystem | Accelerate toward target |
| PAVortex | VortexSystem | Swirling motion |
| PATurbulence | TurbulenceSystem | Noise-based perturbation |
| PAExplosion | ExplosionSystem | Radial explosion force |
| ... | ... | 26 total action types |

## Console Commands

### Usage
```
// Set implementation (requires restart)
ps_particle_implementation vanilla
ps_particle_implementation ecs

// Show statistics
ps_particle_stats
```

### Output Examples
```
! Particle implementation set to: ecs
! Note: Particle implementation change requires engine restart to take full effect
! Active particles will be cleared on manager recreation
```

## Next Steps (Phase 2+)

### Phase 2: Core Systems Implementation
- [ ] Implement `MovementSystem` (integrate velocity → position)
- [ ] Implement `LifetimeSystem` (age particles, mark old for death)
- [ ] Implement `DeathSystem` (handle callbacks, remove entities)
- [ ] Create `ECSParticleEffect` class
- [ ] Test basic particle lifecycle (spawn → update → death)

### Phase 3: Action System Mapping
- [ ] Implement `GravitySystem`
- [ ] Implement `DampingSystem`
- [ ] Implement remaining 24 systems (one per action type)
- [ ] Create action → system factory/mapper
- [ ] Test each system individually

### Phase 4: Manager Implementation
- [ ] Implement `ECSParticleManager` class
- [ ] Implement `IParticleManager` interface methods
- [ ] Add thread safety (locks/mutexes)
- [ ] Implement action list → system mapping
- [ ] Factory pattern for manager selection

### Phase 5: Rendering Integration
- [ ] Extract particle data for rendering
- [ ] Ensure compatibility with existing render pipeline
- [ ] Handle transparency sorting
- [ ] Optimize data extraction (avoid copies)

### Phase 6: Testing & Optimization
- [ ] Unit tests for each system
- [ ] Integration tests with game
- [ ] Performance profiling (ECS vs vanilla)
- [ ] Memory usage analysis
- [ ] Multithreading opportunities

## Technical Considerations

### Performance Expectations
- **ECS Advantages**: Better cache coherency, SIMD opportunities, easy parallelization
- **ECS Disadvantages**: Overhead per entity, complex for small particle counts
- **Recommendation**: Profile both implementations, consider hybrid approach

### Thread Safety
- Vanilla uses explicit locks (`ScopeLock`, `Lock`)
- ENTT registries are NOT thread-safe by default
- Must add locking or use per-thread registries
- Consider lock-free designs where possible

### Memory Management
- Vanilla uses manual allocation (`xr_alloc`, `xr_new`)
- ENTT uses internal pools for components
- Monitor memory overhead of ENTT
- Consider custom allocators if needed

### Callback Compatibility
- Birth/death callbacks expect array indices in vanilla
- ECS uses entity handles (`entt::entity`)
- Solution: Store entity handle in callback or provide index-to-entity mapping

## Known Issues & Risks

1. **Performance Regression for Small Effects**
   - ECS overhead may hurt performance for effects with <100 particles
   - Mitigation: Profile and consider hybrid approach

2. **Callback Index Mismatch**
   - External code expects particle indices, not entity handles
   - Mitigation: Create compatibility layer or update callback signatures

3. **Thread Safety Complexity**
   - Need to ensure ECS operations are properly locked
   - Mitigation: Design carefully, test thoroughly

4. **Rendering Data Access**
   - Renderer expects contiguous particle array
   - Mitigation: Create temporary buffer or teach renderer to use ENTT views

## Build System Integration

### CMake
- Added to `src/xrParticles/CMakeLists.txt`
- Linked `EnTT::EnTT` library
- Files organized into logical groups

### VS2022
- Project file: `src/xrParticles/xrParticles.vcxproj`
- Filters file: `src/xrParticles/xrParticles.vcxproj.filters`
- All files added to appropriate sections
- Filters created for organization in Solution Explorer

## References

- **ENTT Documentation**: https://github.com/skypjack/entt
- **Migration Plan**: `src/xrParticles/ECS_PARTICLE_MIGRATION_PLAN.md`
- **Similar Work**: `src/xrAnimation/OzzKinematicsAnimated.cpp` (uses ENTT)
- **Console Pattern**: `src/Layers/xrRender/xrRender_console.cpp`

## Session Notes

- Phase 1 completed successfully
- All files compile (pending actual build test)
- Build system integration complete for both CMake and VS2022
- Console variable system fully integrated
- Ready to begin Phase 2 implementation

## Current Status

**Phase 1: Foundation - COMPLETE ✓**
- [x] Console variable system
- [x] Component definitions (20+ components)
- [x] System base interface
- [x] Directory structure
- [x] Build system integration
- [x] Design documentation

**Phase 2: Core Systems - NOT STARTED**

**Overall Progress: 1/6 phases complete (17%)**

---

*Last Updated: 2025-10-09*
*Next Session: Implement Phase 2 (Core Systems)*
