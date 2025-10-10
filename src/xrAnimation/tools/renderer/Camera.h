#pragma once

#include "ozz/base/maths/vec_float.h"
#include "ozz/base/maths/simd_math.h"

struct GLFWwindow;

namespace xray {
namespace animation {
namespace renderer {

// Arcball camera controller for 3D viewport navigation
class Camera {
public:
    Camera();
    ~Camera() = default;

    // Initialize camera with window dimensions
    void Initialize(float viewport_width, float viewport_height);

    // Update camera based on input (call each frame)
    void Update(GLFWwindow* window, float delta_time);

    // Get view matrix (world -> camera space)
    ozz::math::Float4x4 GetViewMatrix() const;

    // Get projection matrix (camera space -> clip space)
    ozz::math::Float4x4 GetProjectionMatrix() const;

    // Get combined view-projection matrix
    ozz::math::Float4x4 GetViewProjectionMatrix() const;

    // Camera control settings
    void SetDistance(float distance);
    void SetTarget(const ozz::math::Float3& target);
    void SetFOV(float fov_degrees);
    void SetNearFar(float near_plane, float far_plane);

    // Mouse input handling (call from GLFW callbacks)
    void OnMouseButton(int button, int action, double xpos, double ypos);
    void OnMouseMove(double xpos, double ypos);
    void OnMouseScroll(double xoffset, double yoffset);

    // Getters
    float GetDistance() const { return distance_; }
    ozz::math::Float3 GetTarget() const { return target_; }
    ozz::math::Float3 GetPosition() const;

    // Reset camera to default position
    void Reset();

private:
    // Viewport dimensions
    float viewport_width_;
    float viewport_height_;
    float aspect_ratio_;

    // Camera parameters
    ozz::math::Float3 target_;        // Look-at target point
    float distance_;                   // Distance from target
    float yaw_;                        // Horizontal rotation (radians)
    float pitch_;                      // Vertical rotation (radians)

    // Projection parameters
    float fov_;                        // Field of view in radians
    float near_plane_;
    float far_plane_;

    // Mouse interaction state
    bool is_rotating_;
    bool is_panning_;
    double last_mouse_x_;
    double last_mouse_y_;

    // Camera control sensitivity
    float rotation_sensitivity_;
    float pan_sensitivity_;
    float zoom_sensitivity_;

    // Cached matrices
    mutable ozz::math::Float4x4 view_matrix_;
    mutable ozz::math::Float4x4 projection_matrix_;
    mutable bool matrices_dirty_;

    // Helper functions
    void UpdateMatrices() const;
    ozz::math::Float3 CalculateCameraPosition() const;
};

} // namespace renderer
} // namespace animation
} // namespace xray
