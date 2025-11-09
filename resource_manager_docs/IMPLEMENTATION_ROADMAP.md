# 🚀 FGResourceManager - Implementation Progress & Roadmap

**Status:** 75% Complete (Phase 1 Complete, Critical Issues Identified)
**Goal:** Fully functional streaming resource system for FrameGraph renderer
**Timeline:** ~2-3 working days remaining
**Last Updated:** 2025-01-09

---

## 📊 Current State Assessment

### ✅ **COMPLETED INFRASTRUCTURE**
- [x] FGResourceManager integrated into RenderDevice
- [x] ps_r4_use_framegraph flag system working
- [x] FrameGraph receives FGResourceManager instance
- [x] Handle system (generational indices)
- [x] TextureManager **FULLY FUNCTIONAL**
- [x] DDSLoader (DDS parsing + multi-format support)
- [x] FGResourcePool structure
- [x] **Actual texture loading** (LoadTextureSync working!)
- [x] Multi-VFS search ($game_textures$ + $level$)
- [x] Video texture support (.ogm files via CTheoraSurface)
- [x] 3D texture support (volumetric textures)
- [x] UI texture rendering
- [x] World texture rendering
- [x] **UNIFIED TEXTURE PIPELINE** - All textures route through FGResourceManager!

### 🎯 **MAJOR MILESTONE ACHIEVED**
**Main Menu → Loading Screen → In-Game WORKING!**
- ✅ UI textures render correctly
- ✅ World textures render correctly
- ✅ All textures managed via FGResourceManager
- ✅ Proper texture dimension handling (no NaN/Inf UVs)
- ✅ Cross-renderer compatibility (GL builds successfully)

### ❌ **CRITICAL BUGS TO FIX**

#### 🔴 **Bug #1: Reference Count Underflow on Menu Close**
**Status:** BLOCKER
**Location:** `dx11SH_Texture.cpp:522` - `CTexture::Unload()`

**Issue:**
```
D3D11: DeviceChild reference counter underflow.
Release should not be called on objects with zero reference count.
```

**Root Cause:**
- When FrameGraph mode is enabled, NVRHI owns the D3D11 texture
- CTexture stores a raw pointer to the D3D11 texture via `getNativeObject()`
- When CTexture::Unload() is called, it tries to `Release()` the D3D11 texture
- But NVRHI already manages the ref count, so CTexture shouldn't call Release()

**Solution:**
Add FrameGraph mode check in CTexture::Unload() to skip Release() when NVRHI owns the texture.

**Callstack:**
```
CTexture::~CTexture() → CTexture::Unload() → pSurface->Release() [ERROR: RefCount=0]
```

---

#### 🟡 **Bug #2: Video Textures Don't Animate**
**Status:** HIGH
**Location:** `TextureManager::UpdateVideoTextures()`

**Issue:**
- .ogm video textures load and display first frame
- But subsequent frames don't update (video is frozen)

**Root Cause (Suspected):**
- `UpdateVideoTextures()` is being called each frame
- But video decoding or GPU upload may be failing silently
- Need to verify:
  - Is `DDSLoader::UpdateVideoFrame()` being called?
  - Is frame data actually changing?
  - Is `writeTexture()` succeeding?

**Debug Steps:**
1. Add logging to `UpdateVideoTextures()` to track frame updates
2. Verify `CTheoraSurface::Update()` returns true
3. Check if `needsUpdate` flag is being set
4. Verify `writeTexture()` isn't failing

---

#### 🟡 **Bug #3: No Font Rendering**
**Status:** HIGH
**Location:** Font rendering system

**Issue:**
- Fonts don't render in UI
- Need to route font rendering through FGResourceManager

**Root Cause:**
- `dxFontRender` likely uses legacy texture loading path
- Needs to be updated to use FGResourceManager like `dxUIShader`

**Solution:**
Similar fix to what we did for `dxUIShader`:
1. Check `ps_r4_use_framegraph` flag
2. Route font texture loading through FGResourceManager
3. Read dimensions from NVRHI textures

---

#### 🟡 **Bug #4: No Mouse/Cursor Rendering**
**Status:** MEDIUM
**Location:** Cursor rendering system

**Issue:**
- Mouse cursor doesn't render
- Need to investigate how cursor rendering works

**Research Needed:**
1. Find where cursor textures are loaded
2. Find where cursor is drawn
3. Verify if it's using legacy or FrameGraph path

---

### ⚠️ **DEFERRED FEATURES**
- [ ] Memory budget enforcement (eviction, LRU)
- [ ] Mip streaming upload (progressive detail)
- [ ] FrameGraph resource aliasing (memory optimization)
- [ ] AsyncIOManager (async file loading)
- [ ] StreamingManager (mip streaming)
- [ ] Integration test suite

**Rationale:** Core functionality works. These are optimizations that can be added incrementally.

---

## 🔧 **IMMEDIATE PRIORITIES**

### Priority 1: Fix Reference Count Underflow (BLOCKER)
**Estimated Time:** 30 minutes

The ref count crash prevents menu from being closed properly. This is the highest priority fix.

**Implementation:**
```cpp
// In dx11SH_Texture.cpp - CTexture::Unload()
void CTexture::Unload() {
    if (pSurface) {
        // CRITICAL: In FrameGraph mode, NVRHI owns the texture
        // CTexture just holds a raw pointer, so don't Release()
        extern ENGINE_API int ps_r4_use_framegraph;
        if (ps_r4_use_framegraph && RImplementation.m_renderDevice) {
            // NVRHI manages the ref count - don't call Release()
            pSurface = nullptr;
            return;
        }

        // Legacy path - we own it, so Release()
        pSurface->Release();
        pSurface = nullptr;
    }
}
```

---

### Priority 2: Debug Video Animation
**Estimated Time:** 1-2 hours

Add comprehensive logging to understand why video frames aren't updating.

**Debug Logging:**
```cpp
// In TextureManager::UpdateVideoTextures()
void TextureManager::UpdateVideoTextures() {
    static u32 s_debugFrameCount = 0;
    s_debugFrameCount++;

    for (auto& meta : m_textures) {
        if (!meta.isAlive || !meta.videoTextureData) continue;

        Msg("! [DEBUG] Frame %u: Updating video texture: %s",
            s_debugFrameCount, meta.filePath.c_str());

        bool frameChanged = DDSLoader::UpdateVideoFrame(*meta.videoTextureData, Device.dwTimeContinual);

        Msg("! [DEBUG] frameChanged=%s, needsUpdate=%s",
            frameChanged ? "YES" : "NO",
            meta.videoTextureData->videoState->needsUpdate ? "YES" : "NO");

        // ... rest of function
    }
}
```

---

### Priority 3: Font Rendering
**Estimated Time:** 2-3 hours

Investigate and fix font rendering similar to UI shader fix.

**Tasks:**
1. Find `dxFontRender` implementation
2. Identify texture loading path
3. Add FrameGraph mode check
4. Route through FGResourceManager
5. Update dimension reading

---

### Priority 4: Cursor Rendering
**Estimated Time:** 1-2 hours

Research and implement cursor rendering.

---

## 🎯 Implementation Plan (Original)

We'll complete this in **4 focused phases**, building incrementally:

### **Phase 1: Core Texture Loading (MVP)**
*Make textures actually load and display*
- Estimated: **8-10 hours**
- Deliverable: Can load DDS textures into NVRHI and use them

### **Phase 2: Memory Management**
*Prevent VRAM exhaustion*
- Estimated: **6-8 hours**
- Deliverable: Memory budget enforced, LRU eviction working

### **Phase 3: Mip Streaming**
*Progressive detail loading*
- Estimated: **8-10 hours**
- Deliverable: Can stream mip levels progressively

### **Phase 4: FrameGraph Integration & Aliasing**
*Memory optimization and validation*
- Estimated: **6-8 hours**
- Deliverable: Resource aliasing, full test suite

---

# 📝 Phase 1: Core Texture Loading (MVP)

**Goal:** Load textures from disk/memory and use them in FrameGraph passes.

---

## Task 1.1: Implement LoadTextureSync()

**File:** `src/Layers/xrRender/ResourceManager/TextureManager.cpp`

### Current State:
```cpp
void TextureManager::LoadTextureSync(TextureHandle handle) {
    // TODO: Implement
}
```

### Implementation:

```cpp
void TextureManager::LoadTextureSync(TextureHandle handle) {
    if (!ValidateHandle(handle)) return;

    TextureMetadata& meta = m_textures[handle.index];

    if (meta.state != TextureState::Unloaded) {
        // Already loaded or loading
        return;
    }

    meta.state = TextureState::Loading;

    // ═══════════════════════════════════════════════════
    //  STEP 1: LOAD DDS FILE FROM DISK
    // ═══════════════════════════════════════════════════

    DDSData ddsData;
    xr_vector<u8> fileBuffer;

    if (!DDSLoader::LoadFromFile(meta.filePath.c_str(), ddsData, fileBuffer)) {
        Msg("! [TextureManager] ❌ Failed to load DDS: %s", meta.filePath.c_str());
        meta.state = TextureState::Unloaded;
        return;
    }

    // ═══════════════════════════════════════════════════
    //  STEP 2: FILL TEXTURE DESCRIPTOR
    // ═══════════════════════════════════════════════════

    meta.desc.width = ddsData.width;
    meta.desc.height = ddsData.height;
    meta.desc.depth = ddsData.depth;
    meta.desc.format = ddsData.format;
    meta.desc.mipLevels = ddsData.mipLevels;
    meta.desc.arraySize = ddsData.arraySize;

    if (ddsData.isCubemap) {
        meta.desc.type = TextureDesc::TextureCube;
        meta.desc.arraySize = 6;
    } else if (ddsData.isVolume) {
        meta.desc.type = TextureDesc::Texture3D;
    } else {
        meta.desc.type = TextureDesc::Texture2D;
    }

    meta.desc.debugName = meta.filePath.c_str();
    meta.totalMips = ddsData.mipLevels;

    // ═══════════════════════════════════════════════════
    //  STEP 3: CREATE NVRHI TEXTURE DESCRIPTOR
    // ═══════════════════════════════════════════════════

    nvrhi::TextureDesc nvrhiDesc;
    nvrhiDesc.width = ddsData.width;
    nvrhiDesc.height = ddsData.height;
    nvrhiDesc.depth = ddsData.depth;
    nvrhiDesc.arraySize = meta.desc.arraySize;
    nvrhiDesc.mipLevels = ddsData.mipLevels;
    nvrhiDesc.format = ddsData.format;
    nvrhiDesc.debugName = meta.filePath.c_str();
    nvrhiDesc.isRenderTarget = false;
    nvrhiDesc.isUAV = false;
    nvrhiDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    nvrhiDesc.keepInitialState = true;

    // Set dimension
    if (ddsData.isCubemap) {
        nvrhiDesc.dimension = nvrhi::TextureDimension::TextureCube;
    } else if (ddsData.isVolume) {
        nvrhiDesc.dimension = nvrhi::TextureDimension::Texture3D;
    } else {
        nvrhiDesc.dimension = nvrhi::TextureDimension::Texture2D;
    }

    // ═══════════════════════════════════════════════════
    //  STEP 4: CREATE NVRHI TEXTURE (EMPTY)
    // ═══════════════════════════════════════════════════

    meta.nvrhiTexture = m_device->GetNativeDevice()->createTexture(nvrhiDesc);

    if (!meta.nvrhiTexture) {
        Msg("! [TextureManager] ❌ Failed to create NVRHI texture: %s", meta.filePath.c_str());
        meta.state = TextureState::Unloaded;
        return;
    }

    // ═══════════════════════════════════════════════════
    //  STEP 5: UPLOAD TEXTURE DATA TO GPU
    // ═══════════════════════════════════════════════════

    nvrhi::ICommandList* cmd = m_device->GetNativeDevice()->createCommandList();
    cmd->open();

    // Upload all mip levels
    for (u32 mipLevel = 0; mipLevel < ddsData.mipLevels; mipLevel++) {
        const DDSData::MipLevel& mipData = ddsData.mips[mipLevel];

        // For each array slice (1 for 2D, 6 for cubemaps)
        for (u32 arraySlice = 0; arraySlice < meta.desc.arraySize; arraySlice++) {
            // Calculate offset in file buffer for this slice
            // (For now, assume data is laid out sequentially)
            const u8* sliceData = mipData.data + (arraySlice * mipData.size / meta.desc.arraySize);

            cmd->writeTexture(
                meta.nvrhiTexture,
                arraySlice,      // array slice
                mipLevel,        // mip level
                sliceData,       // source data
                mipData.rowPitch,
                mipData.slicePitch
            );
        }
    }

    cmd->close();
    m_device->GetNativeDevice()->executeCommandList(cmd);

    // ═══════════════════════════════════════════════════
    //  STEP 6: UPDATE METADATA
    // ═══════════════════════════════════════════════════

    meta.state = TextureState::Resident;
    meta.residentMips = ddsData.mipLevels;
    meta.memoryUsed = ddsData.totalSize;

    // Update global memory counter
    m_memoryUsed += meta.memoryUsed;

    // Check if over budget
    if (m_memoryUsed > m_memoryBudget) {
        Msg("! [TextureManager] ⚠️ Over budget! (%llu / %llu MB)",
            m_memoryUsed / (1024 * 1024),
            m_memoryBudget / (1024 * 1024));

        // TODO Phase 2: Trigger eviction
    }

    Msg("! [TextureManager] ✅ Loaded: %s (%ux%u, %u mips, %llu KB)",
        meta.filePath.c_str(),
        meta.desc.width,
        meta.desc.height,
        meta.residentMips,
        meta.memoryUsed / 1024);
}
```

### Changes Required:
1. ✅ DDSLoader already handles file I/O
2. ✅ Need to handle cubemap array slicing properly
3. ⚠️ Need to calculate proper rowPitch/slicePitch in DDSLoader (currently simplified)

**Testing:**
```cpp
// In TestTextureManager.cpp
void TestBasicLoading() {
    TextureHandle handle = texManager->LoadTexture(
        "textures/concrete_diff.dds",
        TexturePriority::High
    );

    VERIFY(handle.IsValid());
    VERIFY(texManager->IsResident(handle));

    const TextureMetadata* meta = texManager->GetMetadata(handle);
    VERIFY(meta->state == TextureState::Resident);
    VERIFY(meta->nvrhiTexture != nullptr);

    Msg("✅ Basic loading test passed");
}
```

**Estimated Time:** 3 hours

---

## Task 1.2: Implement CreateTexture() (Runtime Creation)

**File:** `src/Layers/xrRender/ResourceManager/TextureManager.cpp`

### Purpose:
Create textures at runtime (render targets, UAVs) without loading from disk.

### Implementation:

```cpp
TextureHandle TextureManager::CreateTexture(
    const TextureDesc& desc,
    const void* initialData)
{
    // ═══════════════════════════════════════════════════
    //  STEP 1: ALLOCATE HANDLE
    // ═══════════════════════════════════════════════════

    TextureHandle handle = AllocateHandle();
    TextureMetadata& meta = m_textures[handle.index];

    // ═══════════════════════════════════════════════════
    //  STEP 2: FILL METADATA
    // ═══════════════════════════════════════════════════

    meta.desc = desc;
    meta.filePath = desc.debugName;
    meta.state = TextureState::Loading;
    meta.totalMips = desc.mipLevels;
    meta.isAlive = true;

    // ═══════════════════════════════════════════════════
    //  STEP 3: CREATE NVRHI TEXTURE DESC
    // ═══════════════════════════════════════════════════

    nvrhi::TextureDesc nvrhiDesc;
    nvrhiDesc.width = desc.width;
    nvrhiDesc.height = desc.height;
    nvrhiDesc.depth = desc.depth;
    nvrhiDesc.arraySize = desc.arraySize;
    nvrhiDesc.mipLevels = desc.mipLevels;
    nvrhiDesc.format = desc.format;
    nvrhiDesc.debugName = desc.debugName.c_str();

    // Set usage flags
    nvrhiDesc.isRenderTarget = desc.isRenderTarget;
    nvrhiDesc.isUAV = desc.isUAV;
    nvrhiDesc.isTypeless = false;  // TODO: Add to TextureDesc if needed

    // Set dimension
    switch (desc.type) {
        case TextureDesc::Texture1D:
            nvrhiDesc.dimension = nvrhi::TextureDimension::Texture1D;
            break;
        case TextureDesc::Texture2D:
        case TextureDesc::Texture2DArray:
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

    // Initial state
    if (desc.isRenderTarget) {
        nvrhiDesc.initialState = nvrhi::ResourceStates::RenderTarget;
    } else if (desc.isDepthStencil) {
        nvrhiDesc.initialState = nvrhi::ResourceStates::DepthWrite;
    } else if (desc.isUAV) {
        nvrhiDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    } else {
        nvrhiDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    }

    nvrhiDesc.keepInitialState = true;

    // ═══════════════════════════════════════════════════
    //  STEP 4: CREATE TEXTURE
    // ═══════════════════════════════════════════════════

    meta.nvrhiTexture = m_device->GetNativeDevice()->createTexture(nvrhiDesc);

    if (!meta.nvrhiTexture) {
        Msg("! [TextureManager] ❌ Failed to create runtime texture: %s", desc.debugName.c_str());
        FreeHandle(handle);
        return TextureHandle();
    }

    // ═══════════════════════════════════════════════════
    //  STEP 5: UPLOAD INITIAL DATA (IF PROVIDED)
    // ═══════════════════════════════════════════════════

    if (initialData) {
        nvrhi::ICommandList* cmd = m_device->GetNativeDevice()->createCommandList();
        cmd->open();

        // Calculate data size
        u64 dataSize = desc.CalculateMemorySize(0, desc.mipLevels);

        // Simple upload (assume single mip, can extend for multi-mip)
        cmd->writeTexture(
            meta.nvrhiTexture,
            0,  // array slice
            0,  // mip level
            initialData,
            desc.width * nvrhi::getFormatInfo(desc.format).bytesPerBlock,  // row pitch
            dataSize  // slice pitch
        );

        cmd->close();
        m_device->GetNativeDevice()->executeCommandList(cmd);
    }

    // ═══════════════════════════════════════════════════
    //  STEP 6: UPDATE METADATA
    // ═══════════════════════════════════════════════════

    meta.state = TextureState::Resident;
    meta.residentMips = desc.mipLevels;
    meta.memoryUsed = desc.CalculateMemorySize();

    // Mark as non-evictable if RT/DS
    if (desc.isRenderTarget || desc.isDepthStencil) {
        meta.priority = TexturePriority::Critical;
    }

    m_memoryUsed += meta.memoryUsed;
    m_stats.texturesTotal++;
    m_stats.texturesResident++;

    Msg("! [TextureManager] Created runtime texture: %s (%ux%u, %u mips, %llu KB)",
        desc.debugName.c_str(),
        desc.width,
        desc.height,
        desc.mipLevels,
        meta.memoryUsed / 1024);

    return handle;
}
```

**Testing:**
```cpp
void TestCreateTexture() {
    TextureDesc desc;
    desc.type = TextureDesc::Texture2D;
    desc.width = 1024;
    desc.height = 1024;
    desc.format = nvrhi::Format::RGBA8_UNORM;
    desc.mipLevels = 1;
    desc.isRenderTarget = true;
    desc.debugName = "TestRT";

    TextureHandle handle = texManager->CreateTexture(desc);

    VERIFY(handle.IsValid());
    VERIFY(texManager->IsResident(handle));

    nvrhi::ITexture* tex = texManager->GetNVRHITexture(handle);
    VERIFY(tex != nullptr);

    Msg("✅ CreateTexture test passed");
}
```

**Estimated Time:** 2 hours

---

## Task 1.3: Implement ImportTexture()

**File:** `src/Layers/xrRender/ResourceManager/TextureManager.cpp`

### Purpose:
Wrap externally-created NVRHI textures (e.g., backbuffer, swapchain).

### Implementation:

```cpp
TextureHandle TextureManager::ImportTexture(
    nvrhi::TextureHandle nvrhiTexture,
    const TextureDesc& desc,
    const char* debugName)
{
    if (!nvrhiTexture) {
        Msg("! [TextureManager] ❌ Cannot import null texture");
        return TextureHandle();
    }

    // ═══════════════════════════════════════════════════
    //  STEP 1: ALLOCATE HANDLE
    // ═══════════════════════════════════════════════════

    TextureHandle handle = AllocateHandle();
    TextureMetadata& meta = m_textures[handle.index];

    // ═══════════════════════════════════════════════════
    //  STEP 2: FILL METADATA
    // ═══════════════════════════════════════════════════

    meta.desc = desc;
    meta.filePath = debugName;
    meta.state = TextureState::Resident;
    meta.totalMips = desc.mipLevels;
    meta.residentMips = desc.mipLevels;
    meta.isAlive = true;

    // Imported textures are NEVER evicted
    meta.priority = TexturePriority::Critical;

    // ═══════════════════════════════════════════════════
    //  STEP 3: WRAP NVRHI TEXTURE
    // ═══════════════════════════════════════════════════

    meta.nvrhiTexture = nvrhiTexture;

    // ═══════════════════════════════════════════════════
    //  STEP 4: CALCULATE MEMORY SIZE
    // ═══════════════════════════════════════════════════

    meta.memoryUsed = desc.CalculateMemorySize();
    m_memoryUsed += meta.memoryUsed;

    m_stats.texturesTotal++;
    m_stats.texturesResident++;

    Msg("! [TextureManager] Imported texture: %s (%ux%u, %u mips, %llu KB)",
        debugName,
        desc.width,
        desc.height,
        desc.mipLevels,
        meta.memoryUsed / 1024);

    return handle;
}
```

**Usage Example:**
```cpp
// In FrameGraph setup
nvrhi::ITexture* backbuffer = swapChain->GetCurrentBackBuffer();

TextureDesc desc;
desc.width = swapChain->GetDesc().width;
desc.height = swapChain->GetDesc().height;
desc.format = swapChain->GetDesc().format;
desc.mipLevels = 1;
desc.isRenderTarget = true;

TextureHandle backbufferHandle = texManager->ImportTexture(
    backbuffer,
    desc,
    "Backbuffer"
);
```

**Estimated Time:** 1 hour

---

## Task 1.4: Fix DDSLoader RowPitch/SlicePitch Calculation

**File:** `src/Layers/xrRender/ResourceManager/DDSLoader.cpp`

### Current Issue:
```cpp
// Line ~1047 in week1.md plan
level.rowPitch = level.size / level.height;  // ❌ WRONG for compressed!
```

### Fix:

```cpp
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

        // ═══════════════════════════════════════════════════
        //  CORRECT PITCH CALCULATION
        // ═══════════════════════════════════════════════════

        const nvrhi::FormatInfo& formatInfo = nvrhi::getFormatInfo(data.format);

        if (formatInfo.isCompressed) {
            // Block-compressed formats (BC1-BC7)
            u32 blockWidth = (level.width + 3) / 4;
            u32 blockHeight = (level.height + 3) / 4;

            level.rowPitch = blockWidth * formatInfo.bytesPerBlock;
            level.slicePitch = level.rowPitch * blockHeight;
        } else {
            // Uncompressed formats
            level.rowPitch = level.width * formatInfo.bytesPerBlock;
            level.slicePitch = level.rowPitch * level.height;
        }

        data.totalSize += level.size;
        mipData += level.size;

        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
}
```

**Estimated Time:** 1 hour

---

## Task 1.5: Update LoadTexture() to Call LoadTextureSync()

**File:** `src/Layers/xrRender/ResourceManager/TextureManager.cpp`

### Current State:
```cpp
TextureHandle TextureManager::LoadTexture(const char* path, TexturePriority priority) {
    // ... deduplication ...

    TextureHandle handle = AllocateHandle();
    // ... setup metadata ...

    // TODO: Kick off async load
    // LoadTextureAsync(handle);

    return handle;  // ❌ Returns UNLOADED texture!
}
```

### Fix:

```cpp
TextureHandle TextureManager::LoadTexture(
    const char* path,
    TexturePriority priority)
{
    shared_str pathStr = path;

    // ═══════════════════════════════════════════════════
    //  STEP 1: CHECK IF ALREADY LOADED (DEDUPLICATION)
    // ═══════════════════════════════════════════════════

    auto it = m_pathToHandle.find(pathStr);
    if (it != m_pathToHandle.end()) {
        TextureHandle existing = it->second;
        if (ValidateHandle(existing)) {
            Msg("! [TextureManager] Texture already loaded: %s", path);
            AddRef(existing);
            return existing;
        } else {
            // Stale handle, remove
            m_pathToHandle.erase(it);
        }
    }

    // ═══════════════════════════════════════════════════
    //  STEP 2: ALLOCATE NEW HANDLE
    // ═══════════════════════════════════════════════════

    TextureHandle handle = AllocateHandle();
    TextureMetadata& meta = m_textures[handle.index];

    // Setup metadata
    meta.filePath = pathStr;
    meta.state = TextureState::Unloaded;
    meta.priority = priority;
    meta.isAlive = true;
    meta.refCount = 1;  // Initial reference

    // Register in path lookup
    m_pathToHandle[pathStr] = handle;

    // ═══════════════════════════════════════════════════
    //  STEP 3: LOAD SYNCHRONOUSLY (For Now)
    // ═══════════════════════════════════════════════════

    // TODO Phase 3: Make this async
    // For now, load synchronously to get MVP working
    LoadTextureSync(handle);

    m_stats.texturesTotal++;

    return handle;
}
```

**Estimated Time:** 0.5 hours

---

## Phase 1 Summary

**Total Estimated Time:** 8 hours

### Deliverables:
- ✅ Can load DDS textures from disk
- ✅ Can create runtime textures (RTs, UAVs)
- ✅ Can import external textures (backbuffer)
- ✅ Textures are usable in FrameGraph passes
- ✅ Basic memory tracking (no eviction yet)

### Testing Checklist:
```cpp
// TestTextureManager.cpp
void RunPhase1Tests() {
    TestBasicLoading();          // Load DDS from disk
    TestCreateTexture();         // Create RT
    TestImportTexture();         // Import backbuffer
    TestDeduplication();         // Same path = same handle
    TestMemoryTracking();        // Verify m_memoryUsed updates
}
```

**Next:** Phase 2 - Memory Management

---

# 📝 Phase 2: Memory Management

**Goal:** Enforce memory budget with LRU eviction.

---

## Task 2.1: Implement CheckMemoryBudget()

**File:** `src/Layers/xrRender/ResourceManager/TextureManager.cpp`

### Implementation:

```cpp
bool TextureManager::CheckMemoryBudget(u64 requiredBytes) const {
    return (m_memoryUsed + requiredBytes) <= m_memoryBudget;
}
```

**Estimated Time:** 0.25 hours

---

## Task 2.2: Implement EnforceMemoryBudget()

**File:** `src/Layers/xrRender/ResourceManager/TextureManager.cpp`

### Implementation:

```cpp
bool TextureManager::EnforceMemoryBudget(u64 requiredBytes) {
    // Already within budget?
    if (CheckMemoryBudget(requiredBytes)) {
        return true;
    }

    // Calculate how much we need to free
    u64 bytesNeeded = (m_memoryUsed + requiredBytes) - m_memoryBudget;

    Msg("! [TextureManager] Over budget! Need to free %llu MB",
        bytesNeeded / (1024 * 1024));

    // Attempt eviction
    if (EvictTextures(bytesNeeded)) {
        Msg("! [TextureManager] ✅ Successfully freed %llu MB",
            bytesNeeded / (1024 * 1024));
        return true;
    } else {
        Msg("! [TextureManager] ❌ Failed to free enough memory!");
        return false;
    }
}
```

**Estimated Time:** 0.5 hours

---

## Task 2.3: Implement EvictTextures() - LRU Algorithm

**File:** `src/Layers/xrRender/ResourceManager/TextureManager.cpp`

### Implementation:

```cpp
bool TextureManager::EvictTextures(u64 bytesNeeded) {
    u64 bytesFreed = 0;

    // ═══════════════════════════════════════════════════
    //  STEP 1: BUILD EVICTION CANDIDATE LIST
    // ═══════════════════════════════════════════════════

    struct EvictionCandidate {
        TextureHandle handle;
        float score;      // Higher = evict first
        u64 memoryUsed;

        bool operator<(const EvictionCandidate& other) const {
            return score > other.score;  // Descending order
        }
    };

    xr_vector<EvictionCandidate> candidates;
    candidates.reserve(m_textures.size() / 4);  // Estimate

    for (u32 i = 0; i < m_textures.size(); i++) {
        const TextureMetadata& meta = m_textures[i];

        if (!meta.isAlive) continue;
        if (!meta.CanEvict()) continue;  // Check priority, refCount

        // ═══════════════════════════════════════════════════
        //  CALCULATE EVICTION SCORE
        //  Higher score = more likely to evict
        // ═══════════════════════════════════════════════════

        float score = 0.0f;

        // Factor 1: Time since last access (LRU)
        // More time = higher score
        score += meta.lastAccessTime * 10.0f;

        // Factor 2: Priority (low priority = evict first)
        // VeryLow=4, Low=3, Medium=2, High=1, Critical=0
        score += (float)meta.priority * 100.0f;

        // Factor 3: Access frequency (less used = evict first)
        // Invert: fewer accesses = higher score
        score -= (float)meta.accessCount * 0.1f;

        // Factor 4: Memory size (larger = prefer to evict for space)
        score += (float)meta.memoryUsed / (1024.0f * 1024.0f);

        EvictionCandidate candidate;
        candidate.handle = TextureHandle(i, meta.generation);
        candidate.score = score;
        candidate.memoryUsed = meta.memoryUsed;

        candidates.push_back(candidate);
    }

    if (candidates.empty()) {
        Msg("! [TextureManager] ❌ No eviction candidates found!");
        return false;
    }

    // ═══════════════════════════════════════════════════
    //  STEP 2: SORT BY SCORE (HIGHEST FIRST)
    // ═══════════════════════════════════════════════════

    std::sort(candidates.begin(), candidates.end());

    Msg("! [TextureManager] Found %u eviction candidates", (u32)candidates.size());

    // ═══════════════════════════════════════════════════
    //  STEP 3: EVICT UNTIL WE HAVE ENOUGH SPACE
    // ═══════════════════════════════════════════════════

    for (const auto& candidate : candidates) {
        if (bytesFreed >= bytesNeeded) {
            break;  // Freed enough
        }

        const TextureMetadata* meta = GetMetadata(candidate.handle);
        if (!meta) continue;

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
```

**Estimated Time:** 3 hours

---

## Task 2.4: Implement EvictTextureInternal()

**File:** `src/Layers/xrRender/ResourceManager/TextureManager.cpp`

### Implementation:

```cpp
void TextureManager::EvictTextureInternal(TextureHandle handle) {
    if (!ValidateHandle(handle)) return;

    TextureMetadata& meta = m_textures[handle.index];

    if (meta.state != TextureState::Resident) {
        return;  // Already evicted or not loaded
    }

    // ═══════════════════════════════════════════════════
    //  STEP 1: RELEASE NVRHI TEXTURE
    // ═══════════════════════════════════════════════════

    if (meta.nvrhiTexture) {
        // NVRHI will destroy when ref count reaches zero
        meta.nvrhiTexture = nullptr;
    }

    // ═══════════════════════════════════════════════════
    //  STEP 2: UPDATE STATE
    // ═══════════════════════════════════════════════════

    meta.state = TextureState::Evicted;
    meta.residentMips = 0;

    // ═══════════════════════════════════════════════════
    //  STEP 3: UPDATE MEMORY TRACKING
    // ═══════════════════════════════════════════════════

    m_memoryUsed -= meta.memoryUsed;
    meta.memoryUsed = 0;

    m_stats.texturesResident--;
    m_stats.texturesEvicted++;

    Msg("! [TextureManager] Evicted: %s", meta.filePath.c_str());
}
```

**Estimated Time:** 1 hour

---

## Task 2.5: Update LoadTextureSync() to Enforce Budget

**File:** `src/Layers/xrRender/ResourceManager/TextureManager.cpp`

### Change:

```cpp
void TextureManager::LoadTextureSync(TextureHandle handle) {
    // ... existing DDS loading code ...

    // BEFORE creating NVRHI texture:

    // ═══════════════════════════════════════════════════
    //  CHECK MEMORY BUDGET
    // ═══════════════════════════════════════════════════

    u64 requiredMemory = ddsData.totalSize;

    if (!EnforceMemoryBudget(requiredMemory)) {
        Msg("! [TextureManager] ❌ Cannot load %s - out of memory!", meta.filePath.c_str());
        meta.state = TextureState::Unloaded;
        return;
    }

    // ... continue with NVRHI texture creation ...
}
```

**Estimated Time:** 0.5 hours

---

## Task 2.6: Implement Touch() and Update Access Tracking

**File:** `src/Layers/xrRender/ResourceManager/TextureManager.cpp`

