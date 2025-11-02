# 🗄️ Modern ResourceManager Implementation - Complete Guide

## 📋 Overview: Old vs New Approach

### **Current "Naive" Approach (Wrapping Legacy)**

```cpp
// Old way - direct wrapping of X-Ray resources
class GeometryPass {
    void Execute() {
        // Get legacy texture directly
        ref_texture legacyTexture = m_material->GetTexture("base");
        
        // Wrap it for NVRHI
        TextureHandle handle = WrapLegacyTexture(legacyTexture);
        
        // Hope it's loaded and in memory
        ctx.SetTexture(0, handle);
    }
};

// Problems:
// ❌ No control over memory usage
// ❌ No streaming (all or nothing loading)
// ❌ No async loading (blocks on texture load)
// ❌ Can't prioritize what's visible
// ❌ Can't evict unused textures
```

### **New Modern Approach (Streaming Resource Manager)**

```cpp
// New way - managed resources with streaming
class GeometryPass {
    void Execute() {
        // Request texture with priority
        TextureHandle handle = m_resourceManager->LoadTexture(
            "textures/base.dds",
            TexturePriority::High  // Visible this frame
        );
        
        // Request appropriate mip levels
        float screenSize = CalculateScreenSize(mesh);
        u32 mipsNeeded = CalculateMipsFromScreenSize(screenSize);
        m_resourceManager->RequestMips(handle, mipsNeeded);
        
        // Bind (might use lower mips if streaming in progress)
        ctx.SetTexture(0, handle);
    }
};

// Benefits:
// ✅ Memory budget enforced (e.g. 2GB texture pool)
// ✅ Streaming (load high mips first, stream in detail)
// ✅ Async loading (doesn't block rendering)
// ✅ Priority system (visible objects load first)
// ✅ Automatic eviction (LRU, least important first)
```

---

## 🎯 Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│           ResourceManager (Top Level)               │
├─────────────────────────────────────────────────────┤
│                                                     │
│  ┌─────────────────────────────────────────────┐   │
│  │      TextureManager                         │   │
│  │  • Streaming system                         │   │
│  │  • Memory budget enforcement                │   │
│  │  • Priority-based loading                   │   │
│  │  • Mip-level management                     │   │
│  │  • LRU eviction                             │   │
│  └─────────────────────────────────────────────┘   │
│                                                     │
│  ┌─────────────────────────────────────────────┐   │
│  │      BufferManager                          │   │
│  │  • Static buffers (geometry)                │   │
│  │  • Dynamic buffers (constants, per-frame)   │   │
│  │  • Staging buffers                          │   │
│  │  • Ring buffer allocation                   │   │
│  └─────────────────────────────────────────────┘   │
│                                                     │
│  ┌─────────────────────────────────────────────┐   │
│  │      SamplerCache                           │   │
│  │  • Sampler state management                 │   │
│  │  • Deduplication                            │   │
│  └─────────────────────────────────────────────┘   │
│                                                     │
└─────────────────────────────────────────────────────┘
         ↓ Uses
┌─────────────────────────────────────────────────────┐
│              RenderDevice                           │
│  • NVRHI texture/buffer creation                    │
│  • Physical GPU resource management                 │
└─────────────────────────────────────────────────────┘
```

---

# 📅 Implementation Timeline: 4 Weeks

## Week 1: Core Handle System + Texture Manager Foundation
## Week 2: Streaming System + Memory Management  
## Week 3: Buffer Manager + Async Loading
## Week 4: Integration + Migration Tools

---

# 🗓️ Week 1: Core Handle System + Texture Manager Foundation

## **Day 1 (Monday): Resource Handle System**

### Morning (4 hours):

#### **Task 1.1: Create ResourceHandle Base**
**File:** `xrRender/ResourceManager/ResourceHandle.h`

```cpp
#pragma once

namespace xray::render::resources {

// ═══════════════════════════════════════════════════
//  GENERATIONAL HANDLE (Detect Use-After-Free)
// ═══════════════════════════════════════════════════

constexpr u32 INVALID_RESOURCE_INDEX = 0xFFFFFF;  // 24 bits
constexpr u32 INVALID_GENERATION = 0xFF;          // 8 bits

struct ResourceHandle {
    u32 index : 24;        // Index into resource array
    u32 generation : 8;    // Generation counter
    
    ResourceHandle() : index(INVALID_RESOURCE_INDEX), generation(0) {}
    
    ResourceHandle(u32 idx, u32 gen) 
        : index(idx), generation(gen) {}
    
    bool IsValid() const { 
        return index != INVALID_RESOURCE_INDEX; 
    }
    
    void Invalidate() { 
        index = INVALID_RESOURCE_INDEX; 
    }
    
    bool operator==(const ResourceHandle& other) const {
        return index == other.index && generation == other.generation;
    }
    
    bool operator!=(const ResourceHandle& other) const {
        return !(*this == other);
    }
    
    // For use in maps
    bool operator<(const ResourceHandle& other) const {
        return (u32(*this)) < (u32(other));
    }
    
    // Convert to/from u32 (for serialization, debugging)
    explicit operator u32() const {
        return (generation << 24) | index;
    }
    
