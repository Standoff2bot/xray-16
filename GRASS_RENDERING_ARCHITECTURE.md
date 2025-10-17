# X-Ray Engine - Grass/Detail Rendering Architecture Analysis

## Current Implementation Overview

### Core Components

The grass (detail) rendering system is managed by the `CDetailManager` class, split across multiple files:

- **DetailManager.h/cpp** - Main manager class and initialization
- **DetailManager_VS.cpp** - Hardware vertex shader rendering path
- **DetailManager_soft.cpp** - Software/CPU rendering fallback
- **DetailManager_CACHE.cpp** - Grid cache management system
- **DetailManager_Decompress.cpp** - Slot decompression and placement logic
- **DetailFormat.h** - Data format definitions
- **DetailModel.h/cpp** - Detail object mesh data

### Data Structure

#### Spatial Organization
- **Grid System**: World divided into 2m × 2m slots (`DETAIL_SLOT_SIZE = 2.0f`)
- **Two-Level Cache**:
  - `cache_level1`: Large grid sections (CacheSlot1)
  - `cache`: Fine-grained individual slots
- **Slot Contents**: Each slot can contain up to 4 different detail object types (`dm_obj_in_slot = 4`)

#### Detail Slot Format (DetailFormat.h:129-206)
Highly packed bitfield structure (16 bytes):
```cpp
struct DetailSlot {
    u32 y_base : 12;      // Height base (20cm units, -200m to +619.2m)
    u32 y_height : 8;     // Height range (10cm units, up to 25.6m)
    u32 id0 : 6;          // Object type IDs (0x3F = empty)
    u32 id1 : 6;
    u32 id2 : 6;
    u32 id3 : 6;
    u32 c_dir : 4;        // Directional lighting (quantized)
    u32 c_hemi : 4;       // Hemispherical lighting
    u32 c_r : 4;          // RGB color (4.4.4 format)
    u32 c_g : 4;
    u32 c_b : 4;
    DetailPalette palette[4];  // Alpha masks for placement
};
```

#### Per-Instance Data (DetailManager.h:60-73)
```cpp
struct SlotItem {
    float scale;
    float scale_calculated;
    Fmatrix mRotY;        // 4×4 rotation/translation matrix
    u32 vis_ID;           // 0=still, 1=wave1, 2=wave2
    float c_hemi;         // Hemispherical lighting
    float c_sun;          // Sun lighting
    float distance;       // Distance from camera
    Fvector position;     // World position
#if RENDER == R_R1
    Fvector c_rgb;        // RGB color (R1 only)
#endif
};
```

### Rendering Pipeline

#### 1. Cache Update (DetailManager_CACHE.cpp:101-253)
**Called every frame in MT task** (`DispatchMTCalc`, DetailManager.cpp:438-464)

```cpp
cache_Update(s_x, s_z, EYE)
```

- Shifts grid cache as camera moves
- Selects up to `dm_max_decompress` (7 or 14) nearest pending slots
- Decompresses selected slots on-demand
- Updates hierarchical bounding volumes

**Issues:**
- Limited decompression rate creates pop-in
- Cache shifting causes full-grid validation
- CPU-bound decompression

#### 2. Visibility Culling (DetailManager.cpp:256-396)
**`UpdateVisibleM()`** - Multi-level frustum culling:

1. **Level 1**: Test `cache_level1` bounding spheres against frustum
2. **Level 2**: Test individual slot bounding spheres
3. **Occlusion**: Hardware Occlusion Queries (HOM) test
4. **Distance**: Fade based on distance squared
5. **LOD Selection**: SSA (Screen Space Area) calculation per instance

**Per-slot processing:**
- Calculates per-instance fade alpha
- Computes SSA: `ssa = scale² × R² / distance²`
- Sorts instances into 3 vis lists: still(0), wave1(1), wave2(2)

**Issues:**
- Per-instance SSA calculation on CPU
- No spatial batching optimization
- Frame throttling (updates slot every 15-30 frames)

