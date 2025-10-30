# 🚀 **Clean Architecture: Modernization Plan**

## 📋 **Executive Summary**

This plan takes a **clean slate approach** for the resource manager and scene graph, designed from the ground up to work seamlessly with FrameGraph and modern rendering patterns.

**Timeline**: **28 weeks** (~7 months)  
**Approach**: Clean rewrite of resource manager + scene graph, keep existing mesh loader

---

## 🎯 **What We're Building**

```
┌─────────────────────────────────────────────────────┐
│           Modern Renderer Architecture              │
├─────────────────────────────────────────────────────┤
│                                                     │
│  FrameGraph (High-level)                           │
│  ↓ Declares resource dependencies                  │
│                                                     │
│  RenderContext (Mid-level)                         │
│  ↓ Records commands                                │
│                                                     │
│  ResourceManager (NEW - Clean Design)              │
│  • Texture streaming                               │
│  • Memory budget management                        │
│  • Async loading                                   │
│  • Reference counting                              │
│  ↓                                                  │
│                                                     │
│  SceneGraph (NEW - Clean Design)                   │
│  • BVH acceleration                                │
│  • Frustum culling                                 │
│  • LOD selection                                   │
│  • Visibility queries                              │
│  ↓                                                  │
│                                                     │
│  MeshLoader (Keep Existing - Adapter)              │
│  • Load .ogf files (existing loader)               │
│  • Thin adapter to modern interface                │
│  • Future: can be replaced incrementally           │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### **Design Principles**

1. **FrameGraph-First**: Resource manager designed for FrameGraph's virtual resources
2. **Data-Oriented**: Cache-friendly layouts, minimal indirection
3. **Streaming-First**: Texture streaming built-in from day one
4. **Memory-Aware**: Explicit memory budgets and eviction policies
5. **Async-Ready**: All I/O operations async by default

---

## 📐 **Understanding Current System**

### **Current X-Ray Shader System**

```
┌─────────────────────────────────────────────────────┐
│          ACTUAL X-Ray Shader System                 │
├─────────────────────────────────────────────────────┤
│                                                     │
│  1. Lua Scripts (.s files)                         │
│     • Location: gamedata/shaders/r3/*.s            │
│     • Purpose: Define shader techniques/passes     │
│     • Example: accum_volumetric.s, effects_water.s │
│     • Call Lua functions: normal(), l_point(), etc.│
│                                                     │
│  2. Luabind Bridge (adopt_* classes)               │
│     • adopt_compiler → CBlender_Compile            │
│     • adopt_sampler → texture/sampler setup        │
│     • Defined in: dx11ResourceManager_Scripting.cpp│
│                                                     │
│  3. C++ CBlender Classes (Optional)                │
│     • Location: Layers/xrRender/blenders/*.cpp    │
│     • Purpose: Programmatic shader generation      │
│     • Example: CBlender_default, CBlender_deffer_* │
│     • Inherit from IBlender base class             │
│                                                     │
│  4. HLSL Shaders (.ps, .vs, .gs, .hs, .ds, .cs)  │
│     • Location: gamedata/shaders/r3/*.ps/vs/etc   │
│     • Modern HLSL (Shader Model 5.0+)              │
│     • Include system (#include "common.h", etc.)   │
│     • Compiled at runtime with variant support     │
│                                                     │
└─────────────────────────────────────────────────────┘
```

**Key Insight**: HLSL shaders are already modern - no rewrite needed!

---

## 🏗️ **Modern Architecture Design**

### **Resource Manager**

```cpp
namespace xray::render::ng {

// ═══════════════════════════════════════════════════
//  RESOURCE HANDLES (Type-safe, generation counted)
// ═══════════════════════════════════════════════════

struct TextureHandle {
    u32 index : 24;        // Index into texture array
    u32 generation : 8;    // Generation counter (detect stale handles)
};

struct BufferHandle {
    u32 index : 24;
    u32 generation : 8;
};

struct MeshHandle {
    u32 index : 24;
    u32 generation : 8;
};

// ═══════════════════════════════════════════════════
//  TEXTURE SYSTEM (with streaming)
// ═══════════════════════════════════════════════════

enum class TextureState : u8 {
    Unloaded,       // Not in memory
    Loading,        // Async load in progress
    Resident,       // Fully loaded in VRAM
    Evicted,        // Marked for eviction
};

enum class TexturePriority : u8 {
    Critical = 0,   // Never evict (UI, HUD)
    High = 1,       // Visible this frame
    Medium = 2,     // Visible recently
    Low = 3,        // Not visible, keep if memory available
    VeryLow = 4,    // Evict first
};

struct TextureDesc {
    u32 width;
    u32 height;
    u32 mipLevels;
    nvrhi::Format format;
    bool isRenderTarget;
    bool generateMips;
    const char* debugName;
};

struct TextureMetadata {
    string256 path;                  // Disk path
    TextureState state;              // Current state
    TexturePriority priority;        // Streaming priority
    u32 memorySize;                  // GPU memory size (bytes)
    float lastAccessTime;            // For LRU eviction
    u32 refCount;                    // Reference count
    
    // Mip streaming
    u32 residentMips;                // Mips currently loaded
    u32 requestedMips;               // Mips needed for rendering
    
    // NVRHI handle
    nvrhi::TextureHandle nvrhiTexture;
};

class TextureManager {
public:
    // ─── Loading ───
    TextureHandle LoadTexture(const char* path, 
                             TexturePriority priority = TexturePriority::Medium);
    TextureHandle CreateTexture(const TextureDesc& desc, 
                               const void* initialData = nullptr);
    
    // ─── Access ───
    nvrhi::TextureHandle GetNVRHIHandle(TextureHandle handle);
    const TextureMetadata& GetMetadata(TextureHandle handle);
    
    // ─── Streaming ───
    void SetMemoryBudget(u64 bytes);
    void SetPriority(TextureHandle handle, TexturePriority priority);
    void RequestMips(TextureHandle handle, u32 mipCount);
    
    // ─── Lifecycle ───
    void AddRef(TextureHandle handle);
    void Release(TextureHandle handle);  // Decrements refcount
    
    // ─── Update (per frame) ───
    void Update(float deltaTime);        // Stream textures, evict unused
    
    // ─── Stats ───
    struct Stats {
        u64 totalMemoryUsed;
        u64 memoryBudget;
        u32 texturesResident;
        u32 texturesLoading;
        u32 texturesEvicted;
    };
    Stats GetStats() const;
    
private:
    // Sparse array of textures (generations prevent stale handles)
    xr_vector<TextureMetadata> m_textures;
    xr_vector<u8> m_generations;
    xr_vector<u32> m_freeIndices;
    
    // Streaming system
    u64 m_memoryBudget = 2ULL * 1024 * 1024 * 1024;  // 2GB default
    
    // Async loading
    struct LoadRequest {
        TextureHandle handle;
        string256 path;
        TexturePriority priority;
    };
    ThreadSafeQueue<LoadRequest> m_loadQueue;
    xr_vector<std::thread> m_workerThreads;
    
    // Eviction policy (LRU)
    void EvictTextures(u64 bytesNeeded);
    void StreamTextures();  // Stream in requested mips
};

// ═══════════════════════════════════════════════════
//  BUFFER SYSTEM
// ═══════════════════════════════════════════════════

struct BufferDesc {
    u64 size;
    nvrhi::BufferUsageFlags usage;
    bool cpuAccess;
    const char* debugName;
};

struct BufferMetadata {
    BufferDesc desc;
    nvrhi::BufferHandle nvrhiBuffer;
    u32 refCount;
};

class BufferManager {
public:
    BufferHandle CreateBuffer(const BufferDesc& desc, 
                             const void* initialData = nullptr);
    
    // Dynamic buffers (for per-frame data)
    BufferHandle CreateDynamicBuffer(u64 size, const char* debugName);
    void UpdateDynamicBuffer(BufferHandle handle, const void* data, 
                           u64 size, u64 offset = 0);
    
    nvrhi::BufferHandle GetNVRHIHandle(BufferHandle handle);
    
    void AddRef(BufferHandle handle);
    void Release(BufferHandle handle);
    
private:
    xr_vector<BufferMetadata> m_buffers;
    xr_vector<u8> m_generations;
    xr_vector<u32> m_freeIndices;
};

// ═══════════════════════════════════════════════════
//  MESH SYSTEM (ADAPTER TO LEGACY)
// ═══════════════════════════════════════════════════

struct MeshLOD {
    BufferHandle vertexBuffer;
    BufferHandle indexBuffer;
    u32 indexCount;
    float distance;           // LOD switch distance
};

struct MeshMetadata {
    string256 path;
    xr_vector<MeshLOD> lods;
    u32 refCount;
    
    // Bounding volumes
    Fbox boundingBox;
    Fsphere boundingSphere;
};

class MeshLoader {
public:
    // Load using EXISTING .ogf loader
    MeshHandle LoadMesh(const char* path);
    
    // Access
    const MeshMetadata& GetMetadata(MeshHandle handle);
    const MeshLOD* SelectLOD(MeshHandle handle, float distance);
    
    // Lifecycle
    void AddRef(MeshHandle handle);
    void Release(MeshHandle handle);
    
private:
    xr_vector<MeshMetadata> m_meshes;
    xr_vector<u8> m_generations;
    xr_vector<u32> m_freeIndices;
    
    // Bridge to legacy loader
    CKinematics* LoadOGF_Legacy(const char* path);
    void ConvertToModern(CKinematics* legacy, MeshMetadata& modern);
};

// ═══════════════════════════════════════════════════
//  UNIFIED RESOURCE MANAGER
// ═══════════════════════════════════════════════════

class ResourceManager {
public:
    ResourceManager(nvrhi::IDevice* device);
    ~ResourceManager();
    
    // Subsystems (exposed as public members for convenience)
    TextureManager textures;
    BufferManager buffers;
    MeshLoader meshes;
    
    // Global operations
    void Update(float deltaTime);
    void SetGlobalMemoryBudget(u64 bytes);
    
    // Stats
    struct Stats {
        TextureManager::Stats textures;
        u64 buffersMemoryUsed;
        u64 totalMemoryUsed;
    };
    Stats GetStats() const;
    
private:
    nvrhi::IDevice* m_device;
};

} // namespace xray::render::ng
```

---

### **Scene Graph**

```cpp
namespace xray::render::ng {

// ═══════════════════════════════════════════════════
//  SCENE NODE
// ═══════════════════════════════════════════════════

struct SceneNode {
    // Transform
    Fmatrix worldMatrix;
    Fbox worldBounds;
    
    // Rendering
    MeshHandle mesh;
    xr_vector<TextureHandle> textures;  // Albedo, normal, etc.
    
    // Visibility
    bool visible = true;
    bool castsShadows = true;
    float lodBias = 1.0f;
    
    // User data
    void* userData = nullptr;  // Game object pointer
    
    // Hierarchy (optional - keep flat for performance)
    SceneNode* parent = nullptr;
    xr_vector<SceneNode*> children;
};

// ═══════════════════════════════════════════════════
//  BVH ACCELERATION STRUCTURE
// ═══════════════════════════════════════════════════

struct BVHNode {
    Fbox bounds;
    
    // Internal node
    u32 leftChild;   // Index to left child
    u32 rightChild;  // Index to right child
    
    // Leaf node
    u32 objectStart; // Index into object array
    u32 objectCount; // Number of objects in leaf
    
    bool IsLeaf() const { return objectCount > 0; }
};

class BVH {
public:
    void Build(const xr_vector<SceneNode*>& objects);
    void Rebuild();  // Incremental rebuild
    
    // Queries
    void FrustumCull(const Fmatrix& viewProj, xr_vector<SceneNode*>& outVisible);
    void RayCast(const Fvector& origin, const Fvector& direction, 
                xr_vector<SceneNode*>& outHits);
    void SphereQuery(const Fvector& center, float radius, 
                    xr_vector<SceneNode*>& outResults);
    
private:
    xr_vector<BVHNode> m_nodes;
    xr_vector<SceneNode*> m_objects;
    
    // Build helpers
    u32 BuildRecursive(u32 start, u32 end, u32 depth);
    void SplitObjects(u32 start, u32 end, u32& mid, int axis);
};

// ═══════════════════════════════════════════════════
//  SCENE GRAPH
// ═══════════════════════════════════════════════════

class SceneGraph {
public:
    SceneGraph(ResourceManager* resourceManager);
    ~SceneGraph();
    
    // ─── Node Management ───
    SceneNode* CreateNode();
    void DestroyNode(SceneNode* node);
    void UpdateNode(SceneNode* node);  // Update bounds, mark dirty
    
    // ─── Queries ───
    struct VisibilityQuery {
        Fmatrix viewProjection;
        Fvector cameraPosition;
        float lodBias = 1.0f;
        
        // Outputs
        xr_vector<SceneNode*> visibleNodes;
        xr_vector<SceneNode*> shadowCasters;
    };
    
    void QueryVisibility(VisibilityQuery& query);
    
    // ─── LOD Selection ───
    const MeshLOD* SelectLOD(SceneNode* node, float distance);
    
    // ─── Update ───
    void Update(float deltaTime);     // Rebuild BVH if dirty
    
    // ─── Stats ───
    struct Stats {
        u32 totalNodes;
        u32 visibleNodes;
        u32 culledNodes;
        float bvhBuildTime;
    };
    Stats GetStats() const;
    
private:
    ResourceManager* m_resourceManager;
    
    // All scene nodes (flat array, cache-friendly)
    xr_vector<SceneNode*> m_nodes;
    
    // Spatial acceleration
    BVH m_bvh;
    bool m_bvhDirty = false;
    
    // Visibility helpers
    void FrustumCullBVH(const Fmatrix& viewProj, xr_vector<SceneNode*>& outVisible);
    bool IsNodeVisible(SceneNode* node, const Fmatrix& viewProj);
};

} // namespace xray::render::ng
```

---

## 📅 **Implementation Timeline: 28 Weeks**

### **Phase 0: Foundation (Weeks 1-3)**

#### **Week 1-3: NVRHI Integration**

Wrap existing D3D11 device with NVRHI:

```cpp
class NVRHIDevice {
    bool Initialize(ID3D11Device* existingDevice, 
                   ID3D11DeviceContext* existingContext);
    nvrhi::IDevice* GetDevice();
    nvrhi::ICommandList* GetCommandList();
};
```

**Deliverable**: Blue screen via NVRHI ✅

---

### **Phase 1: Core Systems (Weeks 4-12)**

#### **Week 4-7: RenderContext**

Clean command recording API:

```cpp
class RenderContext {
    void BeginRenderPass(const RenderPassDesc& desc);
    void SetPipeline(PipelineStateHandle pso);
    void SetVertexBuffer(BufferHandle vb, u32 stride);
    void SetIndexBuffer(BufferHandle ib);
    void SetTexture(u32 slot, TextureHandle tex);
    void DrawIndexed(u32 indexCount, u32 startIndex, u32 baseVertex);
    void EndRenderPass();
};
```

**Deliverable**: Draw triangle via RenderContext ✅

---

#### **Week 8-10: FrameGraph Core**

Dependency graph for passes:

```cpp
class FrameGraph {
    VirtualResourceHandle CreateTexture(const TextureDesc& desc);
    PassHandle AddPass(const char* name);
    
    void PassRead(PassHandle pass, VirtualResourceHandle resource);
    void PassWrite(PassHandle pass, VirtualResourceHandle resource);
    
    void Compile();  // Build execution order, insert barriers
    void Execute(RenderContext& ctx);  // Run passes
};
```

**Deliverable**: Clear pass via FrameGraph ✅

---

#### **Week 11-12: Resource Manager Foundation** 🔑

**Build modern resource manager from scratch**:

**Week 11**: Handle system + Texture manager basics
```cpp
class TextureManager {
    TextureHandle LoadTexture(const char* path);
    nvrhi::TextureHandle GetNVRHIHandle(TextureHandle handle);
    void AddRef(TextureHandle handle);
    void Release(TextureHandle handle);
};
```

**Week 12**: Buffer manager + mesh adapter
```cpp
class BufferManager {
    BufferHandle CreateBuffer(const BufferDesc& desc, const void* data);
    nvrhi::BufferHandle GetNVRHIHandle(BufferHandle handle);
};

class MeshLoader {
    MeshHandle LoadMesh(const char* path);  // Uses legacy .ogf loader
    const MeshMetadata& GetMetadata(MeshHandle handle);
};
```

**Deliverable**: Can load textures and meshes, render simple objects ✅

---

### **Phase 2: Shader System (Weeks 13-16)**

#### **Week 13: Lua→JSON Migration**

Automated conversion of `.s` Lua scripts to `.json`:

**Migration Script** (Python):
```python
#!/usr/bin/env python3
class ShaderMigrator:
    def migrate_file(self, lua_path, output_dir):
        # Parse Lua script
        # Extract: normal(), l_point(), l_spot(), l_special()
        # Generate JSON with passes, states, textures, samplers
        pass
```

**Example Conversion**:

Input (`accum_volumetric.s`):
```lua
function normal(shader, t_base, t_second, t_detail)
    shader:begin("accum_volumetric", "accum_volumetric")
          :fog(false)
          :zb(true, false)
    shader:dx10texture("s_lmap", t_base)
end
```

Output (`accum_volumetric.json`):
```json
{
  "technique": "accum_volumetric",
  "passes": [{
    "name": "normal",
    "vs": "accum_volumetric.vs",
    "ps": "accum_volumetric.ps",
    "state": {"fog": false, "ztest": true, "zwrite": false},
    "textures": [{"slot": "s_lmap", "param": "$base"}]
  }]
}
```

**Deliverable**: All `.s` files converted to `.json` ✅

---

#### **Week 14: Modern ShaderCompiler**

```cpp
class ShaderCompiler {
    struct CompileOptions {
        const char* entryPoint;
        const char* target;  // "vs_5_0", "ps_5_0"
        std::vector<std::pair<const char*, const char*>> defines;
    };
    
    CompileResult CompileFromFile(const char* hlslPath, 
                                  const CompileOptions& options);
};
```

**Deliverable**: New shader compiler works ✅

---

#### **Week 15: Shader Variant System**

```cpp
class ShaderVariantGenerator {
    struct VariantKey {
        u32 skinning_bones;  // 0, 1, 2, 3, 4
        bool msaa_enabled;
        u64 Hash() const;
    };
    
    std::vector<VariantKey> GenerateVariants(const ShaderDesc& desc);
};
```

**Deliverable**: Shader permutations work ✅

---

#### **Week 16: Shader Hot-Reload**

```cpp
class ShaderHotReloader {
    void WatchDirectory(const char* directory);
    void OnFileChanged(const char* filePath);
};
```

**Deliverable**: Edit shader → instant reload! ✅

---

### **Phase 3: Resource Streaming (Weeks 17-19)** 🔑

#### **Week 17: Texture Streaming**

```cpp
class TextureManager {
    void SetMemoryBudget(u64 bytes);
    void RequestMips(TextureHandle handle, u32 mipCount);
    
    void Update(float deltaTime) {
        StreamTextures();   // Load requested mips (async)
        EvictTextures(0);   // Evict if over budget (LRU)
    }
};
```

**Features**:
- Async I/O with thread pool
- Mip streaming (load lower mips first)
- LRU eviction policy
- Memory budget enforcement

**Test**: Load 1000 textures, stay under 512MB ✅

---

#### **Week 18: Memory Management**

```cpp
class ResourceManager {
    void SetGlobalMemoryBudget(u64 bytes) {
        // Split: 70% textures, 20% buffers, 10% scratch
        textures.SetMemoryBudget(bytes * 0.7);
    }
    
    Stats GetStats();  // Detailed memory statistics
};
```

**Deliverable**: Memory budget system working ✅

---

#### **Week 19: Reference Counting**

```cpp
class TextureManager {
    void AddRef(TextureHandle handle);
    void Release(TextureHandle handle);  // Auto-evict when refcount == 0
};

// RAII wrapper
class TextureRef {
    TextureRef(TextureHandle h, TextureManager* mgr);
    ~TextureRef();  // Auto-release
};
```

**Deliverable**: Automatic resource lifecycle ✅

---

### **Phase 4: Scene Graph (Weeks 20-22)** 🔑

#### **Week 20: BVH Construction**

```cpp
class BVH {
    void Build(const xr_vector<SceneNode*>& objects);
    
private:
    u32 BuildRecursive(u32 start, u32 end, u32 depth);
    void SplitObjects(u32 start, u32 end, u32& mid, int axis);
};
```

**Algorithm**:
1. Compute bounds for objects
2. Split along longest axis at median
3. Recurse until leaf (≤4 objects or depth > 32)

**Test**: Build BVH for 10,000 objects in <10ms ✅

---

#### **Week 21: Frustum Culling**

```cpp
class BVH {
    void FrustumCull(const Fmatrix& viewProj, 
                    xr_vector<SceneNode*>& outVisible);
    
private:
    void FrustumCullNode(u32 nodeIdx, const Fplane planes[6],
                        xr_vector<SceneNode*>& outVisible);
    void ExtractFrustumPlanes(const Fmatrix& viewProj, Fplane planes[6]);
};
```

**Test**: Cull 10,000 objects to ~500 visible in <1ms ✅

---

#### **Week 22: LOD Selection**

```cpp
class SceneGraph {
    void QueryVisibility(VisibilityQuery& query) {
        // 1. Frustum cull via BVH
        m_bvh.FrustumCull(query.viewProjection, query.visibleNodes);
        
        // 2. LOD selection
        for (SceneNode* node : query.visibleNodes) {
            float distance = query.cameraPosition.distance_to(
                node->worldBounds.getcenter()
            );
            const MeshLOD* lod = SelectLOD(node, distance);
            
            // 3. Update texture priorities + request mips
            for (TextureHandle tex : node->textures) {
                m_resourceManager->textures.SetPriority(tex, TexturePriority::High);
                
                float screenSize = CalculateScreenSize(node, query.viewProjection);
                u32 mipsNeeded = CalculateMipsNeeded(screenSize);
                m_resourceManager->textures.RequestMips(tex, mipsNeeded);
            }
        }
    }
};
```

**Deliverable**: LOD + streaming integrated ✅

---

### **Phase 5: Rendering Pipeline (Weeks 23-25)**

#### **Week 23: G-Buffer Pass**

```cpp
PassHandle gbufferPass = fg.AddPass("GBuffer");

// Create G-Buffer resources
auto albedo = fg.CreateTexture("GBuffer.Albedo", {...});
auto normal = fg.CreateTexture("GBuffer.Normal", {...});
auto depth = fg.CreateTexture("GBuffer.Depth", {...});

fg.PassWrite(gbufferPass, albedo);
fg.PassWrite(gbufferPass, normal);
fg.PassWrite(gbufferPass, depth);

fg.SetPassCallback(gbufferPass, [&](RenderContext& ctx, const FrameGraph& fg) {
    // Query visible objects
    SceneGraph::VisibilityQuery query;
    query.viewProjection = camera.GetViewProj();
    query.cameraPosition = camera.GetPosition();
    sceneGraph.QueryVisibility(query);
    
    // Render
    for (SceneNode* node : query.visibleNodes) {
        const MeshLOD* lod = sceneGraph.SelectLOD(node, distance);
        
        ctx.SetVertexBuffer(resourceManager.buffers.GetNVRHIHandle(lod->vertexBuffer));
        ctx.SetIndexBuffer(resourceManager.buffers.GetNVRHIHandle(lod->indexBuffer));
        
        // Textures with streaming!
        for (u32 i = 0; i < node->textures.size(); i++) {
            ctx.SetTexture(i, resourceManager.textures.GetNVRHIHandle(node->textures[i]));
        }
        
        ctx.DrawIndexed(lod->indexCount, 0, 0);
    }
});
```

**Deliverable**: G-Buffer with streaming textures ✅

---

#### **Week 24: Deferred Lighting**

```cpp
PassHandle lightingPass = fg.AddPass("DeferredLighting");

fg.PassRead(lightingPass, albedo);
fg.PassRead(lightingPass, normal);
fg.PassRead(lightingPass, depth);

auto hdr = fg.CreateTexture("HDR", {...});
fg.PassWrite(lightingPass, hdr);

fg.SetPassCallback(lightingPass, [](RenderContext& ctx, const FrameGraph& fg) {
    // Bind G-Buffer
    ctx.SetTexture(0, fg.GetPhysicalTexture(albedo));
    ctx.SetTexture(1, fg.GetPhysicalTexture(normal));
    ctx.SetTexture(2, fg.GetPhysicalTexture(depth));
    
    // Light accumulation
    for (auto* light : lightManager.GetVisibleLights()) {
        UpdateLightConstants(light);
        ctx.Draw(3, 0);  // Fullscreen triangle or light volume
    }
});
```

**Deliverable**: Deferred lighting ✅

---

#### **Week 25: Shadow Cascades**

```cpp
for (u32 i = 0; i < 3; i++) {
    PassHandle shadowPass = fg.AddPass(fmt("ShadowCascade%d", i));
    auto shadowMap = fg.CreateTexture(fmt("ShadowMap%d", i), {...});
    
    fg.PassWrite(shadowPass, shadowMap);
    
    fg.SetPassCallback(shadowPass, [i](RenderContext& ctx) {
        // Render shadow casters from light's perspective
        RenderShadowCascade(ctx, i, query.shadowCasters);
    });
    
    fg.PassRead(lightingPass, shadowMap);
}
```

**Deliverable**: Cascaded shadows ✅

---

### **Phase 6: Advanced Features (Weeks 26-28)**

#### **Week 26: Detail/Grass Rendering**

```cpp
// GPU culling
PassHandle detailCullPass = fg.AddPass("DetailCulling");
detailCullPass.SetQueue(QueueType::Compute);

auto visibleInstances = fg.CreateBuffer("VisibleInstances", {...});
fg.PassWrite(detailCullPass, visibleInstances);

fg.SetPassCallback(detailCullPass, [](RenderContext& ctx) {
    ctx.SetComputePipeline(detailCullingCS);
    ctx.Dispatch(numInstances / 64, 1, 1);
});

// Render culled
PassHandle detailRenderPass = fg.AddPass("DetailRendering");
fg.PassRead(detailRenderPass, visibleInstances);
fg.SetPassCallback(detailRenderPass, [](RenderContext& ctx) {
    ctx.DrawIndexedIndirect(visibleInstances, 0);
});
```

**Deliverable**: Grass with GPU culling ✅

---

#### **Week 27: Post-Processing**

```cpp
// Bloom
auto bloomPass = fg.AddPass("Bloom");
fg.PassRead(bloomPass, hdr);
auto bloomTex = fg.CreateTexture("Bloom", {...});
fg.PassWrite(bloomPass, bloomTex);

// Tonemap
auto tonemapPass = fg.AddPass("Tonemap");
fg.PassRead(tonemapPass, hdr);
fg.PassRead(tonemapPass, bloomTex);
fg.PassWrite(tonemapPass, backbuffer);
```

**Deliverable**: Post-FX chain ✅

---

#### **Week 28: Async Compute + Polish**

```cpp
// SSAO on compute queue (runs parallel)
PassHandle ssaoPass = fg.AddPass("SSAO");
ssaoPass.SetQueue(QueueType::Compute);

fg.PassRead(ssaoPass, depthBuffer);
auto ssaoTex = fg.CreateTexture("SSAO", {...});
fg.PassWrite(ssaoPass, ssaoTex);

fg.PassRead(lightingPass, ssaoTex);  // Auto-sync
```

**Deliverable**: Feature complete! ✅

---

## 📊 **Timeline Summary**

| **Phase** | **Weeks** | **Key Deliverable** |
|-----------|-----------|---------------------|
| 0: Foundation | 1-3 | NVRHI integration |
| 1: Core Systems | 4-12 | RenderContext + FrameGraph + Resource Foundation |
| 2: Shader System | 13-16 | Modern shader compiler + hot-reload |
| 3: **Resource Streaming** | 17-19 | **Texture streaming + memory management** |
| 4: **Scene Graph** | 20-22 | **BVH + culling + LOD** |
| 5: Rendering | 23-25 | G-Buffer + Lighting + Shadows |
| 6: Advanced | 26-28 | Details + Post-FX + Async |

**Total: 28 weeks** (~7 months)

---

## 💰 **Why 28 Weeks vs 24 Weeks?**

| Aspect | Hybrid (24 weeks) | Clean (28 weeks) |
|--------|-------------------|------------------|
| **Resource Manager** | Wrap legacy | **Clean rewrite** |
| **Scene Graph** | Wrap HOM/BVH | **Clean BVH** |
| **Texture Streaming** | ❌ Hard to add | ✅ Built-in |
| **Memory Management** | ⚠️ Limited | ✅ Full control |
| **Code Quality** | ⚠️ Wrappers | ✅ Modern |
| **Future Maintenance** | ⚠️ Technical debt | ✅ Clean slate |

**Extra 4 weeks investment = Years of better architecture**

---

## ✅ **Success Criteria**

### **Technical**
- ✅ Feature parity with existing renderer
- ✅ 1.5-2x performance improvement
- ✅ Texture streaming working (1000+ textures under budget)
- ✅ BVH culling <1ms (10K objects)
- ✅ Hot-reload shaders (instant iteration)
- ✅ DX12/Vulkan ready (via NVRHI)

### **Quality**
- ✅ Zero regressions
- ✅ No asset re-export needed
- ✅ Existing .ogf meshes work
- ✅ Existing .dds textures work

---

## 🚀 **Ready to Start?**

This is the **clean architecture approach** you requested:
- Modern resource manager with streaming
- Clean BVH scene graph
- Keeps existing mesh loader (adapter pattern)
- 28 weeks total

**Next step**: Create detailed Week 1-3 implementation guide (NVRHI integration)?
