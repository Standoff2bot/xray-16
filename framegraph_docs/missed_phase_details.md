# 🎯 Complete RenderContext-First Architecture: Technical Implementation Guide

## 📋 Executive Summary

**Architecture Strategy**: Build from the ground up in three layers:
1. **NVRHI** (Weeks 1-3): Battle-tested graphics abstraction
2. **RenderContext** (Weeks 4-7): X-Ray-specific rendering API
3. **FrameGraph** (Weeks 8-10): High-level dependency management

**Timeline**: 16-20 weeks for complete pipeline  
**Approach**: Separate codepath, zero impact on existing renderer

---

## 🏗️ Architecture Stack

```
┌─────────────────────────────────────┐
│     FrameGraph (High-level)         │  ← Week 8-10
│     • Virtual resources             │     Dependency graph
│     • Pass scheduling                │     Automatic barriers
│     • Memory aliasing                │     Culling optimization
├─────────────────────────────────────┤
│     RenderContext (Mid-level)       │  ← Week 4-7
│     • Command recording              │     PSO management
│     • State tracking                 │     Resource binding
│     • Explicit barriers              │     Draw call API
├─────────────────────────────────────┤
│     NVRHI (Low-level abstraction)   │  ← Week 1-3
│     • Command lists                  │     Resource tracking
│     • Pipeline objects               │     Multi-API support
│     • Descriptor management          │     State validation
├─────────────────────────────────────┤
│     DX11 / DX12 / Vulkan            │  ← Hardware
└─────────────────────────────────────┘
```

**Key Design Principles:**
- **Incremental Validation**: Prove each layer works before building next
- **Clean Separation**: No NVRHI types leak into RenderContext, no RenderContext types leak into FrameGraph
- **Future-Proof**: DX12/Vulkan support via backend swap (change 1 line)
- **Zero Disruption**: Parallel development, toggle between old/new renderer

---

# 🔧 Phase 0: NVRHI Integration (Weeks 1-3)

## Overview

NVRHI (NVIDIA Hardware Interface) provides:
- Unified API across DX11, DX12, Vulkan
- Production-tested (Quake RTX, DLSS samples)
- Command list management, PSO handling, descriptor management
- Resource tracking and barrier insertion

**Repository**: https://github.com/NVIDIAGameWorks/nvrhi

---

## Milestone 0.1: NVRHI Library Integration

### Objective
Get NVRHI compiling and linking in X-Ray engine build system.

### Implementation Steps

#### 1. Add NVRHI as Git Submodule

```bash
cd xray-monolith/
git submodule add https://github.com/NVIDIAGameWorks/nvrhi External/nvrhi
git submodule update --init --recursive
```

**Why submodule?**
- Pin to specific commit (stability)
- Easy updates (`git submodule update`)
- Clean separation from engine code

---

#### 2. CMake Integration

```cmake
# xray-monolith/CMakeLists.txt

# Add NVRHI library
add_subdirectory(External/nvrhi)

# Configure NVRHI options
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "Build NVRHI as static lib")
set(NVRHI_WITH_DX11 ON CACHE BOOL "Enable DX11 backend")
set(NVRHI_WITH_DX12 OFF CACHE BOOL "Disable DX12 for now")
set(NVRHI_WITH_VULKAN OFF CACHE BOOL "Disable Vulkan for now")
```

```cmake
# Layers/xrRender/CMakeLists.txt

target_link_libraries(xrRender PRIVATE
    nvrhi           # Core NVRHI interface
    nvrhi-d3d11     # DX11 backend implementation
)

target_include_directories(xrRender PRIVATE
    ${CMAKE_SOURCE_DIR}/External/nvrhi/include
)
```

**Performance Note**: Static linking avoids DLL overhead (~0.2ms per frame).

---

#### 3. Minimal Test Integration

```cpp
// xrRender/NVRHI/NVRHITest.h
#pragma once

namespace xray::render::nvrhi_test {

// Test functions (call from main menu or console command)
void TestNVRHIBasics();        // Verify library loads
void TestNVRHIDeviceCreation(); // Wrap existing D3D11 device
void TestNVRHIClear();         // Clear screen test

} // namespace
```

```cpp
// xrRender/NVRHI/NVRHITest.cpp
#include "stdafx.h"
#include "NVRHITest.h"
#include <nvrhi/nvrhi.h>
#include <nvrhi/d3d11.h>

#include "../xrRender_console.h"  // For HW.pDevice access

namespace xray::render::nvrhi_test {

void TestNVRHIBasics() {
    Msg("! [NVRHI Test] Starting basic tests...");
    
    // Verify NVRHI headers accessible
    nvrhi::DeviceDesc desc;
    Msg("! [NVRHI Test] Headers OK");
    
    // Verify version
    const char* version = nvrhi::GetVersionString();
    Msg("! [NVRHI Test] Version: %s", version);
}

void TestNVRHIDeviceCreation() {
    Msg("! [NVRHI Test] Creating NVRHI device from existing D3D11...");
    
    // Wrap existing X-Ray D3D11 device
    nvrhi::d3d11::DeviceDesc deviceDesc;
    deviceDesc.device = HW.pDevice;        // X-Ray's ID3D11Device
    deviceDesc.context = HW.pContext;      // X-Ray's ID3D11DeviceContext
    deviceDesc.messageCallback = [](nvrhi::MessageSeverity severity, 
                                     const char* message) {
        switch (severity) {
            case nvrhi::MessageSeverity::Info:
                Msg("! [NVRHI Info] %s", message);
                break;
            case nvrhi::MessageSeverity::Warning:
                Msg("! [NVRHI Warning] %s", message);
                break;
            case nvrhi::MessageSeverity::Error:
                Msg("! [NVRHI Error] %s", message);
                break;
            case nvrhi::MessageSeverity::Fatal:
                Msg("! [NVRHI Fatal] %s", message);
                break;
        }
    };
    
    nvrhi::DeviceHandle nvrhiDevice = nvrhi::d3d11::createDevice(deviceDesc);
    
    if (nvrhiDevice) {
        Msg("! [NVRHI Test] ✅ Device created successfully");
        Msg("! [NVRHI Test] Device name: %s", 
            nvrhiDevice->getDeviceName().c_str());
        Msg("! [NVRHI Test] Graphics API: %s",
            nvrhi::utils::GraphicsAPIToString(nvrhiDevice->getGraphicsAPI()));
    } else {
        Msg("! [NVRHI Test] ❌ Device creation failed");
    }
}

} // namespace
```

```cpp
// Register console command for testing
// In xrRender_console.cpp or similar

class CCC_NVRHITest : public IConsole_Command {
public:
    virtual void Execute(LPCSTR args) {
        xray::render::nvrhi_test::TestNVRHIBasics();
        xray::render::nvrhi_test::TestNVRHIDeviceCreation();
    }
};

// In console registration
Console->AddCommand(new CCC_NVRHITest(), "nvrhi_test");
```

---

### Success Criteria

- ✅ NVRHI builds without errors (0 warnings preferred)
- ✅ Can call `nvrhi::d3d11::createDevice()` successfully
- ✅ Console command `nvrhi_test` runs without crashes
- ✅ No impact on existing renderer (old renderer still works)
- ✅ Build time increase: <5 seconds

### Performance Metrics

- **Memory Overhead**: ~2MB (NVRHI code + metadata)
- **Initialization Time**: <1ms (device wrapping)
- **Runtime Overhead**: 0ms (no code executing yet)

---

## Milestone 0.2: NVRHI Device Wrapper

### Objective
Create X-Ray-specific device manager that encapsulates NVRHI device lifecycle and provides resource creation helpers.

### Implementation

```cpp
// xrRender/NVRHI/NVRHIDevice.h
#pragma once

#include <nvrhi/nvrhi.h>

namespace xray::render::nvrhi_backend {

class NVRHIDevice {
public:
    NVRHIDevice();
    ~NVRHIDevice();
    
    // ═══════════════════════════════════════════════════
    //  INITIALIZATION
    // ═══════════════════════════════════════════════════
    
    // Hybrid mode: wrap existing D3D11 device
    bool InitializeFromD3D11(ID3D11Device* device, 
                             ID3D11DeviceContext* context);
    
    // Standalone mode (future DX12/Vulkan)
    bool InitializeStandalone(nvrhi::GraphicsAPI api);
    
    void Shutdown();
    
    // ═══════════════════════════════════════════════════
    //  DEVICE ACCESS
    // ═══════════════════════════════════════════════════
    
    nvrhi::IDevice* GetDevice() const { 
        return m_device.Get(); 
    }
    
    nvrhi::ICommandList* GetCommandList() const { 
        return m_cmdList.Get(); 
    }
    
    bool IsInitialized() const { 
        return m_device != nullptr; 
    }
    
    // ═══════════════════════════════════════════════════
    //  RESOURCE CREATION
    // ═══════════════════════════════════════════════════
    
    // Textures
    nvrhi::TextureHandle CreateTexture(
        const nvrhi::TextureDesc& desc);
    
    nvrhi::TextureHandle CreateTextureFromD3D11(
        ID3D11Resource* d3d11Resource,
        const nvrhi::TextureDesc& desc);
    
    // Buffers
    nvrhi::BufferHandle CreateBuffer(
        const nvrhi::BufferDesc& desc,
        const void* initialData = nullptr);
    
    // Samplers
    nvrhi::SamplerHandle CreateSampler(
        const nvrhi::SamplerDesc& desc);
    
    // Shaders
    nvrhi::ShaderHandle CreateShader(
        const nvrhi::ShaderDesc& desc,
        const void* binary,
        size_t binarySize);
    
    // ═══════════════════════════════════════════════════
    //  PIPELINE CREATION
    // ═══════════════════════════════════════════════════
    
    nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(
        const nvrhi::GraphicsPipelineDesc& desc,
        const nvrhi::FramebufferHandle& framebuffer);
    
    nvrhi::ComputePipelineHandle CreateComputePipeline(
        const nvrhi::ComputePipelineDesc& desc);
    
    // ═══════════════════════════════════════════════════
    //  COMMAND EXECUTION
    // ═══════════════════════════════════════════════════
    
    void ExecuteCommandList(nvrhi::ICommandList* cmd);
    void WaitForIdle();
    
    // ═══════════════════════════════════════════════════
    //  STATISTICS
    // ═══════════════════════════════════════════════════
    
    struct Statistics {
        u32 texturesCreated = 0;
        u32 buffersCreated = 0;
        u32 pipelinesCreated = 0;
        u64 vramAllocated = 0;     // Bytes
        u32 commandListsExecuted = 0;
    };
    
    const Statistics& GetStatistics() const { return m_stats; }
    void ResetStatistics();
    
private:
    // Core NVRHI objects
    nvrhi::DeviceHandle m_device;
    nvrhi::CommandListHandle m_cmdList;
    
    // Statistics
    Statistics m_stats;
    
    // Resource tracking (for debugging/leak detection)
    xr_vector<nvrhi::TextureHandle> m_textures;
    xr_vector<nvrhi::BufferHandle> m_buffers;
    xr_vector<nvrhi::SamplerHandle> m_samplers;
    
    // Internal helpers
    void LogDeviceInfo();
};

} // namespace xray::render::nvrhi_backend
```

