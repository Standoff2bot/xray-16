#include "stdafx.h"
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
    , near_plane_(0.5f)    // CRITICAL: Increased from 0.1 for better depth precision
    , far_plane_(50.0f)    // CRITICAL: Reduced from 100.0 for better depth precision
    , distance_(3.464f)    // Default distance (sqrt(2^2 + 2^2 + 2^2))
    , rotation_(ozz::math::Quaternion::identity())
    , last_mouse_x_(0.0)
    , last_mouse_y_(0.0)
    , left_button_down_(false)
    , middle_button_down_(false)
    , right_button_down_(false)
    , matrices_dirty_(true) {
    // Near/far ratio of 1:100 provides much better depth buffer precision
    // than 1:1000. With scene at ~3.5 units from camera, this gives adequate
    // range while maintaining precision.

    // Calculate initial rotation from current position and target
    rotation_ = CalculateInitialRotation();
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

float Camera::CalculateDistance() const {
    const ozz::math::Float3 delta = eye_position_ - look_at_target_;
    return std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
}

ozz::math::Quaternion Camera::CalculateInitialRotation() const {
    // Calculate direction from target to camera
    ozz::math::Float3 direction = eye_position_ - look_at_target_;
    const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len > 0.0001f) {
        direction.x /= len;
        direction.y /= len;
        direction.z /= len;
    } else {
        direction = ozz::math::Float3(0.0f, 0.0f, 1.0f);
    }

    // Calculate rotation from default forward direction (-Z) to current direction
    const ozz::math::Float3 default_forward(0.0f, 0.0f, -1.0f);

    // Cross product to get rotation axis
    ozz::math::Float3 axis;
    axis.x = default_forward.y * direction.z - default_forward.z * direction.y;
    axis.y = default_forward.z * direction.x - default_forward.x * direction.z;
    axis.z = default_forward.x * direction.y - default_forward.y * direction.x;

    // Dot product to get rotation angle
    const float dot = default_forward.x * direction.x + default_forward.y * direction.y + default_forward.z * direction.z;
    const float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));

    // Handle parallel/antiparallel cases
    const float axis_len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (axis_len < 0.0001f) {
        if (dot > 0.0f) {
            return ozz::math::Quaternion::identity();
        } else {
            return ozz::math::Quaternion(0.0f, 1.0f, 0.0f, 0.0f);  // 180 degree rotation around Y
        }
    }

    // Normalize axis
    axis.x /= axis_len;
    axis.y /= axis_len;
    axis.z /= axis_len;

    // Create quaternion from axis-angle
    const float half_angle = angle * 0.5f;
    const float sin_half = std::sin(half_angle);
    return ozz::math::Quaternion(
        axis.x * sin_half,
        axis.y * sin_half,
        axis.z * sin_half,
        std::cos(half_angle)
    );
}

void Camera::UpdatePositionFromRotation() {
    // Apply rotation to default forward direction (-Z)
    const ozz::math::Float3 default_forward(0.0f, 0.0f, -1.0f);

    // Rotate forward vector by quaternion
    const float qx = rotation_.x;
    const float qy = rotation_.y;
    const float qz = rotation_.z;
    const float qw = rotation_.w;

    const float ix = qw * default_forward.x + qy * default_forward.z - qz * default_forward.y;
    const float iy = qw * default_forward.y + qz * default_forward.x - qx * default_forward.z;
    const float iz = qw * default_forward.z + qx * default_forward.y - qy * default_forward.x;
    const float iw = -qx * default_forward.x - qy * default_forward.y - qz * default_forward.z;

    ozz::math::Float3 direction;
    direction.x = ix * qw + iw * -qx + iy * -qz - iz * -qy;
    direction.y = iy * qw + iw * -qy + iz * -qx - ix * -qz;
    direction.z = iz * qw + iw * -qz + ix * -qy - iy * -qx;

    // Position camera at distance along direction from target
    eye_position_.x = look_at_target_.x + direction.x * distance_;
    eye_position_.y = look_at_target_.y + direction.y * distance_;
    eye_position_.z = look_at_target_.z + direction.z * distance_;

    matrices_dirty_ = true;
}

