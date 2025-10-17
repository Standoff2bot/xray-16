struct PlacementSeedGPU
{
    int slot_x;
    int slot_z;
    uint base_seed;
    uint object_mask_low;
    uint object_mask_high;
    float density_scale;
    float c_hemi;
    float c_sun;
    float world_base_x;
    float world_base_z;
    float pad0;
    float pad1;
};

struct SlotReferenceGPU
{
    uint slot_index;
    uint slot_local_x;
    uint slot_local_z;
    uint pad;
};

struct SlotHeightGPU
{
    float min_height;
    float max_height;
};

struct DetailObjectGPU
{
    float3 bbox_min;
    float min_scale;
    float3 bbox_max;
    float max_scale;
    float radius;
    uint flags;
    uint base_vis_id;
    uint padding;
};

struct DetailInstanceGPU
{
    float3 position;
    float scale;
    float rotation_y;
    float3 padding0;

    float c_hemi;
    float c_sun;
    uint object_id;
    uint vis_id;

    float3 color_rgb;
    float padding1;

    float3 bounds_min;
    float bounds_radius;

    float3 bounds_max;
    float padding2;

    uint slot_x;
    uint slot_z;
    uint flags;
    float fade_distance_sqr;
};

StructuredBuffer<PlacementSeedGPU> g_seeds           : register(t0);
StructuredBuffer<SlotReferenceGPU> g_slot_refs       : register(t1);
StructuredBuffer<SlotHeightGPU>    g_slot_heights    : register(t2);
StructuredBuffer<DetailObjectGPU>  g_detail_objects  : register(t3);
StructuredBuffer<uint4>            g_slot_object_ids : register(t4);
StructuredBuffer<float4>           g_palette_layers  : register(t5);
StructuredBuffer<float>            g_height_samples  : register(t6);

RWStructuredBuffer<DetailInstanceGPU> g_instances : register(u0);
RWStructuredBuffer<uint>             g_instance_counter : register(u1);

cbuffer PlacementParams : register(b0)
{
    uint instance_offset;
    uint slot_offset;
    uint slot_count;
    uint tile_span;
    uint samples_per_slot;
    uint sample_dim;
    uint max_instances;
    uint pad0;
    float slot_size;
    float density;
    float jitter_amplitude;
    float detail_height_scale;
    float tile_origin_x;
    float tile_origin_z;
    float invalid_height_value;
    float pad1;
};

static const uint DitherMatrix[16][16] =
{
    {  0, 192,  48, 240, 128,  64, 176, 112,  32, 224,  16, 208, 160,  96, 144,  80 },
    { 128,  64, 176, 112,   0, 192,  48, 240, 160,  96, 144,  80,  32, 224,  16, 208 },
    {  32, 224,  16, 208, 160,  96, 144,  80,   0, 192,  48, 240, 128,  64, 176, 112 },
    { 160,  96, 144,  80,  32, 224,  16, 208, 128,  64, 176, 112,   0, 192,  48, 240 },
    {  48, 240,   0, 192, 176, 112, 128,  64,  16, 208,  32, 224, 144,  80, 160,  96 },
    { 176, 112, 128,  64,  48, 240,   0, 192, 144,  80, 160,  96,  16, 208,  32, 224 },
    {  16, 208,  32, 224, 144,  80, 160,  96,  48, 240,   0, 192, 176, 112, 128,  64 },
    { 144,  80, 160,  96,  16, 208,  32, 224, 176, 112, 128,  64,  48, 240,   0, 192 },
    {  64, 176, 112, 128, 192,   0, 240,  48,  96, 160,  80, 144, 224,  32, 208,  16 },
    { 192,   0, 240,  48,  64, 176, 112, 128, 224,  32, 208,  16,  96, 160,  80, 144 },
    {  96, 160,  80, 144, 224,  32, 208,  16,  64, 176, 112, 128, 192,   0, 240,  48 },
    { 224,  32, 208,  16,  96, 160,  80, 144, 192,   0, 240,  48,  64, 176, 112, 128 },
    { 112, 128,  64, 176, 240,  48, 192,   0,  80, 144,  96, 160, 208,  16, 224,  32 },
    { 240,  48, 192,   0, 112, 128,  64, 176, 208,  16, 224,  32,  80, 144,  96, 160 },
    {  80, 144,  96, 160, 208,  16, 224,  32, 112, 128,  64, 176, 240,  48, 192,   0 },
    { 208,  16, 224,  32,  80, 144,  96, 160, 240,  48, 192,   0, 112, 128,  64, 176 }
};

uint Hash(uint a, uint b, uint c, uint seed)
{
    uint h = seed;
    h ^= a * 1664525u;
    h ^= b * 1013904223u;
    h ^= c * 2246822519u;
    h ^= (h >> 13);
    h *= 1274126177u;
    h ^= (h >> 16);
    return h;
}

float Random01(uint h)
{
    return (h & 0x00FFFFFFu) / 16777216.0f;
}

float RandomSigned(uint h, float amplitude)
{
    return (Random01(h) * 2.0f - 1.0f) * amplitude;
}

float InterpolateAlpha(float4 alpha, float fx, float fz)
{
    float a0 = alpha.x;
    float a1 = alpha.y;
    float a2 = alpha.z;
    float a3 = alpha.w;

    float c01 = lerp(a0, a1, fx);
    float c23 = lerp(a2, a3, fx);
    return lerp(c01, c23, fz);
}

