# Jolt Physics Migration - Status

Branch: `yohji/feat/jolt`

## Overview

This branch contains the initial infrastructure for migrating OpenXRay from ODE (Open Dynamics Engine) to Jolt Physics.

## Current Status

### ✅ Completed (Phase 1 - Infrastructure)

1. **Jolt Physics Integration**
   - Added Jolt Physics as git submodule at `Externals/JoltPhysics`
   - Configured CMake build system with options:
     - `XRPHYSICS_USE_JOLT` - Enable Jolt Physics (default: OFF)
     - `XRPHYSICS_USE_ODE` - Enable ODE Physics (default: ON)

2. **Abstraction Layer**
   - Created physics engine abstraction interface (`IPhysicsAdapter.h`)
   - Implemented engine registry for runtime selection
   - Defined interfaces for:
     - `IPhysicsWorld` - Main physics simulation
     - `IPhysicsBody` - Rigid bodies
     - `IPhysicsShape` - Collision shapes
     - `IPhysicsConstraint` - Joints/constraints
     - `ICollisionCallback` - Contact callbacks

3. **Jolt Adapter (Stub Implementation)**
   - `JoltPhysicsWorld` - World management (basic initialization working)
   - `JoltPhysicsBody` - Stub implementation
   - `JoltPhysicsShape` - Stub implementation
   - `JoltPhysicsConstraint` - Stub implementation

### 🚧 In Progress

- None currently

### 📋 TODO (Phase 2 - Core Implementation)

1. **Implement Shape Creation**
   - Sphere shapes
   - Box shapes
   - Cylinder shapes
   - Capsule shapes
   - Mesh shapes (tri-mesh)

2. **Implement Body System**
   - Body creation (static, dynamic, kinematic)
   - Transform management
   - Velocity control
   - Force/impulse application
   - Mass properties

3. **Implement Constraints**
   - Point constraint (ball joint)
   - Hinge constraint
   - Slider constraint
   - Fixed constraint
   - Generic 6-DOF constraint

4. **Collision System**
   - Collision callbacks
   - Material system integration
   - Collision filtering/groups
   - Ray casting

### 📋 TODO (Phase 3 - Advanced Features)

1. Character controller
2. Vehicle physics
3. Ragdoll system
4. Network synchronization
5. Fracture/breakable objects

## Build Instructions

### Enable Jolt Physics

```bash
cd build
cmake .. -DXRPHYSICS_USE_JOLT=ON -DXRPHYSICS_USE_ODE=OFF
make
```

### Keep ODE (Default)

```bash
cd build
cmake .. -DXRPHYSICS_USE_JOLT=OFF -DXRPHYSICS_USE_ODE=ON
make
```

## Testing

Currently, the Jolt adapter is stub-only and will not provide functional physics. Testing will begin once Phase 2 is complete.

### Test Plan

1. Simple world initialization test
2. Shape creation tests
3. Body creation and transform tests
4. Constraint tests
5. Performance benchmarks vs ODE
6. Game integration tests

## Architecture

### Directory Structure

```
src/xrPhysics/
├── adapters/                          # Physics abstraction layer
│   ├── IPhysicsAdapter.h             # Main interfaces
│   ├── PhysicsEngineRegistry.cpp     # Engine registration
│   └── Jolt/                         # Jolt implementation
│       ├── JoltPhysicsWorld.h/cpp
│       ├── JoltPhysicsBody.h/cpp
│       ├── JoltPhysicsShape.h/cpp
│       └── JoltPhysicsConstraint.h/cpp
├── [existing ODE code]               # Legacy implementation
└── PHYSICS_ENGINE_MIGRATION_PLAN.md  # Detailed migration plan
```

### Compilation Defines

- `XRPHYSICS_JOLT=1` - Jolt Physics is enabled
- `XRPHYSICS_ODE=1` - ODE Physics is enabled (legacy)

## Dependencies

- **Jolt Physics**: v5.x (submodule)
  - License: MIT
  - Repository: https://github.com/jrouwe/JoltPhysics
  - Build: CMake-based, integrated into OpenXRay build

## Performance Goals

- 2-4x faster than ODE on multi-core CPUs
- Better multi-threading utilization
- Lower memory footprint
- Improved stability

## Known Issues

- Stub implementations return placeholder values
- No actual physics simulation yet with Jolt
- Cannot mix ODE and Jolt in the same build

## Timeline

- **Phase 1 (Infrastructure)**: ✅ Complete (current)
- **Phase 2 (Core Implementation)**: 📅 4-8 weeks (planned)
- **Phase 3 (Advanced Features)**: 📅 4-6 weeks (planned)
- **Phase 4 (Custom Systems)**: 📅 3-5 weeks (planned)
- **Phase 5 (Optimization)**: 📅 2-4 weeks (planned)

**Total Estimated**: 15-27 weeks (4-6 months)

## Documentation

- Detailed migration plan: `src/xrPhysics/PHYSICS_ENGINE_MIGRATION_PLAN.md`
- API documentation: See interface headers in `src/xrPhysics/adapters/`

## Contributing

When working on this branch:

1. Keep ODE build working (don't break existing code)
2. Add Jolt implementations behind `#ifdef XRPHYSICS_JOLT`
3. Update this status document as features are completed
4. Add tests for new functionality
5. Profile performance improvements

## References

- [Jolt Physics Documentation](https://jrouwe.github.io/JoltPhysics/)
- [Jolt Physics GitHub](https://github.com/jrouwe/JoltPhysics)
- [OpenXRay Physics Migration Plan](src/xrPhysics/PHYSICS_ENGINE_MIGRATION_PLAN.md)
