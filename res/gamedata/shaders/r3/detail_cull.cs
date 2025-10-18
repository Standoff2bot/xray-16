// detail_cull.cs - GPU-driven frustum culling for detail objects
// Processes all detail instances and outputs visible indices for indirect drawing
//
#define SM_5_0
#include "common.h"

// ===========================
// Structures (must match C++)
// ===========================

struct DetailInstanceGPU
{
    // Transform data (32 bytes)
    float3 position;        // 12 bytes
    float scale;            // 4 bytes
    float rotation_y;       // 4 bytes
    float3 padding0;        // 12 bytes = 32 total

    // Rendering data (32 bytes)
    float c_hemi;
    float c_sun;
    uint object_id;
    uint vis_id;
    float3 color_rgb;
    float padding1;

    // Bounding data (32 bytes)
    float3 bounds_min;
    float bounds_radius;
    float3 bounds_max;
    float padding2;

    // Metadata (16 bytes)
    uint slot_x;
    uint slot_z;
    uint flags;
    float fade_distance_sqr;
};

struct FrustumPlane
{
    float4 plane;  // xyz = normal, w = distance
};

struct IndirectDrawArgs
{
    uint index_count;
    uint instance_count;
    uint start_index;
    int base_vertex;
    uint start_instance;
};

// ===========================
// Input Buffers
// ===========================

cbuffer CullParams : register(b0)
{
    float3 g_camera_pos;
    float g_fade_limit_sqr;

    float3 g_camera_dir;
    float g_fade_start_sqr;

    float g_r_ssa_discard;
    float g_r_ssa_cheap;
    uint g_instance_count;
    uint g_frame_number;
    uint g_instance_base;
    uint g_tile_instance_count;
    uint g_padding0;
    uint g_padding1;

    float4 g_frustum_planes[6];  // Left, Right, Top, Bottom, Near, Far
};

// All instances (input, read-only)
StructuredBuffer<DetailInstanceGPU> g_instances : register(t0);

// ===========================
// Output Buffers
// ===========================

// Visible instance indices per vis_id (output, append)
RWStructuredBuffer<uint> g_visible_still : register(u0);     // vis_id = 0
RWStructuredBuffer<uint> g_visible_wave1 : register(u1);     // vis_id = 1
RWStructuredBuffer<uint> g_visible_wave2 : register(u2);     // vis_id = 2

// Atomic counters for each vis_id
RWStructuredBuffer<uint> g_counters : register(u3);  // [0]=still, [1]=wave1, [2]=wave2

// Indirect draw arguments per vis_id (RAW buffers for DrawIndexedInstancedIndirect)
// Must use RWByteAddressBuffer because DX11 doesn't allow DRAWINDIRECT_ARGS + STRUCTURED flags
RWByteAddressBuffer g_indirect_args_still : register(u4);
RWByteAddressBuffer g_indirect_args_wave1 : register(u5);
RWByteAddressBuffer g_indirect_args_wave2 : register(u6);

// ===========================
// Culling Functions
// ===========================

// Test if a sphere intersects or is inside a frustum plane
bool SphereInsidePlane(float3 center, float radius, float4 plane)
{
    float distance = dot(plane.xyz, center) + plane.w;
    return distance > -radius;
}

// Frustum culling: test sphere against all 6 planes
bool FrustumCullSphere(float3 center, float radius)
{
    [unroll]
    for (uint i = 0; i < 6; ++i)
    {
        if (!SphereInsidePlane(center, radius, g_frustum_planes[i]))
            return false;  // Outside frustum
    }
    return true;  // Inside or intersecting
}

// Compute Screen Space Area (SSA) for LOD selection
float ComputeSSA(float3 world_pos, float radius, float scale)
{
    float dist_sqr = dot(world_pos - g_camera_pos, world_pos - g_camera_pos);
    if (dist_sqr < 0.001f)
        return 1e6f;  // Very close, assume visible

    // SSA = (scale * radius)^2 / distance^2
    float scaled_radius = scale * radius;
    return (scaled_radius * scaled_radius) / dist_sqr;
}

// ===========================
// Compute Shader Entry Point
// ===========================

[numthreads(256, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID)
{
    uint instance_idx = dispatch_thread_id.x;

    if (instance_idx >= g_tile_instance_count)
        return;

    uint instance_global_idx = g_instance_base + instance_idx;

    GroupMemoryBarrierWithGroupSync();

    DetailInstanceGPU inst = g_instances[instance_global_idx];

    // =========================
    // Distance Culling
    // =========================
    float3 to_camera = g_camera_pos - inst.position;
    float dist_sqr = dot(to_camera, to_camera);

    // Fade limit culling
    if (dist_sqr > g_fade_limit_sqr)
        return;  // Too far, cull

    // =========================
    // Frustum Culling
    // =========================
    // Use bounding sphere for conservative culling
    float world_radius = inst.bounds_radius * inst.scale;
    if (!FrustumCullSphere(inst.position, world_radius))
        return;  // Outside frustum, cull

    // =========================
    // SSA (Screen Space Area) Culling
    // =========================
    float ssa = ComputeSSA(inst.position, inst.bounds_radius, inst.scale);
    if (ssa < g_r_ssa_discard)
        return;  // Too small, cull

    // =========================
    // Fade Factor (for future use)
    // =========================
    float fade_alpha = 1.0f;
    if (dist_sqr > g_fade_start_sqr)
    {
        float fade_range = g_fade_limit_sqr - g_fade_start_sqr;
        fade_alpha = 1.0f - ((dist_sqr - g_fade_start_sqr) / fade_range);
    }

    // =========================
    // Append to Visible List
    // =========================
    uint output_idx = 0;

    // Determine which output buffer based on vis_id
    if (inst.vis_id == 0)
    {
        // Still (no animation)
        InterlockedAdd(g_counters[0], 1, output_idx);
        g_visible_still[output_idx] = instance_global_idx;

        // Update indirect draw instance count (offset 4 = instance_count field)
        uint original_value;
        g_indirect_args_still.InterlockedAdd(4, 1, original_value);
    }
    else if (inst.vis_id == 1)
    {
        // Wave 1
        InterlockedAdd(g_counters[1], 1, output_idx);
        g_visible_wave1[output_idx] = instance_global_idx;

        // Update indirect draw instance count (offset 4 = instance_count field)
        uint original_value;
        g_indirect_args_wave1.InterlockedAdd(4, 1, original_value);
    }
    else // inst.vis_id == 2
    {
        // Wave 2
        InterlockedAdd(g_counters[2], 1, output_idx);
        g_visible_wave2[output_idx] = instance_global_idx;

        // Update indirect draw instance count (offset 4 = instance_count field)
        uint original_value;
        g_indirect_args_wave2.InterlockedAdd(4, 1, original_value);
    }
}
