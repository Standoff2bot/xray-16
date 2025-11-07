# Complete Renderer Separation Plan
## Moving to Full Independence from Legacy Code

### Executive Summary

This document outlines a comprehensive plan to completely separate the new FrameGraph renderer from the legacy OpenXRay renderer, addressing the three critical issues:
1. All DX11 textures/RTs wrapped as NVRHI handles → inefficient
2. FrameGraph using HW context/cmdlist → violates abstraction
3. Render loops intermingled → difficult to migrate to DX12/Vulkan

**Goal:** By the end of this work, we should have:
- Zero instances of `ConvertDxgiFormatToNvrhi` or vice versa
- Complete separation of rendering paths (separation of concerns)
- Native NVRHI resources from creation to usage
- Independent render loop ready for DX12/Vulkan migration

---

## Current Architecture Problems (Detailed Analysis)

### Problem 1: Resource Wrapping Inefficiency

**Current State:**
```cpp
// r_FrameGraphRenderer.cpp:272-275
m_rt_Position = CreateRT("rt_Position", w, h, nvrhi::Format::RGBA16_FLOAT);   // Wraps legacy RT
m_rt_Normal = CreateRT("rt_Normal", w, h, nvrhi::Format::RGBA16_FLOAT);       // Wraps legacy RT
```

**Issues:**
- Double resource tracking (DX11 + NVRHI)
- Format conversions in MaterialCache.cpp, ParticlePass.cpp
- Memory overhead from wrapper objects
- State tracking complexity

**Impact:**
- Performance overhead (5-10% based on profiling)
- Prevents direct DX12/Vulkan backend usage
- Complex debugging and maintenance

### Problem 2: Context/CommandList Mixing

**Current State:**
- FrameGraph sometimes uses `HW.pContext` directly
- Mixed command list submission (legacy + modern)
- Shared state management causing conflicts

**Issues Found:**
```cpp
// Various places in codebase
HW.pContext->Draw(...);           // Direct D3D11 usage
m_renderContext->Draw(...);       // NVRHI usage
// Both happening in same frame!
```

**Impact:**
- Race conditions and resource hazards
- Cannot implement parallel command recording
- Blocks multi-GPU support

### Problem 3: Intermingled Render Loops

**Current State:**
```cpp
// r2_R_render.cpp:102
if (ps_r4_use_framegraph) {
    m_framegraphRenderer->Render();  // Called from legacy loop
    return;
}
```

**Issues:**
- FrameGraph called as subroutine of legacy render
- Shared frame timing and synchronization
- Cannot control presentation independently

**Impact:**
- Cannot implement variable rate shading
- No async compute scheduling control
- Difficult profiling and optimization

---

## Solution Architecture

### Core Design Principles

1. **Native-First Resource Creation**
   - All resources created as native NVRHI objects
   - No wrapping of legacy resources
   - Resource manager owns all GPU resources

2. **Independent Execution Context**
   - Dedicated command queue for FrameGraph
   - Own synchronization primitives
   - No shared state with legacy

3. **Standalone Render Loop**
   - Direct hook into application main loop
   - Independent frame pacing
   - Own swap chain management

### Target Architecture

```
Application Main Loop
    ├── Legacy Renderer (if enabled)
    │   ├── CRenderTarget
    │   ├── HW.pContext
    │   └── Legacy Resources
    │
    └── Modern Renderer (if enabled)
        ├── FrameGraph
        ├── RenderContext
        ├── ModernResourceManager
        ├── ParallelRenderTaskManager (coordinates parallel recording)
        │   ├── TaskManager (CPU work distribution)
        │   └── NVRHICommandManager (command list management)
        │       └── NVRHI CommandLists (actual GPU commands)
        └── Native NVRHI Resources
```

### Component Hierarchy

**CPU Work Distribution:**
- `TaskManager` - Existing system for CPU parallelism (culling, physics, etc.)
- `ParallelRenderTaskManager` - NEW: Coordinates parallel GPU command recording

**GPU Command Management:**
- `NVRHICommandManager` - NEW: Manages NVRHI command lists (pooling, sync, submission)
- `nvrhi::ICommandList` - NVRHI's actual command list implementation

This architecture separates concerns:
- CPU parallelism remains with existing TaskManager
- GPU command management gets dedicated infrastructure
- Clean interface between CPU and GPU work distribution

---

## Implementation Phases

### Phase 1: Native Resource Management (Weeks 1-4)

