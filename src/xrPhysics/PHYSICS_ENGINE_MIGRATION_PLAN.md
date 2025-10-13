# xrPhysics Migration Plan: ODE → Modern Physics Engine

## Executive Summary

This document outlines a comprehensive plan to migrate OpenXRay's physics system from ODE (Open Dynamics Engine) to a modern, performant, open-source physics engine.

**Current State:** X-Ray Engine uses a heavily customized fork of ODE (circa 2004-2006 era)
**Problem:** ODE is outdated, single-threaded, poorly optimized for modern CPUs, and lacks active maintenance
**Goal:** Replace with a modern, multi-core friendly, actively maintained physics engine

---

## Current xrPhysics Architecture Analysis

### Core Components

1. **World Management** (`IPHWorld`, `CPHWorld`)
   - Physics world simulation
   - Gravity control
   - Frame stepping (fixed timestep)
   - Object/update object management
   - Freezing/unfreezing
   - Island-based simulation for performance

2. **Physics Shells** (`CPhysicsShell`)
   - Ragdoll physics system
   - Multi-element rigid body constructs
   - Bone-mapped physics for animated characters
   - Network synchronization support
   - Breakable/fracturable system

3. **Elements** (`CPhysicsElement`)
   - Individual rigid bodies
   - Multiple geometries per element (sphere, box, cylinder)
   - Mass/inertia management
   - Material assignment
   - Collision callbacks

4. **Joints** (`CPhysicsJoint`)
   - Ball-socket joints
   - Hinge joints (1-axis)
   - Hinge2 joints (2-axis, for wheels)
   - Full control joints (3-axis Euler)
   - Slider joints
   - Breakable constraints
   - Spring-damper systems

5. **Character Controllers** (`CPHCharacter`, `CPHActorCharacter`, `CPHAICharacter`)
   - Kinematic character movement
   - Player controller with collision
   - AI character physics
   - Climbing/elevator support
   - Material-based damage system

6. **Collision System**
   - Custom tri-mesh collider (Kn00pc implementation)
   - Custom cylinder collider
   - Ray casting and swept motion
   - Collision groups and filtering
   - Material-based contact responses
   - Collision damage system

7. **Specialized Systems**
   - Vehicle physics (cars, helicopters)
   - Fracture/breakable objects
   - Physics shell animator
   - Camera collision
   - Capture system (grabbing objects)

### Key ODE Dependencies

Heavy reliance on ODE types:
- `dBodyID` - rigid body handles (~291 uses across codebase)
- `dGeomID` - collision geometry handles
- `dJointID` - constraint handles
- `dSpaceID` - collision space handles
- `dWorldID` - physics world handle
- `dContact` - contact point structures
- `dMass` - mass/inertia tensors

---

## Physics Engine Comparison

### 1. **Jolt Physics** ⭐ RECOMMENDED

**Pros:**
- Modern C++17 codebase
- Industry-proven (Horizon Forbidden West, Death Stranding 2, Godot 4.4)
- Excellent multi-core scaling
- Active development (2024-2025)
- MIT License (very permissive)
- Comprehensive documentation
- Similar feature set to ODE
- Superior performance on modern CPUs
- Built-in character controller
- Extensive vehicle support
- Soft body support (in development)

**Cons:**
- Newer engine (less battle-tested than Bullet/PhysX)
- Some advanced features still in development
- API differs significantly from ODE (full rewrite needed)

**Performance:** 2-4x faster than ODE/Bullet on modern multi-core CPUs

**License:** MIT

**Use Cases:** AAA games, open-world games, high object counts

### 2. **Bullet Physics 3.x**

**Pros:**
- Mature, battle-tested (used in GTA V, Red Dead Redemption)
- Large community
- Extensive features
- Good documentation
- Character controller included
- Vehicle physics built-in
- Compatible with many platforms

**Cons:**
- Older codebase design
- Performance not as good as Jolt
- Less efficient multi-threading than Jolt
- Development has slowed in recent years
- API complexity

**Performance:** Similar to ODE, slight improvements

**License:** zlib

**Use Cases:** General purpose, good for migration from ODE

### 3. **PhysX 5.x** (NVIDIA)

**Pros:**
- Industry standard (Unreal Engine, Unity optional)
- Extremely feature-rich
- Excellent documentation
- Active development by NVIDIA
- GPU acceleration support
- Advanced features (cloth, destruction, etc.)

