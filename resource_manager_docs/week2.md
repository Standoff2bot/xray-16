# 🗄️ Modern ResourceManager Implementation - Weeks 2-5

## 📋 Migration Strategy (Simplified)

### **Approach: Flag-Based System Swap**

```cpp
// When use_framegraph = true:
//   → Use new TextureManager, BufferManager, ResourceManager
// When use_framegraph = false:
//   → Use vanilla CResourceManager, ref_texture, etc.

// Implementation:
if (psDeviceFlags.test(rsUseFrameGraph)) {
    // Modern path
    m_modernRenderer->RenderFrame();
} else {
    // Vanilla path
    RImplementation.Render();
}
```

**No gradual migration, no compatibility layer, just a clean swap.**

---

# 🗓️ Week 2: Streaming System + Memory Management

## **Day 3 (Wednesday): Mip Streaming Foundation**

### Morning (4 hours):

#### **Task 3.1: Mip Streaming State Machine**
**File:** `xrRender/ResourceManager/TextureStreaming.h`

```cpp
#pragma once

#include "TextureManager.h"

namespace xray::render::resources {

// ═══════════════════════════════════════════════════
//  STREAMING REQUEST
// ═══════════════════════════════════════════════════

struct StreamingRequest {
    TextureHandle handle;
    u32 currentMips;        // Currently resident
    u32 targetMips;         // Want to reach
    TexturePriority priority;
    float requestTime;      // When requested (for timeout)
    
    enum Status {
        Pending,            // Waiting to start
        InProgress,         // Loading from disk
        Uploading,          // Uploading to GPU
        Complete,           // Done
        Failed              // Error occurred
    };
    Status status = Pending;
    
    // For async I/O
    void* ioHandle = nullptr;  // Platform-specific async I/O handle
    xr_vector<u8> stagingBuffer;  // CPU memory for loaded data
    
    // Comparison for priority queue
    bool operator<(const StreamingRequest& other) const {
        // Higher priority = process first
        if (priority != other.priority)
            return priority < other.priority;
        return requestTime < other.requestTime;
    }
};

// ═══════════════════════════════════════════════════
//  STREAMING MANAGER
// ═══════════════════════════════════════════════════

class StreamingManager {
public:
    StreamingManager(RenderDevice* device, TextureManager* texManager);
    ~StreamingManager();
    
    // ═══════════════════════════════════════════════════
    //  REQUEST MANAGEMENT
    // ═══════════════════════════════════════════════════
    
    // Request specific mip levels
    void RequestMips(TextureHandle handle, u32 targetMips, TexturePriority priority);
    
    // Cancel pending request
    void CancelRequest(TextureHandle handle);
    
    // Check if request is active
    bool HasPendingRequest(TextureHandle handle) const;
    
    // ═══════════════════════════════════════════════════
    //  UPDATE (Per Frame)
    // ═══════════════════════════════════════════════════
    
    void Update(float deltaTime);
    
    // ═══════════════════════════════════════════════════
    //  CONFIGURATION
    // ═══════════════════════════════════════════════════
    
    // Max concurrent streaming operations
    void SetMaxConcurrentStreams(u32 count) { m_maxConcurrentStreams = count; }
    u32 GetMaxConcurrentStreams() const { return m_maxConcurrentStreams; }
    
    // Bandwidth limit (bytes per frame)
    void SetBandwidthLimit(u64 bytesPerFrame) { m_bandwidthLimit = bytesPerFrame; }
    u64 GetBandwidthLimit() const { return m_bandwidthLimit; }
    
    // ═══════════════════════════════════════════════════
    //  STATISTICS
    // ═══════════════════════════════════════════════════
    
    struct Statistics {
        u32 requestsPending = 0;
        u32 requestsInProgress = 0;
        u32 requestsCompleted = 0;
        u32 requestsFailed = 0;
        
        u64 bytesStreamedThisFrame = 0;
        u64 bytesStreamedTotal = 0;
        
        float avgStreamTime = 0.0f;
    };
    
    Statistics GetStatistics() const { return m_stats; }
    void PrintStatistics() const;
    
private:
    RenderDevice* m_device;
    TextureManager* m_texManager;
    
    // ═══════════════════════════════════════════════════
    //  REQUEST QUEUE (Priority-based)
    // ═══════════════════════════════════════════════════
    
    xr_vector<StreamingRequest> m_pendingRequests;  // Sorted by priority
    xr_vector<StreamingRequest> m_activeRequests;   // Currently processing
    
    // Handle → Request lookup (for cancellation)
    xr_map<TextureHandle, u32> m_handleToRequest;
    
    // ═══════════════════════════════════════════════════
    //  CONFIGURATION
    // ═══════════════════════════════════════════════════
    
    u32 m_maxConcurrentStreams = 4;       // Max parallel loads
    u64 m_bandwidthLimit = 32 * 1024 * 1024;  // 32 MB/frame
    
    // ═══════════════════════════════════════════════════
    //  INTERNAL METHODS
    // ═══════════════════════════════════════════════════
    
    void ProcessPendingRequests();
    void ProcessActiveRequests();
    void StartRequest(StreamingRequest& request);
    void CompleteRequest(StreamingRequest& request);
    void FailRequest(StreamingRequest& request, const char* reason);
    
    // Mip loading
    bool LoadMipsFromDisk(StreamingRequest& request);
    bool UploadMipsToGPU(StreamingRequest& request);
    
    // Statistics
    mutable Statistics m_stats;
};

} // namespace xray::render::resources
```

**Est:** 2 hours

---

#### **Task 3.2: Implement Streaming Manager Core**
**File:** `xrRender/ResourceManager/TextureStreaming.cpp`