#### 3. Geometry Preparation (DetailManager_VS.cpp:42-111)
**Hardware path** (`hw_Load_Geom`):

- Pre-bakes all detail meshes × batch size into static VB/IB
- Batch size limited by vertex shader constants: `hw_BatchSize = (registers - 10) / 4` (max 64)
- Vertex format: `{float3 pos, short4 uv_t_mid}` where `mid` = matrix index

**Total memory:**
```cpp
VB_size = Σ(vertices_per_mesh × batch_size × 16 bytes)
IB_size = Σ(indices_per_mesh × batch_size × 2 bytes)
```

**Issues:**
- Massive static buffer allocation
- Limited batching (typically ~16-32 instances)
- Pre-multiplication wastes memory

#### 4. Rendering (dx11DetailManager_VS.cpp:93-267)
**`hw_Render_dump()`** - Called 3× per frame (wave0, wave1, still):

**Per-object type loop:**
1. Set shader element
2. Update per-frame constants:
   - `consts` (scale, lighting params)
   - `wave` (animation phase timings)
   - `dir2D` (wind direction)
   - `xform` (MVP matrix)
3. **NPC Grass Interaction** (lines 106-173):
   - Updates `benders_pos[16]` array with NPC positions
   - Updates `benders_setup` parameters
   - **HACK**: Shader-side distance checks and fake animation
4. **Instance batching loop**:
   - Fills `array[batch_size × 4]` with per-instance 3×4 matrices + lighting
   - Issues draw call every `hw_BatchSize` instances
5. Advance VB/IB offsets

**Animation System:**
- CPU calculates two rotating wind directions: `dir1`, `dir2`
- Vertex shader applies sinusoidal displacement based on:
  - Vertex height (`t` coordinate)
  - Wave parameters (frequency, phase, amplitude)
  - Wind direction

**NPC Grass Interaction Issues:**
- Hardcoded 16 bender limit
- CPU updates every frame
- Shader does radius checks (inefficient)
- No actual grass state persistence
- Fake "bend" is just displacement

---

## Performance Bottlenecks

### 1. CPU-Heavy Workload
- ✗ Cache decompression (geometry intersection tests)
- ✗ Per-instance visibility + SSA calculation
- ✗ Per-frame matrix building (scale, rotation, position)
- ✗ Constant buffer updates (up to 64×4 float4s per batch)

### 2. Draw Call Overhead
- Typically renders 100s-1000s of instances
- Small batch sizes (16-64) = many draw calls
- Per-batch shader constant updates

### 3. Memory Inefficiency
- Static VB pre-multiplied by batch size
- Pre-allocated worst-case memory
- No memory sharing between LODs

### 4. Limited Culling
- No occlusion culling at instance level (only slot-level HOM)
- No hi-Z or GPU-driven culling
- Frame-throttled updates cause stale visibility

### 5. Animation Limitations
- Uniform wind animation (not localized)
- NPC interaction limited to 16 entities
- No persistent grass state (crushing, growth)

---

## Optimization Strategy

### Phase 1: GPU-Driven Rendering Pipeline

#### 1.1 Compute Shader Instance Culling
**New System:**
```
[Persistent GPU Buffer] Detail Instances (all loaded slots)
         ↓
[Compute: Frustum Cull] → Visible Instance Buffer
         ↓
[Compute: Occlusion Cull (Hi-Z)] → Culled Instance Buffer
         ↓
[Compute: LOD Selection] → Draw Commands Buffer (indirect args)
         ↓
[Vertex Shader: Instance Rendering]
```

**Benefits:**
- Entire culling pipeline on GPU
- No CPU readback
- Hi-Z occlusion queries
- Per-instance, not per-slot

**Implementation:**
- Create `StructuredBuffer<DetailInstance>` with all instances
- Compute shader reads camera frustum from CB
- Use `InterlockedAdd` to build compacted output
- `DrawIndexedInstancedIndirect` for rendering