    static ResourceHandle FromU32(u32 value) {
        return ResourceHandle(value & 0xFFFFFF, value >> 24);
    }
};

// Strongly typed handles (prevent mixing texture/buffer)
struct TextureHandle : ResourceHandle {
    using ResourceHandle::ResourceHandle;
    TextureHandle() = default;
    TextureHandle(const ResourceHandle& base) : ResourceHandle(base) {}
};

struct BufferHandle : ResourceHandle {
    using ResourceHandle::ResourceHandle;
    BufferHandle() = default;
    BufferHandle(const ResourceHandle& base) : ResourceHandle(base) {}
};

struct SamplerHandle : ResourceHandle {
    using ResourceHandle::ResourceHandle;
    SamplerHandle() = default;
    SamplerHandle(const ResourceHandle& base) : ResourceHandle(base) {}
};

} // namespace xray::render::resources

// Hash support for xr_map
namespace std {
    template<>
    struct hash<xray::render::resources::ResourceHandle> {
        size_t operator()(const xray::render::resources::ResourceHandle& h) const {
            return hash<u32>()(u32(h));
        }
    };
}
```

**Est:** 1.5 hours

**Deliverable:**
- [ ] ResourceHandle.h compiles
- [ ] Generational index system working
- [ ] Can detect stale handles in debug

---

#### **Task 1.2: Texture Metadata Structures**
**File:** `xrRender/ResourceManager/TextureManager.h`

```cpp
#pragma once

#include "ResourceHandle.h"
#include <nvrhi/nvrhi.h>

namespace xray::render::resources {

// ═══════════════════════════════════════════════════
//  TEXTURE STATE TRACKING
// ═══════════════════════════════════════════════════

enum class TextureState : u8 {
    Unloaded,       // Not in memory (on disk only)
    Loading,        // Async load in progress
    Resident,       // Fully loaded in VRAM
    Evicting,       // Marked for eviction
    Evicted,        // Was resident, now evicted (keep metadata)
};

const char* TextureStateToString(TextureState state);

// ═══════════════════════════════════════════════════
//  TEXTURE PRIORITY (For Streaming Decisions)
// ═══════════════════════════════════════════════════

enum class TexturePriority : u8 {
    Critical = 0,   // Never evict (UI, HUD, player weapon)
    High = 1,       // Visible this frame
    Medium = 2,     // Visible recently (within last 5 frames)
    Low = 3,        // Not visible, keep if memory available
    VeryLow = 4,    // Evict first
};

const char* TexturePriorityToString(TexturePriority priority);

// ═══════════════════════════════════════════════════
//  TEXTURE DESCRIPTOR (Logical Description)
// ═══════════════════════════════════════════════════

struct TextureDesc {
    enum Type {
        Texture1D,
        Texture2D,
        Texture3D,
        TextureCube
    };
    
    Type type = Texture2D;
    
    u32 width = 0;
    u32 height = 0;
    u32 depth = 1;           // For 3D textures
    u32 arraySize = 1;       // For texture arrays/cubemaps
    u32 mipLevels = 1;
    
    nvrhi::Format format = nvrhi::Format::RGBA8_UNORM;
    
    // Usage flags
    bool isRenderTarget = false;
    bool isDepthStencil = false;
    bool isUAV = false;
    bool isSRGB = false;     // sRGB color space
    
    // Streaming hints
    bool allowStreaming = true;   // Can stream mips?
    u32 minResidentMips = 3;      // Always keep this many mips
    
    shared_str debugName;
    
    // Calculate memory size for specific mip range
    u64 CalculateMemorySize(u32 startMip = 0, u32 mipCount = 0) const;
};

// ═══════════════════════════════════════════════════
//  TEXTURE METADATA (Runtime Tracking)
// ═══════════════════════════════════════════════════

struct TextureMetadata {
    // Identity
    shared_str filePath;         // "textures/concrete_diff.dds"
    TextureDesc desc;
    
    // State
    TextureState state = TextureState::Unloaded;
    TexturePriority priority = TexturePriority::Medium;
    u32 generation = 0;          // For handle validation
    bool isAlive = true;         // Still valid?
    
    // Streaming state
    u32 residentMips = 0;        // How many mips currently loaded
    u32 requestedMips = 0;       // How many mips needed
    u32 totalMips = 0;           // Total mips available on disk
    
    // Memory tracking
    u64 memoryUsed = 0;          // Bytes in VRAM
    
    // Usage tracking (for eviction)
    float lastAccessTime = 0.0f; // Time since last use
    u32 accessCount = 0;         // Total access count
    u32 refCount = 0;            // Active references
    
    // Physical resource
    nvrhi::TextureHandle nvrhiTexture;  // May be null if unloaded
    
    // Async loading
    struct LoadRequest {
        bool inProgress = false;
        u32 targetMips = 0;
        // Add thread handle, etc.
    } loadRequest;
    
    // Helpers
    bool IsResident() const { 
        return state == TextureState::Resident; 
    }
    
    bool NeedsStreaming() const {
        return requestedMips > residentMips && state == TextureState::Resident;
    }
    