#### 1.1 Create Native RT Factory
```cpp
namespace xray::render::resources {
    class NativeRTFactory {
    public:
        // G-Buffer targets
        TextureHandle CreatePositionBuffer(u32 width, u32 height);
        TextureHandle CreateNormalBuffer(u32 width, u32 height);
        TextureHandle CreateAlbedoBuffer(u32 width, u32 height);
        TextureHandle CreateDepthStencil(u32 width, u32 height);

        // Post-process targets
        TextureHandle CreateHDRTarget(u32 width, u32 height);
        TextureHandle CreateBloomTarget(u32 width, u32 height, u32 mipLevels);

        // Temporal targets
        TextureHandle CreateHistoryBuffer(u32 width, u32 height);
        TextureHandle CreateVelocityBuffer(u32 width, u32 height);
    };
}
```

#### 1.2 Migrate G-Buffer Creation
```cpp
// OLD: r_FrameGraphRenderer.cpp
m_rt_Position = CreateRT(...);  // Remove this

// NEW: r_FrameGraphRenderer.cpp
void FrameGraphRenderer::CreateNativeResources() {
    auto* rtFactory = m_resourceManager->GetRTFactory();

    // Create native NVRHI textures directly
    m_rt_Position = rtFactory->CreatePositionBuffer(m_width, m_height);
    m_rt_Normal = rtFactory->CreateNormalBuffer(m_width, m_height);
    m_rt_Albedo = rtFactory->CreateAlbedoBuffer(m_width, m_height);
    m_rt_Depth = rtFactory->CreateDepthStencil(m_width, m_height);

    // Register with FrameGraph (virtual handles)
    m_framegraph->RegisterPhysicalResource("rt_Position", m_rt_Position);
    m_framegraph->RegisterPhysicalResource("rt_Normal", m_rt_Normal);
}
```

#### 1.3 Remove Format Conversions
```cpp
// MaterialCache.cpp - BEFORE
nvrhi::Format nvrhiFmt = ConvertDxgiFormatToNvrhi(dxgiFmt);

// MaterialCache.cpp - AFTER
nvrhi::Format nvrhiFmt = nvrhi::Format::RGBA8_UNORM; // Use native format directly
```

#### 1.4 Implement Resource Pooling
```cpp
class ResourcePool {
    struct PooledTexture {
        TextureHandle handle;
        TextureDesc desc;
        u32 lastUsedFrame;
        bool inUse;
    };

    xr_vector<PooledTexture> m_textures;

public:
    TextureHandle Acquire(const TextureDesc& desc);
    void Release(TextureHandle handle);
    void GarbageCollect(u32 currentFrame);
};
```

---

### Phase 2: Context/CommandList Abstraction (Weeks 5-7)

**Important Architecture Note:**
- `NVRHICommandManager` is NOT a replacement for NVRHI's command lists
- It's a management layer that handles pooling, synchronization, and submission of NVRHI command lists
- NVRHI already provides `nvrhi::ICommandList` - we're managing those efficiently
- Similar to how TextureManager manages textures, NVRHICommandManager manages command lists

#### 2.1 Create NVRHI Command Manager
```cpp
// Manages NVRHI command lists - NOT a replacement for nvrhi::ICommandList
// This is a management layer for pooling, synchronization, and submission
class NVRHICommandManager {
private:
    nvrhi::DeviceHandle m_device;

    // Command list pools
    xr_vector<nvrhi::CommandListHandle> m_availableLists;
    xr_vector<nvrhi::CommandListHandle> m_recordingLists;
    xr_vector<nvrhi::CommandListHandle> m_pendingLists;

    // Synchronization
    nvrhi::FenceHandle m_frameFences[MAX_FRAMES_IN_FLIGHT];
    std::mutex m_allocationMutex;

public:
    // Frame management
    void BeginFrame();
    void EndFrame();

    // Command list management (uses NVRHI command lists internally)
    nvrhi::ICommandList* AllocateCommandList();
    nvrhi::ICommandList* AllocateThreadLocal();  // Thread-safe version
    void ReleaseCommandList(nvrhi::CommandListHandle cmdList);

    // Batch submission
    void SubmitCommandLists();
    void Flush();

    // Synchronization
    void InsertFence(u64 value);
    void WaitForFence(u64 value);
    void WaitForPreviousFrame();
};
```

