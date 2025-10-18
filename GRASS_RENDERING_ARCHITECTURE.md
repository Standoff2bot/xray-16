# GPU-Driven Grass Rendering Architecture

## 1. Objectives
- Eliminate CPU-side decompression and per-frame instance rebuilds.
- Stream grass data entirely on the GPU using clipmap-aligned resources.
- Maintain determinism with the legacy cache output while unlocking modern effects (wind, interaction, LOD).
- Provide a migration path that lets us validate parity before retiring `DetailManager_Decompress`.

## 2. Legacy Summary (for Context)
- CPU decompresses `DetailSlot` blocks, performs palette dithering, stochastic selection, per-instance jitter, and triangle-ray height refinement (`DetailManager_Decompress.cpp`).
- `BuildGPUInstanceList()` walks every cached slot and rewrites the instance staging buffer whenever the cache dirties.
- `DetailComputeManager::UploadInstances()` calls `UpdateSubresource` for the entire buffer, even if one slot changes.
- GPU path currently only performs frustum/SSA culling and indirect draws.

## 3. Design Pillars
1. **Offline-first data** – Bake clipmap-ready slot payloads, material masks, and height/normal clipmaps so the runtime never raycasts.
2. **Sparse residency** – Track regions→tiles→cells with per-tile versioning; only touched tiles trigger GPU work.
3. **All-GPU expansion** – Compute shaders fully reproduce CPU placement logic and append into persistent buffers.
4. **GPU orchestration** – Culling, sorting, and indirect draw emission stay on the device; CPU just issues high-level commands.
5. **Deterministic parity** – Hash seeds and palette evaluation exactly match CPU behavior to allow side-by-side validation.

## 4. Offline Bake Pipeline
The bake step exports GPU-ready assets per level.

```
Assets/
 ├─ grass_slots.bin          // Compressed slot records (flat array, sorted by clipmap tile)
 ├─ grass_slot_index.bin     // Region/tile → offset/count
 ├─ grass_height_clipmap.dds // MIP stack of height (R16F) + normal (RG16F)
 ├─ grass_object_info.bin    // Static blade metadata (scale range, bounds, wind params, texture ids)
 └─ grass_masks.dds          // Material mask atlases driving procedural rejection
```

```cpp
struct OfflineSlotRecord
{
    uint16_t height_min_cm;     // floor cm from world zero
    uint16_t height_range_cm;   // height span
    uint8_t  palette[4][4];     // packed 4x4 weights (0..15)
    uint8_t  object_id[4];      // detail ids or 0x3F for empty
    uint32_t rand_seed;         // seed (derived from world slot coords)
};

struct OfflineTileHeader
{
    uint32_t slot_offset;   // start index in grass_slots.bin
    uint16_t slot_count;
    uint16_t version;       // increments each bake – copied into runtime residency table
};
```

**Bake Steps**
1. Rasterize terrain/collision geometry into a high-resolution height+normal atlas.
2. For each detail slot, evaluate palette masks, apply noise dithering, and store packed data into `OfflineSlotRecord`.
3. Build region→tile→cell indirection tables (clipmap-friendly 32×32 tiles, 8×8 cells per tile).
4. Emit object metadata with bounds, materials, wind parameters, and swaying presets.
5. Serialize precomputed blue-noise, interaction masks, and optional SDFs for bending.

## 5. Runtime High-Level Flow
1. **Camera update** issues desired clipmap region to `GrassResidencyManager`.
2. Residency manager resolves visible tiles, checks version numbers, schedules GPU jobs for dirty tiles.
3. For each dirty tile:
   - Upload compressed slot payload (if not resident) into a ring-buffered `StructuredBuffer<OfflineSlotRecord>`.
   - Dispatch `cs_grass_decompress` to expand slots into the persistent instance buffer using append UAVs.
   - Update tile metadata (start index/count) in a GPU-visible table.
4. Issue global culling (`detail_cull.cs`) re-using existing pipeline, extended with cell metadata.
5. Submit indirect draws for still/wave1/wave2 (and optional height-LOD impostors).

## 6. Residency & Metadata

```cpp
struct GrassRegion
{
    int2 origin_slot;       // world-space slot origin (clipmap-aligned)
    uint32_t lod;           // clipmap level (0 = highest detail)
};

struct GrassTile
{
    uint32_t slot_offset;   // into compressed slot buffer
    uint16_t slot_count;
    uint16_t version;       // matches bake version

    uint32_t instance_offset; // base index in persistent instance buffer
    uint32_t instance_count;
    uint32_t last_touched_frame;
};

struct GrassResidencyTable
{
    StaticArray<GrassTile, MAX_TILES_PER_LOD> tiles;
    Bitset residency_mask;
    RingQueue<uint32_t> upload_queue;   // tile ids needing GPU expansion
};
```

CPU only mutates `upload_queue`, `residency_mask`, and the sparse upload staging buffer. Everything else is written by compute shaders using UAV counters.

## 7. GPU Buffers

```cpp
// Compressed data (read-only)
StructuredBuffer<OfflineSlotRecord> g_slots;
ByteAddressBuffer                 g_slot_offsets; // tile id → {offset,count}

// Height & normal clipmap
Texture2DArray<float4> g_height_clipmap; // R=height, G,B=encoded normal, A=unused or mask

// Persistent outputs
RWStructuredBuffer<DetailInstanceGPU> g_instances;
AppendStructuredBuffer<uint>         g_free_list;      // optional compaction
RWStructuredBuffer<GrassTileState>   g_tile_state;     // mirrors GrassTile instance offsets/counts
```

