#include "StdAfx.h"
#include "JoltPhysicsCharacter.h"
#include "JoltPhysicsWorld.h"

#ifdef XRPHYSICS_JOLT

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Character/Character.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

using namespace JPH;

// Quake/Counter-Strike movement constants
// These values are tuned to feel similar to Source Engine movement
constexpr float DEFAULT_MAX_SPEED = 5.2f;           // ~320 units/sec in Source
constexpr float DEFAULT_MAX_AIR_SPEED = 30.0f;      // High cap allows bunnyhopping
constexpr float DEFAULT_GROUND_ACCEL = 100.0f;       // Ground acceleration
constexpr float DEFAULT_AIR_ACCEL = 12.0f;          // Air acceleration (key for bhopping)
constexpr float DEFAULT_GROUND_FRICTION = 6.0f;     // Ground friction
constexpr float DEFAULT_STOP_SPEED = 1.0f;          // Speed threshold for friction
constexpr float DEFAULT_MASS = 70.0f;                // 70kg character
constexpr float DEFAULT_STEP_HEIGHT = 0.5f;         // Can step up 0.5m
constexpr float DEFAULT_MAX_SLOPE = 45.0f;          // 45 degree max slope

JoltPhysicsCharacter::JoltPhysicsCharacter(JoltPhysicsWorld* world, float radius, float height)
    : m_world(world)
    , m_character(nullptr)
    , m_radius(radius)
    , m_height(height)
    , m_mass(DEFAULT_MASS)
    , m_max_speed(DEFAULT_MAX_SPEED)
    , m_max_air_speed(DEFAULT_MAX_AIR_SPEED)
    , m_ground_acceleration(DEFAULT_GROUND_ACCEL)
    , m_air_acceleration(DEFAULT_AIR_ACCEL)
    , m_ground_friction(DEFAULT_GROUND_FRICTION)
    , m_stop_speed(DEFAULT_STOP_SPEED)
    , m_max_step_height(DEFAULT_STEP_HEIGHT)
    , m_max_slope_angle(DEFAULT_MAX_SLOPE)
    , m_state(CharacterMovementState::InAir)
    , m_is_jumping(false)
    , m_jump_velocity(0.0f)
    , m_time_in_air(0.0f)
    , m_is_on_ground(false)
    , m_ground_distance(FLT_MAX)
    , m_user_data(nullptr)
{
    m_velocity.set(0, 0, 0);
    m_movement_direction.set(0, 0, 0);
    m_ground_normal.set(0, 1, 0);

    // Create capsule shape for character
    RefConst<Shape> capsule_shape = new CapsuleShape(height * 0.5f, radius);

    // Create character settings
    CharacterSettings settings;
    settings.mMaxSlopeAngle = DegreesToRadians(m_max_slope_angle);
    settings.mMaxStrength = 100.0f;
    settings.mShape = capsule_shape;
    settings.mLayer = BroadPhaseLayer::MOVING; // Use moving layer
    settings.mMass = m_mass;
    settings.mFriction = 0.0f; // We handle friction ourselves
    settings.mGravityFactor = 1.0f;

    // Create the character
    m_character = new Character(&settings, Vec3::sZero(), Quat::sIdentity(),
                                 0, m_world->GetPhysicsSystem());
    m_character->AddToPhysicsSystem();
}

JoltPhysicsCharacter::~JoltPhysicsCharacter()
{
    if (m_character)
    {
        m_character->RemoveFromPhysicsSystem();
        delete m_character;
        m_character = nullptr;
    }
}

void JoltPhysicsCharacter::GetPosition(Fvector& pos) const
{
    Vec3 jolt_pos = m_character->GetPosition();
    pos.set(jolt_pos.GetX(), jolt_pos.GetY(), jolt_pos.GetZ());
}

void JoltPhysicsCharacter::SetPosition(const Fvector& pos)
{
    m_character->SetPosition(Vec3(pos.x, pos.y, pos.z));
}

void JoltPhysicsCharacter::GetVelocity(Fvector& vel) const
{
    vel = m_velocity;
}

void JoltPhysicsCharacter::SetVelocity(const Fvector& vel)
{
    m_velocity = vel;
    m_character->SetLinearVelocity(Vec3(vel.x, vel.y, vel.z));
}

void JoltPhysicsCharacter::SetMovementDirection(const Fvector& direction)
{
    m_movement_direction = direction;
}

void JoltPhysicsCharacter::SetMaxSpeed(float speed)
{
    m_max_speed = speed;
}

float JoltPhysicsCharacter::GetMaxSpeed() const
{
    return m_max_speed;
}

