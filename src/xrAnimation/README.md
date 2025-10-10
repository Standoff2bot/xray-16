# X-Ray Animation / ozz-animation Integration

This module hosts the ongoing effort to move the OpenXRay animation toolchain and runtime over to [ozz-animation](https://github.com/guillaumeblanc/ozz-animation). It now ships production-ready converters, regression tooling, and the MVP runtime façade (`OzzKinematics`) plus `.ozzx` visual integration that mirrors the legacy `IKinematics` interface.

## Current State

- **Asset conversion**: The `xray_to_ozz_converter` CLI and helper scripts convert `.ogf/.omf` pairs into `.ozz` skeletons/animations plus `.ozzx` bundles. Outputs are regression-tested against Blender exports.
- **Visualization**: `ozz_animation_viewer` loads converted bundles, dumps bind-pose/animation data, and provides profiling overlays to compare with upstream ozz samples.
- **Runtime façade**: Three-tier architecture (2025-10-07 refactoring):
  - `OzzKinematicsCore`: Shared skeleton/bone state management
  - `OzzKinematics`: Static models implementing `IKinematics` only
  - `OzzKinematicsAnimated`: Animated models extending `OzzKinematics` with `IKinematicsAnimated`
  - **Key benefit**: Static models return `nullptr` from `dcast_PKinematicsAnimated()`, eliminating "$editor" spam
- **Visual integration**: `COzzKinematicsVisual` uses composition to conditionally create static or animated kinematics based on motion references, hydrating from `.ozzx` bundles.
- **Parity tests**: GoogleTest fixtures under `src/xrAnimation/tests` diff world-space transforms between legacy `CKinematics` and `OzzKinematics` for bind pose and sampled clips to ensure behaviour remains aligned.
- **Operational focus**: Architecture refactoring complete; next priorities are stability testing and performance validation.

## Key Components

- `OzzConversion.*`: Matrix/transform helpers shared by the converter, tests, and runtime façade.
- `OzzBundle.*`: Minimal reader/writer for `.ozzx` bundles (skeleton + mesh payload).
- `OzzKinematicsCore.*`: Shared skeleton and bone state management (no interface implementation).
- `OzzKinematics.*`: `IKinematics` implementation for static models - returns `nullptr` from `dcast_PKinematicsAnimated()`.
- `OzzKinematicsAnimated.*`: Extends `OzzKinematics` with `IKinematicsAnimated` for animated models - includes motion library, blend management, and animation playback.
- `OzzKinematics_legacy.*`: Original monolithic implementation (kept for reference, not used in builds).
- `tests/`: Converter smoke tests, `ozz_kinematics_tests`, and parity fixtures that compare against legacy assets.
- `tools/`: Viewer integration, converter CLI, and support scripts for running conversions on sample data.

## Building & Running Tools

```bash
cmake -S xray-16 -B ozz_utils -DCMAKE_BUILD_TYPE=Debug
cmake --build ozz_utils -j$(nproc)
```

- **Converters**: `ozz_utils/bin/<cfg>/xray_to_ozz_converter`
- **Viewer**: `ozz_utils/bin/<cfg>/ozz_animation_viewer --bundle=<path>.ozzx` (disabled when generating Visual Studio solutions)

Helper scripts in the repository root (e.g. `run_stalker_hero_conversion.sh`) regenerate sample assets under `src/xrAnimation/tests/testdata/` and launch the viewer in verification modes.

## Runtime Usage Snapshot

```cpp
XRay::Animation::OzzKinematics kinematics;
if (kinematics.InitializeFromOzz("path/to/skeleton.ozz"))
{
    // Evaluate rest pose or sampled animation (locals come from an ozz SamplingJob)
    kinematics.CalculateBones(TRUE); // forces recompute this frame
    const Fmatrix& world = kinematics.LL_GetTransform(bone_id);
    // Feed matrices into renderer/physics as needed.
}
```

`OzzKinematics` owns ozz sampling context/cache buffers; multi-threaded sampling is on the roadmap, so avoid sharing an instance across threads without external synchronization.

## Tests

- Build tests with the same CMake cache as the tools.
- Run `ctest --output-on-failure` from the build directory or invoke binaries directly:
  - `ozz_kinematics_tests`
  - `xrAnimation_converter_tests`
- Parity fixtures require the testdata produced by the converter scripts; regenerate them when converter logic changes.

## Roadmap / Known Gaps

- Extend automation around `.ozzx` bundle hydration and smoke coverage so future assets slot into the pipeline without regressions.
- Capture frame-cost telemetry (legacy vs. Ozz) and surface it in docs/dashboards to guide optimisation work.
- Plan the next phase: threaded sampling, GPU palette uploads/skin, and richer motion metadata exposure to gameplay.
- Expand optimization tests to assert size wins for `--optimize` and detect pose drift on representative clips.

## Resources

- `AGENT_DOCS.md`: Session context, workflows, and tooling quick reference.
- `AGENT_COMMANDS.md`: Ready-to-run snippets for gathering bind-pose and sampled animation data.
- `AGENT_NEXT_STEPS.md`: Rolling plan that tracks façade work, visual integration, and testing priorities.

## License

This module is part of the OpenXRay project and follows the same licensing terms.