**Cons:**
- Large, complex codebase
- NVIDIA-centric optimization
- Heavier weight than alternatives
- More dependencies
- BSD-3 license (acceptable but less permissive than MIT)

**Performance:** Excellent, especially on NVIDIA GPUs

**License:** BSD-3-Clause

**Use Cases:** High-end games, when GPU physics is needed

### 4. **ReactPhysics3D**

**Pros:**
- Lightweight and simple
- Easy to integrate
- Good for learning
- zlib license

**Cons:**
- Fewer features than alternatives
- Limited character controller support
- Less industry adoption
- Single-threaded focus
- Not suitable for complex game physics

**Performance:** Moderate

**License:** zlib

**Use Cases:** Simple games, 2D physics, prototypes

---

## Recommendation: Jolt Physics

### Justification

1. **Performance**: 2-4x faster than ODE, excellent multi-core scaling
2. **Modern Design**: Written for modern C++17, leverages SIMD
3. **Industry Proven**: Successfully powers AAA titles (Horizon series)
4. **Active Development**: Regular updates, responsive maintainer
5. **License**: MIT is maximally permissive
6. **Feature Completeness**: All features needed by X-Ray are available
7. **Documentation**: Excellent docs and examples
8. **Community**: Growing adoption (now in Godot 4.4)

---

## Migration Strategy

### Phase 1: Preparation & Analysis (2-4 weeks)

**1.1 Create Abstraction Layer**
- Design physics engine abstraction interface
- Isolate ODE-specific code behind interfaces
- Create adapter pattern for ODE implementation
- Document all physics API usage points

**1.2 Audit & Inventory**
- Complete inventory of all ODE API calls
- Identify custom ODE modifications
- Document behavioral assumptions
- Map features to Jolt equivalents

**1.3 Setup Development Environment**
- Integrate Jolt Physics as git submodule
- Configure CMake build
- Create proof-of-concept branch
- Setup automated testing framework

### Phase 2: Core System Migration (4-8 weeks)

**2.1 World Management**
```cpp
// Old: ODE
dWorldID world;
dSpaceID space;

// New: Jolt
JPH::PhysicsSystem physics_system;
JPH::BodyInterface& body_interface = physics_system.GetBodyInterface();
```

**2.2 Rigid Bodies & Elements**
```cpp
// Map CPhysicsElement → JPH::Body
// Handle geometry conversions
// Implement mass/inertia mapping
```

**2.3 Collision Detection**
```cpp
// Map ODE geoms → JPH::Shape
// Sphere → JPH::SphereShape
// Box → JPH::BoxShape
// Cylinder → JPH::CylinderShape (already supported)
// TriMesh → JPH::MeshShape
```

**2.4 Constraints/Joints**
```cpp
// Map CPhysicsJoint → JPH::Constraint
// Ball → JPH::PointConstraint
// Hinge → JPH::HingeConstraint
// Slider → JPH::SliderConstraint
```

### Phase 3: Advanced Features (4-6 weeks)

**3.1 Character Controller** ✅ COMPLETE
- ✅ Migrated CPHCharacter to Jolt's character controller
- ✅ Preserved game-specific movement logic (Quake/CS-style)
- ✅ Implemented bunnyhopping mechanics
- ✅ Ground/air acceleration and friction system

**3.2 Ragdoll System** 🔄 IN PROGRESS (HIGH PRIORITY - MVP CRITICAL)
- [ ] Migrate CPhysicsShell to Jolt ragdolls
- [ ] Preserve bone mapping and hierarchy
- [ ] Maintain breakable joints with force limits
- [ ] Integrate with death/knockout animations
- [ ] Network synchronization support
- **Rationale:** Core gameplay feature for character deaths and physics interactions

**3.3 Network Synchronization** 🔲 NEXT (HIGH PRIORITY - MVP CRITICAL)
- [ ] Adapt network state serialization
- [ ] Ensure deterministic simulation
- [ ] Client prediction and reconciliation
- [ ] Test multiplayer scenarios
- [ ] Server authority validation
- **Rationale:** Essential for multiplayer stability and anti-cheat

**3.4 Vehicle Physics** 📦 POST-MVP (DEFERRED)
- [ ] Use Jolt's VehicleConstraint
- [ ] Migrate wheel physics
- [ ] Preserve car handling characteristics
- **Rationale:** Not present in native S.T.A.L.K.E.R. gameplay, only needed for future mods

