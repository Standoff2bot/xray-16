//----------------------------------------------------------------------------//
//                                                                            //
// ozz-animation is hosted at http://github.com/guillaumeblanc/ozz-animation  //
// and distributed under the MIT License (MIT).                               //
//                                                                            //
// Copyright (c) Guillaume Blanc                                              //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included in //
// all copies or substantial portions of the Software.                        //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//----------------------------------------------------------------------------//
#include "stdafx.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "../Externals/ozz-animation/samples/framework/application.h"
#include "../Externals/ozz-animation/samples/framework/mesh.h"
#include "../Externals/ozz-animation/samples/framework/renderer.h"
#define OZZ_INCLUDE_PRIVATE_HEADER
#include "../Externals/ozz-animation/samples/framework/internal/camera.h"
#include "../Externals/ozz-animation/samples/framework/internal/renderer_impl.h"
#undef OZZ_INCLUDE_PRIVATE_HEADER
#include "../Externals/ozz-animation/samples/framework/utils.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/ik_two_bone_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/io/stream.h"
#include "ozz/base/log.h"
#include "ozz/base/maths/box.h"
#include "ozz/base/maths/quaternion.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/simd_quaternion.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/maths/vec_float.h"
#include "ozz/options/options.h"
#if defined(__APPLE__)
#    include <OpenGL/gl3.h>
#else
#    include <GL/gl.h>
#endif
#include "../../../Externals/imgui/backends/imgui_impl_glfw.h"
#include "../../../Externals/imgui/backends/imgui_impl_opengl3.h"
#include "../../../Externals/imgui/imgui.h"
#include "../../../Externals/imgui/imgui_internal.h"
#include "ozz/base/span.h"

#include "../OzzBundle.h"

// Skeleton archive can be specified as an option.
OZZ_OPTIONS_DECLARE_STRING(bundle, "Path to a combined skeleton/mesh bundle (.ozzx).", "", false)
OZZ_OPTIONS_DECLARE_STRING(skeleton, "Path to the skeleton (ozz archive format).", "media/skeleton.ozz", false)

// Animation archive can be specified as an option.
OZZ_OPTIONS_DECLARE_STRING(animation, "Path to the animation (ozz archive format).", "media/animation.ozz", false)

// Mesh archive can be specified as an option.
OZZ_OPTIONS_DECLARE_STRING(mesh, "Path to the skinned mesh (ozz archive format).", "", false)

// Optional JSON export for sampled animation data.
OZZ_OPTIONS_DECLARE_STRING(dump_animation_json, "Path to write sampled animation data as JSON.", "", false)

OZZ_OPTIONS_DECLARE_STRING(texture_root, "Optional root directory to resolve texture metadata paths.", "", false)

OZZ_OPTIONS_DECLARE_BOOL(random_triangle_colors, "Randomize per-triangle debug colors when displaying meshes.", true, false)

namespace
{
namespace fs = std::filesystem;

constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
    return (static_cast<uint32_t>(a)) | (static_cast<uint32_t>(b) << 8) | (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24);
}

constexpr float kIkHandleDrawRadius = 0.03f;
constexpr float kIkAxisScale = 0.1f;
constexpr float kIkHandleHitRadius = 0.045f;

struct DdsPixelFormat
{
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};

struct DdsHeader
{
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DdsPixelFormat ddspf;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};

static_assert(sizeof(DdsPixelFormat) == 32, "Unexpected DDS pixel format header size.");
static_assert(sizeof(DdsHeader) == 124, "Unexpected DDS header size.");

std::vector<float> CollectRecordSamples(const ozz::sample::Record* record)
{
    std::vector<float> samples;
    if (!record)
    {
        return samples;
    }

    const float* begin = record->record_begin();
    const float* end = record->record_end();
    const float* cursor = record->cursor();
    if (!begin || !end || begin == end || !cursor || cursor == end)
    {
        return samples;
    }

    const float* current = cursor;
    const float* segment_end = end;
    while (current < segment_end)
    {
        samples.push_back(*current);
        ++current;
        if (current == end)
        {
            segment_end = cursor;
            current = begin;
        }
    }

    std::reverse(samples.begin(), samples.end());
    return samples;
}

struct IkOffsetUi
{
    float forward = 0.f;
    float lateral = 0.f;
    float vertical = 0.f;
};

inline IkOffsetUi ToUiOffset(const ozz::math::Float3& model_offset)
{
    return { model_offset.z, model_offset.x, model_offset.y };
}

inline ozz::math::Float3 FromUiOffset(const IkOffsetUi& ui_offset)
{
    return { ui_offset.lateral, ui_offset.vertical, ui_offset.forward };
}

inline ozz::math::Float3 ExtractTranslation(const ozz::math::Float4x4& matrix)
{
    return { ozz::math::GetX(matrix.cols[3]), ozz::math::GetY(matrix.cols[3]), ozz::math::GetZ(matrix.cols[3]) };
}

