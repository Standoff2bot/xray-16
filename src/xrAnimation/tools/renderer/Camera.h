#pragma once

#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/vec_float.h>

struct GLFWwindow;

namespace xray {
namespace animation {
namespace renderer {

// Clean, simple camera implementation following Vulkan tutorial conventions
class Camera {
public:
    Camera();

    void Initialize(float viewport_width, float viewport_height);
    void Update(float delta_time);

    // Get matrices
    ozz::math::Float4x4 GetViewMatrix() const;
    ozz::math::Float4x4 GetProjectionMatrix() const;
    ozz::math::Float4x4 GetViewProjectionMatrix() const;

    // Camera controls
    void SetPosition(const ozz::math::Float3& position);
    void SetTarget(const ozz::math::Float3& target);
    void SetUp(const ozz::math::Float3& up);

    // Compatibility with old Camera interface
    void Update(GLFWwindow* window, float delta_time);
    void OnMouseButton(int button, int action, int mods, double xpos, double ypos);
    void OnMouseMove(double xpos, double ypos);
    void OnMouseScroll(double xoffset, double yoffset);
    bool SetPivotFromScreen(double xpos, double ypos) { return false; }  // Stub for compatibility
    void SetDistance(float distance);
    float GetDistance() const;
    void Reset() {
        SetPosition(ozz::math::Float3(2.0f, 2.0f, 2.0f));
        SetTarget(ozz::math::Float3(0.0f, 0.0f, 0.0f));
        distance_ = CalculateDistance();
    }

    // Projection settings
    void SetFOV(float fov_degrees);
    void SetNearFar(float near_plane, float far_plane);
    void SetAspectRatio(float aspect);

    // Get camera state
    const ozz::math::Float3& GetPosition() const { return eye_position_; }
    const ozz::math::Float3& GetTarget() const { return look_at_target_; }
    float GetNear() const { return near_plane_; }
    float GetFar() const { return far_plane_; }

private:
    void UpdateMatrices() const;
    float CalculateDistance() const;
    void UpdatePositionFromRotation();
    ozz::math::Quaternion CalculateInitialRotation() const;

    // Camera state
    ozz::math::Float3 eye_position_;
    ozz::math::Float3 look_at_target_;
    ozz::math::Float3 up_vector_;

    // Projection parameters
    float fov_radians_;
    float aspect_ratio_;
    float near_plane_;
    float far_plane_;

    // Camera control state
    float distance_;           // Distance from target
    ozz::math::Quaternion rotation_; // Camera rotation as quaternion

    // Mouse tracking
    double last_mouse_x_;
    double last_mouse_y_;
    bool left_button_down_;
    bool middle_button_down_;
    bool right_button_down_;

    // Cached matrices
    mutable ozz::math::Float4x4 view_matrix_;
    mutable ozz::math::Float4x4 projection_matrix_;
    mutable bool matrices_dirty_;
};

} // namespace renderer
} // namespace animation
} // namespace xray