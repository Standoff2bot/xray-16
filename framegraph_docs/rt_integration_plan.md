# Render Target Integration Plan
## FrameGraph + Vanilla Code Resource Sharing

### Current Status (2025-01-06)

**Problem:** Dual resource management causing TDR
- `CRenderTarget` creates RTs via `CRT::create()` → NVRHI
- `FrameGraphRenderer` creates separate RTs via `FrameGraph::CreateRenderTarget()`
- Both systems try to manage D3D11 state → **Resource hazards → TDR**

**Solution:** Unified RT access through RenderTargetRegistry
- FrameGraph creates and owns all RTs
- Vanilla code accesses FrameGraph RTs via registry lookup
- No duplicate RT creation when FrameGraph enabled

---

## Architecture

### Phase 1: Registry Integration (✅ COMPLETED)

**Added to FrameGraphRenderer.h:**
```cpp
// Get FrameGraph instance (for RT registry access)
framegraph::FrameGraph* GetFrameGraph() const;

// Get physical texture from virtual handle
nvrhi::ITexture* GetPhysicalTexture(VirtualResourceHandle handle) const;

// Get VirtualResourceHandle by name
VirtualResourceHandle GetRTHandle(const char* name) const;

// Check if RT exists
bool HasRT(const char* name) const;
```

**Legacy RT Name Aliases (registered in BuildFrameGraphStructure):**
- `"$user$position"` → `m_rt_Position` (r2_RT_P)
- `"$user$normal"` → `m_rt_Normal` (r2_RT_N)
- `"$user$albedo"` → `m_rt_Albedo` (r2_RT_albedo)
- `"$user$accum"` → `m_rt_Accumulator` (r2_RT_accum)
- `"$user$generic0/1/2"` → `m_rt_Generic_0/1/2`
- `"$user$base_depth"` → `m_rt_Depth`

### Phase 2: Vanilla Code Migration (NEXT)

**Current CRenderTarget Initialization:**
```cpp
// r2.cpp:576
if (!ps_r4_use_framegraph) {
    Target = xr_new<CRenderTarget>(); // Creates duplicate RTs
} else {
    Target = nullptr; // Skip legacy initialization
}
```

**Proposed Access Pattern for Vanilla Code:**

#### Option A: Direct FrameGraph Access (Preferred)
```cpp
// Legacy code that used CRenderTarget::rt_Position
if (ps_r4_use_framegraph && RImplementation.m_framegraphRenderer) {
    auto handle = RImplementation.m_framegraphRenderer->GetRTHandle("$user$position");
    nvrhi::ITexture* tex = RImplementation.m_framegraphRenderer->GetPhysicalTexture(handle);
    // Use tex...
} else {
    // Legacy path
    auto& rt = RImplementation.Target->rt_Position;
}
```

#### Option B: CRenderTarget Wrapper (If needed)
Create thin CRenderTarget that wraps FrameGraph RTs instead of creating new ones.
```cpp
class CRenderTarget {
    ref_rt rt_Position; // Points to FrameGraph RT via handle wrapper
    // ...

    void Initialize_FrameGraphMode() {
        // Wrap FrameGraph RTs instead of creating new ones
        rt_Position.wrap_framegraph(RImplementation.m_framegraphRenderer->GetRTHandle("$user$position"));
    }
};
```

---

## RT Name Mapping

| Legacy Name (r2_types.h) | FrameGraph Registry Names | FrameGraph Member |
|--------------------------|---------------------------|-------------------|
| `r2_RT_P` ("$user$position") | "rt_Position", "$user$position", "s_position" | `m_rt_Position` |
| `r2_RT_N` ("$user$normal") | "rt_Normal", "$user$normal", "s_normal" | `m_rt_Normal` |
| `r2_RT_albedo` ("$user$albedo") | "rt_Albedo", "rt_Color", "$user$albedo", "s_albedo" | `m_rt_Albedo` |
| `r2_RT_accum` ("$user$accum") | "rt_Accumulator", "$user$accum", "s_accumulator" | `m_rt_Accumulator` |
| `r2_RT_generic0` | "rt_Generic_0", "$user$generic0", "s_generic0" | `m_rt_Generic_0` |
| `r2_RT_generic1` | "rt_Generic_1", "$user$generic1", "s_generic1" | `m_rt_Generic_1` |
| `r2_RT_generic2` | "rt_Generic_2", "$user$generic2", "s_generic2" | `m_rt_Generic_2` |
| `r2_RT_base_depth` | "rt_Depth", "$user$base_depth", "s_depth" | `m_rt_Depth` |

---

## Implementation Phases

### ✅ Phase 1: FrameGraph Accessors (DONE)
- [x] Add `GetFrameGraph()`, `GetPhysicalTexture()`, `GetRTHandle()`, `HasRT()` to FrameGraphRenderer
- [x] Register legacy RT name aliases in BuildFrameGraphStructure
- [x] Skip `CRenderTarget` initialization when FrameGraph enabled (r2.cpp:574-580)

### 🔄 Phase 2: Vanilla Code Migration (IN PROGRESS)
- [ ] Identify all `RImplementation.Target->rt_*` usage in vanilla rendering
- [ ] Replace with FrameGraph RT lookups when `ps_r4_use_framegraph` enabled
- [ ] Test menu rendering with FrameGraph RTs

### 📋 Phase 3: Texture Migration (FUTURE)
- [ ] Migrate `CTexture` creation to use FrameGraph texture registry
- [ ] Ensure all texture creation goes through `ResourceManager`
- [ ] Remove duplicate texture management

### 🎯 Phase 4: Complete Separation (FUTURE)
- [ ] Hook into `CApplication::Run()` with separate FrameGraph `ProcessFrame()`
- [ ] Complete render loop separation (user's suggestion from previous session)

---

## Testing Plan

1. **Build Test:**
   - Ensure no compilation errors
   - Verify RT registry lookups compile correctly

2. **Menu Rendering Test:**
   - Run game with `r4_use_framegraph 1`
   - Verify menu renders without TDR
   - Check log for RT registry messages

3. **RT Lookup Test:**
   - Add debug logging to RT access points
   - Verify FrameGraph RTs are being used (not legacy CRT)

---

## Benefits

✅ **Single Source of Truth:** FrameGraph owns all RTs
✅ **No Duplication:** Vanilla code uses FrameGraph RTs directly
✅ **Future-Proof:** Ready for DX12/Vulkan via NVRHI
✅ **Clean Separation:** `ps_r4_use_framegraph` flag controls entire pipeline

---

## Notes

- **CRT already uses NVRHI!** The `CRT::create()` path already creates textures via `RenderDevice::CreateTexture()` with NVRHI handles. The problem isn't the texture creation method, it's the **dual ownership**.

- **FrameGraph RTs are already NVRHI-backed.** No conversion needed.

- **Shader compatibility:** Legacy shaders expect names like `"$user$position"`, which we've now registered as aliases.

- **User's architectural vision:** Complete render loop separation via `CApplication::Run()` hook is the end goal, but incremental RT sharing is the safe first step.

---

**Last Updated:** 2025-01-06
**Status:** Phase 1 complete, ready to test