#### 1.2 Indirect Drawing with Mesh Shaders (Optional DX12 Path)
For modern GPUs:
- Mesh shaders generate geometry procedurally
- Avoid pre-baked VB/IB multiplication
- Amplification shader for LOD selection

### Phase 2: Persistent Grass State System

#### 2.1 Grass State Texture
**Replace benders array with GPU texture:**
```
RWTexture2D<float4> grassStateTexture
    .r = bend amount [0..1]
    .g = bend direction [0..2π]
    .b = growth/health [0..1]
    .a = last interaction time
```

**Resolution:** World size / 0.5m = manageable texture
- Example: 512×512m world = 1024×1024 texture

#### 2.2 Compute Shader State Updates
**Per-frame compute passes:**

1. **Decay Pass**: Grass slowly returns to upright
   ```hlsl
   bend = lerp(bend, 0, deltaTime * recoverySpeed);
   ```

2. **Interaction Pass**: Write NPC/player positions
   ```hlsl
   for each active entity:
       splatDisturbance(position, radius, strength, direction)
   ```

3. **Growth Pass** (optional): Seasonal changes
   ```hlsl
   growth += deltaTime * growthRate * environmentFactor;
   ```

**Benefits:**
- No 16-entity limit
- Persistent deformation
- Trails behind moving entities
- Seasons/dynamic world

### Phase 3: Enhanced Spatial Data Structures

#### 3.1 Hierarchical Grid Optimization
**Current 2-level → 3-level hierarchy:**
```
World
├─ Regions (128m × 128m) - coarse frustum cull
│  ├─ Chunks (16m × 16m) - medium frustum cull
│  │  └─ Slots (2m × 2m) - fine-grained
```

**Augment with:**
- Tight AABB per hierarchy level
- Pre-computed worst-case density
- Early-out for empty regions

#### 3.2 GPU-Resident Slot Data
Move decompression to load-time or streaming thread:
- Build full instance list during level load
- Stream into GPU buffers
- No runtime decompression

**Streaming Strategy:**
- Ring buffer for far instances
- Priority queue based on camera velocity
- Async compute for decompression

### Phase 4: Rendering Improvements

#### 4.1 Instanced Rendering with Indirect
Replace batched constant arrays:
```hlsl
StructuredBuffer<InstanceData> instances;
DrawIndexedInstancedIndirect(cmdBuffer);
```

**Vertex Shader:**
```hlsl
InstanceData inst = instances[instanceID];
float4x4 world = BuildMatrix(inst.position, inst.rotation, inst.scale);
```

#### 4.2 LOD System Enhancement
**Current:** 3 visibility lists (still, wave1, wave2)
**New:** Distance-based LOD + Billboard imposters

1. **LOD0** (0-10m): Full geometry, per-instance animation
2. **LOD1** (10-30m): Simplified mesh, uniform animation
3. **LOD2** (30-60m): Billboard clusters
4. **LOD3** (60m+): Merged billboards / grass texture

#### 4.3 Shader Optimizations
**Wind Animation:**
- Precompute wind field texture
- Sample wind per-instance, not per-vertex
- GPU noise for variation

**Grass State Integration:**
```hlsl
float4 state = grassStateTex.SampleLevel(sampler, worldPos.xz / worldSize, 0);
float bend = state.r;
float2 bendDir = float2(cos(state.g), sin(state.g));
position.xz += bendDir * bend * heightFactor;
```

### Phase 5: Advanced Features

#### 5.1 GPU Particle Grass (Dense Areas)
For meadows with 1000s of grass blades:
- Compute shader spawns particles
- Geometry shader generates quads
- Billboarded camera-facing grass

#### 5.2 Procedural Placement (Optional)
Replace pre-baked slots:
- Compute shader reads terrain height + material masks
- Generates instances on-the-fly
- Deterministic random (position-based seed)

#### 5.3 Physics Integration
- Grass state texture as read-only physics query
- Ragdolls/debris write to grass state
- Explosion effects ripple through grass

---

## Implementation Roadmap

