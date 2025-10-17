# GPU Grass Validation Strategy

## Objective
Guarantee deterministic parity between the legacy CPU decompressor and the new GPU placement pipeline so we can retire cache-based systems without visual regressions.

## Reference Inputs
- `level.details` header and slot data (`DetailHeader`, `DetailSlot`) from the shipped assets.
- Legacy decompressed output gathered by instrumenting `CDetailManager::cache_Decompress`.
- Offline bake artifacts generated via `gpu_grass::OfflineBaker` and the clipmap configuration described in `GPU_GRASS_ARCHITECTURE.md`.

## Validation Workflow
1. **CPU Baseline Capture**
   - Instrument `cache_Decompress` (temporary build) to dump per-slot instance data: world position, object id, scale, rotation, lighting.
   - Persist data per slot coordinate `(sx, sz)` with deterministic ordering (`SlotPart` index then placement loop order).
2. **GPU Seed Verification**
   - During offline bake, emit `PlacementSeed` records mirroring the CPU input (slot coordinates, enabled objects, density multiplier).
   - Ensure hash seeding uses the same `(slot_x, slot_z, local_x, local_z)` tuple used by the CPU jitter so both paths resolve identical pseudorandom streams.
3. **Compute Placement Replay**
   - Author a standalone GPU test harness (DX11/DX12) that uploads the baked tile payloads, dispatches the placement compute shader, and reads back generated instances into CPU memory.
   - Disable wind deformation and interaction textures; only raw placement data is compared.
4. **Diff & Tolerance Checks**
   - Sort both CPU and GPU instance arrays by `{object_id, position.x, position.z}` for deterministic diffing.
   - Compare per-instance attributes with tight tolerances (<=1e-4 for position/scale, exact match for ids/flags).
   - Produce summary metrics: total mismatch count, per-attribute deltas, slot coverage percentage.
5. **Regression Harness**
   - Package baseline dumps and GPU outputs alongside automated tests; integrate into CI so future shader or bake changes rerun parity checks.

## Deliverables
- Instrumentation toggle (`r_detail_dump_cpu`) used only in validation builds.
- GPU harness tool capable of reading baked assets and emitting CSV/JSON diffs.
- Continuous integration step invoking the harness on representative levels.

