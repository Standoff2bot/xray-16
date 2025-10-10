#pragma once

#include "xrParticles/psystem.h"
#include <entt/entt.hpp>

namespace ParticleECS
{

// ============================================================================
// Core Particle Components
// ============================================================================

// Position component - stores current and secondary position
struct PositionComponent
{
    PAPI::pVector pos;      // Current position
    PAPI::pVector posB;     // Secondary position (for trails/ribbons/interpolation)
};

// Velocity component - stores particle velocity
struct VelocityComponent
{
    PAPI::pVector vel;      // Velocity vector
};

// Visual properties of the particle
struct VisualComponent
{
    PAPI::pVector size;     // Particle size (can be per-axis for non-uniform)
    PAPI::Rotation rot;     // Current rotation
    u32 color;              // ARGB color
    u16 frame;              // Current animation frame
    ::Flags16 flags;        // Visual flags (ANIMATE_CCW, etc.)
};

// Lifetime and age tracking
struct LifetimeComponent
{
    float age;              // Current age in seconds
    float maxAge;           // Maximum lifetime (0 = infinite/controlled by actions)
};

// Effect association - which effect does this particle belong to?
struct EffectComponent
{
    int effectId;           // Reference to the effect ID
    void* owner;            // Owner object (for callbacks)
    u32 param;              // User parameter (for callbacks)
};

// ============================================================================
// Behavior Components (optional, added based on active actions)
// ============================================================================

// Gravity behavior - constant acceleration
struct GravityComponent
{
    PAPI::pVector gravity;  // Gravity vector (usually (0, -9.8, 0))
};

// Damping behavior - velocity reduction over time
struct DampingComponent
{
    PAPI::pVector damping;  // Damping coefficients per axis [0,1]
    float scale;            // Overall damping scale
};

// Rotation velocity for spinning particles
struct RotationVelocityComponent
{
    float rotVel;           // Rotation speed (radians per second)
};

// Target color interpolation
struct TargetColorComponent
{
    u32 targetColor;        // Target ARGB color
    float speed;            // Interpolation speed
};

// Target size interpolation
struct TargetSizeComponent
{
    PAPI::pVector targetSize;   // Target size
    float speed;                // Interpolation speed
};

// Target velocity for homing/guided particles
struct TargetVelocityComponent
{
    PAPI::pVector targetVel;    // Target velocity
    float speed;                // Acceleration toward target
};

// Vortex effect parameters
struct VortexComponent
{
    PAPI::pVector center;       // Vortex center
    PAPI::pVector axis;         // Vortex axis (normalized)
    float magnitude;            // Vortex strength
    float tightnessExponent;    // How tight the vortex is
    float maxRadius;            // Maximum radius of effect
};

// Turbulence/noise parameters
struct TurbulenceComponent
{
    float frequency;            // Noise frequency
    float magnitude;            // Effect magnitude
    PAPI::pVector offset;       // Noise offset for variation
};

// Bounce behavior - for collision with domains
struct BounceComponent
{
    float friction;             // Friction coefficient [0,1]
    float resilience;           // Bounciness [0,1]
    float cutoffSqr;            // Cutoff velocity squared
};

// Orbit point behavior
struct OrbitPointComponent
{
    PAPI::pVector center;       // Center point to orbit
    float magnitude;            // Orbit strength
    float epsilon;              // Small value to prevent division by zero
    float maxRadius;            // Maximum radius of effect
};

// Orbit line behavior
struct OrbitLineComponent
{
    PAPI::pVector p;            // Point on line
    PAPI::pVector axis;         // Line direction (normalized)
    float magnitude;            // Orbit strength
    float epsilon;              // Small value to prevent division by zero
    float maxRadius;            // Maximum radius of effect
};

// Sink behavior - removes particles in domain
struct SinkComponent
{
    bool killInside;            // Kill if inside domain (true) or outside (false)
    // Domain information stored in action, checked per-particle
};

// Speed limit behavior
struct SpeedLimitComponent
{
    float minSpeed;             // Minimum speed
    float maxSpeed;             // Maximum speed
};

// Scatter behavior - random displacement
struct ScatterComponent
{
    float magnitude;            // Scatter magnitude
    float rate;                 // How often to scatter
    float accumulator;          // Time accumulator for rate limiting
};

// Explosion behavior
struct ExplosionComponent
{
    PAPI::pVector center;       // Explosion center
    float velocity;             // Explosion velocity magnitude
    float magnitude;            // Explosion strength
    float stdev;                // Standard deviation for variation
    float epsilon;              // Small value to prevent division by zero
    float age;                  // Explosion age (for falloff)
};

// Gravitate behavior - particles attract each other
struct GravitateComponent
{
    float magnitude;            // Gravitational strength
    float epsilon;              // Small value to prevent singularities
    float maxRadius;            // Maximum radius of effect
};

// Follow behavior - follow previous particle
struct FollowComponent
{
    float magnitude;            // Follow strength
    float maxRadius;            // Maximum distance to follow
};

// Match velocity behavior
struct MatchVelocityComponent
{
    float magnitude;            // Matching strength
    float epsilon;              // Small value
    float maxRadius;            // Maximum radius of effect
};

// Jet behavior - particle emitter that spawns new particles
struct JetComponent
{
    PAPI::pVector center;       // Jet center
    PAPI::pVector direction;    // Jet direction
    float magnitude;            // Jet strength
};

// Avoid behavior - steer away from domain
struct AvoidComponent
{
    float lookAhead;            // How far ahead to look
    float magnitude;            // Avoidance strength
};

// Restore behavior - restore to initial position
struct RestoreComponent
{
    PAPI::pVector initialPos;   // Initial position to restore to
    float timeLeft;             // Time remaining to fully restore
};

// ============================================================================
// Tag Components (no data, just markers)
// ============================================================================

// Particle is marked for removal
struct MarkedForDeath {};

// Particle was just created this frame (for birth callbacks)
struct NewlyBorn {};

// Particle is currently active/playing
struct ActiveParticle {};

// ============================================================================
// Helper Structures
// ============================================================================

// Domain storage for actions that need spatial queries
// This will be stored per-action, not per-particle
// Forward declare pDomain from particle_core.h
// struct ActionDomain
// {
//     PAPI::pDomain domain;
// };

// Rendering batch data - for grouping particles by effect for rendering
struct RenderBatchComponent
{
    int effectId;               // Which effect to render with
    u32 renderOrder;            // Sort order for transparency
};

} // namespace ParticleECS
