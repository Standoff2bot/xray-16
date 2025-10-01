# Startup Ozz Conversion Stage

## Goal
Stand up a level-loading stage that converts legacy `.ogf/.omf` assets into `.ozz/.ozzx` bundles during game start, so the runtime can immediately hydrate `COzzKinematicsVisual` without manual preprocessing.

## Current Loading Flow
- `IGame_Persistent::OnGameStart()` sets the stage title to `st_prefetching_objects` and, unless `-noprefetch` is present, calls `Prefetch()` (`src/xrEngine/IGame_Persistent.cpp:374-417`).
- `Prefetch()` hydrates object pools, invokes `GEnv.Render->models_Prefetch()`, and pushes staged visuals into the model pool.
- `CModelPool::Prefetch()` reads the `prefetch_visuals_<game_type>` section and touches each listed visual (`src/Layers/xrRender/ModelPool.cpp:449-470`), emitting standard load stages via `LoadTitle()`/`LoadStage()`.
- Loading UI exposes stage names when `g_loading_stages` is toggled (`src/xrGame/ui/UILoadingScreen.cpp:101-138`).

## Sequential Tasks
1. **Build Asset Inventory & Cache Criteria**
   - Use `FS.file_list` (and related iterators) across `$game_meshes$`, `$level$`, and any packaged roots to enumerate every `*.ogf` and `*.omf` visible to the runtime.
   - For each `.ogf`, resolve the canonical identifier via `NormalizeModelIdentifier()`, collect referenced motions via `load_motion_refs_from_ogf()`, and record the filesystem root so later lookups can pull payloads from archives or loose files.
   - Define the cache manifest (e.g., `gamedata/ozz_cache_manifest.json`) that stores source timestamps/hashes, the emitted `.ozz/.ozzx` targets, converter build/version info, and the last conversion outcome.
   - Establish skip rules: if matching `.ozz/.ozzx` (and any referenced `.ozz` motions) are newer than the source or the manifest marks them up-to-date, bypass conversion unless a developer override (such as `g_force_startup_conversion`) is enabled.

2. **Extract Shared Conversion Primitives**
   - Lift the skeleton/mesh/motion helpers from the CLI (`load_skeleton_bones_from_ogf`, `build_raw_skeleton`, `build_mesh`, bundle writer, `ConvertLegacyOmf`) into a dedicated runtime-accessible module under `src/xrAnimation/`.
   - Expose engine-facing APIs such as `ConvertLegacyVisualToOzzBundle(const ConversionRequest&)` and motion batch helpers so the startup stage can execute conversions without shelling out to the CLI.
   - Keep the CLI thin wrappers over the shared APIs to preserve existing test coverage and ensure converter/unit tests exercise exactly what the engine will call.

3. **Integrate Startup Conversion Stage**
   - In `IGame_Persistent::OnGameStart()`, immediately after `Prefetch()` (while honoring `-noprefetch` and `g_use_ozz_visuals`), invoke the conversion orchestrator.
   - Surface progress via `LoadTitle("st_converting_ozz_assets")`, reuse `UILoadingScreen::SetStageTitle()` and `LoadStage()` to show counts, and emit per-asset telemetry with `Msg()`.
   - Iterate the inventory, consult the manifest to skip fresh assets, convert the rest through the new APIs, and write `.ozz/.ozzx` outputs under the mirrored `gamedata/meshes` / `gamedata/anims` layout. Failures log warnings and fall back to legacy assets instead of aborting the stage.
   - Update the manifest after each asset completes (conversion result, timestamps, durations) so subsequent runs remain incremental.

4. **Tests, Telemetry, and Documentation**
   - Add unit/integration coverage that exercises the runtime conversion API (using temporary filesystem roots) and validates outputs against existing fixtures, including manifest hit/miss paths.
   - Create a loading-stage smoke test that boots the engine with a curated asset set, runs the conversion pass, and asserts the expected `.ozz/.ozzx` files appear before gameplay begins.
   - Refresh developer docs, stage/UI strings (including `g_loading_stages` copy), and thread lightweight timers/counters around the stage for basic telemetry.

## Parallel Workstreams
- **Telemetry & UI Polish**: Once the stage skeleton works (post-Step 3), refine status messaging, aggregate metrics (total assets, cache hits, conversion time), and surface summaries to the loading screen/logs.
- **Async Execution Evaluation**: Prototype moving the conversion queue onto the job system or background threads to reduce load-time spikes while preserving deterministic ordering.
- **Error & Recovery Tooling**: Build console commands or debug menus to inspect the manifest, retry failed assets, and purge cache entries without restarting the engine.
- **Extended Test Automation**: Expand CI scripts to regenerate fixtures, execute the startup conversion smoke, and flag bundle layout changes automatically.

## Implementation Notes
- Stage skips assets whenever existing `.ozz/.ozzx` outputs (and referenced motion `.ozz`) are newer than their `.ogf/.omf` sources or the manifest marks them fresh; override toggles can force reconversion when needed.
- Failed conversions log errors via `Msg()`/`Log()` and allow the queue to continue so legacy rendering remains a fallback.
- Start with a synchronous queue guarded by telemetry; promote the async workstream after load-time profiling validates the benefit.