### Milestone 1: Foundation (Week 1-2)
- [ ] Create new branch: `yohji/feat/optimize-detail-manager`
- [ ] Refactor DetailManager to separate concerns:
  - `DetailDataManager` - loading/storage
  - `DetailCullingManager` - visibility (move to compute)
  - `DetailRenderManager` - drawing
- [ ] Implement StructuredBuffer instance storage
- [ ] Basic compute shader frustum culling
- [ ] Indirect draw calls

**Success Criteria:** Same visual output, reduced CPU usage

### Milestone 2: GPU Culling (Week 3-4)
- [ ] Hi-Z occlusion buffer generation
- [ ] Compute shader occlusion culling
- [ ] Multi-draw indirect batching
- [ ] Performance profiling vs. baseline

**Success Criteria:** 2×+ instances rendered at same framerate

### Milestone 3: Grass State System (Week 5-6)
- [ ] Create grass state texture
- [ ] Compute shader decay pass
- [ ] Replace benders_pos array with texture writes
- [ ] Integrate state into vertex shader
- [ ] Add artistic controls (recovery speed, bend strength)

**Success Criteria:** Grass trails behind player, no entity limit

### Milestone 4: LOD & Optimization (Week 7-8)
- [ ] Implement billboard LODs
- [ ] Add LOD selection to compute culling
- [ ] Optimize shader (wind texture, reduce ALU)
- [ ] Memory optimization (streaming, compression)

**Success Criteria:** 50m+ grass draw distance, stable 60fps

### Milestone 5: Polish & Advanced (Week 9+)
- [ ] GPU particle grass for dense patches
- [ ] Seasonal growth system
- [ ] Physics interaction API
- [ ] Documentation & editor tools

---

## Technical Considerations

### Compatibility
- **DX11**: All features supported (compute, indirect draw)
- **DX12/Vulkan**: Optional mesh shader path
- **OpenGL**: Requires GL 4.3+ for compute shaders

### Memory Budget
**Current System:** ~50-100MB (pre-baked VB/IB)
**New System:**
- Instance buffer: ~100 bytes × 100K instances = 10MB
- Grass state texture: 4K×4K×4 bytes = 64MB
- Indirect args: negligible
- **Total:** ~75MB (25% reduction + scalability)

### CPU Impact
**Savings:**
- Remove per-frame visibility (10-20ms)
- Remove matrix building (5-10ms)
- Remove decompression stalls (1-5ms)
- **Total:** 15-35ms/frame → background thread only

### GPU Impact
**Additions:**
- Frustum cull compute: ~0.5ms
- Occlusion cull compute: ~0.5ms
- Grass state update: ~0.2ms
- **Total:** ~1.2ms/frame

**Savings:**
- Reduced draw calls: ~2-5ms
- Better batching: ~1-2ms
- **Net:** 1-5ms improvement

---

## Risk Mitigation

### Fallback Paths
1. Keep software rendering path for old hardware
2. Disable grass state on low memory systems
3. Option to use old batched rendering

### Testing Strategy
1. A/B comparison screenshots
2. Performance benchmarks (CPU/GPU time)
3. Memory profiling
4. Visual quality validation (art team review)

### Incremental Rollout
- Feature flags for each system
- Configurable via console commands
- Gradual migration (both paths coexist)

---

## Next Steps

1. **Create feature branch**: `yohji/feat/optimize-detail-manager`
2. **Prototype compute culling** with minimal changes
3. **Benchmark** to validate approach
4. **Iterate** based on results

### Prototype Code Checklist
- [ ] `DetailManager_Compute.cpp` - compute shader manager
- [ ] `detail_cull.compute.hlsl` - frustum culling shader
- [ ] `detail_render_indirect.vs/ps.hlsl` - instanced rendering
- [ ] `StructuredBuffer<DetailInstanceGPU>` definition
- [ ] Indirect draw command buffer setup

---

**Document Author:** Claude (Sonnet 4.5)
**Date:** 2025-10-09
**Engine Version:** X-Ray 16
**Target Platforms:** DX11/DX12/Vulkan/OpenGL