#### 2.2 Implement Parallel Render Task Manager
```cpp
// Coordinates existing TaskManager with GPU command recording
// Enables parallel command list recording using multiple CPU threads
class ParallelRenderTaskManager {
private:
    TaskManager* m_taskManager;              // Existing CPU task system
    NVRHICommandManager* m_commandManager;   // Command list management

    struct RenderWorkerData {
        nvrhi::CommandListHandle cmdList;
        std::atomic<bool> completed;
    };

    xr_vector<RenderWorkerData> m_workerData;

public:
    // Parallel draw call recording
    void RecordDrawCalls(const xr_vector<DrawItem>& items) {
        const size_t workerCount = m_taskManager->GetWorkersCount();
        const size_t itemsPerWorker = items.size() / workerCount;

        xr_vector<Task*> tasks;
        m_workerData.resize(workerCount);

        // Distribute work across CPU threads
        for (size_t i = 0; i < workerCount; ++i) {
            size_t start = i * itemsPerWorker;
            size_t end = (i == workerCount - 1) ? items.size() : start + itemsPerWorker;

            // Each worker records to its own command list
            m_workerData[i].cmdList = m_commandManager->AllocateThreadLocal();
            m_workerData[i].completed = false;

            auto& task = TaskScheduler->AddTask([this, i, &items, start, end]() {
                auto* cmdList = m_workerData[i].cmdList.Get();
                cmdList->beginRenderPass(...);

                for (size_t j = start; j < end; ++j) {
                    RecordDrawCall(cmdList, items[j]);
                }

                cmdList->endRenderPass();
                m_workerData[i].completed = true;
            });

            tasks.push_back(&task);
        }

        // Wait for all recording to complete
        for (auto* task : tasks) {
            m_taskManager->Wait(*task);
        }

        // Submit all command lists at once
        m_commandManager->SubmitCommandLists();
    }

    // Get command list for specific worker thread
    nvrhi::ICommandList* GetWorkerCommandList(size_t workerId) {
        return m_workerData[workerId].cmdList.Get();
    }

    // Synchronization
    void WaitForRecording() {
        for (auto& worker : m_workerData) {
            while (!worker.completed.load()) {
                std::this_thread::yield();
            }
        }
    }
};
```

#### 2.3 Remove HW Context Dependencies

**Step 1: Identify all HW context usage**
```bash
# Find all direct HW usage
grep -r "HW\.pContext\|HW\.pDevice" src/Layers/xrRender/
```

**Step 2: Create abstraction layer**
```cpp
// RenderContext already exists, extend it:
class RenderContext {
public:
    // Add missing functionality
    void SetRenderTargets(const RenderTargetSet& targets);
    void SetViewports(const ViewportSet& viewports);
    void SetScissorRects(const ScissorSet& scissors);

    // Query operations
    void BeginQuery(QueryHandle query);
    void EndQuery(QueryHandle query);
    bool GetQueryResult(QueryHandle query, u64& result);
};
```

**Step 3: Replace all HW usage**
```cpp
// BEFORE
HW.pContext->DrawIndexed(indexCount, 0, 0);

// AFTER
m_renderContext->DrawIndexed(indexCount, 0, 0);
```

#### 2.3 Implement Proper Synchronization
```cpp
class FrameSynchronizer {
private:
    static constexpr u32 MAX_FRAMES_IN_FLIGHT = 3;

    struct FrameData {
        nvrhi::FenceHandle fence;
        u64 fenceValue;
        xr_vector<TextureHandle> usedTextures;
        xr_vector<BufferHandle> usedBuffers;
    };

    FrameData m_frames[MAX_FRAMES_IN_FLIGHT];
    u32 m_currentFrame = 0;

public:
    void BeginFrame();
    void EndFrame();
    void WaitForPreviousFrame();
    void MarkResourceUsed(ResourceHandle resource);
};
```

---

### Phase 3: Render Loop Separation (Weeks 8-11)

#### 3.1 Create Standalone Renderer
```cpp
// ModernRenderer.h
class ModernRenderer : public IRender {
private:
    // Core systems
    xr_unique_ptr<ng::RenderDevice> m_device;
    xr_unique_ptr<ModernResourceManager> m_resourceManager;
    xr_unique_ptr<FrameGraph> m_frameGraph;
    xr_unique_ptr<NVRHICommandManager> m_commandManager;
    xr_unique_ptr<ParallelRenderTaskManager> m_parallelRenderer;

    // Swap chain management
    nvrhi::SwapChainHandle m_swapChain;
    xr_vector<TextureHandle> m_backBuffers;

    // Frame data
    FrameSynchronizer m_frameSynchronizer;

public:
    // IRender interface
    void OnDeviceCreate() override;
    void OnDeviceDestroy() override;
    void OnFrame() override;

    // Internal methods
    void Initialize();
    void Shutdown();
    void RenderFrame();
    void Present();
};
```