void Camera::SetDistance(float distance) {
    distance_ = std::max(0.1f, distance);
    UpdatePositionFromRotation();
}

float Camera::GetDistance() const {
    return distance_;
}

void Camera::OnMouseButton(int button, int action, int mods, double xpos, double ypos) {
    const bool pressed = (action == 1);  // GLFW_PRESS = 1

    if (button == 0) {  // GLFW_MOUSE_BUTTON_LEFT
        left_button_down_ = pressed;
    } else if (button == 1) {  // GLFW_MOUSE_BUTTON_RIGHT
        right_button_down_ = pressed;
    } else if (button == 2) {  // GLFW_MOUSE_BUTTON_MIDDLE
        middle_button_down_ = pressed;
    }

    if (pressed) {
        last_mouse_x_ = xpos;
        last_mouse_y_ = ypos;
    }
}

void Camera::OnMouseMove(double xpos, double ypos) {
    const double dx = xpos - last_mouse_x_;
    const double dy = ypos - last_mouse_y_;

    last_mouse_x_ = xpos;
    last_mouse_y_ = ypos;

    // Rotation (left mouse button)
    if (left_button_down_) {
        const float rotation_speed = 0.005f;

        // Horizontal rotation (yaw) around world Y axis
        if (std::abs(dx) > 0.001) {
            const float yaw_angle = static_cast<float>(-dx) * rotation_speed;
            const float sin_half = std::sin(yaw_angle * 0.5f);
            const float cos_half = std::cos(yaw_angle * 0.5f);
            const ozz::math::Quaternion yaw_rotation(0.0f, sin_half, 0.0f, cos_half);
            rotation_ = yaw_rotation * rotation_;
        }

        // Vertical rotation (pitch) around local X axis
        if (std::abs(dy) > 0.001) {
            const float pitch_angle = static_cast<float>(-dy) * rotation_speed;
            const float sin_half = std::sin(pitch_angle * 0.5f);
            const float cos_half = std::cos(pitch_angle * 0.5f);
            const ozz::math::Quaternion pitch_rotation(sin_half, 0.0f, 0.0f, cos_half);
            rotation_ = rotation_ * pitch_rotation;
        }

        // Normalize quaternion to prevent drift
        const float len = std::sqrt(rotation_.x * rotation_.x + rotation_.y * rotation_.y +
                                    rotation_.z * rotation_.z + rotation_.w * rotation_.w);
        if (len > 0.0001f) {
            rotation_.x /= len;
            rotation_.y /= len;
            rotation_.z /= len;
            rotation_.w /= len;
        }

        UpdatePositionFromRotation();
    }

    // Panning (middle mouse button or right mouse button)
    if (middle_button_down_ || right_button_down_) {
        const float pan_speed = 0.0001f * distance_;

        // Calculate camera right and up vectors from rotation
        const float qx = rotation_.x;
        const float qy = rotation_.y;
        const float qz = rotation_.z;
        const float qw = rotation_.w;

        // Right vector (rotate world X by quaternion)
        const float rx = 1.0f - 2.0f * (qy * qy + qz * qz);
        const float ry = 2.0f * (qx * qy + qz * qw);
        const float rz = 2.0f * (qx * qz - qy * qw);

        // Up vector (rotate world Y by quaternion)
        const float ux = 2.0f * (qx * qy - qz * qw);
        const float uy = 1.0f - 2.0f * (qx * qx + qz * qz);
        const float uz = 2.0f * (qy * qz + qx * qw);

        // Pan target and camera position
        const float pan_x = static_cast<float>(-dx) * pan_speed;
        const float pan_y = static_cast<float>(dy) * pan_speed;

        look_at_target_.x += rx * pan_x + ux * pan_y;
        look_at_target_.y += ry * pan_x + uy * pan_y;
        look_at_target_.z += rz * pan_x + uz * pan_y;

        UpdatePositionFromRotation();
    }
}

