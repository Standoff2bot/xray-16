struct PlacementSeedGPU
{
    uint slot_x;
    uint slot_z;
    uint base_seed;
    uint object_mask_low;
    uint object_mask_high;
    float density_scale;
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

RWStructuredBuffer<DetailInstanceGPU> g_instances : register(u0);
RWStructuredBuffer<uint>             g_instance_counter : register(u1);

StructuredBuffer<PlacementSeedGPU>   g_seeds         : register(t0);
StructuredBuffer<SlotReferenceGPU>   g_slot_refs     : register(t1);
StructuredBuffer<SlotHeightGPU>      g_slot_heights  : register(t2);
StructuredBuffer<DetailObjectGPU>    g_detail_objects: register(t3);

cbuffer PlacementParams : register(b0)
{
    uint instance_offset;
    uint slot_offset;
    uint slot_count;
    uint max_instances;
    float slot_size;
    float pad0;
    float pad1;
    float pad2;
};

uint SelectDetailObject(uint lowMask, uint highMask)
{
    if (lowMask != 0)
        return firstbitlow(lowMask);
    if (highMask != 0)
        return firstbitlow(highMask) + 32;
    return 0xFFFFFFFF;
}

[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint index = dtid.x;
    if (index >= slot_count)
        return;

    const PlacementSeedGPU seed = g_seeds[slot_offset + index];
    const SlotHeightGPU height = g_slot_heights[slot_offset + index];

    const uint object_index = SelectDetailObject(seed.object_mask_low, seed.object_mask_high);
    if (object_index == 0xFFFFFFFFu || object_index >= g_detail_objects.Length)
        return;

    uint counter = 0;
    InterlockedAdd(g_instance_counter[0], 1, counter);
    counter += instance_offset;
    if (counter >= max_instances)
        return;

    const DetailObjectGPU obj = g_detail_objects[object_index];

    DetailInstanceGPU inst;
    inst.position = float3(seed.slot_x * slot_size + slot_size * 0.5f, height.min_height, seed.slot_z * slot_size + slot_size * 0.5f);
    inst.scale = obj.min_scale;
    inst.rotation_y = 0.0f;
    inst.padding0 = float3(0.0f, 0.0f, 0.0f);

    inst.c_hemi = 1.0f;
    inst.c_sun = 1.0f;
    inst.object_id = object_index;
    inst.vis_id = obj.base_vis_id;
    inst.color_rgb = float3(1.0f, 1.0f, 1.0f);
    inst.padding1 = 0.0f;

    inst.bounds_min = obj.bbox_min;
    inst.bounds_radius = obj.radius;
    inst.bounds_max = obj.bbox_max;
    inst.padding2 = 0.0f;

    inst.slot_x = seed.slot_x;
    inst.slot_z = seed.slot_z;
    inst.flags = obj.flags;
    inst.fade_distance_sqr = 0.0f;

    g_instances[counter] = inst;
}