[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint thread_index = dtid.x;
    const uint total_samples = (samples_per_slot > 0u) ? slot_count * samples_per_slot : slot_count;
    if (thread_index >= total_samples)
        return;

    const uint slot_index = thread_index / samples_per_slot;
    const uint sample_index = thread_index % samples_per_slot;

    const PlacementSeedGPU seed = g_seeds[slot_offset + slot_index];
    const SlotReferenceGPU slot_ref = g_slot_refs[slot_offset + slot_index];
    const SlotHeightGPU slot_height = g_slot_heights[slot_offset + slot_index];
    const uint4 slot_objects = g_slot_object_ids[slot_offset + slot_index];
    const uint palette_base = (slot_offset + slot_index) * 4u;

    const uint sample_dim_m1 = max(sample_dim - 1u, 1u);
    const uint sample_x = sample_index % sample_dim;
    const uint sample_z = sample_index / sample_dim;

    const float fx = (sample_dim > 1u) ? float(sample_x) / float(sample_dim_m1) : 0.0f;
    const float fz = (sample_dim > 1u) ? float(sample_z) / float(sample_dim_m1) : 0.0f;

    const uint base_hash = Hash((uint)seed.slot_x, (uint)seed.slot_z, sample_index, seed.base_seed);
    const uint shift_hash = Hash(base_hash, 1u, 0u, seed.base_seed ^ 0xB5297A4Du);
    const uint jitter_hash = Hash(base_hash, 2u, 0u, seed.base_seed ^ 0x68E31DA4u);

    const uint shift_x = shift_hash & 15u;
    const uint shift_z = (shift_hash >> 4) & 15u;

    const float jitter_x = RandomSigned(jitter_hash, jitter_amplitude);
    const float jitter_z = RandomSigned(Hash(jitter_hash, 3u, 0u, seed.base_seed ^ 0x1B56C4E9u), jitter_amplitude);

    uint selected_layers[4];
    uint selected_objects[4];
    uint selected_count = 0;

    [unroll]
    for (uint layer = 0; layer < 4; ++layer)
    {
        uint object_id = slot_objects[layer];
        if (object_id == 0x3Fu || object_id == 0xFFFFFFFFu)
            continue;

        float4 alpha = g_palette_layers[palette_base + layer];
        float value = InterpolateAlpha(alpha, fx, fz) * 255.0f + 0.5f;
        float threshold = float(DitherMatrix[(sample_z + shift_z) & 15u][(sample_x + shift_x) & 15u]);

        if (value > threshold)
        {
            selected_layers[selected_count] = layer;
            selected_objects[selected_count] = object_id;
            ++selected_count;
        }
    }

    if (selected_count == 0)
        return;

    const uint choice_hash = Hash(base_hash, 4u, 0u, seed.base_seed ^ 0x9E3779B9u);
    const uint chosen_index = (selected_count > 1u) ? choice_hash % selected_count : 0u;
    const uint chosen_layer = selected_layers[chosen_index];
    const uint object_id = selected_objects[chosen_index];

    if (object_id >= g_detail_objects.Length)
        return;

    const DetailObjectGPU detail_obj = g_detail_objects[object_id];

    const float world_base_x = seed.world_base_x;
    const float world_base_z = seed.world_base_z;
    const float world_x = world_base_x + fx * slot_size + jitter_x;
    const float world_z = world_base_z + fz * slot_size + jitter_z;

    const float local_tile_x = world_x - tile_origin_x;
    const float local_tile_z = world_z - tile_origin_z;

    float height = slot_height.min_height;
    if (samples_per_slot > 0u && g_height_samples.Length > 0)
    {
        const uint sample_offset = (slot_offset + slot_index) * samples_per_slot + sample_index;
        const float encoded_height = g_height_samples[sample_offset];
        if (encoded_height > invalid_height_value)
            height = encoded_height;
    }

    uint counter = 0;
    InterlockedAdd(g_instance_counter[0], 1u, counter);
    counter += instance_offset;
    if (counter >= max_instances)
        return;

    const uint scale_hash = Hash(base_hash, 5u, 0u, seed.base_seed ^ 0xC2B2AE35u);
    const float scale_random = Random01(scale_hash);
    const float instance_scale = lerp(detail_obj.min_scale * 0.5f, detail_obj.max_scale * 0.9f, scale_random) * detail_height_scale;

    const float rotation = Random01(Hash(scale_hash, 6u, 0u, seed.base_seed ^ 0x6C8E9CF1u)) * 6.28318530718f;

    DetailInstanceGPU inst;
    inst.position = float3(world_x, height, world_z);
    inst.scale = instance_scale;
    inst.rotation_y = rotation;
    inst.padding0 = float3(0.0f, 0.0f, 0.0f);

    inst.c_hemi = seed.c_hemi;
    inst.c_sun = seed.c_sun;
    inst.object_id = object_id;
    inst.vis_id = detail_obj.base_vis_id;
    inst.color_rgb = float3(1.0f, 1.0f, 1.0f);
    inst.padding1 = 0.0f;

    inst.bounds_min = detail_obj.bbox_min * instance_scale;
    inst.bounds_radius = detail_obj.radius * instance_scale;
    inst.bounds_max = detail_obj.bbox_max * instance_scale;
    inst.padding2 = 0.0f;

    inst.slot_x = seed.slot_x;
    inst.slot_z = seed.slot_z;
    inst.flags = detail_obj.flags;
    inst.fade_distance_sqr = 0.0f;

    g_instances[counter] = inst;
}
