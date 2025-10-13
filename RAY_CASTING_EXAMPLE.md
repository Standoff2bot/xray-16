# Ray Casting Implementation

## Overview
Ray casting is now implemented for the Jolt Physics integration. This allows for line-of-sight checks, weapon hit detection, mouse picking, and general physics queries.

## Features Implemented
- ✅ Single ray casting with closest hit detection
- ✅ Configurable filters for broad phase, object layer, and bodies
- ✅ Surface normal calculation at hit point
- ✅ Body reference lookup
- ✅ Hit position, normal, and fraction calculation
- ✅ Proper coordinate conversion between X-Ray and Jolt

## API Usage

### Basic Ray Cast

```cpp
// Create physics world
IPhysicsWorld* world = PhysicsEngineRegistry::CreateWorld(PhysicsEngineType::Jolt);
world->Initialize();

// Setup scene with a ground plane
IPhysicsBody* ground = world->CreateBody(PhysicsBodyType::Static);
ground->SetPosition(Fvector(0, -5, 0));

// Cast a ray from above
Fvector ray_origin(0, 10, 0);
Fvector ray_direction(0, -1, 0);  // Downward
float max_distance = 20.0f;

PhysicsRayHit hit;
bool did_hit = world->RayCast(ray_origin, ray_direction, max_distance, hit);

if (did_hit)
{
    Msg("Ray hit at position: (%.2f, %.2f, %.2f)",
        hit.position.x, hit.position.y, hit.position.z);
    Msg("Hit normal: (%.2f, %.2f, %.2f)",
        hit.normal.x, hit.normal.y, hit.normal.z);
    Msg("Hit fraction: %.3f (distance: %.2f)",
        hit.fraction, max_distance * hit.fraction);

    if (hit.body)
    {
        Msg("Hit body with user data: %p", hit.user_data);
    }
}
else
{
    Msg("Ray did not hit anything");
}
```

### Weapon Hit Detection Example

```cpp
// Fire a weapon ray from the player's position
void FireWeapon(IPhysicsWorld* world, const Fvector& start, const Fvector& direction)
{
    PhysicsRayHit hit;
    float weapon_range = 1000.0f;  // 100 meters

    if (world->RayCast(start, direction, weapon_range, hit))
    {
        // Calculate damage falloff based on distance
        float distance = weapon_range * hit.fraction;
        float damage = CalculateDamage(distance);

        // Apply damage to hit object
        if (hit.body && hit.user_data)
        {
            GameObject* hit_object = static_cast<GameObject*>(hit.user_data);
            hit_object->TakeDamage(damage, hit.position, hit.normal);
        }

        // Spawn impact effect at hit position
        SpawnImpactEffect(hit.position, hit.normal);
    }
}
```

### Line-of-Sight Check

```cpp
// Check if there's a clear line of sight between two points
bool HasLineOfSight(IPhysicsWorld* world, const Fvector& from, const Fvector& to)
{
    Fvector direction = to;
    direction.sub(from);

    float distance = direction.magnitude();
    direction.normalize();

    PhysicsRayHit hit;
    if (world->RayCast(from, direction, distance, hit))
    {
        // Check if we hit something before reaching the target
        return hit.fraction >= 0.99f;  // Allow small tolerance
    }

    // No obstruction
    return true;
}
```

### Mouse Picking Example

```cpp
// Pick an object in the 3D scene using mouse cursor
IPhysicsBody* PickObject(IPhysicsWorld* world, const Fvector& camera_pos,
                         const Fvector& mouse_ray_dir)
{
    PhysicsRayHit hit;
    float pick_distance = 100.0f;

    if (world->RayCast(camera_pos, mouse_ray_dir, pick_distance, hit))
    {
        return hit.body;
    }

    return nullptr;
}
```

## Return Values

### PhysicsRayHit Structure

