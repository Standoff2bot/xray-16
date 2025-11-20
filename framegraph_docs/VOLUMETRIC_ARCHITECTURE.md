# Volumetric Lighting System Architecture
**X-Ray Engine Forward+ — Native Volumetric Support Design**

---

## The Architectural Insight

You've identified a critical design decision:

### ❌ Approach A: "Injection Model" (Initial Plan)
```
Phase 6: Build froxel system for world fog
Phase 6.5: Bolt on particle injection into froxels
```

**Problem:** Particles are an **afterthought** - "injected" into a system designed for fog.

---

### ✅ Approach B: "Native Volumetric Model" (Your Proposal)
```
Phase 6: Build volumetric lighting system (froxels are just storage)

Volumetric sources (all treated equally):
├── World fog (uniform density field)
├── Particle emitters (localized density contributions)
├── Light shafts (from sun/spotlights)
├── Height fog (exponential falloff)
└── Future: Volumetric GI, cloud shadows, etc.
```

**Benefit:** The lighting system **natively understands volumetrics**. Particles, fog, and light shafts are just different **volumetric primitives** that contribute to the same unified system.

---

## Why This Is Better Architecture

### 1. Cleaner Abstraction

**Injection Model:**
```cpp
// System "knows" about fog and particles separately
class FroxelRenderer {
    void InjectWorldFog();        // Fog-specific code
    void InjectParticles();       // Particle-specific code
    void InjectLightShafts();     // Light shaft-specific code
    // Every new volumetric effect needs a new injection function
};
```

**Native Volumetric Model:**
```cpp
// System knows about generic "volumetric contributions"
class VolumetricRenderer {
    void AddVolumetricPrimitive(const IVolumetricSource* source);
    void RenderVolume();  // Generic rendering, doesn't care about source type
};

// Different sources implement common interface
class WorldFog : public IVolumetricSource {
    void ContributeToFroxel(uint3 froxelCoord, FroxelData& inout) override;
};

class ParticleEmitter : public IVolumetricSource {
    void ContributeToFroxel(uint3 froxelCoord, FroxelData& inout) override;
};

class LightShaft : public IVolumetricSource {
    void ContributeToFroxel(uint3 froxelCoord, FroxelData& inout) override;
};
```

---

### 2. Extensibility

**New volumetric effects are trivial to add:**

```cpp
// Future: Volumetric decals (smoke on walls)
class VolumetricDecal : public IVolumetricSource {
    void ContributeToFroxel(uint3 froxelCoord, FroxelData& inout) override {
        // Project decal texture onto froxels within radius
    }
};

// Future: Volumetric GI (light bouncing through fog)
class VolumetricGI : public IVolumetricSource {
    void ContributeToFroxel(uint3 froxelCoord, FroxelData& inout) override {
        // Sample light probes, add indirect scattering
    }
};

// Just register with volumetric system - no changes to renderer
volumetricRenderer->AddVolumetricPrimitive(new VolumetricDecal(...));
volumetricRenderer->AddVolumetricPrimitive(new VolumetricGI(...));
```

---

### 3. Lighting Inheritance

**Key insight:** If the lighting system natively understands volumetrics, **all volumetric sources automatically get correct lighting** without special-case code.

**Example: Adding point light support**

**Injection Model (manual work):**
```cpp
// Must update EVERY injection function to support new light type
void FroxelRenderer::InjectWorldFog() {
    // ... existing sun lighting code
    // NEW: Add point light support (copy-paste for each source)
    for (auto& light : pointLights) {
        // ... lighting math
    }
}

void FroxelRenderer::InjectParticles() {
    // ... existing sun lighting code
    // NEW: Add point light support (duplicate code!)
    for (auto& light : pointLights) {
        // ... lighting math (same as above)
    }
}
```

**Native Model (automatic):**
```cpp
// Add point light support ONCE in the volumetric renderer
void VolumetricRenderer::RenderVolume() {
    // All sources already contributed density
    // Now light the entire volume (sun + point lights)
    for (auto& light : pointLights) {
        LightVolume(light);  // Automatically affects fog, particles, everything
    }
}
```

---

## Recommended Architecture

### Phase 6: Unified Volumetric System

**Core Abstraction:**

