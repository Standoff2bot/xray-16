# GPU Grass Architecture

## Guiding Goals
- Eliminate CPU triangle queries and matrix builds by moving placement, height sampling, and per-instance attributes fully to GPU-friendly data representations.
- Replace the mutable slot cache with a hierarchical streaming grid that natively supports clipmap-style residency, async updates, and deterministic placement.
- Keep grass data resident on the GPU as compressed textures/buffers so decompression is either one-time (level load) or compute-driven per visible page.
- Ensure the system scales to open worlds (virtual textures, ring buffers) and supports persistent interaction state (bending, trails) without CPU bottlenecks.
- Maintain compatibility via fallbacks while enabling modern features like hi-Z culling, wind fields, and procedural overrides.

## Core Architecture Pillars

### Offline / Load-Time Data Bake
- Convert `level.details` and static geometry into GPU-friendly assets: palette masks → array-texture pages (one RGBA8 layer per detail object with 2×2 corner densities promoted to 8-bit) that can be streamed like clipmap tiles, quantized metadata textures, and per-tile height clipmaps or signed-distance fields so the GPU can resolve ground height without CPU rays.
- Validate deterministic seeding against the legacy CPU decompressor so the GPU path reproduces placement when needed for regression testing.

### Runtime Residency Grid
- Introduce a clipmap-based world grid (e.g., Regions→Tiles→Cells) with residency metadata; tiles back their compressed data via sparse textures or manual page tables and expose bounds for culling before decode.
- The residency manager tracks which tiles fall inside each clipmap ring, manages sparse bindings, and queues GPU tasks for newly resident pages.
- Optional persistence: when a tile leaves residency, either drop interaction data or cache it CPU-side (compressed RLE) keyed by tile coordinate.

### GPU Placement & Decompression
- A compute pipeline ingests compressed tile data, stochastically generates instances (deterministic hash seed per cell), samples the pre-baked height field, fetches palette densities from the per-tile array textures, and writes directly into large persistent instance buffers (with support for append or scatter based on ring-buffer generations).
- Tiles can stream in/out, regenerate deterministically, and scale to infinite terrain without growing buffers unbounded.

### GPU Culling & Rendering
- Extend the existing compute infrastructure to include LOD tagging, hi-Z occlusion, and multi-draw indirect submission; instance data stays on GPU, with clipmap tiers selecting impostors/billboards for far distances.
- Treat culling as part of a hierarchical pipeline—cells (tiles) culled first, per-instance work only for resident/visible tiles—so GPU cost scales with what’s on screen rather than total instance count.
- True GPU instancing replaces the legacy batched constant-buffer path; `DetailInstanceGPU` becomes the single source of per-instance transforms and material state.

### Grass Interactivity System
- Anchor the interaction texture to the same region/tile clipmap used for grass placement, so every resident grass tile owns a co-resident RW texture page (e.g., 256×256 texels covering the tile footprint). That keeps addressing simple—world `xz` → tile index + local UV—and lets the residency manager stream the bend data in/out alongside placement data.
- Store state per texel as a compact vector: `.r = bend magnitude`, `.g = bend azimuth (encoded as sin/cos or packed angle)`, `.b = recovery timer or velocity`, `.a = optional metadata (e.g., wetness, burn)`. Quantize to 16-bit channels if bandwidth matters; fall back to 8-bit if you only need coarse response.
- Each frame, run a compute pass sequence: (1) **Decay** on the resident pages to ease values back toward rest using exponential recovery tied to delta time; (2) **Impulse** pass that splats entity contributions—take the CPU list of movers intersecting a tile, push it into a structured buffer, and in the shader stamp Gaussian or capsule shapes into the texture, writing bend magnitude/direction and refreshing timestamps; (3) optional **Blur** or **SDF build** to smooth footprints. Because these passes operate per tile, dispatch counts stay small and you can overlap them with grass placement or culling on async compute.
- When the tile leaves residency, either drop the interaction data (fast) or cache it in a CPU-side pool keyed by tile coordinate if you want persistence—compressed RLE or run-length blobs work because most texels sit near zero. Reloading a tile simply seeds the GPU page from that cached blob before the next decay/impulse cycle.
- The grass vertex/mesh shaders sample the interaction texture using the same UV mapping they already use for wind/height lookup. Blend the sampled bend vector with procedural wind so entity impulses dominate nearby and fade with distance. Because the interaction texture is clipmap-aligned, far-LOD billboards can sample the same data at reduced resolution to keep footprints coherent across LOD transitions.
- NPCs or physics systems that need read access (e.g., to detect “flattened” areas) can either read back sparsely (rare) or query a downsampled copy maintained on the CPU—generate it cheaply by mipmapping the interaction texture in compute and copying only the lowest mip every few frames.
- Palette and interaction textures participate in the same clipmap pipeline and must support dynamic filtering: after gameplay edits (footprints, fire, scripted density changes) regenerate low-frequency mips or run compute blurs so downsampled sampling and impostors remain coherent.