inline ozz::math::Float3 Add3(const ozz::math::Float3& a, const ozz::math::Float3& b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline ozz::math::Float3 Sub3(const ozz::math::Float3& a, const ozz::math::Float3& b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline ozz::math::Float3 Scale3(const ozz::math::Float3& v, float scale)
{
    return { v.x * scale, v.y * scale, v.z * scale };
}

inline float Dot3(const ozz::math::Float3& a, const ozz::math::Float3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float LengthSq3(const ozz::math::Float3& v)
{
    return Dot3(v, v);
}

inline ozz::math::Float3 Normalize3(const ozz::math::Float3& v)
{
    const float len_sq = LengthSq3(v);
    if (len_sq <= std::numeric_limits<float>::epsilon())
    {
        return { 0.f, 0.f, 0.f };
    }
    const float inv_len = 1.f / std::sqrt(len_sq);
    return Scale3(v, inv_len);
}

inline bool IntersectRaySphere(const ozz::math::Float3& origin, const ozz::math::Float3& dir, const ozz::math::Float3& center, float radius, float& t_hit)
{
    const ozz::math::Float3 m = Sub3(origin, center);
    const float b = Dot3(m, dir);
    const float c = Dot3(m, m) - radius * radius;
    if (c > 0.f && b > 0.f)
    {
        return false;
    }

    const float discriminant = b * b - c;
    if (discriminant < 0.f)
    {
        return false;
    }

    float t = -b - std::sqrt(discriminant);
    if (t < 0.f)
    {
        t = 0.f;
    }

    t_hit = t;
    return true;
}

inline bool IntersectRayPlane(const ozz::math::Float3& origin, const ozz::math::Float3& dir, const ozz::math::Float3& plane_point, const ozz::math::Float3& plane_normal, ozz::math::Float3& hit)
{
    const float denom = Dot3(dir, plane_normal);
    const float epsilon = 1e-6f;
    if (std::fabs(denom) <= epsilon)
    {
        return false;
    }

    const ozz::math::Float3 diff = Sub3(plane_point, origin);
    const float t = Dot3(diff, plane_normal) / denom;
    if (t < 0.f)
    {
        return false;
    }

    hit = Add3(origin, Scale3(dir, t));
    return true;
}

GLuint LoadDdsTexture(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        ozz::log::Err() << "Failed to open DDS texture: " << path << std::endl;
        return 0;
    }

    uint32_t magic = 0;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!stream || magic != MakeFourCC('D', 'D', 'S', ' '))
    {
        ozz::log::Err() << "Invalid DDS magic for texture: " << path << std::endl;
        return 0;
    }

    DdsHeader header{};
    stream.read(reinterpret_cast<char*>(&header), sizeof(DdsHeader));
    if (!stream || header.size != sizeof(DdsHeader))
    {
        ozz::log::Err() << "Invalid DDS header for texture: " << path << std::endl;
        return 0;
    }

    const uint32_t fourcc = header.ddspf.fourCC;
    GLenum gl_format = 0;
    uint32_t block_size = 0;
    if (fourcc == MakeFourCC('D', 'X', 'T', '1'))
    {
        gl_format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        block_size = 8;
    }
    else if (fourcc == MakeFourCC('D', 'X', 'T', '3'))
    {
        gl_format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        block_size = 16;
    }
    else if (fourcc == MakeFourCC('D', 'X', 'T', '5'))
    {
        gl_format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        block_size = 16;
    }
    else
    {
        ozz::log::Err() << "Unsupported DDS FourCC in texture: " << path << std::endl;
        return 0;
    }

    const uint32_t mip_count = header.mipMapCount > 0 ? header.mipMapCount : 1;
    const size_t data_offset = static_cast<size_t>(stream.tellg());
    stream.seekg(0, std::ios::end);
    const size_t file_size = static_cast<size_t>(stream.tellg());
    const size_t data_size = file_size - data_offset;
    stream.seekg(data_offset, std::ios::beg);

    std::vector<uint8_t> data(data_size);
    if (!stream.read(reinterpret_cast<char*>(data.data()), data_size))
    {
        ozz::log::Err() << "Failed to read DDS texture data: " << path << std::endl;
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip_count > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    uint32_t width = header.width;
    uint32_t height = header.height;
    const uint8_t* bytes = data.data();

    for (uint32_t level = 0; level < mip_count; ++level)
    {
        const uint32_t ww = std::max(1u, width);
        const uint32_t hh = std::max(1u, height);
        const size_t size = std::max<size_t>(1, ((ww + 3) / 4) * ((hh + 3) / 4) * block_size);

        glCompressedTexImage2D(GL_TEXTURE_2D, level, gl_format, ww, hh, 0, static_cast<GLsizei>(size), bytes);

        bytes += size;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

GLuint LoadTextureFromFile(const fs::path& path)
{
    const std::string extension = path.extension().string();
    std::string lowercase;
    lowercase.resize(extension.size());
    std::transform(extension.begin(), extension.end(), lowercase.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    if (lowercase == ".dds")
    {
        return LoadDdsTexture(path);
    }

    ozz::log::LogV() << "Unsupported texture format for path: " << path << std::endl;
    return 0;
}

fs::path SanitizeTexturePath(const std::string& raw)
{
    std::string sanitized = raw;
    for (char& ch : sanitized)
    {
        if (ch == '\\')
        {
            ch = fs::path::preferred_separator;
        }
    }
    return fs::path(sanitized);
}

std::vector<fs::path> BuildTextureCandidates(const fs::path& texture, const fs::path& mesh_dir)
{
    std::vector<fs::path> candidates;
    auto add_candidate = [&candidates](const fs::path& candidate)
    {
        if (candidate.empty())
        {
            return;
        }
        for (const fs::path& existing : candidates)
        {
            if (existing == candidate)
            {
                return;
            }
        }
        candidates.push_back(candidate);
    };

    const bool has_extension = texture.has_extension();

    if (texture.is_absolute())
    {
        add_candidate(texture);
    }
    else
    {
        add_candidate(texture);
        add_candidate(mesh_dir / texture);
        add_candidate(fs::current_path() / texture);
    }

    if (!has_extension)
    {
        const fs::path with_dds = texture.string() + ".dds";
        if (texture.is_absolute())
        {
            add_candidate(with_dds);
        }
        else
        {
            add_candidate(with_dds);
            add_candidate(mesh_dir / with_dds);
            add_candidate(fs::current_path() / with_dds);
        }
    }

    return candidates;
}

std::string EscapeJsonString(const char* input)
{
    std::string escaped;
    if (!input)
    {
        return escaped;
    }
    escaped.reserve(std::strlen(input));
    for (const unsigned char ch : std::string_view(input))
    {
        switch (ch)
        {
        case '"':  escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (ch < 0x20)
            {
                std::ostringstream oss;
                oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
                escaped += oss.str();
            }
            else
            {
                escaped += static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped;
}

std::string FormatFloat(float value, int precision = 6)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

struct ProgressiveMetadataAnalysis
{
    bool has_data = false;
    bool header_valid = false;
    uint32_t collapse_count = 0;
    uint32_t window_count = 0;
    std::size_t max_required_indices = 0;
    bool has_zero_offset = false;
    uint32_t first_offset = 0;
    uint16_t first_tris = 0;
    uint32_t last_offset = 0;
    uint16_t last_tris = 0;
};

ProgressiveMetadataAnalysis AnalyzeProgressiveMetadata(const std::vector<std::uint8_t>& data)
{
    ProgressiveMetadataAnalysis result;
    if (data.empty())
        return result;

    result.has_data = true;
    if (data.size() < sizeof(uint32_t) * 5u)
        return result;

    const uint8_t* cursor = data.data();
    const uint8_t* const end = cursor + data.size();

    auto read_u32 = [&](uint32_t& value) -> bool
    {
        if (cursor + sizeof(uint32_t) > end)
            return false;
        std::memcpy(&value, cursor, sizeof(uint32_t));
        cursor += sizeof(uint32_t);
        return true;
    };

    uint32_t header_values[5] = {};
    for (uint32_t i = 0; i < 5; ++i)
    {
        if (!read_u32(header_values[i]))
            return result;
    }

    result.header_valid = true;
    result.collapse_count = header_values[4];
    result.window_count = header_values[4];

    for (uint32_t window_index = 0; window_index < result.window_count; ++window_index)
    {
        if (cursor + sizeof(uint32_t) + sizeof(uint16_t) * 2 > end)
        {
            result.header_valid = false;
            break;
        }

        uint32_t offset = 0;
        std::memcpy(&offset, cursor, sizeof(uint32_t));
        cursor += sizeof(uint32_t);

        uint16_t num_tris = 0;
        std::memcpy(&num_tris, cursor, sizeof(uint16_t));
        cursor += sizeof(uint16_t);

        uint16_t num_verts = 0;
        std::memcpy(&num_verts, cursor, sizeof(uint16_t));
        cursor += sizeof(uint16_t);
        (void)num_verts;

        if (window_index == 0)
        {
            result.first_offset = offset;
            result.first_tris = num_tris;
        }
        result.last_offset = offset;
        result.last_tris = num_tris;

        if (offset == 0)
            result.has_zero_offset = true;

        const std::size_t required = static_cast<std::size_t>(offset) + static_cast<std::size_t>(num_tris) * 3u;
        if (required > result.max_required_indices)
            result.max_required_indices = required;
    }

    return result;
}

const char* DescribeTranslationFormat(uint8_t format)
{
    switch (format)
    {
    case 1: return "Quantized8";
    case 2: return "Quantized16";
    case 0: return "None";
    default: return "Unknown";
    }
}

std::string GetFileStem(const char* path)
{
    if (path == nullptr)
    {
        return std::string();
    }
    std::string value(path);
    const size_t separator = value.find_last_of("/\\");
    if (separator != std::string::npos)
    {
        value.erase(0, separator + 1);
    }
    const size_t dot = value.find_last_of('.');
    if (dot != std::string::npos)
    {
        value.erase(dot);
    }
    return value;
}

bool LoadSkeletonFromBytes(const std::vector<std::uint8_t>& bytes, ozz::animation::Skeleton* skeleton)
{
    if (!skeleton)
        return false;

    ozz::io::MemoryStream stream;
    if (!bytes.empty() && !stream.Write(bytes.data(), bytes.size()))
        return false;
    stream.Seek(0, ozz::io::Stream::kSet);

    ozz::io::IArchive archive(&stream);
    if (!archive.TestTag<ozz::animation::Skeleton>())
        return false;
    archive >> *skeleton;
    return skeleton->num_joints() > 0;
}

bool LoadMeshesFromBytes(const std::vector<std::uint8_t>& bytes, ozz::vector<ozz::sample::Mesh>* meshes)
{
    if (!meshes)
        return false;

    ozz::io::MemoryStream stream;
    if (!bytes.empty() && !stream.Write(bytes.data(), bytes.size()))
        return false;
    stream.Seek(0, ozz::io::Stream::kSet);

    ozz::io::IArchive archive(&stream);
    meshes->clear();
    while (archive.TestTag<ozz::sample::Mesh>())
    {
        meshes->resize(meshes->size() + 1);
        archive >> meshes->back();
    }
    return true;
}

std::string BuildMeshDisplayName(const std::string& base_label, size_t mesh_index, size_t mesh_count)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    if (!base_label.empty())
    {
        oss << base_label;
        if (mesh_count > 1)
        {
            oss << '_' << mesh_index;
        }
        return oss.str();
    }

    oss << "mesh_" << mesh_index;
    return oss.str();
}
} // namespace

struct MotionMarkData
{
    std::string name;
    std::vector<std::pair<float, float>> intervals;
};

struct BoneMotionData
{
    uint16_t bone_id = 0;
    uint8_t flags = 0;
    uint8_t translation_format = 0;
    uint32_t rotation_crc = 0;
    uint32_t translation_crc = 0;
    uint32_t rotation_key_count = 0;
    uint32_t translation_key_count = 0;
    std::array<float, 3> translation_size{ { 0.f, 0.f, 0.f } };
    std::array<float, 3> translation_init{ { 0.f, 0.f, 0.f } };
};

struct MotionMetadataData
{
    std::string name;
    uint32_t flags = 0;
    uint16_t bone_or_part = 0;
    uint16_t motion_id = 0;
    float speed = 0.f;
    float power = 0.f;
    float accrue = 0.f;
    float falloff = 0.f;
    std::vector<MotionMarkData> marks;
    uint32_t frame_count = 0;
    std::vector<BoneMotionData> bone_motions;
};

class DearImGuiLayer
{
public:
    DearImGuiLayer() = default;

    ~DearImGuiLayer()
    {
        Shutdown();
    }

    bool Initialize(GLFWwindow* window)
    {
        if (!window)
        {
            return false;
        }
        if (initialized_)
        {
            window_ = window;
            return true;
        }

        IMGUI_CHECKVERSION();
        context_ = ImGui::CreateContext();
        if (!context_)
        {
            return false;
        }

        ImGui::StyleColorsDark();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
        {
            ImGui::DestroyContext(context_);
            context_ = nullptr;
            return false;
        }

        if (!ImGui_ImplOpenGL3_Init(kGlslVersion))
        {
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext(context_);
            context_ = nullptr;
            return false;
        }

        window_ = window;
        initialized_ = true;
        return true;
    }

    void BeginFrame()
    {
        if (!initialized_)
        {
            return;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void Render()
    {
        if (!initialized_)
        {
            return;
        }
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void Shutdown()
    {
        if (!initialized_)
        {
            return;
        }
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        if (context_)
        {
            ImGui::DestroyContext(context_);
            context_ = nullptr;
        }
        initialized_ = false;
        window_ = nullptr;
    }

private:
    static constexpr const char* kGlslVersion = "#version 150";

    GLFWwindow* window_ = nullptr;
    ImGuiContext* context_ = nullptr;
    bool initialized_ = false;
};

class PlaybackSampleApplication : public ozz::sample::Application
{
public:
    ~PlaybackSampleApplication() override = default;

protected:
    static constexpr int kNoAnimationIndex = -1;

    struct ViewerUiState
    {
        int current_animation = kNoAnimationIndex;
        bool draw_skeleton = true;
        bool draw_mesh = false;
        ozz::sample::Renderer::Options renderer_options;
        bool random_triangle_colors = true;
        bool show_bone_debug = true;
        int bone_display_limit = 16;
        bool foot_ik_enabled = false;
        float foot_ik_ground_height = 0.f;
        float foot_ik_weight = 1.f;
        float foot_ik_soften = 0.97f;
        float foot_ik_twist_angle_deg = 0.f;
        bool arm_ik_enabled = false;
        float arm_ik_weight = 1.f;
        float arm_ik_soften = 0.85f;
        float arm_ik_twist_angle_deg = 0.f;
        struct ArmHandle
        {
            float forward_offset = 0.f;
            float lateral_offset = 0.f;
            float vertical_offset = 0.f;
        };
        ArmHandle arm_handles[2];
        bool ragdoll_enabled = false;
        float ragdoll_stiffness = 8.f;
        float ragdoll_damping = 0.35f;
        float ragdoll_gravity = -9.8f;
        bool crouch_enabled = false;
        float crouch_amount = 0.f;
        float crouch_height = 0.4f;
        float crouch_spine_pitch_deg = 15.f;
        bool crouch_affects_arms = true;
        std::vector<uint8_t> mesh_visibility;
    };

    bool HasAnimationSelected() const
    {
        return ui_state_.current_animation >= 0 && ui_state_.current_animation < static_cast<int>(animations_.size());
    }

    std::string GetAnimationName(int index) const
    {
        if (index < 0 || index >= static_cast<int>(animations_.size()))
        {
            return std::string();
        }
        if (index < static_cast<int>(animation_metadata_.size()))
        {
            const std::string& meta_name = animation_metadata_[index].name;
            if (!meta_name.empty())
            {
                return meta_name;
            }
        }
        const char* runtime_name = animations_[index].name();
        if (runtime_name && runtime_name[0] != '\0')
        {
            return runtime_name;
        }
        return std::string();
    }

    std::string ReadString(ozz::io::IArchive& archive)
    {
        uint32_t length = 0;
        archive >> length;
        std::string value;
        value.resize(length);
        if (length > 0)
        {
            archive >> ozz::io::MakeArray(value.data(), length);
        }
        return value;
    }

    // Read serialized metadata chunk that follows each animation in converter output.
    MotionMetadataData ReadSerializedMetadata(ozz::io::IArchive& archive)
    {
        MotionMetadataData metadata;
        metadata.name = ReadString(archive);
        archive >> metadata.flags;
        archive >> metadata.bone_or_part;
        archive >> metadata.motion_id;
        archive >> metadata.speed;
        archive >> metadata.power;
        archive >> metadata.accrue;
        archive >> metadata.falloff;

        uint32_t mark_count = 0;
        archive >> mark_count;
        metadata.marks.resize(mark_count);
        for (uint32_t mark_index = 0; mark_index < mark_count; ++mark_index)
        {
            MotionMarkData& mark = metadata.marks[mark_index];
            mark.name = ReadString(archive);

            uint32_t interval_count = 0;
            archive >> interval_count;
            mark.intervals.resize(interval_count);
            for (uint32_t interval_index = 0; interval_index < interval_count; ++interval_index)
            {
                float start = 0.f;
                float end = 0.f;
                archive >> start;
                archive >> end;
                mark.intervals[interval_index] = { start, end };
            }
        }

        uint32_t frame_count = 0;
        archive >> frame_count;
        metadata.frame_count = frame_count;

        uint32_t bone_motion_count = 0;
        archive >> bone_motion_count;
        metadata.bone_motions.resize(bone_motion_count);

        for (uint32_t bone_index = 0; bone_index < bone_motion_count; ++bone_index)
        {
            ozz::log::LogV() << "[metadata] motion=" << metadata.name.c_str() << " index=" << bone_index << std::endl;
            BoneMotionData bone;
            auto* stream = archive.stream();

            auto read_value = [stream](auto& value) -> bool
            {
                return stream->Read(&value, sizeof(value)) == sizeof(value);
            };

            if (!read_value(bone.bone_id) || !read_value(bone.flags) ||
                !read_value(bone.translation_format) || !read_value(bone.rotation_crc) ||
                !read_value(bone.translation_crc))
            {
                ozz::log::Err() << "Failed to read bone metadata header for index " << bone_index << std::endl;
                break;
            }

            if (!read_value(bone.rotation_key_count))
            {
                ozz::log::Err() << "Failed to read rotation key count for bone index " << bone_index << std::endl;
                break;
            }

            for (uint32_t key_index = 0; key_index < bone.rotation_key_count; ++key_index)
            {
                int16_t components[4] = {};
                if (stream->Read(components, sizeof(components)) != sizeof(components))
                {
                    ozz::log::Err() << "Failed to read rotation key data for bone index " << bone_index << std::endl;
                    break;
                }
            }

            if (!read_value(bone.translation_key_count))
            {
                ozz::log::Err() << "Failed to read translation key count for bone index " << bone_index << std::endl;
                break;
            }

            switch (bone.translation_format)
            {
            case 1:
            {
                const size_t bytes = static_cast<size_t>(bone.translation_key_count) * 3;
                std::vector<uint8_t> buffer(bytes);
                if (stream->Read(buffer.data(), bytes) != bytes)
                {
                    ozz::log::Err() << "Failed to read quantized8 translation data for bone index " << bone_index << std::endl;
                    break;
                }
                break;
            }
            case 2:
            {
                const size_t bytes = static_cast<size_t>(bone.translation_key_count) * 3 * sizeof(int16_t);
                std::vector<int16_t> buffer(bytes / sizeof(int16_t));
                if (stream->Read(buffer.data(), bytes) != bytes)
                {
                    ozz::log::Err() << "Failed to read quantized16 translation data for bone index " << bone_index << std::endl;
                    break;
                }
                break;
            }
            default:
            {
                const size_t bytes = static_cast<size_t>(bone.translation_key_count) * 3 * sizeof(float);
                std::vector<float> buffer(bytes / sizeof(float));
                if (stream->Read(buffer.data(), bytes) != bytes)
                {
                    ozz::log::Err() << "Failed to read uncompressed translation data for bone index " << bone_index << std::endl;
                    break;
                }
                break;
            }
            }

            if (!read_value(bone.translation_size[0]) || !read_value(bone.translation_size[1]) ||
                !read_value(bone.translation_size[2]) || !read_value(bone.translation_init[0]) ||
                !read_value(bone.translation_init[1]) || !read_value(bone.translation_init[2]))
            {
                ozz::log::Err() << "Failed to read translation bounds for bone index " << bone_index << std::endl;
                break;
            }

            metadata.bone_motions[bone_index] = std::move(bone);
        }

        return metadata;
    }

    bool LoadAnimationsFromStream(ozz::io::Stream* stream, const char* label)
    {
        if (!stream)
            return false;

        const char* source_label = (label && label[0] != '\0') ? label : "<memory>";

        if (stream->Seek(0, ozz::io::Stream::kSet) < 0)
        {
            ozz::log::Err() << "Failed to seek animation stream: " << source_label << std::endl;
            return false;
        }

        ozz::io::IArchive archive(stream);

        animation_metadata_.clear();
        animations_.clear();

        if (archive.TestTag<ozz::animation::Animation>())
        {
            animations_.resize(1);
            archive >> animations_[0];

            MotionMetadataData metadata;
            const char* name = animations_[0].name();
            metadata.name = name ? name : "";
            animation_metadata_.push_back(std::move(metadata));

            ozz::log::LogV() << "Loaded 1 animation from " << source_label << std::endl;
            return true;
        }

        if (stream->Seek(0, ozz::io::Stream::kSet) < 0)
        {
            ozz::log::Err() << "Failed to rewind animation stream: " << source_label << std::endl;
            return false;
        }

        archive = ozz::io::IArchive(stream);

        uint32_t anim_count = 0;
        archive >> anim_count;

        if (anim_count == 0)
        {
            ozz::log::Err() << "Invalid animation count in " << source_label << std::endl;
            return false;
        }

        animations_.resize(anim_count);
        animation_metadata_.resize(anim_count);
        for (uint32_t i = 0; i < anim_count; ++i)
        {
            archive >> animations_[i];
            animation_metadata_[i] = ReadSerializedMetadata(archive);
            if (animation_metadata_[i].name.empty())
            {
                const char* animation_name = animations_[i].name();
                if (animation_name)
                    animation_metadata_[i].name = animation_name;
            }
        }

        ozz::log::LogV() << "Loaded " << anim_count << " animations from " << source_label << std::endl;
        return true;
    }

    bool LoadMultipleAnimations(const char* filename)
    {
        if (!filename || filename[0] == '\0')
            return false;

        ozz::io::File file(filename, "rb");
        if (!file.opened())
        {
            ozz::log::Err() << "Failed to open animation file: " << filename << std::endl;
            animations_.clear();
            animation_metadata_.clear();
            return false;
        }

        return LoadAnimationsFromStream(&file, filename);
    }

    bool LoadEmbeddedAnimations(const std::vector<std::uint8_t>& bytes, const char* label)
    {
        if (bytes.empty())
            return false;

        ozz::io::MemoryStream stream;
        if (!stream.Write(bytes.data(), bytes.size()))
            return false;
        return LoadAnimationsFromStream(&stream, label);
    }

    void PrintPoseTable()
    {
        const auto joint_names = skeleton_.joint_names();
        const int joint_count = skeleton_.num_joints();

        size_t name_width_sz = std::strlen("Bone");
        for (int joint = 0; joint < joint_count; ++joint)
        {
            name_width_sz = std::max(name_width_sz, std::strlen(joint_names[joint]));
        }
        const int name_width = static_cast<int>(name_width_sz);

        const std::array<const char*, 6> headers = { "PosX", "PosY", "PosZ", "RotX", "RotY", "RotZ" };
        std::array<int, 6> column_widths{};
        for (size_t i = 0; i < headers.size(); ++i)
        {
            column_widths[i] = std::max<int>(10, std::strlen(headers[i]));
        }

        auto format_value = [](float value, int precision)
        {
            if (!std::isfinite(value))
            {
                return std::string("--");
            }
            if (std::fabs(value) < 1e-6f)
            {
                value = 0.0f;
            }
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(precision);
            if (value >= 0.f)
            {
                oss << ' ';
            }
            oss << value;
            return oss.str();
        };

        std::cout << "\n=== OZZ BIND POSE TABLE ===" << std::endl;
        std::cout << std::left << std::setw(name_width) << "Bone" << "  ";
        for (size_t i = 0; i < headers.size(); ++i)
        {
            std::cout << std::left << std::setw(column_widths[i]) << headers[i];
            if (i + 1 != headers.size())
            {
                std::cout << "  ";
            }
        }
        std::cout << std::endl;

        std::cout << std::string(name_width, '-') << "  ";
        for (size_t i = 0; i < headers.size(); ++i)
        {
            std::cout << std::string(column_widths[i], '-');
            if (i + 1 != headers.size())
            {
                std::cout << "  ";
            }
        }
        std::cout << std::endl;

        for (int joint = 0; joint < joint_count; ++joint)
        {
            const ozz::math::Float4x4& matrix = models_[joint];
            const float tx = ozz::math::GetX(matrix.cols[3]);
            const float ty = ozz::math::GetY(matrix.cols[3]);
            const float tz = ozz::math::GetZ(matrix.cols[3]);

            float rx = std::numeric_limits<float>::quiet_NaN();
            float ry = std::numeric_limits<float>::quiet_NaN();
            float rz = std::numeric_limits<float>::quiet_NaN();
            if (ozz::math::AreAllTrue1(ozz::math::IsOrthogonal(matrix)))
            {
                const ozz::math::SimdFloat4 quat_simd = ozz::math::ToQuaternion(matrix);
                float quat_values[4];
                ozz::math::StorePtrU(quat_simd, quat_values);
                const ozz::math::Quaternion quat(quat_values[0], quat_values[1], quat_values[2], quat_values[3]);
                const ozz::math::Float3 euler = ozz::math::ToEuler(quat);
                rx = euler.x * ozz::math::kRadianToDegree;
                ry = euler.y * ozz::math::kRadianToDegree;
                rz = euler.z * ozz::math::kRadianToDegree;
            }

            std::cout << std::left << std::setw(name_width) << joint_names[joint] << "  " << std::right << std::setw(column_widths[0]) << format_value(tx, 6)
                      << "  " << std::setw(column_widths[1]) << format_value(ty, 6) << "  " << std::setw(column_widths[2]) << format_value(tz, 6) << "  "
                      << std::setw(column_widths[3]) << format_value(rx, 3) << "  " << std::setw(column_widths[4]) << format_value(ry, 3) << "  "
                      << std::setw(column_widths[5]) << format_value(rz, 3) << std::endl;
        }

        std::cout << std::endl;
    }

protected:
    // Updates current animation time and skeleton pose.
    virtual bool OnUpdate(float _dt, float)
    {
        GLFWwindow* window = nullptr;
        if (ozz::sample::Application::GetCurrent())
        {
            window = ozz::sample::Application::GetCurrent()->GetWindow();
        }
        const bool space_down = window && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (space_down && !space_was_down_ && HasAnimationSelected() && animations_[ui_state_.current_animation].duration() > 0.0f)
        {
            controller_.TogglePlay();
        }
        space_was_down_ = space_down;

        if (window)
        {
            const bool mouse_down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            ImGuiContext* context = ImGui::GetCurrentContext();
            bool mouse_captured = false;
            if (context)
            {
                const ImGuiIO& io = ImGui::GetIO();
                mouse_captured = io.WantCaptureMouse;
            }
            if (mouse_down && !mouse_left_was_down_)
            {
                if (!mouse_captured)
                {
                    double cursor_x = 0.0;
                    double cursor_y = 0.0;
                    glfwGetCursorPos(window, &cursor_x, &cursor_y);
                    pending_pick_pos_ = ImVec2(static_cast<float>(cursor_x), static_cast<float>(cursor_y));
                    pending_pick_ = true;
                }
            }
            else if (mouse_down && ik_handle_dragging_ && !mouse_captured)
            {
                double cursor_x = 0.0;
                double cursor_y = 0.0;
                glfwGetCursorPos(window, &cursor_x, &cursor_y);
                pending_pick_pos_ = ImVec2(static_cast<float>(cursor_x), static_cast<float>(cursor_y));
                pending_pick_ = true;
            }
            else if (!mouse_down && mouse_left_was_down_)
            {
                if (ik_handle_dragging_)
                {
                    ik_handle_dragging_ = false;
                    active_ik_handle_ = IkHandleId::None;
                }
            }
            mouse_left_was_down_ = mouse_down;
        }

        if (HasAnimationSelected() && animations_[ui_state_.current_animation].duration() > 0.0f)
        {
            controller_.Update(animations_[ui_state_.current_animation], _dt);

            ozz::animation::SamplingJob sampling_job;
            sampling_job.animation = &animations_[ui_state_.current_animation];
            sampling_job.context = &context_;
            sampling_job.ratio = controller_.time_ratio();
            sampling_job.output = make_span(locals_);
            if (!sampling_job.Run())
            {
                return false;
            }
        }
        else
        {
            for (int i = 0; i < skeleton_.num_soa_joints(); ++i)
            {
                locals_[i] = skeleton_.joint_rest_poses()[i];
            }
        }

        ApplyProceduralCrouch();

        const bool ragdoll_enabled = ui_state_.ragdoll_enabled;
        if (ragdoll_enabled)
        {
            EnsureRagdollState();

            if (ragdoll_reset_requested_ || !ragdoll_was_enabled_)
            {
                ResetRagdollStateFromLocals();
                ragdoll_reset_requested_ = false;
            }

            ragdoll_target_locals_ = locals_;
            UpdateActiveRagdoll(std::max(_dt, 0.f));
        }
        else if (ragdoll_was_enabled_)
        {
            ragdoll_was_enabled_ = false;
            ragdoll_target_locals_.clear();
            for (RagdollJointState& state : ragdoll_state_)
            {
                state.initialized = false;
                state.velocity = { 0.f, 0.f, 0.f };
            }
        }

        ozz::animation::LocalToModelJob ltm_job;
        ltm_job.skeleton = &skeleton_;
        ltm_job.input = make_span(locals_);
        ltm_job.output = make_span(models_);
        if (!ltm_job.Run())
        {
            return false;
        }

        if (!ApplyLimbIKAfterLocal())
        {
            return false;
        }

        ragdoll_was_enabled_ = ragdoll_enabled;
        return true;
    }

    virtual bool OnDisplay(ozz::sample::Renderer* _renderer)
    {
        bool success = true;
        const ozz::math::Float4x4 transform = ozz::math::Float4x4::identity();

        ozz::sample::internal::RendererImpl* renderer_impl = dynamic_cast<ozz::sample::internal::RendererImpl*>(_renderer);
        const ozz::math::Float4x4* view_projection = nullptr;
        ozz::sample::internal::Camera* renderer_camera = nullptr;
        ozz::math::Float4x4 inv_view;
        bool inv_view_valid = false;
        ozz::math::Float3 camera_position{ 0.f, 0.f, 0.f };
        if (renderer_impl)
        {
            renderer_camera = renderer_impl->camera();
            if (renderer_camera)
            {
                view_projection = &renderer_camera->view_proj();
                inv_view = ozz::math::Invert(renderer_camera->view());
                inv_view_valid = true;
                camera_position = { ozz::math::GetX(inv_view.cols[3]), ozz::math::GetY(inv_view.cols[3]), ozz::math::GetZ(inv_view.cols[3]) };
            }
        }

        imgui_frame_ready_ = false;

        if (ui_state_.draw_skeleton)
        {
            success &= _renderer->DrawPosture(skeleton_, make_span(models_), transform);
        }

        auto draw_ik_target = [&](IkHandleId handle, const ozz::sample::Color& base_color)
        {
            const LimbIkChain* chain = GetChainForHandle(handle);
            if (!chain || !chain->debug_target_valid)
            {
                return;
            }

            const bool active = ik_handle_dragging_ && active_ik_handle_ == handle;
            const ozz::sample::Color& color = active ? ozz::sample::kMagenta : base_color;

            const ozz::math::Float4x4 marker = ozz::math::Float4x4::Translation(chain->debug_target);
            const ozz::math::Float4x4 axes = marker * ozz::math::Float4x4::Scaling(ozz::math::simd_float4::Load1(kIkAxisScale));
            success &= _renderer->DrawSphereIm(kIkHandleDrawRadius, marker, color);
            success &= _renderer->DrawAxes(axes);

            if (chain->Valid() && static_cast<size_t>(chain->end) < models_.size())
            {
                const ozz::math::Float3 end_position = ExtractTranslation(models_[chain->end]);
                const ozz::math::Float3 line[] = { end_position, chain->debug_target };
                success &= _renderer->DrawLines(line, color, ozz::math::Float4x4::identity());
            }
        };

        draw_ik_target(IkHandleId::LeftLeg, ozz::sample::kCyan);
        draw_ik_target(IkHandleId::RightLeg, ozz::sample::kCyan);
        draw_ik_target(IkHandleId::LeftArm, ozz::sample::kYellow);
        draw_ik_target(IkHandleId::RightArm, ozz::sample::kYellow);

        bool request_pick = false;
        float pick_x = 0.f;
        float pick_y = 0.f;
        float viewport_width = 0.f;
        float viewport_height = 0.f;
        SelectedTriangle best_triangle_candidate{};
        bool triangle_pick_found = false;
        float pick_ndc_x = 0.f;
        float pick_ndc_y = 0.f;
        bool pick_has_ndc = false;
        if (pending_pick_ && !meshes_.empty())
        {
            GLFWwindow* window = nullptr;
            if (ozz::sample::Application::GetCurrent())
            {
                window = ozz::sample::Application::GetCurrent()->GetWindow();
            }
            if (window)
            {
                int fb_width = 0;
                int fb_height = 0;
                glfwGetFramebufferSize(window, &fb_width, &fb_height);
                viewport_width = static_cast<float>(fb_width);
                viewport_height = static_cast<float>(fb_height);
                pick_x = pending_pick_pos_.x;
                pick_y = viewport_height - pending_pick_pos_.y;
                request_pick = true;
            }
            pending_pick_ = false;
        }

        if (request_pick)
        {
            if (renderer_camera && viewport_width > 0.f && viewport_height > 0.f)
            {
                pick_ndc_x = (pick_x / viewport_width) * 2.f - 1.f;
                pick_ndc_y = (pick_y / viewport_height) * 2.f - 1.f;
                pick_has_ndc = true;

                if (!inv_view_valid)
                {
                    pick_ray_valid_ = false;
                }
                else
                {
                    const ozz::math::Float4x4 inv_proj = ozz::math::Invert(renderer_camera->projection());

                    const ozz::math::SimdFloat4 near_ndc = ozz::math::simd_float4::Load(pick_ndc_x, pick_ndc_y, -1.f, 1.f);
                    const ozz::math::SimdFloat4 far_ndc = ozz::math::simd_float4::Load(pick_ndc_x, pick_ndc_y, 1.f, 1.f);

                    ozz::math::SimdFloat4 view_near = ozz::math::TransformPoint(inv_proj, near_ndc);
                    ozz::math::SimdFloat4 view_far = ozz::math::TransformPoint(inv_proj, far_ndc);

                    float view_near_components[4];
                    float view_far_components[4];
                    ozz::math::StorePtrU(view_near, view_near_components);
                    ozz::math::StorePtrU(view_far, view_far_components);

                    const float near_w = view_near_components[3];
                    const float far_w = view_far_components[3];
                    const float epsilon = std::numeric_limits<float>::epsilon();

                    if (std::fabs(near_w) <= epsilon || std::fabs(far_w) <= epsilon)
                    {
                        pick_ray_valid_ = false;
                    }
                    else
                    {
                        ozz::math::Float3 view_origin = {
                            view_near_components[0] / near_w,
                            view_near_components[1] / near_w,
                            view_near_components[2] / near_w,
                        };

                        ozz::math::Float3 view_point = {
                            view_far_components[0] / far_w,
                            view_far_components[1] / far_w,
                            view_far_components[2] / far_w,
                        };

                        ozz::math::Float3 view_dir = {
                            view_point.x - view_origin.x,
                            view_point.y - view_origin.y,
                            view_point.z - view_origin.z,
                        };

                        const float dir_length_sq = view_dir.x * view_dir.x + view_dir.y * view_dir.y + view_dir.z * view_dir.z;
                        if (dir_length_sq <= epsilon)
                        {
                            pick_ray_valid_ = false;
                        }
                        else
                        {
                            const float inv_dir_length = 1.f / std::sqrt(dir_length_sq);
                            view_dir.x *= inv_dir_length;
                            view_dir.y *= inv_dir_length;
                            view_dir.z *= inv_dir_length;

                            const ozz::math::SimdFloat4 view_origin_simd = ozz::math::simd_float4::Load(view_origin.x, view_origin.y, view_origin.z, 1.f);
                            const ozz::math::SimdFloat4 view_dir_simd = ozz::math::simd_float4::Load(view_dir.x, view_dir.y, view_dir.z, 0.f);

                            const ozz::math::SimdFloat4 world_origin_simd = ozz::math::TransformPoint(inv_view, view_origin_simd);
                            ozz::math::SimdFloat4 world_dir_simd = ozz::math::TransformVector(inv_view, view_dir_simd);
                            world_dir_simd = ozz::math::Normalize3(world_dir_simd);

                            ozz::math::Store3PtrU(world_origin_simd, &pick_ray_origin_.x);
                            ozz::math::Store3PtrU(world_dir_simd, &pick_ray_direction_.x);
                            pick_ray_length_ = 200.f;
                            pick_ray_valid_ = true;
                        }
                    }
                }
            }
        }

        bool ik_pick_consumed = false;
        if (request_pick && pick_ray_valid_)
        {
            if (ik_handle_dragging_)
            {
                UpdateActiveIkHandle(pick_ray_origin_, pick_ray_direction_);
                ik_pick_consumed = true;
            }
            else
            {
                const ozz::math::Float3 camera_pos = inv_view_valid ? camera_position : pick_ray_origin_;
                const ozz::math::Float4x4* selection_vp = (pick_has_ndc && view_projection) ? view_projection : nullptr;
                if (TrySelectIkHandle(pick_ray_origin_, pick_ray_direction_, camera_pos,
                        pick_has_ndc ? pick_ndc_x : 0.f, pick_has_ndc ? pick_ndc_y : 0.f, selection_vp))
                {
                    ik_pick_consumed = true;
                }
            }
        }

        if (ik_pick_consumed)
        {
            pick_ray_valid_ = false;
        }

        const ozz::math::Float3* pick_ray_origin_ptr = (request_pick && pick_ray_valid_) ? &pick_ray_origin_ : nullptr;
        const ozz::math::Float3* pick_ray_direction_ptr = (request_pick && pick_ray_valid_) ? &pick_ray_direction_ : nullptr;

        if (ui_state_.draw_mesh && !meshes_.empty())
        {
            const bool visibility_missing = ui_state_.mesh_visibility.size() != meshes_.size();
            for (size_t mesh_index = 0; mesh_index < meshes_.size(); ++mesh_index)
            {
                if (!visibility_missing && ui_state_.mesh_visibility[mesh_index] == 0)
                {
                    continue;
                }
                const ozz::sample::Mesh& mesh = meshes_[mesh_index];
                const size_t palette_size = mesh.joint_remaps.size();
                if (palette_size > skinning_matrices_.size())
                {
                    continue;
                }

                for (size_t palette_index = 0; palette_index < palette_size; ++palette_index)
                {
                    const uint16_t joint = mesh.joint_remaps[palette_index];
                    skinning_matrices_[palette_index] = models_[joint] * mesh.inverse_bind_poses[palette_index];
                }

                auto skinning_span = ozz::make_span(skinning_matrices_);
                skinning_span = skinning_span.first(palette_size);

                const bool triangle_debug_requested =
                    ui_state_.renderer_options.triangles && mesh_index < triangle_debug_meshes_.size() && !triangle_debug_meshes_[mesh_index].empty();

                ozz::sample::Renderer::Options draw_options = ui_state_.renderer_options;
                if (triangle_debug_requested)
                {
                    draw_options.triangles = false;
                    draw_options.colors = false;
                    draw_options.texture = false;
                }

                if (draw_options.texture && mesh_index < mesh_textures_.size())
                {
                    draw_options.texture_override = mesh_textures_[mesh_index];
                }

                success &= _renderer->DrawSkinnedMesh(mesh, skinning_span, transform, draw_options);

                const bool triangle_mode_pick = request_pick;
                const bool needs_triangle_refresh = selected_triangle_.valid && selected_triangle_.mesh_index == mesh_index;
                const bool compute_selection_data = triangle_mode_pick || needs_triangle_refresh || triangle_debug_requested;

                if (compute_selection_data)
                {
                    const size_t vertex_count = static_cast<size_t>(mesh.vertex_count());
                    if (vertex_count == 0)
                    {
                        continue;
                    }

                    std::vector<ozz::math::Float3> world_positions(vertex_count);
                    std::vector<VertexReference> vertex_refs(vertex_count);

                    const std::vector<uint32_t>& remapped_to_original = mesh.xray_metadata.remapped_to_original;
                    constexpr uint32_t kInvalidVertexIndex = std::numeric_limits<uint32_t>::max();

                    uint32_t global_vertex = 0;

                    for (size_t part_index = 0; part_index < mesh.parts.size(); ++part_index)
                    {
                        const ozz::sample::Mesh::Part& part = mesh.parts[part_index];
                        const int vertex_count_in_part = part.vertex_count();
                        if (vertex_count_in_part <= 0)
                        {
                            continue;
                        }

                        const int influences = std::max(1, part.influences_count());
                        const bool part_has_skinning = mesh.skinned() && part.joint_indices.size() >= static_cast<size_t>(vertex_count_in_part * influences);
                        const bool part_has_weights = influences > 1 && part.joint_weights.size() >= static_cast<size_t>(vertex_count_in_part * (influences - 1));
                        const bool part_has_normals = !part.normals.empty();

                        for (int local_index = 0; local_index < vertex_count_in_part; ++local_index, ++global_vertex)
                        {
                            const float* position_ptr = &part.positions[local_index * ozz::sample::Mesh::Part::kPositionsCpnts];
                            const ozz::math::SimdFloat4 local_pos_simd = ozz::math::simd_float4::Load(position_ptr[0], position_ptr[1], position_ptr[2], 1.f);

                            ozz::math::SimdFloat4 world_pos_simd = local_pos_simd;
                            std::array<uint16_t, 4> palette_indices{
                                { 0, 0, 0, 0 }
                            };
                            std::array<float, 4> weights{
                                { 0.f, 0.f, 0.f, 0.f }
                            };

                            if (part_has_skinning)
                            {
                                world_pos_simd = ozz::math::simd_float4::zero();
                                float accumulated = 0.f;
                                for (int influence = 0; influence < influences && influence < 4; ++influence)
                                {
                                    float weight = 1.f;
                                    if (influences > 1)
                                    {
                                        if (influence < influences - 1)
                                        {
                                            const size_t weight_index =
                                                static_cast<size_t>(local_index) * static_cast<size_t>(influences - 1) + static_cast<size_t>(influence);
                                            weight = part_has_weights && weight_index < part.joint_weights.size() ? part.joint_weights[weight_index] : 0.f;
                                            accumulated += weight;
                                        }
                                        else
                                        {
                                            weight = std::max(0.f, 1.f - accumulated);
                                        }
                                    }

                                    const size_t joint_index =
                                        static_cast<size_t>(local_index) * static_cast<size_t>(influences) + static_cast<size_t>(influence);
                                    const uint16_t palette_index = joint_index < part.joint_indices.size() ? part.joint_indices[joint_index] : uint16_t{ 0 };
                                    palette_indices[static_cast<size_t>(influence)] = palette_index;
                                    weights[static_cast<size_t>(influence)] = weight;

                                    if (weight <= 0.f || palette_index >= skinning_span.size())
                                    {
                                        continue;
                                    }

                                    const ozz::math::SimdFloat4 transformed = ozz::math::TransformPoint(skinning_span[palette_index], local_pos_simd);
                                    world_pos_simd = ozz::math::MAdd(transformed, ozz::math::simd_float4::Load1(weight), world_pos_simd);
                                }
                            }
                            else
                            {
                                weights[0] = 1.f;
                            }

                            ozz::math::Float3 world_position;
                            ozz::math::Store3PtrU(world_pos_simd, &world_position.x);
                            world_positions[global_vertex] = world_position;

                            const uint32_t remapped_index = global_vertex;
                            uint32_t original_index = kInvalidVertexIndex;
                            if (remapped_index < remapped_to_original.size())
                            {
                                original_index = remapped_to_original[remapped_index];
                            }
                            vertex_refs[global_vertex] = VertexReference{ part_index, local_index, remapped_index, original_index };

                            ozz::math::Float3 world_normal{ 0.f, 0.f, 0.f };
                            if (part_has_normals)
                            {
                                const float* normal_ptr = &part.normals[local_index * ozz::sample::Mesh::Part::kNormalsCpnts];
                                const ozz::math::SimdFloat4 local_normal_simd = ozz::math::simd_float4::Load(normal_ptr[0], normal_ptr[1], normal_ptr[2], 0.f);
                                ozz::math::SimdFloat4 world_normal_simd = local_normal_simd;
                                if (part_has_skinning)
                                {
                                    world_normal_simd = ozz::math::simd_float4::zero();
                                    for (int influence = 0; influence < influences && influence < 4; ++influence)
                                    {
                                        const float weight = weights[static_cast<size_t>(influence)];
                                        const uint16_t palette_index = palette_indices[static_cast<size_t>(influence)];
                                        if (weight <= 0.f || palette_index >= skinning_span.size())
                                        {
                                            continue;
                                        }
                                        const ozz::math::SimdFloat4 transformed = ozz::math::TransformVector(skinning_span[palette_index], local_normal_simd);
                                        world_normal_simd = ozz::math::MAdd(transformed, ozz::math::simd_float4::Load1(weight), world_normal_simd);
                                    }
                                }
                                ozz::math::Store3PtrU(world_normal_simd, &world_normal.x);
                                const float normal_length_sq =
                                    world_normal.x * world_normal.x + world_normal.y * world_normal.y + world_normal.z * world_normal.z;
                                if (normal_length_sq > std::numeric_limits<float>::epsilon())
                                {
                                    const float inv_length = 1.f / std::sqrt(normal_length_sq);
                                    world_normal.x *= inv_length;
                                    world_normal.y *= inv_length;
                                    world_normal.z *= inv_length;
                                }
                                else
                                {
                                    world_normal = { 0.f, 0.f, 0.f };
                                }
                            }
                        }
                    }

                    if (triangle_mode_pick)
                    {
                        SelectedTriangle candidate;
                        if (EvaluateTrianglePick(mesh_index, mesh, world_positions, vertex_refs, pick_x, pick_y, viewport_width, viewport_height,
                                view_projection, pick_ray_origin_ptr, pick_ray_direction_ptr, candidate))
                        {
                            if (!triangle_pick_found || IsBetterTrianglePick(candidate, best_triangle_candidate))
                            {
                                best_triangle_candidate = candidate;
                                triangle_pick_found = true;
                            }
                        }
                    }

                    if (needs_triangle_refresh)
                    {
                        bool valid = true;
                        for (size_t i = 0; i < 3; ++i)
                        {
                            const uint32_t vertex_index = selected_triangle_.vertex_indices[i];
                            if (vertex_index >= world_positions.size())
                            {
                                valid = false;
                                break;
                            }
                            selected_triangle_.world_positions[i] = world_positions[vertex_index];
                        }

                        if (valid)
                        {
                            const ozz::math::Float3& a = selected_triangle_.world_positions[0];
                            const ozz::math::Float3& b = selected_triangle_.world_positions[1];
                            const ozz::math::Float3& c = selected_triangle_.world_positions[2];
                            selected_triangle_.centroid = { (a.x + b.x + c.x) * (1.f / 3.f), (a.y + b.y + c.y) * (1.f / 3.f), (a.z + b.z + c.z) * (1.f / 3.f) };

                            const ozz::math::Float3 edge0 = { b.x - a.x, b.y - a.y, b.z - a.z };
                            const ozz::math::Float3 edge1 = { c.x - a.x, c.y - a.y, c.z - a.z };
                            ozz::math::Float3 normal = { edge0.y * edge1.z - edge0.z * edge1.y, edge0.z * edge1.x - edge0.x * edge1.z,
                                edge0.x * edge1.y - edge0.y * edge1.x };
                            const float normal_length_sq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
                            if (normal_length_sq > std::numeric_limits<float>::epsilon())
                            {
                                const float normal_length = std::sqrt(normal_length_sq);
                                selected_triangle_.world_normal = { normal.x / normal_length, normal.y / normal_length, normal.z / normal_length };
                                selected_triangle_.area = 0.5f * normal_length;
                            }
                            else
                            {
                                selected_triangle_.world_normal = { 0.f, 0.f, 0.f };
                                selected_triangle_.area = 0.f;
                            }

                            for (size_t corner = 0; corner < 3; ++corner)
                            {
                                const uint32_t vertex_index = selected_triangle_.vertex_indices[corner];
                                ozz::math::Float2 uv{ 0.f, 0.f };
                                if (vertex_index < vertex_refs.size())
                                {
                                    const VertexReference& ref = vertex_refs[vertex_index];
                                    if (ref.part_index < mesh.parts.size())
                                    {
                                        const ozz::sample::Mesh::Part& ref_part = mesh.parts[ref.part_index];
                                        if (!ref_part.uvs.empty() &&
                                            ref.local_index >= 0 &&
                                            static_cast<size_t>(ref.local_index * ozz::sample::Mesh::Part::kUVsCpnts + 1) < ref_part.uvs.size())
                                        {
                                            const float* uv_ptr = &ref_part.uvs[ref.local_index * ozz::sample::Mesh::Part::kUVsCpnts];
                                            uv = { uv_ptr[0], uv_ptr[1] };
                                        }
                                    }
                                }
                                selected_triangle_.uvs[corner] = uv;
                            }
                        }
                        else
                        {
                            selected_triangle_ = SelectedTriangle{};
                        }
                    }

                    if (triangle_debug_requested && mesh_index < triangle_debug_meshes_.size())
                    {
                        for (TriangleDebugMesh& segment : triangle_debug_meshes_[mesh_index])
                        {
                            UpdateTriangleDebugMesh(world_positions, segment);
                        }
                    }
                }

                if (triangle_debug_requested && mesh_index < triangle_debug_meshes_.size())
                {
                    ozz::sample::Renderer::Options triangle_options;
                    triangle_options.triangles = true;
                    triangle_options.colors = true;
                    triangle_options.texture = false;
                    triangle_options.skip_skinning = true;
                    triangle_options.wireframe = ui_state_.renderer_options.wireframe;

                    for (const TriangleDebugMesh& segment : triangle_debug_meshes_[mesh_index])
                    {
                        if (!segment.mesh.parts.empty())
                        {
                            success &= _renderer->DrawMesh(segment.mesh, ozz::math::Float4x4::identity(), triangle_options);
                        }
                    }
                }
            }
        }

        if (request_pick && triangle_pick_found)
        {
            selected_triangle_ = best_triangle_candidate;
        }

        if (pick_ray_valid_)
        {
            std::array<ozz::math::Float3, 2> ray_points;
            ray_points[0] = pick_ray_origin_;
            ray_points[1] = { pick_ray_origin_.x + pick_ray_direction_.x * pick_ray_length_, pick_ray_origin_.y + pick_ray_direction_.y * pick_ray_length_,
                pick_ray_origin_.z + pick_ray_direction_.z * pick_ray_length_ };

            success &= _renderer->DrawLines(ozz::make_span(ray_points), ozz::sample::kYellow, transform);
        }

        if (selected_triangle_.valid)
        {
            const float surface_offset = 0.001f;
            const ozz::math::Float3 offset = {
                selected_triangle_.world_normal.x * surface_offset,
                selected_triangle_.world_normal.y * surface_offset,
                selected_triangle_.world_normal.z * surface_offset,
            };

            ozz::sample::Mesh highlight_mesh;
            ozz::sample::Mesh::Part part;
            part.positions.reserve(3 * ozz::sample::Mesh::Part::kPositionsCpnts);
            part.normals.reserve(3 * ozz::sample::Mesh::Part::kNormalsCpnts);
            part.tangents.reserve(3 * ozz::sample::Mesh::Part::kTangentsCpnts);

            static const uint8_t kColorR = 255;
            static const uint8_t kColorG = 32;
            static const uint8_t kColorB = 255;
            static const uint8_t kColorA = 96;

            for (size_t i = 0; i < 3; ++i)
            {
                const ozz::math::Float3& base = selected_triangle_.world_positions[i];
                const ozz::math::Float3 elevated = { base.x + offset.x, base.y + offset.y, base.z + offset.z };
                part.positions.push_back(elevated.x);
                part.positions.push_back(elevated.y);
                part.positions.push_back(elevated.z);

                part.normals.push_back(selected_triangle_.world_normal.x);
                part.normals.push_back(selected_triangle_.world_normal.y);
                part.normals.push_back(selected_triangle_.world_normal.z);

                part.tangents.push_back(selected_triangle_.world_normal.x);
                part.tangents.push_back(selected_triangle_.world_normal.y);
                part.tangents.push_back(selected_triangle_.world_normal.z);
                part.tangents.push_back(1.f);

                part.colors.push_back(kColorR);
                part.colors.push_back(kColorG);
                part.colors.push_back(kColorB);
                part.colors.push_back(kColorA);
            }

            highlight_mesh.parts.push_back(std::move(part));
            highlight_mesh.triangle_indices = { 0, 1, 2 };

            ozz::sample::Renderer::Options highlight_options;
            highlight_options.triangles = true;
            highlight_options.texture = false;
            highlight_options.vertices = false;
            highlight_options.normals = false;
            highlight_options.tangents = false;
            highlight_options.binormals = false;
            highlight_options.colors = true;
            highlight_options.wireframe = false;
            highlight_options.skip_skinning = true;
            highlight_options.triangles = true;
            highlight_options.colors = true;
            highlight_options.texture = false;

            success &= _renderer->DrawMesh(highlight_mesh, ozz::math::Float4x4::identity(), highlight_options);
        }

        if (imgui_layer_)
        {
            imgui_layer_->BeginFrame();
            DrawDearImGuiPanels();
            imgui_frame_ready_ = true;
        }

        return success;
    }

    bool OnRenderUiOverlay() override
    {
        if (imgui_layer_ && imgui_frame_ready_)
        {
            imgui_layer_->Render();
            imgui_frame_ready_ = false;
        }
        return true;
    }

    virtual bool OnInitialize()
    {
        SetUseSampleGui(false);

        if (!imgui_layer_)
        {
            imgui_layer_ = std::make_unique<DearImGuiLayer>();
        }
        GLFWwindow* window = nullptr;
        if (ozz::sample::Application::GetCurrent())
        {
            window = ozz::sample::Application::GetCurrent()->GetWindow();
        }
        if (window && imgui_layer_)
        {
            if (!imgui_layer_->Initialize(window))
            {
                ozz::log::Err() << "Failed to initialize Dear ImGui layer." << std::endl;
            }
        }

        ui_state_.random_triangle_colors = OPTIONS_random_triangle_colors;

        const char* bundle_option = OPTIONS_bundle;
        const bool bundle_requested = bundle_option && bundle_option[0] != '\0';
        std::string mesh_reference_path;
        bool mesh_loaded = false;
        embedded_animation_loaded_ = false;
        bool has_animation = false;
        bool attempted_animation_load = false;

        if (bundle_requested)
        {
            XRay::Animation::OzzxBundle bundle;
            if (!XRay::Animation::ReadOzzxBundle(fs::absolute(fs::path(bundle_option)), bundle))
            {
                ozz::log::Err() << "Failed to read OZZX bundle: " << bundle_option << std::endl;
                return false;
            }
            if (!LoadSkeletonFromBytes(bundle.skeleton, &skeleton_))
            {
                ozz::log::Err() << "Failed to load skeleton from bundle: " << bundle_option << std::endl;
                return false;
            }
            skeleton_label_ = GetFileStem(bundle_option);
            if (!LoadMeshesFromBytes(bundle.mesh, &meshes_))
            {
                ozz::log::Err() << "Failed to load meshes from bundle: " << bundle_option << std::endl;
                return false;
            }
            mesh_loaded = !meshes_.empty();
            mesh_reference_path = bundle_option;
            using_bundle_ = true;
            bundle_source_path_ = bundle_option;

            bool external_animation_requested = OPTIONS_animation && OPTIONS_animation[0] != '\0';
            if (external_animation_requested && std::strcmp(OPTIONS_animation, "media/animation.ozz") == 0)
            {
                external_animation_requested = false;
            }
            if (external_animation_requested && !fs::exists(fs::path(OPTIONS_animation.value())))
            {
                external_animation_requested = false;
            }
            bool external_loaded = false;
            if (external_animation_requested)
            {
                external_loaded = LoadMultipleAnimations(OPTIONS_animation);
                has_animation = external_loaded;
                attempted_animation_load = true;
            }

            if (!has_animation && !bundle.embedded_animation_data.empty())
            {
                if (LoadEmbeddedAnimations(bundle.embedded_animation_data, bundle_option))
                {
                    has_animation = true;
                    embedded_animation_loaded_ = true;
                    std::cout << "Loaded embedded animations from bundle: " << bundle_option << std::endl;
                }
                else
                {
                    ozz::log::Err() << "Failed to load embedded animations from bundle: " << bundle_option << std::endl;
                }
            }
            else if (external_loaded)
            {
                embedded_animation_loaded_ = false;
            }
        }
        else
        {
            if (!ozz::sample::LoadSkeleton(OPTIONS_skeleton, &skeleton_))
            {
                ozz::log::Err() << "Failed to load skeleton: " << OPTIONS_skeleton << std::endl;
                return false;
            }
            skeleton_label_ = GetFileStem(OPTIONS_skeleton);
            using_bundle_ = false;
            bundle_source_path_.clear();

            if (OPTIONS_mesh && OPTIONS_mesh[0] != '\0')
            {
                if (!ozz::sample::LoadMeshes(OPTIONS_mesh, &meshes_))
                {
                    ozz::log::Err() << "Failed to load meshes: " << OPTIONS_mesh << std::endl;
                    return false;
                }
                mesh_loaded = true;
                mesh_reference_path = OPTIONS_mesh;
            }

            has_animation = LoadMultipleAnimations(OPTIONS_animation);
            attempted_animation_load = true;
        }

        // Try reading animation(s), but allow failure for bind pose testing
        if (!has_animation && !embedded_animation_loaded_ && !attempted_animation_load)
        {
            bool should_attempt = false;
            if (OPTIONS_animation && OPTIONS_animation[0] != '\0')
            {
                if (std::strcmp(OPTIONS_animation, "media/animation.ozz") != 0)
                {
                    should_attempt = fs::exists(fs::path(OPTIONS_animation.value()));
                }
            }
            if (should_attempt)
            {
                has_animation = LoadMultipleAnimations(OPTIONS_animation);
                attempted_animation_load = true;
            }
        }

        if (mesh_loaded)
        {
            std::cout << "\n=== DEBUG MESH INFO ===" << std::endl;
            for (size_t mesh_index = 0; mesh_index < meshes_.size(); ++mesh_index)
            {
                const ozz::sample::Mesh& mesh = meshes_[mesh_index];
                const ozz::sample::XRayMeshMetadata& metadata = mesh.xray_metadata;
                const std::size_t index_count = mesh.triangle_indices.size();

                std::cout << "Mesh[" << mesh_index << "]: triangle_indices=" << index_count
                          << ", original_faces=" << metadata.original_face_count
                          << ", original_vertices=" << metadata.original_vertex_count
                          << ", ogf_type=" << static_cast<int>(metadata.ogf_type) << std::endl;

                const ProgressiveMetadataAnalysis analysis = AnalyzeProgressiveMetadata(metadata.progressive_data);
                if (analysis.has_data)
                {
                    if (!analysis.header_valid)
                    {
                        std::cout << "  Progressive metadata invalid (size=" << metadata.progressive_data.size() << " bytes)" << std::endl;
                    }
                    else
                    {
                        std::cout << "  Progressive collapse_count=" << analysis.collapse_count
                                  << ", window_count=" << analysis.window_count
                                  << ", first_window(offset=" << analysis.first_offset
                                  << ", tris=" << analysis.first_tris
                                  << "), last_window(offset=" << analysis.last_offset
                                  << ", tris=" << analysis.last_tris
                                  << "), requires_indices=" << analysis.max_required_indices
                                  << ", has_zero_offset=" << (analysis.has_zero_offset ? "yes" : "no") << std::endl;

                        if (analysis.max_required_indices > index_count)
                        {
                            std::cout << "  WARNING: progressive windows reference " << analysis.max_required_indices
                                      << " indices but mesh stores only " << index_count << std::endl;
                        }
                    }
                }
            }
        }

        if (has_animation && !animations_.empty())
        {
            // Skeleton and animation needs to match.
            if (skeleton_.num_joints() != animations_[0].num_tracks())
            {
                ozz::log::Err() << "Animation track count " << animations_[0].num_tracks()
                                << " does not match skeleton joints " << skeleton_.num_joints() << std::endl;
                return false;
            }
        }

        // Debug: output skeleton information
        std::cout << "\n=== DEBUG SKELETON INFO ===" << std::endl;
        std::cout << "Skeleton joints: " << skeleton_.num_joints() << std::endl;
        if (has_animation && !animations_.empty())
        {
            std::cout << "Loaded " << animations_.size() << " animations" << std::endl;
            for (size_t i = 0; i < animations_.size(); ++i)
            {
                const char* animation_name = animations_[i].name();
                const std::string display_name = animation_name && animation_name[0] != '\0' ? animation_name :
                    animation_metadata_.size() > i                                           ? animation_metadata_[i].name :
                                                                                               std::string();
                std::cout << "  Animation " << i << " ('" << display_name << "'): tracks=" << animations_[i].num_tracks()
                          << ", duration=" << animations_[i].duration() << " seconds" << std::endl;
            }
        }
        else
        {
            std::cout << "No animation loaded - using bind pose" << std::endl;
        }

        // Output all bone names
        const ozz::span<const char* const> joint_names = skeleton_.joint_names();
        std::cout << "Bone names:" << std::endl;
        for (int i = 0; i < skeleton_.num_joints(); ++i)
        {
            std::cout << "  [" << i << "] " << joint_names[i] << std::endl;
        }

        ui_state_.current_animation = animations_.empty() ? kNoAnimationIndex : 0;

        // Allocates runtime buffers.
        const int num_soa_joints = skeleton_.num_soa_joints();
        locals_.resize(num_soa_joints);
        const int num_joints = skeleton_.num_joints();
        models_.resize(num_joints);

        // Allocates a context that matches animation requirements.
        context_.Resize(num_joints);

        auto finalize_mesh_load = [&](const std::string& reference_path) -> bool
        {
            mesh_label_ = GetFileStem(reference_path.c_str());
            size_t skinning_count = 0;
            for (const ozz::sample::Mesh& mesh : meshes_)
            {
                skinning_count = std::max(skinning_count, mesh.joint_remaps.size());
            }
            skinning_matrices_.resize(skinning_count);
            for (const ozz::sample::Mesh& mesh : meshes_)
            {
                if (num_joints < mesh.highest_joint_index())
                {
                    ozz::log::Err() << "The provided mesh doesn't match skeleton"
                                    << " (joint count mismatch)." << std::endl;
                    ozz::log::Err() << "  Skeleton joints: " << num_joints
                                    << ", mesh requires: " << mesh.highest_joint_index() << std::endl;
                    return false;
                }
            }
            ui_state_.draw_mesh = true;
            LoadMeshTextures(reference_path.c_str());
            RefreshMeshDisplayState();
            BuildTriangleDebugMeshes();
            pick_ray_valid_ = false;
            return true;
        };

        if (mesh_loaded)
        {
            if (!finalize_mesh_load(mesh_reference_path))
            {
                return false;
            }
        }
        else
        {
            meshes_.clear();
            skinning_matrices_.clear();
            ui_state_.draw_mesh = false;
            mesh_names_.clear();
            ui_state_.mesh_visibility.clear();
            ReleaseMeshTextures();
            triangle_debug_meshes_.clear();
            pick_ray_valid_ = false;
        }

        if (!ComputeBindPoseModelMatrices())
        {
            return false;
        }

        InitializeLimbIkChains();

        ui_state_.bone_display_limit = std::min(num_joints, 16);

        if (OPTIONS_dump_animation_json && OPTIONS_dump_animation_json[0] != '\0')
        {
            if (!ExportAnimationToJson(OPTIONS_dump_animation_json))
            {
                return false;
            }
        }

        // Optionally set a forced animation time ratio from environment.
        const char* ratio_env = std::getenv("OZZ_SAMPLE_RATIO");
        const char* time_env = std::getenv("OZZ_SAMPLE_TIME");
        if (ratio_env)
        {
            forced_time_ratio_ = std::clamp(static_cast<float>(std::atof(ratio_env)), 0.f, 1.f);
            use_forced_time_ratio_ = true;
        }
        else if (time_env && !animations_.empty())
        {
            const float duration = animations_[ui_state_.current_animation].duration();
            if (duration > 0.f)
            {
                const float sample_time = static_cast<float>(std::atof(time_env));
                forced_time_ratio_ = std::clamp(sample_time / duration, 0.f, 1.f);
                use_forced_time_ratio_ = true;
            }
        }

        if (use_forced_time_ratio_)
        {
            controller_.set_playback_speed(0.f);
            controller_.set_time_ratio(forced_time_ratio_);
        }

        return true;
    }

    void OnDestroy() override
    {
        if (imgui_layer_)
        {
            imgui_layer_->Shutdown();
            imgui_layer_.reset();
        }
        ReleaseMeshTextures();
    }

    virtual bool OnGui(ozz::sample::ImGui*)
    {
        return true;
    }

    virtual void GetSceneBounds(ozz::math::Box* _bound) const
    {
        ozz::sample::ComputePostureBounds(make_span(models_), ozz::math::Float4x4::identity(), _bound);
    }

private:
    // Playback animation controller. This is a utility class that helps with
    // controlling animation playback time.
    ozz::sample::PlaybackController controller_;

    // Runtime skeleton.
    ozz::animation::Skeleton skeleton_;

    // Runtime animations (support multiple).
    ozz::vector<ozz::animation::Animation> animations_;

    // Metadata per animation loaded from converter archives.
    std::vector<MotionMetadataData> animation_metadata_;

    struct LimbIkChain
    {
        enum class Role
        {
            Leg,
            Arm,
        };

        std::string label;
        Role role = Role::Leg;
        int start = -1;
        int mid = -1;
        int end = -1;
        ozz::math::SimdFloat4 mid_axis;
        ozz::math::Float3 target_offset = { 0.f, 0.f, 0.f };
        bool enabled = true;
        bool reached = false;
        std::string start_name;
        std::string mid_name;
        std::string end_name;
        ozz::math::Float3 debug_target = { 0.f, 0.f, 0.f };
        bool debug_target_valid = false;

        bool Valid() const
        {
            return start >= 0 && mid >= 0 && end >= 0;
        }
    };

    // UI state shared between rendering and presentation layers.
    ViewerUiState ui_state_{};

    std::unique_ptr<DearImGuiLayer> imgui_layer_;
    bool imgui_frame_ready_ = false;

    // Sampling context.
    ozz::animation::SamplingJob::Context context_;

    // Buffer of local transforms as sampled from animation_.
    ozz::vector<ozz::math::SoaTransform> locals_;

    // Buffer of model space matrices.
    ozz::vector<ozz::math::Float4x4> models_;
    ozz::vector<ozz::math::Float4x4> bind_pose_models_;

    // Optional meshes used for skinning validation / rendering.
    ozz::vector<ozz::sample::Mesh> meshes_;
    bool using_bundle_ = false;
    bool embedded_animation_loaded_ = false;
    std::string bundle_source_path_;
    ozz::vector<ozz::math::Float4x4> skinning_matrices_;

    // Cached labels for reporting/exporting.
    std::string skeleton_label_;
    std::string mesh_label_;
    std::vector<std::string> mesh_names_;
    std::vector<GLuint> mesh_textures_;
    std::vector<std::string> mesh_texture_paths_;

    // Optional externally forced time ratio for sampling animations.
    float forced_time_ratio_ = 0.f;
    bool use_forced_time_ratio_ = false;

    bool space_was_down_ = false;
    bool mouse_left_was_down_ = false;

    struct VertexReference
    {
        size_t part_index = 0;
        int local_index = 0;
        uint32_t remapped_index = std::numeric_limits<uint32_t>::max();
        uint32_t original_index = std::numeric_limits<uint32_t>::max();
    };

    struct SelectedTriangle
    {
        bool valid = false;
        size_t mesh_index = 0;
        uint32_t triangle_index = 0;
        std::array<uint32_t, 3> vertex_indices{
            { 0u, 0u, 0u }
        };
        std::array<VertexReference, 3> vertex_refs{
            { VertexReference{}, VertexReference{}, VertexReference{} }
        };
        std::array<ozz::math::Float3, 3> world_positions{
            { ozz::math::Float3{ 0.f, 0.f, 0.f }, ozz::math::Float3{ 0.f, 0.f, 0.f }, ozz::math::Float3{ 0.f, 0.f, 0.f } }
        };
        std::array<ozz::math::Float2, 3> uvs{
            { ozz::math::Float2{ 0.f, 0.f }, ozz::math::Float2{ 0.f, 0.f }, ozz::math::Float2{ 0.f, 0.f } }
        };
        ozz::math::Float3 centroid{ 0.f, 0.f, 0.f };
        ozz::math::Float3 world_normal{ 0.f, 0.f, 0.f };
        float area = 0.f;
        bool has_view_depth = false;
        float view_depth = std::numeric_limits<float>::infinity();
        float ray_distance = std::numeric_limits<float>::infinity();
        float screen_distance_sq = std::numeric_limits<float>::infinity();
    };

    SelectedTriangle selected_triangle_{};
    bool pending_pick_ = false;
    ImVec2 pending_pick_pos_{ 0.f, 0.f };

    enum class IkHandleId
    {
        None,
        LeftLeg,
        RightLeg,
        LeftArm,
        RightArm,
    };

    IkHandleId active_ik_handle_ = IkHandleId::None;
    bool ik_handle_dragging_ = false;
    ozz::math::Float3 ik_drag_plane_point_{ 0.f, 0.f, 0.f };
    ozz::math::Float3 ik_drag_plane_normal_{ 0.f, 1.f, 0.f };

    bool pick_ray_valid_ = false;
    ozz::math::Float3 pick_ray_origin_{ 0.f, 0.f, 0.f };
    ozz::math::Float3 pick_ray_direction_{ 0.f, 0.f, -1.f };
    float pick_ray_length_ = 100.f;

    struct TriangleDebugMesh
    {
        ozz::sample::Mesh mesh;
        std::vector<uint32_t> vertex_remap;
    };

    std::vector<std::vector<TriangleDebugMesh>> triangle_debug_meshes_;

    static bool IsBetterTrianglePick(const SelectedTriangle& candidate, const SelectedTriangle& current)
    {
        constexpr float depth_epsilon = 1e-5f;

        if (candidate.valid && !current.valid)
        {
            return true;
        }

        if (!candidate.valid)
        {
            return false;
        }

        const bool candidate_has_ray = std::isfinite(candidate.ray_distance);
        const bool current_has_ray = std::isfinite(current.ray_distance);

        if (candidate_has_ray && current_has_ray)
        {
            if (candidate.ray_distance + depth_epsilon < current.ray_distance)
            {
                return true;
            }
            if (current.ray_distance + depth_epsilon < candidate.ray_distance)
            {
                return false;
            }
        }
        else if (candidate_has_ray != current_has_ray)
        {
            return candidate_has_ray;
        }

        const bool candidate_has_depth = candidate.has_view_depth && std::isfinite(candidate.view_depth);
        const bool current_has_depth = current.has_view_depth && std::isfinite(current.view_depth);

        if (candidate_has_depth && current_has_depth)
        {
            if (candidate.view_depth + depth_epsilon < current.view_depth)
            {
                return true;
            }
            if (current.view_depth + depth_epsilon < candidate.view_depth)
            {
                return false;
            }
        }
        else if (candidate_has_depth != current_has_depth)
        {
            return candidate_has_depth;
        }

        return candidate.screen_distance_sq < current.screen_distance_sq;
    }

    // Cached lowercase joint names for lookup.
    std::vector<std::string> joint_names_lower_;

    struct RagdollJointState
    {
        ozz::math::Float3 translation = { 0.f, 0.f, 0.f };
        ozz::math::Float3 velocity = { 0.f, 0.f, 0.f };
        ozz::math::Quaternion rotation = ozz::math::Quaternion::identity();
        bool initialized = false;
    };

    std::vector<RagdollJointState> ragdoll_state_;
    ozz::vector<ozz::math::SoaTransform> ragdoll_target_locals_;
    bool ragdoll_reset_requested_ = false;
    bool ragdoll_was_enabled_ = false;
    float active_crouch_offset_ = 0.f;
    int pelvis_joint_ = -1;
    int spine_joint_ = -1;

    // IK and dynamic control state.
    bool limb_ik_initialized_ = false;
    bool leg_ik_available_ = false;
    bool arm_ik_available_ = false;
    LimbIkChain left_leg_chain_{};
    LimbIkChain right_leg_chain_{};
    LimbIkChain left_arm_chain_{};
    LimbIkChain right_arm_chain_{};

    void BuildTriangleDebugMeshes();
    void UpdateTriangleDebugMesh(const std::vector<ozz::math::Float3>& world_positions, TriangleDebugMesh& debug_mesh);

    LimbIkChain* GetChainForHandle(IkHandleId handle)
    {
        switch (handle)
        {
        case IkHandleId::LeftLeg:
            return &left_leg_chain_;
        case IkHandleId::RightLeg:
            return &right_leg_chain_;
        case IkHandleId::LeftArm:
            return &left_arm_chain_;
        case IkHandleId::RightArm:
            return &right_arm_chain_;
        default:
            return nullptr;
        }
    }

    const LimbIkChain* GetChainForHandle(IkHandleId handle) const
    {
        return const_cast<PlaybackSampleApplication*>(this)->GetChainForHandle(handle);
    }

    void ReleaseMeshTextures()
    {
        if (!mesh_textures_.empty())
        {
            for (GLuint texture : mesh_textures_)
            {
                if (texture != 0)
                {
                    glDeleteTextures(1, &texture);
                }
            }
        }
        mesh_textures_.clear();
        mesh_texture_paths_.clear();
    }

    void LoadMeshTextures(const char* mesh_path)
    {
        ReleaseMeshTextures();
        mesh_textures_.resize(meshes_.size(), 0);
        mesh_texture_paths_.assign(meshes_.size(), std::string());

        if (!mesh_path)
        {
            return;
        }

        const fs::path mesh_file = fs::absolute(fs::path(mesh_path));
        const fs::path mesh_dir = mesh_file.parent_path();
        fs::path override_root;
        if (OPTIONS_texture_root && OPTIONS_texture_root[0] != '\0')
        {
            override_root = fs::absolute(fs::path(std::string(OPTIONS_texture_root)));
        }

        for (size_t mesh_index = 0; mesh_index < meshes_.size(); ++mesh_index)
        {
            const ozz::sample::XRayMeshMetadata& metadata = meshes_[mesh_index].xray_metadata;
            if (metadata.texture_path.empty())
            {
                continue;
            }

            const fs::path relative = SanitizeTexturePath(metadata.texture_path);
            std::vector<fs::path> candidates = BuildTextureCandidates(relative, mesh_dir);
            if (!override_root.empty())
            {
                std::vector<fs::path> override_candidates = BuildTextureCandidates(relative, override_root);
                candidates.insert(candidates.end(), override_candidates.begin(), override_candidates.end());
            }

            GLuint texture = 0;
            std::string last_candidate;
            for (const fs::path& candidate : candidates)
            {
                const std::string abs_candidate = fs::absolute(candidate).string();
                last_candidate = abs_candidate;

                if (!fs::exists(candidate))
                {
                    continue;
                }

                texture = LoadTextureFromFile(candidate);
                if (texture != 0)
                {
                    ozz::log::Out() << "Loaded texture for mesh " << mesh_index << " from " << abs_candidate << std::endl;
                    mesh_texture_paths_[mesh_index] = abs_candidate;
                    break;
                }
            }

            if (texture == 0)
            {
                ozz::log::Out() << "Unable to load texture '" << metadata.texture_path << "' for mesh " << mesh_index;
                if (!last_candidate.empty())
                {
                    ozz::log::Out() << " (last candidate: " << last_candidate << ")";
                }
                ozz::log::Out() << std::endl;
                mesh_texture_paths_[mesh_index] = last_candidate;
            }

            mesh_textures_[mesh_index] = texture;
        }
    }

    void RefreshMeshDisplayState()
    {
        mesh_names_.clear();
        ui_state_.mesh_visibility.assign(meshes_.size(), 1);
        selected_triangle_ = SelectedTriangle{};

        mesh_names_.reserve(meshes_.size());
        const size_t mesh_count = meshes_.size();
        for (size_t mesh_index = 0; mesh_index < mesh_count; ++mesh_index)
        {
            const ozz::sample::Mesh& mesh = meshes_[mesh_index];
            const ozz::sample::XRayMeshMetadata& metadata = mesh.xray_metadata;

            std::string display_name;
            if (!metadata.texture_path.empty())
            {
                display_name = metadata.texture_path;
            }
            else if (!metadata.shader_name.empty())
            {
                display_name = metadata.shader_name;
            }

            if (!display_name.empty())
            {
                if (mesh_count > 1)
                {
                    display_name += "_" + std::to_string(mesh_index);
                }
            }
            else
            {
                display_name = BuildMeshDisplayName(mesh_label_, mesh_index, mesh_count);
            }

            mesh_names_.push_back(std::move(display_name));
        }
    }

    void SetCurrentAnimation(int index)
    {
        int resolved = index;
        if (animations_.empty())
        {
            resolved = kNoAnimationIndex;
        }
        else
        {
            const int max_index = static_cast<int>(animations_.size()) - 1;
            resolved = std::clamp(index, kNoAnimationIndex, max_index);
        }

        if (resolved == ui_state_.current_animation)
        {
            return;
        }

        ui_state_.current_animation = resolved;
        controller_.Reset();
        context_.Invalidate();
    }

    void DrawDearImGuiPanels()
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

            const ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_NoBackground;

            if (ImGui::Begin("##ViewerDockHost", nullptr, host_flags))
            {
                const ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;
                const ImGuiID dockspace_id = ImGui::GetID("ViewerDockSpace");
                ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dock_flags);

                static bool dock_built = false;
                if (!dock_built)
                {
                    dock_built = true;
                    ImGui::DockBuilderRemoveNode(dockspace_id);
                    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
                    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

                    ImGuiID dock_main_id = dockspace_id;
                    ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.32f, nullptr, &dock_main_id);
                    ImGuiID dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.23f, nullptr, &dock_main_id);
                    ImGuiID dock_right_bottom_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down, 0.42f, nullptr, &dock_right_id);

                    ImGui::DockBuilderDockWindow("Performance", dock_left_id);
                    ImGui::DockBuilderDockWindow("Animation", dock_right_id);
                    ImGui::DockBuilderDockWindow("Inverse Kinematics", dock_right_id);
                    ImGui::DockBuilderDockWindow("Rendering", dock_right_id);
                    ImGui::DockBuilderDockWindow("Metadata", dock_right_id);
                    ImGui::DockBuilderDockWindow("Triangle Inspector", dock_right_id);
                    ImGui::DockBuilderDockWindow("Logging", dock_right_bottom_id);
                    ImGui::DockBuilderDockWindow("Bone Transforms", dock_right_bottom_id);

                    ImGui::DockBuilderFinish(dockspace_id);
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
        }

        DrawPerformancePanel();
        DrawAnimationPanel();
        DrawRenderingPanel();
        DrawCharacterControlPanel();
        DrawBoneTransformsPanel();
        DrawMetadataPanel();
        DrawTriangleInspectorPanel();
    }

    void DrawPerformancePanel()
    {
        if (!ImGui::Begin("Performance"))
        {
            ImGui::End();
            return;
        }

        const auto draw_timing_section = [&](const char* label, const char* plot_label, ozz::sample::Record* record, bool show_fps)
        {
            if (!record)
            {
                return;
            }

            std::vector<float> samples = CollectRecordSamples(record);
            if (samples.empty())
            {
                return;
            }

            ozz::sample::Record::Statistics stats = record->GetStatistics();

            ImGui::SeparatorText(label);

            if (show_fps)
            {
                const float fps = stats.mean > 0.f ? 1000.f / stats.mean : 0.f;
                ImGui::Text("Average: %.2f ms  (%.0f fps)", stats.mean, fps);
            }
            else
            {
                ImGui::Text("Average: %.2f ms", stats.mean);
            }
            ImGui::Text("Latest: %.2f ms     Min: %.2f ms     Max: %.2f ms", stats.latest, stats.min, stats.max);

            const auto max_it = std::max_element(samples.begin(), samples.end());
            float y_max = max_it != samples.end() ? *max_it : stats.max;
            y_max = std::max(y_max, stats.max);
            if (!std::isfinite(y_max) || y_max <= 0.f)
            {
                y_max = 1.f;
            }

            ImGui::PlotLines(plot_label, samples.data(), static_cast<int>(samples.size()), 0, nullptr, 0.f, y_max * 1.1f, ImVec2(-1.0f, 72.0f));
        };

        draw_timing_section("Frame", "Frame time (ms)", GetFpsRecord(), true);
        draw_timing_section("Update", "Update (ms)", GetUpdateTimeRecord(), false);
        draw_timing_section("Render", "Render (ms)", GetRenderTimeRecord(), false);

        ImGui::End();
    }

    void DrawAnimationPanel()
    {
        if (!ImGui::Begin("Animation"))
        {
            ImGui::End();
            return;
        }

        if (using_bundle_)
        {
            ImGui::TextUnformatted("Bundle:");
            ImGui::SameLine();
            ImGui::TextUnformatted(bundle_source_path_.c_str());
            const char* source_label = nullptr;
            if (embedded_animation_loaded_)
                source_label = "Embedded payload";
            else if (!animations_.empty())
                source_label = OPTIONS_animation;
            else
                source_label = "None";
            ImGui::Text("Animation source: %s", source_label);
        }
        else
        {
            ImGui::Text("Animation file: %s", animations_.empty() ? "None" : OPTIONS_animation);
        }

        ImGui::Spacing();

        const int animation_count = static_cast<int>(animations_.size());
        const bool has_animations = animation_count > 0;

        if (ImGui::Button("Previous"))
        {
            if (has_animations)
            {
                int target = ui_state_.current_animation;
                if (target == kNoAnimationIndex)
                {
                    target = animation_count - 1;
                }
                else if (target > 0)
                {
                    --target;
                }
                else
                {
                    target = kNoAnimationIndex;
                }
                SetCurrentAnimation(target);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Next"))
        {
            if (has_animations)
            {
                int target = ui_state_.current_animation;
                if (target == kNoAnimationIndex)
                {
                    target = 0;
                }
                else if (target < animation_count - 1)
                {
                    ++target;
                }
                else
                {
                    target = kNoAnimationIndex;
                }
                SetCurrentAnimation(target);
            }
        }

        if (!has_animations)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("No animations loaded. Displaying bind pose.");
        }
        else
        {
            if (ImGui::RadioButton("Bind pose", ui_state_.current_animation == kNoAnimationIndex))
            {
                SetCurrentAnimation(kNoAnimationIndex);
            }

            if (ImGui::BeginListBox("##AnimationList"))
            {
                for (int i = 0; i < animation_count; ++i)
                {
                    const std::string name = GetAnimationName(i);
                    const bool selected = ui_state_.current_animation == i;
                    const std::string display_name = name.empty() ? ("Animation " + std::to_string(i + 1)) : name;
                    const std::string label = display_name + "##anim" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        SetCurrentAnimation(i);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndListBox();
            }
        }

        ImGui::Separator();

        if (HasAnimationSelected())
        {
            const int index = ui_state_.current_animation;
            const ozz::animation::Animation& animation = animations_[index];
            const MotionMetadataData* metadata = animation_metadata_.size() > static_cast<size_t>(index) ? &animation_metadata_[index] : nullptr;

            DrawPlaybackControls(animation);

            if (metadata)
            {
                ImGui::Spacing();
                ImGui::Text("Flags: 0x%08X", metadata->flags);
                ImGui::Text("Motion ID: %u", metadata->motion_id);
                ImGui::Text("Speed: %.3f  Power: %.3f", metadata->speed, metadata->power);
                ImGui::Text("Accrue: %.3f  Falloff: %.3f", metadata->accrue, metadata->falloff);
                ImGui::Text("Frame Count: %u", metadata->frame_count);
                ImGui::Text("Bone Tracks: %zu", metadata->bone_motions.size());
                if (!metadata->marks.empty())
                {
                    ImGui::SeparatorText("Marks");
                    for (const MotionMarkData& mark : metadata->marks)
                    {
                        std::ostringstream mark_stream;
                        mark_stream.imbue(std::locale::classic());
                        mark_stream << mark.name << " [";
                        for (size_t idx = 0; idx < mark.intervals.size(); ++idx)
                        {
                            mark_stream << std::fixed << std::setprecision(3) << mark.intervals[idx].first << " - " << mark.intervals[idx].second;
                            if (idx + 1 < mark.intervals.size())
                            {
                                mark_stream << ", ";
                            }
                        }
                        mark_stream << "]";
                        ImGui::BulletText("%s", mark_stream.str().c_str());
                    }
                }

                if (!metadata->bone_motions.empty() && ImGui::CollapsingHeader("Bone Motions", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    const auto joint_names = skeleton_.joint_names();
                    const int joint_count = skeleton_.num_joints();

                    if (ImGui::BeginTable("BoneMotionTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Bone");
                        ImGui::TableSetupColumn("Flags");
                        ImGui::TableSetupColumn("Rot Keys");
                        ImGui::TableSetupColumn("Trans Keys");
                        ImGui::TableSetupColumn("Trans Format");
                        ImGui::TableSetupColumn("CRC (rot/trans)");
                        ImGui::TableSetupColumn("Translation Range");
                        ImGui::TableHeadersRow();

                        for (const BoneMotionData& bone : metadata->bone_motions)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            const bool valid_index = static_cast<int>(bone.bone_id) < joint_count;
                            const char* joint_name = valid_index ? joint_names[bone.bone_id] : "<invalid>";
                            ImGui::Text("%u (%s)", bone.bone_id, joint_name ? joint_name : "");

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("0x%02X", bone.flags);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%u", bone.rotation_key_count);

                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%u", bone.translation_key_count);

                            ImGui::TableSetColumnIndex(4);
                            ImGui::TextUnformatted(DescribeTranslationFormat(bone.translation_format));

                            ImGui::TableSetColumnIndex(5);
                            ImGui::Text("0x%08X / 0x%08X", bone.rotation_crc, bone.translation_crc);

                            ImGui::TableSetColumnIndex(6);
                            const std::array<float, 3>& size = bone.translation_size;
                            const std::array<float, 3>& init = bone.translation_init;
                            ImGui::Text("size=(%.3f, %.3f, %.3f)\ninit=(%.3f, %.3f, %.3f)",
                                size[0], size[1], size[2], init[0], init[1], init[2]);
                        }

                        ImGui::EndTable();
                    }
                }
            }
        }
        else
        {
            ImGui::TextUnformatted("Bind pose (no animation).");
        }

        ImGui::End();
    }

    void DrawPlaybackControls(const ozz::animation::Animation& animation)
    {
        const float duration = animation.duration();

        bool playing = controller_.playing();
        if (ImGui::Checkbox("Play", &playing))
        {
            controller_.set_play(playing);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
        {
            controller_.Reset();
            controller_.set_play(false);
        }

        bool loop = controller_.loop();
        if (ImGui::Checkbox("Loop", &loop))
        {
            controller_.set_loop(loop);
        }

        float speed = controller_.playback_speed();
        if (ImGui::SliderFloat("Speed", &speed, -4.f, 4.f, "%.2fx"))
        {
            controller_.set_playback_speed(speed);
        }

        float ratio = controller_.time_ratio();
        if (ImGui::SliderFloat("Time", &ratio, 0.f, 1.f, "%.3f"))
        {
            controller_.set_time_ratio(ratio);
        }

        ImGui::Text("Duration: %.3f s", duration);
        ImGui::Text("Current time: %.3f s", duration * controller_.time_ratio());
    }

    void DrawRenderingPanel()
    {
        if (!ImGui::Begin("Rendering"))
        {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Draw skeleton", &ui_state_.draw_skeleton);
        if (!meshes_.empty())
        {
            ImGui::Checkbox("Draw mesh", &ui_state_.draw_mesh);

            ImGui::Checkbox("Show triangles", &ui_state_.renderer_options.triangles);
            if (ImGui::Checkbox("Random triangle colors", &ui_state_.random_triangle_colors))
            {
                BuildTriangleDebugMeshes();
            }
            if (ImGui::Checkbox("Show texture", &ui_state_.renderer_options.texture))
            {
                if (ui_state_.renderer_options.texture)
                {
                    ozz::log::Out() << "Texture rendering enabled." << std::endl;
                    for (size_t mesh_index = 0; mesh_index < meshes_.size(); ++mesh_index)
                    {
                        const auto& metadata = meshes_[mesh_index].xray_metadata;
                        const std::string resolved = mesh_index < mesh_texture_paths_.size() ? mesh_texture_paths_[mesh_index] : std::string();
                        const bool loaded = mesh_index < mesh_textures_.size() && mesh_textures_[mesh_index] != 0;
                        ozz::log::Out() << "mesh[" << mesh_index << "] metadata='" << metadata.texture_path << "' resolved='"
                                        << (resolved.empty() ? "" : resolved) << "' loaded=" << (loaded ? "yes" : "no") << std::endl;
                    }
                }
                else
                {
                    ozz::log::Out() << "Texture rendering disabled." << std::endl;
                }
            }
            ImGui::Checkbox("Show vertices", &ui_state_.renderer_options.vertices);
            ImGui::Checkbox("Show normals", &ui_state_.renderer_options.normals);
            ImGui::Checkbox("Show tangents", &ui_state_.renderer_options.tangents);
            ImGui::Checkbox("Show binormals", &ui_state_.renderer_options.binormals);
            ImGui::Checkbox("Show colors", &ui_state_.renderer_options.colors);
            ImGui::Checkbox("Wireframe", &ui_state_.renderer_options.wireframe);
            ImGui::Checkbox("Skip skinning", &ui_state_.renderer_options.skip_skinning);

            if (mesh_names_.size() != meshes_.size() || ui_state_.mesh_visibility.size() != meshes_.size())
            {
                RefreshMeshDisplayState();
            }

            if (ImGui::CollapsingHeader("Mesh Visibility", ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (size_t mesh_index = 0; mesh_index < meshes_.size(); ++mesh_index)
                {
                    bool visible = ui_state_.mesh_visibility[mesh_index] != 0;
                    if (ImGui::Checkbox(mesh_names_[mesh_index].c_str(), &visible))
                    {
                        ui_state_.mesh_visibility[mesh_index] = visible ? 1 : 0;
                    }
                }
            }
        }

        ImGui::End();
    }

    void DrawCharacterControlPanel()
    {
        if (!ImGui::Begin("Character Controls"))
        {
            ImGui::End();
            return;
        }

        if (!limb_ik_initialized_)
        {
            ImGui::TextUnformatted("IK chains initialize after a skeleton loads.");
        }
        else
        {
            ImGui::SeparatorText("Leg IK");
            if (!leg_ik_available_)
            {
                ImGui::TextUnformatted("No leg chains detected for this skeleton.");
            }
            else
            {
                ImGui::Checkbox("Enable leg IK", &ui_state_.foot_ik_enabled);
                if (ui_state_.foot_ik_enabled)
                {
                    ui_state_.foot_ik_weight = std::clamp(ui_state_.foot_ik_weight, 0.f, 1.f);
                    const float soften_max = 0.999f;
                    ui_state_.foot_ik_soften = std::clamp(ui_state_.foot_ik_soften, 0.f, soften_max);

                    ImGui::SliderFloat("Ground height", &ui_state_.foot_ik_ground_height, -2.f, 2.f);
                    ImGui::SliderFloat("IK weight", &ui_state_.foot_ik_weight, 0.f, 1.f);
                    ImGui::SliderFloat("IK soften", &ui_state_.foot_ik_soften, 0.f, soften_max);
                    ImGui::SliderFloat("Twist (deg)", &ui_state_.foot_ik_twist_angle_deg, -180.f, 180.f);

                    if (ImGui::CollapsingHeader("Left leg", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        DrawLimbIkChainPanel(left_leg_chain_, -1);
                    }
                    if (ImGui::CollapsingHeader("Right leg", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        DrawLimbIkChainPanel(right_leg_chain_, -1);
                    }
                }
            }

            ImGui::SeparatorText("Arm IK");
            if (!arm_ik_available_)
            {
                ImGui::TextUnformatted("No arm chains detected for this skeleton.");
            }
            else
            {
                ImGui::Checkbox("Enable arm IK", &ui_state_.arm_ik_enabled);
                if (ui_state_.arm_ik_enabled)
                {
                    ui_state_.arm_ik_weight = std::clamp(ui_state_.arm_ik_weight, 0.f, 1.f);
                    const float arm_soften_max = 0.999f;
                    ui_state_.arm_ik_soften = std::clamp(ui_state_.arm_ik_soften, 0.f, arm_soften_max);

                    ImGui::SliderFloat("IK weight", &ui_state_.arm_ik_weight, 0.f, 1.f);
                    ImGui::SliderFloat("IK soften", &ui_state_.arm_ik_soften, 0.f, arm_soften_max);
                    ImGui::SliderFloat("Twist (deg)", &ui_state_.arm_ik_twist_angle_deg, -180.f, 180.f);

                    if (ImGui::CollapsingHeader("Left arm", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        DrawLimbIkChainPanel(left_arm_chain_, 0);
                    }
                    if (ImGui::CollapsingHeader("Right arm", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        DrawLimbIkChainPanel(right_arm_chain_, 1);
                    }
                }
            }

            ImGui::SeparatorText("Active Ragdoll");
            ImGui::Checkbox("Enable ragdoll", &ui_state_.ragdoll_enabled);
            if (ui_state_.ragdoll_enabled)
            {
                ui_state_.ragdoll_stiffness = std::max(ui_state_.ragdoll_stiffness, 0.f);
                ui_state_.ragdoll_damping = std::clamp(ui_state_.ragdoll_damping, 0.f, 1.f);
                ImGui::SliderFloat("Stiffness", &ui_state_.ragdoll_stiffness, 0.f, 50.f);
                ImGui::SliderFloat("Damping", &ui_state_.ragdoll_damping, 0.f, 1.f);
                ImGui::SliderFloat("Gravity (m/s^2)", &ui_state_.ragdoll_gravity, -30.f, 10.f);
                if (ImGui::Button("Reset ragdoll pose"))
                {
                    ragdoll_reset_requested_ = true;
                }
            }

            ImGui::SeparatorText("Procedural Crouch");
            ImGui::Checkbox("Enable crouch", &ui_state_.crouch_enabled);
            if (ui_state_.crouch_enabled)
            {
                ui_state_.crouch_amount = std::clamp(ui_state_.crouch_amount, 0.f, 1.f);
                ui_state_.crouch_height = std::max(ui_state_.crouch_height, 0.f);
                ImGui::SliderFloat("Crouch amount", &ui_state_.crouch_amount, 0.f, 1.f);
                ImGui::SliderFloat("Max crouch height", &ui_state_.crouch_height, 0.f, 1.f);
                ImGui::SliderFloat("Spine pitch (deg)", &ui_state_.crouch_spine_pitch_deg, -45.f, 45.f);
                ImGui::Checkbox("Lower arm targets", &ui_state_.crouch_affects_arms);
            }
        }

        ImGui::End();
    }

    void DrawLimbIkChainPanel(LimbIkChain& chain, int arm_handle_index)
    {
        if (!chain.Valid())
        {
            ImGui::TextUnformatted("Chain unavailable for this skeleton.");
            return;
        }

        ImGui::PushID(chain.label.c_str());
        ImGui::Checkbox("Enabled", &chain.enabled);
        ImGui::Text("Start: %s", chain.start_name.c_str());
        ImGui::Text("Mid: %s", chain.mid_name.c_str());
        ImGui::Text("End: %s", chain.end_name.c_str());
        ImGui::Text("Status: %s", chain.reached ? "target reached" : "solving");

        IkOffsetUi offset_ui = ToUiOffset(chain.target_offset);

        if (chain.role == LimbIkChain::Role::Leg)
        {
            bool offset_changed = false;

            float vertical = offset_ui.vertical;
            if (ImGui::SliderFloat("Vertical offset", &vertical, -0.5f, 0.5f))
            {
                offset_ui.vertical = vertical;
                offset_changed = true;
            }

            float forward = offset_ui.forward;
            if (ImGui::SliderFloat("Forward offset", &forward, -0.5f, 0.5f))
            {
                offset_ui.forward = forward;
                offset_changed = true;
            }

            float lateral = offset_ui.lateral;
            if (ImGui::SliderFloat("Lateral offset", &lateral, -0.5f, 0.5f))
            {
                offset_ui.lateral = lateral;
                offset_changed = true;
            }

            if (offset_changed)
            {
                chain.target_offset = FromUiOffset(offset_ui);
            }
        }
        else
        {
            if (arm_handle_index >= 0 && arm_handle_index < 2)
            {
                ViewerUiState::ArmHandle& handle = ui_state_.arm_handles[arm_handle_index];
                handle.forward_offset = offset_ui.forward;
                handle.lateral_offset = offset_ui.lateral;
                handle.vertical_offset = offset_ui.vertical;

                bool offset_changed = false;
                if (ImGui::DragFloat("Forward offset", &handle.forward_offset, 0.01f, -1.0f, 1.0f))
                {
                    offset_ui.forward = handle.forward_offset;
                    offset_changed = true;
                }
                if (ImGui::DragFloat("Lateral offset", &handle.lateral_offset, 0.01f, -1.0f, 1.0f))
                {
                    offset_ui.lateral = handle.lateral_offset;
                    offset_changed = true;
                }
                if (ImGui::DragFloat("Vertical offset", &handle.vertical_offset, 0.01f, -1.0f, 1.0f))
                {
                    offset_ui.vertical = handle.vertical_offset;
                    offset_changed = true;
                }

                if (offset_changed)
                {
                    chain.target_offset = FromUiOffset(offset_ui);
                }
            }
            else
            {
                ImGui::TextUnformatted("Arm control unavailable.");
            }
        }

        chain.target_offset = FromUiOffset(offset_ui);

        ImGui::PopID();
    }

    void DrawBoneTransformsPanel()
    {
        if (!ImGui::Begin("Bone Transforms"))
        {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Show bone transforms", &ui_state_.show_bone_debug);

        const int max_joints = skeleton_.num_joints();
        if (max_joints > 0)
        {
            ui_state_.bone_display_limit = std::clamp(ui_state_.bone_display_limit, 1, max_joints);
            ImGui::SliderInt("Bones shown", &ui_state_.bone_display_limit, 1, max_joints);
        }
        else
        {
            ui_state_.bone_display_limit = 0;
        }

        if (ui_state_.show_bone_debug && max_joints > 0)
        {
            const auto joint_names = skeleton_.joint_names();
            const int display_count = std::min(ui_state_.bone_display_limit, max_joints);

            if (HasAnimationSelected())
            {
                const float duration = animations_[ui_state_.current_animation].duration();
                const float ratio = controller_.time_ratio();
                const float time = duration * ratio;
                ImGui::Text("Time %.3fs / %.3fs (ratio %.3f)", time, duration, ratio);
            }
            else
            {
                ImGui::TextUnformatted("Bind pose (no animation)");
            }

            if (ImGui::BeginTable("BoneTransformsTable", 8, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Bone");
                ImGui::TableSetupColumn("Pos X");
                ImGui::TableSetupColumn("Pos Y");
                ImGui::TableSetupColumn("Pos Z");
                ImGui::TableSetupColumn("Rot W");
                ImGui::TableSetupColumn("Rot X");
                ImGui::TableSetupColumn("Rot Y");
                ImGui::TableSetupColumn("Rot Z");
                ImGui::TableHeadersRow();

                for (int joint = 0; joint < display_count; ++joint)
                {
                    const ozz::math::Float4x4& matrix = models_[joint];
                    const float tx = ozz::math::GetX(matrix.cols[3]);
                    const float ty = ozz::math::GetY(matrix.cols[3]);
                    const float tz = ozz::math::GetZ(matrix.cols[3]);

                    const ozz::math::SimdFloat4 quat_simd = ozz::math::ToQuaternion(matrix);
                    float quat[4];
                    ozz::math::StorePtrU(quat_simd, quat);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(joint_names[joint]);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f", tx);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.3f", ty);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.3f", tz);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%.3f", quat[3]);
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%.3f", quat[0]);
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%.3f", quat[1]);
                    ImGui::TableSetColumnIndex(7);
                    ImGui::Text("%.3f", quat[2]);
                }

                ImGui::EndTable();
            }

            if (display_count < max_joints)
            {
                ImGui::Text("Showing %d of %d bones", display_count, max_joints);
            }
        }

        ImGui::End();
    }

    void DrawMetadataPanel()
    {
        if (!ImGui::Begin("Metadata"))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("Skeleton: %s", skeleton_label_.c_str());
        ImGui::Text("Joints: %d", skeleton_.num_joints());
        if (!meshes_.empty())
        {
            ImGui::Separator();
            ImGui::Text("Mesh file: %s", mesh_label_.c_str());
            ImGui::Text("Loaded meshes: %zu", meshes_.size());
        }

        ImGui::End();
    }

    bool EvaluateTrianglePick(size_t mesh_index, const ozz::sample::Mesh& mesh, const std::vector<ozz::math::Float3>& world_positions,
        const std::vector<VertexReference>& vertex_refs, [[maybe_unused]] float pick_x, [[maybe_unused]] float pick_y, [[maybe_unused]] float viewport_width,
        [[maybe_unused]] float viewport_height, const ozz::math::Float4x4*, const ozz::math::Float3* ray_origin, const ozz::math::Float3* ray_direction,
        SelectedTriangle& out_triangle) const
    {
        if (world_positions.empty() || !ray_origin || !ray_direction)
        {
            return false;
        }

        const auto& indices = mesh.triangle_indices;
        if (indices.empty())
        {
            return false;
        }

        const ozz::math::Float3 origin = *ray_origin;
        const ozz::math::Float3 direction = *ray_direction;
        const float ray_length = pick_ray_length_;

        auto Cross = [](const ozz::math::Float3& a, const ozz::math::Float3& b) -> ozz::math::Float3
        {
            return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        };
        auto Dot = [](const ozz::math::Float3& a, const ozz::math::Float3& b) -> float
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        };

        bool found = false;
        SelectedTriangle best_triangle;
        const size_t triangle_count = indices.size() / 3;
        const float epsilon = 1e-6f;

        for (size_t tri = 0; tri < triangle_count; ++tri)
        {
            const uint32_t idx0 = indices[tri * 3 + 0];
            const uint32_t idx1 = indices[tri * 3 + 1];
            const uint32_t idx2 = indices[tri * 3 + 2];

            if (idx0 >= world_positions.size() || idx1 >= world_positions.size() || idx2 >= world_positions.size())
            {
                continue;
            }

            const ozz::math::Float3& p0 = world_positions[idx0];
            const ozz::math::Float3& p1 = world_positions[idx1];
            const ozz::math::Float3& p2 = world_positions[idx2];

            const ozz::math::Float3 edge0{ p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
            const ozz::math::Float3 edge1{ p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };

            const ozz::math::Float3 pvec = Cross(direction, edge1);
            const float det = Dot(edge0, pvec);
            if (std::fabs(det) <= epsilon)
            {
                continue;
            }

            const float inv_det = 1.f / det;
            const ozz::math::Float3 s{ origin.x - p0.x, origin.y - p0.y, origin.z - p0.z };
            const float u = Dot(s, pvec) * inv_det;
            if (u < 0.f || u > 1.f)
            {
                continue;
            }

            const ozz::math::Float3 qvec = Cross(s, edge0);
            const float v = Dot(direction, qvec) * inv_det;
            if (v < 0.f || u + v > 1.f)
            {
                continue;
            }

            const float t = Dot(edge1, qvec) * inv_det;
            if (t < epsilon || t > ray_length)
            {
                continue;
            }

            SelectedTriangle candidate;
            candidate.valid = true;
            candidate.mesh_index = mesh_index;
            candidate.triangle_index = static_cast<uint32_t>(tri);
            candidate.vertex_indices = { idx0, idx1, idx2 };
            candidate.world_positions = { p0, p1, p2 };
            candidate.centroid = { (p0.x + p1.x + p2.x) * (1.f / 3.f), (p0.y + p1.y + p2.y) * (1.f / 3.f), (p0.z + p1.z + p2.z) * (1.f / 3.f) };
            candidate.screen_distance_sq = 0.f;
            candidate.ray_distance = t;

            const ozz::math::Float3 normal_raw = Cross(edge0, edge1);
            const float normal_length_sq = Dot(normal_raw, normal_raw);
            if (normal_length_sq > epsilon)
            {
                const float normal_length = std::sqrt(normal_length_sq);
                const float inv_normal_length = 1.f / normal_length;
                candidate.world_normal = { normal_raw.x * inv_normal_length, normal_raw.y * inv_normal_length, normal_raw.z * inv_normal_length };
                candidate.area = 0.5f * normal_length;
            }
            else
            {
                candidate.world_normal = { 0.f, 0.f, 0.f };
                candidate.area = 0.f;
            }

            candidate.has_view_depth = false;
            candidate.view_depth = t;

            for (size_t corner = 0; corner < 3; ++corner)
            {
                const uint32_t vertex_index = candidate.vertex_indices[corner];
                candidate.vertex_refs[corner] = (vertex_index < vertex_refs.size()) ? vertex_refs[vertex_index] : VertexReference{};

                ozz::math::Float2 uv{ 0.f, 0.f };
                if (vertex_index < vertex_refs.size())
                {
                    const VertexReference& ref = vertex_refs[vertex_index];
                    if (ref.part_index < mesh.parts.size())
                    {
                        const ozz::sample::Mesh::Part& ref_part = mesh.parts[ref.part_index];
                        if (!ref_part.uvs.empty() &&
                            ref.local_index >= 0 &&
                            static_cast<size_t>(ref.local_index * ozz::sample::Mesh::Part::kUVsCpnts + 1) < ref_part.uvs.size())
                        {
                            const float* uv_ptr = &ref_part.uvs[ref.local_index * ozz::sample::Mesh::Part::kUVsCpnts];
                            uv = { uv_ptr[0], uv_ptr[1] };
                        }
                    }
                }
                candidate.uvs[corner] = uv;
            }

            if (!found || IsBetterTrianglePick(candidate, best_triangle))
            {
                best_triangle = candidate;
                found = true;
            }
        }

        if (found)
        {
            out_triangle = best_triangle;
        }
        return found;
    }

    void DrawTriangleInspectorPanel()
    {
        if (!ImGui::Begin("Triangle Inspector"))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("Left-click a triangle in the viewport to inspect it.");
        ImGui::Separator();

        if (!selected_triangle_.valid)
        {
            ImGui::TextUnformatted("No triangle selected.");
            ImGui::End();
            return;
        }

        if (ImGui::Button("Clear selection"))
        {
            selected_triangle_ = SelectedTriangle{};
            ImGui::End();
            return;
        }

        const size_t mesh_index = selected_triangle_.mesh_index;
        const char* mesh_label = (mesh_index < mesh_names_.size()) ? mesh_names_[mesh_index].c_str() : "(mesh)";
        ImGui::Text("Mesh: %s (index %zu)", mesh_label, mesh_index);
        ImGui::Text("Triangle index: %u", selected_triangle_.triangle_index);
        ImGui::Text("Area: %.6f", selected_triangle_.area);
        ImGui::Text("Centroid: (%.4f, %.4f, %.4f)", selected_triangle_.centroid.x, selected_triangle_.centroid.y, selected_triangle_.centroid.z);
        ImGui::Text("World normal: (%.4f, %.4f, %.4f)", selected_triangle_.world_normal.x, selected_triangle_.world_normal.y, selected_triangle_.world_normal.z);

        ImGui::Separator();
        ImGui::TextUnformatted("Vertices:");
        const auto joint_names_span = skeleton_.joint_names();
        const int joint_count = skeleton_.num_joints();
        const ozz::sample::Mesh* mesh_ptr = mesh_index < meshes_.size() ? &meshes_[mesh_index] : nullptr;
        for (size_t i = 0; i < selected_triangle_.world_positions.size(); ++i)
        {
            const ozz::math::Float3& pos = selected_triangle_.world_positions[i];
            const ozz::math::Float2& uv = selected_triangle_.uvs[i];
            const uint32_t remapped_index = selected_triangle_.vertex_indices[i];
            const VertexReference& vref = selected_triangle_.vertex_refs[i];
            constexpr uint32_t kInvalidVertexIndex = std::numeric_limits<uint32_t>::max();
            const uint32_t original_index = vref.original_index;
            if (original_index != kInvalidVertexIndex)
            {
                ImGui::Text("%zu: remapped %u, original %u, world (%.4f, %.4f, %.4f), uv (%.4f, %.4f)", i, remapped_index, original_index, pos.x, pos.y, pos.z, uv.x,
                            uv.y);
            }
            else
            {
                ImGui::Text("%zu: remapped %u, original N/A, world (%.4f, %.4f, %.4f), uv (%.4f, %.4f)", i, remapped_index, pos.x, pos.y, pos.z, uv.x, uv.y);
            }
            ImGui::Indent();
            ImGui::Text("Part %zu, local %d", vref.part_index, vref.local_index);

            if (mesh_ptr && vref.part_index < mesh_ptr->parts.size() && vref.local_index >= 0)
            {
                const ozz::sample::Mesh::Part& part = mesh_ptr->parts[vref.part_index];
                const int influences = std::max(1, part.influences_count());
                const size_t vertex_local = static_cast<size_t>(vref.local_index);

                if (influences > 0 && vertex_local < part.positions.size() / ozz::sample::Mesh::Part::kPositionsCpnts)
                {
                    std::vector<float> weight_values(static_cast<size_t>(influences), 0.f);
                    std::vector<uint16_t> palette_indices(static_cast<size_t>(influences), uint16_t{ 0 });

                    float accumulated = 0.f;
                    for (int influence = 0; influence < influences; ++influence)
                    {
                        float weight = 1.f;
                        if (influences > 1)
                        {
                            if (influence < influences - 1)
                            {
                                const size_t weight_index = vertex_local * static_cast<size_t>(influences - 1) + static_cast<size_t>(influence);
                                if (weight_index < part.joint_weights.size())
                                {
                                    weight = part.joint_weights[weight_index];
                                }
                                else
                                {
                                    weight = 0.f;
                                }
                                accumulated += weight;
                            }
                            else
                            {
                                weight = std::max(0.f, 1.f - accumulated);
                            }
                        }

                        const size_t joint_offset = vertex_local * static_cast<size_t>(influences) + static_cast<size_t>(influence);
                        const uint16_t palette_index = joint_offset < part.joint_indices.size() ? part.joint_indices[joint_offset] : uint16_t{ 0 };

                        weight_values[static_cast<size_t>(influence)] = weight;
                        palette_indices[static_cast<size_t>(influence)] = palette_index;
                    }

                    for (size_t influence = 0; influence < weight_values.size(); ++influence)
                    {
                        const uint16_t palette_index = palette_indices[influence];
                        int skeleton_joint = -1;
                        if (palette_index < mesh_ptr->joint_remaps.size())
                        {
                            skeleton_joint = mesh_ptr->joint_remaps[palette_index];
                        }

                        const char* joint_name = "(unknown)";
                        if (skeleton_joint >= 0 && skeleton_joint < joint_count)
                        {
                            joint_name = joint_names_span[static_cast<size_t>(skeleton_joint)];
                        }

                        const float weight = weight_values[influence];
                        ImGui::BulletText("Influence %zu: palette %u, joint %d (%s), weight %.4f", influence, palette_index, skeleton_joint, joint_name, weight);
                    }
                }
                else
                {
                    ImGui::BulletText("No skinning data available for this vertex.");
                }
            }
            else
            {
                if (!mesh_ptr)
                {
                    ImGui::BulletText("Mesh data unavailable to resolve this vertex.");
                }
                else
                {
                    ImGui::BulletText("Invalid vertex reference (part %zu, local %d).", vref.part_index, vref.local_index);
                }
            }
            ImGui::Unindent();
        }

        ImGui::End();
    }

    bool ComputeBindPoseModelMatrices()
    {
        if (skeleton_.num_joints() == 0)
        {
            models_.clear();
            bind_pose_models_.clear();
            return true;
        }

        const auto rest_poses = skeleton_.joint_rest_poses();
        if (rest_poses.size() != locals_.size())
        {
            locals_.resize(rest_poses.size());
        }
        for (size_t i = 0; i < rest_poses.size(); ++i)
        {
            locals_[i] = rest_poses[i];
        }

        if (models_.size() != static_cast<size_t>(skeleton_.num_joints()))
        {
            models_.resize(skeleton_.num_joints());
        }

        ozz::animation::LocalToModelJob ltm_job;
        ltm_job.skeleton = &skeleton_;
        ltm_job.input = make_span(locals_);
        ltm_job.output = make_span(models_);
        if (!ltm_job.Run())
        {
            ozz::log::Err() << "Failed to build bind-pose model matrices." << std::endl;
            return false;
        }
        bind_pose_models_ = models_;
        return true;
    }

    static std::string ToLowerString(std::string_view value)
    {
        std::string lowered;
        lowered.reserve(value.size());
        for (char ch : value)
        {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        return lowered;
    }

    int FindJointIndexByNames(std::initializer_list<const char*> candidates) const
    {
        if (joint_names_lower_.empty())
        {
            return -1;
        }
        for (const char* candidate : candidates)
        {
            if (!candidate)
            {
                continue;
            }
            const std::string candidate_lower = ToLowerString(candidate);
            for (size_t index = 0; index < joint_names_lower_.size(); ++index)
            {
                if (joint_names_lower_[index] == candidate_lower)
                {
                    return static_cast<int>(index);
                }
            }
        }
        return -1;
    }

    void InitializeLimbIkChains()
    {
        const bool previously_initialized = limb_ik_initialized_;
        const bool previous_left_leg_enabled = left_leg_chain_.enabled;
        const bool previous_right_leg_enabled = right_leg_chain_.enabled;
        bool previous_left_arm_enabled = left_arm_chain_.enabled;
        bool previous_right_arm_enabled = right_arm_chain_.enabled;

        left_leg_chain_ = LimbIkChain{};
        right_leg_chain_ = LimbIkChain{};
        left_arm_chain_ = LimbIkChain{};
        right_arm_chain_ = LimbIkChain{};

        joint_names_lower_.clear();
        const int joint_count = skeleton_.num_joints();
        joint_names_lower_.reserve(joint_count);
        const auto joint_names = skeleton_.joint_names();
        for (int joint = 0; joint < joint_count; ++joint)
        {
            joint_names_lower_.push_back(ToLowerString(joint_names[joint]));
        }

        pelvis_joint_ = FindJointIndexByNames({ "bip01_pelvis", "pelvis", "hip", "hips" });
        spine_joint_ = FindJointIndexByNames({ "bip01_spine", "spine", "spine1" });

        auto resolve_axis = [&](LimbIkChain& chain)
        {
            chain.mid_axis = ozz::math::simd_float4::z_axis();
            if (chain.Valid() && chain.mid >= 0 && static_cast<size_t>(chain.mid) < bind_pose_models_.size())
            {
                ozz::math::SimdFloat4 axis_candidate = bind_pose_models_[chain.mid].cols[2];
                const ozz::math::SimdFloat4 axis_len_sq = ozz::math::Length3Sqr(axis_candidate);
                const ozz::math::SimdFloat4 min_len = ozz::math::simd_float4::Load1(1e-6f);
                if (ozz::math::AreAllTrue1(ozz::math::CmpGt(axis_len_sq, min_len)))
                {
                    chain.mid_axis = ozz::math::Normalize3(axis_candidate);
                }
            }
        };

        auto populate_chain = [&](LimbIkChain& chain, LimbIkChain::Role role, std::initializer_list<const char*> start_candidates,
                                   std::initializer_list<const char*> mid_candidates, std::initializer_list<const char*> end_candidates)
        {
            chain.role = role;
            chain.start = FindJointIndexByNames(start_candidates);
            chain.mid = FindJointIndexByNames(mid_candidates);
            chain.end = FindJointIndexByNames(end_candidates);
            chain.start_name = chain.start >= 0 ? joint_names[chain.start] : std::string();
            chain.mid_name = chain.mid >= 0 ? joint_names[chain.mid] : std::string();
            chain.end_name = chain.end >= 0 ? joint_names[chain.end] : std::string();
            chain.target_offset = { 0.f, 0.f, 0.f };
            chain.reached = false;
            resolve_axis(chain);
        };

        populate_chain(left_leg_chain_, LimbIkChain::Role::Leg,
            { "bip01_l_thigh", "l_thigh", "left_thigh" },
            { "bip01_l_calf", "l_calf", "left_calf", "bip01_l_knee" },
            { "bip01_l_foot", "l_foot", "left_foot", "bip01_l_ankle" });
        populate_chain(right_leg_chain_, LimbIkChain::Role::Leg,
            { "bip01_r_thigh", "r_thigh", "right_thigh" },
            { "bip01_r_calf", "r_calf", "right_calf", "bip01_r_knee" },
            { "bip01_r_foot", "r_foot", "right_foot", "bip01_r_ankle" });

        populate_chain(left_arm_chain_, LimbIkChain::Role::Arm,
            { "bip01_l_upperarm", "bip01_l_shoulder", "l_upperarm", "left_shoulder" },
            { "bip01_l_forearm", "l_forearm", "left_forearm", "bip01_l_elbow" },
            { "bip01_l_hand", "l_hand", "left_hand", "bip01_l_wrist" });
        populate_chain(right_arm_chain_, LimbIkChain::Role::Arm,
            { "bip01_r_upperarm", "bip01_r_shoulder", "r_upperarm", "right_shoulder" },
            { "bip01_r_forearm", "r_forearm", "right_forearm", "bip01_r_elbow" },
            { "bip01_r_hand", "r_hand", "right_hand", "bip01_r_wrist" });

        if (left_arm_chain_.Valid() && right_arm_chain_.Valid() && !bind_pose_models_.empty())
        {
            const float left_x = ozz::math::GetX(bind_pose_models_[left_arm_chain_.end].cols[3]);
            const float right_x = ozz::math::GetX(bind_pose_models_[right_arm_chain_.end].cols[3]);
            if (left_x > right_x)
            {
                std::swap(left_arm_chain_, right_arm_chain_);
                std::swap(previous_left_arm_enabled, previous_right_arm_enabled);
                std::swap(ui_state_.arm_handles[0], ui_state_.arm_handles[1]);
            }
        }

        leg_ik_available_ = left_leg_chain_.Valid() || right_leg_chain_.Valid();
        arm_ik_available_ = left_arm_chain_.Valid() || right_arm_chain_.Valid();

        left_leg_chain_.label = "Left leg";
        left_leg_chain_.role = LimbIkChain::Role::Leg;
        right_leg_chain_.label = "Right leg";
        right_leg_chain_.role = LimbIkChain::Role::Leg;
        left_arm_chain_.label = "Left arm";
        left_arm_chain_.role = LimbIkChain::Role::Arm;
        right_arm_chain_.label = "Right arm";
        right_arm_chain_.role = LimbIkChain::Role::Arm;

        if (!previously_initialized)
        {
            ui_state_.foot_ik_enabled = false;
            ui_state_.foot_ik_ground_height = 0.f;
            ui_state_.foot_ik_weight = 1.f;
            ui_state_.foot_ik_soften = 0.97f;
            ui_state_.foot_ik_twist_angle_deg = 0.f;

            ui_state_.arm_ik_enabled = false;
            ui_state_.arm_ik_weight = 1.f;
            ui_state_.arm_ik_soften = 0.85f;
            ui_state_.arm_ik_twist_angle_deg = 0.f;
            ui_state_.arm_handles[0] = ViewerUiState::ArmHandle{};
            ui_state_.arm_handles[1] = ViewerUiState::ArmHandle{};
        }
        else
        {
            if (!leg_ik_available_)
            {
                ui_state_.foot_ik_enabled = false;
            }
            if (!arm_ik_available_)
            {
                ui_state_.arm_ik_enabled = false;
            }
        }

        left_leg_chain_.enabled = left_leg_chain_.Valid() && (previously_initialized ? previous_left_leg_enabled : true);
        right_leg_chain_.enabled = right_leg_chain_.Valid() && (previously_initialized ? previous_right_leg_enabled : true);
        left_arm_chain_.enabled = left_arm_chain_.Valid() && (previously_initialized ? previous_left_arm_enabled : true);
        right_arm_chain_.enabled = right_arm_chain_.Valid() && (previously_initialized ? previous_right_arm_enabled : true);

        left_leg_chain_.reached = false;
        right_leg_chain_.reached = false;
        left_arm_chain_.reached = false;
        right_arm_chain_.reached = false;

        if (left_arm_chain_.Valid())
        {
            IkOffsetUi offset{ ui_state_.arm_handles[0].forward_offset, ui_state_.arm_handles[0].lateral_offset, ui_state_.arm_handles[0].vertical_offset };
            left_arm_chain_.target_offset = FromUiOffset(offset);
        }
        if (right_arm_chain_.Valid())
        {
            IkOffsetUi offset{ ui_state_.arm_handles[1].forward_offset, ui_state_.arm_handles[1].lateral_offset, ui_state_.arm_handles[1].vertical_offset };
            right_arm_chain_.target_offset = FromUiOffset(offset);
        }

        left_leg_chain_.debug_target_valid = false;
        right_leg_chain_.debug_target_valid = false;
        left_arm_chain_.debug_target_valid = false;
        right_arm_chain_.debug_target_valid = false;

        ik_handle_dragging_ = false;
        active_ik_handle_ = IkHandleId::None;

        limb_ik_initialized_ = true;
    }

    bool ApplyLimbIKAfterLocal()
    {
        const bool leg_active = ui_state_.foot_ik_enabled && leg_ik_available_;
        const bool arm_active = ui_state_.arm_ik_enabled && arm_ik_available_;

        if (!leg_active)
        {
            left_leg_chain_.reached = false;
            right_leg_chain_.reached = false;
            left_leg_chain_.debug_target_valid = false;
            right_leg_chain_.debug_target_valid = false;
        }
        if (!arm_active)
        {
            left_arm_chain_.reached = false;
            right_arm_chain_.reached = false;
            left_arm_chain_.debug_target_valid = false;
            right_arm_chain_.debug_target_valid = false;
        }

        if (!leg_active && !arm_active)
        {
            return true;
        }

        bool applied = false;
        int min_dirty_joint = skeleton_.num_joints();

        if (leg_active)
        {
            const float weight = std::clamp(ui_state_.foot_ik_weight, 0.f, 1.f);
            const float soften = std::clamp(ui_state_.foot_ik_soften, 0.f, 0.999f);
            const float twist = ui_state_.foot_ik_twist_angle_deg * ozz::math::kDegreeToRadian;

            auto solve_leg = [&](LimbIkChain& chain)
            {
                if (!chain.enabled || !chain.Valid())
                {
                    chain.reached = false;
                    chain.debug_target_valid = false;
                    return;
                }

                if (static_cast<size_t>(chain.end) >= models_.size())
                {
                    chain.reached = false;
                    chain.debug_target_valid = false;
                    return;
                }

                const ozz::math::Float3 target = ComputeChainTarget(chain);
                chain.debug_target = target;
                chain.debug_target_valid = true;
                if (SolveLimbIk(chain, target, weight, soften, twist, &min_dirty_joint))
                {
                    applied = true;
                }
            };

            solve_leg(left_leg_chain_);
            solve_leg(right_leg_chain_);
        }

        if (arm_active)
        {
            const float weight = std::clamp(ui_state_.arm_ik_weight, 0.f, 1.f);
            const float soften = std::clamp(ui_state_.arm_ik_soften, 0.f, 0.999f);
            const float twist = ui_state_.arm_ik_twist_angle_deg * ozz::math::kDegreeToRadian;

            auto solve_arm = [&](LimbIkChain& chain, size_t handle_index)
            {
                if (!chain.enabled || !chain.Valid())
                {
                    chain.reached = false;
                    chain.debug_target_valid = false;
                    return;
                }
                if (static_cast<size_t>(chain.end) >= models_.size())
                {
                    chain.reached = false;
                    chain.debug_target_valid = false;
                    return;
                }

                if (handle_index < 2)
                {
                    const ViewerUiState::ArmHandle& handle = ui_state_.arm_handles[handle_index];
                    IkOffsetUi offset{ handle.forward_offset, handle.lateral_offset, handle.vertical_offset };
                    chain.target_offset = FromUiOffset(offset);
                }

                ozz::math::Float3 target = ComputeChainTarget(chain);
                chain.debug_target = target;
                chain.debug_target_valid = true;
                if (SolveLimbIk(chain, target, weight, soften, twist, &min_dirty_joint))
                {
                    applied = true;
                }
            };

            solve_arm(left_arm_chain_, 0);
            solve_arm(right_arm_chain_, 1);
        }

        if (applied)
        {
            ozz::animation::LocalToModelJob ltm_job;
            ltm_job.skeleton = &skeleton_;
            ltm_job.input = make_span(locals_);
            ltm_job.output = make_span(models_);
            if (min_dirty_joint > 0 && min_dirty_joint < skeleton_.num_joints())
            {
                ltm_job.from = min_dirty_joint;
            }
            if (!ltm_job.Run())
            {
                return false;
            }
        }

        return true;
    }

    bool SolveLimbIk(LimbIkChain& chain, const ozz::math::Float3& target_model, float weight, float soften, float twist_angle, int* min_dirty_joint)
    {
        chain.reached = false;
        if (!chain.Valid() || !chain.enabled)
        {
            chain.debug_target_valid = false;
            return false;
        }

        const size_t model_count = models_.size();
        if (chain.start < 0 || chain.mid < 0 || chain.end < 0 ||
            static_cast<size_t>(chain.start) >= model_count ||
            static_cast<size_t>(chain.mid) >= model_count ||
            static_cast<size_t>(chain.end) >= model_count)
        {
            chain.debug_target_valid = false;
            return false;
        }

        const ozz::math::SimdFloat4 target_ms = ozz::math::simd_float4::Load3PtrU(&target_model.x);

        ozz::math::SimdFloat4 pole_vector_ms = models_[chain.mid].cols[1];
        const ozz::math::SimdFloat4 pole_len_sq = ozz::math::Length3Sqr(pole_vector_ms);
        const ozz::math::SimdFloat4 min_len = ozz::math::simd_float4::Load1(1e-6f);
        if (ozz::math::AreAllTrue1(ozz::math::CmpGt(pole_len_sq, min_len)))
        {
            pole_vector_ms = ozz::math::Normalize3(pole_vector_ms);
        }

        ozz::animation::IKTwoBoneJob ik_job;
        ik_job.target = target_ms;
        ik_job.pole_vector = pole_vector_ms;
        ik_job.mid_axis = chain.mid_axis;
        ik_job.weight = std::clamp(weight, 0.f, 1.f);
        ik_job.soften = std::clamp(soften, 0.f, 0.999f);
        ik_job.twist_angle = twist_angle;
        ik_job.start_joint = &models_[chain.start];
        ik_job.mid_joint = &models_[chain.mid];
        ik_job.end_joint = &models_[chain.end];
        ozz::math::SimdQuaternion start_correction;
        ozz::math::SimdQuaternion mid_correction;
        ik_job.start_joint_correction = &start_correction;
        ik_job.mid_joint_correction = &mid_correction;
        ik_job.reached = &chain.reached;

        if (!ik_job.Run())
        {
            chain.reached = false;
            return false;
        }

        ozz::sample::MultiplySoATransformQuaternion(chain.start, start_correction, make_span(locals_));
        ozz::sample::MultiplySoATransformQuaternion(chain.mid, mid_correction, make_span(locals_));

        if (min_dirty_joint)
        {
            *min_dirty_joint = std::min(*min_dirty_joint, chain.start);
        }

        return true;
    }

    ozz::math::Float3 ComputeChainTarget(const LimbIkChain& chain) const
    {
        if (!chain.Valid() || static_cast<size_t>(chain.end) >= models_.size())
        {
            return { 0.f, 0.f, 0.f };
        }

        ozz::math::Float3 target = ExtractTranslation(models_[chain.end]);
        if (chain.role == LimbIkChain::Role::Leg)
        {
            target.x += chain.target_offset.x;
            target.z += chain.target_offset.z;
            target.y = ui_state_.foot_ik_ground_height + chain.target_offset.y;
        }
        else
        {
            target.x += chain.target_offset.x;
            target.y += chain.target_offset.y;
            target.z += chain.target_offset.z;
            if (ui_state_.crouch_enabled && ui_state_.crouch_affects_arms)
            {
                target.y -= active_crouch_offset_;
            }
        }
        return target;
    }

    void ApplyDraggedTarget(IkHandleId handle, const ozz::math::Float3& target_world)
    {
        LimbIkChain* chain = GetChainForHandle(handle);
        if (!chain || !chain->Valid() || static_cast<size_t>(chain->end) >= models_.size())
        {
            return;
        }

        const ozz::math::Float3 end_position = ExtractTranslation(models_[chain->end]);

        if (chain->role == LimbIkChain::Role::Leg)
        {
            chain->target_offset.x = target_world.x - end_position.x;
            chain->target_offset.z = target_world.z - end_position.z;
            chain->target_offset.y = target_world.y - ui_state_.foot_ik_ground_height;
        }
        else
        {
            const float crouch_drop = (ui_state_.crouch_enabled && ui_state_.crouch_affects_arms) ? active_crouch_offset_ : 0.f;
            chain->target_offset.x = target_world.x - end_position.x;
            chain->target_offset.y = target_world.y + crouch_drop - end_position.y;
            chain->target_offset.z = target_world.z - end_position.z;

            const int handle_index = (handle == IkHandleId::LeftArm) ? 0 : (handle == IkHandleId::RightArm ? 1 : -1);
            if (handle_index >= 0)
            {
                IkOffsetUi offset = ToUiOffset(chain->target_offset);
                ui_state_.arm_handles[handle_index].forward_offset = offset.forward;
                ui_state_.arm_handles[handle_index].lateral_offset = offset.lateral;
                ui_state_.arm_handles[handle_index].vertical_offset = offset.vertical;
            }
        }

        chain->debug_target = target_world;
        chain->debug_target_valid = true;
    }

    bool TrySelectIkHandle(const ozz::math::Float3& ray_origin, const ozz::math::Float3& ray_dir, const ozz::math::Float3& camera_pos,
        float pick_ndc_x, float pick_ndc_y, const ozz::math::Float4x4* view_projection)
    {
        const bool legs_active = ui_state_.foot_ik_enabled && leg_ik_available_;
        const bool arms_active = ui_state_.arm_ik_enabled && arm_ik_available_;
        const bool use_screen_metric = (view_projection != nullptr);

        struct Candidate
        {
            IkHandleId id;
            bool active;
        };

        const Candidate candidates[] = {
            { IkHandleId::LeftLeg, legs_active },
            { IkHandleId::RightLeg, legs_active },
            { IkHandleId::LeftArm, arms_active },
            { IkHandleId::RightArm, arms_active },
        };

        float best_t = std::numeric_limits<float>::infinity();
        IkHandleId best_id = IkHandleId::None;
        ozz::math::Float3 best_target{ 0.f, 0.f, 0.f };
        float best_screen_dist_sq = std::numeric_limits<float>::infinity();

        for (const Candidate& candidate : candidates)
        {
            if (!candidate.active)
            {
                continue;
            }

            LimbIkChain* chain = GetChainForHandle(candidate.id);
            if (!chain || !chain->enabled || !chain->Valid())
            {
                continue;
            }

            const ozz::math::Float3 target = ComputeChainTarget(*chain);
            chain->debug_target = target;
            chain->debug_target_valid = true;

            float t = 0.f;
            if (!IntersectRaySphere(ray_origin, ray_dir, target, kIkHandleHitRadius, t))
            {
                continue;
            }

            float screen_dist_sq = use_screen_metric ? 0.f : std::numeric_limits<float>::infinity();
            if (use_screen_metric)
            {
                const ozz::math::SimdFloat4 target_simd = ozz::math::simd_float4::Load(target.x, target.y, target.z, 1.f);
                const ozz::math::SimdFloat4 clip_simd = ozz::math::TransformPoint(*view_projection, target_simd);
                float clip_values[4];
                ozz::math::StorePtrU(clip_simd, clip_values);
                const float clip_w = clip_values[3];
                const float epsilon = 1e-6f;
                if (std::fabs(clip_w) > epsilon)
                {
                    const float ndc_x = clip_values[0] / clip_w;
                    const float ndc_y = clip_values[1] / clip_w;
                    const float dx = ndc_x - pick_ndc_x;
                    const float dy = ndc_y - pick_ndc_y;
                    screen_dist_sq = dx * dx + dy * dy;
                }
            }

            const float kEpsilon = 1e-6f;
            const bool better_screen = use_screen_metric ? (screen_dist_sq + kEpsilon < best_screen_dist_sq) : false;
            const bool screen_tie = use_screen_metric ? (std::fabs(screen_dist_sq - best_screen_dist_sq) <= kEpsilon) : true;
            const bool better_t = t < best_t;

            if (better_screen || (screen_tie && better_t))
            {
                best_screen_dist_sq = screen_dist_sq;
                best_t = t;
                best_id = candidate.id;
                best_target = target;
            }
        }

        if (best_id == IkHandleId::None)
        {
            return false;
        }

        LimbIkChain* selected_chain = GetChainForHandle(best_id);
        if (!selected_chain)
        {
            return false;
        }

        active_ik_handle_ = best_id;
        ik_handle_dragging_ = true;
        ik_drag_plane_point_ = best_target;

        ozz::math::Float3 normal = Normalize3(Sub3(best_target, camera_pos));
        if (LengthSq3(normal) <= 1e-6f)
        {
            normal = { 0.f, 1.f, 0.f };
        }
        ik_drag_plane_normal_ = normal;

        return true;
    }

    void UpdateActiveIkHandle(const ozz::math::Float3& ray_origin, const ozz::math::Float3& ray_dir)
    {
        if (!ik_handle_dragging_ || active_ik_handle_ == IkHandleId::None)
        {
            return;
        }

        ozz::math::Float3 hit;
        if (!IntersectRayPlane(ray_origin, ray_dir, ik_drag_plane_point_, ik_drag_plane_normal_, hit))
        {
            return;
        }

        ik_drag_plane_point_ = hit;
        ApplyDraggedTarget(active_ik_handle_, hit);
    }

    ozz::math::Float3 GetJointTranslation(const ozz::vector<ozz::math::SoaTransform>& transforms, int joint) const
    {
        if (joint < 0)
        {
            return { 0.f, 0.f, 0.f };
        }

        const size_t soa_index = static_cast<size_t>(joint) / 4;
        if (soa_index >= transforms.size())
        {
            return { 0.f, 0.f, 0.f };
        }

        const int lane = joint & 3;
        float tx[4];
        float ty[4];
        float tz[4];
        ozz::math::StorePtrU(transforms[soa_index].translation.x, tx);
        ozz::math::StorePtrU(transforms[soa_index].translation.y, ty);
        ozz::math::StorePtrU(transforms[soa_index].translation.z, tz);
        return { tx[lane], ty[lane], tz[lane] };
    }

    ozz::math::Quaternion GetJointRotation(const ozz::vector<ozz::math::SoaTransform>& transforms, int joint) const
    {
        if (joint < 0)
        {
            return ozz::math::Quaternion::identity();
        }

        const size_t soa_index = static_cast<size_t>(joint) / 4;
        if (soa_index >= transforms.size())
        {
            return ozz::math::Quaternion::identity();
        }

        const int lane = joint & 3;
        float qx[4];
        float qy[4];
        float qz[4];
        float qw[4];
        ozz::math::StorePtrU(transforms[soa_index].rotation.x, qx);
        ozz::math::StorePtrU(transforms[soa_index].rotation.y, qy);
        ozz::math::StorePtrU(transforms[soa_index].rotation.z, qz);
        ozz::math::StorePtrU(transforms[soa_index].rotation.w, qw);
        return ozz::math::Quaternion(qx[lane], qy[lane], qz[lane], qw[lane]);
    }

    ozz::math::Float3 GetLocalTranslation(int joint) const
    {
        return GetJointTranslation(locals_, joint);
    }

    ozz::math::Quaternion GetLocalRotation(int joint) const
    {
        return GetJointRotation(locals_, joint);
    }

    ozz::math::Float3 GetTargetTranslation(int joint) const
    {
        return GetJointTranslation(ragdoll_target_locals_, joint);
    }

    ozz::math::Quaternion GetTargetRotation(int joint) const
    {
        return GetJointRotation(ragdoll_target_locals_, joint);
    }

    void SetLocalTranslation(int joint, const ozz::math::Float3& translation)
    {
        if (joint < 0)
        {
            return;
        }

        const size_t soa_index = static_cast<size_t>(joint) / 4;
        if (soa_index >= locals_.size())
        {
            return;
        }

        const int lane = joint & 3;
        float tx[4];
        float ty[4];
        float tz[4];
        ozz::math::StorePtrU(locals_[soa_index].translation.x, tx);
        ozz::math::StorePtrU(locals_[soa_index].translation.y, ty);
        ozz::math::StorePtrU(locals_[soa_index].translation.z, tz);
        tx[lane] = translation.x;
        ty[lane] = translation.y;
        tz[lane] = translation.z;
        locals_[soa_index].translation.x = ozz::math::simd_float4::Load(tx[0], tx[1], tx[2], tx[3]);
        locals_[soa_index].translation.y = ozz::math::simd_float4::Load(ty[0], ty[1], ty[2], ty[3]);
        locals_[soa_index].translation.z = ozz::math::simd_float4::Load(tz[0], tz[1], tz[2], tz[3]);
    }

    void SetLocalRotation(int joint, const ozz::math::Quaternion& rotation)
    {
        if (joint < 0)
        {
            return;
        }

        const size_t soa_index = static_cast<size_t>(joint) / 4;
        if (soa_index >= locals_.size())
        {
            return;
        }

        const ozz::math::Quaternion normalized = ozz::math::NormalizeSafe(rotation, ozz::math::Quaternion::identity());

        const int lane = joint & 3;
        float qx[4];
        float qy[4];
        float qz[4];
        float qw[4];
        ozz::math::StorePtrU(locals_[soa_index].rotation.x, qx);
        ozz::math::StorePtrU(locals_[soa_index].rotation.y, qy);
        ozz::math::StorePtrU(locals_[soa_index].rotation.z, qz);
        ozz::math::StorePtrU(locals_[soa_index].rotation.w, qw);
        qx[lane] = normalized.x;
        qy[lane] = normalized.y;
        qz[lane] = normalized.z;
        qw[lane] = normalized.w;
        locals_[soa_index].rotation.x = ozz::math::simd_float4::Load(qx[0], qx[1], qx[2], qx[3]);
        locals_[soa_index].rotation.y = ozz::math::simd_float4::Load(qy[0], qy[1], qy[2], qy[3]);
        locals_[soa_index].rotation.z = ozz::math::simd_float4::Load(qz[0], qz[1], qz[2], qz[3]);
        locals_[soa_index].rotation.w = ozz::math::simd_float4::Load(qw[0], qw[1], qw[2], qw[3]);
    }

    void ApplyProceduralCrouch()
    {
        active_crouch_offset_ = 0.f;
        if (!ui_state_.crouch_enabled)
        {
            return;
        }

        const float crouch_amount = std::clamp(ui_state_.crouch_amount, 0.f, 1.f);
        const float crouch_height = std::max(ui_state_.crouch_height, 0.f);
        if (crouch_amount <= 0.f || crouch_height <= 0.f)
        {
            return;
        }

        const float offset = crouch_amount * crouch_height;
        active_crouch_offset_ = offset;

        ozz::math::Float3 root_translation = GetLocalTranslation(0);
        root_translation.y -= offset;
        SetLocalTranslation(0, root_translation);

        const float pitch_deg = ui_state_.crouch_spine_pitch_deg * crouch_amount;
        if (std::fabs(pitch_deg) > std::numeric_limits<float>::epsilon())
        {
            const float pitch_rad = pitch_deg * ozz::math::kDegreeToRadian;
            const ozz::math::Float3 axis{ 0.f, 1.f, 0.f };
            const ozz::math::Quaternion pelvis_rot = ozz::math::Quaternion::FromAxisAngle(axis, -pitch_rad);
            const ozz::math::SimdQuaternion pelvis_delta{ ozz::math::simd_float4::Load(pelvis_rot.x, pelvis_rot.y, pelvis_rot.z, pelvis_rot.w) };
            if (pelvis_joint_ >= 0)
            {
                ozz::sample::MultiplySoATransformQuaternion(pelvis_joint_, pelvis_delta, make_span(locals_));
            }

            const float spine_pitch_rad = -pitch_rad * 0.5f;
            if (spine_joint_ >= 0 && spine_joint_ != pelvis_joint_)
            {
                const ozz::math::Quaternion spine_rot = ozz::math::Quaternion::FromAxisAngle(axis, spine_pitch_rad);
                const ozz::math::SimdQuaternion spine_delta{ ozz::math::simd_float4::Load(spine_rot.x, spine_rot.y, spine_rot.z, spine_rot.w) };
                ozz::sample::MultiplySoATransformQuaternion(spine_joint_, spine_delta, make_span(locals_));
            }
        }
    }

    void EnsureRagdollState()
    {
        const int joint_count = skeleton_.num_joints();
        if (joint_count <= 0)
        {
            ragdoll_state_.clear();
            ragdoll_target_locals_.clear();
            return;
        }

        if (ragdoll_state_.size() != static_cast<size_t>(joint_count))
        {
            ragdoll_state_.assign(static_cast<size_t>(joint_count), RagdollJointState{});
        }

        const size_t soa_count = static_cast<size_t>(skeleton_.num_soa_joints());
        if (ragdoll_target_locals_.size() != soa_count)
        {
            ragdoll_target_locals_.resize(soa_count);
        }
    }

    void ResetRagdollStateFromLocals()
    {
        EnsureRagdollState();
        const int joint_count = skeleton_.num_joints();
        for (int joint = 0; joint < joint_count; ++joint)
        {
            RagdollJointState& state = ragdoll_state_[static_cast<size_t>(joint)];
            state.translation = GetLocalTranslation(joint);
            state.rotation = ozz::math::NormalizeSafe(GetLocalRotation(joint), ozz::math::Quaternion::identity());
            state.velocity = { 0.f, 0.f, 0.f };
            state.initialized = true;
        }
    }

    void UpdateActiveRagdoll(float dt)
    {
        if (dt <= 0.f || ragdoll_state_.empty() || ragdoll_target_locals_.empty())
        {
            return;
        }

        const int joint_count = skeleton_.num_joints();
        const float stiffness = std::max(ui_state_.ragdoll_stiffness, 0.f);
        const float damping = std::clamp(ui_state_.ragdoll_damping, 0.f, 1.f);
        const float gravity = ui_state_.ragdoll_gravity;

        const float translation_gain = std::clamp(stiffness * dt, 0.f, 1.f);
        const float rotation_gain = std::clamp(stiffness * dt * 0.5f, 0.f, 1.f);

        for (int joint = 0; joint < joint_count; ++joint)
        {
            RagdollJointState& state = ragdoll_state_[static_cast<size_t>(joint)];
            if (!state.initialized)
            {
                state.translation = GetLocalTranslation(joint);
                state.rotation = ozz::math::NormalizeSafe(GetLocalRotation(joint), ozz::math::Quaternion::identity());
                state.velocity = { 0.f, 0.f, 0.f };
                state.initialized = true;
            }

            const ozz::math::Float3 target_translation = GetTargetTranslation(joint);
            const ozz::math::Quaternion target_rotation = ozz::math::NormalizeSafe(GetTargetRotation(joint), ozz::math::Quaternion::identity());

            ozz::math::Float3 delta_translation{ target_translation.x - state.translation.x,
                target_translation.y - state.translation.y,
                target_translation.z - state.translation.z };

            state.velocity.x += delta_translation.x * stiffness * dt;
            state.velocity.y += delta_translation.y * stiffness * dt;
            state.velocity.z += delta_translation.z * stiffness * dt;

            if (joint == 0)
            {
                state.velocity.z += gravity * dt;
            }

            state.velocity.x *= (1.f - damping);
            state.velocity.y *= (1.f - damping);
            state.velocity.z *= (1.f - damping);

            state.translation.x += state.velocity.x * dt;
            state.translation.y += state.velocity.y * dt;
            state.translation.z += state.velocity.z * dt;

            const ozz::math::Quaternion blended = ozz::math::NLerp(state.rotation, target_rotation, rotation_gain);
            state.rotation = ozz::math::NormalizeSafe(blended, target_rotation);

            // Blend translation toward the target to prevent drift when stiffness is low.
            state.translation.x += delta_translation.x * translation_gain;
            state.translation.y += delta_translation.y * translation_gain;
            state.translation.z += delta_translation.z * translation_gain;

            SetLocalTranslation(joint, state.translation);
            SetLocalRotation(joint, state.rotation);
        }
    }

    bool ExportAnimationToJson(const char* path)
    {
        if (path == nullptr || path[0] == '\0')
        {
            return true;
        }

        if (animations_.empty())
        {
            ozz::log::Err() << "No animation available for JSON export." << std::endl;
            return false;
        }

        if (!HasAnimationSelected())
        {
            ozz::log::Err() << "No animation selected for JSON export." << std::endl;
            return false;
        }

        const ozz::animation::Animation& animation = animations_[ui_state_.current_animation];
        const float duration = animation.duration();
        const ozz::span<const float> timepoints = animation.timepoints();

        ozz::vector<float> samples;
        samples.reserve(timepoints.size() + 2);
        constexpr float kEpsilon = 1e-5f;
        if (timepoints.empty() || timepoints.front() > kEpsilon)
        {
            samples.push_back(0.f);
        }
        samples.insert(samples.end(), timepoints.begin(), timepoints.end());
        if (!samples.empty())
        {
            std::sort(samples.begin(), samples.end());
            samples.erase(std::unique(samples.begin(), samples.end(),
                              [](float a, float b)
                              {
                                  return std::fabs(a - b) <= kEpsilon;
                              }),
                samples.end());
        }
        if (duration > kEpsilon)
        {
            if (samples.empty() || std::fabs(samples.back() - duration) > kEpsilon)
            {
                samples.push_back(duration);
            }
        }
        else if (samples.empty())
        {
            samples.push_back(0.f);
        }

        std::ofstream file(path, std::ios::binary);
        if (!file)
        {
            ozz::log::Err() << "Failed to open JSON export path: " << path << std::endl;
            return false;
        }
        file.imbue(std::locale::classic());

        const int joint_count = skeleton_.num_joints();
        const ozz::span<const char* const> joint_names = skeleton_.joint_names();

        ozz::animation::SamplingJob sampling_job;
        sampling_job.animation = &animation;
        sampling_job.context = &context_;
        sampling_job.output = make_span(locals_);

        ozz::animation::LocalToModelJob ltm_job;
        ltm_job.skeleton = &skeleton_;
        ltm_job.input = make_span(locals_);
        ltm_job.output = make_span(models_);

        const int frame_count = static_cast<int>(samples.size());
        const float frame_rate = duration > kEpsilon && frame_count > 1 ? static_cast<float>(frame_count - 1) / std::max(duration, kEpsilon) : 0.f;

        file << "{\n";
        const char* skeleton_source = using_bundle_ ? bundle_source_path_.c_str() : OPTIONS_skeleton;
        file << "  \"skeleton\": \"" << EscapeJsonString(skeleton_source ? skeleton_source : "") << "\",\n";
        file << "  \"animation\": \"" << EscapeJsonString(OPTIONS_animation) << "\",\n";
        file << "  \"duration\": " << FormatFloat(duration) << ",\n";
        file << "  \"frame_rate\": " << FormatFloat(frame_rate) << ",\n";
        file << "  \"frame_count\": " << frame_count << ",\n";
        file << "  \"frame_start\": 0,\n";
        file << "  \"frame_end\": " << (frame_count > 0 ? frame_count - 1 : 0) << ",\n";
        file << "  \"frames\": [\n";

        for (int frame = 0; frame < frame_count; ++frame)
        {
            const float sample_time = samples[frame];
            const float ratio = duration > kEpsilon ? sample_time / duration : 0.f;
            sampling_job.ratio = std::clamp(ratio, 0.f, 1.f);
            if (!sampling_job.Run())
            {
                ozz::log::Err() << "SamplingJob failed at frame " << frame << std::endl;
                return false;
            }
            if (!ltm_job.Run())
            {
                ozz::log::Err() << "LocalToModelJob failed at frame " << frame << std::endl;
                return false;
            }

            file << "    {\n";
            file << "      \"frame\": " << frame << ",\n";
            file << "      \"time\": " << FormatFloat(sample_time) << ",\n";
            file << "      \"ratio\": " << FormatFloat(sampling_job.ratio) << ",\n";
            file << "      \"bones\": {\n";

            for (int joint = 0; joint < joint_count; ++joint)
            {
                const ozz::math::Float4x4& matrix = models_[joint];
                const float tx = ozz::math::GetX(matrix.cols[3]);
                const float ty = ozz::math::GetY(matrix.cols[3]);
                const float tz = ozz::math::GetZ(matrix.cols[3]);

                const ozz::math::SimdFloat4 quat_simd = ozz::math::ToQuaternion(matrix);
                float quat_values[4];
                ozz::math::StorePtrU(quat_simd, quat_values);

                const float sx = ozz::math::GetX(ozz::math::Length3(matrix.cols[0]));
                const float sy = ozz::math::GetX(ozz::math::Length3(matrix.cols[1]));
                const float sz = ozz::math::GetX(ozz::math::Length3(matrix.cols[2]));

                file << "        \"" << EscapeJsonString(joint_names[joint]) << "\": {\n";
                file << "          \"location\": [" << FormatFloat(tx) << ", " << FormatFloat(ty) << ", " << FormatFloat(tz) << "],\n";
                file << "          \"rotation\": [" << FormatFloat(quat_values[0]) << ", " << FormatFloat(quat_values[1]) << ", " << FormatFloat(quat_values[2])
                     << ", " << FormatFloat(quat_values[3]) << "],\n";
                file << "          \"scale\": [" << FormatFloat(sx) << ", " << FormatFloat(sy) << ", " << FormatFloat(sz) << "]\n";
                file << "        }";
                if (joint + 1 != joint_count)
                {
                    file << ",";
                }
                file << "\n";
            }

            file << "      }\n";
            file << "    }";
            if (frame + 1 != frame_count)
            {
                file << ",";
            }
            file << "\n";
        }

        file << "  ]\n";
        file << "}\n";

        ozz::log::LogV() << "Exported animation JSON to " << path << std::endl;
        return true;
    }
};

void PlaybackSampleApplication::BuildTriangleDebugMeshes()
{
    triangle_debug_meshes_.clear();
    triangle_debug_meshes_.resize(meshes_.size());

    const bool random_colors = ui_state_.random_triangle_colors;
    if (!random_colors)
    {
        return;
    }

    std::mt19937 base_rng(0x715517u);
    std::uniform_real_distribution<float> color_distribution(0.25f, 0.95f);
    const size_t max_triangles_per_segment = std::numeric_limits<uint16_t>::max() / 3;

    for (size_t mesh_index = 0; mesh_index < meshes_.size(); ++mesh_index)
    {
        const ozz::sample::Mesh& mesh = meshes_[mesh_index];
        auto& segments = triangle_debug_meshes_[mesh_index];
        segments.clear();

        const size_t triangle_count = mesh.triangle_indices.size() / 3;
        if (triangle_count == 0)
        {
            continue;
        }

        std::mt19937 mesh_rng(base_rng());

        size_t triangle_offset = 0;
        while (triangle_offset < triangle_count)
        {
            const size_t segment_triangles = std::min(max_triangles_per_segment, triangle_count - triangle_offset);
            TriangleDebugMesh segment;
            segment.vertex_remap.resize(segment_triangles * 3);
            segment.mesh.triangle_indices.clear();
            segment.mesh.parts.clear();
            segment.mesh.inverse_bind_poses.clear();
            segment.mesh.joint_remaps.clear();

            ozz::sample::Mesh::Part part;
            part.positions.resize(segment_triangles * 3 * ozz::sample::Mesh::Part::kPositionsCpnts, 0.f);
            part.normals.resize(segment_triangles * 3 * ozz::sample::Mesh::Part::kNormalsCpnts, 0.f);
            part.tangents.clear();
            part.uvs.clear();
            part.colors.resize(segment_triangles * 3 * ozz::sample::Mesh::Part::kColorsCpnts);
            part.joint_indices.clear();
            part.joint_weights.clear();

            for (size_t tri = 0; tri < segment_triangles; ++tri)
            {
                const size_t mesh_triangle = triangle_offset + tri;
                const uint32_t idx0 = mesh.triangle_indices[mesh_triangle * 3 + 0];
                const uint32_t idx1 = mesh.triangle_indices[mesh_triangle * 3 + 1];
                const uint32_t idx2 = mesh.triangle_indices[mesh_triangle * 3 + 2];

                const size_t base_vertex = tri * 3;
                segment.vertex_remap[base_vertex + 0] = idx0;
                segment.vertex_remap[base_vertex + 1] = idx1;
                segment.vertex_remap[base_vertex + 2] = idx2;

                segment.mesh.triangle_indices.push_back(static_cast<uint16_t>(base_vertex + 0));
                segment.mesh.triangle_indices.push_back(static_cast<uint16_t>(base_vertex + 1));
                segment.mesh.triangle_indices.push_back(static_cast<uint16_t>(base_vertex + 2));

                const float r = color_distribution(mesh_rng);
                const float g = color_distribution(mesh_rng);
                const float b = color_distribution(mesh_rng);
                const uint8_t r8 = static_cast<uint8_t>(std::clamp(r, 0.f, 1.f) * 255.f);
                const uint8_t g8 = static_cast<uint8_t>(std::clamp(g, 0.f, 1.f) * 255.f);
                const uint8_t b8 = static_cast<uint8_t>(std::clamp(b, 0.f, 1.f) * 255.f);
                const uint8_t a8 = 255;

                for (size_t corner = 0; corner < 3; ++corner)
                {
                    const size_t color_offset = (base_vertex + corner) * ozz::sample::Mesh::Part::kColorsCpnts;
                    part.colors[color_offset + 0] = r8;
                    part.colors[color_offset + 1] = g8;
                    part.colors[color_offset + 2] = b8;
                    part.colors[color_offset + 3] = a8;
                }
            }

            segment.mesh.parts.push_back(std::move(part));
            segments.push_back(std::move(segment));

            triangle_offset += segment_triangles;
        }
    }
}

void PlaybackSampleApplication::UpdateTriangleDebugMesh(const std::vector<ozz::math::Float3>& world_positions, TriangleDebugMesh& debug_mesh)
{
    if (debug_mesh.vertex_remap.empty() || debug_mesh.mesh.parts.empty())
    {
        return;
    }

    ozz::sample::Mesh::Part& part = debug_mesh.mesh.parts[0];
    if (part.positions.size() / ozz::sample::Mesh::Part::kPositionsCpnts != debug_mesh.vertex_remap.size())
    {
        return;
    }

    const float surface_offset = 0.001f;
    const float epsilon = std::numeric_limits<float>::epsilon();

    const size_t triangle_count = debug_mesh.vertex_remap.size() / 3;
    for (size_t tri = 0; tri < triangle_count; ++tri)
    {
        const size_t base_vertex = tri * 3;
        const uint32_t remap0 = debug_mesh.vertex_remap[base_vertex + 0];
        const uint32_t remap1 = debug_mesh.vertex_remap[base_vertex + 1];
        const uint32_t remap2 = debug_mesh.vertex_remap[base_vertex + 2];

        if (remap0 >= world_positions.size() || remap1 >= world_positions.size() || remap2 >= world_positions.size())
        {
            continue;
        }

        const ozz::math::Float3& p0 = world_positions[remap0];
        const ozz::math::Float3& p1 = world_positions[remap1];
        const ozz::math::Float3& p2 = world_positions[remap2];

        const ozz::math::Float3 edge0{ p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
        const ozz::math::Float3 edge1{ p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
        ozz::math::Float3 normal{
            edge0.y * edge1.z - edge0.z * edge1.y,
            edge0.z * edge1.x - edge0.x * edge1.z,
            edge0.x * edge1.y - edge0.y * edge1.x,
        };

        float normal_length_sq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
        if (normal_length_sq <= epsilon)
        {
            normal = { 0.f, 1.f, 0.f };
            normal_length_sq = 1.f;
        }
        else
        {
            const float inv_length = 1.f / std::sqrt(normal_length_sq);
            normal.x *= inv_length;
            normal.y *= inv_length;
            normal.z *= inv_length;
        }

        const ozz::math::Float3 offset{
            normal.x * surface_offset,
            normal.y * surface_offset,
            normal.z * surface_offset,
        };

        auto store_vertex = [&](size_t vertex_index, const ozz::math::Float3& position)
        {
            const size_t position_offset = vertex_index * ozz::sample::Mesh::Part::kPositionsCpnts;
            part.positions[position_offset + 0] = position.x + offset.x;
            part.positions[position_offset + 1] = position.y + offset.y;
            part.positions[position_offset + 2] = position.z + offset.z;

            const size_t normal_offset = vertex_index * ozz::sample::Mesh::Part::kNormalsCpnts;
            part.normals[normal_offset + 0] = normal.x;
            part.normals[normal_offset + 1] = normal.y;
            part.normals[normal_offset + 2] = normal.z;
        };

        store_vertex(base_vertex + 0, p0);
        store_vertex(base_vertex + 1, p1);
        store_vertex(base_vertex + 2, p2);
    }
}

int main(int _argc, const char** _argv)
{
    const char* title = "Ozz-animation sample: Binary animation/skeleton playback";
    return PlaybackSampleApplication().Run(_argc, _argv, "1.0", title);
}
