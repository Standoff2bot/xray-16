// DetailManager_Compute.h: GPU-driven rendering infrastructure for detail objects
// Implements compute shader-based frustum culling and indirect drawing
//
#pragma once

#include "DetailFormat.h"
#include <cstddef>

// Forward declarations for DirectX types
struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;
typedef ID3D11Buffer ID3DBuffer;
typedef ID3D11ShaderResourceView ID3DShaderResourceView;
typedef ID3D11UnorderedAccessView ID3DUnorderedAccessView;

namespace xray::render::RENDER_NAMESPACE
{
namespace gpu_grass
{
struct TileResourceSlice;
}

// Forward declarations
class CDetailManager;
class CDetail;

// ===========================
// GPU Data Structures
// ===========================

// Must match HLSL layout - tightly packed for GPU consumption
// Size: 112 bytes to match shader definition
#pragma pack(push, 1)
struct DetailInstanceGPU
{
    // Transform data (32 bytes)
    Fvector position;        // World position (12 bytes)
    float scale;             // Uniform scale (4 bytes)
    float rotation_y;        // Y-axis rotation in radians (4 bytes)
    float padding0[3];       // Alignment padding (12 bytes) - matches float3 in shader

    // Rendering data (32 bytes)
    float c_hemi;            // Hemispherical lighting [0..1] (4 bytes)
    float c_sun;             // Sun lighting [0..1] (4 bytes)
    u32 object_id;           // Index into objects array (4 bytes)
    u32 vis_id;              // Animation type: 0=still, 1=wave1, 2=wave2 (4 bytes)
    Fvector color_rgb;       // RGB color (R1 only) (12 bytes)
    float padding1;          // Alignment (4 bytes)

    // Bounding data for culling (32 bytes)
    Fvector bounds_min;      // Local-space AABB min (12 bytes)
    float bounds_radius;     // Bounding sphere radius (4 bytes)
    Fvector bounds_max;      // Local-space AABB max (12 bytes)
    float padding2;          // Alignment (4 bytes)

    // Metadata (16 bytes)
    u32 slot_x;              // Source slot X coordinate (4 bytes)
    u32 slot_z;              // Source slot Z coordinate (4 bytes)
    u32 flags;               // Flags for special rendering (4 bytes)
    float fade_distance_sqr; // Pre-computed squared distance for fading (4 bytes)
};
#pragma pack(pop)

static_assert(sizeof(DetailInstanceGPU) == 112, "DetailInstanceGPU must be 112 bytes to match shader");
static_assert(offsetof(DetailInstanceGPU, position) == 0, "DetailInstanceGPU::position offset mismatch");
static_assert(offsetof(DetailInstanceGPU, scale) == 12, "DetailInstanceGPU::scale offset mismatch");
static_assert(offsetof(DetailInstanceGPU, rotation_y) == 16, "DetailInstanceGPU::rotation_y offset mismatch");
static_assert(offsetof(DetailInstanceGPU, c_hemi) == 32, "DetailInstanceGPU::c_hemi offset mismatch");
static_assert(offsetof(DetailInstanceGPU, c_sun) == 36, "DetailInstanceGPU::c_sun offset mismatch");
static_assert(offsetof(DetailInstanceGPU, object_id) == 40, "DetailInstanceGPU::object_id offset mismatch");
static_assert(offsetof(DetailInstanceGPU, vis_id) == 44, "DetailInstanceGPU::vis_id offset mismatch");
static_assert(offsetof(DetailInstanceGPU, bounds_min) == 64, "DetailInstanceGPU::bounds_min offset mismatch");
static_assert(offsetof(DetailInstanceGPU, bounds_radius) == 76, "DetailInstanceGPU::bounds_radius offset mismatch");
static_assert(offsetof(DetailInstanceGPU, slot_x) == 96, "DetailInstanceGPU::slot_x offset mismatch");
static_assert(offsetof(DetailInstanceGPU, fade_distance_sqr) == 108, "DetailInstanceGPU::fade_distance_sqr offset mismatch");

struct DetailObjectGPU
{
    Fvector bbox_min;
    float min_scale;
    Fvector bbox_max;
    float max_scale;
    float radius;
    u32 flags;
    u32 base_vis_id;
    u32 padding;
};

struct PlacementParamsGPU
{
    u32 instance_offset;
    u32 slot_offset;
    u32 slot_count;
    u32 tile_span;
    u32 samples_per_slot;
    u32 sample_dim;
    u32 max_instances;
    u32 pad0;
    float slot_size;
    float density;
    float jitter_amplitude;
    float detail_height_scale;
    float tile_origin_x;
    float tile_origin_z;
    float invalid_height_value;
    float pad1;
};

// GPU-friendly frustum planes (64 bytes)
struct FrustumGPU
{
    Fvector4 planes[6];  // Left, Right, Top, Bottom, Near, Far
};

// Culling parameters passed to compute shader (160 bytes)
struct DetailCullParams
{
    Fvector camera_pos;         // Camera world position (12 bytes)
    float fade_limit_sqr;       // Maximum draw distance squared (4 bytes)

