# Steam Audio Integration Plan for OpenXRay

## Executive Summary

This document outlines a comprehensive plan to integrate Steam Audio into OpenXRay's xrSound library, providing modern spatial audio capabilities including physics-based occlusion, realistic reflections/reverb, and HRTF-based 3D audio while maintaining compatibility with the original game's audio systems.

## Table of Contents

1. [Why Steam Audio?](#why-steam-audio)
2. [Current xrSound Architecture Analysis](#current-xrsound-architecture-analysis)
3. [Steam Audio Overview](#steam-audio-overview)
4. [Integration Strategy](#integration-strategy)
5. [Implementation Phases](#implementation-phases)
6. [Technical Architecture](#technical-architecture)
7. [Compatibility & Migration](#compatibility--migration)
8. [Performance Considerations](#performance-considerations)
9. [Testing Strategy](#testing-strategy)
10. [Timeline & Resources](#timeline--resources)

---

## Why Steam Audio?

### Benefits Over Current OpenAL Implementation

| Feature | Current (OpenAL) | With Steam Audio |
|---------|------------------|------------------|
| **Basic 3D Audio** | ✓ Distance attenuation, basic positioning | ✓ Enhanced with HRTF for accurate directional audio |
| **Occlusion** | ✓ Basic raycast occlusion | ✓ Physics-based with partial occlusion & transmission |
| **Reverb/Effects** | ✓ EAX effects (limited) | ✓ Convolution reverb, geometry-aware |
| **Sound Propagation** | ✗ Direct path only | ✓ Multi-path (hallways, corners, doors) |
| **Dynamic Acoustics** | ✗ Static only | ✓ Moving geometry support (doors, elevators) |
| **Material Properties** | ✗ Not supported | ✓ Acoustic material simulation |
| **Performance** | Single-threaded | Multi-core CPU + GPU acceleration |
| **VR Support** | Limited | First-class HRTF & binaural rendering |
| **License** | LGPL | Apache 2.0 (more permissive) |

### Use Cases for S.T.A.L.K.E.R.

1. **Realistic Underground Environments**: Accurate reverb in labs, tunnels, and bunkers
2. **Dynamic Combat**: Bullets and explosions propagating through corridors realistically
3. **Anomaly Ambience**: Spatial audio for anomaly effects with environmental interaction
4. **Outdoor vs Indoor**: Automatic acoustic transitions when entering/exiting buildings
5. **Enhanced Immersion**: HRTF-based audio for better directional awareness
6. **Modder-Friendly**: Physics-based audio automatically adapts to custom levels

---

## Current xrSound Architecture Analysis

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    xrSound Current Architecture              │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌─────────────────┐                                         │
│  │ ISoundManager   │  (Interface to game engine)            │
│  │ ISoundScene     │                                         │
│  └────────┬────────┘                                         │
│           │                                                   │
│  ┌────────▼──────────────────────────────────┐              │
│  │     CSoundRender_Core                      │              │
│  │  - Listener management                     │              │
│  │  - Scene management                        │              │
│  │  - Source cache                            │              │
│  │  - Target pool                             │              │
│  └────────┬───────────────────┬───────────────┘              │
│           │                   │                               │
│  ┌────────▼─────────┐  ┌─────▼────────────┐                 │
│  │ CSoundRender_    │  │  CSoundRender_   │                 │
│  │ CoreA (OpenAL)   │  │  Effects (EAX)   │                 │
│  └────────┬─────────┘  └──────────────────┘                 │
│           │                                                   │
│  ┌────────▼────────────────────────────────────┐            │
│  │          CSoundRender_Scene                  │            │
│  │  - Emitter management                        │            │
│  │  - Occlusion queries (via xrCDB)            │            │
│  │  - Environment management                    │            │
│  └────────┬─────────────────────────────────────┘            │
│           │                                                   │
│  ┌────────▼────────────────────┐                             │
│  │  CSoundRender_Emitter (N)   │                             │
│  │  - 3D position               │                             │
│  │  - Volume/frequency          │                             │
│  │  - State machine             │                             │
│  │  - OGG streaming             │                             │
│  └────────┬────────────────────┘                             │
│           │                                                   │
│  ┌────────▼────────────────────┐                             │
│  │  CSoundRender_Target (M)    │                             │
│  │  - OpenAL buffer mgmt        │                             │
│  │  - Playback control          │                             │
│  └────────┬────────────────────┘                             │
│           │                                                   │
│  ┌────────▼────────────────────┐                             │
│  │  CSoundRender_Source        │                             │
│  │  - OGG Vorbis data          │                             │
│  │  - PCM/Float32 support      │                             │
│  │  - Metadata (volume, range) │                             │
│  └─────────────────────────────┘                             │
│           │                                                   │
│  ┌────────▼────────────────────┐                             │
│  │     OpenAL Runtime           │                             │
│  │  - Hardware mixing           │                             │
│  │  - Basic 3D positioning      │                             │
│  └─────────────────────────────┘                             │
└─────────────────────────────────────────────────────────────┘
```

### Key Classes

1. **CSoundRender_Core**: Core manager
   - Source cache management
   - Listener position/orientation
   - Scene lifecycle
   - Timer management

2. **CSoundRender_CoreA**: OpenAL implementation
   - ALCdevice/ALCcontext management
   - Master volume control
   - Device enumeration

3. **CSoundRender_Scene**: Per-scene sound management
   - Emitter pool
   - Occlusion queries (via CDB::MODEL)
   - Environment management
   - Event propagation

4. **CSoundRender_Emitter**: Sound instance
   - State machine (stopped, starting, playing, simulating)
   - OGG Vorbis streaming
   - Block-based prefill
   - Priority management

5. **CSoundRender_Target**: Playback target
   - OpenAL buffer/source management
   - Rendering state
   - Parameter application

6. **CSoundRender_Source**: Audio data
   - OGG Vorbis file loading
   - PCM/Float32 format support
   - Metadata (volume, distance curves)

### Audio Pipeline

```
Sound File (.ogg)
    → CSoundRender_Source::load()
    → OGG Vorbis decompression
    → CSoundRender_Emitter::start()
    → Block prefill (100ms chunks)
    → CSoundRender_Target rendering
    → OpenAL alBufferData()
    → Audio output
```

### Dependencies

- **OpenAL** (AL/ALC): 3D audio backend
- **libvorbis/libvorbisfile**: OGG Vorbis codec
- **xrCDB**: Collision detection for occlusion
- **xrCore**: Core utilities

---

## Steam Audio Overview

### Core Features

1. **HRTF-Based Binaural Rendering**
   - Head-Related Transfer Functions for accurate 3D positioning
   - Height perception (above/below listener)
   - Front/back discrimination

2. **Physics-Based Occlusion & Transmission**
   - Ray-traced occlusion
   - Partial occlusion for large sources
   - Sound transmission through materials
   - Frequency-dependent absorption

3. **Multi-Path Sound Propagation**
   - Direct path
   - Early reflections
   - Late reverb (diffuse)
   - Indirect paths (hallways, doors)

4. **Convolution Reverb**
   - Geometry-aware impulse responses
   - Real-time or baked
   - Frequency-accurate reflections

5. **Material Simulation**
   - Absorption coefficients per material
   - Scattering properties
   - Transmission loss

6. **Dynamic Acoustics**
   - Moving geometry support
   - Runtime scene updates
   - Doors, elevators, destructible objects

7. **Performance Optimization**
   - Multi-threaded simulation
   - GPU acceleration (Radeon Rays, TrueAudio Next, CUDA)
   - Baked data for static geometry

### API Architecture

#### Core Types

```cpp
// Context - global state
IPLContext context;

// Scene - geometry for acoustic simulation
IPLScene scene;
IPLStaticMesh mesh;
IPLInstancedMesh instancedMesh;

// Simulation - acoustic calculations
IPLSimulator simulator;
IPLSource source;

// Rendering - audio processing
IPLHRTFEffect hrtfEffect;
IPLDirectEffect directEffect;
IPLReflectionEffect reflectionEffect;
IPLPathEffect pathEffect;

// Audio buffers
IPLAudioBuffer inputBuffer;
IPLAudioBuffer outputBuffer;
```

#### Basic Workflow

```cpp
// 1. Initialize
iplContextCreate(&contextSettings, &context);
iplHRTFCreate(context, &audioSettings, &hrtfSettings, &hrtf);

// 2. Create scene geometry
iplSceneCreate(context, &sceneSettings, &scene);
iplStaticMeshCreate(scene, &meshSettings, &mesh);
iplStaticMeshLoad(mesh, serialized_data);
iplSceneCommit(scene);

// 3. Create simulator
iplSimulatorCreate(context, &simulatorSettings, &simulator);
iplSimulatorSetScene(simulator, scene);
iplSimulatorCommit(simulator);

// 4. Create source
iplSourceCreate(simulator, &sourceSettings, &source);
iplSourceAdd(source, simulator);
iplSimulatorCommit(simulator);

// 5. Per-frame: Run simulation
iplSourceSetInputs(source, flags, &sourceInputs);
iplSimulatorRunDirect(simulator);
iplSimulatorRunReflections(simulator);

// 6. Per-frame: Apply effects
iplDirectEffectApply(directEffect, &directEffectParams, &inBuffer, &outBuffer);
iplReflectionEffectApply(reflectionEffect, &reflectionParams, &inBuffer, &outBuffer, nullptr);
```

### Integration with OpenAL

Steam Audio can work **alongside** OpenAL:
- **OpenAL**: Handles audio playback, streaming, basic positioning
- **Steam Audio**: Processes spatial effects (occlusion, reflections, HRTF)

**Hybrid Workflow**:
```
Source Audio
    → Steam Audio processing (HRTF, occlusion, reflections)
    → OpenAL buffer
    → OpenAL playback
```

**Alternative Workflow** (OpenAL-free):
```
Source Audio
    → Steam Audio processing
    → Custom mixer/output
    → Platform audio API (WASAPI, CoreAudio, ALSA)
```

### License & Distribution

- **License**: Apache 2.0 (permissive, commercial-friendly)
- **Source**: https://github.com/ValveSoftware/steam-audio
- **Platforms**: Windows, Linux, macOS, Android, iOS
- **Cost**: Free, no royalties

---

## Integration Strategy

### Option 1: Hybrid Approach (Recommended)

**Keep OpenAL for playback, add Steam Audio for spatial processing**

**Pros**:
- Lower risk - OpenAL playback is proven
- Gradual migration path
- Can toggle Steam Audio features on/off
- Existing EAX code can coexist

**Cons**:
- Two audio systems in memory
- Some duplicate functionality
- Extra audio processing step

**Architecture**:
```
CSoundRender_Core
    ├── CSoundRender_CoreA (OpenAL) - keeps playback
    ├── CSoundRender_SteamAudio - NEW spatial processing
    └── CSoundRender_Effects - enhanced with Steam Audio
```

### Option 2: Full Replacement

**Replace OpenAL entirely with Steam Audio**

**Pros**:
- Single audio system
- Full control over audio pipeline
- Maximum Steam Audio features
- Cleaner architecture long-term

**Cons**:
- High risk - major rewrite
- Loss of OpenAL device enumeration
- Need custom audio output layer
- Longer development time

### Option 3: Abstraction Layer

**Create audio backend abstraction, support both**

**Pros**:
- Users can choose backend
- Best for testing/comparison
- Future-proof for other backends

**Cons**:
- Most complex implementation
- Maintenance burden
- Potential for inconsistent behavior

### Recommended: **Option 1 (Hybrid)**

Start with hybrid approach:
1. Add Steam Audio as optional effects processor
2. Keep OpenAL for playback and device management
3. Gradually enhance features with Steam Audio
4. Later: consider Option 2 if needed

---

## Implementation Phases

### Phase 1: Foundation (2-3 weeks)

**Goal**: Add Steam Audio SDK and basic infrastructure

**Tasks**:
1. Add Steam Audio as git submodule to `Externals/`
2. Update CMake build system
   - Add `XRSOUND_USE_STEAMAUDIO` option
   - Link Steam Audio libraries
3. Create wrapper classes
   - `CSteamAudioContext` - global Steam Audio state
   - `CSteamAudioScene` - scene geometry management
   - `CSteamAudioMaterial` - material property database
4. Add compile-time feature flags
   - `#if XRSOUND_USE_STEAMAUDIO`
5. Create initialization/shutdown hooks in `CSoundRender_Core`

**Deliverables**:
- Steam Audio SDK integrated in build
- Basic context creation working
- No functionality changes (foundation only)

**Testing**:
- Build with Steam Audio enabled/disabled
- Verify no regressions

---

### Phase 2: Geometry Integration (3-4 weeks)

**Goal**: Feed level geometry to Steam Audio

**Tasks**:
1. Add geometry export to Steam Audio scene
   - Hook into level loading (`CSoundRender_Scene::set_geometry_occ`)
   - Convert `CDB::MODEL` to Steam Audio mesh
   - Material mapping (X-Ray materials → Steam Audio absorption)
2. Implement material system
   - Default material properties
   - Configurable material database (config file)
   - Material inheritance from game materials
3. Scene management
   - Static geometry (buildings, terrain)
   - Dynamic geometry (doors, destructible objects)
   - Instance support for repeated objects
4. Baking support (optional)
   - Pre-compute reverb for static areas
   - Editor integration

**Deliverables**:
- Level geometry visible to Steam Audio
- Material system functional
- Scene updates on level change

**Testing**:
- Verify geometry is correct (debug visualization)
- Test material properties
- Performance profiling (scene build time)

---

### Phase 3: Direct Sound & Occlusion (3-4 weeks)

**Goal**: Replace basic 3D audio with Steam Audio HRTF and occlusion

**Tasks**:
1. Integrate HRTF rendering
   - Add `IPLHRTFEffect` to `CSoundRender_Target` or new processor
   - Convert emitter position to Steam Audio source
   - Apply HRTF to audio buffers before OpenAL
2. Replace occlusion system
   - Currently: `CSoundRender_Scene::get_occlusion_to()` uses raycasts
   - New: Use `IPLDirectEffect` with occlusion simulation
   - Preserve occlusion scaling (`psSoundOcclusionScale`)
3. Transmission through geometry
   - Configure transmission in `IPLDirectEffectParams`
   - Adjust based on material properties
4. Distance attenuation
   - Use Steam Audio distance curves or keep X-Ray's custom curves
5. Add configuration options
   - Enable/disable HRTF
   - Occlusion quality settings
   - Direct simulation flags

**Deliverables**:
- HRTF-based 3D audio working
- Physics-based occlusion replacing raycast
- Configuration console variables

**Testing**:
- A/B comparison with old system
- Occlusion accuracy tests
- Performance impact measurement

---

### Phase 4: Reflections & Reverb (4-5 weeks)

**Goal**: Add realistic environmental audio

**Tasks**:
1. Reflection simulation
   - Add `IPLSimulator` to `CSoundRender_Scene`
   - Configure reflection settings (rays, bounces, duration)
   - Run simulation per-frame or per-interval
2. Replace EAX reverb
   - Add `IPLReflectionEffect`
   - Apply to emitters in reverberant spaces
   - Smooth transitions between environments
3. Environment system integration
   - Map existing X-Ray environments to Steam Audio settings
   - Preserve environment scripts (`sEnvironment.xr`)
   - Allow manual environment overrides
4. Performance optimization
   - Threaded simulation
   - Limit active simulations
   - Use baked data where possible
5. GPU acceleration (optional)
   - Detect GPU support (Radeon Rays, CUDA)
   - Enable GPU path tracing

**Deliverables**:
- Real-time reflections working
- Convolution reverb replacing EAX
- Environment system preserved

**Testing**:
- Indoor/outdoor transitions
- Complex geometry (tunnels, labs)
- Performance with multiple emitters

---

### Phase 5: Dynamic Acoustics & Pathing (3-4 weeks)

**Goal**: Support moving geometry and indirect sound paths

**Tasks**:
1. Dynamic geometry updates
   - Hook door opening/closing events
   - Update Steam Audio scene in real-time
   - Instanced mesh transforms
2. Sound pathing
   - Add `IPLPathEffect` for indirect paths
   - Configure path validation
   - Probe placement (manual or automatic)
3. Anomaly audio
   - Special handling for anomaly effects
   - Pulsing/warping audio via Steam Audio modulation
4. AI audio integration
   - Ensure AI hears propagated sounds
   - Update `CSound_params::max_ai_distance` with path info

**Deliverables**:
- Dynamic geometry affecting audio
- Indirect sound paths working
- AI audio awareness intact

**Testing**:
- Door occlusion tests
- Hallway propagation
- AI response to distant sounds

---

### Phase 6: Polish & Optimization (2-3 weeks)

**Goal**: Production-ready integration

**Tasks**:
1. Performance optimization
   - Profile hotspots
   - Tune simulation parameters
   - Implement LOD for distant sounds
   - Optimize geometry mesh
2. Configuration system
   - Console variables for all Steam Audio features
   - Presets (low/medium/high/ultra)
   - Save/load settings
3. Debugging tools
   - Visualize occlusion rays
   - Show reflection paths
   - Performance overlay
4. Compatibility
   - Test on various hardware (CPU, GPU)
   - Test all game levels
   - Modded content compatibility
5. Documentation
   - User guide (console commands)
   - Modder guide (material setup)
   - Developer docs (API usage)

**Deliverables**:
- Optimized performance
- Complete configuration system
- Debug tools
- Documentation

**Testing**:
- Full playthrough
- Performance targets met
- Compatibility testing

---

### Phase 7: Future Enhancements (Post-Release)

**Optional features for future updates**:

1. **Ambisonics support**
   - Spatial ambience sounds
   - 360-degree reverb

2. **Advanced pathing**
   - Automatic probe placement
   - AI-assisted path finding

3. **VR mode**
   - First-class HRTF for VR devices
   - Head tracking integration

4. **Advanced materials**
   - Frequency-dependent absorption
   - Per-weather material changes (wet surfaces)

5. **Audio occlusion painting**
   - Manual occlusion zones
   - Acoustic portals

6. **OpenAL removal** (if desired)
   - Custom mixer implementation
   - Direct platform audio output

---

## Technical Architecture

### Proposed Class Structure

```cpp
// New Steam Audio integration classes
namespace SteamAudio
{
    // Global Steam Audio context
    class CSteamAudioContext
    {
        IPLContext context;
        IPLHRTF hrtf;
        IPLAudioSettings audioSettings;

    public:
        bool Initialize();
        void Shutdown();

        IPLContext GetContext() const { return context; }
        IPLHRTF GetHRTF() const { return hrtf; }
    };

    // Scene geometry management
    class CSteamAudioScene
    {
        IPLScene scene;
        IPLStaticMesh staticMesh;
        xr_vector<IPLInstancedMesh> dynamicMeshes;

        // Material database
        xr_unordered_map<shared_str, IPLMaterial> materials;

    public:
        bool CreateFromCDB(CDB::MODEL* model);
        void UpdateDynamicGeometry(const Fmatrix& transform);

        IPLMaterial GetMaterial(pcstr name);
        void SetMaterial(pcstr name, const IPLMaterial& material);

        IPLScene GetScene() const { return scene; }
    };

    // Per-scene simulator
    class CSteamAudioSimulator
    {
        IPLSimulator simulator;
        xr_vector<IPLSource> sources;

        // Configuration
        int maxReflections = 3;
        int numRays = 4096;
        float duration = 2.0f;

    public:
        bool Initialize(IPLScene scene);
        void Shutdown();

        IPLSource CreateSource(const Fvector& position);
        void DestroySource(IPLSource source);

        void SetListener(const Fvector& position, const Fvector& forward, const Fvector& up);

        void RunSimulation();
        void GetDirectSoundPath(IPLSource source, IPLDirectSoundPath& outPath);
    };

    // Audio effect processor
    class CSteamAudioEffects
    {
        IPLDirectEffect directEffect;
        IPLReflectionEffect reflectionEffect;
        IPLPathEffect pathEffect;

        // Temp buffers for processing
        IPLAudioBuffer tempBuffer;

    public:
        bool Initialize();
        void Shutdown();

        // Process audio with Steam Audio effects
        void ApplyDirectSound(const IPLDirectSoundPath& path,
                             const IPLAudioBuffer& in,
                             IPLAudioBuffer& out);

        void ApplyReflections(const IPLReflectionEffectParams& params,
                             const IPLAudioBuffer& in,
                             IPLAudioBuffer& out);
    };

    // Material properties database
    class CSteamAudioMaterials
    {
        struct MaterialDef
        {
            IPLMaterial steamAudioMaterial;
            shared_str xrayMaterialName;
        };

        xr_unordered_map<shared_str, MaterialDef> materials;

    public:
        void LoadDefaults();
        void LoadFromConfig(IReader* reader);

        IPLMaterial GetMaterial(pcstr xrayMaterial);
        void SetMaterial(pcstr name, const IPLMaterial& material);
    };
}
```

### Modified xrSound Classes

```cpp
// CSoundRender_Core - add Steam Audio context
class CSoundRender_Core
{
    // ...existing members...

#if XRSOUND_USE_STEAMAUDIO
    SteamAudio::CSteamAudioContext* steamAudio = nullptr;
    SteamAudio::CSteamAudioMaterials* steamAudioMaterials = nullptr;
#endif

public:
    void _initialize() override
    {
        // ...existing OpenAL init...

#if XRSOUND_USE_STEAMAUDIO
        // Initialize Steam Audio
        steamAudio = xr_new<SteamAudio::CSteamAudioContext>();
        steamAudio->Initialize();

        steamAudioMaterials = xr_new<SteamAudio::CSteamAudioMaterials>();
        steamAudioMaterials->LoadDefaults();
#endif
    }
};

// CSoundRender_Scene - add Steam Audio scene
class CSoundRender_Scene
{
    // ...existing members...

#if XRSOUND_USE_STEAMAUDIO
    SteamAudio::CSteamAudioScene* steamAudioScene = nullptr;
    SteamAudio::CSteamAudioSimulator* steamAudioSimulator = nullptr;
#endif

public:
    void set_geometry_occ(CDB::MODEL* M, const Fbox& aabb) override
    {
        // ...existing code...

#if XRSOUND_USE_STEAMAUDIO
        // Create Steam Audio scene from geometry
        if (steamAudioScene)
            steamAudioScene->CreateFromCDB(M);

        if (steamAudioSimulator)
            steamAudioSimulator->Initialize(steamAudioScene->GetScene());
#endif
    }

    float get_occlusion_to(const Fvector& hear_pt, const Fvector& snd_pt, float dispersion) override
    {
#if XRSOUND_USE_STEAMAUDIO
        // Use Steam Audio occlusion
        if (psSoundFlags.test(ss_UseSteamAudio))
        {
            return steamAudioSimulator->GetOcclusion(hear_pt, snd_pt);
        }
#endif
        // Fallback to raycast occlusion
        // ...existing code...
    }
};

// CSoundRender_Emitter - add Steam Audio source
class CSoundRender_Emitter
{
    // ...existing members...

#if XRSOUND_USE_STEAMAUDIO
    IPLSource steamAudioSource = nullptr;
#endif

public:
    void start(const ref_sound& _owner, u32 flags, float delay) override
    {
        // ...existing code...

#if XRSOUND_USE_STEAMAUDIO
        if (scene->steamAudioSimulator && psSoundFlags.test(ss_UseSteamAudio))
        {
            steamAudioSource = scene->steamAudioSimulator->CreateSource(p_source.position);
        }
#endif
    }

    void render() override
    {
        // ...existing code...

#if XRSOUND_USE_STEAMAUDIO
        if (steamAudioSource && psSoundFlags.test(ss_UseSteamAudio))
        {
            // Apply Steam Audio effects before OpenAL playback
            // (details in next section)
        }
#endif
    }
};

// CSoundRender_Target - add Steam Audio processing
class CSoundRender_TargetA : public CSoundRender_Target
{
    // ...existing members...

#if XRSOUND_USE_STEAMAUDIO
    IPLAudioBuffer steamAudioBuffer;
    SteamAudio::CSteamAudioEffects* effects = nullptr;
#endif

public:
    void render() override
    {
#if XRSOUND_USE_STEAMAUDIO
        if (m_pEmitter && m_pEmitter->steamAudioSource && psSoundFlags.test(ss_UseSteamAudio))
        {
            // 1. Get audio data from emitter
            auto [data, size] = m_pEmitter->obtain_block();

            // 2. Convert to Steam Audio buffer format
            ConvertToSteamAudioBuffer(data, size, steamAudioBuffer);

            // 3. Get simulation results
            IPLDirectSoundPath directPath;
            m_pEmitter->scene->steamAudioSimulator->GetDirectSoundPath(
                m_pEmitter->steamAudioSource, directPath);

            // 4. Apply Steam Audio effects
            IPLAudioBuffer processedBuffer;
            effects->ApplyDirectSound(directPath, steamAudioBuffer, processedBuffer);

            // 5. Convert back and send to OpenAL
            ConvertFromSteamAudioBuffer(processedBuffer, data, size);

            // 6. Submit to OpenAL
            // ...existing OpenAL code...
        }
        else
#endif
        {
            // Original OpenAL-only path
            // ...existing code...
        }
    }
};
```

### Audio Buffer Conversion

Steam Audio uses **deinterleaved float32 buffers**, while OpenAL typically uses **interleaved PCM or float32**.

```cpp
void ConvertToSteamAudioBuffer(const u8* pcm_data, u32 size, IPLAudioBuffer& outBuffer)
{
    const CSoundRender_Source* source = m_pEmitter->source();
    const u16 channels = source->data_info().channels;
    const u32 samples = size / (channels * sizeof(float));

    // Allocate Steam Audio buffer
    outBuffer.numChannels = channels;
    outBuffer.numSamples = samples;

    // Deinterleave
    const float* interleaved = reinterpret_cast<const float*>(pcm_data);
    for (u32 ch = 0; ch < channels; ++ch)
    {
        float* channel = outBuffer.data[ch];
        for (u32 s = 0; s < samples; ++s)
        {
            channel[s] = interleaved[s * channels + ch];
        }
    }
}

void ConvertFromSteamAudioBuffer(const IPLAudioBuffer& steamBuffer, u8* pcm_data, u32 size)
{
    const u32 channels = steamBuffer.numChannels;
    const u32 samples = steamBuffer.numSamples;

    // Interleave
    float* interleaved = reinterpret_cast<float*>(pcm_data);
    for (u32 s = 0; s < samples; ++s)
    {
        for (u32 ch = 0; ch < channels; ++ch)
        {
            interleaved[s * channels + ch] = steamBuffer.data[ch][s];
        }
    }
}
```

### Configuration

```cpp
// New console variables (cvars)
XRSOUND_API extern Flags32 psSoundFlags;

enum : u32
{
    ss_Hardware         = 1ul << 1ul, // Existing
    ss_EFX              = 1ul << 2ul, // Existing
    ss_UseFloat32       = 1ul << 3ul, // Existing

    // NEW Steam Audio flags
    ss_UseSteamAudio    = 1ul << 4ul, // Enable Steam Audio processing
    ss_SteamAudio_HRTF  = 1ul << 5ul, // Enable HRTF
    ss_SteamAudio_Refl  = 1ul << 6ul, // Enable reflections
    ss_SteamAudio_Path  = 1ul << 7ul, // Enable pathing
};

// New configuration variables
XRSOUND_API extern int psSteamAudioQuality; // 0=Low, 1=Med, 2=High, 3=Ultra
XRSOUND_API extern int psSteamAudioRays;    // Number of rays (512-16384)
XRSOUND_API extern int psSteamAudioBounces; // Max bounces (1-8)
XRSOUND_API extern float psSteamAudioReverbTime; // Max reverb duration (0.5-5.0s)
```

---

## Compatibility & Migration

### Backwards Compatibility

**Goal**: Existing game content must work without changes

1. **Audio Files**: No changes needed
   - Keep OGG Vorbis support
   - PCM and Float32 formats preserved

2. **Sound Scripts**: Fully compatible
   - All existing sound definitions work
   - Volume, range, game type preserved

3. **Environment Scripts**: Map to Steam Audio
   - `sEnvironment.xr` still loaded
   - EAX parameters mapped to Steam Audio reverb
   - Custom environments supported

4. **API**: No breaking changes
   - `ISoundManager`, `ISoundScene`, `CSound_emitter` unchanged
   - Game code requires no modifications

### Migration Path

**Phase 1**: Opt-in Steam Audio
- Default: OFF (use OpenAL only)
- Console: `snd_flags +4` to enable Steam Audio
- Testing: Players can compare old vs new

**Phase 2**: Enable by default
- Default: ON (Steam Audio active)
- Console: `snd_flags -4` to disable
- Fallback: Auto-disable if performance issues

**Phase 3**: OpenAL removal (future)
- Only if Steam Audio proves superior
- Remove OpenAL dependencies
- Requires custom audio output layer

### Data Migration

**Geometry**: Automatic
- X-Ray levels automatically export to Steam Audio
- No content changes needed

**Materials**: New config file
- Create `sounds/steam_audio_materials.ltx`
- Map X-Ray materials to Steam Audio properties
- Defaults provided, moddable

**Example: `steam_audio_materials.ltx`**
```ini
[materials]
; Format: xray_material = low_freq_absorption, mid_freq_absorption, high_freq_absorption, scattering, transmission

default         = 0.10, 0.20, 0.30, 0.05, 0.10
materials\earth = 0.15, 0.25, 0.40, 0.10, 0.05
materials\metal = 0.01, 0.02, 0.02, 0.10, 0.20
materials\wood  = 0.15, 0.25, 0.30, 0.10, 0.10
materials\concrete = 0.05, 0.07, 0.08, 0.05, 0.02
materials\glass = 0.03, 0.02, 0.02, 0.05, 0.90

; Anomaly zones - special handling
materials\anomaly = 0.90, 0.90, 0.90, 0.50, 0.01
```

---

## Performance Considerations

### Performance Targets

| Scenario | Target | Notes |
|----------|--------|-------|
| **Direct sound (HRTF + occlusion)** | < 0.5ms per emitter | Minimal overhead |
| **Reflections (real-time)** | 1-3ms per scene | Depends on geometry complexity |
| **Reflections (baked)** | < 0.5ms per scene | Pre-computed data |
| **Path tracing** | 2-5ms per source | Limited to nearby sources only |
| **Total audio overhead** | < 10% of frame time (< 1.6ms @ 60fps) | |

### Optimization Strategies

1. **LOD System**
   - **Close sounds** (< 20m): Full Steam Audio (HRTF, reflections, paths)
   - **Medium sounds** (20-50m): Direct + occlusion only
   - **Far sounds** (> 50m): Simple attenuation, no Steam Audio

2. **Simulation Throttling**
   - Run reflection simulation at lower rate (e.g., 15 Hz instead of 60 Hz)
   - Interpolate results between frames
   - Update distance-sorted priority queue

3. **Emitter Limiting**
   - Existing: `psSoundTargets` limits active sounds
   - New: Further limit Steam Audio processing to N most important sounds

4. **Geometry Simplification**
   - Use simplified collision mesh for Steam Audio (not visual mesh)
   - Merge small triangles
   - Remove interior details

5. **Baking**
   - Pre-compute reverb for static areas (indoors)
   - Runtime reflections only for dynamic/outdoor areas

6. **GPU Acceleration**
   - Enable Radeon Rays on AMD GPUs
   - Enable CUDA on NVIDIA GPUs
   - Fallback to CPU if unavailable

7. **Multi-threading**
   - Run Steam Audio simulation on dedicated thread
   - Async update, apply on main thread
   - Use Steam Audio's built-in threading

### Memory Budget

| Component | Estimated Memory |
|-----------|------------------|
| **Steam Audio Context** | ~10 MB (HRTF data, buffers) |
| **Scene Geometry** | 50-200 MB (depends on level size) |
| **Baked Reverb** | 10-50 MB per level (optional) |
| **Per-Emitter Buffers** | ~100 KB × active emitters |
| **Total Overhead** | ~100-300 MB |

### Profiling Points

```cpp
// Add profiling markers
CTimer profTimer;

profTimer.Start();
steamAudioSimulator->RunSimulation();
float simTime = profTimer.GetElapsed_sec() * 1000.0f; // ms

profTimer.Start();
effects->ApplyDirectSound(...);
float effectTime = profTimer.GetElapsed_sec() * 1000.0f;

// Log to console or performance overlay
Msg("Steam Audio: Sim=%.2fms, Effect=%.2fms", simTime, effectTime);
```

---

## Testing Strategy

### Unit Tests

1. **API Initialization**
   - Context creation/destruction
   - HRTF loading
   - Device enumeration

2. **Geometry Conversion**
   - CDB::MODEL → IPLStaticMesh
   - Vertex/index correctness
   - Material assignment

3. **Audio Processing**
   - Buffer conversion (interleaved ↔ deinterleaved)
   - Effect application
   - No audio corruption

### Integration Tests

1. **Basic Playback**
   - All sound types (2D, 3D, looped)
   - Volume/frequency control
   - Distance attenuation

2. **Occlusion**
   - Direct line of sight: No occlusion
   - Behind wall: Full occlusion
   - Partial cover: Partial occlusion

3. **Reflections**
   - Indoor: Reverb present
   - Outdoor: Minimal reverb
   - Transition: Smooth change

4. **Dynamic Geometry**
   - Door opening: Occlusion changes
   - Moving objects: Audio follows

### Regression Tests

1. **Existing Content**
   - Test all vanilla levels
   - Verify no crashes
   - Compare audio quality (A/B test)

2. **Modded Content**
   - Test popular mods
   - Ensure compatibility

### Performance Tests

1. **Stress Test**
   - 50+ simultaneous emitters
   - Complex geometry (Lab X-18)
   - Monitor frame time

2. **Memory Test**
   - Load all levels sequentially
   - Check for memory leaks
   - Monitor peak usage

3. **Platform Test**
   - Various CPUs (Intel, AMD)
   - Various GPUs (NVIDIA, AMD, Intel)
   - Low-end hardware validation

### Quality Tests

1. **Listening Tests**
   - Compare Steam Audio vs OpenAL
   - Identify audio artifacts
   - Subjective quality assessment

2. **AI Tests**
   - NPCs respond to sounds correctly
   - Mutants detect player sounds
   - Sound propagation affects AI

---

## Timeline & Resources

### Development Timeline

| Phase | Duration | Dependencies |
|-------|----------|--------------|
| **Phase 1: Foundation** | 2-3 weeks | - |
| **Phase 2: Geometry Integration** | 3-4 weeks | Phase 1 |
| **Phase 3: Direct Sound & Occlusion** | 3-4 weeks | Phase 2 |
| **Phase 4: Reflections & Reverb** | 4-5 weeks | Phase 3 |
| **Phase 5: Dynamic Acoustics** | 3-4 weeks | Phase 4 |
| **Phase 6: Polish & Optimization** | 2-3 weeks | Phase 5 |
| **Total** | **17-23 weeks (4-6 months)** | |

### Resource Requirements

**Developer Skills Needed**:
- C++ programming
- Audio DSP knowledge
- CMake build systems
- X-Ray engine familiarity
- Steam Audio API experience (nice to have)

**Hardware for Testing**:
- Low-end PC (Intel i3, integrated graphics)
- Mid-range PC (Ryzen 5, GTX 1060)
- High-end PC (Ryzen 9, RTX 4080)
- Various audio devices (headphones, speakers, surround)

**External Dependencies**:
- Steam Audio SDK (Apache 2.0, free)
- No additional licenses needed

### Milestones

**M1: Foundation Complete** (Week 3)
- Steam Audio builds successfully
- Basic context initialization working

**M2: Geometry Working** (Week 7)
- Levels load into Steam Audio
- Material system functional

**M3: Direct Sound & HRTF** (Week 11)
- HRTF audio playback
- Occlusion replacing raycast

**M4: Reflections Enabled** (Week 16)
- Real-time reflections working
- Reverb replacing EAX

**M5: Dynamic Features** (Week 20)
- Dynamic geometry updates
- Sound pathing functional

**M6: Release Ready** (Week 23)
- Performance targets met
- Full compatibility validated
- Documentation complete

---

## Risks & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| **Performance below target** | Medium | High | Extensive profiling, LOD system, GPU acceleration |
| **Compatibility issues with mods** | Low | Medium | Fallback to OpenAL, community testing |
| **Audio quality regressions** | Low | High | A/B testing, quality assurance, adjustable parameters |
| **Steam Audio API changes** | Low | Medium | Pin to stable release, monitor updates |
| **Complex integration bugs** | Medium | Medium | Incremental development, thorough testing |
| **OpenAL removal too difficult** | High | Low | Keep hybrid approach, no need to remove OpenAL |

---

## Conclusion

Integrating Steam Audio into OpenXRay's xrSound library will provide significant audio quality improvements, particularly for indoor environments, dynamic scenes, and VR use cases. The hybrid approach (Option 1) offers the best balance of risk, effort, and reward, allowing gradual feature enablement while preserving compatibility.

**Key Benefits**:
- Physics-based occlusion and transmission
- Realistic reverb and reflections
- Multi-path sound propagation
- HRTF for accurate 3D audio
- Dynamic acoustic responses
- Free, open-source, commercial-friendly license

**Recommended Next Steps**:
1. Review and approve this plan
2. Set up development environment with Steam Audio SDK
3. Begin Phase 1 (Foundation) implementation
4. Establish testing pipeline
5. Iterate based on feedback

**Questions for Discussion**:
- Target release version (1.0? 1.1?)
- Performance vs. quality trade-offs
- OpenAL: Keep long-term or eventual removal?
- Baking workflow for level designers
- VR support priority

---

## References

- **Steam Audio Website**: https://valvesoftware.github.io/steam-audio/
- **Steam Audio GitHub**: https://github.com/ValveSoftware/steam-audio
- **Steam Audio C API Docs**: https://valvesoftware.github.io/steam-audio/doc/capi/
- **Steam Audio Programmer's Guide**: https://valvesoftware.github.io/steam-audio/doc/capi/guide.html
- **Apache 2.0 License**: https://www.apache.org/licenses/LICENSE-2.0
- **OpenXRay GitHub**: https://github.com/OpenXRay/xray-16
- **OpenAL Soft**: https://github.com/kcat/openal-soft

---

## Appendix A: Console Commands (Proposed)

```
// Enable/disable Steam Audio
snd_steam_audio <0|1>

// HRTF control
snd_steam_audio_hrtf <0|1>

// Reflection quality
snd_steam_audio_reflections <0|1>
snd_steam_audio_rays <512-16384>        // Number of reflection rays
snd_steam_audio_bounces <1-8>           // Max reflection bounces
snd_steam_audio_reverb_time <0.5-5.0>   // Max reverb duration (seconds)

// Pathing
snd_steam_audio_pathing <0|1>

// Performance
snd_steam_audio_sim_rate <15|30|60>     // Simulation update rate (Hz)
snd_steam_audio_gpu <0|1>               // Enable GPU acceleration
snd_steam_audio_threads <1-16>          // Number of simulation threads

// Quality presets
snd_steam_audio_preset <low|medium|high|ultra>

// Debug
snd_steam_audio_debug_rays <0|1>        // Visualize reflection rays
snd_steam_audio_debug_occlusion <0|1>   // Show occlusion info
snd_steam_audio_stats <0|1>             // Show performance stats
```

---

## Appendix B: Material Property Reference

Steam Audio materials have these properties:

| Property | Range | Description |
|----------|-------|-------------|
| **Low Frequency Absorption** | 0.0-1.0 | Absorption at 250-500 Hz |
| **Mid Frequency Absorption** | 0.0-1.0 | Absorption at 1-2 kHz |
| **High Frequency Absorption** | 0.0-1.0 | Absorption at 4-8 kHz |
| **Scattering** | 0.0-1.0 | Diffuse reflection amount |
| **Transmission** | 0.0-1.0 | Sound transmission through material |

**Common Material Examples**:

```cpp
// Brick wall
{ 0.03f, 0.04f, 0.07f, 0.05f, 0.02f }

// Concrete
{ 0.05f, 0.07f, 0.08f, 0.05f, 0.015f }

// Wood
{ 0.15f, 0.25f, 0.30f, 0.10f, 0.10f }

// Metal
{ 0.01f, 0.02f, 0.02f, 0.10f, 0.20f }

// Glass
{ 0.03f, 0.02f, 0.02f, 0.05f, 0.90f }

// Fabric/Curtains
{ 0.25f, 0.60f, 0.80f, 0.20f, 0.01f }

// Earth/Dirt
{ 0.15f, 0.25f, 0.40f, 0.10f, 0.05f }
```

---

**Document Version**: 1.0
**Last Updated**: 2025-10-13
**Author**: OpenXRay Development Team
**Status**: Proposal / Planning Phase