    bool CanEvict() const {
        return priority >= TexturePriority::Low && 
               refCount == 0 && 
               state == TextureState::Resident;
    }
};

} // namespace xray::render::resources
```

**Est:** 2 hours

**Deliverable:**
- [ ] TextureMetadata structure defined
- [ ] State machine documented
- [ ] Helper methods implemented

---

### Afternoon (4 hours):

#### **Task 1.3: Texture Manager Skeleton**
**File:** `xrRender/ResourceManager/TextureManager.h` (continued)

```cpp
namespace xray::render::resources {

// ═══════════════════════════════════════════════════
//  TEXTURE MANAGER (Main Interface)
// ═══════════════════════════════════════════════════

class TextureManager {
public:
    explicit TextureManager(RenderDevice* device);
    ~TextureManager();
    
    // ═══════════════════════════════════════════════════
    //  LOADING
    // ═══════════════════════════════════════════════════
    
    // Load texture from disk (async)
    TextureHandle LoadTexture(
        const char* path,
        TexturePriority priority = TexturePriority::Medium
    );
    
    // Create runtime texture (not from disk)
    TextureHandle CreateTexture(
        const TextureDesc& desc,
        const void* initialData = nullptr
    );
    
    // Create from existing NVRHI texture (e.g. backbuffer)
    TextureHandle ImportTexture(
        nvrhi::TextureHandle nvrhiTexture,
        const TextureDesc& desc,
        const char* debugName
    );
    
    // ═══════════════════════════════════════════════════
    //  ACCESS
    // ═══════════════════════════════════════════════════
    
    // Get NVRHI texture (may trigger load if unloaded)
    nvrhi::ITexture* GetNVRHITexture(TextureHandle handle);
    
    // Get metadata (for inspection)
    const TextureMetadata* GetMetadata(TextureHandle handle) const;
    
    // Check if texture is resident
    bool IsResident(TextureHandle handle) const;
    
    // ═══════════════════════════════════════════════════
    //  STREAMING CONTROL
    // ═══════════════════════════════════════════════════
    
    // Set memory budget (total VRAM for textures)
    void SetMemoryBudget(u64 bytes);
    u64 GetMemoryBudget() const { return m_memoryBudget; }
    
    // Request specific number of mips (for LOD)
    void RequestMips(TextureHandle handle, u32 mipCount);
    
    // Change priority (affects eviction order)
    void SetPriority(TextureHandle handle, TexturePriority priority);
    
    // Mark as accessed (updates LRU)
    void Touch(TextureHandle handle);
    
    // ═══════════════════════════════════════════════════
    //  LIFECYCLE
    // ═══════════════════════════════════════════════════
    
    // Increment reference count
    void AddRef(TextureHandle handle);
    
    // Decrement reference count (evict if zero)
    void Release(TextureHandle handle);
    
    // Force eviction
    void Evict(TextureHandle handle);
    
    // ═══════════════════════════════════════════════════
    //  UPDATE (Per Frame)
    // ═══════════════════════════════════════════════════
    
    void Update(float deltaTime);
    
    // ═══════════════════════════════════════════════════
    //  STATISTICS
    // ═══════════════════════════════════════════════════
    
    struct Statistics {
        u64 totalMemoryUsed = 0;
        u64 memoryBudget = 0;
        
        u32 texturesTotal = 0;
        u32 texturesResident = 0;
        u32 texturesLoading = 0;
        u32 texturesEvicted = 0;
        
        u32 streamingRequestsPending = 0;
        u32 evictionsPending = 0;
        
        float memoryUsagePercent() const {
            return (float)totalMemoryUsed / (float)memoryBudget * 100.0f;
        }
    };
    
    Statistics GetStatistics() const;
    void PrintStatistics() const;
    
private:
    RenderDevice* m_device;
    
    // ═══════════════════════════════════════════════════
    //  RESOURCE STORAGE (Sparse Array)
    // ═══════════════════════════════════════════════════
    
    xr_vector<TextureMetadata> m_textures;
    xr_vector<u32> m_freeSlots;  // Reusable indices
    
    // Name → Handle lookup (for deduplication)
    xr_map<shared_str, TextureHandle> m_pathToHandle;
    
    // ═══════════════════════════════════════════════════
    //  MEMORY MANAGEMENT
    // ═══════════════════════════════════════════════════
    
    u64 m_memoryBudget = 2ULL * 1024 * 1024 * 1024;  // 2GB default
    u64 m_memoryUsed = 0;
    
    // ═══════════════════════════════════════════════════
    //  INTERNAL METHODS
    // ═══════════════════════════════════════════════════
    
    // Handle allocation
    TextureHandle AllocateHandle();
    void FreeHandle(TextureHandle handle);
    bool ValidateHandle(TextureHandle handle) const;
    
    // Loading (to be implemented)
    void LoadTextureAsync(TextureHandle handle);
    void StreamMips(TextureHandle handle, u32 targetMips);
    
    // Eviction (to be implemented)
    void EvictTextures(u64 bytesNeeded);
    void EvictTextureInternal(TextureHandle handle);
    