```cpp
// xrRender/NVRHI/NVRHIDevice.cpp
#include "stdafx.h"
#include "NVRHIDevice.h"
#include <nvrhi/d3d11.h>

namespace xray::render::nvrhi_backend {

NVRHIDevice::NVRHIDevice() {
    Msg("! [NVRHIDevice] Constructor");
}

NVRHIDevice::~NVRHIDevice() {
    Shutdown();
}

bool NVRHIDevice::InitializeFromD3D11(ID3D11Device* device, 
                                       ID3D11DeviceContext* context) {
    VERIFY(device);
    VERIFY(context);
    
    Msg("! [NVRHIDevice] Initializing from D3D11 device...");
    
    // Create device descriptor
    nvrhi::d3d11::DeviceDesc deviceDesc;
    deviceDesc.device = device;
    deviceDesc.context = context;
    deviceDesc.messageCallback = [](nvrhi::MessageSeverity severity,
                                     const char* message) {
        // Route NVRHI messages to X-Ray logging
        switch (severity) {
            case nvrhi::MessageSeverity::Info:
                Msg("! [NVRHI] %s", message);
                break;
            case nvrhi::MessageSeverity::Warning:
                Msg("~ [NVRHI Warning] %s", message);
                break;
            case nvrhi::MessageSeverity::Error:
                Msg("! [NVRHI Error] %s", message);
                break;
            case nvrhi::MessageSeverity::Fatal:
                Msg("! [NVRHI FATAL] %s", message);
                R_ASSERT(false);
                break;
        }
    };
    
    // Create NVRHI device
    m_device = nvrhi::d3d11::createDevice(deviceDesc);
    
    if (!m_device) {
        Msg("! [NVRHIDevice] ❌ Failed to create NVRHI device");
        return false;
    }
    
    // Create command list
    m_cmdList = m_device->createCommandList();
    
    if (!m_cmdList) {
        Msg("! [NVRHIDevice] ❌ Failed to create command list");
        m_device = nullptr;
        return false;
    }
    
    LogDeviceInfo();
    
    Msg("! [NVRHIDevice] ✅ Initialization successful");
    return true;
}

bool NVRHIDevice::InitializeStandalone(nvrhi::GraphicsAPI api) {
    // Future: DX12/Vulkan initialization
    Msg("! [NVRHIDevice] Standalone initialization not yet implemented");
    return false;
}

void NVRHIDevice::Shutdown() {
    if (!m_device) return;
    
    Msg("! [NVRHIDevice] Shutting down...");
    
    // Wait for GPU
    WaitForIdle();
    
    // Release resources
    m_textures.clear();
    m_buffers.clear();
    m_samplers.clear();
    
    m_cmdList = nullptr;
    m_device = nullptr;
    
    Msg("! [NVRHIDevice] Shutdown complete");
}

// ═══════════════════════════════════════════════════
//  RESOURCE CREATION
// ═══════════════════════════════════════════════════

nvrhi::TextureHandle NVRHIDevice::CreateTexture(
    const nvrhi::TextureDesc& desc) {
    
    VERIFY(m_device);
    
    nvrhi::TextureHandle texture = m_device->createTexture(desc);
    
    if (texture) {
        m_textures.push_back(texture);
        m_stats.texturesCreated++;
        m_stats.vramAllocated += texture->getDesc().getMemorySize();
    }
    
    return texture;
}

nvrhi::TextureHandle NVRHIDevice::CreateTextureFromD3D11(
    ID3D11Resource* d3d11Resource,
    const nvrhi::TextureDesc& desc) {
    
    VERIFY(m_device);
    VERIFY(d3d11Resource);
    
    nvrhi::TextureHandle texture = m_device->createHandleForNativeTexture(
        nvrhi::ObjectTypes::D3D11_Resource,
        nvrhi::Object(d3d11Resource),
        desc
    );
    
    if (texture) {
        m_textures.push_back(texture);
        m_stats.texturesCreated++;
    }
    
    return texture;
}

nvrhi::BufferHandle NVRHIDevice::CreateBuffer(
    const nvrhi::BufferDesc& desc,
    const void* initialData) {
    
    VERIFY(m_device);
    
    nvrhi::BufferHandle buffer = m_device->createBuffer(desc);
    
    if (buffer && initialData) {
        // Upload initial data
        m_cmdList->open();
        m_cmdList->writeBuffer(buffer, initialData, desc.byteSize);
        m_cmdList->close();
        ExecuteCommandList(m_cmdList);
    }
    
    if (buffer) {
        m_buffers.push_back(buffer);
        m_stats.buffersCreated++;
        m_stats.vramAllocated += desc.byteSize;
    }
    
    return buffer;
}

nvrhi::SamplerHandle NVRHIDevice::CreateSampler(
    const nvrhi::SamplerDesc& desc) {
    
    VERIFY(m_device);
    
    nvrhi::SamplerHandle sampler = m_device->createSampler(desc);
    
    if (sampler) {
        m_samplers.push_back(sampler);
    }
    
    return sampler;
}

nvrhi::ShaderHandle NVRHIDevice::CreateShader(
    const nvrhi::ShaderDesc& desc,
    const void* binary,
    size_t binarySize) {
    
    VERIFY(m_device);
    
    return m_device->createShader(desc, binary, binarySize);
}

// ═══════════════════════════════════════════════════
//  PIPELINE CREATION
// ═══════════════════════════════════════════════════

nvrhi::GraphicsPipelineHandle NVRHIDevice::CreateGraphicsPipeline(
    const nvrhi::GraphicsPipelineDesc& desc,
    const nvrhi::FramebufferHandle& framebuffer) {
    
    VERIFY(m_device);
    
    nvrhi::GraphicsPipelineHandle pipeline = 
        m_device->createGraphicsPipeline(desc, framebuffer);
    
    if (pipeline) {
        m_stats.pipelinesCreated++;
    }
    
    return pipeline;
}

nvrhi::ComputePipelineHandle NVRHIDevice::CreateComputePipeline(
    const nvrhi::ComputePipelineDesc& desc) {
    
    VERIFY(m_device);
    
    nvrhi::ComputePipelineHandle pipeline = 
        m_device->createComputePipeline(desc);
    
    if (pipeline) {
        m_stats.pipelinesCreated++;
    }
    
    return pipeline;
}

// ═══════════════════════════════════════════════════
//  COMMAND EXECUTION
// ═══════════════════════════════════════════════════

void NVRHIDevice::ExecuteCommandList(nvrhi::ICommandList* cmd) {
    VERIFY(m_device);
    VERIFY(cmd);
    
    m_device->executeCommandList(cmd);
    m_stats.commandListsExecuted++;
}

void NVRHIDevice::WaitForIdle() {
    VERIFY(m_device);
    m_device->waitForIdle();
}

// ═══════════════════════════════════════════════════
//  STATISTICS
// ═══════════════════════════════════════════════════

void NVRHIDevice::ResetStatistics() {
    m_stats = Statistics{};
}

void NVRHIDevice::LogDeviceInfo() {
    VERIFY(m_device);
    
    Msg("! [NVRHIDevice] Device Information:");
    Msg("!   Name: %s", m_device->getDeviceName().c_str());
    Msg("!   API: %s", 
        nvrhi::utils::GraphicsAPIToString(m_device->getGraphicsAPI()));
    
    const nvrhi::DeviceDesc& desc = m_device->getDesc();
    Msg("!   VRAM: %llu MB", desc.maxTextureSize / (1024 * 1024));
}

} // namespace xray::render::nvrhi_backend
```

---

### Success Criteria

- ✅ `NVRHIDevice` successfully wraps existing D3D11 device
- ✅ Can create basic resources (texture, buffer, sampler)
- ✅ Resource tracking works (no leaks detected)
- ✅ Statistics reporting accurate
- ✅ Zero crashes during initialization/shutdown cycle

### Performance Metrics

- **Initialization Overhead**: <2ms
- **Memory Per Device**: ~8KB (excluding resources)
- **Resource Creation**: Same as native D3D11 (no overhead)

---

## Milestone 0.3: First NVRHI Render

### Objective
Prove NVRHI works end-to-end by clearing the screen to a solid color.

### Implementation

```cpp
// xrRender/NVRHI/NVRHITest.cpp (continued)

void TestNVRHIClear() {
    Msg("! [NVRHI Test] Testing clear operation...");
    
    // ═══════════════════════════════════════════════════
    //  SETUP
    // ═══════════════════════════════════════════════════
    
    // Create device
    NVRHIDevice nvDevice;
    if (!nvDevice.InitializeFromD3D11(HW.pDevice, HW.pContext)) {
        Msg("! [NVRHI Test] ❌ Device initialization failed");
        return;
    }
    
    // ═══════════════════════════════════════════════════
    //  WRAP BACKBUFFER
    // ═══════════════════════════════════════════════════
    
    // Describe backbuffer
    nvrhi::TextureDesc backbufferDesc;
    backbufferDesc.width = Device.dwWidth;
    backbufferDesc.height = Device.dwHeight;
    backbufferDesc.format = nvrhi::Format::RGBA8_UNORM;
    backbufferDesc.isRenderTarget = true;
    backbufferDesc.debugName = "Backbuffer";
    backbufferDesc.initialState = nvrhi::ResourceStates::RenderTarget;
    backbufferDesc.keepInitialState = true;
    
    // Wrap X-Ray's backbuffer
    nvrhi::TextureHandle backbuffer = nvDevice.CreateTextureFromD3D11(
        HW.pBaseRT,  // X-Ray's ID3D11RenderTargetView -> underlying texture
        backbufferDesc
    );
    
    if (!backbuffer) {
        Msg("! [NVRHI Test] ❌ Failed to wrap backbuffer");
        return;
    }
    
    // ═══════════════════════════════════════════════════
    //  CLEAR OPERATION
    // ═══════════════════════════════════════════════════
    
    nvrhi::CommandListHandle cmd = nvDevice.GetDevice()->createCommandList();
    
    cmd->open();
    
    // Clear to blue
    nvrhi::Color clearColor(0.1f, 0.2f, 0.4f, 1.0f);
    cmd->clearTextureFloat(backbuffer, nvrhi::AllSubresources, clearColor);
    
    cmd->close();
    
    // Execute
    nvDevice.ExecuteCommandList(cmd);
    
    // ═══════════════════════════════════════════════════
    //  VALIDATION
    // ═══════════════════════════════════════════════════
    
    const auto& stats = nvDevice.GetStatistics();
    Msg("! [NVRHI Test] Statistics:");
    Msg("!   Textures Created: %u", stats.texturesCreated);
    Msg("!   Command Lists Executed: %u", stats.commandListsExecuted);
    
    Msg("! [NVRHI Test] ✅ Clear test complete");
}
```

**Testing Integration:**

```cpp
// Add to console command or startup test
void RunNVRHITests() {
    xray::render::nvrhi_test::TestNVRHIBasics();
    xray::render::nvrhi_test::TestNVRHIDeviceCreation();
    xray::render::nvrhi_test::TestNVRHIClear();  // This one renders!
}
```

**Toggle Between Renderers:**

```cpp
// In main render loop (dxRenderDeviceRender.cpp or similar)
bool use_nvrhi_test = false;  // Console variable

void CRender::Render() {
    if (use_nvrhi_test) {
        xray::render::nvrhi_test::TestNVRHIClear();
        return;  // Skip old renderer
    }
    
    // ... existing render code ...
}
```

---

### Success Criteria

- ✅ Screen clears to blue (RGB: 0.1, 0.2, 0.4)
- ✅ No D3D11 debug layer errors
- ✅ Can toggle between NVRHI test and old renderer
- ✅ No crashes, no memory leaks
- ✅ Statistics show 1 texture, 1 command list executed

### Performance Metrics

- **Clear Operation**: <0.1ms (same as native D3D11)
- **Command Recording**: <0.05ms
- **Memory Usage**: Backbuffer wrap = 0 bytes (references existing)

---

# 🎨 Phase 1: RenderContext Abstraction (Weeks 4-7)

## Overview

RenderContext provides a **clean, X-Ray-specific API** for rendering that:
- Hides NVRHI implementation details
- Uses familiar X-Ray patterns (handles, naming conventions)
- Provides explicit control over state and barriers
- Enables easy FrameGraph integration later

**Architecture**:
```
RenderContext (public API)
    ↓ uses
NVRHIDevice (NVRHI wrapper)
    ↓ uses
NVRHI (multi-API abstraction)
    ↓ uses
DX11/DX12/Vulkan
```

---

## Milestone 1.1: RenderContext Interface

