#pragma once

#include "xrCore/xrCore.h"
#include "xrCommon/xr_vector.h"

// Forward declarations
struct SGameMtl;
class IPhysicsWorld;
class IPhysicsBody;
class IPhysicsShape;
class IPhysicsConstraint;
class IPhysicsCharacter;
class ICollisionCallback;

// Physics engine type
enum class PhysicsEngineType
{
    ODE,
    Jolt,
    Unknown
};

// Contact point information
struct PhysicsContact
{
    Fvector position;           // Contact position in world space
    Fvector normal;             // Contact normal (from body2 to body1)
    float depth;                // Penetration depth
    IPhysicsBody* body1;        // First body
    IPhysicsBody* body2;        // Second body
    SGameMtl* material1;        // Material of first body
    SGameMtl* material2;        // Material of second body
    void* user_data1;           // User data from body1
    void* user_data2;           // User data from body2
};

// Collision callback interface
class ICollisionCallback
{
public:
    virtual ~ICollisionCallback() = default;

    // Called when two bodies collide
    // Return false to disable collision response
    virtual bool OnCollision(const PhysicsContact& contact) = 0;
};

// Shape types
enum class PhysicsShapeType
{
    Sphere,
    Box,
    Cylinder,
    Capsule,
    Mesh,
    ConvexHull,
    Compound
};

// Shape interface
class IPhysicsShape
{
public:
    virtual ~IPhysicsShape() = default;

    virtual PhysicsShapeType GetType() const = 0;
    virtual void SetUserData(void* data) = 0;
    virtual void* GetUserData() const = 0;

    // Bounding volume
    virtual void GetAABB(Fvector& min_out, Fvector& max_out) const = 0;
    virtual float GetVolume() const = 0;
};

// Body type
enum class PhysicsBodyType
{
    Static,     // Immovable
    Dynamic,    // Affected by forces
    Kinematic   // User-controlled motion
};

// Body interface
class IPhysicsBody
{
public:
    virtual ~IPhysicsBody() = default;

    // Type
    virtual PhysicsBodyType GetType() const = 0;
    virtual void SetType(PhysicsBodyType type) = 0;

    // Transform
    virtual void GetPosition(Fvector& pos) const = 0;
    virtual void SetPosition(const Fvector& pos) = 0;
    virtual void GetRotation(Fmatrix& rot) const = 0;
    virtual void SetRotation(const Fmatrix& rot) = 0;
    virtual void GetTransform(Fmatrix& transform) const = 0;
    virtual void SetTransform(const Fmatrix& transform) = 0;

    // Velocity
    virtual void GetLinearVelocity(Fvector& vel) const = 0;
    virtual void SetLinearVelocity(const Fvector& vel) = 0;
    virtual void GetAngularVelocity(Fvector& vel) const = 0;
    virtual void SetAngularVelocity(const Fvector& vel) = 0;

    // Forces and impulses
    virtual void AddForce(const Fvector& force) = 0;
    virtual void AddForceAtPosition(const Fvector& force, const Fvector& pos) = 0;
    virtual void AddImpulse(const Fvector& impulse) = 0;
    virtual void AddImpulseAtPosition(const Fvector& impulse, const Fvector& pos) = 0;
    virtual void AddTorque(const Fvector& torque) = 0;

    // Mass properties
    virtual float GetMass() const = 0;
    virtual void SetMass(float mass) = 0;
    virtual void GetMassCenter(Fvector& center) const = 0;
    virtual void SetMassCenter(const Fvector& center) = 0;

    // Material
    virtual void SetFriction(float friction) = 0;
    virtual void SetRestitution(float restitution) = 0;
    virtual float GetFriction() const = 0;
    virtual float GetRestitution() const = 0;

    // State
    virtual void Activate() = 0;
    virtual void Deactivate() = 0;
    virtual bool IsActive() const = 0;
    virtual void Enable() = 0;
    virtual void Disable() = 0;
    virtual bool IsEnabled() const = 0;

    // Shape
    virtual void AddShape(IPhysicsShape* shape) = 0;
    virtual void RemoveShape(IPhysicsShape* shape) = 0;
    virtual u32 GetShapeCount() const = 0;
    virtual IPhysicsShape* GetShape(u32 index) = 0;

    // User data
    virtual void SetUserData(void* data) = 0;
    virtual void* GetUserData() const = 0;

