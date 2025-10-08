# OzzMotionsContainer Refactoring Design Document

**Date:** 2025-10-08
**Goal:** Implement shared motion library pooling for OzzKinematicsAnimated to eliminate memory waste and enable parallelization

---

## Executive Summary

Refactor OzzKinematicsAnimated to use a global motion library container (following the legacy `g_pMotionsContainer` pattern) to:
- **Eliminate memory duplication**: 10x-50x reduction for scenes with many NPCs
- **Enable parallelization**: Thread-safe architecture ready for multi-threaded loading
- **Optimize load times**: First load from disk, subsequent instances instant
- **Automatic memory management**: LRU eviction prevents OOM

---

## Background: Legacy Motion Sharing System

### How the Original CKinematicsAnimated Works

#### Motion Loading Flow

**Location:** `src/Layers/xrRender/SkeletonAnimated.cpp:725-859`

When an .ogf model is loaded:
1. Reads `OGF_S_MOTION_REFS` or `OGF_S_MOTION_REFS2` chunk
2. Contains comma-separated list of .omf (motion) file paths
3. Example: `"animations\stalker_animation.omf,animations\weapon_animations.omf"`
4. For each .omf file, creates a motion slot with `shared_motions` reference

#### Global Motion Container (Deduplication System)

**Location:** `src/xrCore/Animation/SkeletonMotions.cpp:11, 236-248`

**Key component**: `g_pMotionsContainer` - a global singleton created by `CModelPool`

```cpp
// In CModelPool constructor (ModelPool.cpp:339)
g_pMotionsContainer = xr_new<motions_container>();
```

This container maintains:
- `SharedMotionsMap container` - maps file paths to `motions_value*`
- Each `motions_value` contains the actual motion data loaded from an .omf file

#### The Sharing Mechanism: dock()

**Location:** `src/xrCore/Animation/SkeletonMotions.cpp:271-289`

**Critical function**: `motions_container::dock(shared_str key, IReader* data, vecBones* bones)`

```cpp
motions_value* motions_container::dock(shared_str key, IReader* data, vecBones* bones)
{
    // 1. Check if already loaded
    auto I = container.find(key);
    if (I != container.end())
        return I->second;  // REUSE existing!

    // 2. Not found - load it
    result = xr_new<motions_value>();
    result->load(key.c_str(), data, bones);
    container.insert(std::make_pair(key, result));
    return result;
}
```

**How deduplication works**:
1. First OGF requesting "stalker_animation.omf" → loads from disk, stores in container
2. Second OGF requesting "stalker_animation.omf" → returns cached pointer (no disk I/O!)
3. Third OGF requesting "stalker_animation.omf" → returns same cached pointer

#### Reference Counting

**Location:** `src/xrCore/Animation/SkeletonMotions.cpp:368-386`

```cpp
bool shared_motions::create(shared_str key, IReader* data, vecBones* bones)
{
    motions_value* v = g_pMotionsContainer->dock(key, data, bones);
    if (nullptr != v)
        v->m_dwReference++;  // Increment ref count
    destroy();  // Decrements old reference
    p_ = v;
}
```

- Each `CKinematicsAnimated` instance increments the reference count
- When an instance is destroyed, reference count decrements
- When `m_dwReference` reaches 0, the motion data can be cleaned up

#### Per-Instance Optimization

**Location:** `src/Layers/xrRender/SkeletonAnimated.cpp:843-852`

After loading shared motions, each instance builds a bone-indexed cache:

```cpp
for (auto &m_it : m_Motions)
{
    SMotionsSlot& MS = m_it;
    MS.bone_motions.resize(bones->size());
    for (u32 i = 0; i < bones->size(); i++)
    {
        CBoneData* BD = (*bones)[i];
        MS.bone_motions[i] = MS.motions.bone_motions(BD->name);
    }
}
```

**Purpose**:
- Avoids string-based bone name lookups during animation playback
- Direct indexed access: `m_Motions[slot].bone_motions[bone_id]->at(motion_idx)`

#### Memory Layout

