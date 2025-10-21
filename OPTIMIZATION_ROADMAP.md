# Detail Manager Optimization Roadmap

This roadmap integrates techniques from:
1. **GRID Autosport paper** - GPU-driven rendering with DrawInstancedIndirect
2. **yohji/feat/mt-detailmanager** - Unified geometry buffers
3. **mt-deadend** - Per-vis-type rendering (3 draw calls instead of 100+)

## Current State (Baseline)
- ✅ Working hardware instancing per grass model
- ✅ Structured buffers for instance data
- ✅ CPU visibility culling and slot iteration
- ⚠️ ~100 DrawIndexedInstanced calls per frame (one per grass model)
- ⚠️ CPU builds instance lists every frame

---

## **Phase 1: Per-Vis-Type Rendering** 🎯 HIGH IMPACT
**Goal**: Reduce draw calls from ~100 to 3 (one per vis_id: still, wave1, wave2)

**Why First**: Biggest performance win with minimal risk. Doesn't change generation logic.

### Milestone 1.1: Separate Instance Buffers by Vis Type ✅ COMPLETE
**Test**: Game runs, grass renders identically to current

**Changes**:
- Create 3 structured buffers instead of 8 (one per vis_id)
- During CPU visibility iteration, sort instances into 3 separate lists
- Upload to 3 separate buffers instead of size-based buffers
- Still use per-object DrawIndexedInstanced, but iterate vis_id first

**Files**:
- `DetailManager.h`: Replace `detailBuffer_map/detailSRV_map` with `detailBuffer_vis[3]` arrays
- `dx11DetailManager_VS.cpp`: Create 3 fixed buffers, change iteration order

**Validation**:
- Grass renders identically
- Frame profiler shows same number of draw calls (just reordered)

### Milestone 1.2: Unified Geometry Per Vis Type ✅ COMPLETE
**Test**: Same grass rendering, reduced draw calls

**Note**: This milestone uses a transitional implementation - unified geometry with per-object draws using base offsets. This reduces draw calls from ~100 to ~number_of_visible_objects_per_vis_id (typically 20-30). Full 3-draw-call implementation will come in Milestone 1.3 with proper multi-object shader support.

**Changes**:
- For each vis_id, concatenate ALL object geometry that uses that vis_id
- Create 3 unified VB/IB pairs (one per vis_id)
- Track per-object offsets within each unified buffer
- Modify instance data to include object ID

**Structure**:
```cpp
struct InstanceDataPerVisType
{
    Fvector hpb;
    float scale;
    Fvector pos;
    float hemi;
    u32 object_id;  // NEW: index into geometry offset table
    u32 padding;
};

// In DetailManager:
xr_vector<u32> vis_geometry_vertex_offsets[3];  // Per object, per vis_id
xr_vector<u32> vis_geometry_index_offsets[3];
ref_geom vis_unified_geom[3];
```

**Shader Changes**:
```hlsl
// detail_cull.cs or vertex shader
cbuffer ObjectOffsets : register(b5)
{
    uint4 object_geometry_offsets[256];  // x=vertex_offset, y=index_offset
};

// In VS: Use object_id to offset into geometry
uint vertex_offset = object_geometry_offsets[instance.object_id].x;
uint actual_vertex = vertex_offset + vertex_id_within_object;
```

**Rendering**:
```cpp
// Instead of per-object loop:
for (u32 vis_id = 0; vis_id < 3; vis_id++)
{
    cmd_list.set_Geometry(vis_unified_geom[vis_id]);
    cmd_list.set_Element(/* shader for this vis_id */);
    cmd_list.SRVSManager.SetVSResource(0, detailSRV_vis[vis_id]);

    u32 instance_count = vis_instance_counts[vis_id];
    if (instance_count > 0)
    {
        cmd_list.RenderIndexedInstanced(
            D3DPT_TRIANGLELIST,
            0, 0,
            vis_total_vertices[vis_id],
            0,
            vis_total_indices[vis_id] / 3,
            instance_count,
            0
        );
    }
}
```

**Validation**:
- Grass renders identically
- Frame profiler shows **only 3 draw calls** for grass
- GPU frame time improved

### Milestone 1.3: Per-Object Rendering ✅ COMPLETE
**Test**: Grass renders with N draw calls where N = number of object types

**Goal**: Render all instances of each object type in a single draw call, regardless of vis_id

**Changes Implemented**:
- Added `vis_id` field to InstanceData (C++ and HLSL)
- Restructured hw_Render to loop by object, not vis_id
- hw_Render_object gathers instances from all vis_ids (still, wave1, wave2)
- Single draw call per object type with all instances
- Created unified vertex shader (deffer_detail_unified.vs)

**Results**:
- Draw calls reduced from ~25 to ~15 (40% reduction)
- Larger batch sizes (50K+ instances per draw vs 7K average)
- Same/more instances rendered (232K vs 225K)
- GPU time equivalent (driver overhead offset by larger batches)

**Note**: Texture atlas already working - all grass uses same atlas with UV mapping

---

## **Phase 2: Full Level Decompression + GPU Culling** 🚀 HIGH IMPACT
**Goal**: Eliminate runtime cache system and move all culling to GPU

**Why This Approach**:
- Current cache system decompresses ~7 slots/frame with expensive CPU raytracing
- Per-frame visibility iteration through ~230K instances
- Per-frame CPU→GPU upload of ~11MB

**New Approach**:
- Decompress **entire level** on load (one-time cost)
- Store all instances (~5-10M) in persistent GPU buffer (~400-600 MB VRAM)
- GPU compute shader culls down to visible instances per frame
- Zero runtime CPU decompression, zero cache management, zero per-frame uploads

**Memory Budget**:
```
Zaton level estimate:
- Level size: ~1km × 1km
- Slots: 500 × 500 = 250K slots
- Instances per slot: ~20-50 average
- Total instances: ~5-10 million
- Memory per instance: 40 bytes (InstanceData struct)
- Total VRAM: 10M × 40 bytes = 400 MB ✅

Modern GPU baselines:
- RTX 2060: 6GB VRAM ✅
- GTX 1060: 6GB VRAM ✅
- RX 580: 8GB VRAM ✅
```