    // Collision callback
    virtual void SetCollisionCallback(ICollisionCallback* callback) = 0;
    virtual ICollisionCallback* GetCollisionCallback() const = 0;

    // Gravity
    virtual void SetGravityEnabled(bool enabled) = 0;
    virtual bool IsGravityEnabled() const = 0;
};

// Constraint types
enum class PhysicsConstraintType
{
    Fixed,
    Point,      // Ball joint
    Hinge,      // 1-axis rotation
    Slider,     // 1-axis translation
    Hinge2,     // 2-axis (for wheels)
    Cone,       // Cone limit
    Distance,   // Maintain distance
    Generic     // 6-DOF
};

// Constraint interface
class IPhysicsConstraint
{
public:
    virtual ~IPhysicsConstraint() = default;

    virtual PhysicsConstraintType GetType() const = 0;

    // Bodies
    virtual IPhysicsBody* GetBody1() const = 0;
    virtual IPhysicsBody* GetBody2() const = 0;

    // Anchor points
    virtual void SetAnchor(const Fvector& anchor) = 0;
    virtual void GetAnchor(Fvector& anchor) const = 0;

    // Axes (for hinges, sliders, etc.)
    virtual void SetAxis(const Fvector& axis, u32 axis_index = 0) = 0;
    virtual void GetAxis(Fvector& axis, u32 axis_index = 0) const = 0;

    // Limits
    virtual void SetLimits(float low, float high, u32 axis_index = 0) = 0;
    virtual void GetLimits(float& low, float& high, u32 axis_index = 0) const = 0;

    // Motors
    virtual void SetMotor(float target_velocity, float max_force, u32 axis_index = 0) = 0;
    virtual void EnableMotor(bool enabled, u32 axis_index = 0) = 0;

    // Spring/damping
    virtual void SetSpringDamping(float spring, float damping, u32 axis_index = 0) = 0;

    // Breaking
    virtual void SetBreakForce(float force, float torque) = 0;
    virtual bool IsBroken() const = 0;

    // Enable/disable
    virtual void Enable() = 0;
    virtual void Disable() = 0;
    virtual bool IsEnabled() const = 0;
};

// Ray cast result
struct PhysicsRayHit
{
    bool hit;
    Fvector position;
    Fvector normal;
    float fraction;
    IPhysicsBody* body;
    IPhysicsShape* shape;
    void* user_data;
};

// Character movement state
enum class CharacterMovementState
{
    OnGround,
    InAir,
    OnLadder
};

// Character controller interface
// Provides high-level character movement with ground detection, jumping, and air control
class IPhysicsCharacter
{
public:
    virtual ~IPhysicsCharacter() = default;

    // Transform
    virtual void GetPosition(Fvector& pos) const = 0;
    virtual void SetPosition(const Fvector& pos) = 0;
    virtual void GetVelocity(Fvector& vel) const = 0;
    virtual void SetVelocity(const Fvector& vel) = 0;

    // Movement input (normalized direction in XZ plane)
    virtual void SetMovementDirection(const Fvector& direction) = 0;
    virtual void SetMaxSpeed(float speed) = 0;
    virtual float GetMaxSpeed() const = 0;

    // Jumping
    virtual void Jump(float jump_velocity) = 0;
    virtual bool CanJump() const = 0;
    virtual bool IsJumping() const = 0;

    // Ground detection
    virtual CharacterMovementState GetMovementState() const = 0;
    virtual bool IsOnGround() const = 0;
    virtual void GetGroundNormal(Fvector& normal) const = 0;
    virtual float GetGroundDistance() const = 0;

    // Physics properties
    virtual void SetMass(float mass) = 0;
    virtual float GetMass() const = 0;
    virtual void SetCapsuleSize(float radius, float height) = 0;
    virtual void GetCapsuleSize(float& radius, float& height) const = 0;

    // Movement tuning (for Quake-style physics)
    virtual void SetGroundAcceleration(float accel) = 0;
    virtual void SetAirAcceleration(float accel) = 0;
    virtual void SetGroundFriction(float friction) = 0;
    virtual void SetStopSpeed(float speed) = 0;
    virtual void SetMaxAirSpeed(float speed) = 0; // For bunnyhopping control

    // Step/slope handling
    virtual void SetMaxStepHeight(float height) = 0;
    virtual void SetMaxSlopeAngle(float angle_degrees) = 0;

    // User data
    virtual void SetUserData(void* data) = 0;
    virtual void* GetUserData() const = 0;