    Fvector camera_dir;         // Camera forward direction (12 bytes)
    float fade_start_sqr;       // Fade start distance squared (4 bytes)

    float r_ssa_discard;        // SSA threshold for culling (4 bytes)
    float r_ssa_cheap;          // SSA threshold for LOD selection (4 bytes)
    u32 instance_count;         // Total number of instances to process (4 bytes)
    u32 frame_number;           // Current frame for temporal throttling (4 bytes)

    Fvector4 frustum_planes[6]; // Frustum planes for culling (96 bytes)
};

// Indirect draw arguments structure (must match D3D11_DRAW_INDEXED_ARGUMENTS)
struct IndirectDrawArgs
{
    u32 index_count;      // Number of indices per instance
    u32 instance_count;   // Number of instances to draw (written by compute shader)
    u32 start_index;      // Base index location
    s32 base_vertex;      // Base vertex location
    u32 start_instance;   // Instance index offset
};

// ===========================
// Compute Manager Class
// ===========================

class DetailComputeManager
{
public:
    DetailComputeManager();
    ~DetailComputeManager();

    // Initialization
    void Initialize(u32 max_instances);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Set geometry info (index count for indirect draw args)
    void SetGeometryInfo(u32 index_count) { m_index_count = index_count; }
    u32 GetIndexCount() const { return m_index_count; }

    // Placement pipeline
    void ResetInstanceAllocator(CBackend& cmd_list);
    void ProcessPlacementTiles(CBackend& cmd_list, const xr_vector<gpu_grass::TileResourceSlice>& tiles);
    void FinalizePlacement(CBackend& cmd_list);
    void UploadDetailObjects(const xr_vector<DetailObjectGPU>& details);

    // Instance management
    void BeginInstanceUpdate();
    void AddInstance(const DetailInstanceGPU& instance);
    void EndInstanceUpdate();
    u32 GetInstanceCount() const { return m_instance_count; }

    // Culling & rendering
    void DispatchCulling(CBackend& cmd_list, const Fmatrix& view_proj);
    void RenderIndirect(CBackend& cmd_list, u32 vis_id);

    // Statistics
    struct Stats
    {
        u32 total_instances;
        u32 culled_instances[3];  // Per vis_id: still, wave1, wave2
        u32 compute_dispatches;
        float cull_time_ms;
    };
    const Stats& GetStats() const { return m_stats; }
    void ResetStats();

    // Debug
    void ReadDebugData();  // Read debug buffer and print culling statistics

private:
    // GPU Resources
    struct GPUResources
    {
        // Instance data buffers
        ID3DBuffer* instance_buffer;                   // All instances (input)
        ID3DShaderResourceView* instance_buffer_srv;   // SRV for reading in compute shader
        ID3DUnorderedAccessView* instance_buffer_uav;  // UAV for placement writes

        ID3DBuffer* visible_indices[3];                // Visible instance indices per vis_id (output)
        ID3DUnorderedAccessView* visible_indices_uav[3]; // UAV for writing
        ID3DShaderResourceView* visible_indices_srv[3];  // SRV for reading during draw

        ID3DBuffer* counter_buffer;                    // Atomic counters for visible instances
        ID3DUnorderedAccessView* counter_buffer_uav;   // UAV for atomic operations

        ID3DBuffer* instance_counter_buffer;           // Global instance counter for placement
        ID3DUnorderedAccessView* instance_counter_uav; // UAV for counter operations
        ID3DBuffer* instance_counter_readback;         // Staging readback

        // Indirect draw arguments
        ID3DBuffer* indirect_args[3];                  // One per vis_id (D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS)
        ID3DUnorderedAccessView* indirect_args_uav[3]; // UAV for compute shader writes