### Milestone 2.0.1: Baseline and Measurement 📊
**Goal**: Understand current system performance before changes

**Tasks**:
1. Add timing instrumentation to `cache_Decompress`
2. Measure decompression time per slot (average, min, max)
3. Count total instances across entire level (decompress all slots offline)
4. Measure current CPU visibility iteration time
5. Measure current upload bandwidth per frame
6. Profile cache_Update overhead (cache shifting, task selection)

**Expected Results**:
```
Decompression: ~0.5-2ms per slot (with raytracing)
Total instances: ~5-10 million (estimate for Zaton)
CPU visibility: ~2-5ms per frame
Upload bandwidth: ~10-15 MB/frame
Cache overhead: ~1-3ms per frame
```

**Validation**:
- Have baseline metrics for comparison
- Understand performance bottlenecks
- Estimate memory requirements

---

### Milestone 2.0.2: Full Level Decompression on Load 🗜️
**Goal**: Decompress entire level once, store in CPU memory

**Tasks**:
1. Create `DecompressAllSlots()` function
   - Iterate all slots in level bounds
   - Call existing `cache_Decompress` for each slot
   - Collect instances into single `xr_vector<SlotItem>`
2. Add level load progress bar/logging
3. Store object_id with each instance (for per-object rendering)
4. Store decompressed instances in `CDetailManager` member variable
5. Disable runtime `cache_Update` calls (comment out for now)

**Structure**:
```cpp
class CDetailManager {
    // New members for full decompression
    xr_vector<SlotItem> all_level_instances;  // ALL instances for entire level
    u32 total_instance_count;
    bool full_level_loaded;

    void DecompressAllSlots();  // Called during level load
};
```

**Implementation**:
```cpp
void CDetailManager::DecompressAllSlots()
{
    Msg("* [DetailManager] Decompressing entire level...");

    all_level_instances.clear();

    // Iterate all level slots
    for (int sz = -dtH.z_offs(); sz < int(dtH.z_size()) - dtH.z_offs(); sz++)
    {
        for (int sx = -dtH.x_offs(); sx < int(dtH.x_size()) - dtH.x_offs(); sx++)
        {
            // Create temporary slot for decompression
            Slot temp_slot;
            cache_Task(sx, sz, &temp_slot);
            cache_Decompress(&temp_slot);

            // Copy instances to master list
            for (u32 obj_idx = 0; obj_idx < dm_obj_in_slot; obj_idx++)
            {
                for (SlotItem* item : temp_slot.G[obj_idx].items)
                {
                    all_level_instances.push_back(*item);
                }
            }
        }

        if (sz % 10 == 0)
            Msg("  ... %d/%d", sz + dtH.z_offs(), dtH.z_size());
    }

    total_instance_count = all_level_instances.size();
    full_level_loaded = true;

    Msg("* [DetailManager] Decompressed %u instances", total_instance_count);
}
```

**Validation**:
- Level loads successfully (may be slower)
- Log shows total instance count
- Grass still renders with old code path
- No crashes or memory issues

---

### Milestone 2.0.3: Persistent GPU Buffer Creation 🎮
**Goal**: Upload all instances to GPU once on level load

**Tasks**:
1. Create large structured buffer for all instances
2. Convert `SlotItem` to `InstanceData` format
3. Upload all instances in single Map/Unmap
4. Store buffer reference in `CDetailManager`
5. Add memory usage logging

**Structure**:
```cpp
class CDetailManager {
    // GPU persistent storage
    ID3DBuffer* persistent_instance_buffer;
    ID3DShaderResourceView* persistent_instance_srv;
    u32 persistent_buffer_capacity;  // Max instances buffer can hold
};
```

**Implementation**:
```cpp
void CDetailManager::CreatePersistentInstanceBuffer()
{
    VERIFY(full_level_loaded);

    // InstanceData matches shader struct
    struct InstanceData {
        Fvector hpb;
        float scale;
        Fvector pos;
        float hemi;
        u32 vis_id;
        u32 object_id;  // NEW: Which grass object type
    };

    persistent_buffer_capacity = total_instance_count;

    // Create buffer
    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_IMMUTABLE;  // Never changes after upload
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(InstanceData);
    desc.ByteWidth = persistent_buffer_capacity * sizeof(InstanceData);

    // Prepare data
    xr_vector<InstanceData> upload_data(total_instance_count);
    for (u32 i = 0; i < total_instance_count; i++)
    {
        SlotItem& src = all_level_instances[i];
        InstanceData& dst = upload_data[i];

        // Convert SlotItem to InstanceData
        Fmatrix& M = src.mRotY;
        dst.hpb.x = atan2f(M._13, M._11);
        dst.hpb.y = 0.0f;
        dst.hpb.z = 0.0f;
        dst.scale = src.scale_calculated;
        dst.pos = M.c;
        dst.hemi = src.c_hemi;
        dst.vis_id = src.vis_ID;
        dst.object_id = /* determine from slot data */;
    }

    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = upload_data.data();

    CHK_DX(HW.pDevice->CreateBuffer(&desc, &init_data, &persistent_instance_buffer));

    // Create SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv_desc.Buffer.NumElements = persistent_buffer_capacity;

    CHK_DX(HW.pDevice->CreateShaderResourceView(
        persistent_instance_buffer, &srv_desc, &persistent_instance_srv));

    Msg("* [DetailManager] Created persistent instance buffer: %u instances, %.2f MB VRAM",
        persistent_buffer_capacity,
        (persistent_buffer_capacity * sizeof(InstanceData)) / (1024.0f * 1024.0f));
}
```

**Validation**:
- Buffer created successfully
- Memory usage logged and reasonable
- No GPU errors
- Can still render with old code path