```cpp
#include "stdafx.h"
#include "TextureStreaming.h"
#include "DDSLoader.h"

namespace xray::render::resources {

StreamingManager::StreamingManager(RenderDevice* device, TextureManager* texManager)
    : m_device(device)
    , m_texManager(texManager)
{
    VERIFY(m_device);
    VERIFY(m_texManager);
    
    Msg("! [StreamingManager] Created (max concurrent: %u, bandwidth: %llu MB/frame)",
        m_maxConcurrentStreams,
        m_bandwidthLimit / (1024 * 1024));
}

StreamingManager::~StreamingManager() {
    // Cancel all pending requests
    for (auto& request : m_activeRequests) {
        if (request.ioHandle) {
            // TODO: Cancel async I/O
        }
    }
    
    PrintStatistics();
}

// ═══════════════════════════════════════════════════
//  REQUEST MANAGEMENT
// ═══════════════════════════════════════════════════

void StreamingManager::RequestMips(
    TextureHandle handle,
    u32 targetMips,
    TexturePriority priority)
{
    // Check if already requested
    auto it = m_handleToRequest.find(handle);
    if (it != m_handleToRequest.end()) {
        // Already have a request, update priority
        StreamingRequest& existing = m_pendingRequests[it->second];
        existing.priority = std::min(existing.priority, priority);  // Higher priority wins
        existing.targetMips = std::max(existing.targetMips, targetMips);
        return;
    }
    
    // Get current state
    const TextureMetadata* meta = m_texManager->GetMetadata(handle);
    if (!meta) {
        Msg("! [StreamingManager] ⚠️ Invalid handle");
        return;
    }
    
    // Already have enough mips?
    if (meta->residentMips >= targetMips) {
        return;
    }
    
    // Create request
    StreamingRequest request;
    request.handle = handle;
    request.currentMips = meta->residentMips;
    request.targetMips = std::min(targetMips, meta->totalMips);
    request.priority = priority;
    request.requestTime = Device.fTimeGlobal;
    request.status = StreamingRequest::Pending;
    
    // Add to pending queue
    u32 index = (u32)m_pendingRequests.size();
    m_pendingRequests.push_back(request);
    m_handleToRequest[handle] = index;
    
    // Sort by priority
    std::sort(m_pendingRequests.begin(), m_pendingRequests.end());
    
    m_stats.requestsPending++;
    
    Msg("! [StreamingManager] Request: %s (%u → %u mips, priority=%d)",
        meta->filePath.c_str(),
        request.currentMips,
        request.targetMips,
        (int)priority);
}

void StreamingManager::CancelRequest(TextureHandle handle) {
    auto it = m_handleToRequest.find(handle);
    if (it == m_handleToRequest.end()) {
        return;
    }
    
    u32 index = it->second;
    
    // Remove from pending
    if (index < m_pendingRequests.size()) {
        m_pendingRequests.erase(m_pendingRequests.begin() + index);
        m_handleToRequest.erase(it);
        m_stats.requestsPending--;
    }
    
    // TODO: Cancel active request if in progress
}

bool StreamingManager::HasPendingRequest(TextureHandle handle) const {
    return m_handleToRequest.find(handle) != m_handleToRequest.end();
}

// ═══════════════════════════════════════════════════
//  UPDATE (Called Every Frame)
// ═══════════════════════════════════════════════════

void StreamingManager::Update(float deltaTime) {
    m_stats.bytesStreamedThisFrame = 0;
    
    // Process active requests (check for completion)
    ProcessActiveRequests();
    
    // Start new requests if we have bandwidth
    ProcessPendingRequests();
}

void StreamingManager::ProcessPendingRequests() {
    // Start new requests up to concurrent limit
    while (m_activeRequests.size() < m_maxConcurrentStreams &&
           !m_pendingRequests.empty()) {
        
        // Get highest priority request
        StreamingRequest request = m_pendingRequests.front();
        m_pendingRequests.erase(m_pendingRequests.begin());
        
        // Start it
        StartRequest(request);
        
        // Move to active
        m_activeRequests.push_back(request);
        m_stats.requestsPending--;
        m_stats.requestsInProgress++;
    }
}

void StreamingManager::ProcessActiveRequests() {
    for (auto it = m_activeRequests.begin(); it != m_activeRequests.end(); ) {
        StreamingRequest& request = *it;
        
        switch (request.status) {
            case StreamingRequest::Pending:
                // Shouldn't happen
                StartRequest(request);
                ++it;
                break;
                
            case StreamingRequest::InProgress:
                // Check if I/O complete
                // TODO: Check async I/O status
                // For now, assume synchronous
                if (LoadMipsFromDisk(request)) {
                    request.status = StreamingRequest::Uploading;
                } else {
                    FailRequest(request, "Failed to load from disk");
                    it = m_activeRequests.erase(it);
                    continue;
                }
                ++it;
                break;
                
            case StreamingRequest::Uploading:
                // Upload to GPU
                if (UploadMipsToGPU(request)) {
                    CompleteRequest(request);
                    it = m_activeRequests.erase(it);
                    continue;
                } else {
                    FailRequest(request, "Failed to upload to GPU");
                    it = m_activeRequests.erase(it);
                    continue;
                }
                break;
                
            case StreamingRequest::Complete:
            case StreamingRequest::Failed:
                // Remove from active
                it = m_activeRequests.erase(it);
                continue;
        }
    }
}

// ═══════════════════════════════════════════════════
//  REQUEST PROCESSING
// ═══════════════════════════════════════════════════

void StreamingManager::StartRequest(StreamingRequest& request) {
    Msg("! [StreamingManager] Starting: handle=%u.%u",
        request.handle.index, request.handle.generation);
    
    request.status = StreamingRequest::InProgress;
    
    // TODO: Kick off async I/O
    // For now, will load synchronously in next frame
}

bool StreamingManager::LoadMipsFromDisk(StreamingRequest& request) {
    const TextureMetadata* meta = m_texManager->GetMetadata(request.handle);
    if (!meta) {
        return false;
    }
    
    Msg("! [StreamingManager] Loading mips from disk: %s", meta->filePath.c_str());
    
    // Load DDS file
    DDSData ddsData;
    xr_vector<u8> fileBuffer;
    
    if (!DDSLoader::LoadFromFile(meta->filePath.c_str(), ddsData, fileBuffer)) {
        return false;
    }
    
    // Extract only the mips we need
    u32 startMip = request.currentMips;
    u32 endMip = request.targetMips;
    
    request.stagingBuffer.clear();
    
    for (u32 mip = startMip; mip < endMip; mip++) {
        if (mip >= ddsData.mips.size()) break;
        
        const DDSData::MipLevel& mipData = ddsData.mips[mip];
        
        // Copy to staging buffer
        u32 offset = (u32)request.stagingBuffer.size();
        request.stagingBuffer.resize(offset + mipData.size);
        Memory.mem_copy(
            request.stagingBuffer.data() + offset,
            mipData.data,
            mipData.size
        );
    }
    
    Msg("! [StreamingManager] Loaded %u mips (%llu KB)",
        endMip - startMip,
        request.stagingBuffer.size() / 1024);
    
    return true;
}

bool StreamingManager::UploadMipsToGPU(StreamingRequest& request) {
    TextureMetadata* meta = const_cast<TextureMetadata*>(
        m_texManager->GetMetadata(request.handle)
    );
    
    if (!meta || !meta->nvrhiTexture) {
        return false;
    }
    
    Msg("! [StreamingManager] Uploading mips to GPU...");
    
    // Check bandwidth limit
    if (m_stats.bytesStreamedThisFrame + request.stagingBuffer.size() > m_bandwidthLimit) {
        // Defer to next frame
        return false;
    }
    
    // Upload via NVRHI
    nvrhi::ICommandList* cmd = m_device->GetNativeDevice()->createCommandList();
    cmd->open();
    
    // TODO: Parse staging buffer and upload individual mips
    // For now, simplified version
    
    cmd->close();
    m_device->GetNativeDevice()->executeCommandList(cmd);
    
    // Update metadata
    meta->residentMips = request.targetMips;
    
    m_stats.bytesStreamedThisFrame += request.stagingBuffer.size();
    m_stats.bytesStreamedTotal += request.stagingBuffer.size();
    
    Msg("! [StreamingManager] ✅ Upload complete");
    
    return true;
}

void StreamingManager::CompleteRequest(StreamingRequest& request) {
    Msg("! [StreamingManager] ✅ Request complete: handle=%u.%u",
        request.handle.index, request.handle.generation);
    
    // Remove from lookup
    m_handleToRequest.erase(request.handle);
    
    m_stats.requestsInProgress--;
    m_stats.requestsCompleted++;
}

void StreamingManager::FailRequest(StreamingRequest& request, const char* reason) {
    Msg("! [StreamingManager] ❌ Request failed: %s", reason);
    
    m_handleToRequest.erase(request.handle);
    
    m_stats.requestsInProgress--;
    m_stats.requestsFailed++;
}

// ═══════════════════════════════════════════════════
//  STATISTICS
// ═══════════════════════════════════════════════════

void StreamingManager::PrintStatistics() const {
    Msg("! [StreamingManager] Statistics:");
    Msg("!   Pending: %u, Active: %u", m_stats.requestsPending, m_stats.requestsInProgress);
    Msg("!   Completed: %u, Failed: %u", m_stats.requestsCompleted, m_stats.requestsFailed);
    Msg("!   Streamed: %llu MB total, %llu KB this frame",
        m_stats.bytesStreamedTotal / (1024 * 1024),
        m_stats.bytesStreamedThisFrame / 1024);
}

} // namespace xray::render::resources
```