### FBM Wind System
- Treat wind as a procedural 3D vector field sampled in clipmap space instead of precomputed sin/cos curves. Generate it in a compute shader with a small stack of FBM noise (e.g., curl noise built from gradient of summed simplex/octave noise) so velocity varies smoothly in space and time; expose wind direction/speed parameters as uniforms that modulate the noise domain to support gusts.
- Each grass instance stores a phase seed (hash of world position or slot index), so the shader offsets its sample time (`t + seed`) and spatial lookup slightly—this decorrelates neighboring blades while keeping motion deterministic across frames.
- In the vertex/mesh shader, sample the wind field at multiple heights along the blade (base, mid, tip) by lerping world position’s `y` component. Accumulate the FBM vector, optionally apply a simple spring response using the interaction texture so bent areas lag and overshoot. Because FBM yields low-frequency gusts plus high-frequency ripple, you can mix two spectra—one for stem sway, one for leaf flutter.
- Use cheap temporal integration per instance: store the previous frame’s bend direction/magnitude in a per-instance buffer or derive it on the fly with exponential smoothing (`bend = lerp(bend, target, damping)`), keeping the motion coherent even if wind samples jump due to large gust frequency.
- Drive wide-area effects like gust fronts by animating the FBM domain—add a scrolling offset based on global wind velocity and a secondary time-varying amplitude curve so gusts roll through the field. You can layer weather events by blending multiple FBM fields with different wavelengths/amplitudes.
- For distant LODs/billboards, precompute a subset of the FBM field into a low-res flow texture per tile (updated on GPU alongside interaction textures). Billboards sample that texture to sway synchronously with the high-detail blades, avoiding popping at transitions.

### Optional Procedural Geometry Variant
- Provide a build-time switch that swaps the legacy textured detail meshes for procedurally generated prism or ribbon blades with solid colors; leverage a small compute/vertex pass to extrude simple quads or triangular strips per instance when the simplified path is active.
- Keep both geometry paths compatible with the same instance data so artists can A/B test without changing placement—only the shader include or mesh binding changes at compile time.

### Legacy System Decommissioning
- Remove grass_benders, detail cache manager, wave0/wave1/still CPU computations, and all per-instance CPU culling lists once the GPU architecture is active.
- Strip the batched constant-buffer update path in `DetailManager_VS` in favor of mesh or vertex shader variants that source transforms from `DetailInstanceGPU`.
- Maintain compatibility toggles during transition but plan to delete the legacy code after parity validation.

## Implementation Roadmap

### Phase 1 – Data & Struct Foundations
1. Convert existing CPU structs into GPU-friendly structs, ensuring `DetailInstanceGPU` stays tightly packed and matches HLSL layouts.
2. Build the offline/export pipeline that produces clipmap-aligned compressed mask textures, height clipmaps, and object metadata.
3. Validate deterministic placement by comparing GPU-generated instances against CPU decompressor snapshots.

### Phase 2 – Residency & Placement
1. Implement the residency manager (Regions→Tiles→Cells) with clipmap rings, sparse bindings, and tile lifecycle events.
2. Author the compute placement shaders that ingest compressed slots, evaluate palettes, sample height, apply jitter, and populate persistent instance buffers.
3. Integrate optional CPU-side persistence for interaction textures when tiles are evicted.

### Phase 3 – Culling & Rendering
1. Extend compute culling with hierarchical cell rejection, hi-Z occlusion hooks, and LOD tagging; ensure multi-draw indirect submission per vis channel.
2. Wire true GPU instancing by binding structured buffers in the vertex/mesh shader and removing the legacy constant-buffer batching.
3. Add support for the optional procedural blade geometry path and confirm both mesh modes share the same pipeline.

### Phase 4 – Interaction & Wind
1. Implement the interaction texture update passes (decay, impulse, optional blur/SDF) and shader sampling for deformation.
2. Integrate entity impulse ingestion on the CPU side, feeding structured buffers per-tile to the impulse pass.
3. Build the FBM wind compute pass and shader sampling logic, including multi-height sampling, per-instance seeding, temporal smoothing, and gust domain animation.

### Phase 5 – Streaming, LOD, and Polish
1. Finalize clipmap streaming distances, far-LOD impostors/billboards, and ensure interaction/wind data remain coherent across LOD transitions.
2. Profile memory, bandwidth, and GPU cost; tune tile resolution, interaction texture format, and FBM octave counts.
3. Remove legacy systems, clean up feature toggles, and document the GPU path for engine and tooling teams.

## Open Questions
- What resolution and encoding (height vs. distance field) provide enough accuracy for grounding without ballooning memory or bandwidth for clipmap tiles?
- How should we reconcile deterministic placement with artist-authored randomness so GPU regeneration matches baked CPU results for parity testing?
- Do we target DX11 only, or can we assume mesh shader availability for modern platforms (changes batching strategy)?
- What are the streaming constraints (world size, movement speed) so the residency system can size queues, buffers, and async budgets appropriately?