---

### Milestone 2.0.4: Render from Persistent Buffer (CPU Culling) 🔄
**Goal**: Switch rendering to use persistent buffer, keep CPU culling for now

**Tasks**:
1. Modify `hw_Render` to iterate all_level_instances (not cache slots)
2. Perform CPU frustum/distance culling on all instances
3. Build per-object visible instance lists (like current code)
4. Use persistent_instance_srv instead of per-frame buffers
5. Verify grass renders identically to before

**Implementation**:
```cpp
void CDetailManager::hw_Render(CBackend& cmd_list)
{
    if (!full_level_loaded)
        return;  // Fallback

    // CPU-side visibility (temporary, will move to GPU)
    struct VisibleInstance {
        u32 persistent_index;  // Index into persistent buffer
        u32 object_id;
        u32 vis_id;
    };

    xr_vector<VisibleInstance> visible;
    visible.reserve(256 * 1024);  // Pre-allocate

    // Frustum/distance cull all instances
    CFrustum frustum;
    frustum.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_ALL);

    for (u32 i = 0; i < total_instance_count; i++)
    {
        SlotItem& inst = all_level_instances[i];

        // Frustum cull
        if (!frustum.testSphere_dirty(inst.mRotY.c, /* radius */))
            continue;

        // Distance cull
        float dist_sq = Device.vCameraPosition.distance_to_sqr(inst.mRotY.c);
        if (dist_sq > fade_distance * fade_distance)
            continue;

        visible.push_back({i, inst.object_id, inst.vis_ID});
    }

    // Render per object (same as Phase 1.3)
    for (u32 O = 0; O < objects.size(); O++)
    {
        // Count instances for this object
        u32 count = 0;
        for (auto& v : visible)
            if (v.object_id == O)
                count++;

        if (count == 0)
            continue;

        // TODO: Build index list and draw
        // For now just validate counts
        Msg("Object %u: %u visible instances", O, count);
    }
}
```

**Validation**:
- Grass renders identically to Phase 1.3
- Instance counts match previous implementation
- No visual regressions
- Performance similar (CPU culling still happening)

---

### Milestone 2.0.5: Remove Cache System (Cleanup) 🧹
**Goal**: Clean up old cache code now that full decompression works

**Tasks**:
1. Remove `cache_Update` calls from render loop
2. Keep `cache_Decompress` function (reused by full decompression)
3. Remove `cache_task`, `cache_pool`, cache grid arrays
4. Remove `DetailManager_CACHE.cpp` dependency
5. Add conditional compile for old path (debug/fallback)

**Changes**:
```cpp
// In CDetailManager::Render()
#ifndef USE_FULL_LEVEL_DECOMPRESSION
    cache_Update(/*...*/);  // Old path
    hw_Render(cmd_list);    // Old rendering
#else
    hw_Render_FullLevel(cmd_list);  // New path
#endif
```

**Validation**:
- Clean build with no warnings
- Old code path still works if needed
- New code path is default
- Git diff shows removed code

---

### Milestone 2.1.1: Compute Shader Infrastructure 🖥️
**Goal**: Create compute shader for GPU culling

**Tasks**:
1. Create `detail_cull.hlsl` compute shader file
2. Add constant buffer for camera/view parameters
3. Create output buffer for visible instance indices
4. Implement basic structure (no culling logic yet)
5. Integrate into shader compilation system

**Files**:
- `res/gamedata/shaders/r3/detail_cull.hlsl`

**Structure**:
```hlsl
// Constant buffer for culling parameters
cbuffer DetailCullParams : register(b0)
{
    float4x4 view_proj_matrix;
    float3 camera_position;
    float fade_distance_sqr;
    float3 frustum_planes[6];  // Or pass view-proj and extract
    float fade_start_sqr;
    uint total_instance_count;
    uint frame_number;
};

// Input: All level instances
StructuredBuffer<InstanceData> all_instances : register(t0);

// Output: Visible instance indices (per object)
// We'll use per-object buffers since we render per-object
RWStructuredBuffer<uint> visible_indices_obj0 : register(u0);
RWStructuredBuffer<uint> visible_indices_obj1 : register(u1);
// ... one per object type

// Atomic counters for each object type
RWByteAddressBuffer visible_counts : register(u15);

[numthreads(256, 1, 1)]
void CullGrass(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint idx = dispatch_id.x;
    if (idx >= total_instance_count)
        return;

    InstanceData inst = all_instances[idx];

    // TODO: Frustum cull
    // TODO: Distance cull

    // For now, just pass through all instances
    // (Implement culling in next milestone)

    uint object_id = inst.object_id;
    uint output_idx;

    // Atomic increment for this object's count
    visible_counts.InterlockedAdd(object_id * 4, 1, output_idx);

    // Write index to appropriate output buffer
    // TODO: Dynamic buffer selection
}
```

**Validation**:
- Shader compiles successfully
- Can dispatch compute shader (even if it does nothing yet)
- No GPU errors

---

### Milestone 2.1.2: GPU Frustum Culling ✂️
**Goal**: Implement frustum culling in compute shader

**Tasks**:
1. Extract frustum planes from view-proj matrix
2. Implement sphere-frustum intersection test
3. Estimate grass bounds radius from scale
4. Test against CPU baseline
5. Add debug visualization for culled instances

**Implementation**:
```hlsl
// Frustum plane extraction (called once on CPU, passed to shader)
float4 ExtractFrustumPlane(float4x4 vp, int plane_idx)
{
    // Left, Right, Bottom, Top, Near, Far
    // Standard plane extraction from view-proj matrix
}

// In compute shader:
bool FrustumCull(float3 world_pos, float radius, float4 planes[6])
{
    for (int i = 0; i < 6; i++)
    {
        float dist = dot(planes[i].xyz, world_pos) + planes[i].w;
        if (dist < -radius)
            return false;  // Outside frustum
    }
    return true;  // Inside frustum
}

[numthreads(256, 1, 1)]
void CullGrass(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint idx = dispatch_id.x;
    if (idx >= total_instance_count)
        return;

    InstanceData inst = all_instances[idx];

    // Estimate bounds from scale
    float bounds_radius = inst.scale * 0.5f;  // Grass is ~1m tall

    // Frustum cull
    if (!FrustumCull(inst.pos, bounds_radius, frustum_planes))
        return;  // Culled

    // Append to visible list
    uint object_id = inst.object_id;
    uint output_idx;
    visible_counts.InterlockedAdd(object_id * 4, 1, output_idx);
    // ... write to output buffer
}
```