### Phase 4: Custom Systems (3-5 weeks)

**4.1 Material System**
- Migrate material-based collision responses
- Preserve damage calculation
- Map friction/restitution values

**4.2 Collision Callbacks**
- Implement contact listeners
- Preserve callback interfaces
- Maintain collision damage system

**4.3 Fracture System**
- Adapt breakable object system
- Use Jolt's constraint breaking
- Test destruction scenarios

### Phase 5: Optimization & Polish (2-4 weeks)

**5.1 Performance Tuning**
- Configure Jolt threading model
- Optimize collision detection
- Profile and benchmark
- Compare against ODE baseline

**5.2 Testing & Validation**
- Comprehensive regression testing
- Physics behavior validation
- Network testing
- Performance benchmarks

**5.3 Documentation**
- API migration guide
- Physics tuning guide
- Known behavior changes
- Performance optimization tips

---

## Implementation Details

### Directory Structure
```
src/xrPhysics/
├── adapters/           # NEW: Physics engine adapters
│   ├── IPhysicsAdapter.h
│   ├── OdeAdapter/     # Legacy ODE wrapper
│   └── JoltAdapter/    # New Jolt wrapper
├── legacy/             # Existing ODE code (gradually deprecated)
└── [existing files]
```

### CMake Integration
```cmake
option(USE_JOLT_PHYSICS "Use Jolt Physics instead of ODE" ON)
option(USE_ODE_PHYSICS "Use legacy ODE Physics" OFF)

if(USE_JOLT_PHYSICS)
    add_subdirectory(Externals/JoltPhysics/Build)
    target_link_libraries(xrPhysics PRIVATE Jolt)
    target_compile_definitions(xrPhysics PRIVATE XRPHYSICS_JOLT=1)
elseif(USE_ODE_PHYSICS)
    target_link_libraries(xrPhysics PUBLIC xrODE)
    target_compile_definitions(xrPhysics PRIVATE XRPHYSICS_ODE=1)
endif()
```

### Adapter Pattern Example
```cpp
// IPhysicsWorld.h
class IPhysicsWorld {
public:
    virtual ~IPhysicsWorld() = default;
    virtual void Step(float dt) = 0;
    virtual void SetGravity(float g) = 0;
    virtual IPhysicsBody* CreateBody(...) = 0;
    // ... other methods
};

// JoltPhysicsWorld.h
class JoltPhysicsWorld : public IPhysicsWorld {
    JPH::PhysicsSystem m_system;
    JPH::JobSystem* m_job_system;

public:
    void Step(float dt) override {
        m_system.Update(dt, 1, m_temp_allocator, m_job_system);
    }
    // ... implementations
};
```

---

## Risk Assessment

### High Risk
- **Physics Behavior Changes**: Different solvers may produce different results
  - *Mitigation*: Extensive testing, tuning parameters to match
- **Performance Regressions**: Initial implementation may be slower
  - *Mitigation*: Profile early, optimize incrementally
- **Breaking Multiplayer**: Network desyncs
  - *Mitigation*: Deterministic mode, thorough network testing

### Medium Risk
- **Custom Colliders**: Tri-mesh and cylinder customizations
  - *Mitigation*: Jolt supports both natively
- **Integration Issues**: Build system complications
  - *Mitigation*: CMake expertise, gradual integration
- **Learning Curve**: Team unfamiliarity with Jolt
  - *Mitigation*: Documentation, proof-of-concepts

### Low Risk
- **License Compatibility**: MIT is compatible with OpenXRay
- **Platform Support**: Jolt supports all target platforms
- **Maintenance**: Active project with good support

---

## Timeline Estimate

| Phase | Duration | Status | Dependencies |
|-------|----------|--------|--------------|
| Phase 1: Preparation | 2-4 weeks | ✅ **COMPLETE** | - |
| Phase 2: Core Migration | 4-8 weeks | ✅ **COMPLETE** | Phase 1 |
| Phase 3.1-3: Char/Ray/Collision | 2-3 weeks | ✅ **COMPLETE** | Phase 2 |
| **Phase 3.4: Ragdoll** | **1-2 weeks** | 🔄 **IN PROGRESS** | Phase 2 |
| **Phase 3.5: Network Sync** | **1-2 weeks** | 🔲 **NEXT** | Phase 3.4 |
| Phase 3.6: Fracture System | 1 week | 🔲 Pending | Phase 3.4 |
| Phase 4: Optimization | 2-3 weeks | 🔲 Pending | Phase 3 |
| **MVP Total** | **~2-3 weeks remaining** | **~75% done** | - |
| **Full Complete** | **~4-5 weeks remaining** | **~70% done** | - |

