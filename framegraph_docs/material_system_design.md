# Material System Design for FrameGraph Renderer

## Overview

This document describes the material/shader system integration with the FrameGraph renderer, including PSO creation, binding layouts, and texture management.

---

## X-Ray Shader Hierarchy

### Structure Discovery

X-Ray uses a hierarchical shader system:

```cpp
// Visual has reference to shader
dxRender_Visual::shader                    // ref_shader (line FBasicVisual.h:67)
  ↓
Shader                                      // Shader.h:136
  E[6]: ref_selement[]                     // Array of ShaderElements for different render modes
                                           // E[0] = deferred rendering (what we use)
  ↓
ShaderElement                               // Shader.h:115
  passes: svector<ref_pass, 2>             // Up to 2 passes (usually just 1)
  flags: Sflags                            // Priority, emissive, distort, etc.
  ↓
SPass                                       // Shader.h:88
  state: ref_state                         // Render states (Z-buffer, blend, etc.)
  vs: ref_vs                               // Vertex shader
  ps: ref_ps                               // Pixel shader
  gs: ref_gs                               // Geometry shader (optional)
  hs/ds/cs: ref_hs/ds/cs                   // Hull/Domain/Compute (DX11)
  T: ref_texture_list                      // *** TEXTURES HERE ***
  C: ref_constant_list                     // Constants
  M: ref_matrix_list                       // Matrices
  ↓
STextureList                                // Shader.h:26
  vector<pair<u32, ref_texture>>           // (stage, texture) pairs
  ↓
CTexture                                    // SH_Texture.h:10
  get_SRView() -> ID3DShaderResourceView*  // D3D11 texture view (line 114)
  surface_get() -> ID3DBaseTexture*        // D3D11 texture resource
```

### Key Access Paths

```cpp
// Get shader element for deferred rendering
ShaderElement* elem = visual->shader->E[0]._get();

// Get first pass (GBuffer pass)
SPass* pass = elem->passes[0]._get();

// Get texture list
STextureList* textures = pass->T._get();

// Iterate textures
for (auto& [stage, tex] : *textures) {
    ID3DShaderResourceView* srv = tex->get_SRView();
    // Use srv for binding...
}
```

---

## Material System Architecture

### Goals

1. **Per-Material PSOs**: Each unique shader/texture combination gets its own PSO
2. **Proper Binding Integration**: PSOs created WITH binding layouts, not separately
3. **Efficient Caching**: Hash-based lookup to avoid recreating PSOs
4. **Clean State Management**: Single `setGraphicsState()` call with everything

### Components

#### 1. MaterialKey (for PSO cache lookup)

```cpp
struct MaterialKey {
    Shader* shader;              // Pointer to shader object
    u64 textureHash;             // Hash of texture combination
    u64 stateHash;               // Hash of render state

    bool operator<(const MaterialKey& other) const {
        if (shader != other.shader) return shader < other.shader;
        if (textureHash != other.textureHash) return textureHash < other.textureHash;
        return stateHash < other.stateHash;
    }
};
```

#### 2. MaterialPSO (cached PSO + bindings)

```cpp
struct MaterialPSO {
    ng::PipelineState* pso;                  // Graphics pipeline
    nvrhi::BindingLayoutHandle bindingLayout;// What resources PSO expects
    nvrhi::BindingSetHandle bindingSet;      // Actual bound resources

    // Extracted data for quick access
    xr_vector<ID3DShaderResourceView*> textures;
    u32 vertexStride;
    nvrhi::Format indexFormat;
};
```

#### 3. MaterialCache (PSO manager)

```cpp
class MaterialCache {
public:
    MaterialPSO* GetOrCreatePSO(
        dxRender_Visual* visual,
        const GBufferOutputs& outputs,
        ng::RenderDevice* device);

private:
    xr_map<MaterialKey, xr_unique_ptr<MaterialPSO>> m_cache;

    MaterialPSO* CreatePSO(
        ShaderElement* elem,
        SPass* pass,
        const GBufferOutputs& outputs,
        ng::RenderDevice* device);
};
```

---

## PSO Creation with Binding Layout Integration

### Current Problem

Currently we create PSO and bindings separately:

```cpp
// WRONG: Create PSO without binding layout
CreatePipeline(outputs, fg);  // No binding info!

// WRONG: Create binding set later per-frame
ctx.SetConstantBuffer(0, buffer);  // Creates temp binding set every frame!
```

This is inefficient and doesn't match how modern APIs work.

### Correct Approach

Create PSO WITH binding layout from the start:

```cpp
MaterialPSO* MaterialCache::CreatePSO(
    ShaderElement* elem,
    SPass* pass,
    const GBufferOutputs& outputs,
    ng::RenderDevice* device)
{
    auto pso = xr_make_unique<MaterialPSO>();

    // Step 1: Extract textures
    STextureList* texList = pass->T._get();
    if (texList) {
        for (auto& [stage, tex] : *texList) {
            pso->textures.push_back(tex->get_SRView());
        }
    }

    // Step 2: Create binding layout (FIRST!)
    // This describes what resources the PSO expects
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;

    // Slot 0: Per-object constant buffer
    layoutDesc.bindings.push_back(
        nvrhi::BindingLayoutItem::ConstantBuffer(0));

    // Slots 1+: Textures
    for (u32 i = 0; i < pso->textures.size(); i++) {
        layoutDesc.bindings.push_back(
            nvrhi::BindingLayoutItem::Texture_SRV(i + 1));
    }

    pso->bindingLayout = device->CreateBindingLayout(layoutDesc);

    // Step 3: Create PSO with binding layout
    nvrhi::GraphicsPipelineDesc psoDesc;
    psoDesc.bindingLayouts = { pso->bindingLayout };  // *** KEY: PSO knows what it needs! ***
    psoDesc.VS = LoadVertexShader(pass->vs);
    psoDesc.PS = LoadPixelShader(pass->ps);
    psoDesc.renderState = ConvertRenderState(pass->state);
    // ... vertex format, render targets, etc.

    nvrhi::IFramebuffer* tempFB = CreateTempFramebuffer(outputs);
    pso->pso = device->CreateGraphicsPipeline(psoDesc, tempFB);

    return pso.release();
}
```

---

## Binding Set Creation

### Per-Frame Binding Updates

For each draw call, create binding set with actual resources:

```cpp
void GBufferPass::Execute(...) {
    for (auto& batch : batches) {
        MaterialPSO* mat = m_materialCache->GetOrCreatePSO(batch.visual, outputs, device);

        // Create binding set for this frame (binds actual resources)
        nvrhi::BindingSetDesc bindingDesc;

        // Bind constant buffer (updated per-object)
        bindingDesc.bindings.push_back(
            nvrhi::BindingSetItem::ConstantBuffer(0, m_perObjectCB));

        // Bind textures from material
        for (u32 i = 0; i < mat->textures.size(); i++) {
            bindingDesc.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(i + 1, mat->textures[i]));
        }

        nvrhi::BindingSetHandle bindingSet =
            device->CreateBindingSet(bindingDesc, mat->bindingLayout);

        // Set complete graphics state in ONE call
        nvrhi::GraphicsState state;
        state.pipeline = mat->pso->GetNativePipeline();
        state.bindings = { bindingSet };
        state.vertexBuffers = { batch.vertexBuffer };
        state.indexBuffer = { batch.indexBuffer, nvrhi::Format::R16_UINT, 0 };

        cmdList->setGraphicsState(state);
        cmdList->drawIndexed(...);
    }
}
```

---

## Optimization: Binding Set Caching

### Problem

Creating binding sets every frame is wasteful if constant buffer doesn't change.

### Solution (Future)

Cache binding sets per (MaterialPSO, ConstantBuffer) pair:

```cpp
struct BindingCacheKey {
    MaterialPSO* material;
    nvrhi::IBuffer* constantBuffer;
};

xr_map<BindingCacheKey, nvrhi::BindingSetHandle> m_bindingCache;
```

For now, create per-frame (NVRHI internally caches anyway).

---

## Vertex Format Compatibility

### X-Ray Vertex Formats

Need to verify what vertex format X-Ray uses. Common formats:

```cpp
// Check in visual->rm_geom->dcl (vertex declaration)
// Likely format:
struct Vertex {
    Fvector3 position;   // POSITION
    Fvector3 normal;     // NORMAL
    Fvector2 texcoord;   // TEXCOORD0
    Fvector3 tangent;    // TANGENT (maybe)
    Fvector3 binormal;   // BINORMAL (maybe)
};
```

Must match shader input layout in `gbuffer.vs.hlsl`.

---

## Implementation Plan

### Phase 1: Basic Material Extraction (Current)

1. ✅ Understand shader hierarchy
2. ✅ Document material system
3. ⏳ Extract textures from visual->shader
4. ⏳ Create MaterialKey for hashing

### Phase 2: PSO with Binding Layout

5. ⏳ Design binding layout (CB + textures)
6. ⏳ Create PSO WITH binding layout
7. ⏳ Implement MaterialCache

### Phase 3: Integration

8. ⏳ Update GBufferPass to use MaterialCache
9. ⏳ Create binding sets per-frame
10. ⏳ Update state management (single setGraphicsState)

### Phase 4: Testing & Optimization

11. ⏳ Test rendering with materials
12. ⏳ Verify texture sampling works
13. ⏳ Add binding set caching
14. ⏳ Profile and optimize

---

## Code Locations

- **Visual Structure**: `src/Layers/xrRender/FBasicVisual.h:53-84`
- **Shader Structure**: `src/Layers/xrRender/Shader.h:136-153`
- **Texture Access**: `src/Layers/xrRender/SH_Texture.h:10-150`
- **GBufferPass**: `src/Layers/xrRender/FrameGraphPasses/GBufferPass.cpp`
- **GeometryBatch**: `src/Layers/xrRender/Geometry/GeometryBatch.h`

---

## X-Ray Shader System Clarification

### Compile Time vs Runtime

**Compile Time** (Blenders):
- Blenders are C++ code that runs during **shader compilation** (not runtime!)
- Example: `CBlender_Model::CompileProgrammable()` runs when building shaders
- Generates `SPass` objects with shader names + textures + render states
- Output: `.ps`, `.vs` HLSL source files → compiled to bytecode

```cpp
// Example Blender (runs at COMPILE TIME):
void CBlender_Model::CompileProgrammable(CBlender_Compile& C) {
    C.r_Pass("model_def_hq", "model_def_hq", TRUE);  // VS + PS names
    C.r_Sampler("s_base", C.L_textures[0]);         // Bind texture slot
    C.r_End();
}
```

**Runtime** (What we use):
- Visuals have pre-compiled `ref_shader` pointing to `Shader` objects
- `Shader::E[0]` (deferred mode) has `ShaderElement` with compiled passes
- `SPass` contains:
  - `vs/ps`: **Pre-compiled bytecode** (ref_vs, ref_ps)
  - `T`: Texture list (already resolved!)
  - `state`: Render states (already configured!)

### Key Insight

**WE DON'T RUN BLENDERS AT RUNTIME!**

The blenders already ran when the game's shaders were compiled. At runtime:
1. Visual references a Shader
2. Shader has pre-built SPasses
3. We extract VS/PS bytecode + textures + states from SPass
4. Create NVRHI PSO from this data

### Modern DX11 Shaders

**NOT Fixed-Function Pipeline (FFP)**:
- FFP is only for legacy R1 renderer / SDK tools
- Modern DX11/DX10 uses real HLSL shaders
- `.ps` / `.vs` files in `gamedata/shaders/r3/` are HLSL source
- Compiled to bytecode at shader compile time
- Stored in `ref_vs` / `ref_ps` at runtime

**Example Shader Names**:
- `model_def_hq.vs` / `model_def_hq.ps` - Model high quality
- `base.vs` / `base.ps` - Basic deferred geometry
- `accum_sun.vs` / `accum_sun.ps` - Sun lighting

---

## Notes

- At runtime, shaders are already compiled (bytecode in ref_vs/ref_ps)
- Some visuals may have NULL shader (use default pass)
- Texture stages may not be sequential (gaps in stage numbers)
- Render states in SPass are D3D9-style, need conversion to NVRHI
- Use shader element E[0] (SE_R2_NORMAL_HQ) for deferred rendering
- Blenders + .s scripts are compile-time only, not runtime

---

**Last Updated**: 2025-01-31
**Status**: Design Complete, Ready for Implementation