    // Statistics
    mutable Statistics m_stats;
};

} // namespace xray::render::resources
```

**Est:** 2 hours

---

#### **Task 1.4: Texture Manager Implementation (Basic)**
**File:** `xrRender/ResourceManager/TextureManager.cpp`

```cpp
#include "stdafx.h"
#include "TextureManager.h"
#include "../RenderContext/RenderDevice.h"

namespace xray::render::resources {

// ═══════════════════════════════════════════════════
//  CONSTRUCTION
// ═══════════════════════════════════════════════════

TextureManager::TextureManager(RenderDevice* device)
    : m_device(device)
{
    VERIFY(m_device);
    
    // Pre-allocate some capacity
    m_textures.reserve(1024);
    
    Msg("! [TextureManager] Created with budget: %llu MB",
        m_memoryBudget / (1024 * 1024));
}

TextureManager::~TextureManager() {
    Msg("! [TextureManager] Destroying...");
    
    // Check for leaks
    u32 leakCount = 0;
    for (const auto& tex : m_textures) {
        if (tex.isAlive) {
            Msg("! [TextureManager] ⚠️ Leak: %s (refCount=%u)",
                tex.filePath.c_str(), tex.refCount);
            leakCount++;
        }
    }
    
    if (leakCount > 0) {
        Msg("! [TextureManager] ❌ %u texture leaks detected!", leakCount);
    }
    
    PrintStatistics();
}

// ═══════════════════════════════════════════════════
//  HANDLE MANAGEMENT
// ═══════════════════════════════════════════════════

TextureHandle TextureManager::AllocateHandle() {
    u32 index;
    u32 generation;
    
    if (!m_freeSlots.empty()) {
        // Reuse freed slot
        index = m_freeSlots.back();
        m_freeSlots.pop_back();
        
        // Increment generation to invalidate old handles
        generation = m_textures[index].generation + 1;
        m_textures[index].generation = generation;
    } else {
        // Allocate new slot
        index = (u32)m_textures.size();
        generation = 0;
        
        m_textures.emplace_back();
        m_textures[index].generation = generation;
    }
    
    return TextureHandle(index, generation);
}

void TextureManager::FreeHandle(TextureHandle handle) {
    if (!ValidateHandle(handle)) return;
    
    TextureMetadata& meta = m_textures[handle.index];
    meta.isAlive = false;
    
    m_freeSlots.push_back(handle.index);
}

bool TextureManager::ValidateHandle(TextureHandle handle) const {
    if (!handle.IsValid()) return false;
    if (handle.index >= m_textures.size()) return false;
    
    const TextureMetadata& meta = m_textures[handle.index];
    if (!meta.isAlive) return false;
    if (meta.generation != handle.generation) return false;  // Stale!
    
    return true;
}

// ═══════════════════════════════════════════════════
//  LOADING (Stub - Will Implement in Day 2+)
// ═══════════════════════════════════════════════════

TextureHandle TextureManager::LoadTexture(
    const char* path,
    TexturePriority priority)
{
    shared_str pathStr = path;
    
    // Check if already loaded
    auto it = m_pathToHandle.find(pathStr);
    if (it != m_pathToHandle.end()) {
        TextureHandle existing = it->second;
        if (ValidateHandle(existing)) {
            Msg("! [TextureManager] Texture already loaded: %s", path);
            AddRef(existing);
            return existing;
        }
    }
    
    // Allocate handle
    TextureHandle handle = AllocateHandle();
    TextureMetadata& meta = m_textures[handle.index];
    
    // Setup metadata
    meta.filePath = pathStr;
    meta.state = TextureState::Unloaded;
    meta.priority = priority;
    meta.isAlive = true;
    
    // Register path
    m_pathToHandle[pathStr] = handle;
    
    Msg("! [TextureManager] Registered texture: %s (handle=%u.%u)",
        path, handle.index, handle.generation);
    
    // TODO: Kick off async load
    // LoadTextureAsync(handle);
    
    m_stats.texturesTotal++;
    
    return handle;
}

// ═══════════════════════════════════════════════════
//  ACCESS
// ═══════════════════════════════════════════════════

nvrhi::ITexture* TextureManager::GetNVRHITexture(TextureHandle handle) {
    if (!ValidateHandle(handle)) return nullptr;
    
    TextureMetadata& meta = m_textures[handle.index];
    
    // Touch for LRU
    meta.lastAccessTime = 0.0f;  // TODO: Use actual time
    meta.accessCount++;
    
    // TODO: If unloaded, trigger load
    if (meta.state == TextureState::Unloaded) {
        Msg("! [TextureManager] ⚠️ Accessing unloaded texture: %s",
            meta.filePath.c_str());
        // LoadTextureAsync(handle);
    }
    
    return meta.nvrhiTexture.Get();
}

const TextureMetadata* TextureManager::GetMetadata(TextureHandle handle) const {
    if (!ValidateHandle(handle)) return nullptr;
    return &m_textures[handle.index];
}

bool TextureManager::IsResident(TextureHandle handle) const {
    if (!ValidateHandle(handle)) return false;
    return m_textures[handle.index].IsResident();
}

// ═══════════════════════════════════════════════════
//  LIFECYCLE
// ═══════════════════════════════════════════════════

void TextureManager::AddRef(TextureHandle handle) {
    if (!ValidateHandle(handle)) return;
    
    TextureMetadata& meta = m_textures[handle.index];
    meta.refCount++;
}

void TextureManager::Release(TextureHandle handle) {
    if (!ValidateHandle(handle)) return;
    
    TextureMetadata& meta = m_textures[handle.index];
    
    if (meta.refCount > 0) {
        meta.refCount--;
    }
    
    // If no more references and not high priority, consider evicting
    if (meta.refCount == 0 && meta.priority >= TexturePriority::Low) {
        // Mark as evictable (will be handled by Update())
    }
}

// ═══════════════════════════════════════════════════
//  UPDATE (Stub)
// ═══════════════════════════════════════════════════

void TextureManager::Update(float deltaTime) {
    // TODO: Implement streaming and eviction
    // For now, just update timers
    
    for (auto& meta : m_textures) {
        if (!meta.isAlive) continue;
        meta.lastAccessTime += deltaTime;
    }
}

// ═══════════════════════════════════════════════════
//  STATISTICS
// ═══════════════════════════════════════════════════

TextureManager::Statistics TextureManager::GetStatistics() const {
    m_stats.totalMemoryUsed = m_memoryUsed;
    m_stats.memoryBudget = m_memoryBudget;
    m_stats.texturesTotal = 0;
    m_stats.texturesResident = 0;
    m_stats.texturesLoading = 0;
    m_stats.texturesEvicted = 0;
    
    for (const auto& meta : m_textures) {
        if (!meta.isAlive) continue;
        
        m_stats.texturesTotal++;
        
        switch (meta.state) {
            case TextureState::Resident:
                m_stats.texturesResident++;
                break;
            case TextureState::Loading:
                m_stats.texturesLoading++;
                break;
            case TextureState::Evicted:
                m_stats.texturesEvicted++;
                break;
            default:
                break;
        }
    }
    
    return m_stats;
}

void TextureManager::PrintStatistics() const {
    auto stats = GetStatistics();
    
    Msg("! [TextureManager] Statistics:");
    Msg("!   Memory: %llu / %llu MB (%.1f%%)",
        stats.totalMemoryUsed / (1024 * 1024),
        stats.memoryBudget / (1024 * 1024),
        stats.memoryUsagePercent());
    Msg("!   Textures: %u total, %u resident, %u loading, %u evicted",
        stats.texturesTotal,
        stats.texturesResident,
        stats.texturesLoading,
        stats.texturesEvicted);
}

// ═══════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════

const char* TextureStateToString(TextureState state) {
    switch (state) {
        case TextureState::Unloaded: return "Unloaded";
        case TextureState::Loading: return "Loading";
        case TextureState::Resident: return "Resident";
        case TextureState::Evicting: return "Evicting";
        case TextureState::Evicted: return "Evicted";
        default: return "Unknown";
    }
}

const char* TexturePriorityToString(TexturePriority priority) {
    switch (priority) {
        case TexturePriority::Critical: return "Critical";
        case TexturePriority::High: return "High";
        case TexturePriority::Medium: return "Medium";
        case TexturePriority::Low: return "Low";
        case TexturePriority::VeryLow: return "VeryLow";
        default: return "Unknown";
    }
}

} // namespace xray::render::resources
```

**Est:** 2 hours

**Deliverable:**
- [ ] TextureManager compiles
- [ ] Can allocate/free handles
- [ ] Handle validation works (detects stale handles)
- [ ] Statistics tracking functional

---

## **Day 2 (Tuesday): Texture Loading + DDS Parser**

### Morning (4 hours):

#### **Task 2.1: DDS File Format Parser**
**File:** `xrRender/ResourceManager/DDSLoader.h`

```cpp
#pragma once