**Est:** 2 hours

**Deliverable:**
- [ ] Streaming request queue implemented
- [ ] Priority-based ordering works
- [ ] Can request specific mip levels
- [ ] Basic load/upload flow functional

---

### Afternoon (4 hours):

#### **Task 3.3: Memory Budget Enforcement**
**File:** `xrRender/ResourceManager/TextureManager.cpp` (additions)

```cpp
// Add to TextureManager class:

// ═══════════════════════════════════════════════════
//  MEMORY BUDGET ENFORCEMENT
// ═══════════════════════════════════════════════════

bool TextureManager::CheckMemoryBudget(u64 requiredBytes) const {
    return (m_memoryUsed + requiredBytes) <= m_memoryBudget;
}

bool TextureManager::EnforceMemoryBudget(u64 requiredBytes) {
    if (CheckMemoryBudget(requiredBytes)) {
        return true;  // Within budget
    }
    
    u64 bytesNeeded = (m_memoryUsed + requiredBytes) - m_memoryBudget;
    
    Msg("! [TextureManager] Over budget! Need to free %llu MB",
        bytesNeeded / (1024 * 1024));
    
    // Evict textures to make room
    return EvictTextures(bytesNeeded);
}

bool TextureManager::EvictTextures(u64 bytesNeeded) {
    u64 bytesFreed = 0;
    
    // ═══════════════════════════════════════════════════
    //  BUILD EVICTION CANDIDATES (LRU + Priority)
    // ═══════════════════════════════════════════════════
    
    struct EvictionCandidate {
        TextureHandle handle;
        float score;  // Higher = evict first
        u64 memoryUsed;
        
        bool operator<(const EvictionCandidate& other) const {
            return score > other.score;  // Descending order
        }
    };
    
    xr_vector<EvictionCandidate> candidates;
    
    for (u32 i = 0; i < m_textures.size(); i++) {
        const TextureMetadata& meta = m_textures[i];
        
        if (!meta.isAlive) continue;
        if (!meta.CanEvict()) continue;  // Check priority, refCount
        
        // Calculate eviction score
        // Higher score = more likely to evict
        float score = 0.0f;
        
        // Factor 1: Time since last access (LRU)
        score += meta.lastAccessTime * 10.0f;
        
        // Factor 2: Priority (low priority = evict first)
        score += (float)meta.priority * 100.0f;
        
        // Factor 3: Access count (less used = evict first)
        score -= (float)meta.accessCount * 0.1f;
        
        // Factor 4: Memory used (larger = prefer to evict for space)
        score += (float)meta.memoryUsed / (1024.0f * 1024.0f);
        
        EvictionCandidate candidate;
        candidate.handle = TextureHandle(i, meta.generation);
        candidate.score = score;
        candidate.memoryUsed = meta.memoryUsed;
        
        candidates.push_back(candidate);
    }
    
    // Sort by score
    std::sort(candidates.begin(), candidates.end());
    
    Msg("! [TextureManager] Found %u eviction candidates", candidates.size());
    
    // ═══════════════════════════════════════════════════
    //  EVICT UNTIL WE HAVE ENOUGH SPACE
    // ═══════════════════════════════════════════════════
    
    for (const auto& candidate : candidates) {
        if (bytesFreed >= bytesNeeded) {
            break;  // Freed enough
        }
        
        const TextureMetadata* meta = GetMetadata(candidate.handle);
        
        Msg("!   Evicting: %s (score=%.2f, %llu KB)",
            meta->filePath.c_str(),
            candidate.score,
            candidate.memoryUsed / 1024);
        
        EvictTextureInternal(candidate.handle);
        
        bytesFreed += candidate.memoryUsed;
    }
    
    Msg("! [TextureManager] Freed %llu MB (needed %llu MB)",
        bytesFreed / (1024 * 1024),
        bytesNeeded / (1024 * 1024));
    
    return bytesFreed >= bytesNeeded;
}

void TextureManager::EvictTextureInternal(TextureHandle handle) {
    if (!ValidateHandle(handle)) return;
    
    TextureMetadata& meta = m_textures[handle.index];
    
    if (meta.state != TextureState::Resident) {
        return;  // Already evicted
    }
    
    // Release NVRHI texture
    if (meta.nvrhiTexture) {
        // NVRHI will destroy when ref count reaches zero
        meta.nvrhiTexture = nullptr;
    }
    
    // Update state
    meta.state = TextureState::Evicted;
    meta.residentMips = 0;
    
    // Update memory tracking
    m_memoryUsed -= meta.memoryUsed;
    meta.memoryUsed = 0;
    
    Msg("! [TextureManager] Evicted: %s", meta.filePath.c_str());
}
```

**Est:** 2 hours

---

#### **Task 3.4: Integrate Streaming with TextureManager**
**File:** `xrRender/ResourceManager/TextureManager.h` (additions)

```cpp
class TextureManager {
public:
    // ... existing methods ...
    
    // Get streaming manager
    StreamingManager* GetStreamingManager() { return m_streamingManager.get(); }
    
private:
    // ... existing fields ...
    
    // Streaming
    xr_unique_ptr<StreamingManager> m_streamingManager;
    
    // Memory budget enforcement
    bool CheckMemoryBudget(u64 requiredBytes) const;
    bool EnforceMemoryBudget(u64 requiredBytes);
    bool EvictTextures(u64 bytesNeeded);
    void EvictTextureInternal(TextureHandle handle);
};
```