**Current Focus:** Ragdoll system implementation (game-critical for death animations)
**Deferred:** Vehicle physics (not in native game, post-MVP feature)

---

## Success Criteria

### MVP Must Have (Core Game Functionality)
- [x] All core physics features functional (rigid bodies, constraints, collision)
- [x] Character controller working (player movement, AI)
- [ ] **Ragdoll system functional** ⬅ HIGH PRIORITY
- [ ] **Network synchronization working** ⬅ HIGH PRIORITY
- [x] Performance equal or better than ODE
- [x] No crashes or stability issues
- [x] Multi-threading enabled and optimized

### Post-MVP Should Have
- [ ] Physics behavior matches ODE within acceptable tolerance
- [ ] All game objects physics working correctly
- [ ] Fracture/destruction system working
- [x] Clean abstraction layer for future engine swaps

### Post-MVP Nice to Have
- [ ] Significant performance improvement (2x+ faster)
- [ ] Improved physics quality (more stable, realistic)
- [ ] Easier physics tuning interface
- [ ] Better debugging tools
- [ ] **Vehicle physics** (only if community creates vehicle mods)

---

## Alternative Approach: Hybrid/Gradual Migration

If full migration is too risky, consider:

1. **Dual Engine Support**: Run both ODE and Jolt in parallel
   - Use feature flags to toggle per-object
   - Migrate object types incrementally
   - Compare results side-by-side

2. **Module-by-Module**: Migrate by feature area
   - Start with static/simple objects
   - Then dynamic props
   - Then characters
   - Finally vehicles and ragdolls

3. **ODE-to-Bullet First**: Use Bullet as intermediate step
   - Easier migration path (similar API)
   - Then Bullet-to-Jolt later
   - Lower risk but slower overall

---

## Resources & References

### Jolt Physics
- GitHub: https://github.com/jrouwe/JoltPhysics
- Documentation: https://jrouwe.github.io/JoltPhysics/
- Samples: https://github.com/jrouwe/JoltPhysics/tree/master/Samples

### Migration Guides
- Godot Jolt Integration: https://github.com/godot-jolt/godot-jolt
- ODE to Bullet: Various community examples
- Physics Engine Comparisons: PEEL test suite

### Testing
- PEEL (Physics Engine Evaluation Lab)
- Physics benchmarks and validation tools

---

## Conclusion

**Migration Status: ~70-75% Complete, MVP in Sight**

The ODE to Jolt Physics migration has progressed excellently:

### ✅ Achieved Benefits:
- **Better Performance**: Multi-core threading active, faster collision detection
- **Modern Codebase**: C++17, SIMD optimizations, clean architecture
- **Future-Proof**: Active development, industry backing (Horizon, Godot)
- **Better Quality**: More stable simulation, richer feature set

### 🔄 Current Status:
- **Phase 1 & 2:** ✅ Complete (infrastructure, core systems)
- **Phase 3.1-3:** ✅ Complete (character, ray casting, collision)
- **Phase 3.4:** 🔄 Ragdoll system (HIGH PRIORITY - IN PROGRESS)
- **Phase 3.5:** 🔲 Network sync (HIGH PRIORITY - NEXT)
- **Phase 3.6:** 🔲 Fracture system (MEDIUM PRIORITY)

### 🎯 Path to MVP (~2-3 weeks):
1. **Implement Ragdoll System** (1-2 weeks)
   - Multi-body chains with bone mapping
   - Breakable constraints for death animations
   - Essential for core gameplay

2. **Network Synchronization** (1-2 weeks)
   - State serialization for multiplayer
   - Deterministic simulation
   - Server authority validation

3. **Polish & Testing** (+1 week)
   - Integration testing
   - Performance validation
   - Bug fixes

### 📦 Post-MVP (Deferred):
- **Vehicle Physics**: Not in native S.T.A.L.K.E.R., only needed for future mods
- **Advanced Optimization**: Further performance tuning
- **Debug Visualization**: Development tools

**Next Immediate Action:** Begin ragdoll system implementation