## 8. `cs_grass_decompress` Overview

```hlsl
[numthreads(8, 8, 1)]
void cs_grass_decompress(uint3 dispatch_id : SV_DispatchThreadID,
                         uint  tile_id     : SV_GroupID.x)
{
    // Load tile header
    TileHeader header = g_tile_headers[tile_id];

    // Each group handles one slot; each thread handles one candidate instance inside slot
    uint slot_index  = header.slot_offset + dispatch_id.y;
    OfflineSlotRecord slot = g_slots[slot_index];

    uint local_idx = dispatch_id.x;
    uint2 lattice  = IndexToLattice(local_idx); // 4×4 placement samples or blue-noise set

    // Recreate deterministic seed
    uint seed = Hash(slot.rand_seed, lattice);

    // Palette rejection
    if (!PaletteAccept(slot.palette, lattice, seed))
        return;

    // Sample heightmap (clipmap selection derived from tile lod)
    float2 worldXZ = SlotToWorldXZ(header.region_origin, slot_index, lattice);
    float height   = SampleHeight(worldXZ);
    float3 normal  = DecodeNormal(worldXZ);

    // Reject if below water / invalid material mask
    if (!MaterialAllowsGrass(worldXZ, normal))
        return;

    // Compose instance
    DetailInstanceGPU inst;
    inst.position = float3(worldXZ.x, height, worldXZ.y);
    inst.scale    = RandomScale(slot.object_id, seed);
    inst.rotation_y = RandomRotation(seed);
    inst.object_id  = SelectObject(slot.object_id, seed);
    inst.c_hemi = PrecomputedHemi(inst.position);
    inst.c_sun  = PrecomputedSun(inst.position, normal);
    inst.flags  = EncodeFlags(tile_id, lod);

    // Append
    uint write_index;
    InterlockedAdd(g_instance_counter[0], 1, write_index);
    g_instances[write_index] = inst;

    // Update tile stats (one thread per slot commits)
    if (local_idx == 0)
        g_tile_state[tile_id].instance_count += CountGenerated(local_idx);
}
```

**Notes**
- Shader must replicate CPU jitter/selection logic (same hash seeds, dithering patterns).
- Height sampling uses clipmap coordinates; failing that we can fall back to CPU-provided min/max until clipmap is ready.
- Tile instance ranges (`instance_offset`/`instance_count`) live in `g_tile_state`, written atomically per tile.

## 9. Culling & Rendering
Enhance existing `detail_cull.cs` pipeline:
- Fetch tile metadata to quickly reject empty or inactive tiles (`g_tile_state[tile_id].instance_count == 0`).
- Optionally integrate Hierarchical-Z (Hi-Z) culling by sampling the scene depth pyramid.
- Add per-instance wind phase and interaction texture lookups.

```cpp
struct DetailInstanceGPU
{
    float3 position;
    float  scale;
    float  rotation_y;
    float  hemi;
    float  sun;
    uint   object_id;
    uint   vis_id;
    float3 color_rgb;
    float  wind_seed;
    float3 bounds_min;
    float  bounds_radius;
    float3 bounds_max;
    uint   flags;              // bits: tile id, interaction mask id, lod
    float  fade_distance_sqr;
};
```

Indirect draw remains three buckets (still / wave1 / wave2). Additional draws (billboards, impostors) can be appended by writing extra indirect argument buffers during culling.

## 10. CPU Scheduling
- Residency updates run on a job thread, driven by camera velocity and look-ahead distance.
- Upload staging respects bandwidth budgets (e.g., max 4 tiles per frame).
- Interaction system writes impulses into an R16 texture that GPU shaders sample; updates happen via async compute.
- All GPU jobs (decompress, cull, interaction) are scheduled on async queues when available; fences sync before render.

## 11. Interaction & Wind Extensions
1. **Interaction**
   - Maintain an `RWTexture2D<float>` per clipmap LOD receiving player/projectile impulses.
   - Fade via compute pass (`cs_interaction_decay`) every frame.
   - Instance shader samples the appropriate texel using tile/slot metadata.
2. **Wind**
   - Precompute FBM wind fields into 3D textures; animate by scrolling offsets.
   - Each instance stores a seed for gust phase; vertex shader samples wind field to modulate bending.

## 12. Migration Plan
1. **Parity Stage**
   - Bake new data but run GPU decompression in parallel with legacy CPU path.
   - Compare per-slot instance counts and positions within tolerance (debug overlay / checksum).
2. **Hybrid Stage**
   - Enable GPU decompression + residency; retain CPU fallback for unsupported hardware.
   - Keep CPU path for validation toggles (`r_detail_gpu_pipeline 0/1`).
3. **Full GPU Stage**
   - Remove CPU cache updates, convert `DetailManager` to orchestrator of residency + compute dispatch.
   - Deprecate `cache_Decompress`, `BuildGPUInstanceList`, and legacy batching.

## 13. Risk Mitigation
- Store hash seeds & palette parameters verbatim to guarantee deterministic generation.
- Keep instrumentation buffers (per-tile counts, perf timestamps) for diagnosing load spikes.
- Build tooling to visualize clipmap residency and instance densities in-editor.
- Provide graceful degradation (simplified impostors) for GPUs lacking append/consume support.

---

*This document defines the target architecture for the grass system. Next steps: implement the offline bake prototype, scaffold the residency manager API, and author the first iteration of `cs_grass_decompress` using the current compressed slot format.*