```cpp
// xrRender/ResourceManager/TextureManager.cpp
TextureManager::TextureManager(RenderDevice* device)
    : m_device(device)
{
    // ... existing init ...
    
    // Create streaming manager
    m_streamingManager = xr_make_unique<StreamingManager>(device, this);
}

void TextureManager::Update(float deltaTime) {
    // Update timers (existing code)
    for (auto& meta : m_textures) {
        if (!meta.isAlive) continue;
        meta.lastAccessTime += deltaTime;
    }
    
    // Update streaming
    m_streamingManager->Update(deltaTime);
    
    // Check if over budget (trigger eviction)
    if (m_memoryUsed > m_memoryBudget) {
        u64 excess = m_memoryUsed - m_memoryBudget;
        EvictTextures(excess);
    }
}

void TextureManager::RequestMips(TextureHandle handle, u32 mipCount) {
    if (!ValidateHandle(handle)) return;
    
    const TextureMetadata& meta = m_textures[handle.index];
    
    // Forward to streaming manager
    m_streamingManager->RequestMips(handle, mipCount, meta.priority);
}
```

**Est:** 1.5 hours

**Deliverable:**
- [ ] Memory budget enforcement working
- [ ] LRU eviction implemented
- [ ] Streaming integrated with TextureManager
- [ ] Can request mips and have them load

---

#### **Task 3.5: Test Streaming System**

```cpp
// xrRender/ResourceManager/TestStreaming.cpp

void TestTextureStreaming() {
    using namespace xray::render::resources;
    
    RenderDevice device;
    device.InitializeD3D11(HW.pDevice, HW.pContext);
    
    TextureManager texManager(&device);
    
    // Set tight memory budget to force eviction
    texManager.SetMemoryBudget(100 * 1024 * 1024);  // 100 MB
    
    // ═══════════════════════════════════════════════════
    //  TEST 1: Load Textures Until Over Budget
    // ═══════════════════════════════════════════════════
    
    xr_vector<TextureHandle> textures;
    
    for (u32 i = 0; i < 50; i++) {
        string256 path;
        xr_sprintf(path, "textures/test_%03u.dds", i);
        
        TextureHandle handle = texManager.LoadTexture(path);
        textures.push_back(handle);
        
        texManager.Update(0.016f);  // Simulate frame
    }
    
    auto stats = texManager.GetStatistics();
    
    Msg("! [TEST] Memory: %llu / %llu MB (%.1f%%)",
        stats.totalMemoryUsed / (1024 * 1024),
        stats.memoryBudget / (1024 * 1024),
        stats.memoryUsagePercent());
    
    // Should have triggered eviction
    VERIFY(stats.texturesEvicted > 0);
    
    Msg("! [TEST] ✅ Eviction triggered: %u textures evicted", stats.texturesEvicted);
    
    // ═══════════════════════════════════════════════════
    //  TEST 2: Request Mips (Streaming)
    // ═══════════════════════════════════════════════════
    
    TextureHandle tex = textures[0];
    
    // Request more mips
    texManager.RequestMips(tex, 8);
    
    // Simulate frames until complete
    for (u32 frame = 0; frame < 60; frame++) {
        texManager.Update(0.016f);
        
        if (texManager.GetMetadata(tex)->residentMips >= 8) {
            Msg("! [TEST] ✅ Streaming complete on frame %u", frame);
            break;
        }
    }
    
    // ═══════════════════════════════════════════════════
    //  TEST 3: LRU Eviction
    // ═══════════════════════════════════════════════════
    
    // Touch first texture repeatedly
    for (u32 i = 0; i < 10; i++) {
        texManager.Touch(textures[0]);
    }
    
    // Load more textures (should evict least recently used)
    for (u32 i = 0; i < 20; i++) {
        string256 path;
        xr_sprintf(path, "textures/extra_%03u.dds", i);
        texManager.LoadTexture(path);
        texManager.Update(0.016f);
    }
    
    // First texture should still be resident (frequently accessed)
    VERIFY(texManager.IsResident(textures[0]));
    
    // Others should be evicted
    u32 evictedCount = 0;
    for (u32 i = 1; i < 10; i++) {
        if (!texManager.IsResident(textures[i])) {
            evictedCount++;
        }
    }
    
    VERIFY(evictedCount > 0);
    
    Msg("! [TEST] ✅ LRU eviction working: %u textures evicted", evictedCount);
    
    texManager.PrintStatistics();
}
```

**Est:** 0.5 hours

**Deliverable:**
- [ ] Streaming test passes
- [ ] Memory budget enforced
- [ ] LRU eviction working
- [ ] Mip streaming functional

---

## **Day 4 (Thursday): Buffer Manager**

### Morning (4 hours):

#### **Task 4.1: Buffer Manager Structure**
**File:** `xrRender/ResourceManager/BufferManager.h`

