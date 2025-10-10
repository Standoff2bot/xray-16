#include "stdafx.h"

#include <GLFW/glfw3.h>

#include "renderer/VulkanRenderer.h"

#include "../../../Externals/imgui/imgui.h"
#include "../../../Externals/ozz-animation/samples/framework/mesh.h"

#include "../ExtendedBoneMetadata.h"
#include "../OzzBundle.h"

#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/containers/vector.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/io/stream.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/span.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#define Msg(...) printf(__VA_ARGS__), printf("\n")

namespace {

using xray::animation::renderer::SkeletonLinePoint;
using xray::animation::renderer::VulkanRenderer;

constexpr double kStatusMessageDuration = 6.0;

std::string ParseBundleArgument(int argc, const char** argv) {
    const std::string_view prefix = "--bundle=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0) {
            return std::string(arg.substr(prefix.size()));
        }
        if (arg == "--bundle" && i + 1 < argc) {
            return argv[i + 1];
        }
    }
    return {};
}

bool LoadSkeletonFromBytes(const std::vector<std::uint8_t>& bytes, ozz::animation::Skeleton* skeleton) {
    if (!skeleton) {
        return false;
    }

    ozz::io::MemoryStream stream;
    if (!bytes.empty() && !stream.Write(bytes.data(), bytes.size())) {
        return false;
    }
    stream.Seek(0, ozz::io::Stream::kSet);

    ozz::io::IArchive archive(&stream);
    if (!archive.TestTag<ozz::animation::Skeleton>()) {
        return false;
    }
    archive >> *skeleton;
    return skeleton->num_joints() > 0;
}

bool LoadMeshesFromBytes(const std::vector<std::uint8_t>& bytes, ozz::vector<ozz::sample::Mesh>* meshes) {
    if (!meshes) {
        return false;
    }

    ozz::io::MemoryStream stream;
    if (!bytes.empty() && !stream.Write(bytes.data(), bytes.size())) {
        return false;
    }
    stream.Seek(0, ozz::io::Stream::kSet);

    ozz::io::IArchive archive(&stream);
    meshes->clear();
    while (archive.TestTag<ozz::sample::Mesh>()) {
        meshes->resize(meshes->size() + 1);
        archive >> meshes->back();
    }
    return true;
}

std::vector<SkeletonLinePoint> BuildSkeletonLinePoints(const ozz::animation::Skeleton& skeleton) {
    std::vector<SkeletonLinePoint> points;
    const int joint_count = skeleton.num_joints();
    if (joint_count <= 0) {
        return points;
    }

    ozz::vector<ozz::math::Float4x4> models(joint_count);
    ozz::animation::LocalToModelJob job;
    job.skeleton = &skeleton;
    job.input = skeleton.joint_rest_poses();
    job.output = ozz::make_span(models);
    if (!job.Run()) {
        return {};
    }

    const auto parents = skeleton.joint_parents();
    for (int joint = 0; joint < joint_count; ++joint) {
        const int parent = parents[joint];
        if (parent < 0) {
            continue;
        }

        SkeletonLinePoint parent_point{};
        SkeletonLinePoint child_point{};
        ozz::math::Store3PtrU(models[parent].cols[3], &parent_point.x);
        ozz::math::Store3PtrU(models[joint].cols[3], &child_point.x);
        points.push_back(parent_point);
        points.push_back(child_point);
    }

    return points;
}

struct ViewerState {
    GLFWwindow* window = nullptr;
    std::array<char, 512> bundle_path_buffer{};
    std::string loaded_bundle_path;
    bool bundle_loaded = false;

    ozz::animation::Skeleton skeleton;
    ozz::vector<ozz::sample::Mesh> meshes;
    XRay::Animation::ExtendedBoneMetadataCollection bone_metadata;

    int selected_mesh_index = 0;
    int pending_mesh_index = -1;
    bool request_load = false;
    bool request_reload = false;

    std::string status_message;
    bool status_is_error = false;
    double status_timestamp = 0.0;

    bool show_demo_window = false;
};

void QueueStatus(ViewerState& state, std::string message, bool is_error) {
    state.status_message = std::move(message);
    state.status_is_error = is_error;
    state.status_timestamp = glfwGetTime();
}

std::filesystem::path NormalizeBundlePath(const std::string& value) {
    std::filesystem::path path = value;
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (!ec) {
        path = absolute;
    }
    return path.lexically_normal();
}