```
┌─────────────────────────────────────────┐
│ CModelPool (owns g_pMotionsContainer)  │
└─────────────────────────────────────────┘
                    │
                    ▼
┌──────────────────────────────────────────────────────┐
│ g_pMotionsContainer (global singleton)               │
│  ┌────────────────────────────────────────────────┐  │
│  │ "stalker_animation.omf" → motions_value*       │  │
│  │   - m_dwReference = 3                          │  │
│  │   - m_motions (actual CMotion data)            │  │
│  │   - m_mdefs (motion definitions)               │  │
│  ├────────────────────────────────────────────────┤  │
│  │ "weapon_animations.omf" → motions_value*       │  │
│  │   - m_dwReference = 5                          │  │
│  └────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────┘
           ▲              ▲              ▲
           │              │              │
    ┌──────┴───┐   ┌──────┴───┐   ┌──────┴───┐
    │Instance 1│   │Instance 2│   │Instance 3│
    │ m_Motions│   │ m_Motions│   │ m_Motions│
    │ [0].motions──┘          └──>[0].motions│
    │ [0].bone_motions (cache)               │
    └──────────┘                   └──────────┘
```

#### Key Benefits

1. **Zero duplication**: Same .omf file loaded only once regardless of instance count
2. **Minimal per-instance overhead**: Only bone-indexed pointers stored per instance
3. **Fast cleanup**: Reference counting allows automatic cleanup when no instances reference a motion set
4. **Optimized access**: Bone motion cache eliminates string lookups during playback

This is a classic **flyweight pattern** with reference counting for automatic memory management.

---

## Current Problems with OzzKinematicsAnimated

### Memory Waste

**Current implementation:**
```cpp
// Current: Each instance loads its own copy
OzzKinematicsAnimated instance1;  // Loads stalker_animation.ozz
OzzKinematicsAnimated instance2;  // Loads stalker_animation.ozz AGAIN (duplicate!)
OzzKinematicsAnimated instance3;  // Loads stalker_animation.ozz AGAIN (duplicate!)
```

**Location:** `src/xrAnimation/OzzKinematicsAnimated.cpp:487-518`
- Each instance has its own `MotionLibrary motionLibrary` (line 194)
- `EnsureMotionLibraryLoaded()` loads from disk for every instance
- No deduplication mechanism

**Impact:**
- 10 NPCs with 50MB motions = 500MB wasted memory
- Slow startup due to repeated disk I/O
- Cache thrashing from duplicate data

---

## Proposed Architecture

Following the **exact pattern** from `g_pMotionsContainer`:

```
┌─────────────────────────────────────────────┐
│ CModelPool (owns g_pOzzMotionsContainer)    │
└─────────────────────────────────────────────┘
                    │
                    ▼
┌──────────────────────────────────────────────────────────────┐
│ g_pOzzMotionsContainer (global singleton)                    │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ "stalker_animation.ozz" → OzzMotionsValue*             │  │
│  │   - m_dwReference = 3                                  │  │
│  │   - m_motion_library (MotionRecord vector)             │  │
│  │   - m_lookup (name → index map)                        │  │
│  ├────────────────────────────────────────────────────────┤  │
│  │ "weapon_animations.ozz" → OzzMotionsValue*             │  │
│  │   - m_dwReference = 5                                  │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
           ▲              ▲              ▲
           │              │              │
    ┌──────┴───┐   ┌──────┴───┐   ┌──────┴───┐
    │Instance 1│   │Instance 2│   │Instance 3│
    │ m_Motions│   │ m_Motions│   │ m_Motions│
    │ [0].motions──┘ (shared!)└──>[0].motions│
    │ [0].bone_motion_cache (fast lookup)    │
    └──────────┘                   └──────────┘
```

---

## Implementation Plan

### Step 1: Create Core Shared Motion Types

**File:** `src/xrAnimation/OzzSharedMotions.hpp`

