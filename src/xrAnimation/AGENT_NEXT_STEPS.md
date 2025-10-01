# AGENT_NEXT_STEPS

## Current Status
- MVP delivered: converters, parity harness, and `.ozzx` runtime visual ship together and mirror legacy behaviour for the supported fixtures.
- `.ogf/.omf → .ozz` pipeline, viewer tooling, and parity suites remain the authoritative regression harness for future changes.
- Engine builds expose `g_use_ozz_visuals` and `g_dev_ozz_actor`, enabling smoke tests of converted bundles without touching legacy assets.
- Bundle hydration, model-name normalisation, and palette instrumentation are covered by dedicated unit and parity tests.

## Post-MVP Focus
1. **Startup Conversion Stage**
   - Polish the new conversion pass (already invoked from `OnGameStart`) by surfacing progress through `g_loading_stages`, enriching diagnostics, and ensuring failures fall back cleanly to legacy assets.
   - Coordinate with asset caches to avoid duplicate loads, respect `-noprefetch`, and keep rebuilds idempotent when outputs already exist.
2. **Stability & Regression Automation**
   - Keep the converter/runtime suites green, add smoke coverage for newly converted startup bundles, and automate checks that guard palette/visibility behaviour.
3. **Telemetry & Documentation**
   - Capture frame-cost deltas between legacy and Ozz paths, publish lightweight dashboards/logs, and fold the findings into README and workflow docs.

## Execution Checklist
1. **Design the Loading Hook**
   - Inspect `IGame_Persistent::OnGameStart()` / `Prefetch()` to slot a conversion stage immediately after the OGF/OMF fetch, honouring the existing `LoadTitle()` flow.
   - Define ownership/lifetime for the conversion queue, output directory, and any async worker coordination.
2. **Prototype Conversion Pass**
   - Reuse `xrAnimation` converter entry points to process the prefetched resources, write bundles under `gamedata`, and emit diagnostics via `Msg()`.
   - Add toggles for skipping conversion (developer workflows, already-converted assets) and guard against blocking the main thread for large asset sets.
3. **Automation & Telemetry**
   - Expand CI/local scripts that regenerate fixtures, run converter + runtime tests, and flag regressions when bundle layouts change.
   - Add optional timers/toggles around sampling, palette builds, and CPU skinning to quantify runtime costs and store summaries alongside docs.
4. **Roadmap Notes**
   - Document follow-up milestones (threaded sampling, GPU palette uploads, motion metadata passthrough) with assumptions/risks to guide planning discussions.

## Supporting Work
- Keep README/agent docs aligned with the MVP status and future roadmap.
- Maintain the direct X-Ray↔Ozz basis helpers across runtime and tools; avoid reintroducing Blender-dependent math paths when adding new converters or debug outputs.
- Trim build friction by disabling upstream ozz test targets that trigger DLL copy timeouts during CI or local runs.
