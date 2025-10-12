#include "Camera.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>

namespace xray {
namespace animation {
namespace renderer {

namespace {
    constexpr float kPI = 3.14159265358979323846f;

    ozz::math::Float4x4 LookAt(const ozz::math::Float3& eye,
                               const ozz::math::Float3& center,
                               const ozz::math::Float3& up) {
        ozz::math::Float3 f = center - eye;
        float f_len = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
        f.x /= f_len;
        f.y /= f_len;
        f.z /= f_len;

        ozz::math::Float3 s;
        s.x = f.y * up.z - f.z * up.y;
        s.y = f.z * up.x - f.x * up.z;
        s.z = f.x * up.y - f.y * up.x;
        float s_len = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
        s.x /= s_len;
        s.y /= s_len;
        s.z /= s_len;

        ozz::math::Float3 u;
        u.x = s.y * f.z - s.z * f.y;
        u.y = s.z * f.x - s.x * f.z;
        u.z = s.x * f.y - s.y * f.x;

        ozz::math::Float4x4 result;
        result.cols[0] = ozz::math::simd_float4::Load(s.x, u.x, -f.x, 0.0f);
        result.cols[1] = ozz::math::simd_float4::Load(s.y, u.y, -f.y, 0.0f);
        result.cols[2] = ozz::math::simd_float4::Load(s.z, u.z, -f.z, 0.0f);
        result.cols[3] = ozz::math::simd_float4::Load(
            -(s.x * eye.x + s.y * eye.y + s.z * eye.z),
            -(u.x * eye.x + u.y * eye.y + u.z * eye.z),
            f.x * eye.x + f.y * eye.y + f.z * eye.z,
            1.0f
        );

        return result;
    }

    ozz::math::Float4x4 Perspective(float fov_radians, float aspect, float near_plane, float far_plane) {
        const float tan_half_fov = std::tan(fov_radians / 2.0f);

        ozz::math::Float4x4 result;

        result.cols[0] = ozz::math::simd_float4::Load(1.0f / (aspect * tan_half_fov), 0.0f, 0.0f, 0.0f);
        result.cols[1] = ozz::math::simd_float4::Load(0.0f, -1.0f / tan_half_fov, 0.0f, 0.0f);
        result.cols[2] = ozz::math::simd_float4::Load(
            0.0f,
            0.0f,
            far_plane / (near_plane - far_plane),
            -1.0f
        );
        result.cols[3] = ozz::math::simd_float4::Load(
            0.0f,
            0.0f,
            -(far_plane * near_plane) / (far_plane - near_plane),
            0.0f
        );

        return result;
    }
}

Camera::Camera()
    : eye_position_(2.0f, 2.0f, 2.0f)  // Default: looking from above at angle
    , look_at_target_(0.0f, 0.0f, 0.0f)  // Looking at origin
    , up_vector_(0.0f, 1.0f, 0.0f)  // Y-up (modified for our coordinate system)
    , fov_radians_(45.0f * kPI / 180.0f)
    , aspect_ratio_(16.0f / 9.0f)
    , near_plane_(0.1f)
    , far_plane_(100.0f)
    , matrices_dirty_(true) {
}

void Camera::Initialize(float viewport_width, float viewport_height) {
    aspect_ratio_ = viewport_width / viewport_height;
    matrices_dirty_ = true;
}

void Camera::Update(float delta_time) {
    // Simple camera doesn't auto-update; controlled externally
    (void)delta_time;

    if (matrices_dirty_) {
        UpdateMatrices();
    }
}

void Camera::Update(GLFWwindow* window, float delta_time) {
    // Compatibility with old Camera interface
    (void)window;
    Update(delta_time);
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

void Camera::SetPosition(const ozz::math::Float3& position) {
    eye_position_ = position;
    matrices_dirty_ = true;
}

void Camera::SetTarget(const ozz::math::Float3& target) {
    look_at_target_ = target;
    matrices_dirty_ = true;
}

void Camera::SetUp(const ozz::math::Float3& up) {
    up_vector_ = up;
    matrices_dirty_ = true;
}

void Camera::SetFOV(float fov_degrees) {
    fov_radians_ = fov_degrees * kPI / 180.0f;
    matrices_dirty_ = true;
}

void Camera::SetNearFar(float near_plane, float far_plane) {
    near_plane_ = near_plane;
    far_plane_ = far_plane;
    matrices_dirty_ = true;
}

void Camera::SetAspectRatio(float aspect) {
    aspect_ratio_ = aspect;
    matrices_dirty_ = true;
}

void Camera::UpdateMatrices() const {
    // Build view matrix using look-at
    view_matrix_ = LookAt(eye_position_, look_at_target_, up_vector_);

    // Build projection matrix
    projection_matrix_ = Perspective(fov_radians_, aspect_ratio_, near_plane_, far_plane_);

    // Debug output (first frame only)
    static bool printed = false;
    if (!printed) {
        printf("=== Camera Matrices (Clean Implementation) ===\n");
        printf("Eye: (%.2f, %.2f, %.2f)\n", eye_position_.x, eye_position_.y, eye_position_.z);
        printf("Target: (%.2f, %.2f, %.2f)\n", look_at_target_.x, look_at_target_.y, look_at_target_.z);
        printf("Near: %.2f, Far: %.2f\n", near_plane_, far_plane_);

        printf("View Matrix (actual rows - transposed from column-major storage):\n");
        for (int row = 0; row < 4; ++row) {
            printf("  [%.3f, %.3f, %.3f, %.3f]\n",
                row == 0 ? ozz::math::GetX(view_matrix_.cols[0]) : (row == 1 ? ozz::math::GetY(view_matrix_.cols[0]) : (row == 2 ? ozz::math::GetZ(view_matrix_.cols[0]) : ozz::math::GetW(view_matrix_.cols[0]))),
                row == 0 ? ozz::math::GetX(view_matrix_.cols[1]) : (row == 1 ? ozz::math::GetY(view_matrix_.cols[1]) : (row == 2 ? ozz::math::GetZ(view_matrix_.cols[1]) : ozz::math::GetW(view_matrix_.cols[1]))),
                row == 0 ? ozz::math::GetX(view_matrix_.cols[2]) : (row == 1 ? ozz::math::GetY(view_matrix_.cols[2]) : (row == 2 ? ozz::math::GetZ(view_matrix_.cols[2]) : ozz::math::GetW(view_matrix_.cols[2]))),
                row == 0 ? ozz::math::GetX(view_matrix_.cols[3]) : (row == 1 ? ozz::math::GetY(view_matrix_.cols[3]) : (row == 2 ? ozz::math::GetZ(view_matrix_.cols[3]) : ozz::math::GetW(view_matrix_.cols[3]))));
        }

        printf("Projection Matrix (actual rows - transposed from column-major storage):\n");
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

        printed = true;
    }

    matrices_dirty_ = false;
}

} // namespace renderer
} // namespace animation
} // namespace xray