```cpp
// ═══════════════════════════════════════════════════════════════════════════
//  VOLUMETRIC SOURCE INTERFACE
// ═══════════════════════════════════════════════════════════════════════════

struct FroxelContribution {
    float density;          // Scattering coefficient
    float extinction;       // Absorption coefficient
    Fcolor albedo;          // Scattering color
    float anisotropy;       // Phase function (-1 to 1)
};

class IVolumetricSource {
public:
    virtual ~IVolumetricSource() = default;

    // Called during froxel injection pass
    // GPU-friendly: sources upload data to structured buffers
    virtual void PrepareGPUData(nvrhi::BufferHandle& outBuffer) = 0;

    // Shader binding (each source type has its own shader)
    virtual const char* GetShaderName() const = 0;

    // Bounds for culling (only evaluate froxels within bounds)
    virtual void GetWorldBounds(Fvector& outMin, Fvector& outMax) const = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
//  WORLD FOG SOURCE
// ═══════════════════════════════════════════════════════════════════════════

class WorldFogSource : public IVolumetricSource {
public:
    WorldFogSource(float density, float heightFalloff, const Fcolor& color)
        : m_density(density), m_heightFalloff(heightFalloff), m_color(color) {}

    void PrepareGPUData(nvrhi::BufferHandle& outBuffer) override {
        WorldFogParams params;
        params.density = m_density;
        params.heightFalloff = m_heightFalloff;
        params.color = m_color;
        // Upload to GPU buffer
    }

    const char* GetShaderName() const override {
        return "volumetric/world_fog.cs";
    }

    void GetWorldBounds(Fvector& outMin, Fvector& outMax) const override {
        // Infinite bounds (affects all froxels)
        outMin.set(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        outMax.set(FLT_MAX, FLT_MAX, FLT_MAX);
    }

private:
    float m_density;
    float m_heightFalloff;
    Fcolor m_color;
};

// ═══════════════════════════════════════════════════════════════════════════
//  PARTICLE EMITTER SOURCE
// ═══════════════════════════════════════════════════════════════════════════

class ParticleEmitterSource : public IVolumetricSource {
public:
    ParticleEmitterSource(const Fvector& pos, float radius, float density, const Fcolor& color)
        : m_position(pos), m_radius(radius), m_density(density), m_color(color) {}

    void PrepareGPUData(nvrhi::BufferHandle& outBuffer) override {
        ParticleEmitterParams params;
        params.position = m_position;
        params.radius = m_radius;
        params.density = m_density;
        params.color = m_color;
        // Upload to GPU buffer
    }

    const char* GetShaderName() const override {
        return "volumetric/particle_emitter.cs";
    }

    void GetWorldBounds(Fvector& outMin, Fvector& outMax) const override {
        // Localized bounds (only affects nearby froxels)
        outMin = m_position - Fvector(m_radius, m_radius, m_radius);
        outMax = m_position + Fvector(m_radius, m_radius, m_radius);
    }

private:
    Fvector m_position;
    float m_radius;
    float m_density;
    Fcolor m_color;
};

// ═══════════════════════════════════════════════════════════════════════════
//  LIGHT SHAFT SOURCE (Sun through gaps)
// ═══════════════════════════════════════════════════════════════════════════

class LightShaftSource : public IVolumetricSource {
public:
    LightShaftSource(const Fvector& sunDir, float intensity)
        : m_sunDirection(sunDir), m_intensity(intensity) {}

    void PrepareGPUData(nvrhi::BufferHandle& outBuffer) override {
        LightShaftParams params;
        params.sunDirection = m_sunDirection;
        params.intensity = m_intensity;
        // Upload to GPU buffer
    }

    const char* GetShaderName() const override {
        return "volumetric/light_shafts.cs";
    }

    void GetWorldBounds(Fvector& outMin, Fvector& outMax) const override {
        // Infinite bounds (sun affects all froxels)
        outMin.set(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        outMax.set(FLT_MAX, FLT_MAX, FLT_MAX);
    }

private:
    Fvector m_sunDirection;
    float m_intensity;
};

// ═══════════════════════════════════════════════════════════════════════════
//  VOLUMETRIC RENDERER (Generic)
// ═══════════════════════════════════════════════════════════════════════════

class VolumetricRenderer {
public:
    // Register volumetric sources (called by game systems)
    void RegisterSource(IVolumetricSource* source) {
        m_sources.push_back(source);
    }

    void ClearSources() {
        m_sources.clear();
    }

    // Setup FrameGraph pass
    VirtualResourceHandle SetupVolumetricPass(
        FrameGraph& fg,
        VirtualResourceHandle shadowMap,
        VirtualResourceHandle lightClusterData,
        u32 width,
        u32 height)
    {
        struct VolumetricPassData {
            VirtualResourceHandle froxelVolume;
            VirtualResourceHandle shadowMap;
            VirtualResourceHandle lightClusterData;
            // ... source buffers
        };

        auto& data = fg.addCallbackPass<VolumetricPassData>(
            "VolumetricLighting",

            // SETUP
            [&](FrameGraph& builder, PassHandle passHandle, VolumetricPassData& data) {
                RenderPassBuilder passBuilder(builder, passHandle);

                // Create froxel volume
                data.froxelVolume = passBuilder.createTexture3D(/* ... */);
                data.shadowMap = passBuilder.read(shadowMap, ResourceState::ShaderResource);
                data.lightClusterData = passBuilder.read(lightClusterData, ResourceState::ShaderResource);

                // Each source prepares its GPU data
                for (auto* source : m_sources) {
                    nvrhi::BufferHandle buffer;
                    source->PrepareGPUData(buffer);
                    // Store buffer for execute lambda
                }
            },

            // EXECUTE
            [](const VolumetricPassData& data, const FrameGraph& fg, ng::RenderContext* ctx) {
                // Step 1: Clear froxel volume
                ctx->ClearTexture(froxelVolume, float4(0, 0, 0, 0));

                // Step 2: Each source contributes to froxels (order-independent)
                for (auto* source : m_sources) {
                    // Dispatch compute shader for this source type
                    const char* shaderName = source->GetShaderName();
                    auto* shader = GetComputeShader(shaderName);

                    // Bind source-specific buffer + froxel volume UAV
                    ctx->BindShader(shader);
                    ctx->Dispatch(/* ... */);
                }

                // Step 3: Light the entire volume (sun + clustered lights)
                // This happens AFTER all sources contributed density
                auto* lightingShader = GetComputeShader("volumetric/apply_lighting.cs");
                ctx->BindShader(lightingShader);
                ctx->Dispatch(/* ... */);
            }
        );

        return data.froxelVolume;
    }

private:
    std::vector<IVolumetricSource*> m_sources;
};
```