### Implementation:

```cpp
void TextureManager::Touch(TextureHandle handle) {
    if (!ValidateHandle(handle)) return;

    TextureMetadata& meta = m_textures[handle.index];
    meta.lastAccessTime = 0.0f;  // Reset LRU timer
    meta.accessCount++;
}
```

### Update GetNVRHITexture():

```cpp
nvrhi::ITexture* TextureManager::GetNVRHITexture(TextureHandle handle) {
    if (!ValidateHandle(handle)) return nullptr;

    TextureMetadata& meta = m_textures[handle.index];

    // ═══════════════════════════════════════════════════
    //  AUTO-TOUCH FOR LRU
    // ═══════════════════════════════════════════════════

    meta.lastAccessTime = 0.0f;
    meta.accessCount++;

    // TODO: If unloaded, trigger reload
    if (meta.state == TextureState::Unloaded || meta.state == TextureState::Evicted) {
        Msg("! [TextureManager] ⚠️ Accessing evicted texture: %s - reloading...",
            meta.filePath.c_str());
        LoadTextureSync(handle);
    }

    return meta.nvrhiTexture.Get();
}
```

**Estimated Time:** 1 hour

---

## Task 2.7: Implement Update() to Age Textures

**File:** `src/Layers/xrRender/ResourceManager/TextureManager.cpp`

### Current State:
```cpp
void TextureManager::Update(float deltaTime) {
    // Stub
}
```

### Implementation:

```cpp
void TextureManager::Update(float deltaTime) {
    // ═══════════════════════════════════════════════════
    //  UPDATE LRU TIMERS
    // ═══════════════════════════════════════════════════

    for (auto& meta : m_textures) {
        if (!meta.isAlive) continue;

        // Age texture (increase time since last access)
        meta.lastAccessTime += deltaTime;
    }

    // ═══════════════════════════════════════════════════
    //  UPDATE STREAMING MANAGER
    // ═══════════════════════════════════════════════════

    if (m_streamingManager) {
        m_streamingManager->Update(deltaTime);
    }

    // ═══════════════════════════════════════════════════
    //  PROACTIVE EVICTION (OPTIONAL)
    // ═══════════════════════════════════════════════════

    // If very close to budget, proactively evict some textures
    float usagePercent = (float)m_memoryUsed / (float)m_memoryBudget;

    if (usagePercent > 0.95f) {  // 95% full
        Msg("! [TextureManager] ⚠️ Memory usage at %.1f%% - proactive eviction", usagePercent * 100.0f);

        // Free 10% of budget
        u64 targetFree = m_memoryBudget / 10;
        EvictTextures(targetFree);
    }
}
```

**Estimated Time:** 1 hour

---

## Phase 2 Summary

**Total Estimated Time:** 7 hours

### Deliverables:
- ✅ Memory budget enforced on all loads
- ✅ LRU eviction working (score-based)
- ✅ Textures auto-reload when accessed after eviction
- ✅ Proactive eviction at 95% full
- ✅ Access tracking (lastAccessTime, accessCount)

### Testing Checklist:
```cpp
void RunPhase2Tests() {
    TestMemoryBudgetEnforcement();  // Load until over budget
    TestLRUEviction();              // Least recent gets evicted
    TestAutoReload();               // Evicted texture reloads on access
    TestProactiveEviction();        // Eviction at 95% full
}
```

**Next:** Phase 3 - Mip Streaming

---

# 📝 Phase 3: Mip Streaming

**Goal:** Progressive texture detail loading.

---

## Task 3.1: Implement LoadMipsFromDisk() - Complete

**File:** `src/Layers/xrRender/ResourceManager/TextureStreaming.cpp`

### Current State:
Partial async structure, incomplete implementation.

### Full Implementation:

```cpp
bool StreamingManager::LoadMipsFromDisk(StreamingRequest& request) {
    const TextureMetadata* meta = m_texManager->GetMetadata(request.handle);
    if (!meta) {
        return false;
    }

    // ═══════════════════════════════════════════════════
    //  ASYNC I/O VERSION
    // ═══════════════════════════════════════════════════

    if (!request.ioHandle) {
        // ───────────────────────────────────────────────
        //  FIRST CALL: KICK OFF ASYNC READ
        // ───────────────────────────────────────────────

        Msg("! [StreamingManager] Starting async load: %s (mips %u→%u)",
            meta->filePath.c_str(),
            request.currentMips,
            request.targetMips);

        // Submit async read
        u32 requestID = m_asyncIO->ReadAsync(
            meta->filePath.c_str(),
            0,  // offset (read whole file for now)
            0,  // size (0 = read entire file)
            [this, request](AsyncIORequest& ioRequest) mutable {
                // Callback when complete
                this->OnAsyncLoadComplete(request.handle, ioRequest);
            },
            (void*)(uintptr_t)request.handle.index
        );

        request.ioHandle = (void*)(uintptr_t)requestID;
        return false;  // Not complete yet

    } else {
        // ───────────────────────────────────────────────
        //  SUBSEQUENT CALLS: CHECK IF I/O COMPLETE
        // ───────────────────────────────────────────────

        u32 requestID = (u32)(uintptr_t)request.ioHandle;

        if (m_asyncIO->IsRequestComplete(requestID)) {
            // Data is in staging buffer (filled by callback)
            return true;
        }

        return false;  // Still in progress
    }
}
```

**Estimated Time:** 2 hours

---

## Task 3.2: Implement OnAsyncLoadComplete()

**File:** `src/Layers/xrRender/ResourceManager/TextureStreaming.cpp`

### Implementation:

```cpp
void StreamingManager::OnAsyncLoadComplete(
    TextureHandle handle,
    AsyncIORequest& ioRequest)
{
    Msg("! [StreamingManager] Async load complete: handle=%u.%u, status=%d, size=%llu KB",
        handle.index,
        handle.generation,
        (int)ioRequest.status,
        ioRequest.buffer.size() / 1024);

    if (ioRequest.status != IOStatus::Complete) {
        // Failed - mark request as failed
        for (auto& request : m_activeRequests) {
            if (request.handle == handle) {
                request.status = StreamingRequest::Failed;
                Msg("! [StreamingManager] ❌ Load failed: %s", ioRequest.errorMessage.c_str());
                break;
            }
        }
        return;
    }

    // ═══════════════════════════════════════════════════
    //  PARSE DDS DATA
    // ═══════════════════════════════════════════════════

    DDSData ddsData;

    if (!DDSLoader::LoadFromMemory(ioRequest.buffer.data(), (u32)ioRequest.buffer.size(), ddsData)) {
        Msg("! [StreamingManager] ❌ Failed to parse DDS");

        for (auto& request : m_activeRequests) {
            if (request.handle == handle) {
                request.status = StreamingRequest::Failed;
                break;
            }
        }
        return;
    }

    // ═══════════════════════════════════════════════════
    //  EXTRACT MIP RANGE
    // ═══════════════════════════════════════════════════

    // Find corresponding request
    for (auto& request : m_activeRequests) {
        if (request.handle == handle) {
            // Clear staging buffer
            request.stagingBuffer.clear();

            // Copy desired mip range to staging buffer
            u32 startMip = request.currentMips;
            u32 endMip = std::min(request.targetMips, (u32)ddsData.mips.size());

            for (u32 mip = startMip; mip < endMip; mip++) {
                const DDSData::MipLevel& mipData = ddsData.mips[mip];

                u32 offset = (u32)request.stagingBuffer.size();
                request.stagingBuffer.resize(offset + mipData.size);

                memcpy(
                    request.stagingBuffer.data() + offset,
                    mipData.data,
                    mipData.size
                );
            }

            // Move to uploading state
            request.status = StreamingRequest::Uploading;

            Msg("! [StreamingManager] Extracted mips %u→%u (%llu KB)",
                startMip,
                endMip,
                request.stagingBuffer.size() / 1024);

            break;
        }
    }
}
```

**Estimated Time:** 2 hours

---

## Task 3.3: Implement UploadMipsToGPU()

**File:** `src/Layers/xrRender/ResourceManager/TextureStreaming.cpp`

### Current State:
Stub with bandwidth check.

### Full Implementation:

```cpp
bool StreamingManager::UploadMipsToGPU(StreamingRequest& request) {
    TextureMetadata* meta = const_cast<TextureMetadata*>(
        m_texManager->GetMetadata(request.handle)
    );

    if (!meta || !meta->nvrhiTexture) {
        Msg("! [StreamingManager] ❌ Invalid texture for upload");
        return false;
    }

    Msg("! [StreamingManager] Uploading mips to GPU: %s (%llu KB)",
        meta->filePath.c_str(),
        request.stagingBuffer.size() / 1024);

    // ═══════════════════════════════════════════════════
    //  CHECK BANDWIDTH LIMIT
    // ═══════════════════════════════════════════════════

    if (m_stats.bytesStreamedThisFrame + request.stagingBuffer.size() > m_bandwidthLimit) {
        // Defer to next frame
        Msg("! [StreamingManager] ⚠️ Bandwidth limit reached - deferring upload");
        return false;
    }

    // ═══════════════════════════════════════════════════
    //  RELOAD DDS TO GET MIP METADATA
    // ═══════════════════════════════════════════════════

    // We need mip dimensions/pitches - reload DDS header
    DDSData ddsData;
    xr_vector<u8> fileBuffer;

    if (!DDSLoader::LoadFromFile(meta->filePath.c_str(), ddsData, fileBuffer)) {
        Msg("! [StreamingManager] ❌ Failed to reload DDS for metadata");
        return false;
    }

    // ═══════════════════════════════════════════════════
    //  UPLOAD VIA NVRHI
    // ═══════════════════════════════════════════════════

    nvrhi::ICommandList* cmd = m_device->GetNativeDevice()->createCommandList();
    cmd->open();

    u32 startMip = request.currentMips;
    u32 endMip = request.targetMips;
    u64 bufferOffset = 0;

    for (u32 mipLevel = startMip; mipLevel < endMip; mipLevel++) {
        if (mipLevel >= ddsData.mips.size()) break;

        const DDSData::MipLevel& mipData = ddsData.mips[mipLevel];

        // Get data from staging buffer
        const u8* srcData = request.stagingBuffer.data() + bufferOffset;

        // Upload to GPU
        cmd->writeTexture(
            meta->nvrhiTexture,
            0,  // array slice (TODO: handle arrays/cubemaps)
            mipLevel,
            srcData,
            mipData.rowPitch,
            mipData.slicePitch
        );

        bufferOffset += mipData.size;
    }

    cmd->close();
    m_device->GetNativeDevice()->executeCommandList(cmd);

    // ═══════════════════════════════════════════════════
    //  UPDATE METADATA
    // ═══════════════════════════════════════════════════

    meta->residentMips = request.targetMips;

    m_stats.bytesStreamedThisFrame += request.stagingBuffer.size();
    m_stats.bytesStreamedTotal += request.stagingBuffer.size();

    Msg("! [StreamingManager] ✅ Upload complete (resident mips: %u)", meta->residentMips);

    return true;
}
```

**Estimated Time:** 3 hours

---

## Task 3.4: Implement Partial DDS Loading (Optimization)

**File:** `src/Layers/xrRender/ResourceManager/DDSLoader.h`

### Add New Function:

```cpp
class DDSLoader {
public:
    // ... existing functions ...

    // Load specific mip range (optimization)
    static bool LoadMipRange(
        const char* path,
        u32 startMip,
        u32 mipCount,
        DDSData& outData,
        xr_vector<u8>& outBuffer
    );
};
```

### Implementation:

```cpp
// DDSLoader.cpp
bool DDSLoader::LoadMipRange(
    const char* path,
    u32 startMip,
    u32 mipCount,
    DDSData& outData,
    xr_vector<u8>& outBuffer)
{
    // ═══════════════════════════════════════════════════
    //  STEP 1: READ HEADER
    // ═══════════════════════════════════════════════════

    IReader* reader = FS.r_open(path);
    if (!reader) {
        Msg("! [DDSLoader] ❌ Failed to open: %s", path);
        return false;
    }

    DDSHeader header;
    reader->r(&header, sizeof(DDSHeader));

    if (!ValidateHeader(header)) {
        FS.r_close(reader);
        return false;
    }

    // ═══════════════════════════════════════════════════
    //  STEP 2: CALCULATE FILE OFFSET FOR START MIP
    // ═══════════════════════════════════════════════════

    nvrhi::Format format = GetFormatFromFourCC(header.pixelFormat.fourCC);

    u32 width = header.width;
    u32 height = header.height;
    u64 fileOffset = sizeof(DDSHeader);

    // Skip mips before startMip
    for (u32 mip = 0; mip < startMip; mip++) {
        u32 mipSize = CalculateMipSize(
            std::max(1u, width >> mip),
            std::max(1u, height >> mip),
            format
        );

        fileOffset += mipSize;
    }

    // ═══════════════════════════════════════════════════
    //  STEP 3: CALCULATE TOTAL SIZE FOR MIP RANGE
    // ═══════════════════════════════════════════════════

    u64 totalSize = 0;

    for (u32 mip = startMip; mip < startMip + mipCount; mip++) {
        u32 mipSize = CalculateMipSize(
            std::max(1u, width >> mip),
            std::max(1u, height >> mip),
            format
        );

        totalSize += mipSize;
    }

    // ═══════════════════════════════════════════════════
    //  STEP 4: READ MIP DATA
    // ═══════════════════════════════════════════════════

    reader->seek(fileOffset);

    outBuffer.resize(totalSize);
    reader->r(outBuffer.data(), totalSize);

    FS.r_close(reader);

    // ═══════════════════════════════════════════════════
    //  STEP 5: FILL DDSData
    // ═══════════════════════════════════════════════════

    outData.width = width;
    outData.height = height;
    outData.format = format;
    outData.mipLevels = mipCount;
    outData.totalSize = totalSize;

    // Parse mip levels
    const u8* mipDataPtr = outBuffer.data();

    for (u32 mip = 0; mip < mipCount; mip++) {
        DDSData::MipLevel level;

        u32 mipWidth = std::max(1u, width >> (startMip + mip));
        u32 mipHeight = std::max(1u, height >> (startMip + mip));

        level.width = mipWidth;
        level.height = mipHeight;
        level.data = mipDataPtr;
        level.size = CalculateMipSize(mipWidth, mipHeight, format);

        const nvrhi::FormatInfo& formatInfo = nvrhi::getFormatInfo(format);

        if (formatInfo.isCompressed) {
            u32 blockWidth = (mipWidth + 3) / 4;
            u32 blockHeight = (mipHeight + 3) / 4;
            level.rowPitch = blockWidth * formatInfo.bytesPerBlock;
            level.slicePitch = level.rowPitch * blockHeight;
        } else {
            level.rowPitch = mipWidth * formatInfo.bytesPerBlock;
            level.slicePitch = level.rowPitch * mipHeight;
        }

        outData.mips.push_back(level);
        mipDataPtr += level.size;
    }

    Msg("! [DDSLoader] ✅ Loaded mip range %u→%u (%llu KB)",
        startMip,
        startMip + mipCount,
        totalSize / 1024);

    return true;
}
```

### Update StreamingManager to Use It:

```cpp
// In OnAsyncLoadComplete(), replace full file load with:
DDSData ddsData;
u32 mipCount = request.targetMips - request.currentMips;

if (!DDSLoader::LoadMipRange(
        meta->filePath.c_str(),
        request.currentMips,
        mipCount,
        ddsData,
        request.stagingBuffer))
{
    // Error handling...
}
```

**Estimated Time:** 3 hours

---

## Phase 3 Summary

**Total Estimated Time:** 10 hours

### Deliverables:
- ✅ Async mip loading working
- ✅ GPU upload with bandwidth limiting
- ✅ Partial DDS loading (optimization)
- ✅ Progressive detail loading functional
- ✅ Streaming manager fully operational

### Testing Checklist:
```cpp
void RunPhase3Tests() {
    TestBasicStreaming();       // Load base mips, stream higher
    TestBandwidthLimit();       // Respect bytes/frame limit
    TestPartialLoading();       // Only read needed mips
    TestProgressiveDetail();    // Visible quality improvement
}
```

**Next:** Phase 4 - FrameGraph Integration & Polish

---

# 📝 Phase 4: FrameGraph Integration & Aliasing

**Goal:** Resource pooling, aliasing, and validation.

---

## Task 4.1: Implement FGResourcePool Aliasing

**File:** `src/Layers/xrRender/FrameGraph/FGResourcePool.cpp`

### Complete Implementation:

```cpp
resources::TextureHandle FGResourcePool::AllocateTexture(const resources::TextureDesc& desc) {
    m_stats.texturesAllocated++;

    // ═══════════════════════════════════════════════════
    //  TRY TO FIND COMPATIBLE TEXTURE IN POOL
    // ═══════════════════════════════════════════════════

    if (m_aliasingEnabled) {
        resources::TextureHandle pooled = FindCompatibleTexture(desc);

        if (pooled.IsValid()) {
            m_stats.texturesAliased++;
            m_stats.memorySaved += desc.CalculateMemorySize();

            Msg("! [FGResourcePool] ✅ Aliased texture: %s (saved %llu KB)",
                desc.debugName.c_str(),
                desc.CalculateMemorySize() / 1024);

            return pooled;
        }
    }

    // ═══════════════════════════════════════════════════
    //  NO COMPATIBLE TEXTURE - ALLOCATE NEW
    // ═══════════════════════════════════════════════════

    resources::TextureHandle handle =
        m_resourceManager->GetTextureManager()->CreateTexture(desc);

    if (!handle.IsValid()) {
        Msg("! [FGResourcePool] ❌ Failed to allocate texture: %s", desc.debugName.c_str());
        return resources::TextureHandle();
    }

    m_stats.texturesActive++;
    m_stats.memoryAllocated += desc.CalculateMemorySize();

    Msg("! [FGResourcePool] Allocated new texture: %s (%ux%u, %llu KB)",
        desc.debugName.c_str(),
        desc.width,
        desc.height,
        desc.CalculateMemorySize() / 1024);

    return handle;
}
```

**Estimated Time:** 1.5 hours

---

## Task 4.2: Implement FindCompatibleTexture()

**File:** `src/Layers/xrRender/FrameGraph/FGResourcePool.cpp`

### Implementation:

```cpp
resources::TextureHandle FGResourcePool::FindCompatibleTexture(const resources::TextureDesc& desc) {
    for (auto& pooled : m_texturePool) {
        if (!pooled.inUse && AreTexturesCompatible(pooled.desc, desc)) {
            pooled.inUse = true;
            pooled.lastUsedFrame = m_currentFrame;

            Msg("! [FGResourcePool] Found compatible texture: %s → %s",
                pooled.desc.debugName.c_str(),
                desc.debugName.c_str());

            return pooled.handle;
        }
    }

    return resources::TextureHandle();  // Not found
}
```

**Estimated Time:** 0.5 hours

---

## Task 4.3: Implement AreTexturesCompatible()

**File:** `src/Layers/xrRender/FrameGraph/FGResourcePool.cpp`

### Implementation:

```cpp
bool FGResourcePool::AreTexturesCompatible(
    const resources::TextureDesc& a,
    const resources::TextureDesc& b) const
{
    // ═══════════════════════════════════════════════════
    //  EXACT MATCH REQUIRED
    // ═══════════════════════════════════════════════════

    // Dimensions must match
    if (a.width != b.width) return false;
    if (a.height != b.height) return false;
    if (a.depth != b.depth) return false;

    // Format must match
    if (a.format != b.format) return false;

    // Type must match
    if (a.type != b.type) return false;

    // Mip count must match (or B can have more mips than A)
    if (b.mipLevels > a.mipLevels) return false;

    // Array size must match
    if (a.arraySize != b.arraySize) return false;

    // Usage flags must be compatible
    // B's requirements must be subset of A's capabilities
    if (b.isRenderTarget && !a.isRenderTarget) return false;
    if (b.isDepthStencil && !a.isDepthStencil) return false;
    if (b.isUAV && !a.isUAV) return false;

    // ═══════════════════════════════════════════════════
    //  RELAXED MATCHING (OPTIONAL)
    // ═══════════════════════════════════════════════════

    // Could allow reusing larger textures for smaller requests:
    // if (a.width >= b.width && a.height >= b.height) { ... }
    // But for now, require exact match for safety

    return true;
}
```

**Estimated Time:** 1 hour

---

## Task 4.4: Implement FreeTexture() and Reset()

**File:** `src/Layers/xrRender/FrameGraph/FGResourcePool.cpp`

### Implementation:

```cpp
void FGResourcePool::FreeTexture(resources::TextureHandle handle) {
    if (!handle.IsValid()) return;

    const resources::TextureMetadata* meta =
        m_resourceManager->GetTextureManager()->GetMetadata(handle);

    if (!meta) return;

    // ═══════════════════════════════════════════════════
    //  ADD TO POOL FOR POTENTIAL REUSE
    // ═══════════════════════════════════════════════════

    if (m_aliasingEnabled) {
        PooledTexture pooled;
        pooled.handle = handle;
        pooled.desc = meta->desc;
        pooled.inUse = false;
        pooled.lastUsedFrame = m_currentFrame;

        m_texturePool.push_back(pooled);

        Msg("! [FGResourcePool] Freed texture to pool: %s", meta->filePath.c_str());
    } else {
        // Aliasing disabled - actually destroy
        m_resourceManager->GetTextureManager()->Release(handle);
        m_stats.texturesActive--;
    }
}

void FGResourcePool::Reset() {
    // ═══════════════════════════════════════════════════
    //  RELEASE ALL POOLED TEXTURES
    // ═══════════════════════════════════════════════════

    for (auto& pooled : m_texturePool) {
        m_resourceManager->GetTextureManager()->Release(pooled.handle);
    }

    m_texturePool.clear();
    m_stats.texturesActive = 0;

    m_currentFrame++;

    Msg("! [FGResourcePool] Reset (frame %u)", m_currentFrame);
}
```

**Estimated Time:** 1 hour

---

## Task 4.5: Implement Test Suite

**File:** `src/Layers/xrRender/ResourceManager/TestTextureManager.cpp`

### Comprehensive Test Implementation:

```cpp
#include "stdafx.h"
#include "TestTextureManager.h"
#include "ModernResourceManager.h"
#include "../RenderContext/RenderDevice.h"

namespace xray::render::resources {

// ═══════════════════════════════════════════════════
//  TEST 1: BASIC LOADING
// ═══════════════════════════════════════════════════

void TestTextureManager::TestBasicLoading() {
    Msg("═══ TEST 1: Basic Loading ═══");

    // Load single texture
    TextureHandle handle = m_texManager->LoadTexture(
        "textures\\concrete_diff.dds",
        TexturePriority::High
    );

    VERIFY(handle.IsValid());
    VERIFY(m_texManager->IsResident(handle));

    const TextureMetadata* meta = m_texManager->GetMetadata(handle);
    VERIFY(meta);
    VERIFY(meta->state == TextureState::Resident);
    VERIFY(meta->nvrhiTexture != nullptr);

    nvrhi::ITexture* tex = m_texManager->GetNVRHITexture(handle);
    VERIFY(tex != nullptr);

    m_texManager->Release(handle);

    Msg("✅ TestBasicLoading PASSED");
}

// ═══════════════════════════════════════════════════
//  TEST 2: DEDUPLICATION
// ═══════════════════════════════════════════════════

void TestTextureManager::TestDeduplication() {
    Msg("═══ TEST 2: Deduplication ═══");

    TextureHandle handle1 = m_texManager->LoadTexture(
        "textures\\concrete_diff.dds",
        TexturePriority::High
    );

    TextureHandle handle2 = m_texManager->LoadTexture(
        "textures\\concrete_diff.dds",  // Same path
        TexturePriority::High
    );

    // Should be same handle
    VERIFY(handle1.index == handle2.index);
    VERIFY(handle1.generation == handle2.generation);

    const TextureMetadata* meta = m_texManager->GetMetadata(handle1);
    VERIFY(meta->refCount == 2);  // Two references

    m_texManager->Release(handle1);
    m_texManager->Release(handle2);

    Msg("✅ TestDeduplication PASSED");
}

// ═══════════════════════════════════════════════════
//  TEST 3: MEMORY BUDGET ENFORCEMENT
// ═══════════════════════════════════════════════════

void TestTextureManager::TestMemoryBudget() {
    Msg("═══ TEST 3: Memory Budget ═══");

    // Set tight budget
    m_texManager->SetMemoryBudget(100 * 1024 * 1024);  // 100 MB

    xr_vector<TextureHandle> handles;

    // Load textures until over budget
    for (u32 i = 0; i < 50; i++) {
        string256 path;
        xr_sprintf(path, "textures\\test_%03u.dds", i);

        TextureHandle handle = m_texManager->LoadTexture(path);

        if (handle.IsValid()) {
            handles.push_back(handle);
        }
    }

    auto stats = m_texManager->GetStatistics();

    // Should be within budget
    VERIFY(stats.totalMemoryUsed <= stats.memoryBudget);

    // Should have evicted some textures
    VERIFY(stats.texturesEvicted > 0);

    Msg("  Loaded: %u textures", (u32)handles.size());
    Msg("  Evicted: %u textures", stats.texturesEvicted);
    Msg("  Memory: %llu / %llu MB",
        stats.totalMemoryUsed / (1024 * 1024),
        stats.memoryBudget / (1024 * 1024));

    // Cleanup
    for (auto handle : handles) {
        m_texManager->Release(handle);
    }

    Msg("✅ TestMemoryBudget PASSED");
}

// ═══════════════════════════════════════════════════
//  TEST 4: LRU EVICTION
// ═══════════════════════════════════════════════════

void TestTextureManager::TestLRUEviction() {
    Msg("═══ TEST 4: LRU Eviction ═══");

    m_texManager->SetMemoryBudget(50 * 1024 * 1024);  // 50 MB

    xr_vector<TextureHandle> handles;

    // Load 20 textures
    for (u32 i = 0; i < 20; i++) {
        string256 path;
        xr_sprintf(path, "textures\\test_%03u.dds", i);

        TextureHandle handle = m_texManager->LoadTexture(path);
        if (handle.IsValid()) {
            handles.push_back(handle);
        }
    }

    // Touch first texture repeatedly (make it most recently used)
    for (u32 i = 0; i < 10; i++) {
        m_texManager->Touch(handles[0]);
    }

    // Load more textures (should trigger eviction)
    for (u32 i = 20; i < 40; i++) {
        string256 path;
        xr_sprintf(path, "textures\\test_%03u.dds", i);

        TextureHandle handle = m_texManager->LoadTexture(path);
        if (handle.IsValid()) {
            handles.push_back(handle);
        }
    }

    // First texture should still be resident (frequently accessed)
    VERIFY(m_texManager->IsResident(handles[0]));

    // Some others should be evicted
    u32 evictedCount = 0;
    for (u32 i = 1; i < 20; i++) {
        if (!m_texManager->IsResident(handles[i])) {
            evictedCount++;
        }
    }

    VERIFY(evictedCount > 0);

    Msg("  First texture: %s", m_texManager->IsResident(handles[0]) ? "resident" : "evicted");
    Msg("  Evicted: %u / 19 textures", evictedCount);

    // Cleanup
    for (auto handle : handles) {
        m_texManager->Release(handle);
    }

    Msg("✅ TestLRUEviction PASSED");
}

// ═══════════════════════════════════════════════════
//  TEST 5: CREATE RUNTIME TEXTURE
// ═══════════════════════════════════════════════════

void TestTextureManager::TestCreateTexture() {
    Msg("═══ TEST 5: Create Runtime Texture ═══");

    TextureDesc desc;
    desc.type = TextureDesc::Texture2D;
    desc.width = 1024;
    desc.height = 1024;
    desc.format = nvrhi::Format::RGBA8_UNORM;
    desc.mipLevels = 1;
    desc.isRenderTarget = true;
    desc.debugName = "TestRT";

    TextureHandle handle = m_texManager->CreateTexture(desc);

    VERIFY(handle.IsValid());
    VERIFY(m_texManager->IsResident(handle));

    nvrhi::ITexture* tex = m_texManager->GetNVRHITexture(handle);
    VERIFY(tex != nullptr);

    const nvrhi::TextureDesc& nvrhiDesc = tex->getDesc();
    VERIFY(nvrhiDesc.width == 1024);
    VERIFY(nvrhiDesc.height == 1024);
    VERIFY(nvrhiDesc.isRenderTarget);

    m_texManager->Release(handle);

    Msg("✅ TestCreateTexture PASSED");
}

// ═══════════════════════════════════════════════════
//  TEST RUNNER
// ═══════════════════════════════════════════════════

void TestTextureManager::RunAllTests() {
    Msg("╔═══════════════════════════════════════════════╗");
    Msg("║   ModernResourceManager Test Suite          ║");
    Msg("╚═══════════════════════════════════════════════╝");

    try {
        TestBasicLoading();
        TestDeduplication();
        TestMemoryBudget();
        TestLRUEviction();
        TestCreateTexture();

        Msg("");
        Msg("╔═══════════════════════════════════════════════╗");
        Msg("║   ✅ ALL TESTS PASSED!                       ║");
        Msg("╚═══════════════════════════════════════════════╝");

    } catch (const std::exception& e) {
        Msg("❌ TEST FAILED: %s", e.what());
    }
}

} // namespace xray::render::resources
```

**Estimated Time:** 4 hours

---

## Phase 4 Summary

**Total Estimated Time:** 8 hours

### Deliverables:
- ✅ FGResourcePool aliasing working
- ✅ Memory savings from texture reuse
- ✅ Comprehensive test suite
- ✅ All tests passing
- ✅ System validated and production-ready

---

# 📊 Final Checklist

## Core Functionality
- [ ] Load DDS textures from disk (LoadTextureSync)
- [ ] Create runtime textures (CreateTexture)
- [ ] Import external textures (ImportTexture)
- [ ] Memory budget enforcement (EnforceMemoryBudget)
- [ ] LRU eviction (EvictTextures)
- [ ] Mip streaming (LoadMipsFromDisk, UploadMipsToGPU)
- [ ] Resource aliasing (FGResourcePool)

## Integration
- [ ] ModernResourceManager initialized in RenderDevice
- [ ] FrameGraph uses ModernResourceManager
- [ ] ps_r4_use_framegraph enables modern path
- [ ] Texture loading works in actual rendering

## Testing
- [ ] TestBasicLoading passes
- [ ] TestDeduplication passes
- [ ] TestMemoryBudget passes
- [ ] TestLRUEviction passes
- [ ] TestCreateTexture passes
- [ ] Integration test: render 100 frames successfully

## Performance
- [ ] Memory usage stays within budget
- [ ] No VRAM exhaustion
- [ ] Frame time <16.67ms (60 FPS)
- [ ] Streaming bandwidth <32 MB/frame

---

# 🚀 Quick Start Implementation Order

**Day 1-2:** Phase 1 (Core Loading)
1. Task 1.1: LoadTextureSync() - 3h
2. Task 1.2: CreateTexture() - 2h
3. Task 1.3: ImportTexture() - 1h
4. Task 1.4: Fix DDSLoader pitches - 1h
5. Task 1.5: Wire up LoadTexture() - 0.5h

**Day 3:** Phase 2 (Memory Management)
1. Task 2.3: EvictTextures() - 3h
2. Task 2.4: EvictTextureInternal() - 1h
3. Task 2.6: Touch() + access tracking - 1h
4. Task 2.7: Update() - 1h

**Day 4-5:** Phase 3 (Streaming)
1. Task 3.1: LoadMipsFromDisk() - 2h
2. Task 3.2: OnAsyncLoadComplete() - 2h
3. Task 3.3: UploadMipsToGPU() - 3h
4. Task 3.4: Partial DDS loading - 3h

**Day 6:** Phase 4 (Integration & Testing)
1. Task 4.1-4.4: FGResourcePool aliasing - 4h
2. Task 4.5: Test suite - 4h

---

# 📌 Key Files to Modify

```
src/Layers/xrRender/ResourceManager/
├── TextureManager.cpp          [CRITICAL - 80% of work here]
├── DDSLoader.cpp               [Fix pitch calculation]
├── TextureStreaming.cpp        [Mip streaming]
├── FGResourcePool.cpp          [Aliasing]
└── TestTextureManager.cpp      [Validation]
```

---

# ✅ Success Criteria

ModernResourceManager is complete when:
1. ✅ Can load and display DDS textures
2. ✅ Memory stays within budget (no OOM)
3. ✅ Eviction works (LRU algorithm)
4. ✅ Streaming works (progressive detail)
5. ✅ Aliasing saves memory (20%+ reduction)
6. ✅ All tests pass
7. ✅ Game runs at 60 FPS with FrameGraph enabled

---

**IMPLEMENTATION STATUS: Ready to Begin Phase 1**
**ESTIMATED COMPLETION: 5-6 working days**