void Camera::OnMouseScroll(double xoffset, double yoffset) {
    const float zoom_speed = 0.1f;
    const float zoom_factor = 1.0f - static_cast<float>(yoffset) * zoom_speed;

    distance_ *= zoom_factor;
    distance_ = std::clamp(distance_, 0.5f, 50.0f);

    UpdatePositionFromRotation();
}

void Camera::UpdateMatrices() const {
    // Build view matrix using look-at
    view_matrix_ = LookAt(eye_position_, look_at_target_, up_vector_);

    // Build projection matrix
    projection_matrix_ = Perspective(fov_radians_, aspect_ratio_, near_plane_, far_plane_);

    // Debug output (first frame only) - COMPREHENSIVE
    static bool printed = false;
    if (!printed) {
        printf("\n");
        printf("====================================================================\n");
        printf("=== COMPREHENSIVE CAMERA & DEPTH DEBUG ===\n");
        printf("====================================================================\n");
        printf("Camera Position: (%.3f, %.3f, %.3f)\n", eye_position_.x, eye_position_.y, eye_position_.z);
        printf("Look At Target: (%.3f, %.3f, %.3f)\n", look_at_target_.x, look_at_target_.y, look_at_target_.z);
        printf("Up Vector: (%.3f, %.3f, %.3f)\n", up_vector_.x, up_vector_.y, up_vector_.z);
        printf("FOV: %.1f degrees, Aspect: %.3f\n", fov_radians_ * 180.0f / kPI, aspect_ratio_);
        printf("Near: %.3f, Far: %.3f\n", near_plane_, far_plane_);
        printf("\n");

        // Extract and display camera basis vectors
        ozz::math::Float3 cam_right, cam_up, cam_forward;
        cam_right.x = ozz::math::GetX(view_matrix_.cols[0]);
        cam_right.y = ozz::math::GetY(view_matrix_.cols[0]);
        cam_right.z = ozz::math::GetZ(view_matrix_.cols[0]);
        cam_up.x = ozz::math::GetX(view_matrix_.cols[1]);
        cam_up.y = ozz::math::GetY(view_matrix_.cols[1]);
        cam_up.z = ozz::math::GetZ(view_matrix_.cols[1]);
        cam_forward.x = ozz::math::GetX(view_matrix_.cols[2]);
        cam_forward.y = ozz::math::GetY(view_matrix_.cols[2]);
        cam_forward.z = ozz::math::GetZ(view_matrix_.cols[2]);

        printf("Camera Basis Vectors:\n");
        printf("  Right:   (%.3f, %.3f, %.3f)\n", cam_right.x, cam_right.y, cam_right.z);
        printf("  Up:      (%.3f, %.3f, %.3f)\n", cam_up.x, cam_up.y, cam_up.z);
        printf("  Forward: (%.3f, %.3f, %.3f) [should point towards -Z in view space]\n", cam_forward.x, cam_forward.y, cam_forward.z);
        printf("\n");

        printf("View Matrix (row-major display, column-major storage):\n");
        for (int row = 0; row < 4; ++row) {
            printf("  [%7.3f, %7.3f, %7.3f, %7.3f]\n",
                ozz::math::GetX(view_matrix_.cols[row == 0 ? 0 : (row == 1 ? 1 : (row == 2 ? 2 : 3))]) * (row == 0 ? 1.0f : 0.0f) +
                ozz::math::GetY(view_matrix_.cols[row == 0 ? 0 : (row == 1 ? 1 : (row == 2 ? 2 : 3))]) * (row == 1 ? 1.0f : 0.0f) +
                ozz::math::GetZ(view_matrix_.cols[row == 0 ? 0 : (row == 1 ? 1 : (row == 2 ? 2 : 3))]) * (row == 2 ? 1.0f : 0.0f) +
                ozz::math::GetW(view_matrix_.cols[row == 0 ? 0 : (row == 1 ? 1 : (row == 2 ? 2 : 3))]) * (row == 3 ? 1.0f : 0.0f),
                ozz::math::GetX(view_matrix_.cols[0]),
                ozz::math::GetX(view_matrix_.cols[1]),
                ozz::math::GetX(view_matrix_.cols[2]),
                ozz::math::GetX(view_matrix_.cols[3]));
        }
        printf("\n");

        printf("Projection Matrix (row-major display):\n");
        for (int row = 0; row < 4; ++row) {
            float r0 = (row == 0 ? ozz::math::GetX(projection_matrix_.cols[0]) : (row == 1 ? ozz::math::GetY(projection_matrix_.cols[0]) : (row == 2 ? ozz::math::GetZ(projection_matrix_.cols[0]) : ozz::math::GetW(projection_matrix_.cols[0]))));
            float r1 = (row == 0 ? ozz::math::GetX(projection_matrix_.cols[1]) : (row == 1 ? ozz::math::GetY(projection_matrix_.cols[1]) : (row == 2 ? ozz::math::GetZ(projection_matrix_.cols[1]) : ozz::math::GetW(projection_matrix_.cols[1]))));
            float r2 = (row == 0 ? ozz::math::GetX(projection_matrix_.cols[2]) : (row == 1 ? ozz::math::GetY(projection_matrix_.cols[2]) : (row == 2 ? ozz::math::GetZ(projection_matrix_.cols[2]) : ozz::math::GetW(projection_matrix_.cols[2]))));
            float r3 = (row == 0 ? ozz::math::GetX(projection_matrix_.cols[3]) : (row == 1 ? ozz::math::GetY(projection_matrix_.cols[3]) : (row == 2 ? ozz::math::GetZ(projection_matrix_.cols[3]) : ozz::math::GetW(projection_matrix_.cols[3]))));
            printf("  [%7.3f, %7.3f, %7.3f, %7.3f]", r0, r1, r2, r3);
            if (row == 2) printf(" <- Depth mapping row");
            printf("\n");
        }
        printf("\n");

        // Test depth transformation with known values
        printf("=== DEPTH TRANSFORMATION TEST ===\n");
        printf("Testing view-space Z to NDC depth mapping:\n");

        auto test_depth = [&](float z_view) {
            // Projection matrix transforms: z_clip = M[2][2] * z_view + M[2][3]
            //                               w_clip = M[3][2] * z_view + M[3][3]
            float m22 = ozz::math::GetZ(projection_matrix_.cols[2]);
            float m23 = ozz::math::GetZ(projection_matrix_.cols[3]);
            float m32 = ozz::math::GetW(projection_matrix_.cols[2]);
            float m33 = ozz::math::GetW(projection_matrix_.cols[3]);

            float z_clip = m22 * z_view + m23;
            float w_clip = m32 * z_view + m33;
            float z_ndc = z_clip / w_clip;

            printf("  z_view=%7.3f -> z_clip=%7.3f, w_clip=%7.3f -> z_ndc=%7.3f",
                   z_view, z_clip, w_clip, z_ndc);

            // Expected values for Vulkan [0,1] depth
            if (std::abs(z_view + near_plane_) < 0.001f) {
                printf(" [NEAR PLANE, should be ~0.0]");
                if (std::abs(z_ndc) > 0.01f) printf(" *** ERROR: Expected 0.0!");
            } else if (std::abs(z_view + far_plane_) < 0.001f) {
                printf(" [FAR PLANE, should be ~1.0]");
                if (std::abs(z_ndc - 1.0f) > 0.01f) printf(" *** ERROR: Expected 1.0!");
            } else if (std::abs(z_view + (near_plane_ + far_plane_) / 2.0f) < 0.001f) {
                printf(" [MID PLANE]");
            }
            printf("\n");
        };

        // Test at near, mid, and far planes (negative Z in view space)
        test_depth(-near_plane_);
        test_depth(-(near_plane_ + far_plane_) / 2.0f);
        test_depth(-far_plane_);

        // Test at origin (should fail or give weird results)
        printf("  ** Testing at origin (z_view=0.0, invalid for perspective):\n");
        test_depth(0.0f);

        printf("\n");
        printf("=== COORDINATE SYSTEM CHECK ===\n");
        printf("Expected: Right-handed coordinate system\n");
        printf("  - Camera looks down -Z in view space\n");
        printf("  - Objects in front have NEGATIVE z_view\n");
        printf("  - After projection, depth maps to [0,1] (Vulkan)\n");
        printf("\n");

        // Test a point at origin being transformed
        printf("=== TEST: Transform world origin through pipeline ===\n");
        ozz::math::SimdFloat4 world_origin = ozz::math::simd_float4::Load(0.0f, 0.0f, 0.0f, 1.0f);
        ozz::math::SimdFloat4 view_pos = view_matrix_ * world_origin;
        ozz::math::SimdFloat4 clip_pos = projection_matrix_ * view_pos;

        float view_x = ozz::math::GetX(view_pos);
        float view_y = ozz::math::GetY(view_pos);
        float view_z = ozz::math::GetZ(view_pos);
        float view_w = ozz::math::GetW(view_pos);

        float clip_x = ozz::math::GetX(clip_pos);
        float clip_y = ozz::math::GetY(clip_pos);
        float clip_z = ozz::math::GetZ(clip_pos);
        float clip_w = ozz::math::GetW(clip_pos);

        float ndc_x = clip_x / clip_w;
        float ndc_y = clip_y / clip_w;
        float ndc_z = clip_z / clip_w;

        printf("World (0,0,0) -> View (%.3f, %.3f, %.3f, %.3f)\n",
               view_x, view_y, view_z, view_w);
        printf("            -> Clip (%.3f, %.3f, %.3f, %.3f)\n",
               clip_x, clip_y, clip_z, clip_w);
        printf("            -> NDC  (%.3f, %.3f, %.3f)\n",
               ndc_x, ndc_y, ndc_z);

        if (view_z > 0.0f) {
            printf("*** WARNING: Origin is at POSITIVE Z in view space (behind camera)!\n");
            printf("***          This means the camera is inside the scene.\n");
        } else {
            printf("OK: Origin is at negative Z in view space (in front of camera).\n");
        }

        if (ndc_z < 0.0f || ndc_z > 1.0f) {
            printf("*** WARNING: NDC depth %.3f is outside Vulkan range [0,1]!\n", ndc_z);
        } else {
            printf("OK: NDC depth %.3f is within Vulkan range [0,1].\n", ndc_z);
        }

        printf("====================================================================\n");
        printf("\n");
        printf("=== DEPTH PRECISION ANALYSIS ===\n");
        printf("Near/Far Ratio: 1:%.1f\n", far_plane_ / near_plane_);
        printf("Scene is at ~%.2f units from camera (NDC depth %.3f)\n",
               std::sqrt(eye_position_.x * eye_position_.x +
                        eye_position_.y * eye_position_.y +
                        eye_position_.z * eye_position_.z), ndc_z);
        printf("\n");
        printf("DEPTH PRECISION NOTE:\n");
        printf("  - With near/far ratio < 1:200, depth precision is good\n");
        printf("  - Scene objects should be between near and ~30%% of far plane\n");
        printf("  - If you see z-fighting, reduce far plane or increase near plane\n");
        printf("====================================================================\n");
        printf("\n");

        printed = true;
    }

    matrices_dirty_ = false;
}

} // namespace renderer
} // namespace animation
} // namespace xray