---

## Shader Architecture

### Multi-Pass Froxel Injection

**Pass 1: Density Accumulation (Per-Source)**

Each volumetric source runs a compute shader that **adds** to the froxel volume:

```hlsl
// shaders/r5/volumetric/particle_emitter.cs

StructuredBuffer<ParticleEmitterParams> g_Emitters : register(t0);
RWTexture3D<float4> u_FroxelVolume : register(u0);  // Accumulation target

cbuffer Params : register(b0) {
    uint cb_NumEmitters;
};

[numthreads(8, 8, 1)]
void main(uint3 dtID : SV_DispatchThreadID) {
    if (any(dtID >= cb_FroxelDimensions)) return;

    float3 worldPos = FroxelToWorld(dtID);

    // Accumulate density from all emitters
    float totalDensity = 0.0;
    float3 totalAlbedo = float3(0, 0, 0);

    for (uint i = 0; i < cb_NumEmitters; i++) {
        ParticleEmitterParams emitter = g_Emitters[i];

        float dist = distance(worldPos, emitter.position);
        if (dist < emitter.radius) {
            float falloff = 1.0 - saturate(dist / emitter.radius);
            falloff = falloff * falloff;  // Quadratic

            totalDensity += emitter.density * falloff;
            totalAlbedo += emitter.color.rgb * falloff;
        }
    }

    // ADD to froxel volume (not overwrite!)
    // Multiple sources accumulate into same froxels
    float4 contribution = float4(totalDensity, totalDensity, 0, 0);
    u_FroxelVolume[dtID] += contribution;  // Atomic add (or use InterlockedAdd for precision)
}
```

**Pass 2: World Fog (Uniform)**

```hlsl
// shaders/r5/volumetric/world_fog.cs

RWTexture3D<float4> u_FroxelVolume : register(u0);

cbuffer WorldFogParams : register(b0) {
    float cb_BaseDensity;
    float cb_HeightFalloff;
    float3 cb_FogColor;
};

[numthreads(8, 8, 1)]
void main(uint3 dtID : SV_DispatchThreadID) {
    if (any(dtID >= cb_FroxelDimensions)) return;

    float3 worldPos = FroxelToWorld(dtID);

    // Height-based fog
    float heightFactor = exp(-worldPos.y * cb_HeightFalloff);
    float density = cb_BaseDensity * heightFactor;

    // ADD to existing density (from particles, etc.)
    float4 contribution = float4(density, density, 0, 0);
    u_FroxelVolume[dtID] += contribution;
}
```

**Pass 3: Lighting (Generic, Runs Once)**