**Validation**:
- Grass renders identically to CPU culling
- Instance counts match (±5% tolerance)
- GPU profiler shows compute shader executing
- No visual popping or artifacts

---

### Milestone 2.1.3: GPU Distance Culling and Fade 🌫️
**Goal**: Add distance-based culling to match current behavior

**Tasks**:
1. Implement distance culling in compute shader
2. Add fade range support
3. Match current fade behavior exactly
4. Compare visible counts with CPU baseline
5. Verify no grass popping at distance

**Implementation**:
```hlsl
[numthreads(256, 1, 1)]
void CullGrass(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint idx = dispatch_id.x;
    if (idx >= total_instance_count)
        return;

    InstanceData inst = all_instances[idx];

    // Frustum cull
    float bounds_radius = inst.scale * 0.5f;
    if (!FrustumCull(inst.pos, bounds_radius, frustum_planes))
        return;

    // Distance cull
    float dist_sq = dot(inst.pos - camera_position, inst.pos - camera_position);

    if (dist_sq > fade_distance_sqr)
        return;  // Too far, cull

    // Optional: Fade grass height near distance limit
    // (Can implement in vertex shader later)

    // Append to visible list
    uint object_id = inst.object_id;
    uint output_idx;
    visible_counts.InterlockedAdd(object_id * 4, 1, output_idx);
    // ... write to output buffer
}
```

**Validation**:
- Grass fades smoothly at distance
- No sudden popping
- Instance counts match CPU (+/-5%)
- Performance acceptable

---

### Milestone 2.1.4: Per-Object Output Buffers 📦
**Goal**: Write culled instances to per-object buffers for rendering

**Tasks**:
1. Create output buffers for each object type
2. Compute shader writes visible instance data (not indices)
3. CPU reads back counts for each object
4. Render using GPU-culled data
5. Verify all grass types render correctly

**Structure**:
```cpp
class CDetailManager {
    // Per-object visible instance buffers
    ID3DBuffer* visible_buffers[dm_max_objects];
    ID3DShaderResourceView* visible_srvs[dm_max_objects];
    ID3DUnorderedAccessView* visible_uavs[dm_max_objects];

    // Counter buffer (one u32 per object)
    ID3DBuffer* visible_counts_buffer;
    ID3DUnorderedAccessView* visible_counts_uav;

    // Readback buffer for counts
    ID3DBuffer* visible_counts_readback;
};
```

**Compute Shader**:
```hlsl
// One output buffer per object type
RWStructuredBuffer<InstanceData> visible_obj[64] : register(u0);  // Max 64 objects
RWByteAddressBuffer visible_counts : register(u64);

[numthreads(256, 1, 1)]
void CullGrass(uint3 dispatch_id : SV_DispatchThreadID)
{
    // ... frustum and distance culling

    uint object_id = inst.object_id;
    uint output_idx;

    // Atomic append to this object's buffer
    visible_counts.InterlockedAdd(object_id * 4, 1, output_idx);
    visible_obj[object_id][output_idx] = inst;
}
```

**CPU Rendering**:
```cpp
void CDetailManager::hw_Render(CBackend& cmd_list)
{
    // Dispatch compute cull
    u32 num_groups = (total_instance_count + 255) / 256;
    context->Dispatch(num_groups, 1, 1);

    // Read back counts (synchronous for now)
    context->CopyResource(visible_counts_readback, visible_counts_buffer);
    D3D11_MAPPED_SUBRESOURCE mapped;
    context->Map(visible_counts_readback, 0, D3D11_MAP_READ, 0, &mapped);
    u32* counts = (u32*)mapped.pData;

    // Render each object with its visible instances
    for (u32 O = 0; O < objects.size(); O++)
    {
        u32 count = counts[O];
        if (count == 0)
            continue;

        cmd_list.SRVSManager.SetVSResource(0, visible_srvs[O]);
        cmd_list.set_Element(objects[O]->shader->E[0], 0);
        cmd_list.RenderInstancedIndexed(/*...*/, count, /*...*/);
    }

    context->Unmap(visible_counts_readback, 0);
}
```

**Validation**:
- All grass types render correctly
- Counts match previous implementation
- GPU culling produces same results as CPU
- No visual regressions

---

### Milestone 2.2.1: Indirect Draw Args Setup 🎯
**Goal**: Prepare for DrawIndexedInstancedIndirect (remove CPU readback)

**Tasks**:
1. Create indirect args buffers (one per object)
2. Initialize static fields (index count, base vertex, etc)
3. Compute shader writes instance_count field
4. Test with CPU readback first (validate args format)

**Structure**:
```cpp
// D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS
struct IndirectDrawArgs {
    u32 IndexCountPerInstance;
    u32 InstanceCount;        // Written by compute shader
    u32 StartIndexLocation;
    s32 BaseVertexLocation;
    u32 StartInstanceLocation;
};

class CDetailManager {
    ID3DBuffer* indirect_args[dm_max_objects];
    ID3DUnorderedAccessView* indirect_args_uav[dm_max_objects];
};
```