### Objective
Define the high-level rendering API that FrameGraph will eventually use.

### Design Principles

1. **Strongly Typed Handles**: Prevent mixing texture/buffer/sampler handles
2. **Explicit State Management**: No hidden state, all transitions explicit
3. **Minimal API Surface**: Only essential operations
4. **Zero NVRHI Leakage**: No `nvrhi::` types in public interface

---

### Implementation

```cpp
// xrRender/RenderContext/RenderContext.h
#pragma once

#include "ResourceHandle.h"

namespace xray::render {

// Forward declarations
class RenderDevice;
struct PipelineState;

// ═══════════════════════════════════════════════════
//  RENDER CONTEXT (Command Recording Interface)
// ═══════════════════════════════════════════════════

class RenderContext {
public:
    explicit RenderContext(RenderDevice* device);
    ~RenderContext();
    
    // Prevent copying
    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;
    
    // ═══════════════════════════════════════════════════
    //  COMMAND RECORDING LIFECYCLE
    // ═══════════════════════════════════════════════════
    
    // Begin recording commands
    void Begin();
    
    // End recording (must call before execution)
    void End();
    
    bool IsRecording() const { return m_isRecording; }
    
    // ═══════════════════════════════════════════════════
    //  RENDER TARGETS
    // ═══════════════════════════════════════════════════
    
    // Set render targets (up to 8 + optional depth/stencil)
    void SetRenderTargets(
        TextureHandle* renderTargets, 
        u32 rtCount,
        TextureHandle depthStencil = TextureHandle{}
    );
    
    // Clear operations
    void ClearRenderTarget(
        TextureHandle rt, 
        const float color[4]
    );
    
    void ClearDepthStencil(
        TextureHandle ds,
        float depth = 1.0f,
        u8 stencil = 0,
        bool clearDepth = true,
        bool clearStencil = false
    );
    
    // ═══════════════════════════════════════════════════
    //  PIPELINE STATE
    // ═══════════════════════════════════════════════════
    
    void SetPipeline(const PipelineState* pipeline);
    
    void SetViewport(
        float x, float y, 
        float width, float height,
        float minDepth = 0.0f, 
        float maxDepth = 1.0f
    );
    
    void SetScissor(
        u32 x, u32 y,
        u32 width, u32 height
    );
    
    // ═══════════════════════════════════════════════════
    //  RESOURCE BINDING
    // ═══════════════════════════════════════════════════
    
    // Constant buffers (shader constants)
    void SetConstantBuffer(
        u32 slot, 
        BufferHandle buffer,
        u32 offset = 0
    );
    
    // Textures (shader resource views)
    void SetTexture(
        u32 slot,
        TextureHandle texture
    );
    
    // Samplers
    void SetSampler(
        u32 slot,
        SamplerHandle sampler
    );
    
    // Vertex buffers
    void SetVertexBuffer(
        u32 slot,
        BufferHandle buffer,
        u32 stride,
        u32 offset = 0
    );
    
    // Index buffer
    enum class IndexFormat {
        UInt16,
        UInt32
    };
    
    void SetIndexBuffer(
        BufferHandle buffer,
        IndexFormat format,
        u32 offset = 0
    );
    
    // ═══════════════════════════════════════════════════
    //  DRAW CALLS
    // ═══════════════════════════════════════════════════
    
    // Non-indexed draw
    void Draw(
        u32 vertexCount,
        u32 startVertex = 0
    );
    
    // Indexed draw
    void DrawIndexed(
        u32 indexCount,
        u32 startIndex = 0,
        i32 baseVertex = 0
    );
    
    // Instanced draws
    void DrawInstanced(
        u32 vertexCount,
        u32 instanceCount,
        u32 startVertex = 0,
        u32 startInstance = 0
    );
    
    void DrawIndexedInstanced(
        u32 indexCount,
        u32 instanceCount,
        u32 startIndex = 0,
        i32 baseVertex = 0,
        u32 startInstance = 0
    );
    
    // Indirect draw (GPU-driven)
    void DrawIndirect(
        BufferHandle argsBuffer,
        u32 offset = 0
    );
    
    void DrawIndexedIndirect(
        BufferHandle argsBuffer,
        u32 offset = 0
    );
    
    // ═══════════════════════════════════════════════════
    //  COMPUTE
    // ═══════════════════════════════════════════════════
    
    void Dispatch(
        u32 groupsX,
        u32 groupsY,
        u32 groupsZ
    );
    
    void DispatchIndirect(
        BufferHandle argsBuffer,
        u32 offset = 0
    );
    
    // ═══════════════════════════════════════════════════
    //  RESOURCE TRANSITIONS (Explicit Barriers)
    // ═══════════════════════════════════════════════════
    
    enum class ResourceState {
        Undefined,           // Uninitialized
        Common,              // Generic state (D3D12 specific)
        RenderTarget,        // Bound as render target
        DepthWrite,          // Bound as depth/stencil (write)
        DepthRead,           // Bound as depth/stencil (read-only)
        ShaderResource,      // Bound as SRV in shader
        UnorderedAccess,     // Bound as UAV (read/write)
        CopySource,          // Source of copy operation
        CopyDest,            // Destination of copy operation
        Present,             // Ready for presentation
    };
    
    // Transition texture state
    void TransitionTexture(
        TextureHandle resource,
        ResourceState stateBefore,
        ResourceState stateAfter
    );
    
    // Transition buffer state
    void TransitionBuffer(
        BufferHandle resource,
        ResourceState stateBefore,
        ResourceState stateAfter
    );
    
    // ═══════════════════════════════════════════════════
    //  GPU PROFILING
    // ═══════════════════════════════════════════════════
    
    void BeginTimestampQuery(u32 queryIndex);
    void EndTimestampQuery(u32 queryIndex);
    
    // ═══════════════════════════════════════════════════
    //  DEBUG MARKERS
    // ═══════════════════════════════════════════════════
    
    void BeginEvent(const char* name);
    void EndEvent();
    void SetMarker(const char* name);
    
    // ═══════════════════════════════════════════════════
    //  INTERNAL (For RenderDevice only)
    // ═══════════════════════════════════════════════════
    
    nvrhi::ICommandList* GetNativeCommandList() { 
        return m_cmdList.Get(); 
    }
    
private:
    RenderDevice* m_device;
    nvrhi::CommandListHandle m_cmdList;
    
    bool m_isRecording = false;
    
    // Track current state (for validation in debug builds)
    struct CurrentState {
        TextureHandle renderTargets[8];
        TextureHandle depthStencil;
        const PipelineState* pipeline = nullptr;
        
        // Resource state tracking (for automatic barrier validation)
        xr_map<ResourceHandle, ResourceState> textureStates;
        xr_map<ResourceHandle, ResourceState> bufferStates;
    };
    
    CurrentState m_currentState;
    
    // Validation helpers (debug only)
    void ValidateRenderTarget(TextureHandle rt);
    void ValidatePipeline();
    void ValidateResourceState(TextureHandle tex, ResourceState expectedState);
};

} // namespace xray::render
```

```cpp
// xrRender/RenderContext/ResourceHandle.h
#pragma once

namespace xray::render {

// ═══════════════════════════════════════════════════
//  OPAQUE RESOURCE HANDLES (Strongly Typed)
// ═══════════════════════════════════════════════════

constexpr u32 INVALID_INDEX = 0xFFFFFFFF;

// Base handle (not used directly)
struct ResourceHandle {
    u32 index = INVALID_INDEX;
    u32 generation = 0;  // For validation (detect stale handles)
    
    bool IsValid() const { return index != INVALID_INDEX; }
    
    bool operator==(const ResourceHandle& other) const {
        return index == other.index && generation == other.generation;
    }
    
    bool operator!=(const ResourceHandle& other) const {
        return !(*this == other);
    }
};

// Strongly typed handles (prevent mixing)
struct BufferHandle : ResourceHandle {};
struct TextureHandle : ResourceHandle {};
struct SamplerHandle : ResourceHandle {};

} // namespace xray::render

// Hash support (for xr_map)
namespace std {
    template<>
    struct hash<xray::render::ResourceHandle> {
        size_t operator()(const xray::render::ResourceHandle& h) const {
            return hash<u64>()((u64(h.generation) << 32) | u64(h.index));
        }
    };
}
```

---

### Success Criteria