bool LoadBundleFromPath(const std::filesystem::path& bundle_path, ViewerState& state, VulkanRenderer& renderer, std::string& error) {
    XRay::Animation::OzzxBundle bundle;
    if (!XRay::Animation::ReadOzzxBundle(bundle_path, bundle)) {
        error = "Failed to read bundle: " + bundle_path.string();
        return false;
    }

    if (!LoadSkeletonFromBytes(bundle.skeleton, &state.skeleton)) {
        error = "Failed to deserialize skeleton from bundle: " + bundle_path.string();
        return false;
    }

    const auto skeleton_lines = BuildSkeletonLinePoints(state.skeleton);
    if (skeleton_lines.empty()) {
        error = "Skeleton contains no line segments for visualization";
        return false;
    }

    if (!renderer.GetSkeletonRenderer().IsInitialized()) {
        error = "Skeleton renderer is not initialized";
        return false;
    }

    if (!renderer.GetSkeletonRenderer().SetSkeletonLines(ozz::make_span(skeleton_lines))) {
        error = "Failed to upload skeleton line data to GPU";
        return false;
    }

    std::array<ozz::math::Float4x4, 1> transforms = {ozz::math::Float4x4::identity()};
    if (!renderer.GetSkeletonRenderer().SetInstanceTransforms(ozz::make_span(transforms))) {
        error = "Failed to upload skeleton transforms";
        return false;
    }

    if (!renderer.SetSkeletonDebugData(state.skeleton, bundle.bone_metadata)) {
        error = "Failed to prepare skeleton debug metadata";
        return false;
    }

    state.meshes.clear();
    if (!bundle.mesh.empty() && !LoadMeshesFromBytes(bundle.mesh, &state.meshes)) {
        error = "Failed to deserialize skinned mesh data from bundle";
        return false;
    }

    state.bone_metadata = bundle.bone_metadata;
    state.loaded_bundle_path = bundle_path.string();
    state.bundle_loaded = true;

    std::snprintf(state.bundle_path_buffer.data(), state.bundle_path_buffer.size(), "%s", state.loaded_bundle_path.c_str());
    state.bundle_path_buffer.back() = '\0';

    if (!state.meshes.empty()) {
        state.selected_mesh_index = std::clamp(state.selected_mesh_index, 0, static_cast<int>(state.meshes.size() - 1));
        const auto& mesh = state.meshes[static_cast<size_t>(state.selected_mesh_index)];
        if (!renderer.LoadBundleMesh(mesh, state.skeleton)) {
            renderer.SetShowSkinnedMesh(false);
            renderer.SetShowTriangle(true);
            error = "Failed to upload mesh[" + std::to_string(state.selected_mesh_index) + "] to GPU";
            return false;
        }
        renderer.SetShowSkinnedMesh(true);
        renderer.SetShowTriangle(false);
        renderer.SetMeshAnimationTime(0.0f);
    } else {
        renderer.SetShowSkinnedMesh(false);
        renderer.SetShowTriangle(true);
    }

    state.pending_mesh_index = -1;
    return true;
}

void ProcessLoadRequests(ViewerState& state, VulkanRenderer& renderer) {
    if (!state.request_load && !state.request_reload) {
        return;
    }

    std::string path_string;
    if (state.request_reload) {
        path_string = state.loaded_bundle_path;
    } else {
        path_string = state.bundle_path_buffer.data();
    }

    state.request_load = false;
    state.request_reload = false;

    if (path_string.empty()) {
        QueueStatus(state, "Bundle path is empty", true);
        return;
    }

    const std::filesystem::path bundle_path = NormalizeBundlePath(path_string);

    std::string error;
    if (!LoadBundleFromPath(bundle_path, state, renderer, error)) {
        QueueStatus(state, error, true);
        Msg("! %s", error.c_str());
    } else {
        QueueStatus(state, "Loaded bundle: " + bundle_path.string(), false);
        Msg("* Loaded bundle: %s", bundle_path.string().c_str());
    }
}

void ProcessMeshUpload(ViewerState& state, VulkanRenderer& renderer) {
    if (state.pending_mesh_index < 0) {
        return;
    }

    const int mesh_index = state.pending_mesh_index;
    state.pending_mesh_index = -1;

    if (mesh_index < 0 || mesh_index >= static_cast<int>(state.meshes.size())) {
        return;
    }

    const auto& mesh = state.meshes[static_cast<size_t>(mesh_index)];
    if (!renderer.LoadBundleMesh(mesh, state.skeleton)) {
        QueueStatus(state, "Failed to upload mesh selection " + std::to_string(mesh_index), true);
        Msg("! Failed to upload mesh index %d", mesh_index);
        return;
    }

    renderer.SetShowSkinnedMesh(true);
    renderer.SetMeshAnimationTime(0.0f);
    QueueStatus(state, "Active mesh set to index " + std::to_string(mesh_index), false);
}

void BeginDockspaceIfAvailable() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    }
}