namespace xray::render::resources {

// ═══════════════════════════════════════════════════
//  DDS FILE FORMAT (DirectDraw Surface)
// ═══════════════════════════════════════════════════

struct DDSHeader {
    // DDS magic number: "DDS "
    u32 magic;  // 0x20534444
    
    // DDS_HEADER structure
    u32 size;
    u32 flags;
    u32 height;
    u32 width;
    u32 pitchOrLinearSize;
    u32 depth;
    u32 mipMapCount;
    u32 reserved1[11];
    
    // DDS_PIXELFORMAT
    struct PixelFormat {
        u32 size;
        u32 flags;
        u32 fourCC;
        u32 rgbBitCount;
        u32 rBitMask;
        u32 gBitMask;
        u32 bBitMask;
        u32 aBitMask;
    } pixelFormat;
    
    u32 caps;
    u32 caps2;
    u32 caps3;
    u32 caps4;
    u32 reserved2;
};

// DX10 extended header
struct DDSHeaderDXT10 {
    u32 dxgiFormat;
    u32 resourceDimension;
    u32 miscFlag;
    u32 arraySize;
    u32 miscFlags2;
};

// ═══════════════════════════════════════════════════
//  PARSED DDS DATA
// ═══════════════════════════════════════════════════

struct DDSData {
    u32 width;
    u32 height;
    u32 depth;
    u32 mipLevels;
    u32 arraySize;
    nvrhi::Format format;
    
    bool isCubemap;
    bool isVolume;
    