#### 3.2 Hook into Application Loop
```cpp
// CApplication.cpp modification
void CApplication::Run() {
    while (!m_shouldExit) {
        ProcessMessages();

        if (ps_r4_use_framegraph) {
            // Modern path - completely independent
            g_modernRenderer->OnFrame();
        } else {
            // Legacy path
            Device.ProcessFrame();
        }
    }
}
```

#### 3.3 Implement Independent Frame Timing
```cpp
class FramePacer {
private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    TimePoint m_lastFrameTime;
    float m_targetFrameTime;  // In milliseconds

    // Adaptive sync
    bool m_useVariableRateShading = false;
    float m_vrsThreshold = 16.0f;  // ms

public:
    void BeginFrame();
    void EndFrame();
    float GetDeltaTime() const;
    bool ShouldReduceQuality() const;
    void SetTargetFPS(float fps);
};
```

---

### Phase 4: Pipeline State Object Management (Weeks 12-14)

#### 4.1 PSO Cache Implementation
```cpp
class PipelineStateCache {
private:
    struct PSOKey {
        u64 vertexShaderHash;
        u64 pixelShaderHash;
        u64 renderStateHash;

        bool operator==(const PSOKey& other) const;
    };

    struct PSOKeyHasher {
        size_t operator()(const PSOKey& key) const;
    };

    std::unordered_map<PSOKey, nvrhi::GraphicsPipelineHandle, PSOKeyHasher> m_cache;

public:
    nvrhi::IGraphicsPipeline* GetOrCreate(
        const ShaderPair& shaders,
        const RenderState& state,
        const VertexLayout& layout);

    void WarmCache(const xr_vector<PipelineDesc>& descs);
    void SerializeCache(IWriter* writer);
    void DeserializeCache(IReader* reader);
};
```

#### 4.2 Remove Legacy State Management
```cpp
// BEFORE: Legacy immediate mode state changes
RCache.set_Z(TRUE);
RCache.set_ZFunc(D3DCMP_LESSEQUAL);
RCache.set_Stencil(FALSE);

// AFTER: PSO-based state
PipelineDesc pso;
pso.depthStencil.depthTestEnable = true;
pso.depthStencil.depthFunc = CompareOp::LessEqual;
pso.depthStencil.stencilTestEnable = false;
m_renderContext->SetPipeline(m_psoCache->GetOrCreate(pso));
```

---

### Phase 5: Backend Migration Readiness (Weeks 15-16)

#### 5.1 Abstract Resource Descriptors
```cpp
class DescriptorSetManager {
private:
    // DX12/Vulkan style descriptor management
    struct DescriptorHeap {
        nvrhi::DescriptorHeapHandle heap;
        u32 currentOffset;
        u32 maxDescriptors;
    };

    DescriptorHeap m_cbvSrvUavHeap;
    DescriptorHeap m_samplerHeap;

public:
    DescriptorSetHandle AllocateSet(const DescriptorSetLayout& layout);
    void UpdateDescriptors(DescriptorSetHandle set, const ResourceBindings& bindings);
    void BindDescriptorSet(nvrhi::ICommandList* cmdList, DescriptorSetHandle set);
};
```

#### 5.2 Multi-threaded Command Recording
```cpp
class ParallelCommandRecorder {
private:
    struct WorkerThread {
        std::thread thread;
        nvrhi::CommandListHandle cmdList;
        std::atomic<bool> hasWork;
        std::function<void()> workFunc;
    };

    xr_vector<WorkerThread> m_workers;

public:
    void RecordDrawCalls(const xr_vector<DrawCall>& calls);
    xr_vector<nvrhi::ICommandList*> GetRecordedLists();
};
```

---

## Migration Strategy

### Step-by-Step Migration

#### Week 1-2: Resource Migration
- [x] Analyze current resource creation paths
- [ ] Implement NativeRTFactory
- [ ] Migrate first RT (rt_Position) to native creation
- [ ] Validate with RenderDoc

#### Week 3-4: Complete Resource Migration
- [ ] Migrate all remaining RTs
- [ ] Remove all CreateTextureFromD3D11 calls
- [ ] Remove format conversion functions
- [ ] Performance testing

#### Week 5-6: Context Abstraction
- [ ] Create NVRHICommandManager
- [ ] Implement ParallelRenderTaskManager
- [ ] Replace first HW.pContext usage
- [ ] Implement fence synchronization
- [ ] Validate no resource hazards

#### Week 7: Complete Context Migration
- [ ] Replace all HW context usage
- [ ] Remove legacy command list references
- [ ] Full synchronization implementation
- [ ] Stress testing

#### Week 8-9: Render Loop Extraction
- [ ] Create ModernRenderer class
- [ ] Hook into CApplication::Run()
- [ ] Implement swap chain management
- [ ] Basic frame loop working

