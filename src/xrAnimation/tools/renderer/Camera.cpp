#include "Camera.h"
#include <GLFW/glfw3.h>
#include <cmath>

namespace xray {
namespace animation {
namespace renderer {

namespace {
    constexpr float kPI = 3.14159265358979323846f;
    constexpr float kDefaultDistance = 3.5f;
    constexpr float kDefaultFOV = 45.0f;
    constexpr float kDefaultNear = 0.1f;  // Increased from 0.1 to improve depth precision
    constexpr float kDefaultFar = 1000.0f; // Decreased from 1000 to improve depth precision (200:1 ratio)
    constexpr float kMinDistance = 0.1f;
    constexpr float kMaxDistance = 100.0f;
    constexpr float kMinPitch = -kPI * 0.49f; // Just below 90 degrees
    constexpr float kMaxPitch = kPI * 0.49f;  // Just below 90 degrees
}

Camera::Camera()
    : viewport_width_(1280.0f)
    , viewport_height_(720.0f)
    , aspect_ratio_(1280.0f / 720.0f)
    , target_(ozz::math::Float3::zero())
    , distance_(kDefaultDistance)
    , yaw_(0.0f)
    , pitch_(0.3f) // Slight downward angle
    , fov_(kDefaultFOV * kPI / 180.0f)
    , near_plane_(kDefaultNear)
    , far_plane_(kDefaultFar)
    , is_rotating_(false)
    , is_panning_(false)
    , last_mouse_x_(0.0)
    , last_mouse_y_(0.0)
    , rotation_sensitivity_(0.005f)
    , pan_sensitivity_(0.01f)
    , zoom_sensitivity_(0.1f)
    , matrices_dirty_(true)
{
}

void Camera::Initialize(float viewport_width, float viewport_height) {
    viewport_width_ = viewport_width;
    viewport_height_ = viewport_height;
    aspect_ratio_ = viewport_width / viewport_height;
    matrices_dirty_ = true;
}

void Camera::Update(GLFWwindow* window, float delta_time) {
    // Update matrices if needed
    if (matrices_dirty_) {
        UpdateMatrices();
    }
}

ozz::math::Float4x4 Camera::GetViewMatrix() const {
    if (matrices_dirty_) {
        UpdateMatrices();
    }
    return view_matrix_;
}

ozz::math::Float4x4 Camera::GetProjectionMatrix() const {
    if (matrices_dirty_) {
        UpdateMatrices();
    }
    return projection_matrix_;
}

ozz::math::Float4x4 Camera::GetViewProjectionMatrix() const {
    if (matrices_dirty_) {
        UpdateMatrices();
    }
    return projection_matrix_ * view_matrix_;
}

void Camera::SetDistance(float distance) {
    distance_ = std::max(kMinDistance, std::min(distance, kMaxDistance));
    matrices_dirty_ = true;
}

void Camera::SetTarget(const ozz::math::Float3& target) {
    target_ = target;
    matrices_dirty_ = true;
}

void Camera::SetFOV(float fov_degrees) {
    fov_ = fov_degrees * kPI / 180.0f;
    matrices_dirty_ = true;
}

void Camera::SetNearFar(float near_plane, float far_plane) {
    near_plane_ = near_plane;
    far_plane_ = far_plane;
    matrices_dirty_ = true;
}

void Camera::OnMouseButton(int button, int action, double xpos, double ypos) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            is_rotating_ = true;
            last_mouse_x_ = xpos;
            last_mouse_y_ = ypos;
        } else if (action == GLFW_RELEASE) {
            is_rotating_ = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            is_panning_ = true;
            last_mouse_x_ = xpos;
            last_mouse_y_ = ypos;
        } else if (action == GLFW_RELEASE) {
            is_panning_ = false;
        }
    }
}