```cpp
struct PhysicsRayHit
{
    bool hit;                  // True if ray hit something
    Fvector position;          // World-space hit position
    Fvector normal;            // Surface normal at hit point
    float fraction;            // Hit fraction (0.0 to 1.0)
    IPhysicsBody* body;        // Hit body (nullptr if none)
    IPhysicsShape* shape;      // Hit shape (not currently populated)
    void* user_data;           // User data from hit body
};
```

### Fraction Value
- `0.0` = Hit at ray origin
- `0.5` = Hit at half the max distance
- `1.0` = Hit at exactly max distance
- Actual hit distance = `max_distance * fraction`

## Implementation Details

### Ray Definition
- **Origin**: Starting point of the ray in world space
- **Direction**: Direction vector (automatically normalized)
- **Max Distance**: Maximum ray length

### Filters
The current implementation tests against all bodies using:
- **BroadPhaseLayerFilter**: Tests all broad phase layers
- **ObjectLayerFilter**: Tests all object layers
- **BodyFilter**: Tests all bodies

Future versions can add custom filters to:
- Ignore specific bodies (e.g., the shooter)
- Test only dynamic or static bodies
- Filter by collision groups
- Implement ray masks

### Performance Considerations

1. **Broad Phase Acceleration**
   - Jolt uses spatial acceleration structures
   - Only bodies in the ray's path are tested

2. **Early Exit**
   - Uses `ClosestHitCollisionCollector`
   - Stops at first hit for efficiency

3. **Thread Safety**
   - Ray casting is thread-safe
   - Uses body locks for accessing hit information
   - Can be called from multiple threads simultaneously

### Coordinate System
- Uses X-Ray's coordinate system (right-handed)
- Automatic conversion to/from Jolt coordinates
- No manual transformation needed

## Advanced Features (Future)

### Multiple Hits
```cpp
// Future API for collecting all hits along the ray
bool RayCastAll(const Fvector& origin, const Fvector& direction,
                float max_distance, xr_vector<PhysicsRayHit>& hits_out);
```

### Custom Filters
```cpp
// Future API for custom filtering
class CustomBodyFilter : public IPhysicsBodyFilter
{
    bool ShouldCollide(const IPhysicsBody* body) const override
    {
        // Custom filtering logic
        return body->GetType() != PhysicsBodyType::Kinematic;
    }
};

bool RayCastFiltered(const Fvector& origin, const Fvector& direction,
                     float max_distance, PhysicsRayHit& hit_out,
                     IPhysicsBodyFilter* filter);
```

### Shape Casting
```cpp
// Future API for swept shape queries
bool ShapeCast(IPhysicsShape* shape, const Fvector& origin,
               const Fvector& direction, float max_distance,
               PhysicsRayHit& hit_out);
```

## Testing Recommendations

### Unit Tests
1. Ray hits ground plane from above
2. Ray misses all objects
3. Ray hits at exact max distance
4. Ray direction normalization
5. Multiple rays in different directions

### Integration Tests
1. Weapon fire in game scenarios
2. AI line-of-sight checks
3. Character ground detection
4. Mouse object picking
5. Performance with many rays per frame

### Edge Cases
- Zero-length rays
- Non-normalized directions
- Very small max distances
- Rays starting inside objects
- Parallel rays to surfaces

## Known Limitations

1. **Shape Information**
   - Currently returns `nullptr` for `hit.shape`
   - Body reference is available via `hit.body`

2. **Sub-Shape IDs**
   - Not exposed through the API
   - Used internally for normal calculation

3. **Backface Culling**
   - Currently tests both front and back faces
   - No option to ignore backfaces

## Performance Metrics

Typical performance (estimated):
- **Single ray**: < 0.01ms
- **100 rays**: ~0.5-1ms
- **1000 rays**: ~5-10ms

*Actual performance depends on scene complexity and ray length*

## See Also
- `IPhysicsWorld::RayCast()` in `IPhysicsAdapter.h`
- `JoltPhysicsWorld::RayCast()` in `JoltPhysicsWorld.cpp`
- Jolt Physics documentation on ray casting
- Phase 3 implementation summary

---
**Status:** Complete
**Last Updated:** 2025-10-13
**Branch:** `yohji/feat/jolt`