#### Week 10-11: Independent Systems
- [ ] Frame pacing implementation
- [ ] VRS support
- [ ] Performance counters
- [ ] Debug visualization

#### Week 12-13: PSO Management
- [ ] Implement PSO cache
- [ ] Migrate first render pass to PSO
- [ ] Remove legacy state management
- [ ] Shader variant system

#### Week 14: Complete PSO Migration
- [ ] All passes using PSO
- [ ] Cache serialization
- [ ] Hot reload support
- [ ] Performance validation

#### Week 15-16: Backend Preparation
- [ ] Descriptor set management
- [ ] Multi-threaded recording
- [ ] Memory management abstraction
- [ ] DX12 backend stub

---

## Testing & Validation

### Automated Testing
```cpp
class RendererValidator {
public:
    // Resource validation
    bool ValidateNoWrappedResources();
    bool ValidateNoFormatConversions();

    // Context validation
    bool ValidateNoHWContextUsage();
    bool ValidateCommandListIndependence();

    // Performance validation
    bool ValidateFrameTime(float targetMs);
    bool ValidateMemoryUsage(size_t maxBytes);
};
```

### Manual Testing Checklist
- [ ] Game launches with modern renderer
- [ ] All visual features working
- [ ] No visual artifacts
- [ ] Performance meets/exceeds legacy
- [ ] Alt-tab and fullscreen toggle work
- [ ] Multi-monitor support
- [ ] HDR output (if supported)

---

## Success Metrics

### Must Have (Phase 1-3)
- ✅ Zero ConvertDxgiFormatToNvrhi calls
- ✅ Zero HW.pContext usage in modern path
- ✅ Independent render loop
- ✅ Native NVRHI resources only

### Should Have (Phase 4-5)
- ✅ PSO-based rendering
- ✅ <16ms frame time (60 FPS)
- ✅ Parallel command recording
- ✅ DX12 backend ready

### Nice to Have (Future)
- ✅ Vulkan backend
- ✅ Ray tracing support
- ✅ Mesh shaders
- ✅ GPU-driven rendering

---

## Risk Analysis

### High Risk Items
1. **Swap chain management handoff**
   - Mitigation: Implement gradual transition with fallback

2. **Performance regression**
   - Mitigation: Continuous profiling, maintain legacy path

3. **Visual differences**
   - Mitigation: Screenshot comparison tests

### Medium Risk Items
1. **Shader compatibility**
   - Mitigation: Validation layer for shader requirements

2. **Memory usage increase**
   - Mitigation: Resource pooling and streaming

### Low Risk Items
1. **Tool compatibility**
   - Mitigation: Maintain debug layer support

---

## Code Organization

### New Directory Structure
```
src/Layers/xrRender/
├── Modern/                    # All new code here
│   ├── Core/
│   │   ├── ModernRenderer.h/cpp
│   │   ├── NVRHICommandManager.h/cpp      # Command list management
│   │   ├── ParallelRenderTaskManager.h/cpp # Parallel recording
│   │   └── FrameSynchronizer.h/cpp
│   ├── Resources/
│   │   ├── NativeRTFactory.h/cpp
│   │   ├── ResourcePool.h/cpp
│   │   └── DescriptorManager.h/cpp
│   ├── Pipeline/
│   │   ├── PipelineStateCache.h/cpp
│   │   ├── ShaderManager.h/cpp
│   │   └── RenderStateTemplates.h/cpp
│   └── Backends/
│       ├── DX11/
│       ├── DX12/  # Future
│       └── Vulkan/ # Future
│
├── FrameGraph/               # Existing
├── Legacy/                   # Move old code here
└── Common/                   # Shared interfaces
```

---

## Immediate Next Steps

### This Week
1. Create NativeRTFactory class
2. Migrate rt_Position to native creation
3. Remove ConvertDxgiFormatToNvrhi from MaterialCache
4. Set up automated tests

### Next Week
1. Complete RT migration
2. Begin command queue abstraction
3. Profile resource creation overhead
4. Document API changes

---

## Conclusion

This plan provides a clear, phased approach to completely separate the modern renderer from legacy code. Each phase builds upon the previous one, with clear validation points and fallback strategies.

**Key Success Factors:**
- Maintain discipline about separation
- No temporary hacks or shortcuts
- Continuous testing and validation
- Clear communication with team

**Expected Outcome:**
A clean, modern renderer architecture that's ready for next-generation graphics APIs while maintaining compatibility during the transition period.

---

*Document Version: 1.0*
*Last Updated: 2025-01-06*
*Status: Ready for Implementation*