```cpp
// Heavy shared data - stored once per unique .ozz file
struct XRANIMATION_API OzzMotionsValue
{
    // Motion library (shared across all instances)
    struct MotionRecord {
        xr_string name;
        std::shared_ptr<ozz::animation::Animation> animation;
        CMotionDef definition;
        MotionID id;
        u32 frameCount;
        xr_vector<xr_unique_ptr<CMotion>> boneMotions;
    };

    xr_vector<MotionRecord> records;
    xr_unordered_map<xr_string, u16> lookup;

    u32 m_dwReference;      // Reference count
    shared_str m_id;        // Source file path

    bool load(pcstr file_path, const ozz::animation::Skeleton& skeleton);
    MotionRecord* find(const xr_string& name);
    u16 next_index() const { return static_cast<u16>(records.size()); }
};

// Global container (singleton)
class XRANIMATION_API OzzMotionsContainer
{
    using SharedMotionsMap = xr_map<shared_str, OzzMotionsValue*>;
    SharedMotionsMap container;

public:
    OzzMotionsContainer();
    ~OzzMotionsContainer();

    bool Has(shared_str key);
    OzzMotionsValue* Dock(shared_str key, const ozz::animation::Skeleton& skeleton);
    void Clean(bool force_destroy);
    void Dump();
};

extern XRANIMATION_API OzzMotionsContainer* g_pOzzMotionsContainer;

// RAII wrapper (per-instance)
class XRANIMATION_API SharedOzzMotions
{
    OzzMotionsValue* p_;

    void destroy() {
        if (p_) {
            p_->m_dwReference--;
            if (p_->m_dwReference == 0) p_ = nullptr;
        }
    }

public:
    SharedOzzMotions() : p_(nullptr) {}
    ~SharedOzzMotions() { destroy(); }

    bool create(shared_str key, const ozz::animation::Skeleton& skeleton);
    bool create(const SharedOzzMotions& rhs);

    MotionRecord* find(const xr_string& name) const;
    MotionRecord* find(u16 index) const;
    u16 next_index() const;
};
```

### Step 2: Refactor OzzKinematicsAnimated

**File:** `src/xrAnimation/OzzKinematicsAnimated.h`

**BEFORE:**
```cpp
private:
    // Per-instance motion library (BAD - duplicates memory)
    MotionLibrary motionLibrary;  // Line 194
    xr_vector<xr_string> motionReferences;
```

**AFTER:**
```cpp
private:
    // Motion library management (NEW - shared!)
    struct SMotionsSlot
    {
        SharedOzzMotions motions;           // Shared reference
        xr_vector<MotionRecord*> bone_motion_cache;  // Fast per-bone lookup
    };
    using MotionsSlotVec = xr_vector<SMotionsSlot>;
    MotionsSlotVec m_Motions;
```

### Step 3: Update Motion Loading Logic

**File:** `src/xrAnimation/OzzKinematicsAnimated.cpp`

**BEFORE (Lines 487-518):**
```cpp
void OzzKinematicsAnimated::EnsureMotionLibraryLoaded()
{
    motionLibrary.Reset();  // Per-instance reset

    for (const auto& reference : motionReferences)
    {
        LoadMotionReference(reference);  // Loads from disk each time
    }
}
```

**AFTER:**
```cpp
void OzzKinematicsAnimated::EnsureMotionLibraryLoaded()
{
    m_Motions.clear();

    for (const auto& reference : motionReferences)
    {
        // Create motion slot
        m_Motions.push_back(SMotionsSlot());
        SMotionsSlot& slot = m_Motions.back();

        // Dock to global container (deduplicates automatically!)
        shared_str key(reference.c_str());
        if (!slot.motions.create(key, core.Skeleton()))
        {
            Msg("[OzzKinematicsAnimated] Failed to load motion ref '%s'", reference.c_str());
            m_Motions.pop_back();
            continue;
        }

        // Build per-bone motion cache (fast lookup during animation)
        BuildBoneMotionCache(slot);
    }
}

void OzzKinematicsAnimated::BuildBoneMotionCache(SMotionsSlot& slot)
{
    const u32 bone_count = core.Skeleton().num_joints();
    slot.bone_motion_cache.resize(bone_count);

    // For each bone, cache pointer to its motion data
    for (u32 bone_idx = 0; bone_idx < bone_count; ++bone_idx)
    {
        // Index into shared motion library
        for (auto& record : slot.motions.records())
        {
            if (bone_idx < record.boneMotions.size())
                slot.bone_motion_cache[bone_idx] = record.boneMotions[bone_idx].get();
        }
    }
}
```