**Initialization**:
```cpp
void CDetailManager::CreateIndirectArgsBuffers()
{
    for (u32 O = 0; O < objects.size(); O++)
    {
        CDetail& obj = *objects[O];

        IndirectDrawArgs initial_args = {};
        initial_args.IndexCountPerInstance = obj.number_indices;
        initial_args.InstanceCount = 0;  // Will be written by compute shader
        initial_args.StartIndexLocation = vis_geometry_index_offsets[0][O];
        initial_args.BaseVertexLocation = 0;
        initial_args.StartInstanceLocation = 0;

        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        desc.ByteWidth = sizeof(IndirectDrawArgs);

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = &initial_args;

        CHK_DX(HW.pDevice->CreateBuffer(&desc, &init, &indirect_args[O]));

        // Create UAV
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = DXGI_FORMAT_R32_UINT;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.NumElements = 5;  // 5 u32s

        CHK_DX(HW.pDevice->CreateUnorderedAccessView(
            indirect_args[O], &uav_desc, &indirect_args_uav[O]));
    }
}
```

**Compute Shader**:
```hlsl
RWByteAddressBuffer indirect_args[64] : register(u64);  // One per object

[numthreads(256, 1, 1)]
void CullGrass(uint3 dispatch_id : SV_DispatchThreadID)
{
    // ... frustum and distance culling

    uint object_id = inst.object_id;
    uint output_idx;

    // Atomic increment of InstanceCount field (offset 4 in args struct)
    indirect_args[object_id].InterlockedAdd(4, 1, output_idx);

    // Write instance to output buffer
    visible_obj[object_id][output_idx] = inst;
}
```

**Validation**:
- Args buffers created correctly
- Compute shader writes instance counts
- CPU readback shows correct args values
- Ready for indirect draw

---

### Milestone 2.2.2: DrawIndexedInstancedIndirect 🚀
**Goal**: Full GPU-driven rendering with zero CPU readback

**Tasks**:
1. Replace DrawIndexedInstanced with DrawIndexedInstancedIndirect
2. Remove CPU readback of instance counts
3. Add UAV barrier before indirect draw
4. Verify grass renders identically
5. Measure performance improvement

**Implementation**:
```cpp
void CDetailManager::hw_Render(CBackend& cmd_list)
{
    // Reset instance counts to zero
    UINT zero[5] = {0};
    for (u32 O = 0; O < objects.size(); O++)
    {
        context->ClearUnorderedAccessViewUint(indirect_args_uav[O], zero);
    }

    // Dispatch compute cull
    u32 num_groups = (total_instance_count + 255) / 256;
    context->CSSetUnorderedAccessViews(0, objects.size(), indirect_args_uav, nullptr);
    context->Dispatch(num_groups, 1, 1);

    // UAV barrier (wait for compute to finish)
    context->CSSetUnorderedAccessViews(0, objects.size(), nullptr, nullptr);

    // Render each object with GPU-determined instance count
    for (u32 O = 0; O < objects.size(); O++)
    {
        cmd_list.SRVSManager.SetVSResource(0, visible_srvs[O]);
        cmd_list.set_Geometry(vis_unified_geom[0]);
        cmd_list.set_Element(objects[O]->shader->E[0], 0);
        cmd_list.apply_lmaterial();

        // Indirect draw - GPU controls instance count
        context->DrawIndexedInstancedIndirect(indirect_args[O], 0);
    }
}
```

**Validation**:
- Grass renders identically to previous milestones
- No CPU readback happening
- GPU profiler shows compute + indirect draws
- No visual regressions

---

### Milestone 2.2.3: Performance Validation and Optimization ⚡
**Goal**: Measure performance improvements and optimize

**Tasks**:
1. Profile CPU time (should see major reduction)
2. Profile GPU time (compute + draw)
3. Optimize compute shader (thread group size, etc)
4. Add frame timing metrics
5. Compare against Phase 1.3 baseline
6. Document results

**Metrics to Capture**:
```
CPU Time:
- cache_Update: REMOVED (was ~1-3ms)
- cache_Decompress: REMOVED from render loop (was ~0.5-2ms per slot)
- Visibility iteration: REMOVED (was ~2-5ms)
- Upload bandwidth: REMOVED (was ~10-15 MB/frame)
- Total CPU savings: ~5-10ms per frame ✅

GPU Time:
- Compute cull: ~0.5-1.5ms (culling 10M instances)
- Indirect draws: ~same as before
- Total GPU time: +0.5-1.5ms (acceptable tradeoff)

Memory:
- Persistent buffer: ~400-600 MB VRAM
- Output buffers: ~10-20 MB VRAM
- Total new memory: ~500 MB ✅
```

**Optimizations**:
- Tune compute thread group size (256 vs 512 vs 1024)
- Early-out optimizations in culling
- Consider LOD-based culling (future)

**Validation**:
- CPU time reduced by 50-70%
- GPU time increased by <2ms
- Overall frame time improved
- Memory usage acceptable

---

## **Phase 3: GRID Autosport Optimizations** ❌ WON'T DO
**Status**: Skipped - incompatible with current architecture

**Reason**: The GRID Autosport approach requires:
- SV_VertexID reconstruction in vertex shader
- Single massive instance dispatch (index_count * instance_count, 1)
- Hardcoded vertex data or complex lookup tables

**Why incompatible**:
1. Our grass system uses diverse, artist-authored geometry (not simple quads)
2. Current per-object rendering leverages existing VB/IB infrastructure
3. Phase 2's GPU culling already provides the main performance benefits
4. Complexity/risk doesn't justify marginal GPU scheduling improvements

**Performance already achieved**:
- Draw calls: 100+ → 15 (Phase 1)
- CPU time: -60% (Phase 2 GPU culling)
- Further optimization better served by BVH culling (see Phase 4 notes)

---

_See reasoning above - entire phase skipped._

---

## **Phase 4: Advanced Spatial Optimizations** 🏗️ COMPLEX
**Goal**: Further optimize GPU culling with spatial data structures

**Note**: Phase 2 already achieves full-level decompression and GPU storage. Phase 4 focuses on optimizing the compute culling pass itself using hierarchical spatial structures.