    // Update (called each physics step)
    virtual void Update(float dt) = 0;
};

// Forward declarations for ragdoll
class IKinematics;
class IPhysicsRagdoll;

// Ragdoll element - a single body in the ragdoll system
// Maps to a specific bone in the skeleton
struct RagdollElement
{
    u16 bone_id;                    // Skeleton bone ID
    IPhysicsBody* body;             // Physics body
    IPhysicsConstraint* joint;      // Joint connecting to parent (NULL for root)
    RagdollElement* parent;         // Parent element in hierarchy
    xr_vector<RagdollElement*> children; // Child elements

    Fvector mass_center;            // Local mass center
    float mass;                     // Element mass
    bool is_breakable;              // Can joint break?
    float break_force;              // Force threshold for breaking
    float break_torque;             // Torque threshold for breaking

    RagdollElement()
        : bone_id(u16(-1)), body(nullptr), joint(nullptr),
          parent(nullptr), mass(1.0f), is_breakable(false),
          break_force(FLT_MAX), break_torque(FLT_MAX)
    {
        mass_center.set(0, 0, 0);
    }
};

// Ragdoll interface - multi-body physics shell for animated characters
// Provides bone-mapped physics bodies with breakable constraints
class IPhysicsRagdoll
{
public:
    virtual ~IPhysicsRagdoll() = default;

    // Build from skeleton
    virtual bool BuildFromKinematics(IKinematics* kinematics, bool create_all_bones = false) = 0;

    // Element management
    virtual RagdollElement* AddElement(u16 bone_id, u16 parent_bone_id) = 0;
    virtual RagdollElement* GetElement(u16 bone_id) = 0;
    virtual const RagdollElement* GetElement(u16 bone_id) const = 0;
    virtual RagdollElement* GetElementByIndex(u16 index) = 0;
    virtual u16 GetElementCount() const = 0;
    virtual void RemoveElement(u16 bone_id) = 0;

    // Shape addition to elements
    virtual void AddSphereToElement(u16 bone_id, const Fvector& center, float radius) = 0;
    virtual void AddBoxToElement(u16 bone_id, const Fvector& center, const Fvector& half_extents, const Fmatrix& rotation) = 0;
    virtual void AddCapsuleToElement(u16 bone_id, const Fvector& center, float radius, float height, const Fmatrix& rotation) = 0;

    // Joint configuration
    virtual void SetJointLimits(u16 bone_id, float low, float high, u32 axis_index = 0) = 0;
    virtual void SetJointSpringDamping(u16 bone_id, float spring, float damping, u32 axis_index = 0) = 0;
    virtual void SetJointBreakable(u16 bone_id, bool breakable, float break_force, float break_torque) = 0;
    virtual bool IsJointBroken(u16 bone_id) const = 0;

    // Mass properties
    virtual void SetElementMass(u16 bone_id, float mass) = 0;
    virtual float GetElementMass(u16 bone_id) const = 0;
    virtual void SetTotalMass(float mass) = 0;
    virtual float GetTotalMass() const = 0;

    // Activation/Deactivation
    virtual void Activate(const Fmatrix& transform) = 0;
    virtual void Activate(const Fmatrix& transform, const Fvector& linear_vel, const Fvector& angular_vel) = 0;
    virtual void Deactivate() = 0;
    virtual bool IsActive() const = 0;

    // Enable/Disable
    virtual void Enable() = 0;
    virtual void Disable() = 0;
    virtual bool IsEnabled() const = 0;

    // Transform
    virtual void GetRootTransform(Fmatrix& transform) const = 0;
    virtual void SetRootTransform(const Fmatrix& transform) = 0;

    // Velocity
    virtual void GetRootLinearVelocity(Fvector& vel) const = 0;
    virtual void GetRootAngularVelocity(Fvector& vel) const = 0;
    virtual void SetRootLinearVelocity(const Fvector& vel) = 0;
    virtual void SetRootAngularVelocity(const Fvector& vel) = 0;

    // Forces and impulses
    virtual void AddForce(const Fvector& force) = 0;
    virtual void AddForceAtBone(u16 bone_id, const Fvector& force, const Fvector& position) = 0;
    virtual void AddImpulse(const Fvector& impulse) = 0;
    virtual void AddImpulseAtBone(u16 bone_id, const Fvector& impulse, const Fvector& position) = 0;