void Camera::OnMouseMove(double xpos, double ypos) {
    double dx = xpos - last_mouse_x_;
    double dy = ypos - last_mouse_y_;

    if (is_rotating_) {
        // Update yaw and pitch
        yaw_ -= static_cast<float>(dx) * rotation_sensitivity_;
        pitch_ -= static_cast<float>(dy) * rotation_sensitivity_;

        // Clamp pitch to avoid gimbal lock
        pitch_ = std::max(kMinPitch, std::min(pitch_, kMaxPitch));

        matrices_dirty_ = true;
    } else if (is_panning_) {
        // Calculate camera right and up vectors
        float cos_pitch = std::cos(pitch_);
        float sin_pitch = std::sin(pitch_);
        float cos_yaw = std::cos(yaw_);
        float sin_yaw = std::sin(yaw_);

        // Right vector (perpendicular to forward)
        ozz::math::Float3 right = ozz::math::Float3(cos_yaw, 0.0f, -sin_yaw);
        // Up vector
        ozz::math::Float3 up = ozz::math::Float3(
            sin_yaw * sin_pitch,
            cos_pitch,
            cos_yaw * sin_pitch
        );

        // Pan the target
        float pan_scale = distance_ * pan_sensitivity_;
        target_.x -= (right.x * static_cast<float>(dx) + up.x * static_cast<float>(dy)) * pan_scale;
        target_.y -= (right.y * static_cast<float>(dx) + up.y * static_cast<float>(dy)) * pan_scale;
        target_.z -= (right.z * static_cast<float>(dx) + up.z * static_cast<float>(dy)) * pan_scale;

        matrices_dirty_ = true;
    }

    last_mouse_x_ = xpos;
    last_mouse_y_ = ypos;
}

void Camera::OnMouseScroll(double xoffset, double yoffset) {
    // Zoom by adjusting distance
    float zoom_factor = 1.0f - static_cast<float>(yoffset) * zoom_sensitivity_;
    SetDistance(distance_ * zoom_factor);
}

ozz::math::Float3 Camera::GetPosition() const {
    return CalculateCameraPosition();
}

void Camera::Reset() {
    target_ = ozz::math::Float3::zero();
    distance_ = kDefaultDistance;
    yaw_ = 0.0f;
    pitch_ = 0.3f;
    matrices_dirty_ = true;
}

void Camera::UpdateMatrices() const {
    // Calculate camera position
    ozz::math::Float3 position = CalculateCameraPosition();

    // Build view matrix (look-at)
    ozz::math::Float3 forward = target_ - position;
    forward = Normalize(forward);

    ozz::math::Float3 world_up = ozz::math::Float3::y_axis();
    ozz::math::Float3 right = Cross(forward, world_up);
    right = Normalize(right);

    ozz::math::Float3 up = Cross(right, forward);

    // View matrix (world -> camera space)
    view_matrix_.cols[0] = ozz::math::simd_float4::Load(right.x, up.x, -forward.x, 0.0f);
    view_matrix_.cols[1] = ozz::math::simd_float4::Load(right.y, up.y, -forward.y, 0.0f);
    view_matrix_.cols[2] = ozz::math::simd_float4::Load(right.z, up.z, -forward.z, 0.0f);
    view_matrix_.cols[3] = ozz::math::simd_float4::Load(
        -Dot(right, position),
        -Dot(up, position),
        Dot(forward, position),
        1.0f
    );

    // Projection matrix (perspective)
    float tan_half_fov = std::tan(fov_ / 2.0f);
    float f = 1.0f / tan_half_fov;

    // Vulkan NDC: x: [-1, 1], y: [-1, 1], z: [0, 1]
    projection_matrix_.cols[0] = ozz::math::simd_float4::Load(f / aspect_ratio_, 0.0f, 0.0f, 0.0f);
    projection_matrix_.cols[1] = ozz::math::simd_float4::Load(0.0f, -f, 0.0f, 0.0f); // Flip Y for Vulkan
    projection_matrix_.cols[2] = ozz::math::simd_float4::Load(
        0.0f,
        0.0f,
        far_plane_ / (near_plane_ - far_plane_),
        -1.0f
    );
    projection_matrix_.cols[3] = ozz::math::simd_float4::Load(
        0.0f,
        0.0f,
        -(far_plane_ * near_plane_) / (far_plane_ - near_plane_),
        0.0f
    );

    matrices_dirty_ = false;
}

ozz::math::Float3 Camera::CalculateCameraPosition() const {
    // Spherical to Cartesian coordinates
    float cos_pitch = std::cos(pitch_);
    float sin_pitch = std::sin(pitch_);
    float cos_yaw = std::cos(yaw_);
    float sin_yaw = std::sin(yaw_);

    ozz::math::Float3 offset(
        distance_ * cos_pitch * sin_yaw,
        distance_ * sin_pitch,
        distance_ * cos_pitch * cos_yaw
    );

    return target_ + offset;
}

} // namespace renderer
} // namespace animation
} // namespace xray