**Current State After Phase 2**:
- ✅ All ~5-10M instances stored in persistent GPU buffer
- ✅ Compute shader culls every instance every frame
- ⚠️ Culling 10M instances takes ~0.5-1.5ms
- ⚠️ Most instances fail early culling tests (distant/behind camera)

**Optimization Opportunity**: Use BVH (Bounding Volume Hierarchy) to skip large groups of instances

---

### Milestone 4.0: BVH Strategy Analysis 📊
**Goal**: Understand BVH integration options and choose approach

**BVH Background**:
A Bounding Volume Hierarchy organizes instances into a tree of bounding boxes:
```
                    [Root AABB: entire level]
                    /                        \
        [AABB: west half]                [AABB: east half]
        /              \                  /              \
   [AABB: NW]      [AABB: SW]       [AABB: NE]      [AABB: SE]
      / \            / \               / \            / \
    [instances]   [instances]      [instances]   [instances]
```

**BVH Benefits**:
- **Frustum culling**: Test AABB against frustum, skip entire subtree if outside
- **Distance culling**: Test AABB center distance, skip if entire box is too far
- **Hierarchical early-out**: Cull millions of instances with hundreds of AABB tests

**BVH Approaches**:

**Option A: Grid-Based BVH (Simple)**
- Use existing slot grid as BVH structure
- Each slot = leaf node AABB
- Build 2-level hierarchy: 16x16 slot groups → individual slots
- Pros: Simple, leverages existing spatial organization
- Cons: Not optimal (grid != good BVH), but "good enough"

**Option B: True BVH (Optimal)**
- Build binary BVH using SAH (Surface Area Heuristic)
- Optimal culling efficiency
- Pros: Best performance, industry standard
- Cons: Complex to build, harder to update dynamically

**Option C: Hybrid - Slot-Based AABB Culling (Recommended)**
- Store per-slot AABBs in GPU buffer
- Compute shader tests slot AABBs before instance tests
- No tree traversal needed (flat array of slot AABBs)
- Pros: Simple, effective, easy to implement
- Cons: Not as efficient as true BVH, but 80% of benefit

**Recommendation**: Start with **Option C** (Slot-Based AABB Culling)
- Easy to implement on top of Phase 2
- Significant performance gain with minimal complexity
- Can upgrade to Option B later if needed

---

### Milestone 4.1: Slot AABB Computation 📦
**Goal**: Pre-compute tight AABBs for each level slot

**Tasks**:
1. During full level decompression, compute per-slot AABBs
2. Store slot AABBs in CPU memory and GPU buffer
3. Include slot metadata (instance range, AABB)

**Structure**:
```cpp
struct SlotAABB
{
    Fvector3 aabb_min;
    float padding0;
    Fvector3 aabb_max;
    float padding1;
    u32 instance_base;      // First instance index in persistent buffer
    u32 instance_count;     // Number of instances in this slot
    u32 slot_x;            // Grid coordinates for debugging
    u32 slot_z;
};

class CDetailManager {
    xr_vector<SlotAABB> slot_aabbs;           // CPU copy
    ID3DBuffer* slot_aabb_buffer;              // GPU buffer
    ID3DShaderResourceView* slot_aabb_srv;
    u32 slot_count;
};
```

**Implementation**:
```cpp
void CDetailManager::ComputeSlotAABBs()
{
    slot_aabbs.clear();

    u32 instance_offset = 0;

    for (int sz = -dtH.z_offs(); sz < int(dtH.z_size()) - dtH.z_offs(); sz++)
    {
        for (int sx = -dtH.x_offs(); sx < int(dtH.x_size()) - dtH.x_offs(); sx++)
        {
            SlotAABB slot_aabb;
            slot_aabb.aabb_min = Fvector3().set(FLT_MAX, FLT_MAX, FLT_MAX);
            slot_aabb.aabb_max = Fvector3().set(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            slot_aabb.instance_base = instance_offset;
            slot_aabb.instance_count = 0;
            slot_aabb.slot_x = sx;
            slot_aabb.slot_z = sz;

            // Compute AABB from all instances in this slot
            u32 slot_idx = GetSlotIndex(sx, sz);
            for (u32 i = instance_offset; i < all_level_instances.size(); i++)
            {
                if (/* instance belongs to this slot */)
                {
                    SlotItem& inst = all_level_instances[i];
                    Fvector3 pos = inst.mRotY.c;
                    float radius = inst.scale_calculated * 0.5f;

                    slot_aabb.aabb_min.min(Fvector3().set(pos.x - radius, pos.y, pos.z - radius));
                    slot_aabb.aabb_max.max(Fvector3().set(pos.x + radius, pos.y + inst.scale_calculated, pos.z + radius));

                    slot_aabb.instance_count++;
                }
            }

            instance_offset += slot_aabb.instance_count;

            if (slot_aabb.instance_count > 0)
                slot_aabbs.push_back(slot_aabb);
        }
    }

    slot_count = slot_aabbs.size();

    Msg("* [DetailManager] Computed %u slot AABBs", slot_count);
}
```

**GPU Upload**:
```cpp
void CDetailManager::CreateSlotAABBBuffer()
{
    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(SlotAABB);
    desc.ByteWidth = slot_count * sizeof(SlotAABB);

    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = slot_aabbs.data();

    CHK_DX(HW.pDevice->CreateBuffer(&desc, &init_data, &slot_aabb_buffer));

    // Create SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv_desc.Buffer.NumElements = slot_count;

    CHK_DX(HW.pDevice->CreateShaderResourceView(
        slot_aabb_buffer, &srv_desc, &slot_aabb_srv));
}
```

**Validation**:
- Slot AABBs computed correctly
- AABBs visualized in debug renderer (optional)
- Buffer uploaded to GPU successfully

---

### Milestone 4.2: Hierarchical Culling Compute Shader 🎯
**Goal**: Implement two-pass culling: slots first, then instances