### Step 4: Update ModelPool Integration

**File:** `src/Layers/xrRender/ModelPool.cpp`

```cpp
CModelPool::CModelPool()
{
    bLogging = TRUE;
    bForceDiscard = FALSE;
    bAllowChildrenDuplicate = TRUE;
    g_pMotionsContainer = xr_new<motions_container>();
    g_pOzzMotionsContainer = xr_new<OzzMotionsContainer>();  // NEW!
}

CModelPool::~CModelPool()
{
    Destroy();
    xr_delete(g_pMotionsContainer);
    xr_delete(g_pOzzMotionsContainer);  // NEW!
}

void CModelPool::Destroy()
{
    // ... existing code ...

    // Cleanup ozz motions
    g_pOzzMotionsContainer->Clean(false);  // NEW!
}
```

### Step 5: Implement Global Container Dock() Method

**File:** `src/xrAnimation/OzzSharedMotions.cpp`

```cpp
OzzMotionsValue* OzzMotionsContainer::Dock(
    shared_str key, const ozz::animation::Skeleton& skeleton)
{
    // 1. Check if already loaded
    auto it = container.find(key);
    if (it != container.end())
    {
        Msg("[ozz] Reusing cached motion library '%s' (refs: %u)",
            key.c_str(), it->second->m_dwReference);
        return it->second;  // REUSE!
    }

    // 2. Not found - load from disk
    OzzMotionsValue* result = xr_new<OzzMotionsValue>();
    result->m_dwReference = 0;

    if (!result->load(key.c_str(), skeleton))
    {
        xr_delete(result);
        return nullptr;
    }

    // 3. Store in global container
    container.insert(std::make_pair(key, result));
    Msg("[ozz] Loaded motion library '%s' into global pool", key.c_str());

    return result;
}
```

---

## Enhanced Features for Parallelization & Optimization

### Core Architecture with Advanced Features