    // Mip level data
    struct MipLevel {
        const u8* data;
        u32 size;
        u32 width;
        u32 height;
        u32 rowPitch;
        u32 slicePitch;
    };
    
    xr_vector<MipLevel> mips;
    
    // Total data size
    u64 totalSize;
};

// ═══════════════════════════════════════════════════
//  DDS LOADER
// ═══════════════════════════════════════════════════

class DDSLoader {
public:
    // Load DDS from file
    static bool LoadFromFile(
        const char* path,
        DDSData& outData,
        xr_vector<u8>& outBuffer  // Owns the memory
    );
    
    // Load DDS from memory
    static bool LoadFromMemory(
        const u8* data,
        u32 size,
        DDSData& outData
    );
    
    // Helper: Get format from FourCC
    static nvrhi::Format GetFormatFromFourCC(u32 fourCC);
    
    // Helper: Calculate mip level size
    static u32 CalculateMipSize(
        u32 width,
        u32 height,
        nvrhi::Format format
    );
    
private:
    static bool ValidateHeader(const DDSHeader& header);
    static void ParseMipLevels(DDSData& data, const u8* fileData);
};

} // namespace xray::render::resources
```

**Implementation:**

```cpp
// xrRender/ResourceManager/DDSLoader.cpp
#include "stdafx.h"
#include "DDSLoader.h"