    // Hit system (for damage application)
    virtual void ApplyHit(u16 bone_id, const Fvector& position, const Fvector& direction,
                         float impulse_magnitude) = 0;

    // Collision control
    virtual void SetCollisionGroup(u32 group) = 0;
    virtual u32 GetCollisionGroup() const = 0;
    virtual void SetRagdollCollisionMode(bool enabled) = 0;

    // Material
    virtual void SetMaterial(u16 material_id) = 0;
    virtual void SetFriction(float friction) = 0;
    virtual void SetRestitution(float restitution) = 0;

    // Update bone transforms from physics
    virtual void UpdateBoneTransforms(IKinematics* kinematics) = 0;

    // Network synchronization
    virtual void Serialize(void* packet_data, u32& size) = 0;
    virtual void Deserialize(const void* packet_data, u32 size) = 0;

    // Debugging
    virtual void SetDebugDraw(bool enabled) = 0;

    // User data
    virtual void SetUserData(void* data) = 0;
    virtual void* GetUserData() const = 0;
};

// World interface - main physics simulation
class IPhysicsWorld
{
public:
    virtual ~IPhysicsWorld() = default;

    // Engine type
    virtual PhysicsEngineType GetEngineType() const = 0;

    // Lifecycle
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    // Simulation
    virtual void Step(float dt) = 0;
    virtual void SetGravity(const Fvector& gravity) = 0;
    virtual void GetGravity(Fvector& gravity) const = 0;
    virtual float GetGravityMagnitude() const = 0;

    // Object creation
    virtual IPhysicsBody* CreateBody(PhysicsBodyType type) = 0;
    virtual void DestroyBody(IPhysicsBody* body) = 0;

    virtual IPhysicsShape* CreateSphere(float radius) = 0;
    virtual IPhysicsShape* CreateBox(const Fvector& half_extents) = 0;
    virtual IPhysicsShape* CreateCylinder(float radius, float height) = 0;
    virtual IPhysicsShape* CreateCapsule(float radius, float height) = 0;
    virtual IPhysicsShape* CreateMesh(const Fvector* vertices, u32 vertex_count,
                                      const u32* indices, u32 index_count) = 0;
    virtual void DestroyShape(IPhysicsShape* shape) = 0;

    virtual IPhysicsConstraint* CreateConstraint(PhysicsConstraintType type,
                                                   IPhysicsBody* body1,
                                                   IPhysicsBody* body2) = 0;
    virtual void DestroyConstraint(IPhysicsConstraint* constraint) = 0;

    // Character controllers
    virtual IPhysicsCharacter* CreateCharacter(float radius, float height) = 0;
    virtual void DestroyCharacter(IPhysicsCharacter* character) = 0;

    // Ragdoll systems
    virtual IPhysicsRagdoll* CreateRagdoll() = 0;
    virtual void DestroyRagdoll(IPhysicsRagdoll* ragdoll) = 0;

    // Ray casting
    virtual bool RayCast(const Fvector& origin, const Fvector& direction,
                        float max_distance, PhysicsRayHit& hit_out) = 0;

    // Debugging
    virtual void SetDebugDrawEnabled(bool enabled) = 0;
    virtual bool IsDebugDrawEnabled() const = 0;

    // Statistics
    virtual u32 GetBodyCount() const = 0;
    virtual u32 GetConstraintCount() const = 0;
    virtual u32 GetActiveBodyCount() const = 0;

    // Threading
    virtual void SetThreadCount(u32 count) = 0;
    virtual u32 GetThreadCount() const = 0;
};

// Factory function type
using PhysicsWorldFactory = IPhysicsWorld* (*)();

// Registry for physics implementations
class PhysicsEngineRegistry
{
public:
    static void RegisterEngine(PhysicsEngineType type, PhysicsWorldFactory factory);
    static IPhysicsWorld* CreateWorld(PhysicsEngineType type);
    static bool IsEngineAvailable(PhysicsEngineType type);

private:
    static xr_map<PhysicsEngineType, PhysicsWorldFactory>& GetFactories();
};

// Helper macro for registering engines
#define REGISTER_PHYSICS_ENGINE(type, factory_func) \
    namespace { \
        struct PhysicsEngineRegistrar_##type { \
            PhysicsEngineRegistrar_##type() { \
                PhysicsEngineRegistry::RegisterEngine(PhysicsEngineType::type, factory_func); \
            } \
        }; \
        static PhysicsEngineRegistrar_##type s_registrar_##type; \
    }
