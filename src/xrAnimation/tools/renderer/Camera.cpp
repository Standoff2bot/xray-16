#include "Camera.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace xray {
namespace animation {
namespace renderer {

namespace {
    constexpr float kPI = 3.14159265358979323846f;
    constexpr float kDefaultDistance = 7.0f;   // Pulled back further to see more
    constexpr float kDefaultFOV = 60.0f;        // Wider FOV for better view
    constexpr float kDefaultNear = 1.0f;        // Closer near plane
    constexpr float kDefaultFar = 50.0f;        // Extended far plane
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
    , pitch_(-0.3f) // Slight downward angle
    , fov_(kDefaultFOV * kPI / 180.0f)
    , near_plane_(kDefaultNear)
    , far_plane_(kDefaultFar)
    , is_rotating_(false)
    , is_panning_(false)
    , is_zooming_(false)
    , move_direction_(0)
    , last_mouse_x_(0.0)
    , last_mouse_y_(0.0)
    , rotation_sensitivity_(0.0025f)
    , pan_sensitivity_(0.0015f)
    , zoom_sensitivity_(0.12f)
    , zoom_drag_sensitivity_(0.004f)
    , move_speed_(3.5f)
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
    (void)window;

    if (move_direction_ != 0 && delta_time > 0.0f) {
        const float step = static_cast<float>(move_direction_) * move_speed_ * delta_time;
        const float sin_yaw = std::sin(yaw_);
        const float cos_yaw = std::cos(yaw_);
        const ozz::math::Float3 forward(-sin_yaw, 0.0f, -cos_yaw);
        target_.x += forward.x * step;
        target_.y += forward.y * step;
        target_.z += forward.z * step;
        matrices_dirty_ = true;
    }

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

void Camera::OnMouseButton(int button, int action, int mods, double xpos, double ypos) {
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            last_mouse_x_ = xpos;
            last_mouse_y_ = ypos;
            if (mods & GLFW_MOD_CONTROL) {
                is_zooming_ = true;
            } else if (mods & GLFW_MOD_SHIFT) {
                is_panning_ = true;
            } else {
                is_rotating_ = true;
            }
        } else if (action == GLFW_RELEASE) {
            is_rotating_ = false;
            is_panning_ = false;
            is_zooming_ = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            move_direction_ = (mods & GLFW_MOD_SHIFT) ? -1 : 1;
        } else if (action == GLFW_RELEASE) {
            move_direction_ = 0;
        }
    }
}

void Camera::OnMouseMove(double xpos, double ypos) {
    const double dx = xpos - last_mouse_x_;
    const double dy = ypos - last_mouse_y_;

    if (is_rotating_) {
        yaw_ -= static_cast<float>(dx) * rotation_sensitivity_;
        pitch_ += static_cast<float>(dy) * rotation_sensitivity_;
        pitch_ = std::clamp(pitch_, kMinPitch, kMaxPitch);
        matrices_dirty_ = true;
    } else if (is_panning_) {
        const float cos_pitch = std::cos(pitch_);
        const float sin_pitch = std::sin(pitch_);
        const float cos_yaw = std::cos(yaw_);
        const float sin_yaw = std::sin(yaw_);

        const ozz::math::Float3 right(cos_yaw, 0.0f, -sin_yaw);
        const ozz::math::Float3 up(
            sin_yaw * sin_pitch,
            cos_pitch,
            cos_yaw * sin_pitch
        );

        const float pan_scale = distance_ * pan_sensitivity_;
        const float dx_f = static_cast<float>(dx);
        const float inv_dy = -static_cast<float>(dy); // invert Y axis
        target_.x -= (right.x * dx_f + up.x * inv_dy) * pan_scale;
        target_.y -= (right.y * dx_f + up.y * inv_dy) * pan_scale;
        target_.z -= (right.z * dx_f + up.z * inv_dy) * pan_scale;
        matrices_dirty_ = true;
    } else if (is_zooming_) {
        const float zoom_delta = static_cast<float>(dy) * zoom_drag_sensitivity_;
        const float factor = std::exp(zoom_delta);
        SetDistance(distance_ * factor);
    }

    last_mouse_x_ = xpos;
    last_mouse_y_ = ypos;
}

void Camera::OnMouseScroll(double xoffset, double yoffset) {
    (void)xoffset;
    const float factor = std::exp(-static_cast<float>(yoffset) * zoom_sensitivity_);
    SetDistance(distance_ * factor);
}

ozz::math::Float3 Camera::GetPosition() const {
    return CalculateCameraPosition();
}

bool Camera::SetPivotFromScreen(double xpos, double ypos) {
    ozz::math::Float3 hit_point;
    if (!RaycastGround(xpos, ypos, hit_point)) {
        return false;
    }
    hit_point.y = 0.0f;
    SetPivot(hit_point, true);
    return true;
}

void Camera::SetPivot(const ozz::math::Float3& pivot, bool maintain_camera_position) {
    ozz::math::Float3 current_position;
    if (maintain_camera_position) {
        current_position = CalculateCameraPosition();
    }
    target_ = pivot;
    if (maintain_camera_position) {
        RecomputeOrbitFromPosition(current_position);
    }
    matrices_dirty_ = true;
}

bool Camera::RaycastGround(double screen_x, double screen_y, ozz::math::Float3& out_point) const {
    if (viewport_width_ <= 0.0f || viewport_height_ <= 0.0f) {
        return false;
    }

    const float ndc_x = static_cast<float>((2.0 * screen_x / viewport_width_) - 1.0);
    const float ndc_y = static_cast<float>(1.0 - (2.0 * screen_y / viewport_height_));

    const ozz::math::Float4x4 inv_view_proj = ozz::math::Invert(GetViewProjectionMatrix());

    const ozz::math::SimdFloat4 clip_far = ozz::math::simd_float4::Load(ndc_x, ndc_y, 1.0f, 1.0f);
    const ozz::math::SimdFloat4 world_far = inv_view_proj * clip_far;
    const float far_w = ozz::math::GetW(world_far);
    if (std::abs(far_w) < 1e-6f) {
        return false;
    }

    const ozz::math::Float3 camera_pos = CalculateCameraPosition();
    const ozz::math::Float3 far_point(
        ozz::math::GetX(world_far) / far_w,
        ozz::math::GetY(world_far) / far_w,
        ozz::math::GetZ(world_far) / far_w
    );

    ozz::math::Float3 direction(
        far_point.x - camera_pos.x,
        far_point.y - camera_pos.y,
        far_point.z - camera_pos.z
    );

    const float dir_length_sq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (dir_length_sq < 1e-6f) {
        return false;
    }

    const float dir_length = std::sqrt(dir_length_sq);
    direction.x /= dir_length;
    direction.y /= dir_length;
    direction.z /= dir_length;

    if (std::abs(direction.y) < 1e-6f) {
        return false;
    }

    const float t = -camera_pos.y / direction.y;
    if (t < 0.0f) {
        return false;
    }

    out_point = ozz::math::Float3(
        camera_pos.x + direction.x * t,
        0.0f,
        camera_pos.z + direction.z * t
    );
    return true;
}

void Camera::RecomputeOrbitFromPosition(const ozz::math::Float3& position) {
    ozz::math::Float3 offset(
        position.x - target_.x,
        position.y - target_.y,
        position.z - target_.z
    );
    float distance = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
    distance = std::clamp(distance, kMinDistance, kMaxDistance);
    distance_ = distance;

    if (distance_ > 1e-5f) {
        float sin_pitch = offset.y / distance_;
        sin_pitch = std::clamp(sin_pitch, -0.9999f, 0.9999f);
        pitch_ = std::clamp(std::asin(sin_pitch), kMinPitch, kMaxPitch);
        const float cos_pitch = std::cos(pitch_);
        if (std::abs(cos_pitch) > 1e-5f) {
            yaw_ = std::atan2(offset.x, offset.z);
        }
    } else {
        pitch_ = 0.0f;
        yaw_ = 0.0f;
    }
}

void Camera::Reset() {
    target_ = ozz::math::Float3::zero();
    distance_ = kDefaultDistance;
    yaw_ = 0.0f;
    pitch_ = -0.3f;  // Slight downward angle
    fov_ = kDefaultFOV * kPI / 180.0f;
    near_plane_ = kDefaultNear;
    far_plane_ = kDefaultFar;
    is_rotating_ = false;
    is_panning_ = false;
    is_zooming_ = false;
    move_direction_ = 0;
    last_mouse_x_ = 0.0;
    last_mouse_y_ = 0.0;
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
    // ozz uses COLUMN-MAJOR: each cols[i] is a COLUMN vector
    view_matrix_.cols[0] = ozz::math::simd_float4::Load(right.x, right.y, right.z, 0.0f);
    view_matrix_.cols[1] = ozz::math::simd_float4::Load(up.x, up.y, up.z, 0.0f);
    view_matrix_.cols[2] = ozz::math::simd_float4::Load(-forward.x, -forward.y, -forward.z, 0.0f);
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
    // Standard Vulkan perspective projection: maps z from [near, far] to [0, 1]
    // Formula: z_ndc = (far/(far-near)) * z_clip - (far*near/(far-near))
    // After perspective divide: z_ndc = [(far/(far-near)) * (-z_view) - (far*near/(far-near))] / (-z_view)
    //                                 = far/(far-near) + (far*near)/(z_view*(far-near))
    const float range = near_plane_ - far_plane_;
    projection_matrix_.cols[0] = ozz::math::simd_float4::Load(f / aspect_ratio_, 0.0f, 0.0f, 0.0f);
    projection_matrix_.cols[1] = ozz::math::simd_float4::Load(0.0f, -f, 0.0f, 0.0f); // Flip Y for Vulkan
    projection_matrix_.cols[2] = ozz::math::simd_float4::Load(
        0.0f,
        0.0f,
        far_plane_ / range,  // Negative because range = near - far
        -1.0f  // Perspective divide: w_clip = -z_view
    );
    projection_matrix_.cols[3] = ozz::math::simd_float4::Load(
        0.0f,
        0.0f,
        (far_plane_ * near_plane_) / range,  // Matches Vulkan [0,1] depth mapping
        0.0f
    );

    // DEBUG: Print projection matrix AND view matrix ONCE to verify they're correct
    static bool printed_matrices = false;
    if (!printed_matrices) {
        printf("=== VIEW Matrix (rows) ===\n");
        printf("  [%.3f, %.3f, %.3f, %.3f]\n",
            ozz::math::GetX(view_matrix_.cols[0]),
            ozz::math::GetX(view_matrix_.cols[1]),
            ozz::math::GetX(view_matrix_.cols[2]),
            ozz::math::GetX(view_matrix_.cols[3]));
        printf("  [%.3f, %.3f, %.3f, %.3f]\n",
            ozz::math::GetY(view_matrix_.cols[0]),
            ozz::math::GetY(view_matrix_.cols[1]),
            ozz::math::GetY(view_matrix_.cols[2]),
            ozz::math::GetY(view_matrix_.cols[3]));
        printf("  [%.3f, %.3f, %.3f, %.3f]\n",
            ozz::math::GetZ(view_matrix_.cols[0]),
            ozz::math::GetZ(view_matrix_.cols[1]),
            ozz::math::GetZ(view_matrix_.cols[2]),
            ozz::math::GetZ(view_matrix_.cols[3]));
        printf("  [%.3f, %.3f, %.3f, %.3f]\n",
            ozz::math::GetW(view_matrix_.cols[0]),
            ozz::math::GetW(view_matrix_.cols[1]),
            ozz::math::GetW(view_matrix_.cols[2]),
            ozz::math::GetW(view_matrix_.cols[3]));

        printf("=== PROJECTION Matrix (rows) ===\n");
        printf("  [%.3f, %.3f, %.3f, %.3f]\n",
            ozz::math::GetX(projection_matrix_.cols[0]),
            ozz::math::GetX(projection_matrix_.cols[1]),
            ozz::math::GetX(projection_matrix_.cols[2]),
            ozz::math::GetX(projection_matrix_.cols[3]));
        printf("  [%.3f, %.3f, %.3f, %.3f]\n",
            ozz::math::GetY(projection_matrix_.cols[0]),
            ozz::math::GetY(projection_matrix_.cols[1]),
            ozz::math::GetY(projection_matrix_.cols[2]),
            ozz::math::GetY(projection_matrix_.cols[3]));
        printf("  [%.3f, %.3f, %.3f, %.3f]\n",
            ozz::math::GetZ(projection_matrix_.cols[0]),
            ozz::math::GetZ(projection_matrix_.cols[1]),
            ozz::math::GetZ(projection_matrix_.cols[2]),
            ozz::math::GetZ(projection_matrix_.cols[3]));
        printf("  [%.3f, %.3f, %.3f, %.3f]\n",
            ozz::math::GetW(projection_matrix_.cols[0]),
            ozz::math::GetW(projection_matrix_.cols[1]),
            ozz::math::GetW(projection_matrix_.cols[2]),
            ozz::math::GetW(projection_matrix_.cols[3]));
        printf("  near=%.3f, far=%.3f\n", near_plane_, far_plane_);

        printf("  camera_pos=[%.3f, %.3f, %.3f]\n", position.x, position.y, position.z);
        printf("  target=[%.3f, %.3f, %.3f]\n", target_.x, target_.y, target_.z);
        printf("  distance=%.3f\n", distance_);

        printed_matrices = true;
    }

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