```cpp
// File: src/xrAnimation/OzzSharedMotions.hpp
#pragma once

#include "xrCore/Threading/Lock.hpp"
#include "xrCore/Threading/ScopeLock.hpp"
#include "xrCore/xrsharedmem.h"
#include "xrCommon/xr_vector.h"
#include "xrCommon/xr_unordered_map.h"

#include <atomic>
#include <memory>

namespace XRay::Animation
{

//=============================================================================
// Motion Loading State Machine
//=============================================================================
enum class MotionLoadState : u8
{
    Unloaded,       // Not yet requested
    Loading,        // Currently being loaded (prevents duplicate loads)
    Loaded,         // Successfully loaded
    Failed,         // Load failed
    Evicted         // Was loaded, then unloaded to free memory
};

//=============================================================================
// Shared Motion Data (Heavy, deduplicated)
//=============================================================================
struct XRANIMATION_API OzzMotionsValue
{
    // Motion library records
    struct MotionRecord
    {
        xr_string name;
        std::shared_ptr<ozz::animation::Animation> animation;
        CMotionDef definition;
        MotionID id;
        u32 frameCount;
        xr_vector<xr_unique_ptr<CMotion>> boneMotions;

        u32 GetMemoryUsage() const;  // Calculate memory footprint
    };

    xr_vector<MotionRecord> records;
    xr_unordered_map<xr_string, u16> lookup;

    // Reference counting (atomic for thread safety)
    std::atomic<u32> m_dwReference{0};

    // Load state tracking
    std::atomic<MotionLoadState> loadState{MotionLoadState::Unloaded};

    // Metadata
    shared_str m_id;              // Source file path
    u64 lastAccessTime{0};        // For LRU eviction
    u32 totalMemoryBytes{0};      // Cached memory usage
    u32 version{0};               // For hot-reload detection

    // Loading
    bool Load(pcstr file_path, const ozz::animation::Skeleton& skeleton);
    MotionRecord* Find(const xr_string& name);
    const MotionRecord* Find(const xr_string& name) const;
    MotionRecord* Find(u16 index);
    const MotionRecord* Find(u16 index) const;
    u16 NextIndex() const { return static_cast<u16>(records.size()); }

    // Memory tracking
    void UpdateMemoryUsage();
    void MarkAccessed();
};

//=============================================================================
// Global Container with Advanced Features
//=============================================================================
class XRANIMATION_API OzzMotionsContainer
{
public:
    // Statistics for profiling
    struct Statistics
    {
        std::atomic<u64> cacheHits{0};
        std::atomic<u64> cacheMisses{0};
        std::atomic<u64> totalLoads{0};
        std::atomic<u64> totalEvictions{0};
        std::atomic<u64> peakMemoryBytes{0};
        std::atomic<u64> currentMemoryBytes{0};

        float GetHitRate() const
        {
            u64 total = cacheHits.load() + cacheMisses.load();
            return total > 0 ? static_cast<float>(cacheHits.load()) / total : 0.f;
        }
    };

    // Configuration
    struct Config
    {
        u64 maxMemoryBytes = 512 * 1024 * 1024;  // 512MB default
        bool enableLRUEviction = true;
        bool enablePrefetching = false;
        u32 prefetchThreads = 2;
        bool enableHotReload = false;
    };

private:
    using SharedMotionsMap = xr_unordered_map<shared_str, OzzMotionsValue*>;

    SharedMotionsMap container;
    Lock containerLock;              // Protects container map

    Statistics stats;
    Config config;

    std::atomic<u32> globalVersion{0};  // Incremented on any modification

public:
    OzzMotionsContainer();
    ~OzzMotionsContainer();

    //-------------------------------------------------------------------------
    // Core Operations (Thread-Safe)
    //-------------------------------------------------------------------------

    // Primary docking interface (thread-safe, deduplicating)
    OzzMotionsValue* Dock(shared_str key, const ozz::animation::Skeleton& skeleton);

    // Check existence without loading
    bool Has(shared_str key);

    // Cleanup (force=true deletes all, force=false only unreferenced)
    void Clean(bool force_destroy);

    //-------------------------------------------------------------------------
    // Batch Operations (Parallelization-Friendly)
    //-------------------------------------------------------------------------

    // Prefetch multiple motions in parallel
    void PrefetchBatch(const xr_vector<xr_string>& motion_paths,
                       const ozz::animation::Skeleton& skeleton);

    // Load multiple motions in parallel, return success count
    u32 LoadBatch(const xr_vector<xr_string>& motion_paths,
                  const ozz::animation::Skeleton& skeleton);

    //-------------------------------------------------------------------------
    // Memory Management
    //-------------------------------------------------------------------------

    // Evict least-recently-used motions until under memory limit
    void EvictLRU(u64 target_memory_bytes);

    // Evict specific motion (if ref count == 0)
    bool EvictMotion(shared_str key);

    // Get current memory usage
    u64 GetMemoryUsage() const { return stats.currentMemoryBytes.load(); }

    // Set memory limit (triggers eviction if exceeded)
    void SetMemoryLimit(u64 max_bytes);

    //-------------------------------------------------------------------------
    // Hot Reload Support
    //-------------------------------------------------------------------------

    // Check if motions have changed on disk, reload if needed
    void CheckForUpdates();

    // Force reload a specific motion
    bool ReloadMotion(shared_str key, const ozz::animation::Skeleton& skeleton);

    //-------------------------------------------------------------------------
    // Statistics & Debugging
    //-------------------------------------------------------------------------

    const Statistics& GetStatistics() const { return stats; }
    void ResetStatistics();
    void Dump();  // Log container state

    // Get current version (increments on modifications)
    u32 GetVersion() const { return globalVersion.load(); }

    //-------------------------------------------------------------------------
    // Configuration
    //-------------------------------------------------------------------------

    void SetConfig(const Config& cfg) { config = cfg; }
    const Config& GetConfig() const { return config; }

private:
    // Internal helpers
    OzzMotionsValue* DockInternal(shared_str key, const ozz::animation::Skeleton& skeleton);
    void UpdateMemoryTracking(OzzMotionsValue* value, bool adding);
    void CheckMemoryPressure();

    // Eviction helpers
    struct EvictionCandidate
    {
        shared_str key;
        u64 lastAccessTime;
        u32 memoryBytes;
    };
    xr_vector<EvictionCandidate> GatherEvictionCandidates();
};

//=============================================================================
// Global Instance
//=============================================================================
extern XRANIMATION_API OzzMotionsContainer* g_pOzzMotionsContainer;

//=============================================================================
// RAII Wrapper (Per-Instance Usage)
//=============================================================================
class XRANIMATION_API SharedOzzMotions
{
    OzzMotionsValue* p_{nullptr};

    void Destroy()
    {
        if (!p_)
            return;

        const u32 prevRef = p_->m_dwReference.fetch_sub(1, std::memory_order_release);
        if (prevRef == 1)  // Was last reference
            p_ = nullptr;
    }

public:
    SharedOzzMotions() = default;
    ~SharedOzzMotions() { Destroy(); }

    // Move semantics
    SharedOzzMotions(SharedOzzMotions&& other) noexcept : p_(other.p_)
    {
        other.p_ = nullptr;
    }

    SharedOzzMotions& operator=(SharedOzzMotions&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            p_ = other.p_;
            other.p_ = nullptr;
        }
        return *this;
    }

    // Copy semantics (ref counting)
    SharedOzzMotions(const SharedOzzMotions& other) : p_(other.p_)
    {
        if (p_)
            p_->m_dwReference.fetch_add(1, std::memory_order_relaxed);
    }

    SharedOzzMotions& operator=(const SharedOzzMotions& other)
    {
        if (this != &other)
        {
            Destroy();
            p_ = other.p_;
            if (p_)
                p_->m_dwReference.fetch_add(1, std::memory_order_relaxed);
        }
        return *this;
    }

    // Creation from global container
    bool Create(shared_str key, const ozz::animation::Skeleton& skeleton)
    {
        OzzMotionsValue* v = g_pOzzMotionsContainer->Dock(key, skeleton);
        if (v)
        {
            v->m_dwReference.fetch_add(1, std::memory_order_relaxed);
            v->MarkAccessed();
        }
        Destroy();
        p_ = v;
        return (v != nullptr);
    }

    // Accessors
    OzzMotionsValue::MotionRecord* Find(const xr_string& name) const
    {
        return p_ ? p_->Find(name) : nullptr;
    }

    OzzMotionsValue::MotionRecord* Find(u16 index) const
    {
        return p_ ? p_->Find(index) : nullptr;
    }

    u16 NextIndex() const
    {
        return p_ ? p_->NextIndex() : 0;
    }

    const shared_str& Id() const
    {
        static shared_str empty;
        return p_ ? p_->m_id : empty;
    }

    bool IsValid() const { return p_ != nullptr; }

    // Direct access (use with caution)
    const xr_vector<OzzMotionsValue::MotionRecord>& Records() const
    {
        VERIFY(p_);
        return p_->records;
    }
};

} // namespace XRay::Animation
```