void DrawMenuBar(ViewerState& state, VulkanRenderer& renderer) {
    ImGuiIO& io = ImGui::GetIO();
    const bool ctrl_reload = io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false);

    if (ctrl_reload) {
        state.request_reload = true;
    }

    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Reload Bundle", "Ctrl+R", false, state.bundle_loaded)) {
            state.request_reload = true;
        }

        if (ImGui::MenuItem("Quit")) {
            if (state.window) {
                glfwSetWindowShouldClose(state.window, GLFW_TRUE);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Dear ImGui Demo", nullptr, &state.show_demo_window);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Render")) {
        bool show_triangle = renderer.GetShowTriangle();
        if (ImGui::MenuItem("Show Debug Triangle", nullptr, &show_triangle)) {
            renderer.SetShowTriangle(show_triangle);
        }

        bool show_skeleton = renderer.GetShowSkeletonLines();
        if (ImGui::MenuItem("Show Skeleton Lines", nullptr, &show_skeleton, renderer.HasSkeletonLoaded())) {
            renderer.SetShowSkeletonLines(show_skeleton);
        }

        bool show_mesh = renderer.GetShowSkinnedMesh();
        if (ImGui::MenuItem("Show Skinned Mesh", nullptr, &show_mesh, renderer.HasMeshLoaded())) {
            renderer.SetShowSkinnedMesh(show_mesh);
        }

        bool show_debug = renderer.GetShowDebugOverlay();
        if (ImGui::MenuItem("Show Debug Overlay", nullptr, &show_debug, renderer.HasSkeletonLoaded())) {
            renderer.SetShowDebugOverlay(show_debug);
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void DrawBundleInspector(ViewerState& state, VulkanRenderer& renderer, double now_seconds) {
    if (!ImGui::Begin("Bundle Inspector")) {
        ImGui::End();
        return;
    }

    ImGui::InputText("Bundle Path", state.bundle_path_buffer.data(), state.bundle_path_buffer.size());

    if (ImGui::Button("Load Bundle")) {
        state.request_load = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload") && state.bundle_loaded) {
        state.request_reload = true;
    }

    if (!state.bundle_loaded) {
        ImGui::Separator();
        ImGui::TextDisabled("No bundle loaded.");
    } else {
        ImGui::Separator();
        ImGui::Text("Active bundle:");
        ImGui::TextWrapped("%s", state.loaded_bundle_path.c_str());

        const int joint_count = state.skeleton.num_joints();
        ImGui::Text("Skeleton joints: %d", joint_count);
        ImGui::Text("Mesh count: %zu", state.meshes.size());
        ImGui::Text("Extended bone metadata entries: %zu", state.bone_metadata.size());

        if (joint_count > 0 && ImGui::CollapsingHeader("Joint Names", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("##JointNames", ImVec2(0.f, 180.f), true, ImGuiWindowFlags_HorizontalScrollbar);
            const auto names = state.skeleton.joint_names();
            for (int i = 0; i < joint_count; ++i) {
                ImGui::Text("[%03d] %s", i, names[i]);
            }
            ImGui::EndChild();
        }

        if (!state.meshes.empty() && ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("MeshTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 60.f);
                ImGui::TableSetupColumn("Vertices");
                ImGui::TableSetupColumn("Triangles");
                ImGui::TableSetupColumn("Bones");
                ImGui::TableSetupColumn("Original Faces");
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < state.meshes.size(); ++i) {
                    const auto& mesh = state.meshes[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", mesh.vertex_count());
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", mesh.triangle_indices.size() / 3);
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", mesh.joint_remaps.size());
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", mesh.xray_metadata.original_face_count);
                }

                ImGui::EndTable();
            }

            int mesh_index = state.selected_mesh_index;
            if (ImGui::SliderInt("Active Mesh", &mesh_index, 0, static_cast<int>(state.meshes.size() - 1))) {
                state.selected_mesh_index = mesh_index;
                state.pending_mesh_index = mesh_index;
            }
        }

        if (!state.bone_metadata.empty() && ImGui::CollapsingHeader("Bone Metadata")) {
            ImGui::BeginChild("##BoneMetadata", ImVec2(0.f, 160.f), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (size_t bone = 0; bone < state.bone_metadata.size(); ++bone) {
                const auto& metadata = state.bone_metadata[bone];
                ImGui::Text("[%03zu] rest=%.3f mass=%.3f ground=%s weapon=%s",
                    bone,
                    metadata.rest_length,
                    metadata.mass,
                    metadata.ground_contact_candidate ? "yes" : "no",
                    metadata.weapon_anchor_candidate ? "yes" : "no");
            }
            ImGui::EndChild();
        }
    }

    if (!state.status_message.empty()) {
        const double elapsed = now_seconds - state.status_timestamp;
        if (elapsed < kStatusMessageDuration) {
            const ImVec4 color = state.status_is_error ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f) : ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
            ImGui::Spacing();
            ImGui::TextColored(color, "%s", state.status_message.c_str());
        } else {
            state.status_message.clear();
        }
    }

    ImGui::End();
}

void DrawRendererSettings(VulkanRenderer& renderer) {
    if (!ImGui::Begin("Renderer Settings")) {
        ImGui::End();
        return;
    }

    bool show_triangle = renderer.GetShowTriangle();
    if (ImGui::Checkbox("Show Debug Triangle", &show_triangle)) {
        renderer.SetShowTriangle(show_triangle);
    }

    bool show_skeleton = renderer.GetShowSkeletonLines();
    if (ImGui::Checkbox("Show Skeleton Lines", &show_skeleton)) {
        renderer.SetShowSkeletonLines(show_skeleton);
    }

    bool show_mesh = renderer.GetShowSkinnedMesh();
    if (ImGui::Checkbox("Show Skinned Mesh", &show_mesh)) {
        renderer.SetShowSkinnedMesh(show_mesh);
    }

    bool show_debug = renderer.GetShowDebugOverlay();
    if (ImGui::Checkbox("Show Debug Overlay", &show_debug)) {
        renderer.SetShowDebugOverlay(show_debug);
    }

    ImGui::Separator();

    float clear_r = 0.f;
    float clear_g = 0.f;
    float clear_b = 0.f;
    renderer.GetClearColor(clear_r, clear_g, clear_b);
    float color[3] = {clear_r, clear_g, clear_b};
    if (ImGui::ColorEdit3("Clear Color", color, ImGuiColorEditFlags_NoInputs)) {
        renderer.SetClearColor(color[0], color[1], color[2]);
    }

    ImGui::Separator();

    bool animate_mesh = renderer.GetAnimateMesh();
    if (ImGui::Checkbox("Animate Mesh Rotation", &animate_mesh)) {
        renderer.SetAnimateMesh(animate_mesh);
    }

    float rotation_speed = renderer.GetMeshRotationSpeed();
    if (ImGui::SliderFloat("Rotation Speed (rad/s)", &rotation_speed, -4.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        renderer.SetMeshRotationSpeed(rotation_speed);
    }

    if (!renderer.GetAnimateMesh()) {
        float rotation_time = renderer.GetMeshAnimationTime();
        if (ImGui::SliderFloat("Rotation Time", &rotation_time, 0.0f, 60.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
            renderer.SetMeshAnimationTime(rotation_time);
        }
    }

    ImGui::Separator();
    ImGui::Text("Frame %.2f ms (%.1f FPS)", renderer.GetFrameDeltaMilliseconds(), renderer.GetFrameRate());

    ImGui::End();
}

} // namespace

int main(int argc, const char** argv) {
    Msg("* Starting Vulkan viewer...");

    if (!glfwInit()) {
        Msg("! Failed to initialize GLFW");
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1600, 900, "ozz Animation Viewer (Vulkan)", nullptr, nullptr);
    if (!window) {
        Msg("! Failed to create GLFW window");
        glfwTerminate();
        return -1;
    }

    VulkanRenderer renderer;
    if (!renderer.Initialize(window)) {
        Msg("! Failed to initialize Vulkan renderer");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    ViewerState state;
    state.window = window;

    const std::string bundle_argument = ParseBundleArgument(argc, argv);
    if (!bundle_argument.empty()) {
        const std::filesystem::path bundle_path = NormalizeBundlePath(bundle_argument);
        std::string error;
        if (!LoadBundleFromPath(bundle_path, state, renderer, error)) {
            QueueStatus(state, error, true);
            Msg("! %s", error.c_str());
        } else {
            QueueStatus(state, "Loaded bundle: " + bundle_path.string(), false);
            Msg("* Loaded bundle from CLI: %s", bundle_path.string().c_str());
        }
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ProcessLoadRequests(state, renderer);
        ProcessMeshUpload(state, renderer);

        renderer.BeginFrame();

        if (renderer.IsImGuiInitialized()) {
            ImGui::SetCurrentContext(renderer.GetImGuiContext());
            BeginDockspaceIfAvailable();
            const double now_seconds = glfwGetTime();
            DrawMenuBar(state, renderer);
            DrawBundleInspector(state, renderer, now_seconds);
            DrawRendererSettings(renderer);
            if (state.show_demo_window) {
                ImGui::ShowDemoWindow(&state.show_demo_window);
            }
        }

        renderer.RenderScene();
        renderer.EndFrame();
    }

    Msg("* Shutting down viewer...");
    renderer.Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