```hlsl
// shaders/r5/volumetric/apply_lighting.cs

Texture2D t_ShadowMap : register(t0);
StructuredBuffer<LightData> g_Lights : register(t1);
StructuredBuffer<uint> g_LightIndices : register(t2);

RWTexture3D<float4> u_FroxelVolume : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtID : SV_DispatchThreadID) {
    if (any(dtID >= cb_FroxelDimensions)) return;

    // Read accumulated density from ALL sources
    float4 froxelData = u_FroxelVolume[dtID];
    float density = froxelData.r;

    if (density < 0.0001) {
        // Empty froxel - skip lighting
        return;
    }

    float3 worldPos = FroxelToWorld(dtID);

    // ══════════════════════════════════════════════════════════
    //  SUN LIGHTING
    // ══════════════════════════════════════════════════════════

    float shadowFactor = SampleShadowMap(worldPos);
    float3 sunLight = cb_SunColor.rgb * shadowFactor;

    // ══════════════════════════════════════════════════════════
    //  CLUSTERED POINT LIGHTS
    // ══════════════════════════════════════════════════════════

    uint3 cluster = WorldToCluster(worldPos);
    uint clusterIndex = GetClusterIndex(cluster);

    uint lightStart = g_LightIndices[clusterIndex * 2 + 0];
    uint lightCount = g_LightIndices[clusterIndex * 2 + 1];

    float3 pointLightContribution = float3(0, 0, 0);

    for (uint i = 0; i < lightCount; i++) {
        uint lightIndex = g_LightIndices[lightStart + i];
        LightData light = g_Lights[lightIndex];

        float3 L = normalize(light.position - worldPos);
        float distance = length(light.position - worldPos);
        float attenuation = 1.0 / (distance * distance);

        pointLightContribution += light.color * attenuation;
    }

    // ══════════════════════════════════════════════════════════
    //  FINAL INSCATTERING
    // ══════════════════════════════════════════════════════════

    float3 totalLight = sunLight + pointLightContribution;
    float inscattering = luminance(totalLight) * density;

    // Write back (R=density, G=extinction, B=anisotropy, A=inscattering)
    u_FroxelVolume[dtID] = float4(density, density, 0, inscattering);
}
```

---

## Integration with Particle System

**Key Difference from Injection Model:**

Particles **don't know about froxels**. They just register as volumetric sources.

```cpp
// ParticleManager.cpp

void ParticleManager::UpdateVolumetricParticles(VolumetricRenderer* volumetricRenderer) {
    // Clear previous frame's sources
    volumetricRenderer->ClearSources();

    // Register world fog (always active)
    static WorldFogSource worldFog(0.01f, 0.05f, Fcolor(0.5f, 0.6f, 0.7f));
    volumetricRenderer->RegisterSource(&worldFog);

    // Register active particle emitters
    for (auto& effect : m_activeEffects) {
        if (effect.lightingMode == ParticleLightingMode::Volumetric) {
            // Create emitter source (stack-allocated, temporary)
            ParticleEmitterSource emitter(
                effect.GetPosition(),
                effect.GetRadius(),
                effect.GetDensity(),
                effect.GetColor()
            );

            volumetricRenderer->RegisterSource(&emitter);
        }
    }

    // Volumetric renderer handles the rest (no particle-specific code in renderer!)
}
```

---

## Benefits Summary

### 1. **Extensibility**
- New volumetric effects = implement IVolumetricSource (30 lines of code)
- No changes to core renderer

### 2. **Lighting Inheritance**
- Add point light support ONCE → all volumetric sources get it
- Add volumetric GI → automatically affects fog, particles, everything

### 3. **Separation of Concerns**
- Particle system doesn't know about froxels
- Froxel renderer doesn't know about particles
- Clean interface: `IVolumetricSource`

### 4. **Debugging**
- Disable individual sources to isolate issues
- Visualize contribution from each source type

### 5. **Performance**
- Easy to add culling (skip sources outside frustum)
- Easy to add LOD (reduce froxel resolution for distant sources)

---

## Implementation Timeline

**Phase 6: Volumetric Lighting System (Weeks 11-15)**

- **Week 11-12:** Core froxel infrastructure
  - Froxel grid math
  - 3D texture management
  - Temporal reprojection
  - Ray marching shader

- **Week 13:** Volumetric source abstraction
  - `IVolumetricSource` interface
  - `VolumetricRenderer` class
  - Multi-pass injection system

- **Week 14:** First sources
  - `WorldFogSource` (uniform fog)
  - `ParticleEmitterSource` (localized smoke)
  - `LightShaftSource` (sun rays)

- **Week 15:** Lighting integration
  - Apply clustered lights to froxels
  - Shadow map integration
  - Temporal reprojection tuning

**Phase 6.5: Particle Migration (Weeks 16-20)**
- Particles naturally use volumetric system (Tier 1)
- Add Unity 6-way for hero effects (Tier 2)
- Add simple lit particles (Tier 3)
- Add emissive particles (Tier 4)

---

## Recommendation

**Your architectural insight is 100% correct.**

Implement volumetric lighting as a **native system** from the start:
- ✅ Cleaner abstraction (`IVolumetricSource`)
- ✅ Better extensibility (new sources are trivial)
- ✅ Lighting inheritance (automatic for all sources)
- ✅ Separation of concerns (particles don't know about froxels)

This is how **Doom Eternal** and **UE5** actually implement volumetrics - not as "fog with particle injection" but as a **unified volumetric rendering system** with multiple contributors.

**Do this from the start in Phase 6** - it's not significantly more complex than the injection model, and it provides a much better foundation for the future.