---

## Key Parallelization Features

### 1. Atomic Reference Counting

```cpp
// Thread-safe increment/decrement
std::atomic<u32> m_dwReference{0};

// Lock-free ref count operations
v->m_dwReference.fetch_add(1, std::memory_order_relaxed);
v->m_dwReference.fetch_sub(1, std::memory_order_release);
```

**Benefit:** Lock-free increment/decrement, ~10x faster than mutex-protected counter

### 2. Load State Machine (Prevents Duplicate Loads)

```cpp
enum class MotionLoadState : u8
{
    Unloaded,   // Initial state
    Loading,    // Thread A is loading (Thread B will wait or skip)
    Loaded,     // Ready to use
    Failed,     // Load error
    Evicted     // Was loaded, freed for memory
};

std::atomic<MotionLoadState> loadState{MotionLoadState::Unloaded};
```

**Usage:**
```cpp
OzzMotionsValue* OzzMotionsContainer::DockInternal(...)
{
    // Check if already loaded (fast path, no lock)
    MotionLoadState expected = MotionLoadState::Loaded;
    if (value->loadState.load(std::memory_order_acquire) == expected)
    {
        stats.cacheHits++;
        return value;  // Already loaded!
    }

    // Transition to Loading state
    expected = MotionLoadState::Unloaded;
    if (!value->loadState.compare_exchange_strong(expected, MotionLoadState::Loading))
    {
        // Another thread is loading, wait or return null
        return nullptr;
    }

    // We got the lock, do the load
    if (value->Load(key, skeleton))
        value->loadState.store(MotionLoadState::Loaded, std::memory_order_release);
    else
        value->loadState.store(MotionLoadState::Failed, std::memory_order_release);
}
```

