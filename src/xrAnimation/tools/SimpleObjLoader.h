#pragma once

#include <string>
#include <vector>
#include <ozz/base/containers/vector.h>
#include <ozz/base/maths/simd_math.h>

namespace ozz {
namespace sample {
struct Mesh;
}
}

namespace xray {
namespace animation {
namespace tools {

// Simple OBJ loader that creates a non-skinned ozz::sample::Mesh
class SimpleObjLoader {
public:
    // Load an OBJ file into an ozz::sample::Mesh
    // Returns true on success
    static bool LoadObjFile(const std::string& file_path, ozz::sample::Mesh& out_mesh);

private:
    struct TempVertex {
        float position[3];
        float normal[3];
        float uv[2];
    };
};

} // namespace tools
} // namespace animation
} // namespace xray