void JoltPhysicsCharacter::Jump(float jump_velocity)
{
    if (CanJump())
    {
        m_velocity.y = jump_velocity;
        m_jump_velocity = jump_velocity;
        m_is_jumping = true;
        m_is_on_ground = false;
        m_state = CharacterMovementState::InAir;
        m_time_in_air = 0.0f;
    }
}

bool JoltPhysicsCharacter::CanJump() const
{
    return m_is_on_ground && !m_is_jumping;
}

bool JoltPhysicsCharacter::IsJumping() const
{
    return m_is_jumping;
}

CharacterMovementState JoltPhysicsCharacter::GetMovementState() const
{
    return m_state;
}

bool JoltPhysicsCharacter::IsOnGround() const
{
    return m_is_on_ground;
}

void JoltPhysicsCharacter::GetGroundNormal(Fvector& normal) const
{
    normal = m_ground_normal;
}

float JoltPhysicsCharacter::GetGroundDistance() const
{
    return m_ground_distance;
}

void JoltPhysicsCharacter::SetMass(float mass)
{
    m_mass = mass;
}

float JoltPhysicsCharacter::GetMass() const
{
    return m_mass;
}

void JoltPhysicsCharacter::SetCapsuleSize(float radius, float height)
{
    m_radius = radius;
    m_height = height;
    // Note: Recreating the character would be needed to change shape at runtime
}

void JoltPhysicsCharacter::GetCapsuleSize(float& radius, float& height) const
{
    radius = m_radius;
    height = m_height;
}

void JoltPhysicsCharacter::SetGroundAcceleration(float accel)
{
    m_ground_acceleration = accel;
}

void JoltPhysicsCharacter::SetAirAcceleration(float accel)
{
    m_air_acceleration = accel;
}

void JoltPhysicsCharacter::SetGroundFriction(float friction)
{
    m_ground_friction = friction;
}

void JoltPhysicsCharacter::SetStopSpeed(float speed)
{
    m_stop_speed = speed;
}

void JoltPhysicsCharacter::SetMaxAirSpeed(float speed)
{
    m_max_air_speed = speed;
}

void JoltPhysicsCharacter::SetMaxStepHeight(float height)
{
    m_max_step_height = height;
}

void JoltPhysicsCharacter::SetMaxSlopeAngle(float angle_degrees)
{
    m_max_slope_angle = angle_degrees;
}

void JoltPhysicsCharacter::SetUserData(void* data)
{
    m_user_data = data;
}

void* JoltPhysicsCharacter::GetUserData() const
{
    return m_user_data;
}

void JoltPhysicsCharacter::Update(float dt)
{
    // Update ground state
    GroundCheck();

    // Apply appropriate movement physics based on ground state
    if (m_is_on_ground && !m_is_jumping)
    {
        m_state = CharacterMovementState::OnGround;
        ApplyGroundFriction(dt);
        ApplyGroundMovement(dt);
        m_time_in_air = 0.0f;
    }
    else
    {
        m_state = CharacterMovementState::InAir;
        ApplyAirMovement(dt);
        m_time_in_air += dt;

        // Clear jump flag after short delay (prevents double-jump)
        if (m_is_jumping && m_time_in_air > 0.1f)
        {
            m_is_jumping = false;
        }
    }

    // Apply gravity (Jolt handles this internally, but we track it)
    Vec3 velocity = m_character->GetLinearVelocity();
    m_velocity.set(velocity.GetX(), velocity.GetY(), velocity.GetZ());

    // Update character
    Vec3 jolt_velocity(m_velocity.x, m_velocity.y, m_velocity.z);
    m_character->SetLinearVelocity(jolt_velocity);
}

void JoltPhysicsCharacter::ApplyGroundMovement(float dt)
{
    if (m_movement_direction.magnitude() < 0.01f)
        return;

    // Get horizontal velocity
    Fvector horizontal_vel = m_velocity;
    horizontal_vel.y = 0.0f;

    // Normalize movement direction
    Fvector wish_dir = m_movement_direction;
    wish_dir.y = 0.0f;
    float wish_dir_len = wish_dir.magnitude();
    if (wish_dir_len < 0.01f)
        return;

    wish_dir.div(wish_dir_len);
    float wish_speed = m_max_speed * wish_dir_len;

    // Apply ground acceleration
    float current_speed = horizontal_vel.dotproduct(wish_dir);
    float add_speed = wish_speed - current_speed;

    if (add_speed > 0.0f)
    {
        float accel_speed = m_ground_acceleration * dt * wish_speed;
        if (accel_speed > add_speed)
            accel_speed = add_speed;

        m_velocity.x += accel_speed * wish_dir.x;
        m_velocity.z += accel_speed * wish_dir.z;
    }
}

