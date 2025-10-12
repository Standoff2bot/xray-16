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
    , matrices_dirty_(true) {
    // Near/far ratio of 1:100 provides much better depth buffer precision
    // than 1:1000. With scene at ~3.5 units from camera, this gives adequate
    // range while maintaining precision.
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