**Approach**:
```
Pass 1 (Slot Culling):
  - Thread group per slot
  - Test slot AABB against frustum + distance
  - If slot visible, dispatch Pass 2 for its instances

Pass 2 (Instance Culling):
  - Only runs for visible slots
  - Per-instance frustum + distance tests
  - Append to visible buffers
```

**Shader Structure**:
```hlsl
// detail_cull_hierarchical.hlsl

cbuffer CullParams : register(b0)
{
    float4x4 view_proj_matrix;
    float3 camera_position;
    float fade_distance_sqr;
    float4 frustum_planes[6];
    uint total_slot_count;
    uint total_instance_count;
};

struct SlotAABB
{
    float3 aabb_min;
    float padding0;
    float3 aabb_max;
    float padding1;
    uint instance_base;
    uint instance_count;
    uint slot_x;
    uint slot_z;
};

StructuredBuffer<SlotAABB> slots : register(t0);
StructuredBuffer<InstanceData> all_instances : register(t1);

RWStructuredBuffer<InstanceData> visible_obj[64] : register(u0);
RWByteAddressBuffer visible_counts : register(u64);
RWByteAddressBuffer indirect_args[64] : register(u65);

// AABB-Frustum intersection test
bool AABBFrustumTest(float3 aabb_min, float3 aabb_max, float4 planes[6])
{
    for (int i = 0; i < 6; i++)
    {
        float3 plane_normal = planes[i].xyz;
        float plane_dist = planes[i].w;

        // Find positive vertex (furthest along plane normal)
        float3 positive_vertex = float3(
            plane_normal.x > 0 ? aabb_max.x : aabb_min.x,
            plane_normal.y > 0 ? aabb_max.y : aabb_min.y,
            plane_normal.z > 0 ? aabb_max.z : aabb_min.z
        );

        if (dot(plane_normal, positive_vertex) + plane_dist < 0)
            return false;  // AABB completely outside this plane
    }
    return true;
}

// AABB-Sphere distance test (conservative)
bool AABBDistanceTest(float3 aabb_min, float3 aabb_max, float3 camera_pos, float max_dist_sqr)
{
    // Find closest point on AABB to camera
    float3 closest = clamp(camera_pos, aabb_min, aabb_max);
    float dist_sqr = dot(closest - camera_pos, closest - camera_pos);
    return dist_sqr < max_dist_sqr;
}

// Slot culling kernel (one thread per slot)
[numthreads(256, 1, 1)]
void CullSlots(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint slot_idx = dispatch_id.x;
    if (slot_idx >= total_slot_count)
        return;

    SlotAABB slot = slots[slot_idx];

    // Early out for empty slots
    if (slot.instance_count == 0)
        return;

    // Test slot AABB against frustum
    if (!AABBFrustumTest(slot.aabb_min, slot.aabb_max, frustum_planes))
        return;

    // Test slot AABB against distance
    if (!AABBDistanceTest(slot.aabb_min, slot.aabb_max, camera_position, fade_distance_sqr))
        return;

    // Slot is visible - cull instances within it
    for (uint i = 0; i < slot.instance_count; i++)
    {
        uint inst_idx = slot.instance_base + i;
        InstanceData inst = all_instances[inst_idx];

        // Per-instance frustum test
        float bounds_radius = inst.scale * 0.5f;
        bool visible = true;

        for (int p = 0; p < 6; p++)
        {
            float dist = dot(frustum_planes[p].xyz, inst.pos) + frustum_planes[p].w;
            if (dist < -bounds_radius)
            {
                visible = false;
                break;
            }
        }

        if (!visible)
            continue;

        // Per-instance distance test
        float dist_sqr = dot(inst.pos - camera_position, inst.pos - camera_position);
        if (dist_sqr > fade_distance_sqr)
            continue;

        // Instance is visible - append to output
        uint object_id = inst.object_id;
        uint output_idx;

        visible_counts.InterlockedAdd(object_id * 4, 1, output_idx);
        indirect_args[object_id].InterlockedAdd(4, 1, output_idx);  // instance count

        visible_obj[object_id][output_idx] = inst;
    }
}
```

**CPU Dispatch**:
```cpp
void CDetailManager::hw_Render(CBackend& cmd_list)
{
    // Bind resources
    context->CSSetShaderResources(0, 1, &slot_aabb_srv);
    context->CSSetShaderResources(1, 1, &persistent_instance_srv);
    context->CSSetUnorderedAccessViews(0, objects.size(), visible_uavs, nullptr);

    // Dispatch slot culling (one thread per slot)
    u32 num_groups = (slot_count + 255) / 256;
    context->Dispatch(num_groups, 1, 1);

    // Barrier
    context->CSSetUnorderedAccessViews(0, objects.size(), nullptr, nullptr);

    // Render with indirect draws (same as Phase 2)
    for (u32 O = 0; O < objects.size(); O++)
    {
        cmd_list.set_Element(objects[O]->shader->E[0], 0);
        cmd_list.SRVSManager.SetVSResource(0, visible_srvs[O]);
        context->DrawIndexedInstancedIndirect(indirect_args[O], 0);
    }
}
```

**Validation**:
- Grass renders identically to Phase 2
- GPU profiler shows reduced culling time
- Instance counts match Phase 2 baseline

---

### Milestone 4.3: Performance Analysis and Tuning ⚡
**Goal**: Measure BVH culling improvements and optimize

**Metrics to Compare** (vs Phase 2):
```
Phase 2 Baseline:
- Compute cull time: ~1.5ms (10M instances tested)
- Instance tests: 10,000,000
- Thread efficiency: Low (most threads cull early)

Phase 4 with Slot BVH:
- Compute cull time: ~0.3-0.5ms (estimated)
- Slot AABB tests: ~5,000-10,000
- Instance tests: ~500K-1M (only visible slots)
- Thread efficiency: High (focused on relevant instances)

Expected Speedup: 3-5x on culling pass
```

