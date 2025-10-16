// lod_gpu.vs - GPU instanced grass rendering
// Uses compute shader culled instance list for indirect drawing
#include "common.h"

// Simple base vertex format (one grass blade geometry)
struct vv
{
	float3 pos		: POSITION;		// Local space position
	float3 normal	: NORMAL;		// Local space normal
	float2 tc		: TEXCOORD0;	// Texture coordinates
};

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

// Output to pixel shader
struct vf
{
	float3	Pe		: TEXCOORD0;
 	float2 	tc		: TEXCOORD1;
	float4 	af		: COLOR1;		// alpha&factor
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

	// Transform normal
	float3 world_normal = normalize(mul(rot_matrix, I.normal));

	// Output transformed position
	o.hpos = mul(m_VP, world_pos);
	o.Pe = mul(m_V, world_pos);

	// Texture coordinates
	o.tc = I.tc;

	// Lighting (use instance data)
	float h = inst.c_hemi * L_SCALE;
	float sun = inst.c_sun;
	float alpha = 1.0; // TODO: compute alpha from distance

	o.af = float4(h, h, alpha, 0.5); // factor = 0.5 for mid-LOD

	return o;
}
FXVS;