        // Constants
        ConstantBufferHandle cull_params_cb;           // Culling parameters
        ConstantBufferHandle placement_params_cb;      // Placement parameters

        // Compute shader
        ref_cs cull_shader;                            // Frustum culling compute shader
        ref_cs placement_shader;                       // Placement compute shader

        // Detail metadata
        ID3DBuffer* detail_object_buffer;
        ID3DShaderResourceView* detail_object_srv;

        // Staging for readback (debug/stats only)
        void* counter_readback;        // ID3DBuffer* for counter readback staging
        void* indirect_args_readback;  // ID3DBuffer* for indirect args readback staging

        // Debug buffers (optional - for debugging only)
        ID3DBuffer* debug_buffer;                      // Debug output buffer
        ID3DUnorderedAccessView* debug_buffer_uav;     // UAV for writing debug data
        void* debug_readback;                          // ID3DBuffer* for debug readback
    };

    GPUResources m_gpu;

    // CPU-side data
    xr_vector<DetailInstanceGPU> m_instance_staging;  // CPU staging for uploads
    u32 m_instance_count;
    u32 m_max_instances;
    u32 m_index_count;  // Index count for base geometry (for indirect draw args)

    // State
    bool m_initialized;
    bool m_needs_upload;  // Instances changed, need GPU upload

    // Statistics
    Stats m_stats;

    // Internal helpers
    void CreateBuffers(u32 max_instances);
    void DestroyBuffers();
    void UploadInstances(CBackend& cmd_list);
    void CompileShaders();
    void UploadDetailObjectsInternal(const xr_vector<DetailObjectGPU>& details);
    void DispatchPlacement(CBackend& cmd_list, const gpu_grass::TileResourceSlice& tile);
    u32 ReadInstanceCounter(CBackend& cmd_list);
};

// ===========================
// Utility Functions
// ===========================

// Build frustum from view-projection matrix
inline FrustumGPU BuildFrustumGPU(const Fmatrix& view_proj)
{
    FrustumGPU frustum;

    // Extract frustum planes from view-projection matrix
    // Left: row4 + row1
    frustum.planes[0].set(
        view_proj._14 + view_proj._11,
        view_proj._24 + view_proj._21,
        view_proj._34 + view_proj._31,
        view_proj._44 + view_proj._41
    );

    // Right: row4 - row1
    frustum.planes[1].set(
        view_proj._14 - view_proj._11,
        view_proj._24 - view_proj._21,
        view_proj._34 - view_proj._31,
        view_proj._44 - view_proj._41
    );

    // Top: row4 - row2
    frustum.planes[2].set(
        view_proj._14 - view_proj._12,
        view_proj._24 - view_proj._22,
        view_proj._34 - view_proj._32,
        view_proj._44 - view_proj._42
    );

    // Bottom: row4 + row2
    frustum.planes[3].set(
        view_proj._14 + view_proj._12,
        view_proj._24 + view_proj._22,
        view_proj._34 + view_proj._32,
        view_proj._44 + view_proj._42
    );

    // Near: row4 + row3
    frustum.planes[4].set(
        view_proj._14 + view_proj._13,
        view_proj._24 + view_proj._23,
        view_proj._34 + view_proj._33,
        view_proj._44 + view_proj._43
    );

    // Far: row4 - row3
    frustum.planes[5].set(
        view_proj._14 - view_proj._13,
        view_proj._24 - view_proj._23,
        view_proj._34 - view_proj._33,
        view_proj._44 - view_proj._43
    );

    // Normalize planes
    for (int i = 0; i < 6; ++i)
    {
        float len = _sqrt(frustum.planes[i].x * frustum.planes[i].x +
                          frustum.planes[i].y * frustum.planes[i].y +
                          frustum.planes[i].z * frustum.planes[i].z);
        if (len > EPS)
        {
            frustum.planes[i].x /= len;
            frustum.planes[i].y /= len;
            frustum.planes[i].z /= len;
            frustum.planes[i].w /= len;
        }
    }

    return frustum;
}

// Convert CPU SlotItem to GPU instance (implementation in .cpp to avoid circular dependency)
DetailInstanceGPU ConvertToGPUInstance(
    const void* item_ptr,      // CDetailManager::SlotItem*
    u32 object_id,
    const void* detail_ptr,    // CDetail*
    int slot_x,
    int slot_z);

} // namespace xray::render::RENDER_NAMESPACE