```cpp
#pragma once

#include "ResourceHandle.h"
#include <nvrhi/nvrhi.h>

namespace xray::render::resources {

// ═══════════════════════════════════════════════════
//  BUFFER TYPES
// ═══════════════════════════════════════════════════

enum class BufferType : u8 {
    Vertex,          // Vertex buffer
    Index,           // Index buffer
    Constant,        // Constant buffer (uniform buffer)
    Structured,      // Structured buffer (SSBO)
    Raw,             // Raw buffer (ByteAddressBuffer)
};

enum class BufferUsage : u8 {
    Static,          // Written once, read many (geometry)
    Dynamic,         // Written every frame (per-frame constants)
    Staging,         // CPU-visible for readback
};

// ═══════════════════════════════════════════════════
//  BUFFER DESCRIPTOR
// ═══════════════════════════════════════════════════

struct BufferDesc {
    BufferType type = BufferType::Vertex;
    BufferUsage usage = BufferUsage::Static;
    
    u64 size = 0;           // Size in bytes
    u32 stride = 0;         // For structured buffers
    
    // Access flags
    bool cpuAccess = false;    // Can map from CPU?
    bool gpuWrite = false;     // UAV?
    
    shared_str debugName;
    
    BufferDesc() = default;
    
    BufferDesc(BufferType t, u64 s, const char* name = nullptr)
        : type(t), size(s), debugName(name) {}
};

// ═══════════════════════════════════════════════════
//  BUFFER METADATA
// ═══════════════════════════════════════════════════

struct BufferMetadata {
    BufferDesc desc;
    u32 generation = 0;
    bool isAlive = true;
    
    // Physical resource
    nvrhi::BufferHandle nvrhiBuffer;
    
    // Usage tracking
    u32 refCount = 0;
    float lastAccessTime = 0.0f;
    
    // Memory
    u64 memoryUsed = 0;
};

// ═══════════════════════════════════════════════════
//  RING BUFFER (For Dynamic Allocations)
// ═══════════════════════════════════════════════════

class RingBuffer {
public:
    RingBuffer(RenderDevice* device, u64 size, const char* debugName);
    ~RingBuffer();
    
    // Allocate from ring buffer
    struct Allocation {
        nvrhi::BufferHandle buffer;
        u64 offset;
        u64 size;
        void* cpuAddress;  // If mapped
    };
    
    Allocation Allocate(u64 size, u32 alignment = 256);
    
    // Advance to next frame (wrap around)
    void AdvanceFrame();
    
    // Get statistics
    u64 GetSize() const { return m_size; }
    u64 GetUsed() const { return m_used; }
    u64 GetAvailable() const { return m_size - m_used; }
    
private:
    RenderDevice* m_device;
    nvrhi::BufferHandle m_buffer;
    
    u64 m_size;
    u64 m_head;      // Current write position
    u64 m_used;      // Bytes used this frame
    
    void* m_cpuAddress;  // Persistent mapping
};

// ═══════════════════════════════════════════════════
//  BUFFER MANAGER
// ═══════════════════════════════════════════════════

class BufferManager {
public:
    explicit BufferManager(RenderDevice* device);
    ~BufferManager();
    
    // ═══════════════════════════════════════════════════
    //  STATIC BUFFERS
    // ═══════════════════════════════════════════════════
    
    // Create static buffer (geometry, etc.)
    BufferHandle CreateBuffer(
        const BufferDesc& desc,
        const void* initialData = nullptr
    );
    
    // Update static buffer (rare, expensive)
    void UpdateBuffer(
        BufferHandle handle,
        const void* data,
        u64 size,
        u64 offset = 0
    );
    
    // ═══════════════════════════════════════════════════
    //  DYNAMIC BUFFERS (Ring Buffer Allocation)
    // ═══════════════════════════════════════════════════
    
    // Allocate from ring buffer (per-frame data)
    RingBuffer::Allocation AllocateDynamic(u64 size, u32 alignment = 256);
    
    // Allocate constant buffer data (convenience)
    template<typename T>
    RingBuffer::Allocation AllocateConstants(const T& data) {
        auto alloc = AllocateDynamic(sizeof(T), 256);
        if (alloc.cpuAddress) {
            Memory.mem_copy(alloc.cpuAddress, &data, sizeof(T));
        }
        return alloc;
    }
    
    // ═══════════════════════════════════════════════════
    //  ACCESS
    // ═══════════════════════════════════════════════════
    
    nvrhi::IBuffer* GetNVRHIBuffer(BufferHandle handle);
    const BufferMetadata* GetMetadata(BufferHandle handle) const;
    
    // ═══════════════════════════════════════════════════
    //  LIFECYCLE
    // ═══════════════════════════════════════════════════
    
    void AddRef(BufferHandle handle);
    void Release(BufferHandle handle);
    void DestroyBuffer(BufferHandle handle);
    
    // ═══════════════════════════════════════════════════
    //  FRAME MANAGEMENT
    // ═══════════════════════════════════════════════════
    
    void BeginFrame();
    void EndFrame();
    
    // ═══════════════════════════════════════════════════
    //  STATISTICS
    // ═══════════════════════════════════════════════════
    
    struct Statistics {
        u64 staticMemoryUsed = 0;
        u64 dynamicMemoryUsed = 0;
        u64 totalMemoryUsed = 0;
        
        u32 buffersTotal = 0;
        u32 buffersStatic = 0;
        u32 dynamicAllocationsThisFrame = 0;
    };
    
    Statistics GetStatistics() const;
    void PrintStatistics() const;
    
private:
    RenderDevice* m_device;
    
    // Static buffers
    xr_vector<BufferMetadata> m_buffers;
    xr_vector<u32> m_freeSlots;
    
    // Dynamic buffers (ring buffers)
    xr_unique_ptr<RingBuffer> m_constantBufferRing;
    xr_unique_ptr<RingBuffer> m_vertexBufferRing;
    xr_unique_ptr<RingBuffer> m_indexBufferRing;
    
    // Handle management
    BufferHandle AllocateHandle();
    void FreeHandle(BufferHandle handle);
    bool ValidateHandle(BufferHandle handle) const;
    
    // Statistics
    mutable Statistics m_stats;
};

} // namespace xray::render::resources
```

**Est:** 2 hours

---

#### **Task 4.2: Implement Ring Buffer**
**File:** `xrRender/ResourceManager/BufferManager.cpp`

