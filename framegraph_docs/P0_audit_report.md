# Phase 0 Renderer Decoupling Audit Report

**Date**: 2026-04-28  
**Branch**: `yohji/feat/framegraph`  
**Objective**: Decouple `src/Layers/xrRender/` from transitive D3D11-specific dependencies in `xrRenderDX11/`

---

## Executive Summary

A total of **76 tainted files** remain in xrRender that prevent standalone compilation:
- **58 headers** (API-exposing contamination)
- **18 implementation files** (internal contamination)
- **12 files with direct xrRenderDX11 includes** (hardest to decouple)
- **11 API-exposed files** (widest impact on dependent code)

The largest contamination vectors are:
1. **R_Backend.h** (2 xrRender dependents) — Contains D3D11 state manager includes
2. **Shader.h** (24 xrRender dependents) — IBlender forward declaration + references
3. **Blender.h + blenders/** (39 blender class headers, 15+ dependents) — R4-only type hierarchy

**Recommended entry point**: Decouple Blender-related files first (self-contained, no internal xrRender cross-dependencies).

---

## Section 1: D3D11-Tainted Files Inside xrRender/

### Tier 1: Direct xrRenderDX11 Includes (Hardest)

| File | Offending Include | Usage Location |
|------|------------------|---|
| `R_Backend.h` | `dx11StateManager.h`, `dx11ShaderResourceStateCache.h`, `dx11StateCache.h` | Header (API-exposed) |
| `R_Backend_Runtime.h` | `dx11R_Backend_Runtime.h` | Header (internal reference) |
| `r_constants.h` | `dx11ConstantBuffer.h`, `dx11ConstantBuffer_impl.h` | Header (widely included) |
| `r_constants_cache.h` | `dx11r_constants_cache.h` | Header (included by R_Backend.h) |
| `SH_Atomic.h` | `dx11State.h` | Header (Shader subsystem) |
| `tss_def.cpp` | `dx11StateUtils.h` | Implementation (internal) |
| `xrRender_console.cpp` | `dx11StateManager.h`, `dx11SamplerStateCache.h`, `dx11ComputeTest*.h` | Implementation (console diagnostics) |

**Total**: 7 files with direct dx11 includes

---

### Tier 2: D3D11 Type References (No include, but uses ID3D11_* / IDXGI)

| File | D3D11 Type(s) | Scope | Notes |
|------|---|---|---|
| `Backend/D3D11BackendWrapper.h` | `ID3D11Device`, `ID3D11DeviceContext`, `IDXGI*` | Header | Wrapper class |
| `Backend/D3D11BackendWrapper.cpp` | Multiple D3D11 types | Implementation | Direct device interaction |
| `Backend/D3D12Backend.h` | `IDXGI*` | Header | DXGI factory (cross-API) |
| `Backend/D3D12Backend.cpp` | `IDXGI*` | Implementation | Factory code |
| `DetailManager.h` | `ID3D11Texture2D`, `ID3D11ShaderResourceView` | Header (API-exposed) | Legacy detail geometry format |
| `GPUCullingManager.h` | `D3D11_VIEWPORT`, others | Header (API-exposed) | GPU culling state |
| `RenderContext/RenderDevice.h` | `ID3D11Device`, `ID3D11DeviceContext` | Header (API-exposed) | Core device abstraction |
| `RenderContext/RenderDevice.cpp` | `ID3D11*`, `D3D11_*` | Implementation | Device creation/management |
| `RenderContext/RenderStateConversion.h` | `D3D11_BLEND`, etc. | Header (state enums) | Legacy state mapping |
| `ShaderResourceTraits.h` | `ID3D11ShaderResourceView` | Header (widely included) | Resource trait system |
| `r_FrameGraphRenderer.h` | `ID3D11Buffer` | Header (public interface) | Frame graph setup |
| `NVRHI/NVRHIDevice.h` | `ID3D11Device`, `ID3D11DeviceContext` | Header (API-exposed) | NVRHI backend registration |
| `NVRHI/NVRHIDevice.cpp` | `ID3D11*` types | Implementation | Device initialization |
| `ResourceManager/DDSLoader.cpp` | `D3D11_*` format constants | Implementation | Texture format mapping |
| `dxImGuiRender.cpp` | `D3D11_*` vertex format flags | Implementation | ImGui rendering |
| `xr_effgamma.cpp` | `IDXGI*` | Implementation | Color space conversion |

**Total**: 16 files with D3D11 types (no direct includes)

---

### Tier 3: IBlender and Blender_* Type References

#### Core Blender Infrastructure (4 files)

| File | Reference | Scope | Status |
|------|-----------|-------|--------|
| `Blender.h` | `class IBlender` definition | Header (R4-only type) | Public API for blender registration |
| `Blender.cpp` | `IBlender` implementation | Implementation | Class instantiation |
| `Blender_CLSID.h` | Blender class ID constants | Header | R4-only CLASS_ID mapping |
| `Blender_Recorder.h` | `class CBlender_Compile` interface | Header | R4-only compiler |

#### Blender Subclass Implementations (39 headers + corresponding .cpp)

All files in `blenders/` directory:

**Model/Geometry Blenders (8 headers)**
- `BlenderDefault.h`, `Blender_detail_still.h`, `Blender_tree.h`, `Blender_Vertex.h`
- `Blender_Model.h`, `Blender_Model_EbB.h`, `Blender_Particle.h`, `Blender_BmmD.h`

**Screen-Space Blenders (4 headers)**
- `Blender_Screen_SET.h`, `Blender_Screen_GRAY.h`, `Blender_Blur.h`
- `blender_luminance.h`

**Lighting Blenders (14 headers)**
- `blender_light.h`, `blender_light_direct.h`, `blender_light_point.h`, `blender_light_spot.h`
- `blender_light_direct_cascade.h`, `blender_light_reflected.h`, `blender_light_occq.h`
- `blender_light_mask.h`, `blender_ssao.h`, `blender_bloom_build.h`
- `Blender_Blur.h`, `Blender_LaEmB.h`, `Blender_Lm(EbB).h`, `Blender_Shadow_World.h`

**Deferred Rendering Blenders (8 headers)**
- `blender_deffer_flat.h`, `blender_deffer_aref.h`, `blender_deffer_model.h`
- `Blender_Shadow_Texture.h`, `blender_combine.h`, `uber_deffer.h`
- `Blender_Detail_GPU.h`, `Blender_Editor_*.h`

**DX11-Specific Blenders (4 headers, flagged for quarantine)**
- `dx11HDAOCSBlender.h` — HDAO implementation
- `dx11MSAABlender.h` — MSAA post-process
- `dx11MinMaxSMBlender.h` — Shadow map min/max
- `dx11RainBlender.h` — Rain rendering

**Total**: 39 blender headers (58 files including implementations)

---

### Tier 4: Secondary Consumers of Tier 1-3 Headers

| File | Includes/References | Dependents | Scope |
|------|---|---|---|
| `Shader.h` | `r_constants.h`, `IBlender` (fwd decl) | 24 xrRender files | **Highest impact** |
| `ResourceManager.h` | `Blender.h`, `Blender_CLSID.h`, `Blender_Recorder.h` | Multiple subsystems | Resource management |
| `r_constants.cpp` | `r_constants.h` | Shader compilation | Implementation |
| `Geometry/MaterialCache.cpp` | `r_constants.h`, `SH_Atomic.h`, `Blender_CLSID.h`, `dx11*` includes | Material subsystem | Texture/constant extraction |
| `FrameGraphPasses/ExposurePassSetup.cpp` | `dx11HW.h` | Frame graph setup | Pass initialization |
| `FrameGraphPasses/HiZBuildPassSetup.cpp` | `dx11HW.h` | Frame graph setup | Hierarchical-Z setup |
| `Blender_Recorder.cpp`, `_R2.cpp`, `_StandartBinding.cpp` | `Blender.h`, `Blender_Recorder.h` | Blender compilation | R4-only compilation |

---

## Section 2: Transitive Include Graph — Top 5 Offenders

### 1. **Shader.h** (24 xrRender dependents)

**Direct Includes (depth-1)**:
```
#include "r_constants.h"        [TAINTED: dx11ConstantBuffer.h]
#include "SH_Atomic.h"          [TAINTED: dx11State.h]
#include "SH_Texture.h"         [clean]
#include "SH_Matrix.h"          [clean]
#include "SH_Constant.h"        [clean]
#include "SH_RT.h"              [clean]
class IBlender;                 [fwd decl to TAINTED Blender.h]
```

**Reverse Dependents (files including Shader.h)**:
- `ShaderPhaseCache.cpp`, `UIPassSetup.cpp`, `GeometryBatch.h`
- `MaterialCache.cpp`, `NVRHIUIRenderer.cpp`, `ParticleEffectDef.h`
- `PipelineState.h`, `RCShader.cpp`, `ResourceManager.h`
- `Shader.cpp`, `ShaderKey.cpp`, `UIGeometryBatch.h`
- `UIRenderCollector.cpp`, `dxDebugRender.cpp`, `dxRenderFactory.cpp`
- `dxUIRender.cpp`, `dxUIShader.cpp`, `dxUIShader.h`
- `dxWallMarkArray.cpp`, `r_FrameGraphRenderer.cpp/h` (2 files)

**Impact**: Includes Shader.h = pulls in r_constants.h + SH_Atomic.h = pulls in dx11 types

---

### 2. **Blender.h** (15 xrRender dependents)

**Direct Includes (depth-1)**:
```
#include "Blender_Recorder.h"   [TAINTED: includes Blender.h via compiler]
class IBlender               [TAINTED: self-definition]
class CBlender_Compile       [fwd decl, defined in Blender_Recorder.h]
```

**Reverse Dependents**:
- Blender.cpp, Blender_Recorder.cpp, Blender_Recorder_R2.cpp, Blender_Recorder_StandartBinding.cpp
- ResourceManager.cpp, ResourceManager_Loader.cpp, ResourceManager_Scripting.cpp
- dx11HDAOCSBlender.cpp, dx11MSAABlender.cpp, dx11MinMaxSMBlender.cpp, dx11RainBlender.cpp
- glMSAABlender.cpp, glMinMaxSMBlender.cpp, glRainBlender.cpp
- dxEnvironmentRender.cpp

**Impact**: All blender implementations must include Blender.h = pulls in entire Blender_Recorder.h chain

---

### 3. **r_constants.h** (5 direct xrRender dependents, but transitively included by Shader.h = 24)

**Direct Includes (depth-1)**:
```
#if defined(USE_DX11)
#include "Layers/xrRenderDX11/dx11ConstantBuffer.h"
#include "../xrRenderDX11/dx11ConstantBuffer_impl.h"
```

**Reverse Dependents**:
- `Shader.h` (transitive to 24 files)
- `r_constants_cache.h` (included by R_Backend.h)
- `r_constants.cpp`, `GlobalParamsMapper.cpp`, `MaterialCache.cpp`

**Impact**: r_constants.h = fundamental shader constant system, includes DX11 constant buffer implementation

---

### 4. **R_Backend.h** (2 direct + R_Backend_LOD.h includes, but part of rendering core)

**Direct Includes (depth-1)**:
```
#include "BufferUtils.h"        [clean, but .cpp is in xrRenderDX11]
#include "R_DStreams.h"         [clean]
#include "r_constants_cache.h"  [TAINTED: dx11r_constants_cache.h]
#include "R_Backend_xform.h"    [clean]
#include "R_Backend_hemi.h"     [clean]
#include "R_Backend_tree.h"     [clean]
#ifdef USE_DX11
#include "Layers/xrRenderDX11/StateManager/dx11StateManager.h"
#include "Layers/xrRenderDX11/StateManager/dx11ShaderResourceStateCache.h"
#include "Layers/xrRenderDX11/StateManager/dx11StateCache.h"
```

**Reverse Dependents**:
- `R_Backend_hemi.cpp` (self-referential)
- `r_constants_cache.h` (self-include)

**Impact**: R_Backend = render state machine, includes DX11 state manager (USE_DX11 guarded but still tainted)

---

### 5. **SH_Atomic.h** (5 xrRender dependents, transitively included by Shader.h = 24)

**Direct Includes (depth-1)**:
```
#if defined(USE_DX11)
#include "Layers/xrRenderDX11/StateManager/dx11State.h"
```

**Reverse Dependents**:
- `Shader.h` (transitive to 24 files)
- `UIPassSetup.cpp`, `MaterialCache.cpp`, `SH_Atomic.cpp`, `ShaderKey.cpp`

**Impact**: Atomic shader resource definitions; includes DX11 state for validation

---

## Section 3: Decoupling Order (Leaf-First Topological)

### Leaf Tier (No xrRender dependents)

These files are included only by themselves or external systems:

1. **`Blender_Recorder.cpp`** — Implementation of CBlender_Compile; only included in Blender*.cpp
2. **`Blender_Recorder_R2.cpp`** — R4-specific recorder; standalone
3. **`Blender_Recorder_StandartBinding.cpp`** — Binding recorder; standalone
4. **`Backend/D3D12Backend.cpp`** — D3D12 backend; isolated
5. **`Backend/D3D12Backend.h`** — D3D12 backend header; isolated
6. **`RenderContext/RenderStateConversion.h`** — State enum mapping; isolated
7. **`FrameGraphPasses/ExposurePassSetup.cpp`** — Pass setup; isolated
8. **`FrameGraphPasses/HiZBuildPassSetup.cpp`** — Pass setup; isolated
9. **`ResourceManager/DDSLoader.cpp`** — DDS texture loading; isolated
10. **`dxImGuiRender.cpp`** — ImGui rendering; isolated
11. **`xr_effgamma.cpp`** — Gamma/color space; isolated
12. **`tss_def.cpp`** — Texture state sampling; isolated
13. **`xrRender_console.cpp`** — Console diagnostics; isolated
14. **`Shader.cpp`** — Shader implementation; only self-references
15. **`ShaderKey.cpp`** — Shader key generation; only self-references
16. **`r_constants.cpp`** — Constant table implementation; only self-references

---

### Tier 2: Blender Classes (Self-Contained)

All 39 blender headers + implementations:

**Recommendation**: Quarantine entire `blenders/` directory as R4-only module.

**Dependencies**: Each Blender_*.cpp includes only its own .h header, which ultimately inherits from IBlender.
All blenders are leaf nodes in the xrRender graph (only ResourceManager pulls them).

**Order within blenders** (arbitrary; no cross-blender dependencies):
1. All `blender_*.h` (parent lighting/deferred classes)
2. All `Blender_*.h` (concrete blender implementations)
3. All `dx11*Blender.h` (DX11-specific blenders)
4. Corresponding .cpp files

---

### Tier 3: Blender Core Infrastructure (Depends on nothing else R4-only)

1. **`Blender.h`** → defines IBlender base class
2. **`Blender.cpp`** → implementation
3. **`Blender_CLSID.h`** → R4-only CLASS_ID constants (leaf)
4. **`Blender_Recorder.h`** → CBlender_Compile interface

**Dependents**: ResourceManager.h, ResourceManager.cpp, dxEnvironmentRender.cpp

---

### Tier 4: State Subsystem Headers (xrRenderDX11 includes)

1. **`SH_Atomic.h`** → dx11State.h include; used by Shader.h
2. **`r_constants_cache.h`** → dx11r_constants_cache.h include; used by R_Backend.h
3. **`r_constants.h`** → dx11ConstantBuffer*.h includes; used by Shader.h

**Dependents**: Shader.h (24 files), MaterialCache.cpp, etc.

**Action**: Wrap in `#ifdef USE_DX11` guards or create agnostic wrapper headers

---

### Tier 5: R_Backend Core (State Machines)

1. **`R_Backend_Runtime.h`** → dx11R_Backend_Runtime.h include
2. **`R_Backend.h`** → multiple dx11StateManager includes (highest taint density)

**Dependents**: Only internal backend implementations (R_Backend_hemi.cpp, etc.)

**Action**: Full quarantine to R4-only; create abstraction layer

---

### Tier 6: Secondary Consumers (Difficult, API-exposed)

1. **`DetailManager.h`** → D3D11 texture types; used as public geometry manager
2. **`GPUCullingManager.h`** → D3D11_VIEWPORT; public culling manager
3. **`ShaderResourceTraits.h`** → ID3D11ShaderResourceView; public trait system
4. **`r_FrameGraphRenderer.h`** → ID3D11Buffer; public frame graph interface
5. **`Shader.h`** → r_constants.h + SH_Atomic.h + IBlender; **24 dependents**

**Action**: Requires abstraction barrier (NVRHI or opaque types)

---

### Tier 7: Device/Backend Abstractions (Toughest — API-exposed)

1. **`RenderContext/RenderDevice.h`** → ID3D11Device, ID3D11DeviceContext; core abstraction
2. **`NVRHI/NVRHIDevice.h`** → ID3D11Device; NVRHI bridge
3. **`Backend/D3D11BackendWrapper.h`** → ID3D11Device; DX11-specific wrapper

**Action**: Already part of multi-backend abstraction; may be acceptable to keep D3D11-tainted if properly ifdef'd

---

## Recommended Quarantine Sequence

Execute in this order (leaf-first to minimize cascading changes):

### Phase 1: Isolated Leaf Files (Week 1)
1. Move all Blender_*.cpp implementations to `xrRenderDX11/Blenders/` or R4-specific build
2. Move `Blender.h`, `Blender.cpp`, `Blender_CLSID.h`, `Blender_Recorder*.h` to `xrRenderDX11/`
3. Move all `blenders/` folder (39 files) to `xrRenderDX11/blenders/`
4. Move `xrRender_console.cpp`, `tss_def.cpp` to R4-only section (console commands)
5. Move pass setup files (`ExposurePassSetup.cpp`, `HiZBuildPassSetup.cpp`) to appropriate R4 location

**Impact**: Cleanest removal; only removes R4-only functionality.

---

### Phase 2: State System Wrapping (Week 2)
1. Create agnostic wrapper headers for `r_constants.h`, `r_constants_cache.h`, `SH_Atomic.h`
2. Conditionally include dx11 implementations only when `USE_DX11` is defined
3. Or: Move entire constant buffer system to xrRenderDX11 and provide interface in xrRender

**Impact**: Moderate; affects Shader.h and its 24 dependents, but changes are local.

---

### Phase 3: R_Backend Abstraction (Week 3)
1. Create interface files `IBackendState.h`, `IStateCache.h` in xrRender
2. Move `R_Backend.h`, `R_Backend_Runtime.h` implementations to xrRenderDX11 (keep stub interfaces)
3. Update R_Backend_*.cpp files to use abstraction

**Impact**: High; R_Backend is the render state machine core.

---

### Phase 4: Device Abstraction Hardening (Week 3-4)
1. Ensure `RenderContext/RenderDevice.h` types are abstracted via NVRHI handles
2. Verify `DetailManager.h` uses opaque handles, not ID3D11Texture2D directly
3. Check `ShaderResourceTraits.h` for leaked D3D11 types

**Impact**: Moderate; these are already multi-backend aware but need verification.

---

## Summary Statistics

| Metric | Count |
|--------|-------|
| **Total Tainted Files** | 76 |
| **Direct xrRenderDX11 Includes** | 12 |
| **D3D11 Type References** | 16 |
| **IBlender/Blender_ References** | 39+ |
| **Leaf Files (moveable)** | ~40 |
| **API-Exposed Tainted** | 11 |
| **Estimated Decoupling Effort** | 4 weeks |

### Top 3 Offenders by Dependent Count

1. **Shader.h** — 24 xrRender dependents (includes r_constants.h, IBlender fwd decl)
2. **Blender.h** — 15 xrRender dependents (IBlender definition, blender ecosystem)
3. **r_constants.h** — 5 direct + 24 transitive (via Shader.h)

---

**Next Steps**: See P0.9 (Quarantine Blender_*.cpp) and P0.10 (Verify Shader.h agnosticism).

