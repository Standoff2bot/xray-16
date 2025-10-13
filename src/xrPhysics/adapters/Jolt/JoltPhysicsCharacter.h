#pragma once

#include "../IPhysicsAdapter.h"
#include "xrCommon/xr_vector.h"

#ifdef XRPHYSICS_JOLT

namespace JPH
{
    class Character;
    class CharacterSettings;
    class PhysicsSystem;
}

class JoltPhysicsWorld;

class JoltPhysicsCharacter : public IPhysicsCharacter
{
public:
    JoltPhysicsCharacter(JoltPhysicsWorld* world, float radius, float height);
    virtual ~JoltPhysicsCharacter();

    // IPhysicsCharacter implementation

    // Transform
    void GetPosition(Fvector& pos) const override;
    void SetPosition(const Fvector& pos) override;
    void GetVelocity(Fvector& vel) const override;
    void SetVelocity(const Fvector& vel) override;

    // Movement input
    void SetMovementDirection(const Fvector& direction) override;
    void SetMaxSpeed(float speed) override;
    float GetMaxSpeed() const override;

    // Jumping
    void Jump(float jump_velocity) override;
    bool CanJump() const override;
    bool IsJumping() const override;

    // Ground detection
    CharacterMovementState GetMovementState() const override;
    bool IsOnGround() const override;
    void GetGroundNormal(Fvector& normal) const override;
    float GetGroundDistance() const override;

    // Physics properties
    void SetMass(float mass) override;
    float GetMass() const override;
    void SetCapsuleSize(float radius, float height) override;
    void GetCapsuleSize(float& radius, float& height) const override;

    // Movement tuning (Quake-style physics)
    void SetGroundAcceleration(float accel) override;
    void SetAirAcceleration(float accel) override;
    void SetGroundFriction(float friction) override;
    void SetStopSpeed(float speed) override;
    void SetMaxAirSpeed(float speed) override;

    // Step/slope handling
    void SetMaxStepHeight(float height) override;
    void SetMaxSlopeAngle(float angle_degrees) override;

    // User data
    void SetUserData(void* data) override;
    void* GetUserData() const override;

    // Update
    void Update(float dt) override;

private:
    // Quake-style movement physics
    void ApplyGroundMovement(float dt);
    void ApplyAirMovement(float dt);
    void ApplyGroundFriction(float dt);

    // Bunnyhopping implementation - this is the "bug" from Quake/CS
    // The key is that air acceleration doesn't check total speed,
    // only the speed in the direction of acceleration
    void ApplyAirAcceleration(const Fvector& wish_dir, float wish_speed, float dt);

    float ClipVelocity(Fvector& velocity, const Fvector& normal, float overbounce);
    void GroundCheck();

    JoltPhysicsWorld* m_world;
    JPH::Character* m_character;

    // Movement state
    Fvector m_velocity;
    Fvector m_movement_direction;
    CharacterMovementState m_state;
    bool m_is_jumping;
    float m_jump_velocity;
    float m_time_in_air;

    // Ground detection
    bool m_is_on_ground;
    Fvector m_ground_normal;
    float m_ground_distance;

    // Capsule properties
    float m_radius;
    float m_height;
    float m_mass;

    // Movement parameters (Quake-style)
    float m_max_speed;              // Max ground speed
    float m_max_air_speed;          // Max speed cap for air control (set high for bhopping)
    float m_ground_acceleration;    // Ground acceleration
    float m_air_acceleration;       // Air acceleration (key for bhopping)
    float m_ground_friction;        // Ground friction coefficient
    float m_stop_speed;             // Speed threshold for friction

    // Step/slope
    float m_max_step_height;
    float m_max_slope_angle;

    // User data
    void* m_user_data;
};

#endif // XRPHYSICS_JOLT