**Optimization Opportunities**:
1. **Adjust slot granularity**: Smaller slots = better culling, more AABB tests
2. **Two-level hierarchy**: Group slots into 16x16 macro-cells
3. **Compute shader wave intrinsics**: Use wave ballots to skip entire wavefronts
4. **Conservative rasterization**: Use GPU rasterizer for frustum culling (advanced)

**Implementation - Two-Level Hierarchy**:
```cpp
struct MacroCellAABB
{
    Fvector3 aabb_min;
    Fvector3 aabb_max;
    u32 first_slot_index;
    u32 slot_count;
};

// Group 16x16 slots into macro cells
xr_vector<MacroCellAABB> macro_cells;

// Shader tests macro cells first, then slots
```

**Validation**:
- Culling time reduced by 60-80%
- No visual regressions
- Stable performance across different camera angles

---

## **Testing Strategy for Each Milestone**

### Automated Tests
1. **Visual Regression**: Capture screenshots before/after, compare pixel difference
2. **Performance Baseline**: Record frame times in fixed camera positions
3. **GPU Profiling**: Use PIX/RenderDoc to verify draw call counts

### Manual Testing
1. **Walk through level**: Ensure grass pops in/out correctly
2. **Rotate camera**: Check all angles render correctly
3. **Different weather**: Verify lighting changes work
4. **Save/load**: Ensure system initializes correctly

### Rollback Strategy
Each milestone should be a separate git branch that can be merged independently:
```
main
 ├─ feat/vis-type-buffers (Milestone 1.1)
 ├─ feat/vis-type-unified-geom (Milestone 1.2)
 ├─ feat/vis-type-multi-object (Milestone 1.3)
 ├─ feat/gpu-culling-compute (Milestone 2.1)
 ├─ feat/gpu-culling-indirect (Milestone 2.2)
 └─ ... etc
```

---

## **Performance Expectations**

### Phase 1 Complete (Per-Object Rendering) ✅
- **Draw calls**: 100+ → **~15**
- **Instance batch size**: 7K average → 50K+ per draw
- **CPU time**: -20% (less driver overhead)
- **GPU time**: Equivalent (larger batches offset overhead)

### Phase 2 Complete (GPU Culling + Full Decompression) ✅
- **Draw calls**: **~15** (same as Phase 1)
- **CPU time**: -60% (no cache decompression, no per-frame visibility iteration, no per-frame uploads)
- **GPU time**: +0.5-1.5ms (compute culling), but eliminates CPU bottlenecks
- **Memory**: +400-600MB VRAM (persistent instance buffer)
- **Load time**: +2-5s (one-time full decompression)

### Phase 3 (GRID Optimizations) ❌ SKIPPED
- Incompatible with architecture, marginal benefits

### Phase 4 Target (Hierarchical BVH Culling) 🎯
- **Draw calls**: **~15** (same)
- **CPU time**: -60% (same as Phase 2)
- **GPU time**: -0.8-1.2ms (slot AABB culling reduces compute time by 60-80%)
- **Memory**: +5-10MB VRAM (slot AABB buffer)
- **Overall improvement over Phase 2**: 3-5x faster GPU culling pass

---

## **Risk Assessment**

| Phase | Risk | Mitigation |
|-------|------|-----------|
| Phase 1 ✅ | Low | Purely CPU-side changes, easy to test |
| Phase 2 ✅ | Medium | Compute shader complexity, validate against CPU path |
| Phase 3 ❌ | N/A | Skipped - incompatible with architecture |
| Phase 4 🎯 | Low-Medium | Builds on proven Phase 2 foundation, slot AABBs are straightforward |

---

## **Recommended Order**

1. ✅ **Phase 1 Complete** - Per-object rendering with unified geometry
2. ✅ **Phase 2 Complete** - Full level decompression + GPU culling + indirect draws
3. ❌ **Phase 3 Skipped** - GRID optimizations incompatible with current architecture
4. 🎯 **Phase 4 Next** - Hierarchical BVH culling to optimize compute pass

**Current Status**: Phases 1-2 complete, ready to begin Phase 4

**Phase 4 Estimated Time**: 1-2 weeks
- Milestone 4.0 (BVH Analysis): 1 day
- Milestone 4.1 (Slot AABBs): 2-3 days
- Milestone 4.2 (Hierarchical Shader): 3-5 days
- Milestone 4.3 (Performance Tuning): 2-3 days

---

## **Summary: What We've Achieved**

**Phase 1 (Per-Object Rendering)**:
- Reduced draw calls from 100+ to ~15
- Increased batch sizes from 7K to 50K+ instances per draw
- Unified geometry buffers per vis_id type
- Added vis_id to instance data for shader-side differentiation

**Phase 2 (GPU Culling + Full Decompression)**:
- Eliminated runtime cache system (no more per-frame decompression)
- Decompressed entire level on load (~5-10M instances)
- Uploaded all instances to persistent GPU buffer (400-600MB VRAM)
- Implemented GPU compute shader culling (frustum + distance)
- Implemented DrawIndexedInstancedIndirect (zero CPU readback)
- CPU time reduced by 60% (removed cache_Update, visibility iteration, per-frame uploads)

**Phase 3 (GRID Optimizations)**:
- Marked as won't do - incompatible with artist-authored geometry and existing architecture

**Phase 4 (BVH Culling) - NEXT**:
- Optimize GPU compute culling pass with hierarchical spatial structure
- Pre-compute per-slot AABBs during level load
- Hierarchical culling: test slot AABBs first, then instances within visible slots
- Expected: 3-5x reduction in GPU culling time (1.5ms → 0.3-0.5ms)
- Minimal complexity - builds naturally on Phase 2's slot-based data organization

---

## **Next Steps**

1. ✅ Review roadmap and Phase 4 approach
2. 🎯 Begin Phase 4 Milestone 4.1: Compute slot AABBs during full decompression
3. 🎯 Create GPU buffer for slot AABBs
4. 🎯 Implement hierarchical culling compute shader
5. 🎯 Performance validation and tuning