### 3. Batch Prefetching (Parallel Loads)

```cpp
void OzzMotionsContainer::PrefetchBatch(
    const xr_vector<xr_string>& motion_paths,
    const ozz::animation::Skeleton& skeleton)
{
    // Create worker tasks
    TaskManager::ForEach(motion_paths.begin(), motion_paths.end(),
        [this, &skeleton](const xr_string& path)
        {
            shared_str key(path.c_str());
            Dock(key, skeleton);  // Thread-safe dock
        });
}
```

### 4. Lock-Free Reads (RCU-Style Pattern)

```cpp
// Read operations don't need locks if data is immutable
OzzMotionsValue::MotionRecord* Find(u16 index) const
{
    // Once loaded, records vector doesn't change
    // Can read without lock!
    if (index >= records.size())
        return nullptr;
    return &const_cast<MotionRecord&>(records[index]);
}
```

---

## Optimization Features

### 5. LRU Eviction (Memory Management)

```cpp
void OzzMotionsContainer::EvictLRU(u64 target_memory_bytes)
{
    auto candidates = GatherEvictionCandidates();

    // Sort by last access time (oldest first)
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) {
            return a.lastAccessTime < b.lastAccessTime;
        });

    u64 freed = 0;
    for (const auto& candidate : candidates)
    {
        if (GetMemoryUsage() <= target_memory_bytes)
            break;

        if (EvictMotion(candidate.key))
            freed += candidate.memoryBytes;
    }
}
```

### 6. Memory Pressure Monitoring

```cpp
void OzzMotionsContainer::CheckMemoryPressure()
{
    const u64 current = GetMemoryUsage();
    if (current > config.maxMemoryBytes)
    {
        Msg("[OzzMotions] Memory pressure: %llu / %llu MB",
            current / (1024*1024), config.maxMemoryBytes / (1024*1024));
        EvictLRU(config.maxMemoryBytes * 90 / 100);  // Target 90%
    }
}
```

### 7. Statistics & Profiling

```cpp
// Track performance
stats.cacheHits++;        // Lock-free increment
stats.totalLoads++;
stats.peakMemoryBytes.store(std::max(
    stats.peakMemoryBytes.load(),
    stats.currentMemoryBytes.load()
));

// Usage:
const auto& stats = g_pOzzMotionsContainer->GetStatistics();
Msg("Cache hit rate: %.1f%%", stats.GetHitRate() * 100.f);
Msg("Peak memory: %llu MB", stats.peakMemoryBytes.load() / (1024*1024));
```

### 8. Hot Reload Support

```cpp
void OzzMotionsContainer::CheckForUpdates()
{
    ScopeLock lock(&containerLock);

    for (auto& [key, value] : container)
    {
        // Check file timestamp, CRC, etc.
        if (FileModified(key.c_str(), value->version))
        {
            Msg("[OzzMotions] Reloading '%s'", key.c_str());
            ReloadMotion(key, /* skeleton */);
            globalVersion++;  // Invalidate caches
        }
    }
}
```