void JoltPhysicsCharacter::ApplyAirMovement(float dt)
{
    if (m_movement_direction.magnitude() < 0.01f)
        return;

    // Normalize movement direction (horizontal only)
    Fvector wish_dir = m_movement_direction;
    wish_dir.y = 0.0f;
    float wish_dir_len = wish_dir.magnitude();
    if (wish_dir_len < 0.01f)
        return;

    wish_dir.div(wish_dir_len);

    // THIS IS THE BUNNYHOPPING "BUG" from Quake/Counter-Strike:
    // We use max_air_speed for wish_speed, which is typically higher than max_speed
    // This allows players to exceed normal max speed while in the air
    float wish_speed = m_max_air_speed * wish_dir_len;

    // Apply air acceleration (this is where the magic happens)
    ApplyAirAcceleration(wish_dir, wish_speed, dt);
}

void JoltPhysicsCharacter::ApplyAirAcceleration(const Fvector& wish_dir, float wish_speed, float dt)
{
    // THE BUNNYHOPPING BUG:
    // In Quake/CS, air acceleration only checks velocity in the direction
    // of movement, NOT total velocity. This allows building speed perpendicular
    // to current movement by strafing.

    float current_speed = m_velocity.x * wish_dir.x + m_velocity.z * wish_dir.z;
    float add_speed = wish_speed - current_speed;

    // The bug: we don't check if total speed exceeds max_speed
    // We only check if acceleration in the wish direction would exceed wish_speed
    if (add_speed > 0.0f)
    {
        float accel_speed = m_air_acceleration * wish_speed * dt;
        if (accel_speed > add_speed)
            accel_speed = add_speed;

        // Add acceleration in wish direction
        // This allows speed to build up beyond max_speed when strafing!
        m_velocity.x += accel_speed * wish_dir.x;
        m_velocity.z += accel_speed * wish_dir.z;
    }

    // NOTE: In real Quake/CS, there's an additional "air cap" that some servers use
    // to limit bunnyhopping. We expose m_max_air_speed for this purpose.
    // Set it to a high value (like 30.0) for classic bhopping,
    // or lower (like 5.3) to limit speed gain.
}

void JoltPhysicsCharacter::ApplyGroundFriction(float dt)
{
    Fvector horizontal_vel = m_velocity;
    horizontal_vel.y = 0.0f;
    float speed = horizontal_vel.magnitude();

    if (speed < 0.01f)
    {
        m_velocity.x = 0.0f;
        m_velocity.z = 0.0f;
        return;
    }

    // Apply friction
    float drop = 0.0f;

    // Only apply friction when moving below stop speed or when not inputting movement
    if (speed < m_stop_speed || m_movement_direction.magnitude() < 0.01f)
    {
        float control = (speed < m_stop_speed) ? m_stop_speed : speed;
        drop = control * m_ground_friction * dt;
    }

    // Scale the velocity
    float new_speed = speed - drop;
    if (new_speed < 0.0f)
        new_speed = 0.0f;

    if (new_speed > 0.0f)
        new_speed /= speed;

    m_velocity.x *= new_speed;
    m_velocity.z *= new_speed;
}

float JoltPhysicsCharacter::ClipVelocity(Fvector& velocity, const Fvector& normal, float overbounce)
{
    float backoff = velocity.dotproduct(normal) * overbounce;

    velocity.x -= normal.x * backoff;
    velocity.y -= normal.y * backoff;
    velocity.z -= normal.z * backoff;

    return backoff;
}

void JoltPhysicsCharacter::GroundCheck()
{
    // Use Jolt's character ground state
    m_is_on_ground = (m_character->GetGroundState() == Character::EGroundState::OnGround);

    if (m_is_on_ground)
    {
        Vec3 ground_normal = m_character->GetGroundNormal();
        m_ground_normal.set(ground_normal.GetX(), ground_normal.GetY(), ground_normal.GetZ());

        // Estimate ground distance (character is on ground, so ~0)
        m_ground_distance = 0.0f;

        // Reset jump flag when we land
        if (m_is_jumping && m_velocity.y <= 0.0f)
        {
            m_is_jumping = false;
        }
    }
    else
    {
        m_ground_normal.set(0, 1, 0);
        m_ground_distance = FLT_MAX;
    }
}

#endif // XRPHYSICS_JOLT
