// lod_gpu.vs - GPU instanced grass rendering
// Uses compute shader culled instance list for indirect drawing

// NOTE: Define vertex input BEFORE including common.h to avoid conflicts
// Simple base vertex format (one grass blade geometry) - 32 bytes total
// IMPORTANT: Semantic indices must match C++ D3D11_INPUT_ELEMENT_DESC exactly!
struct vv
{
	float3 pos		: POSITION0;	// Local space position (12 bytes) - offset 0
	float3 normal	: NORMAL0;		// Local space normal (12 bytes) - offset 12
	float2 tc		: TEXCOORD0;	// Texture coordinates (8 bytes) - offset 24
};

#include "common.h"

float4x4 m_view;
float4x4 m_project;
float4x4 m_viewproject; 

// Instance data structure (matches C++ DetailInstanceGPU - 112 bytes)
struct DetailInstanceGPU
{
    // Transform data (32 bytes)
    float3 position;        // 12 bytes
    float scale;            // 4 bytes
    float rotation_y;       // 4 bytes
    float3 padding0;        // 12 bytes

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

// Output to pixel shader - matches p_flat struct
struct vf
{
	float2	tc		: TEXCOORD0;	// Texture coordinates (matches p_flat.tcdh)
	float4	position: TEXCOORD1;	// World position + hemi (matches p_flat.position)
	float3	N		: TEXCOORD2;	// World-space normal (matches p_flat.N)
	float4 	hpos	: SV_Position;
};

// Shader resources
StructuredBuffer<uint> g_visible_indices : register(t0);		// Visible instance indices
StructuredBuffer<DetailInstanceGPU> g_instances : register(t1);	// All instance data

#define L_SCALE (2.0h*1.55h)

vf main(vv I, uint instance_id : SV_InstanceID)
{
	vf o;

	// Read visible instance index
	uint visible_idx = g_visible_indices[instance_id];

	// Load instance data
	DetailInstanceGPU inst = g_instances[visible_idx];

	// Build rotation matrix (Y-axis rotation)
	float cos_y = cos(inst.rotation_y);
	float sin_y = sin(inst.rotation_y);
	float3x3 rot_matrix = float3x3(
		cos_y,  0.0, sin_y,
		0.0,    1.0, 0.0,
		-sin_y, 0.0, cos_y
	);

	// Transform vertex: rotate, scale, translate
	float3 local_pos = mul(rot_matrix, I.pos * inst.scale);
	float4 world_pos = float4(local_pos + inst.position, 1.0);

	// Transform normal to world space
	float3 world_normal = normalize(mul(rot_matrix, I.normal));

	// Output transformed position
	o.hpos = mul(m_VP, world_pos);

	// Position in view space (for pixel shader)
	float3 view_pos = mul(m_V, world_pos);

	// Texture coordinates
	o.tc = I.tc;

	// Lighting (use instance data)
	float h = inst.c_hemi * L_SCALE;

	// Pack position + hemi into TEXCOORD1 (matches p_flat.position)
	o.position = float4(view_pos, h);

	// Transform normal to view space (matches p_flat.N expectation)
	o.N = normalize(mul((float3x3)m_V, world_normal));

	return o;
}
FXVS;