namespace xray::render::resources {

bool DDSLoader::LoadFromFile(
    const char* path,
    DDSData& outData,
    xr_vector<u8>& outBuffer)
{
    // Open file
    IReader* reader = FS.r_open(path);
    if (!reader) {
        Msg("! [DDSLoader] ❌ Failed to open: %s", path);
        return false;
    }
    
    // Read entire file
    u32 fileSize = reader->length();
    outBuffer.resize(fileSize);
    reader->r(outBuffer.data(), fileSize);
    FS.r_close(reader);
    
    // Parse
    return LoadFromMemory(outBuffer.data(), fileSize, outData);
}

bool DDSLoader::LoadFromMemory(
    const u8* data,
    u32 size,
    DDSData& outData)
{
    if (size < sizeof(DDSHeader)) {
        Msg("! [DDSLoader] ❌ File too small");
        return false;
    }
    
    // Parse header
    const DDSHeader* header = (const DDSHeader*)data;
    
    if (header->magic != 0x20534444) {  // "DDS "
        Msg("! [DDSLoader] ❌ Invalid magic number");
        return false;
    }
    
    if (!ValidateHeader(*header)) {
        return false;
    }
    
    // Extract dimensions
    outData.width = header->width;
    outData.height = header->height;
    outData.depth = (header->flags & 0x800000) ? header->depth : 1;
    outData.mipLevels = (header->mipMapCount > 0) ? header->mipMapCount : 1;
    outData.arraySize = 1;
    
    // Determine format
    outData.format = GetFormatFromFourCC(header->pixelFormat.fourCC);
    
    if (outData.format == nvrhi::Format::UNKNOWN) {
        Msg("! [DDSLoader] ❌ Unsupported format: 0x%X", 
            header->pixelFormat.fourCC);
        return false;
    }
    
    // Check for cubemap
    outData.isCubemap = (header->caps2 & 0x200) != 0;
    if (outData.isCubemap) {
        outData.arraySize = 6;
    }
    
    // Parse mip levels
    ParseMipLevels(outData, data);
    
    Msg("! [DDSLoader] ✅ Loaded: %ux%u, %u mips, format=%d",
        outData.width, outData.height, outData.mipLevels, (int)outData.format);
    
    return true;
}

void DDSLoader::ParseMipLevels(DDSData& data, const u8* fileData) {
    const u8* mipData = fileData + sizeof(DDSHeader);
    
    data.mips.resize(data.mipLevels);
    data.totalSize = 0;
    
    u32 width = data.width;
    u32 height = data.height;
    
    for (u32 mip = 0; mip < data.mipLevels; mip++) {
        DDSData::MipLevel& level = data.mips[mip];
        
        level.width = std::max(1u, width);
        level.height = std::max(1u, height);
        level.data = mipData;
        level.size = CalculateMipSize(level.width, level.height, data.format);
        
        // TODO: Calculate row pitch properly for compressed formats
        level.rowPitch = level.size / level.height;
        level.slicePitch = level.size;
        
        data.totalSize += level.size;
        mipData += level.size;
        
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
}

nvrhi::Format DDSLoader::GetFormatFromFourCC(u32 fourCC) {
    // Common DDS formats
    switch (fourCC) {
        case 0:  // Uncompressed RGBA
            return nvrhi::Format::RGBA8_UNORM;
            
        case '1TXD':  // DXT1
            return nvrhi::Format::BC1_UNORM;
            
        case '3TXD':  // DXT3
            return nvrhi::Format::BC2_UNORM;
            
        case '5TXD':  // DXT5
            return nvrhi::Format::BC3_UNORM;
            
        case 'U4CB':  // BC4
            return nvrhi::Format::BC4_UNORM;
            
        case 'U5CB':  // BC5
            return nvrhi::Format::BC5_UNORM;
            
        case 'T1CB':  // BC6H
            return nvrhi::Format::BC6H_UFLOAT;
            
        case 'T2CB':  // BC7
            return nvrhi::Format::BC7_UNORM;
            
        default:
            return nvrhi::Format::UNKNOWN;
    }
}

u32 DDSLoader::CalculateMipSize(
    u32 width,
    u32 height,
    nvrhi::Format format)
{
    // TODO: Implement proper size calculation for all formats
    // For now, rough estimate
    
    switch (format) {
        case nvrhi::Format::BC1_UNORM:  // DXT1
            return ((width + 3) / 4) * ((height + 3) / 4) * 8;
            
        case nvrhi::Format::BC2_UNORM:  // DXT3
        case nvrhi::Format::BC3_UNORM:  // DXT5
            return ((width + 3) / 4) * ((height + 3) / 4) * 16;
            
        case nvrhi::Format::RGBA8_UNORM:
            return width * height * 4;
            
        case nvrhi::Format::RGBA16_FLOAT:
            return width * height * 8;
            
        default:
            return width * height * 4;  // Assume 32bpp
    }
}

bool DDSLoader::ValidateHeader(const DDSHeader& header) {
    if (header.size != 124) {
        Msg("! [DDSLoader] ❌ Invalid header size");
        return false;
    }
    
    if (header.pixelFormat.size != 32) {
        Msg("! [DDSLoader] ❌ Invalid pixel format size");
        return false;
    }
    
    return true;
}

} // namespace xray::render::resources
```

**Est:** 3 hours

**Deliverable:**
- [ ] Can parse DDS files
- [ ] Extracts mip levels correctly
- [ ] Format conversion working

---

### Afternoon (4 hours):

#### **Task 2.2: Synchronous Texture Loading**
**File:** `xrRender/ResourceManager/TextureManager.cpp` (add to existing)

```cpp
// Add to TextureManager class:

void TextureManager::LoadTextureSync(TextureHandle handle) {
    if (!ValidateHandle(handle)) return;
    
    TextureMetadata& meta = m_textures[handle.index];
    
    if (meta.state != TextureState::Unloaded) {
        // Already loaded or loading
        return;
    }
    
    meta.state = TextureState::Loading;
    
    // ═══════════════════════════════════════════════════
    //  LOAD DDS FILE
    // ═══════════════════════════════════════════════════
    
    DDSData ddsData;
    xr_vector<u8> fileBuffer;
    
    if (!DDSLoader::LoadFromFile(meta.filePath.c_str(), ddsData, fileBuffer)) {
        Msg("! [TextureManager] ❌ Failed to load: %s", meta.filePath.c_str());
        meta.state = TextureState::Unloaded;
        return;
    }
    
    // ═══════════════════════════════════════════════════
    //  CREATE NVRHI TEXTURE
    // ═══════════════════════════════════════════════════
    
    // Fill desc
    meta.desc.type = TextureDesc::Texture2D;
    meta.desc.width = ddsData.width;
    meta.desc.height = ddsData.height;
    meta.desc.format = ddsData.format;
    meta.desc.mipLevels = ddsData.mipLevels;
    meta.desc.debugName = meta.filePath.c_str();
    
    // Create texture via RenderDevice
    RenderDevice::TextureDesc deviceDesc;
    deviceDesc.width = ddsData.width;
    deviceDesc.height = ddsData.height;
    deviceDesc.mipLevels = ddsData.mipLevels;
    deviceDesc.format = ddsData.format;
    deviceDesc.isRenderTarget = false;
    deviceDesc.debugName = meta.filePath.c_str();
    
    // Create without initial data (will upload separately)
    xray::render::TextureHandle deviceHandle = 
        m_device->CreateTexture(deviceDesc);
    
    if (!deviceHandle.IsValid()) {
        Msg("! [TextureManager] ❌ Failed to create GPU texture");
        meta.state = TextureState::Unloaded;
        return;
    }
    
    // Get NVRHI handle
    meta.nvrhiTexture = m_device->GetNativeTexture(deviceHandle);
    
    // ═══════════════════════════════════════════════════
    //  UPLOAD TEXTURE DATA
    // ═══════════════════════════════════════════════════
    
    // TODO: Use staging buffer and async upload
    // For now, direct upload
    
    nvrhi::ICommandList* cmd = m_device->GetNativeDevice()->createCommandList();
    cmd->open();
    
    for (u32 mip = 0; mip < ddsData.mipLevels; mip++) {
        const DDSData::MipLevel& mipData = ddsData.mips[mip];
        
        cmd->writeTexture(
            meta.nvrhiTexture,
            0,  // array slice
            mip,
            mipData.data,
            mipData.rowPitch,
            mipData.slicePitch
        );
    }
    
    cmd->close();
    m_device->GetNativeDevice()->executeCommandList(cmd);
    
    // ═══════════════════════════════════════════════════
    //  UPDATE METADATA
    // ═══════════════════════════════════════════════════
    
    meta.state = TextureState::Resident;
    meta.residentMips = ddsData.mipLevels;
    meta.totalMips = ddsData.mipLevels;
    meta.memoryUsed = ddsData.totalSize;
    
    m_memoryUsed += meta.memoryUsed;
    
    Msg("! [TextureManager] ✅ Loaded: %s (%llu KB)",
        meta.filePath.c_str(),
        meta.memoryUsed / 1024);
    
    // Check if over budget
    if (m_memoryUsed > m_memoryBudget) {
        Msg("! [TextureManager] ⚠️ Over budget! (%llu / %llu MB)",
            m_memoryUsed / (1024 * 1024),
            m_memoryBudget / (1024 * 1024));
        
        // TODO: Trigger eviction
    }
}

// Update LoadTexture to trigger sync load:
TextureHandle TextureManager::LoadTexture(
    const char* path,
    TexturePriority priority)
{
    // ... existing deduplication code ...
    
    TextureHandle handle = AllocateHandle();
    TextureMetadata& meta = m_textures[handle.index];
    
    meta.filePath = path;
    meta.state = TextureState::Unloaded;
    meta.priority = priority;
    meta.isAlive = true;
    
    m_pathToHandle[path] = handle;
    
    // ⚠️ For now, load synchronously
    // TODO: Make async in Week 3
    LoadTextureSync(handle);
    
    return handle;
}
```

**Est:** 3 hours

**Deliverable:**
- [ ] Can load DDS textures from disk
- [ ] Uploads to GPU correctly
- [ ] Textures visible in RenderDoc
- [ ] Memory tracking updates

---

#### **Task 2.3: Test Texture Loading**

Create test scene:

```cpp
// xrRender/ResourceManager/TestTextureManager.cpp

void TestTextureLoading() {
    using namespace xray::render::resources;
    
    RenderDevice device;
    device.InitializeD3D11(HW.pDevice, HW.pContext);
    
    TextureManager texManager(&device);
    
    // ═══════════════════════════════════════════════════
    //  TEST 1: Load Single Texture
    // ═══════════════════════════════════════════════════
    
    TextureHandle tex1 = texManager.LoadTexture(
        "textures/concrete_diff.dds",
        TexturePriority::High
    );
    
    VERIFY(tex1.IsValid());
    VERIFY(texManager.IsResident(tex1));
    
    Msg("! [TEST] ✅ Loaded single texture");
    
    // ═══════════════════════════════════════════════════
    //  TEST 2: Deduplication
    // ═══════════════════════════════════════════════════
    
    TextureHandle tex2 = texManager.LoadTexture(
        "textures/concrete_diff.dds",  // Same path
        TexturePriority::High
    );
    
    VERIFY(tex1 == tex2);  // Should be same handle
    
    Msg("! [TEST] ✅ Deduplication works");
    
    // ═══════════════════════════════════════════════════
    //  TEST 3: Multiple Textures
    // ═══════════════════════════════════════════════════
    
    xr_vector<TextureHandle> textures;
    for (u32 i = 0; i < 100; i++) {
        string256 path;
        xr_sprintf(path, "textures/test_%03u.dds", i);
        
        TextureHandle handle = texManager.LoadTexture(path);
        textures.push_back(handle);
    }
    
    Msg("! [TEST] ✅ Loaded 100 textures");
    
    // ═══════════════════════════════════════════════════
    //  TEST 4: Statistics
    // ═══════════════════════════════════════════════════
    
    texManager.PrintStatistics();
    
    auto stats = texManager.GetStatistics();
    VERIFY(stats.texturesResident >= 101);  // At least tex1 + 100
    
    Msg("! [TEST] ✅ All tests passed!");
}
```

**Est:** 1 hour

**Deliverable:**
- [ ] Test loads textures successfully
- [ ] Statistics accurate
- [ ] No memory leaks (verified on shutdown)

---

## 🎯 Week 1 Deliverables Checklist

By end of Week 1, you should have:

- [ ] ResourceHandle system working (generational indices)
- [ ] TextureManager skeleton functional
- [ ] Can load DDS files from disk
- [ ] Textures upload to GPU
- [ ] Basic memory tracking
- [ ] Handle validation detects stale handles
- [ ] Statistics reporting
- [ ] Test suite passing

**Next Week Preview:** Week 2 will add streaming, memory budget enforcement, and eviction policies.

---

## 📊 Usage Example: Old vs New (Week 1 Complete)

### **Old Way:**
```cpp
// Direct X-Ray resource usage
ref_texture tex = Device.Resources->_CreateTexture("base");
ID3D11ShaderResourceView* srv = tex->pSurface;
ctx->PSSetShaderResources(0, 1, &srv);
```

### **New Way (After Week 1):**
```cpp
// Modern resource manager
TextureHandle handle = m_resourceManager->LoadTexture(
    "textures/base.dds",
    TexturePriority::High
);

nvrhi::ITexture* nvrhiTex = m_resourceManager->GetNVRHITexture(handle);
ctx.SetTexture(0, nvrhiTex);  // RenderContext handles binding

// Later...
m_resourceManager->Release(handle);  // Decrement refcount
```

---

**Continue to Week 2?** I can provide Days 3-7 covering:
- Streaming system (partial mip loading)
- Memory budget enforcement
- LRU eviction policies
- Priority-based loading
- Async loading foundation

Let me know if you'd like me to continue with the remaining weeks!