- ✅ Interface compiles cleanly
- ✅ No `nvrhi::` types in public headers
- ✅ Handles are strongly typed (can't mix texture/buffer)
- ✅ API is intuitive and familiar to graphics programmers
- ✅ State tracking prepared for validation

### Design Rationale

**Why explicit barriers?**
- FrameGraph will eventually auto-insert these
- During RenderContext development, explicit = debuggable
- DX12/Vulkan require explicit barriers anyway

**Why strong handle typing?**
- Catch bugs at compile-time (can't pass buffer where texture expected)
- Zero runtime overhead (same size as u32)
- Better IDE autocomplete

---

## Milestone 1.2: Pipeline State Objects

### Objective
Create immutable pipeline state objects (PSOs) that encapsulate all fixed-function and shader state, enabling efficient state changes and caching.

### Design Principles

1. **Immutability**: PSOs created once, never modified
2. **Hash-Based Caching**: Reuse identical PSOs across frames
3. **X-Ray Integration**: Compatible with existing shader system
4. **Validation**: Debug builds catch invalid state combinations

---

### Implementation

```cpp
// xrRender/RenderContext/Shader.h
#pragma once

#include <nvrhi/nvrhi.h>

namespace xray::render {

// ═══════════════════════════════════════════════════
//  SHADER WRAPPER
// ═══════════════════════════════════════════════════

enum class ShaderStage {
    Vertex,
    Pixel,
    Geometry,
    Hull,
    Domain,
    Compute
};

class Shader {
public:
    Shader(ShaderStage stage, 
           nvrhi::ShaderHandle nvrhiShader,
           const char* debugName);
    
    ShaderStage GetStage() const { return m_stage; }
    nvrhi::IShader* GetNativeShader() const { return m_nvrhiShader.Get(); }
    const shared_str& GetDebugName() const { return m_debugName; }
    
private:
    ShaderStage m_stage;
    nvrhi::ShaderHandle m_nvrhiShader;
    shared_str m_debugName;
};

} // namespace xray::render
```

```cpp
// xrRender/RenderContext/PipelineState.h
#pragma once

#include "Shader.h"
#include <nvrhi/nvrhi.h>

namespace xray::render {

// ═══════════════════════════════════════════════════
//  VERTEX INPUT LAYOUT
// ═══════════════════════════════════════════════════

struct VertexAttribute {
    const char* semanticName;
    u32 semanticIndex = 0;
    nvrhi::Format format;
    u32 offset;
    u32 bufferIndex = 0;
    bool isInstanced = false;
};

// ═══════════════════════════════════════════════════
//  RASTERIZER STATE
// ═══════════════════════════════════════════════════

enum class CullMode {
    None,
    Front,
    Back
};

enum class FillMode {
    Solid,
    Wireframe
};

struct RasterizerState {
    CullMode cullMode = CullMode::Back;
    FillMode fillMode = FillMode::Solid;
    bool frontCounterClockwise = false;
    bool depthClipEnable = true;
    bool scissorEnable = false;
    bool multisampleEnable = false;
    bool antialiasedLineEnable = false;
    
    i32 depthBias = 0;
    float depthBiasClamp = 0.0f;
    float slopeScaledDepthBias = 0.0f;
    
    // Conservative rasterization (DX12 feature)
    bool conservativeRaster = false;
};

// ═══════════════════════════════════════════════════
//  BLEND STATE
// ═══════════════════════════════════════════════════

enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    InvSrcColor,
    SrcAlpha,
    InvSrcAlpha,
    DstColor,
    InvDstColor,
    DstAlpha,
    InvDstAlpha,
    SrcAlphaSat,
    BlendFactor,
    InvBlendFactor
};

enum class BlendOp {
    Add,
    Subtract,
    RevSubtract,
    Min,
    Max
};

enum class ColorWriteMask : u8 {
    None = 0,
    Red = 1 << 0,
    Green = 1 << 1,
    Blue = 1 << 2,
    Alpha = 1 << 3,
    All = Red | Green | Blue | Alpha
};

struct RenderTargetBlendState {
    bool blendEnable = false;
    
    BlendFactor srcBlend = BlendFactor::One;
    BlendFactor dstBlend = BlendFactor::Zero;
    BlendOp blendOp = BlendOp::Add;
    
    BlendFactor srcBlendAlpha = BlendFactor::One;
    BlendFactor dstBlendAlpha = BlendFactor::Zero;
    BlendOp blendOpAlpha = BlendOp::Add;
    
    ColorWriteMask writeMask = ColorWriteMask::All;
};

struct BlendState {
    RenderTargetBlendState renderTargets[8];
    bool alphaToCoverageEnable = false;
    bool independentBlendEnable = false;  // Different blend per RT
};

// ═══════════════════════════════════════════════════
//  DEPTH/STENCIL STATE
// ═══════════════════════════════════════════════════

enum class ComparisonFunc {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

enum class StencilOp {
    Keep,
    Zero,
    Replace,
    IncrementSaturate,
    DecrementSaturate,
    Invert,
    Increment,
    Decrement
};

struct StencilOpState {
    StencilOp failOp = StencilOp::Keep;
    StencilOp depthFailOp = StencilOp::Keep;
    StencilOp passOp = StencilOp::Keep;
    ComparisonFunc compareFunc = ComparisonFunc::Always;
};

struct DepthStencilState {
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    ComparisonFunc depthFunc = ComparisonFunc::Less;
    
    bool stencilEnable = false;
    u8 stencilReadMask = 0xFF;
    u8 stencilWriteMask = 0xFF;
    
    StencilOpState frontFace;
    StencilOpState backFace;
};

// ═══════════════════════════════════════════════════
//  PRIMITIVE TOPOLOGY
// ═══════════════════════════════════════════════════

enum class PrimitiveTopology {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    PatchList  // For tessellation
};

// ═══════════════════════════════════════════════════
//  PIPELINE STATE DESCRIPTOR
// ═══════════════════════════════════════════════════

struct PipelineStateDesc {
    // ─── Shaders ───
    Shader* vertexShader = nullptr;
    Shader* pixelShader = nullptr;
    Shader* geometryShader = nullptr;
    Shader* hullShader = nullptr;     // Tessellation control
    Shader* domainShader = nullptr;   // Tessellation evaluation
    
    // ─── Vertex Input ───
    xr_vector<VertexAttribute> vertexAttributes;
    
    // ─── Fixed Function State ───
    RasterizerState rasterizerState;
    BlendState blendState;
    DepthStencilState depthStencilState;
    
    // ─── Primitive Topology ───
    PrimitiveTopology primitiveTopology = PrimitiveTopology::TriangleList;
    u32 patchControlPoints = 0;  // For tessellation
    
    // ─── Render Target Formats ───
    nvrhi::Format renderTargetFormats[8] = { nvrhi::Format::UNKNOWN };
    nvrhi::Format depthStencilFormat = nvrhi::Format::UNKNOWN;
    u32 renderTargetCount = 1;
    
    // ─── Multisampling ───
    u32 sampleCount = 1;
    u32 sampleQuality = 0;
    
    // ─── Debug ───
    shared_str debugName;
    
    // ─── Hash for Caching ───
    u64 ComputeHash() const;
};

// ═══════════════════════════════════════════════════
//  COMPILED PIPELINE STATE (Immutable)
// ═══════════════════════════════════════════════════

class PipelineState {
public:
    PipelineState(const PipelineStateDesc& desc,
                  nvrhi::GraphicsPipelineHandle nvrhiPipeline);
    
    const PipelineStateDesc& GetDesc() const { return m_desc; }
    nvrhi::IGraphicsPipeline* GetNativePipeline() const { 
        return m_nvrhiPipeline.Get(); 
    }
    
    u64 GetHash() const { return m_hash; }
    
private:
    PipelineStateDesc m_desc;
    nvrhi::GraphicsPipelineHandle m_nvrhiPipeline;
    u64 m_hash;
};

// ═══════════════════════════════════════════════════
//  PIPELINE STATE CACHE
// ═══════════════════════════════════════════════════

class PipelineStateCache {
public:
    PipelineStateCache(class RenderDevice* device);
    ~PipelineStateCache();
    
    // Get or create PSO (thread-safe)
    PipelineState* GetOrCreate(const PipelineStateDesc& desc);
    
    // Statistics
    struct Stats {
        u32 psoCount = 0;
        u32 cacheHits = 0;
        u32 cacheMisses = 0;
        
        float GetHitRate() const {
            u32 total = cacheHits + cacheMisses;
            return total > 0 ? (float)cacheHits / total : 0.0f;
        }
    };
    
    const Stats& GetStats() const { return m_stats; }
    void ResetStats();
    
    // Clear cache (e.g. on shader reload)
    void Clear();
    
private:
    RenderDevice* m_device;
    
    // Cache: hash -> PSO
    xr_map<u64, xr_unique_ptr<PipelineState>> m_cache;
    
    // Thread safety
    mutable Lock m_cacheLock;
    
    // Statistics
    Stats m_stats;
    
    // Create new PSO
    PipelineState* CreatePipelineState(const PipelineStateDesc& desc);
};

} // namespace xray::render
```

```cpp
// xrRender/RenderContext/PipelineState.cpp
#include "stdafx.h"
#include "PipelineState.h"
#include "RenderDevice.h"

namespace xray::render {

// ═══════════════════════════════════════════════════
//  DESCRIPTOR HASH COMPUTATION
// ═══════════════════════════════════════════════════

u64 PipelineStateDesc::ComputeHash() const {
    // Use CRC32 or FNV-1a hash
    u64 hash = 0xcbf29ce484222325;  // FNV-1a offset basis
    
    // Hash shader pointers
    hash ^= (u64)vertexShader;
    hash *= 0x100000001b3;  // FNV-1a prime
    
    hash ^= (u64)pixelShader;
    hash *= 0x100000001b3;
    
    hash ^= (u64)geometryShader;
    hash *= 0x100000001b3;
    
    // Hash state structures (use memcpy or field-by-field)
    hash ^= *(u32*)&rasterizerState.cullMode;
    hash *= 0x100000001b3;
    
    hash ^= *(u32*)&blendState.renderTargets[0].blendEnable;
    hash *= 0x100000001b3;
    
    hash ^= *(u32*)&depthStencilState.depthTestEnable;
    hash *= 0x100000001b3;
    
    // Hash vertex attributes
    for (const auto& attr : vertexAttributes) {
        hash ^= crc32(attr.semanticName, xr_strlen(attr.semanticName));
        hash *= 0x100000001b3;
        hash ^= (u32)attr.format;
        hash *= 0x100000001b3;
    }
    
    // Hash render target formats
    for (u32 i = 0; i < renderTargetCount; i++) {
        hash ^= (u32)renderTargetFormats[i];
        hash *= 0x100000001b3;
    }
    
    return hash;
}

// ═══════════════════════════════════════════════════
//  PIPELINE STATE
// ═══════════════════════════════════════════════════

PipelineState::PipelineState(
    const PipelineStateDesc& desc,
    nvrhi::GraphicsPipelineHandle nvrhiPipeline)
    : m_desc(desc)
    , m_nvrhiPipeline(nvrhiPipeline)
    , m_hash(desc.ComputeHash())
{
}

// ═══════════════════════════════════════════════════
//  PIPELINE STATE CACHE
// ═══════════════════════════════════════════════════

PipelineStateCache::PipelineStateCache(RenderDevice* device)
    : m_device(device)
{
    Msg("! [PipelineStateCache] Created");
}

PipelineStateCache::~PipelineStateCache() {
    Msg("! [PipelineStateCache] Statistics:");
    Msg("!   Total PSOs: %u", m_stats.psoCount);
    Msg("!   Cache Hits: %u", m_stats.cacheHits);
    Msg("!   Cache Misses: %u", m_stats.cacheMisses);
    Msg("!   Hit Rate: %.1f%%", m_stats.GetHitRate() * 100.0f);
}

PipelineState* PipelineStateCache::GetOrCreate(
    const PipelineStateDesc& desc) {
    
    u64 hash = desc.ComputeHash();
    
    // Try cache first (read lock)
    {
        Lock lock(m_cacheLock);
        auto it = m_cache.find(hash);
        if (it != m_cache.end()) {
            m_stats.cacheHits++;
            return it->second.get();
        }
    }
    
    // Cache miss - create new PSO (write lock)
    {
        Lock lock(m_cacheLock);
        
        // Double-check (another thread might have created it)
        auto it = m_cache.find(hash);
        if (it != m_cache.end()) {
            m_stats.cacheHits++;
            return it->second.get();
        }
        
        // Create PSO
        PipelineState* pso = CreatePipelineState(desc);
        
        if (pso) {
            m_cache[hash] = xr_unique_ptr<PipelineState>(pso);
            m_stats.psoCount++;
            m_stats.cacheMisses++;
            
            Msg("! [PipelineStateCache] Created PSO: %s (hash: 0x%llx)",
                desc.debugName.c_str(), hash);
            
            return pso;
        }
        
        return nullptr;
    }
}

PipelineState* PipelineStateCache::CreatePipelineState(
    const PipelineStateDesc& desc) {
    
    // Convert X-Ray desc -> NVRHI desc
    nvrhi::GraphicsPipelineDesc nvrhiDesc;
    
    // ─── Shaders ───
    if (desc.vertexShader)
        nvrhiDesc.VS = desc.vertexShader->GetNativeShader();
    if (desc.pixelShader)
        nvrhiDesc.PS = desc.pixelShader->GetNativeShader();
    if (desc.geometryShader)
        nvrhiDesc.GS = desc.geometryShader->GetNativeShader();
    if (desc.hullShader)
        nvrhiDesc.HS = desc.hullShader->GetNativeShader();
    if (desc.domainShader)
        nvrhiDesc.DS = desc.domainShader->GetNativeShader();
    
    // ─── Vertex Input Layout ───
    for (const auto& attr : desc.vertexAttributes) {
        nvrhi::VertexAttributeDesc nvrhiAttr;
        nvrhiAttr.name = attr.semanticName;
        nvrhiAttr.format = attr.format;
        nvrhiAttr.offset = attr.offset;
        nvrhiAttr.bufferIndex = attr.bufferIndex;
        nvrhiAttr.isInstanced = attr.isInstanced;
        nvrhiDesc.inputLayout.push_back(nvrhiAttr);
    }
    
    // ─── Rasterizer State ───
    auto& rs = nvrhiDesc.renderState.rasterState;
    rs.cullMode = ConvertCullMode(desc.rasterizerState.cullMode);
    rs.fillMode = ConvertFillMode(desc.rasterizerState.fillMode);
    rs.frontCounterClockwise = desc.rasterizerState.frontCounterClockwise;
    rs.depthClipEnable = desc.rasterizerState.depthClipEnable;
    rs.scissorEnable = desc.rasterizerState.scissorEnable;
    rs.depthBias = desc.rasterizerState.depthBias;
    rs.depthBiasClamp = desc.rasterizerState.depthBiasClamp;
    rs.slopeScaledDepthBias = desc.rasterizerState.slopeScaledDepthBias;
    
    // ─── Blend State ───
    auto& bs = nvrhiDesc.renderState.blendState;
    bs.alphaToCoverageEnable = desc.blendState.alphaToCoverageEnable;
    
    for (u32 i = 0; i < desc.renderTargetCount; i++) {
        const auto& srcRT = desc.blendState.renderTargets[i];
        auto& dstRT = bs.targets[i];
        
        dstRT.blendEnable = srcRT.blendEnable;
        dstRT.srcBlend = ConvertBlendFactor(srcRT.srcBlend);
        dstRT.dstBlend = ConvertBlendFactor(srcRT.dstBlend);
        dstRT.blendOp = ConvertBlendOp(srcRT.blendOp);
        dstRT.srcBlendAlpha = ConvertBlendFactor(srcRT.srcBlendAlpha);
        dstRT.dstBlendAlpha = ConvertBlendFactor(srcRT.dstBlendAlpha);
        dstRT.blendOpAlpha = ConvertBlendOp(srcRT.blendOpAlpha);
        dstRT.colorWriteMask = (u8)srcRT.writeMask;
    }
    
    // ─── Depth/Stencil State ───
    auto& ds = nvrhiDesc.renderState.depthStencilState;
    ds.depthTestEnable = desc.depthStencilState.depthTestEnable;
    ds.depthWriteEnable = desc.depthStencilState.depthWriteEnable;
    ds.depthFunc = ConvertComparisonFunc(desc.depthStencilState.depthFunc);
    ds.stencilEnable = desc.depthStencilState.stencilEnable;
    ds.stencilReadMask = desc.depthStencilState.stencilReadMask;
    ds.stencilWriteMask = desc.depthStencilState.stencilWriteMask;
    
    // ─── Primitive Topology ───
    nvrhiDesc.primType = ConvertPrimitiveTopology(desc.primitiveTopology);
    
    // ─── Create framebuffer info (needed for PSO) ───
    nvrhi::FramebufferDesc fbDesc;
    for (u32 i = 0; i < desc.renderTargetCount; i++) {
        fbDesc.colorFormats[i] = desc.renderTargetFormats[i];
    }
    fbDesc.depthFormat = desc.depthStencilFormat;
    
    nvrhi::FramebufferInfoEx fbInfo;
    fbInfo.width = 1920;  // Dummy values (not used for PSO creation)
    fbInfo.height = 1080;
    
    // ─── Create NVRHI pipeline ───
    nvrhi::GraphicsPipelineHandle nvrhiPipeline = 
        m_device->GetNativeDevice()->createGraphicsPipeline(nvrhiDesc, fbInfo);
    
    if (!nvrhiPipeline) {
        Msg("! [PipelineStateCache] ❌ Failed to create PSO: %s",
            desc.debugName.c_str());
        return nullptr;
    }
    
    // ─── Wrap in X-Ray PSO ───
    return new PipelineState(desc, nvrhiPipeline);
}

void PipelineStateCache::Clear() {
    Lock lock(m_cacheLock);
    m_cache.clear();
    m_stats.psoCount = 0;
    Msg("! [PipelineStateCache] Cache cleared");
}

void PipelineStateCache::ResetStats() {
    m_stats.cacheHits = 0;
    m_stats.cacheMisses = 0;
}

} // namespace xray::render
```

---

### Success Criteria

- ✅ PSO descriptor is hashable and comparable
- ✅ Cache successfully deduplicates identical PSOs
- ✅ Cache hit rate >90% after warmup (typical scene)
- ✅ PSO creation <5ms (NVRHI handles slow D3D11 compilation)
- ✅ Thread-safe for multi-threaded PSO creation

### Performance Metrics

- **Hash Computation**: <0.01ms per descriptor
- **Cache Lookup**: <0.001ms (hash table)
- **PSO Creation**: 1-5ms (D3D11 driver compilation)
- **Memory Per PSO**: ~2KB (descriptor + NVRHI handle)

### Design Rationale

**Why immutable PSOs?**
- Modern APIs (DX12/Vulkan) require this
- Enables aggressive driver optimization
- Cache-friendly (can reuse across frames)

**Why hash-based caching?**
- Fast lookups (O(1) average case)
- Automatic deduplication
- Works across shader reloads

---

## Milestone 1.3: Resource Management

### Objective
Implement RenderDevice resource management system with generational handles, validation, and lifecycle tracking.

### Design Principles

1. **Generational Handles**: Detect use-after-free automatically
2. **Sparse Arrays**: Efficient handle->resource lookup (O(1))
3. **Resource Pools**: Minimize allocations, enable reuse
4. **Debug Validation**: Extensive checks in debug builds, zero cost in release

---

### Implementation

```cpp
// xrRender/RenderContext/RenderDevice.h
#pragma once

#include "ResourceHandle.h"
#include "PipelineState.h"
#include <nvrhi/nvrhi.h>

namespace xray::render {

class RenderContext;

// ═══════════════════════════════════════════════════
//  RENDER DEVICE (Resource Factory + Manager)
// ═══════════════════════════════════════════════════

class RenderDevice {
public:
    RenderDevice();
    ~RenderDevice();
    
    // ═══════════════════════════════════════════════════
    //  INITIALIZATION
    // ═══════════════════════════════════════════════════
    
    bool InitializeD3D11(ID3D11Device* device, ID3D11DeviceContext* context);
    bool InitializeD3D12();  // Future
    bool InitializeVulkan(); // Future
    
    void Shutdown();
    
    // ═══════════════════════════════════════════════════
    //  TEXTURE CREATION
    // ═══════════════════════════════════════════════════
    
    struct TextureDesc {
        u32 width = 0;
        u32 height = 0;
        u32 depth = 1;        // For 3D textures
        u32 arraySize = 1;    // For texture arrays/cubemaps
        u32 mipLevels = 1;
        
        nvrhi::Format format = nvrhi::Format::RGBA8_UNORM;
        
        enum Dimension {
            Texture1D,
            Texture2D,
            Texture3D,
            TextureCube
        };
        Dimension dimension = Texture2D;
        
        // Usage flags
        bool isRenderTarget = false;
        bool isDepthStencil = false;
        bool isUAV = false;           // Unordered access (read/write)
        bool generateMips = false;
        
        // Initial state
        RenderContext::ResourceState initialState = 
            RenderContext::ResourceState::Undefined;
        
        shared_str debugName;
    };
    
    TextureHandle CreateTexture(
        const TextureDesc& desc,
        const void* initialData = nullptr);
    
    // Wrap existing D3D11 texture (for backbuffer, etc.)
    TextureHandle CreateTextureFromD3D11(
        ID3D11Resource* d3d11Texture,
        const TextureDesc& desc);
    
    void DestroyTexture(TextureHandle handle);
    
    // Access
    nvrhi::ITexture* GetNativeTexture(TextureHandle handle);
    const TextureDesc& GetTextureDesc(TextureHandle handle);
    bool IsTextureValid(TextureHandle handle);
    
    // ═══════════════════════════════════════════════════
    //  BUFFER CREATION
    // ═══════════════════════════════════════════════════
    
    struct BufferDesc {
        u64 byteSize = 0;
        u32 structStride = 0;  // For structured buffers (0 = not structured)
        
        // Usage flags
        bool isConstantBuffer = false;
        bool isVertexBuffer = false;
        bool isIndexBuffer = false;
        bool isDrawIndirectArgs = false;
        bool isUAV = false;
        
        // CPU access
        bool cpuRead = false;
        bool cpuWrite = false;
        
        // Update frequency hint
        bool isVolatile = false;  // Updated every frame
        
        shared_str debugName;
    };
    
    BufferHandle CreateBuffer(
        const BufferDesc& desc,
        const void* initialData = nullptr);
    
    void DestroyBuffer(BufferHandle handle);
    
    // Update buffer data (use only for dynamic buffers)
    void UpdateBuffer(
        BufferHandle handle,
        const void* data,
        u64 size,
        u64 offset = 0);
    
    // Access
    nvrhi::IBuffer* GetNativeBuffer(BufferHandle handle);
    const BufferDesc& GetBufferDesc(BufferHandle handle);
    bool IsBufferValid(BufferHandle handle);
    
    // ═══════════════════════════════════════════════════
    //  SAMPLER CREATION
    // ═══════════════════════════════════════════════════
    
    struct SamplerDesc {
        enum class AddressMode {
            Wrap,
            Mirror,
            Clamp,
            Border,
            MirrorOnce
        };
        
        AddressMode addressU = AddressMode::Wrap;
        AddressMode addressV = AddressMode::Wrap;
        AddressMode addressW = AddressMode::Wrap;
        
        enum class Filter {
            Point,
            Linear,
            Anisotropic
        };
        
        Filter minFilter = Filter::Linear;
        Filter magFilter = Filter::Linear;
        Filter mipFilter = Filter::Linear;
        
        bool anisotropyEnable = false;
        u32 maxAnisotropy = 1;
        
        float mipLodBias = 0.0f;
        float minLod = -FLT_MAX;
        float maxLod = FLT_MAX;
        
        // Comparison sampling (for shadow maps)
        bool comparisonEnable = false;
        ComparisonFunc comparisonFunc = ComparisonFunc::Never;
        
        // Border color (if addressMode = Border)
        float borderColor[4] = {0, 0, 0, 0};
        
        shared_str debugName;
    };
    
    SamplerHandle CreateSampler(const SamplerDesc& desc);
    void DestroySampler(SamplerHandle handle);
    
    nvrhi::ISampler* GetNativeSampler(SamplerHandle handle);
    const SamplerDesc& GetSamplerDesc(SamplerHandle handle);
    bool IsSamplerValid(SamplerHandle handle);
    
    // ═══════════════════════════════════════════════════
    //  CONTEXT CREATION
    // ═══════════════════════════════════════════════════
    
    RenderContext* CreateContext();
    void DestroyContext(RenderContext* context);
    
    // Execute a context's recorded commands
    void ExecuteContext(RenderContext* context);
    
    // ═══════════════════════════════════════════════════
    //  PIPELINE CACHE ACCESS
    // ═══════════════════════════════════════════════════
    
    PipelineStateCache* GetPipelineCache() { return m_pipelineCache.get(); }
    
    // ═══════════════════════════════════════════════════
    //  DEVICE ACCESS (for low-level code)
    // ═══════════════════════════════════════════════════
    
    nvrhi::IDevice* GetNativeDevice() const { 
        return m_nvrhiDevice->GetDevice(); 
    }
    
    // ═══════════════════════════════════════════════════
    //  STATISTICS
    // ═══════════════════════════════════════════════════
    
    struct Statistics {
        u32 texturesAlive = 0;
        u32 buffersAlive = 0;
        u32 samplersAlive = 0;
        u32 contextsAlive = 0;
        
        u64 textureMemory = 0;   // Bytes
        u64 bufferMemory = 0;    // Bytes
        
        u32 texturesCreated = 0;
        u32 buffersCreated = 0;
        u32 samplersCreated = 0;
    };
    
    const Statistics& GetStatistics() const { return m_stats; }
    void ResetStatistics();
    
private:
    // NVRHI device wrapper
    xr_unique_ptr<nvrhi_backend::NVRHIDevice> m_nvrhiDevice;
    
    // Pipeline state cache
    xr_unique_ptr<PipelineStateCache> m_pipelineCache;
    
    // ═══════════════════════════════════════════════════
    //  RESOURCE STORAGE (Generational Sparse Arrays)
    // ═══════════════════════════════════════════════════
    
    struct TextureInfo {
        nvrhi::TextureHandle nvrhiHandle;
        TextureDesc desc;
        u32 generation;
        bool isAlive;
    };
    
    struct BufferInfo {
        nvrhi::BufferHandle nvrhiHandle;
        BufferDesc desc;
        u32 generation;
        bool isAlive;
    };
    
    struct SamplerInfo {
        nvrhi::SamplerHandle nvrhiHandle;
        SamplerDesc desc;
        u32 generation;
        bool isAlive;
    };
    
    // Sparse arrays (index = handle.index)
    xr_vector<TextureInfo> m_textures;
    xr_vector<BufferInfo> m_buffers;
    xr_vector<SamplerInfo> m_samplers;
    
    // Free lists (for handle reuse)
    xr_vector<u32> m_freeTextureSlots;
    xr_vector<u32> m_freeBufferSlots;
    xr_vector<u32> m_freeSamplerSlots;
    
    // Contexts
    xr_vector<RenderContext*> m_contexts;
    
    // Statistics
    Statistics m_stats;
    
    // ═══════════════════════════════════════════════════
    //  INTERNAL HELPERS
    // ═══════════════════════════════════════════════════
    
    // Allocate handle
    TextureHandle AllocateTextureHandle();
    BufferHandle AllocateBufferHandle();
    SamplerHandle AllocateSamplerHandle();
    
    // Validate handle
    bool ValidateTextureHandle(TextureHandle handle) const;
    bool ValidateBufferHandle(BufferHandle handle) const;
    bool ValidateSamplerHandle(SamplerHandle handle) const;
};

} // namespace xray::render
```

```cpp
// xrRender/RenderContext/RenderDevice.cpp
#include "stdafx.h"
#include "RenderDevice.h"
#include "RenderContext.h"
#include "../NVRHI/NVRHIDevice.h"

namespace xray::render {

RenderDevice::RenderDevice() {
    Msg("! [RenderDevice] Constructor");
    
    // Pre-allocate some capacity
    m_textures.reserve(1024);
    m_buffers.reserve(2048);
    m_samplers.reserve(64);
}

RenderDevice::~RenderDevice() {
    Shutdown();
}

bool RenderDevice::InitializeD3D11(ID3D11Device* device, 
                                    ID3D11DeviceContext* context) {
    Msg("! [RenderDevice] Initializing...");
    
    // Create NVRHI device
    m_nvrhiDevice = xr_make_unique<nvrhi_backend::NVRHIDevice>();
    if (!m_nvrhiDevice->InitializeFromD3D11(device, context)) {
        Msg("! [RenderDevice] ❌ NVRHI initialization failed");
        return false;
    }
    
    // Create pipeline cache
    m_pipelineCache = xr_make_unique<PipelineStateCache>(this);
    
    Msg("! [RenderDevice] ✅ Initialization complete");
    return true;
}

void RenderDevice::Shutdown() {
    if (!m_nvrhiDevice) return;
    
    Msg("! [RenderDevice] Shutting down...");
    
    // Destroy all contexts
    for (RenderContext* ctx : m_contexts) {
        xr_delete(ctx);
    }
    m_contexts.clear();
    
    // Validate no resource leaks
    if (m_stats.texturesAlive > 0) {
        Msg("! [RenderDevice] ⚠️ Warning: %u textures still alive",
            m_stats.texturesAlive);
    }
    if (m_stats.buffersAlive > 0) {
        Msg("! [RenderDevice] ⚠️ Warning: %u buffers still alive",
            m_stats.buffersAlive);
    }
    
    // Clear resources
    m_textures.clear();
    m_buffers.clear();
    m_samplers.clear();
    
    m_pipelineCache = nullptr;
    m_nvrhiDevice = nullptr;
    
    Msg("! [RenderDevice] Shutdown complete");
}

// ═══════════════════════════════════════════════════
//  TEXTURE MANAGEMENT
// ═══════════════════════════════════════════════════

TextureHandle RenderDevice::CreateTexture(
    const TextureDesc& desc,
    const void* initialData) {
    
    VERIFY(m_nvrhiDevice);
    
    // Convert to NVRHI descriptor
    nvrhi::TextureDesc nvrhiDesc;
    nvrhiDesc.width = desc.width;
    nvrhiDesc.height = desc.height;
    nvrhiDesc.depth = desc.depth;
    nvrhiDesc.arraySize = desc.arraySize;
    nvrhiDesc.mipLevels = desc.mipLevels;
    nvrhiDesc.format = desc.format;
    nvrhiDesc.debugName = desc.debugName.c_str();
    
    // Set dimension
    switch (desc.dimension) {
        case TextureDesc::Texture1D:
            nvrhiDesc.dimension = nvrhi::TextureDimension::Texture1D;
            break;
        case TextureDesc::Texture2D:
            nvrhiDesc.dimension = nvrhi::TextureDimension::Texture2D;
            break;
        case TextureDesc::Texture3D:
            nvrhiDesc.dimension = nvrhi::TextureDimension::Texture3D;
            break;
        case TextureDesc::TextureCube:
            nvrhiDesc.dimension = nvrhi::TextureDimension::TextureCube;
            nvrhiDesc.arraySize = 6;
            break;
    }
    
    // Set usage flags
    nvrhiDesc.isRenderTarget = desc.isRenderTarget;
    nvrhiDesc.isUAV = desc.isUAV;
    
    // Create NVRHI texture
    nvrhi::TextureHandle nvrhiTexture = 
        m_nvrhiDevice->CreateTexture(nvrhiDesc);
    
    if (!nvrhiTexture) {
        Msg("! [RenderDevice] ❌ Failed to create texture: %s",
            desc.debugName.c_str());
        return TextureHandle{};
    }
    
    // Upload initial data if provided
    if (initialData) {
        // TODO: Use staging buffer and copy command
    }
    
    // Allocate handle
    TextureHandle handle = AllocateTextureHandle();
    
    // Store texture info
    TextureInfo& info = m_textures[handle.index];
    info.nvrhiHandle = nvrhiTexture;
    info.desc = desc;
    info.generation = handle.generation;
    info.isAlive = true;
    
    // Update statistics
    m_stats.texturesAlive++;
    m_stats.texturesCreated++;
    m_stats.textureMemory += nvrhiTexture->getDesc().getMemorySize();
    
    Msg("! [RenderDevice] Created texture: %s (handle: %u.%u)",
        desc.debugName.c_str(), handle.index, handle.generation);
    
    return handle;
}

TextureHandle RenderDevice::CreateTextureFromD3D11(
    ID3D11Resource* d3d11Texture,
    const TextureDesc& desc) {
    
    VERIFY(m_nvrhiDevice);
    VERIFY(d3d11Texture);
    
    // Convert descriptor
    nvrhi::TextureDesc nvrhiDesc;
    nvrhiDesc.width = desc.width;
    nvrhiDesc.height = desc.height;
    nvrhiDesc.format = desc.format;
    nvrhiDesc.isRenderTarget = desc.isRenderTarget;
    nvrhiDesc.debugName = desc.debugName.c_str();
    
    // Wrap D3D11 texture
    nvrhi::TextureHandle nvrhiTexture = 
        m_nvrhiDevice->CreateTextureFromD3D11(d3d11Texture, nvrhiDesc);
    
    if (!nvrhiTexture) {
        Msg("! [RenderDevice] ❌ Failed to wrap D3D11 texture");
        return TextureHandle{};
    }
    
    // Allocate handle
    TextureHandle handle = AllocateTextureHandle();
    
    // Store info
    TextureInfo& info = m_textures[handle.index];
    info.nvrhiHandle = nvrhiTexture;
    info.desc = desc;
    info.generation = handle.generation;
    info.isAlive = true;
    
    m_stats.texturesAlive++;
    m_stats.texturesCreated++;
    
    return handle;
}

void RenderDevice::DestroyTexture(TextureHandle handle) {
    if (!ValidateTextureHandle(handle)) {
        Msg("! [RenderDevice] ⚠️ Attempted to destroy invalid texture handle");
        return;
    }
    
    TextureInfo& info = m_textures[handle.index];
    
    // Update stats
    m_stats.texturesAlive--;
    m_stats.textureMemory -= info.nvrhiHandle->getDesc().getMemorySize();
    
    // Mark as dead
    info.isAlive = false;
    info.nvrhiHandle = nullptr;
    
    // Add to free list
    m_freeTextureSlots.push_back(handle.index);
    
    Msg("! [RenderDevice] Destroyed texture: %s",
        info.desc.debugName.c_str());
}

nvrhi::ITexture* RenderDevice::GetNativeTexture(TextureHandle handle) {
    if (!ValidateTextureHandle(handle))
        return nullptr;
    
    return m_textures[handle.index].nvrhiHandle.Get();
}

const RenderDevice::TextureDesc& RenderDevice::GetTextureDesc(
    TextureHandle handle) {
    
    VERIFY(ValidateTextureHandle(handle));
    return m_textures[handle.index].desc;
}

bool RenderDevice::IsTextureValid(TextureHandle handle) {
    return ValidateTextureHandle(handle);
}

// ═══════════════════════════════════════════════════
//  BUFFER MANAGEMENT (Similar pattern)
// ═══════════════════════════════════════════════════

BufferHandle RenderDevice::CreateBuffer(
    const BufferDesc& desc,
    const void* initialData) {
    
    VERIFY(m_nvrhiDevice);
    
    // Convert to NVRHI descriptor
    nvrhi::BufferDesc nvrhiDesc;
    nvrhiDesc.byteSize = desc.byteSize;
    nvrhiDesc.structStride = desc.structStride;
    nvrhiDesc.debugName = desc.debugName.c_str();
    
    // Set usage flags
    nvrhiDesc.isConstantBuffer = desc.isConstantBuffer;
    nvrhiDesc.isVertexBuffer = desc.isVertexBuffer;
    nvrhiDesc.isIndexBuffer = desc.isIndexBuffer;
    nvrhiDesc.isDrawIndirectArgs = desc.isDrawIndirectArgs;
    nvrhiDesc.canHaveUAVs = desc.isUAV;
    
    // CPU access
    nvrhiDesc.cpuAccess = nvrhi::CpuAccessMode::None;
    if (desc.cpuRead && desc.cpuWrite) {
        nvrhiDesc.cpuAccess = nvrhi::CpuAccessMode::ReadWrite;
    } else if (desc.cpuRead) {
        nvrhiDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
    } else if (desc.cpuWrite) {
        nvrhiDesc.cpuAccess = nvrhi::CpuAccessMode::Write;
    }
    
    // Create buffer
    nvrhi::BufferHandle nvrhiBuffer = 
        m_nvrhiDevice->CreateBuffer(nvrhiDesc, initialData);
    
    if (!nvrhiBuffer) {
        Msg("! [RenderDevice] ❌ Failed to create buffer: %s",
            desc.debugName.c_str());
        return BufferHandle{};
    }
    
    // Allocate handle
    BufferHandle handle = AllocateBufferHandle();
    
    // Store info
    BufferInfo& info = m_buffers[handle.index];
    info.nvrhiHandle = nvrhiBuffer;
    info.desc = desc;
    info.generation = handle.generation;
    info.isAlive = true;
    
    // Update stats
    m_stats.buffersAlive++;
    m_stats.buffersCreated++;
    m_stats.bufferMemory += desc.byteSize;
    
    return handle;
}

void RenderDevice::DestroyBuffer(BufferHandle handle) {
    if (!ValidateBufferHandle(handle)) return;
    
    BufferInfo& info = m_buffers[handle.index];
    
    m_stats.buffersAlive--;
    m_stats.bufferMemory -= info.desc.byteSize;
    
    info.isAlive = false;
    info.nvrhiHandle = nullptr;
    
    m_freeBufferSlots.push_back(handle.index);
}

void RenderDevice::UpdateBuffer(
    BufferHandle handle,
    const void* data,
    u64 size,
    u64 offset) {
    
    if (!ValidateBufferHandle(handle)) return;
    
    nvrhi::IBuffer* buffer = m_buffers[handle.index].nvrhiHandle.Get();
    
    // Use NVRHI's writeBuffer (will internally use staging if needed)
    nvrhi::CommandListHandle cmd = m_nvrhiDevice->GetDevice()->createCommandList();
    cmd->open();
    cmd->writeBuffer(buffer, data, size, offset);
    cmd->close();
    
    m_nvrhiDevice->ExecuteCommandList(cmd);
}

// ═══════════════════════════════════════════════════
//  HANDLE ALLOCATION (Generational Indices)
// ═══════════════════════════════════════════════════

TextureHandle RenderDevice::AllocateTextureHandle() {
    TextureHandle handle;
    
    if (!m_freeTextureSlots.empty()) {
        // Reuse freed slot
        handle.index = m_freeTextureSlots.back();
        m_freeTextureSlots.pop_back();
        
        // Increment generation to invalidate old handles
        handle.generation = m_textures[handle.index].generation + 1;
    } else {
        // Allocate new slot
        handle.index = (u32)m_textures.size();
        handle.generation = 0;
        
        m_textures.resize(handle.index + 1);
    }
    
    return handle;
}

BufferHandle RenderDevice::AllocateBufferHandle() {
    BufferHandle handle;
    
    if (!m_freeBufferSlots.empty()) {
        handle.index = m_freeBufferSlots.back();
        m_freeBufferSlots.pop_back();
        handle.generation = m_buffers[handle.index].generation + 1;
    } else {
        handle.index = (u32)m_buffers.size();
        handle.generation = 0;
        m_buffers.resize(handle.index + 1);
    }
    
    return handle;
}

SamplerHandle RenderDevice::AllocateSamplerHandle() {
    SamplerHandle handle;
    
    if (!m_freeSamplerSlots.empty()) {
        handle.index = m_freeSamplerSlots.back();
        m_freeSamplerSlots.pop_back();
        handle.generation = m_samplers[handle.index].generation + 1;
    } else {
        handle.index = (u32)m_samplers.size();
        handle.generation = 0;
        m_samplers.resize(handle.index + 1);
    }
    
    return handle;
}

// ═══════════════════════════════════════════════════
//  HANDLE VALIDATION
// ═══════════════════════════════════════════════════

bool RenderDevice::ValidateTextureHandle(TextureHandle handle) const {
    if (!handle.IsValid()) return false;
    if (handle.index >= m_textures.size()) return false;
    
    const TextureInfo& info = m_textures[handle.index];
    if (!info.isAlive) return false;
    if (info.generation != handle.generation) return false;  // Stale handle!
    
    return true;
}

bool RenderDevice::ValidateBufferHandle(BufferHandle handle) const {
    if (!handle.IsValid()) return false;
    if (handle.index >= m_buffers.size()) return false;
    
    const BufferInfo& info = m_buffers[handle.index];
    if (!info.isAlive) return false;
    if (info.generation != handle.generation) return false;
    
    return true;
}

bool RenderDevice::ValidateSamplerHandle(SamplerHandle handle) const {
    if (!handle.IsValid()) return false;
    if (handle.index >= m_samplers.size()) return false;
    
    const SamplerInfo& info = m_samplers[handle.index];
    if (!info.isAlive) return false;
    if (info.generation != handle.generation) return false;
    
    return true;
}

// ═══════════════════════════════════════════════════
//  CONTEXT MANAGEMENT
// ═══════════════════════════════════════════════════

RenderContext* RenderDevice::CreateContext() {
    RenderContext* ctx = xr_new<RenderContext>(this);
    m_contexts.push_back(ctx);
    m_stats.contextsAlive++;
    return ctx;
}

void RenderDevice::DestroyContext(RenderContext* context) {
    auto it = std::find(m_contexts.begin(), m_contexts.end(), context);
    if (it != m_contexts.end()) {
        m_contexts.erase(it);
        m_stats.contextsAlive--;
    }
    xr_delete(context);
}

void RenderDevice::ExecuteContext(RenderContext* context) {
    VERIFY(context);
    VERIFY(!context->IsRecording());
    
    m_nvrhiDevice->ExecuteCommandList(context->GetNativeCommandList());
}

// ═══════════════════════════════════════════════════
//  STATISTICS
// ═══════════════════════════════════════════════════

void RenderDevice::ResetStatistics() {
    m_stats.texturesCreated = 0;
    m_stats.buffersCreated = 0;
    m_stats.samplersCreated = 0;
}

} // namespace xray::render
```

---

### Success Criteria

- ✅ Generational handles catch use-after-free in debug builds
- ✅ Resource creation/destruction has no leaks (validated on shutdown)
- ✅ Handle lookup is O(1) (array access)
- ✅ Can create 10,000+ resources without performance degradation
- ✅ Statistics accurately track resource counts and memory

### Performance Metrics

- **Handle Allocation**: <0.001ms (array resize amortized)
- **Handle Validation**: <0.0001ms (3 comparisons)
- **Resource Lookup**: <0.0001ms (array access + pointer dereference)
- **Memory Overhead**: ~32 bytes per resource (handle info + metadata)

### Design Rationale

**Why generational handles?**
```cpp
TextureHandle handle = device->CreateTexture(...);
device->DestroyTexture(handle);

// Later (bug: using stale handle)
device->GetNativeTexture(handle);  // ❌ Returns nullptr! Generation mismatch
```

Without generations, this would access freed memory. With generations, we catch the bug immediately.

**Why sparse arrays instead of map<handle, resource>?**
- Faster: O(1) array access vs O(log n) map lookup
- Cache-friendly: contiguous memory
- Simpler: no hash collisions, no iterator invalidation

---

## Milestone 1.4: First Triangle with RenderContext

### Objective
Prove the entire RenderContext stack works end-to-end by rendering a colored triangle.

### Implementation

```cpp
// xrRender/RenderContext/TriangleTest.cpp
#include "stdafx.h"
#include "RenderDevice.h"
#include "RenderContext.h"
#include "PipelineState.h"

namespace xray::render::test {

// ═══════════════════════════════════════════════════
//  TEST: Render Colored Triangle
// ═══════════════════════════════════════════════════

void RenderTriangle() {
    Msg("! [TriangleTest] Starting...");
    
    // ─── SETUP DEVICE ───
    RenderDevice device;
    if (!device.InitializeD3D11(HW.pDevice, HW.pContext)) {
        Msg("! [TriangleTest] ❌ Device initialization failed");
        return;
    }
    
    // ─── WRAP BACKBUFFER ───
    RenderDevice::TextureDesc backbufferDesc;
    backbufferDesc.width = Device.dwWidth;
    backbufferDesc.height = Device.dwHeight;
    backbufferDesc.format = nvrhi::Format::RGBA8_UNORM;
    backbufferDesc.isRenderTarget = true;
    backbufferDesc.debugName = "Backbuffer";
    
    TextureHandle backbuffer = device.CreateTextureFromD3D11(
        HW.pBaseRT, backbufferDesc);
    
    if (!backbuffer.IsValid()) {
        Msg("! [TriangleTest] ❌ Failed to wrap backbuffer");
        return;
    }
    
    // ─── CREATE VERTEX BUFFER ───
    struct Vertex {
        float pos[3];     // Position
        float color[4];   // Color
    };
    
    Vertex vertices[] = {
        // Position          // Color (RGBA)
        { { 0.0f,  0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f} },  // Top (red)
        { { 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f} },  // Right (green)
        { {-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f} }   // Left (blue)
    };
    
    RenderDevice::BufferDesc vbDesc;
    vbDesc.byteSize = sizeof(vertices);
    vbDesc.isVertexBuffer = true;
    vbDesc.debugName = "TriangleVB";
    
    BufferHandle vertexBuffer = device.CreateBuffer(vbDesc, vertices);
    
    if (!vertexBuffer.IsValid()) {
        Msg("! [TriangleTest] ❌ Failed to create vertex buffer");
        return;
    }
    
    // ─── LOAD SHADERS ───
    // Assume these are already compiled (X-Ray shader system)
    // For test, we'll create minimal shaders inline
    
    const char* vsCode = R"(
        struct VSInput {
            float3 pos : POSITION;
            float4 color : COLOR;
        };
        
        struct PSInput {
            float4 pos : SV_Position;
            float4 color : COLOR;
        };
        
        PSInput main(VSInput input) {
            PSInput output;
            output.pos = float4(input.pos, 1.0);
            output.color = input.color;
            return output;
        }
    )";
    
    const char* psCode = R"(
        struct PSInput {
            float4 pos : SV_Position;
            float4 color : COLOR;
        };
        
        float4 main(PSInput input) : SV_Target {
            return input.color;
        }
    )";
    
    // Compile shaders (using D3DCompile)
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    
    HRESULT hr = D3DCompile(
        vsCode, strlen(vsCode),
        "triangle_vs", nullptr, nullptr,
        "main", "vs_5_0", 0, 0,
        &vsBlob, &errorBlob);
    
    if (FAILED(hr)) {
        if (errorBlob) {
            Msg("! [TriangleTest] ❌ VS compilation failed: %s",
                (char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        return;
    }
    
    hr = D3DCompile(
        psCode, strlen(psCode),
        "triangle_ps", nullptr, nullptr,
        "main", "ps_5_0", 0, 0,
        &psBlob, &errorBlob);
    
    if (FAILED(hr)) {
        if (errorBlob) {
            Msg("! [TriangleTest] ❌ PS compilation failed: %s",
                (char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        vsBlob->Release();
        return;
    }
    
    // Create NVRHI shaders
    nvrhi::ShaderDesc vsDesc;
    vsDesc.shaderType = nvrhi::ShaderType::Vertex;
    vsDesc.debugName = "TriangleVS";
    
    nvrhi::ShaderHandle nvrhiVS = device.GetNativeDevice()->createShader(
        vsDesc, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize());
    
    nvrhi::ShaderDesc psDesc;
    psDesc.shaderType = nvrhi::ShaderType::Pixel;
    psDesc.debugName = "TrianglePS";
    
    nvrhi::ShaderHandle nvrhiPS = device.GetNativeDevice()->createShader(
        psDesc, psBlob->GetBufferPointer(), psBlob->GetBufferSize());
    
    vsBlob->Release();
    psBlob->Release();
    
    Shader* vs = xr_new<Shader>(ShaderStage::Vertex, nvrhiVS, "TriangleVS");
    Shader* ps = xr_new<Shader>(ShaderStage::Pixel, nvrhiPS, "TrianglePS");
    
    // ─── CREATE PIPELINE STATE ───
    PipelineStateDesc psoDesc;
    psoDesc.vertexShader = vs;
    psoDesc.pixelShader = ps;
    
    // Vertex input layout
    psoDesc.vertexAttributes = {
        {"POSITION", 0, nvrhi::Format::RGB32_FLOAT, 0, 0, false},
        {"COLOR", 0, nvrhi::Format::RGBA32_FLOAT, 12, 0, false}
    };
    
    // Rasterizer state (no culling for this test)
    psoDesc.rasterizerState.cullMode = CullMode::None;
    psoDesc.rasterizerState.fillMode = FillMode::Solid;
    
    // Render target format
    psoDesc.renderTargetFormats[0] = nvrhi::Format::RGBA8_UNORM;
    psoDesc.renderTargetCount = 1;
    
    // Topology
    psoDesc.primitiveTopology = PrimitiveTopology::TriangleList;
    
    psoDesc.debugName = "TrianglePSO";
    
    // Create PSO via cache
    PipelineState* pso = device.GetPipelineCache()->GetOrCreate(psoDesc);
    
    if (!pso) {
        Msg("! [TriangleTest] ❌ Failed to create PSO");
        return;
    }
    
    // ─── RENDER ───
    RenderContext* ctx = device.CreateContext();
    
    ctx->Begin();
    
    // Clear backbuffer
    float clearColor[4] = {0.1f, 0.1f, 0.1f, 1.0f};
    ctx->ClearRenderTarget(backbuffer, clearColor);
    
    // Set render target
    ctx->SetRenderTargets(&backbuffer, 1);
    
    // Set viewport
    ctx->SetViewport(0, 0, Device.dwWidth, Device.dwHeight);
    
    // Set pipeline
    ctx->SetPipeline(pso);
    
    // Bind vertex buffer
    ctx->SetVertexBuffer(0, vertexBuffer, sizeof(Vertex), 0);
    
    // Draw triangle
    ctx->Draw(3, 0);
    
    ctx->End();
    
    // Execute
    device.ExecuteContext(ctx);
    
    // ─── CLEANUP ───
    device.DestroyContext(ctx);
    device.DestroyBuffer(vertexBuffer);
    device.DestroyTexture(backbuffer);
    
    xr_delete(vs);
    xr_delete(ps);
    
    Msg("! [TriangleTest] ✅ Complete");
}

} // namespace xray::render::test
```

**Console Command Integration:**

```cpp
// Register in xrRender_console.cpp
class CCC_RenderTriangle : public IConsole_Command {
public:
    virtual void Execute(LPCSTR args) {
        xray::render::test::RenderTriangle();
    }
};

Console->AddCommand(new CCC_RenderTriangle(), "render_triangle");
```

---

### Success Criteria

- ✅ Triangle renders with correct vertex colors (red, green, blue)
- ✅ No D3D11 debug layer errors or warnings
- ✅ PSO cache shows 1 PSO created, 0 cache misses
- ✅ Statistics show 1 texture (backbuffer), 1 buffer (VB)
- ✅ Clean shutdown (no leaks)

### Performance Metrics

- **Total Frame Time**: <1ms (clear + 1 draw call)
- **PSO Creation**: <5ms (one-time cost)
- **Draw Call Overhead**: <0.01ms (same as native D3D11)

### Validation Checklist

```cpp
// After rendering, validate state
void ValidateTriangleTest() {
    auto& stats = device.GetStatistics();
    
    VERIFY(stats.texturesAlive == 1);     // Backbuffer only
    VERIFY(stats.buffersAlive == 1);      // Vertex buffer only
    VERIFY(stats.texturesCreated == 1);
    VERIFY(stats.buffersCreated == 1);
    
    auto& psoStats = device.GetPipelineCache()->GetStats();
    VERIFY(psoStats.psoCount == 1);
    VERIFY(psoStats.cacheHits == 0);      // First frame
    VERIFY(psoStats.cacheMisses == 1);
}
```

---

# 🏗️ Phase 2: FrameGraph Core (Weeks 8-10)

## Overview

Now that RenderContext provides a solid rendering API, we build FrameGraph on top to provide:
- **Automatic dependency tracking**: Passes declare what resources they read/write
- **Resource lifetime management**: Virtual resources allocated only when needed
- **Barrier insertion**: Automatic transitions between resource states
- **Memory aliasing**: Reuse memory for non-overlapping resources
- **Pass culling**: Remove unused passes automatically

**Benefits Over Manual Management:**
- No manual barrier tracking (error-prone)
- Optimal resource allocation (minimal memory)
- Self-documenting render pipeline (via graph visualization)
- Easy async compute integration (automatic synchronization)

---

## Milestone 2.1: Virtual Resource System

### Objective
Implement virtual resource handles that decouple logical resource descriptions from physical GPU allocations.

### Design Principles

1. **Declare, Don't Allocate**: Passes declare needed resources, FrameGraph allocates later
2. **Lifetime Tracking**: Automatically compute when resources are first/last used
3. **Memory Aliasing**: Reuse memory for non-overlapping resources
4. **Transient vs Persistent**: Distinguish temporary (per-frame) from long-lived resources

---

### Implementation

```cpp
// xrRender/FrameGraph/FGResource.h
#pragma once

#include "../RenderContext/ResourceHandle.h"
#include "../RenderContext/RenderContext.h"

namespace xray::render::framegraph {

// ═══════════════════════════════════════════════════
//  VIRTUAL RESOURCE HANDLE (Opaque)
// ═══════════════════════════════════════════════════

struct VirtualResourceHandle {
    u32 index = INVALID_INDEX;
    
    bool IsValid() const { return index != INVALID_INDEX; }
    
    bool operator==(const VirtualResourceHandle& other) const {
        return index == other.index;
    }
};

// ═══════════════════════════════════════════════════
//  RESOURCE DESCRIPTION (Logical)
// ═══════════════════════════════════════════════════

struct ResourceDesc {
    enum Type {
        Texture2D,
        Texture3D,
        TextureCube,
        Buffer
    };
    
    Type type = Texture2D;
    
    // ─── Texture Properties ───
    u32 width = 0;
    u32 height = 0;
    u32 depth = 1;
    u32 arraySize = 1;
    u32 mipLevels = 1;
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    
    // ─── Buffer Properties ───
    u64 bufferSize = 0;
    u32 structStride = 0;
    
    // ─── Usage Flags ───
    bool isRenderTarget = false;
    bool isDepthStencil = false;
    bool isUAV = false;
    
    // ─── Memory Hints ───
    enum class LifetimeHint {
        Transient,    // Lives only within this frame
        Persistent,   // Lives across multiple frames
        External      // Managed outside FrameGraph (e.g. backbuffer)
    };
    LifetimeHint lifetimeHint = LifetimeHint::Transient;
    
    shared_str debugName;
    
    // ─── Memory Size Estimation ───
    u64 EstimateMemorySize() const {
        if (type == Buffer) {
            return bufferSize;
        } else {
            u32 pixelSize = GetFormatPixelSize(format);
            u64 size = width * height * depth * arraySize * pixelSize;
            
            // Account for mip chain (roughly 33% more)
            if (mipLevels > 1) {
                size = size * 4 / 3;
            }
            
            return size;
        }
    }
    
private:
    static u32 GetFormatPixelSize(nvrhi::Format fmt) {
        // Simplified - real implementation would handle all formats
        switch (fmt) {
            case nvrhi::Format::RGBA8_UNORM: return 4;
            case nvrhi::Format::RGBA16_FLOAT: return 8;
            case nvrhi::Format::RGBA32_FLOAT: return 16;
            case nvrhi::Format::R32_FLOAT: return 4;
            case nvrhi::Format::D24S8: return 4;
            default: return 4;
        }
    }
};

// ═══════════════════════════════════════════════════
//  RESOURCE NODE (Internal Storage)
// ═══════════════════════════════════════════════════

struct ResourceNode {
    ResourceDesc desc;
    
    // ─── Lifetime Tracking ───
    u32 firstUsedPass = INVALID_INDEX;  // First pass that accesses this
    u32 lastUsedPass = INVALID_INDEX;   // Last pass that accesses this
    u32 refCount = 0;                   // Number of pass accesses
    
    // ─── Resource Type ───
    bool isTransient = true;   // Created/destroyed this frame
    bool isImported = false;   // External resource (don't allocate/free)
    
    // ─── Physical Resource (Allocated During Compile) ───
    xray::render::TextureHandle physicalTexture;
    xray::render::BufferHandle physicalBuffer;
    
    // ─── Memory Aliasing (Optimization) ───
    u32 aliasedWith = INVALID_INDEX;  // Share memory with another resource
    
    // ─── Debug ───
    xr_vector<u32> readByPasses;   // Which passes read this
    xr_vector<u32> writtenByPasses; // Which passes write this
    
    // ─── Helper Methods ───
    bool IsAllocated() const {
        return desc.type == ResourceDesc::Buffer 
            ? physicalBuffer.IsValid()
            : physicalTexture.IsValid();
    }
    
    u64 GetMemorySize() const {
        return desc.EstimateMemorySize();
    }
};

} // namespace xray::render::framegraph
```

---

### Success Criteria

- ✅ Resource descriptors can represent all common GPU resource types
- ✅ Memory size estimation accurate within 10%
- ✅ Lifetime tracking data structures prepared
- ✅ Clean separation between logical (desc) and physical (handles)

### Design Rationale

**Why virtual handles?**
- Defers allocation until we know full lifetime
- Enables memory aliasing optimizations
- Allows compile-time culling of unused resources

**Why track first/last used pass?**
```
Pass 0: Write ResourceA
Pass 1: Read ResourceA, Write ResourceB
Pass 2: Read ResourceB
Pass 3: (doesn't use anything)

ResourceA: first=0, last=1  → Can free after Pass 1
ResourceB: first=1, last=2  → Can ALIAS with ResourceA!
```

---

## Milestone 2.2: Pass System

### Objective
Implement render pass abstraction that declares resource dependencies and execution logic.

### Implementation

```cpp
// xrRender/FrameGraph/FGPass.h
#pragma once

#include "FGResource.h"
#include <functional>

namespace xray::render::framegraph {

// Forward decl
class FrameGraph;

// ═══════════════════════════════════════════════════
//  PASS HANDLE (Opaque)
// ═══════════════════════════════════════════════════

struct PassHandle {
    u32 index = INVALID_INDEX;
    
    bool IsValid() const { return index != INVALID_INDEX; }
};

// ═══════════════════════════════════════════════════
//  PASS EXECUTION CALLBACK
// ═══════════════════════════════════════════════════

using PassExecuteCallback = std::function<void(
    xray::render::RenderContext& ctx,
    const FrameGraph& fg
)>;

// ═══════════════════════════════════════════════════
//  PASS NODE (Internal Storage)
// ═══════════════════════════════════════════════════

struct PassNode {
    shared_str name;
    
    // ─── Queue Type ───
    enum class QueueType {
        Graphics,  // Render, clear, copy operations
        Compute,   // Compute shaders only
        Copy       // Copy operations only (future async DMA)
    };
    QueueType queueType = QueueType::Graphics;
    
    // ─── Resource Accesses ───
    struct ResourceAccess {
        VirtualResourceHandle resource;
        
        enum class AccessType {
            Read,
            Write,
            ReadWrite
        };
        AccessType accessType;
        
        // Required resource state for this access
        xray::render::RenderContext::ResourceState stateBefore;
        xray::render::RenderContext::ResourceState stateAfter;
        
        // Optional: specific mip/slice
        u32 mipLevel = 0;
        u32 arraySlice = 0;
        bool allMips = true;
        bool allSlices = true;
    };
    
    xr_vector<ResourceAccess> resourceAccesses;
    
    // ─── Execution ───
    PassExecuteCallback executeCallback;
    
    // ─── Compilation Results ───
    bool culled = false;                    // Optimized out during compile
    u32 executionOrder = INVALID_INDEX;     // Order in final pass list
    xr_vector<PassNode*> dependsOn;         // Passes that must run before this
    xr_vector<PassNode*> dependedOnBy;      // Passes that need this to run first
    
    // ─── GPU Profiling ───
    u32 timestampQueryStart = INVALID_INDEX;
    u32 timestampQueryEnd = INVALID_INDEX;
    float lastExecutionTimeMs = 0.0f;
    
    // ─── Debug ───
    xr_vector<VirtualResourceHandle> reads;   // Resources read by this pass
    xr_vector<VirtualResourceHandle> writes;  // Resources written by this pass
    
    // ─── Helper Methods ───
    bool ReadsResource(VirtualResourceHandle resource) const {
        for (const auto& access : resourceAccesses) {
            if (access.resource == resource && 
                (access.accessType == ResourceAccess::AccessType::Read ||
                 access.accessType == ResourceAccess::AccessType::ReadWrite)) {
                return true;
            }
        }
        return false;
    }
    
    bool WritesResource(VirtualResourceHandle resource) const {
        for (const auto& access : resourceAccesses) {
            if (access.resource == resource &&
                (access.accessType == ResourceAccess::AccessType::Write ||
                 access.accessType == ResourceAccess::AccessType::ReadWrite)) {
                return true;
            }
        }
        return false;
    }
    
    bool HasDependencyOn(const PassNode* other) const {
        return std::find(dependsOn.begin(), dependsOn.end(), other) != dependsOn.end();
    }
};

} // namespace xray::render::framegraph
```

---

### Success Criteria

- ✅ Pass can declare multiple resource accesses
- ✅ Pass stores execution callback for later invocation
- ✅ Dependency tracking data structures prepared
- ✅ Queue type support for future async compute

### Design Rationale

**Why callback instead of virtual class?**
- Lambdas can capture context (easier to use)
- No virtual dispatch overhead
- More flexible (can use any callable)

**Why separate read/write/readwrite?**
- Determines dependency order
- Enables barrier optimization (read-only = shareable)
- Helps with aliasing analysis

---