```cpp
#include "stdafx.h"
#include "BufferManager.h"
#include "../RenderContext/RenderDevice.h"

namespace xray::render::resources {

// ═══════════════════════════════════════════════════
//  RING BUFFER IMPLEMENTATION
// ═══════════════════════════════════════════════════

RingBuffer::RingBuffer(RenderDevice* device, u64 size, const char* debugName)
    : m_device(device)
    , m_size(size)
    , m_head(0)
    , m_used(0)
    , m_cpuAddress(nullptr)
{
    VERIFY(m_device);
    
    // Create buffer
    nvrhi::BufferDesc desc;
    desc.byteSize = size;
    desc.structStride = 0;
    desc.debugName = debugName;
    desc.isConstantBuffer = true;
    desc.isVolatile = true;  // Hint for driver
    desc.cpuAccess = nvrhi::CpuAccessMode::Write;
    
    m_buffer = m_device->GetNativeDevice()->createBuffer(desc);
    
    if (!m_buffer) {
        Msg("! [RingBuffer] ❌ Failed to create buffer: %s", debugName);
        return;
    }
    
    // Persistent map
    m_cpuAddress = m_device->GetNativeDevice()->mapBuffer(m_buffer, nvrhi::CpuAccessMode::Write);
    
    Msg("! [RingBuffer] Created: %s (%llu MB)",
        debugName, size / (1024 * 1024));
}

RingBuffer::~RingBuffer() {
    if (m_buffer && m_cpuAddress) {
        m_device->GetNativeDevice()->unmapBuffer(m_buffer);
    }
}

RingBuffer::Allocation RingBuffer::Allocate(u64 size, u32 alignment) {
    // Align head
    u64 alignedHead = (m_head + alignment - 1) & ~(alignment - 1);
    
    // Check if we have space
    if (alignedHead + size > m_size) {
        // Wrap around
        Msg("! [RingBuffer] ⚠️ Wrapping around (head=%llu, size=%llu)",
            m_head, m_size);
        
        alignedHead = 0;
        m_head = 0;
        m_used = 0;  // Reset usage for new frame
    }
    
    // Create allocation
    Allocation alloc;
    alloc.buffer = m_buffer;
    alloc.offset = alignedHead;
    alloc.size = size;
    alloc.cpuAddress = m_cpuAddress ? 
        (u8*)m_cpuAddress + alignedHead : nullptr;
    
    // Advance head
    m_head = alignedHead + size;
    m_used += size;
    
    return alloc;
}

void RingBuffer::AdvanceFrame() {
    // Could implement per-frame tracking here
    // For now, just reset usage counter
    m_used = 0;
}

// ═══════════════════════════════════════════════════
//  BUFFER MANAGER IMPLEMENTATION
// ═══════════════════════════════════════════════════

BufferManager::BufferManager(RenderDevice* device)
    : m_device(device)
{
    VERIFY(m_device);
    
    // Create ring buffers
    m_constantBufferRing = xr_make_unique<RingBuffer>(
        device,
        16 * 1024 * 1024,  // 16 MB
        "ConstantBufferRing"
    );
    
    m_vertexBufferRing = xr_make_unique<RingBuffer>(
        device,
        32 * 1024 * 1024,  // 32 MB
        "VertexBufferRing"
    );
    
    m_indexBufferRing = xr_make_unique<RingBuffer>(
        device,
        16 * 1024 * 1024,  // 16 MB
        "IndexBufferRing"
    );
    
    Msg("! [BufferManager] Created");
}

BufferManager::~BufferManager() {
    // Check for leaks
    u32 leakCount = 0;
    for (const auto& buffer : m_buffers) {
        if (buffer.isAlive) {
            Msg("! [BufferManager] ⚠️ Leak: %s (refCount=%u)",
                buffer.desc.debugName.c_str(), buffer.refCount);
            leakCount++;
        }
    }
    
    if (leakCount > 0) {
        Msg("! [BufferManager] ❌ %u buffer leaks detected!", leakCount);
    }
    
    PrintStatistics();
}

// ═══════════════════════════════════════════════════
//  BUFFER CREATION
// ═══════════════════════════════════════════════════

BufferHandle BufferManager::CreateBuffer(
    const BufferDesc& desc,
    const void* initialData)
{
    // Allocate handle
    BufferHandle handle = AllocateHandle();
    BufferMetadata& meta = m_buffers[handle.index];
    
    meta.desc = desc;
    meta.isAlive = true;
    meta.memoryUsed = desc.size;
    
    // Create NVRHI buffer
    nvrhi::BufferDesc nvrhiDesc;
    nvrhiDesc.byteSize = desc.size;
    nvrhiDesc.structStride = desc.stride;
    nvrhiDesc.debugName = desc.debugName.c_str();
    
    switch (desc.type) {
        case BufferType::Vertex:
            nvrhiDesc.isVertexBuffer = true;
            break;
        case BufferType::Index:
            nvrhiDesc.isIndexBuffer = true;
            break;
        case BufferType::Constant:
            nvrhiDesc.isConstantBuffer = true;
            break;
        case BufferType::Structured:
            nvrhiDesc.structStride = desc.stride;
            break;
        default:
            break;
    }
    
    if (desc.usage == BufferUsage::Dynamic) {
        nvrhiDesc.isVolatile = true;
        nvrhiDesc.cpuAccess = nvrhi::CpuAccessMode::Write;
    }
    
    if (desc.gpuWrite) {
        nvrhiDesc.canHaveUAVs = true;
    }
    
    meta.nvrhiBuffer = m_device->GetNativeDevice()->createBuffer(nvrhiDesc);
    
    if (!meta.nvrhiBuffer) {
        Msg("! [BufferManager] ❌ Failed to create buffer: %s",
            desc.debugName.c_str());
        FreeHandle(handle);
        return BufferHandle();
    }
    
    // Upload initial data
    if (initialData) {
        nvrhi::ICommandList* cmd = m_device->GetNativeDevice()->createCommandList();
        cmd->open();
        cmd->writeBuffer(meta.nvrhiBuffer, initialData, desc.size);
        cmd->close();
        m_device->GetNativeDevice()->executeCommandList(cmd);
    }
    
    Msg("! [BufferManager] Created buffer: %s (%llu KB)",
        desc.debugName.c_str(), desc.size / 1024);
    
    m_stats.buffersTotal++;
    m_stats.buffersStatic++;
    m_stats.staticMemoryUsed += desc.size;
    
    return handle;
}

// ═══════════════════════════════════════════════════
//  DYNAMIC ALLOCATION
// ═══════════════════════════════════════════════════

RingBuffer::Allocation BufferManager::AllocateDynamic(u64 size, u32 alignment) {
    // Use constant buffer ring by default
    // TODO: Route to appropriate ring based on usage
    auto alloc = m_constantBufferRing->Allocate(size, alignment);
    
    m_stats.dynamicAllocationsThisFrame++;
    m_stats.dynamicMemoryUsed += size;
    
    return alloc;
}

// ═══════════════════════════════════════════════════
//  FRAME MANAGEMENT
// ═══════════════════════════════════════════════════

void BufferManager::BeginFrame() {
    m_stats.dynamicAllocationsThisFrame = 0;
    m_stats.dynamicMemoryUsed = 0;
}

void BufferManager::EndFrame() {
    // Advance ring buffers
    m_constantBufferRing->AdvanceFrame();
    m_vertexBufferRing->AdvanceFrame();
    m_indexBufferRing->AdvanceFrame();
}

// ═══════════════════════════════════════════════════
//  ACCESS
// ═══════════════════════════════════════════════════

nvrhi::IBuffer* BufferManager::GetNVRHIBuffer(BufferHandle handle) {
    if (!ValidateHandle(handle)) return nullptr;
    
    BufferMetadata& meta = m_buffers[handle.index];
    meta.lastAccessTime = 0.0f;
    
    return meta.nvrhiBuffer.Get();
}

const BufferMetadata* BufferManager::GetMetadata(BufferHandle handle) const {
    if (!ValidateHandle(handle)) return nullptr;
    return &m_buffers[handle.index];
}

// ═══════════════════════════════════════════════════
//  HANDLE MANAGEMENT
// ═══════════════════════════════════════════════════

BufferHandle BufferManager::AllocateHandle() {
    u32 index;
    u32 generation;
    
    if (!m_freeSlots.empty()) {
        index = m_freeSlots.back();
        m_freeSlots.pop_back();
        generation = m_buffers[index].generation + 1;
        m_buffers[index].generation = generation;
    } else {
        index = (u32)m_buffers.size();
        generation = 0;
        m_buffers.emplace_back();
        m_buffers[index].generation = generation;
    }
    
    return BufferHandle(index, generation);
}

void BufferManager::FreeHandle(BufferHandle handle) {
    if (!ValidateHandle(handle)) return;
    
    BufferMetadata& meta = m_buffers[handle.index];
    meta.isAlive = false;
    m_freeSlots.push_back(handle.index);
}

bool BufferManager::ValidateHandle(BufferHandle handle) const {
    if (!handle.IsValid()) return false;
    if (handle.index >= m_buffers.size()) return false;
    
    const BufferMetadata& meta = m_buffers[handle.index];
    if (!meta.isAlive) return false;
    if (meta.generation != handle.generation) return false;
    
    return true;
}

// ═══════════════════════════════════════════════════
//  STATISTICS
// ═══════════════════════════════════════════════════

BufferManager::Statistics BufferManager::GetStatistics() const {
    m_stats.totalMemoryUsed = m_stats.staticMemoryUsed + m_stats.dynamicMemoryUsed;
    return m_stats;
}

void BufferManager::PrintStatistics() const {
    auto stats = GetStatistics();
    
    Msg("! [BufferManager] Statistics:");
    Msg("!   Memory: %llu MB static, %llu MB dynamic, %llu MB total",
        stats.staticMemoryUsed / (1024 * 1024),
        stats.dynamicMemoryUsed / (1024 * 1024),
        stats.totalMemoryUsed / (1024 * 1024));
    Msg("!   Buffers: %u total, %u static, %u dynamic allocs this frame",
        stats.buffersTotal,
        stats.buffersStatic,
        stats.dynamicAllocationsThisFrame);
}

} // namespace xray::render::resources
```