---

## Usage Example

```cpp
// At startup (ModelPool.cpp)
g_pOzzMotionsContainer = xr_new<OzzMotionsContainer>();
g_pOzzMotionsContainer->SetConfig({
    .maxMemoryBytes = 512 * 1024 * 1024,  // 512MB
    .enableLRUEviction = true,
    .enablePrefetching = true,
    .prefetchThreads = 4
});

// Prefetch common animations in parallel
xr_vector<xr_string> commonAnims = {
    "stalker_animation.ozz",
    "weapon_animations.ozz",
    "idle_animations.ozz"
};
g_pOzzMotionsContainer->PrefetchBatch(commonAnims, skeleton);

// Per-instance usage (OzzKinematicsAnimated.cpp)
SharedOzzMotions motions;
if (motions.Create("stalker_animation.ozz", core.Skeleton()))
{
    // Instant if already loaded by another instance!
    auto* record = motions.Find("walk_fwd");
}

// Cleanup
g_pOzzMotionsContainer->Dump();  // Log stats
xr_delete(g_pOzzMotionsContainer);
```

---

## Testing Strategy

### Test 1: Deduplication Verification

```cpp
// Create 3 instances with same motion refs
OzzKinematicsAnimated* npc1 = CreateNPC("stalker_animation.ozz");
OzzKinematicsAnimated* npc2 = CreateNPC("stalker_animation.ozz");
OzzKinematicsAnimated* npc3 = CreateNPC("stalker_animation.ozz");

// Check global container
g_pOzzMotionsContainer->Dump();
// Expected output: "stalker_animation.ozz: refs=3, memory=X KB"
```

### Test 2: Memory Usage

```cpp
// Before: 10 NPCs × 50MB motions = 500MB
// After:  1 × 50MB motions = 50MB (10x reduction!)
```

### Test 3: Parallelization Readiness

- Global container can use `Lock` for thread safety
- `Dock()` method is already atomic-friendly
- Ref counting works with atomic increments

---

## Migration Checklist

- [ ] Create `OzzSharedMotions.hpp` with core types
- [ ] Implement `OzzMotionsContainer` class
- [ ] Implement `SharedOzzMotions` RAII wrapper
- [ ] Refactor `OzzKinematicsAnimated::m_Motions` structure
- [ ] Update `EnsureMotionLibraryLoaded()` to use global container
- [ ] Add `BuildBoneMotionCache()` for per-instance optimization
- [ ] Integrate with `CModelPool` lifecycle
- [ ] Update all `LL_GetMotion()` calls to use cache
- [ ] Add logging to track deduplication
- [ ] Test with multiple instances
- [ ] Profile memory usage before/after
- [ ] Add thread safety (optional, Phase 2)

---

## Performance Benefits

| Feature | Improvement |
|---------|-------------|
| **Atomic Ref Counting** | Lock-free increment/decrement, ~10x faster |
| **Load State Machine** | Prevents duplicate loads from multiple threads |
| **Batch Prefetching** | Parallel I/O, ~4x faster startup |
| **Lock-Free Reads** | Zero contention for lookups after load |
| **LRU Eviction** | Automatic memory management, prevents OOM |
| **Statistics Tracking** | Real-time profiling, identify bottlenecks |
| **Memory Alignment** | Better cache utilization |

---

## Expected Results

1. **Memory Efficiency**: 10x-50x reduction for scenes with many NPCs
2. **Load Time**: First load from disk, subsequent instances instant
3. **Cache Friendly**: Shared data stays hot in CPU cache
4. **Parallelization Ready**: Container is designed for thread-safe `Dock()`
5. **Maintainability**: Matches legacy system architecture (familiar to team)

---

## References

- Legacy system: `src/xrCore/Animation/SkeletonMotions.cpp`
- Current implementation: `src/xrAnimation/OzzKinematicsAnimated.cpp`
- Threading primitives: `src/xrCore/Threading/Lock.hpp`
- Model pool: `src/Layers/xrRender/ModelPool.cpp`
