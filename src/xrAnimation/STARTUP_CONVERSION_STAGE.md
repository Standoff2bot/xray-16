# Startup Ozz Conversion Stage

## Goal
Stand up a level-loading stage that converts legacy `.ogf/.omf` assets into `.ozz/.ozzx` bundles during game start, so the runtime can immediately hydrate `COzzKinematicsVisual` without manual preprocessing.

## Current Implementation Snapshot
- `StartupConversionInventory` inventories legacy visuals across configured FS aliases, normalised by `NormalizeModelIdentifier`, and records the canonical motion references declared in each `.ogf`.
- Motions are discovered directly from the `.ogf` references; the inventory crawls the same visual roots for `.omf` payloads, capturing metadata (root alias, relative path, size, timestamp, VFS flag).
- `ComputeLegacyAssetInventoryDigest` folds the full inventory (visual sources + motion metadata) into a deterministic CRC32 digest. `Load/StoreInventoryDigestInUserConfig` persist that digest inside `user.ltx` so we can tell whether the startup scan matches the last cached run.
- `LegacyOgfConverter` exposes `ConvertLegacyVisualToOzzBundle` so runtime callers and the CLI share identical conversion code paths.
- `CGamePersistent::OnGameStart()` now loads the cached digest from `user.ltx`, verifies that previously generated `.ozzx`/`.ozz` artefacts exist, and triggers a rebuild (writing outputs under `$game_meshes$`/`$game_anims$`) whenever the digest changes or assets are missing. Successful runs persist the freshly computed digest for subsequent boots.
- `test_startup_conversion.cpp` exercises inventory construction, digest stability, digest invalidation on asset edits, and digest persistence round-tripping through an `.ltx` file.

## Inventory Builder
- Configure scan roots through `InventoryScanConfig::visual_roots`. `BuildDefaultLegacyAssetInventory()` scans `$level$` and `$game_meshes$`; tests inject custom aliases for isolated setups.
- Each `.ogf` is normalised via `NormalizeModelIdentifier()`. Every physical source (alias + relative path) is kept so the runtime can resolve back to the originating archive/loose file.
- Motion references are parsed from `OGF_S_MOTION_REFS2` (preferred) or `OGF_S_MOTION_REFS`, canonicalised to lower-case `.omf` paths, and deduplicated on `(root, path)` pairs. Matching `.omf` files are enumerated from the same roots to populate size/timestamp metadata.
- `LegacyAssetInventory` maintains lookup tables for visuals and motions, enabling quick resolution during the conversion pass.

## Inventory Digest & Change Detection
- The digest is a canonical string of every visual source, motion reference, and motion asset metadata sorted deterministically, hashed via `crc32`.
- On startup we load the persisted digest from `user.ltx` (`ozz_startup_conversion/inventory_digest`). A mismatch means the asset set or metadata changed (new `.ogf`, edited `.omf`, different timestamps/sizes).
- When the digest changes, the stage should iterate every `.ogf` in the inventory, verify that the corresponding `.ozzx` bundle and referenced `.ozz` motions exist and are fresher than their sources, and enqueue conversions for anything missing or stale.
- After successfully processing the queue, write the new digest back to `user.ltx` so the next boot can skip the heavy scan.

## Outstanding Integration Work
- Surface progress through the loading screen (`LoadTitle`/`LoadStage`) once localisation strings are in place so the UI communicates the new conversion work clearly.
- After wiring the conversion jobs, verify bundle timestamps before writing new digests so we only persist once outputs are safely on disk. Continue to fallback gracefully to legacy visuals when conversion fails and emit clear telemetry for mismatched assets.
- Record per-asset timings so we can report aggregate conversion costs and feed the legacy vs. Ozz frame-cost telemetry workstream.
- Extend console/debug tooling to dump the current digest, force regeneration, or purge cached bundles when investigating asset issues.

## Test & Tooling Coverage
- `StartupConversionStageTest` now validates inventory construction, digest stability, digest invalidation, and digest persistence via the config helpers.
- `xrAnimation_converter_tests` and the CLI continue to exercise the shared conversion routines (`LegacyOgfConverter`, `LegacyOmfConverter`).
- `convert_assets.sh` remains the quick way to regenerate test fixtures when converter logic changes.
- Update docs, help strings, and loading-stage UI whenever behaviour changes so gameplay smoke tests stay aligned with the automated conversion path.