**Est:** 2 hours

**Deliverable:**
- [ ] Ring buffer implemented
- [ ] Can allocate dynamic per-frame data
- [ ] Static buffer creation working
- [ ] Memory tracking functional

---

### Afternoon (4 hours):

#### **Task 4.3: Sampler Cache**
**File:** `xrRender/ResourceManager/SamplerCache.h`

```cpp
#pragma once

#include "ResourceHandle.h"
#include <nvrhi/nvrhi.h>

namespace xray::render::resources {

// ═══════════════════════════════════════════════════
//  SAMPLER DESCRIPTOR
// ═══════════════════════════════════════════════════

struct SamplerDesc {
    enum class Filter {
        Point,
        Linear,
        Anisotropic
    };
    
    enum class AddressMode {
        Wrap,
        Clamp,
        Mirror,
        Border
    };
    
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Filter mipFilter = Filter::Linear;
    
    AddressMode addressU = AddressMode::Wrap;
    AddressMode addressV = AddressMode::Wrap;
    AddressMode addressW = AddressMode::Wrap;
    
    float mipLODBias = 0.0f;
    u32 maxAnisotropy = 16;
    
    float borderColor[4] = {0, 0, 0, 0};
    
    // Comparison (for shadow maps)
    bool enableComparison = false;
    
    // Generate hash for deduplication
    u64 GetHash() const;
    
    bool operator==(const SamplerDesc& other) const;
};

// ═══════════════════════════════════════════════════
//  SAMPLER CACHE
// ═══════════════════════════════════════════════════

class SamplerCache {
public:
    explicit SamplerCache(RenderDevice* device);
    ~SamplerCache();
    
    // Get or create sampler
    SamplerHandle GetSampler(const SamplerDesc& desc);
    
    // Get NVRHI sampler
    nvrhi::ISampler* GetNVRHISampler(SamplerHandle handle);
    
    // Common samplers (convenience)
    SamplerHandle GetLinearClamp();
    SamplerHandle GetLinearWrap();
    SamplerHandle GetPointClamp();
    SamplerHandle GetAnisotropicWrap();
    SamplerHandle GetShadowSampler();  // Comparison sampler
    
    // Statistics
    u32 GetCacheSize() const { return (u32)m_cache.size(); }
    
private:
    RenderDevice* m_device;
    
    // Hash → Sampler cache
    xr_map<u64, SamplerHandle> m_cache;
    
    // Handle → NVRHI sampler
    xr_vector<nvrhi::SamplerHandle> m_samplers;
    
    // Common samplers (cached on first use)
    SamplerHandle m_linearClamp;
    SamplerHandle m_linearWrap;
    SamplerHandle m_pointClamp;
    SamplerHandle m_anisotropicWrap;
    SamplerHandle m_shadowSampler;
};

} // namespace xray::render::resources
```

```cpp
// xrRender/ResourceManager/SamplerCache.cpp
#include "stdafx.h"
#include "SamplerCache.h"
#include "../RenderContext/RenderDevice.h"

namespace xray::render::resources {

u64 SamplerDesc::GetHash() const {
    // Simple hash combining all fields
    u64 hash = 0;
    hash = (hash * 31) + (u64)minFilter;
    hash = (hash * 31) + (u64)magFilter;
    hash = (hash * 31) + (u64)mipFilter;
    hash = (hash * 31) + (u64)addressU;
    hash = (hash * 31) + (u64)addressV;
    hash = (hash * 31) + (u64)addressW;
    hash = (hash * 31) + (u64)(mipLODBias * 1000.0f);
    hash = (hash * 31) + maxAnisotropy;
    hash = (hash * 31) + (enableComparison ? 1 : 0);
    return hash;
}

bool SamplerDesc::operator==(const SamplerDesc& other) const {
    return GetHash() == other.GetHash();
}

SamplerCache::SamplerCache(RenderDevice* device)
    : m_device(device)
{
    VERIFY(m_device);
    Msg("! [SamplerCache] Created");
}

SamplerCache::~SamplerCache() {
    Msg("! [SamplerCache] Destroyed (cached %u samplers)", GetCacheSize());
}

SamplerHandle SamplerCache::GetSampler(const SamplerDesc& desc) {
    u64 hash = desc.GetHash();
    
    // Check cache
    auto it = m_cache.find(hash);
    if (it != m_cache.end()) {
        return it->second;
    }
    
    // Create new sampler
    nvrhi::SamplerDesc nvrhiDesc;
    
    // Convert filter
    auto convertFilter = [](SamplerDesc::Filter f) {
        switch (f) {
            case SamplerDesc::Filter::Point: return false;
            case SamplerDesc::Filter::Linear: return true;
            case SamplerDesc::Filter::Anisotropic: return true;
            default: return true;
        }
    };
    
    nvrhiDesc.minFilter = convertFilter(desc.minFilter);
    nvrhiDesc.magFilter = convertFilter(desc.magFilter);
    nvrhiDesc.mipFilter = convertFilter(desc.mipFilter);
    
    // Convert address mode
    auto convertAddress = [](SamplerDesc::AddressMode a) -> nvrhi::SamplerAddressMode {
        switch (a) {
            case SamplerDesc::AddressMode::Wrap: 
                return nvrhi::SamplerAddressMode::Wrap;
            case SamplerDesc::AddressMode::Clamp: 
                return nvrhi::SamplerAddressMode::Clamp;
            case SamplerDesc::AddressMode::Mirror: 
                return nvrhi::SamplerAddressMode::Mirror;
            case SamplerDesc::AddressMode::Border: 
                return nvrhi::SamplerAddressMode::Border;
            default: 
                return nvrhi::SamplerAddressMode::Clamp;
        }
    };
    
    nvrhiDesc.addressU = convertAddress(desc.addressU);
    nvrhiDesc.addressV = convertAddress(desc.addressV);
    nvrhiDesc.addressW = convertAddress(desc.addressW);
    
    nvrhiDesc.mipBias = desc.mipLODBias;
    nvrhiDesc.maxAnisotropy = (desc.minFilter == SamplerDesc::Filter::Anisotropic) ? 
        desc.maxAnisotropy : 1;
    
    nvrhiDesc.borderColor = nvrhi::Color(
        desc.borderColor[0],
        desc.borderColor[1],
        desc.borderColor[2],
        desc.borderColor[3]
    );
    
    // Create sampler
    nvrhi::SamplerHandle nvrhiSampler = 
        m_device->GetNativeDevice()->createSampler(nvrhiDesc);
    
    if (!nvrhiSampler) {
        Msg("! [SamplerCache] ❌ Failed to create sampler");
        return SamplerHandle();
    }
    
    // Store
    u32 index = (u32)m_samplers.size();
    m_samplers.push_back(nvrhiSampler);
    
    SamplerHandle handle(index, 0);
    m_cache[hash] = handle;
    
    Msg("! [SamplerCache] Created sampler (hash=0x%llX, index=%u)", hash, index);
    
    return handle;
}

nvrhi::ISampler* SamplerCache::GetNVRHISampler(SamplerHandle handle) {
    if (!handle.IsValid() || handle.index >= m_samplers.size()) {
        return nullptr;
    }
    
    return m_samplers[handle.index].Get();
}

SamplerHandle SamplerCache::GetLinearClamp() {
    if (!m_linearClamp.IsValid()) {
        SamplerDesc desc;
        desc.minFilter = SamplerDesc::Filter::Linear;
        desc.magFilter = SamplerDesc::Filter::Linear;
        desc.addressU = SamplerDesc::AddressMode::Clamp;
        desc.addressV = SamplerDesc::AddressMode::Clamp;
        m_linearClamp = GetSampler(desc);
    }
    return m_linearClamp;
}

SamplerHandle SamplerCache::GetLinearWrap() {
    if (!m_linearWrap.IsValid()) {
        SamplerDesc desc;
        desc.minFilter = SamplerDesc::Filter::Linear;
        desc.magFilter = SamplerDesc::Filter::Linear;
        desc.addressU = SamplerDesc::AddressMode::Wrap;
        desc.addressV = SamplerDesc::AddressMode::Wrap;
        m_linearWrap = GetSampler(desc);
    }
    return m_linearWrap;
}

SamplerHandle SamplerCache::GetAnisotropicWrap() {
    if (!m_anisotropicWrap.IsValid()) {
        SamplerDesc desc;
        desc.minFilter = SamplerDesc::Filter::Anisotropic;
        desc.magFilter = SamplerDesc::Filter::Anisotropic;
        desc.maxAnisotropy = 16;
        desc.addressU = SamplerDesc::AddressMode::Wrap;
        desc.addressV = SamplerDesc::AddressMode::Wrap;
        m_anisotropicWrap = GetSampler(desc);
    }
    return m_anisotropicWrap;
}

} // namespace xray::render::resources
```

**Est:** 1.5 hours

---

#### **Task 4.4: Unified ResourceManager**
**File:** `xrRender/ResourceManager/ResourceManager.h`

```cpp
#pragma once

#include "TextureManager.h"
#include "BufferManager.h"
#include "SamplerCache.h"

namespace xray::render::resources {

// ═══════════════════════════════════════════════════
//  UNIFIED RESOURCE MANAGER
// ═══════════════════════════════════════════════════

class ResourceManager {
public:
    explicit ResourceManager(RenderDevice* device);
    ~ResourceManager();
    
    // ═══════════════════════════════════════════════════
    //  SUB-MANAGERS
    // ═══════════════════════════════════════════════════
    
    TextureManager* GetTextureManager() { return m_textureManager.get(); }
    BufferManager* GetBufferManager() { return m_bufferManager.get(); }
    SamplerCache* GetSamplerCache() { return m_samplerCache.get(); }
    
    // ═══════════════════════════════════════════════════
    //  FRAME MANAGEMENT
    // ═══════════════════════════════════════════════════
    
    void BeginFrame();
    void EndFrame();
    void Update(float deltaTime);
    
    // ═══════════════════════════════════════════════════
    //  STATISTICS
    // ═══════════════════════════════════════════════════
    
    struct Statistics {
        TextureManager::Statistics textures;
        BufferManager::Statistics buffers;
        u32 samplersCached;
        
        u64 totalMemoryUsed() const {
            return textures.totalMemoryUsed + buffers.totalMemoryUsed;
        }
    };
    
    Statistics GetStatistics() const;
    void PrintStatistics() const;
    
private:
    RenderDevice* m_device;
    
    xr_unique_ptr<TextureManager> m_textureManager;
    xr_unique_ptr<BufferManager> m_bufferManager;
    xr_unique_ptr<SamplerCache> m_samplerCache;
};

} // namespace xray::render::resources
```

```cpp
// xrRender/ResourceManager/ResourceManager.cpp
#include "stdafx.h"
#include "ResourceManager.h"

namespace xray::render::resources {

ResourceManager::ResourceManager(RenderDevice* device)
    : m_device(device)
{
    VERIFY(m_device);
    
    Msg("! [ResourceManager] Creating...");
    
    m_textureManager = xr_make_unique<TextureManager>(device);
    m_bufferManager = xr_make_unique<BufferManager>(device);
    m_samplerCache = xr_make_unique<SamplerCache>(device);
    
    Msg("! [ResourceManager] ✅ Created");
}

ResourceManager::~ResourceManager() {
    Msg("! [ResourceManager] Destroying...");
    PrintStatistics();
}

void ResourceManager::BeginFrame() {
    m_bufferManager->BeginFrame();
}

void ResourceManager::EndFrame() {
    m_bufferManager->EndFrame();
}

void ResourceManager::Update(float deltaTime) {
    m_textureManager->Update(deltaTime);
}

ResourceManager::Statistics ResourceManager::GetStatistics() const {
    Statistics stats;
    stats.textures = m_textureManager->GetStatistics();
    stats.buffers = m_bufferManager->GetStatistics();
    stats.samplersCached = m_samplerCache->GetCacheSize();
    return stats;
}

void ResourceManager::PrintStatistics() const {
    Msg("! [ResourceManager] === Statistics ===");
    m_textureManager->PrintStatistics();
    m_bufferManager->PrintStatistics();
    Msg("!   Samplers cached: %u", m_samplerCache->GetCacheSize());
    Msg("! [ResourceManager] Total Memory: %llu MB",
        GetStatistics().totalMemoryUsed() / (1024 * 1024));
}

} // namespace xray::render::resources
```

**Est:** 1 hour

**Deliverable:**
- [ ] Unified ResourceManager working
- [ ] All sub-managers accessible
- [ ] Statistics aggregation working
- [ ] Frame begin/end hooks in place

---

## 🎯 Week 2 Deliverables

By end of Week 2:
- [ ] Mip streaming system functional
- [ ] Memory budget enforcement working
- [ ] LRU eviction implemented
- [ ] BufferManager with ring buffers
- [ ] SamplerCache with deduplication
- [ ] Unified ResourceManager

**Next:** Week 3 will cover async I/O and Week 4-5 will cover integration with FrameGraph. Should I continue?
