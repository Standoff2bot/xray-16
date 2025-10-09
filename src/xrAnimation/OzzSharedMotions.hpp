#pragma once

#include "xrCore/Threading/Lock.hpp"
#include "xrCore/Threading/ScopeLock.hpp"
#include "xrCore/xrsharedmem.h"
#include "xrCore/Animation/Motion.hpp"
#include "Include/xrRender/animation_motion.h"  // for MotionID
#include "xrCommon/xr_vector.h"
#include "xrCommon/xr_map.h"
#include "xrCommon/xr_unordered_map.h"
#include "xrCommon/xr_string.h"

#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/animation.h"

#include <atomic>
#include <memory>

namespace XRay::Animation
{

//=============================================================================
// Forward Declarations
//=============================================================================
using MotionVec = xr_vector<CMotion>;
using BoneMotionMap = xr_map<shared_str, MotionVec>;
using BoneMotionsVec = xr_vector<MotionVec*>;

//=============================================================================
// Motion Handle (Type-Safe, Industry Standard)
//=============================================================================
struct MotionLibraryHandle
{
    u64 id{0};  // Unique ID (could be GUID in future)

    bool IsValid() const { return id != 0; }
    void Invalidate() { id = 0; }

    bool operator==(const MotionLibraryHandle& other) const { return id == other.id; }
    bool operator!=(const MotionLibraryHandle& other) const { return id != other.id; }
};

//=============================================================================
// Skeleton Fingerprint (Prevents Mismatches)
//=============================================================================
struct SkeletonFingerprint
{
    u32 hash{0};        // CRC32 of joint names
    u16 jointCount{0};  // Number of joints
    u16 version{0};     // Format version

    static SkeletonFingerprint Compute(const ozz::animation::Skeleton& skeleton);
    bool Matches(const SkeletonFingerprint& other) const;
};

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
// Shared Motion Data (Bone-Major Layout)
//=============================================================================
struct OzzMotionsValue
{
    //-------------------------------------------------------------------------
    // Motion Metadata (Lightweight, Immutable After Load)
    //-------------------------------------------------------------------------
    struct MotionRecord
    {
        xr_string name;
        std::shared_ptr<ozz::animation::Animation> animation;
        CMotionDef definition;      // Marks, speed, power, etc (immutable)
        MotionID id;
        u32 frameCount{0};

        u32 GetMemoryUsage() const;
    };

    xr_vector<MotionRecord> records;          // Metadata only
    xr_unordered_map<xr_string, u16> lookup;  // Name → index

    //-------------------------------------------------------------------------
    // Heavy Motion Data (Bone-Major Layout - CRITICAL!)
    //-------------------------------------------------------------------------
    BoneMotionMap bone_motions;  // bone_name → [motion0, motion1, ...]
    //                              ^^^ Organized by BONE first, not motion!

    //-------------------------------------------------------------------------
    // Handle & Fingerprinting
    //-------------------------------------------------------------------------
    MotionLibraryHandle handle;
    SkeletonFingerprint skelFingerprint;

    //-------------------------------------------------------------------------
    // Reference Counting (Atomic for Thread Safety)
    //-------------------------------------------------------------------------
    std::atomic<u32> refCount{0};

    //-------------------------------------------------------------------------
    // Load State Tracking
    //-------------------------------------------------------------------------
    std::atomic<MotionLoadState> loadState{MotionLoadState::Unloaded};

    //-------------------------------------------------------------------------
    // Metadata
    //-------------------------------------------------------------------------
    shared_str sourceFile;        // Source file path
    u64 lastAccessTime{0};        // For LRU eviction
    u32 totalMemoryBytes{0};      // Cached memory usage
    u32 fileVersion{0};           // For hot-reload detection

    //-------------------------------------------------------------------------
    // API
    //-------------------------------------------------------------------------

    // Load from file (checks skeleton compatibility)
    bool Load(pcstr file_path, const ozz::animation::Skeleton& skeleton);

    // Load from memory buffer (for embedded animations)
    bool LoadFromMemory(const std::vector<std::uint8_t>& data, const ozz::animation::Skeleton& skeleton, pcstr source_label = "<embedded>");

    // Find motion by name
    MotionRecord* FindMotion(const xr_string& name);
    const MotionRecord* FindMotion(const xr_string& name) const;

    // Find motion by index
    MotionRecord* FindMotion(u16 index);
    const MotionRecord* FindMotion(u16 index) const;

    // Get MotionVec for specific bone (returns pointer to shared data)
    MotionVec* GetBoneMotions(const shared_str& bone_name);
    const MotionVec* GetBoneMotions(const shared_str& bone_name) const;

    // Metadata
    u16 GetMotionCount() const { return static_cast<u16>(records.size()); }

    // Memory tracking
    void UpdateMemoryUsage();
    void MarkAccessed();

    // Validation
    bool IsCompatibleWith(const SkeletonFingerprint& fingerprint) const
    {
        return skelFingerprint.Matches(fingerprint);
    }
};

//=============================================================================
// Global Container with Advanced Features
//=============================================================================
class OzzMotionsContainer
{
public:
    //-------------------------------------------------------------------------
    // Statistics for Profiling
    //-------------------------------------------------------------------------
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

    //-------------------------------------------------------------------------
    // Configuration
    //-------------------------------------------------------------------------
    struct Config
    {
        u64 maxMemoryBytes = 512 * 1024 * 1024;  // 512MB default
        bool enableLRUEviction = true;
        bool enablePrefetching = false;
        u32 prefetchThreads = 2;
        bool enableHotReload = false;
        bool enforceSkeletonCompatibility = true;  // Prevent mismatches
    };

    //-------------------------------------------------------------------------
    // Async Load Request (Industry Standard Pattern)
    //-------------------------------------------------------------------------
    struct LoadRequest
    {
        shared_str key;
        SkeletonFingerprint skelFingerprint;
        bool blocking{true};  // If false, returns immediately with Loading state

        // Optional embedded data (if provided, loads from memory instead of file)
        std::vector<std::uint8_t> embeddedData;

        // Future: Add callback for async completion
        // std::function<void(MotionLibraryHandle)> onComplete;
    };

private:
    using HandleToValueMap = xr_unordered_map<u64, OzzMotionsValue*>;
    using KeyToHandleMap = xr_unordered_map<shared_str, u64>;

    HandleToValueMap values;       // Handle ID → OzzMotionsValue*
    KeyToHandleMap keyToHandle;    // File path → Handle ID
    Lock containerLock;            // Protects maps

    Statistics stats;
    Config config;

    std::atomic<u64> nextHandleID{1};  // Handle ID generator
    std::atomic<u32> globalVersion{0}; // Incremented on modifications

public:
    OzzMotionsContainer();
    ~OzzMotionsContainer();

    //-------------------------------------------------------------------------
    // Core Operations (Thread-Safe)
    //-------------------------------------------------------------------------

    // Primary docking interface (returns handle, not raw pointer!)
    MotionLibraryHandle Dock(const LoadRequest& request, const ozz::animation::Skeleton& skeleton);

    // Simplified sync dock
    MotionLibraryHandle Dock(shared_str key, const ozz::animation::Skeleton& skeleton);

    // Get OzzMotionsValue from handle (thread-safe read)
    OzzMotionsValue* Resolve(MotionLibraryHandle handle);
    const OzzMotionsValue* Resolve(MotionLibraryHandle handle) const;

    // Check existence without loading
    bool Has(shared_str key) const;

    // Release handle (decrements ref count)
    void Release(MotionLibraryHandle handle);

    // Cleanup (force=true deletes all, force=false only unreferenced)
    void Clean(bool force_destroy);

    //-------------------------------------------------------------------------
    // Batch Operations (Parallelization-Friendly)
    //-------------------------------------------------------------------------

    // Prefetch multiple motions in parallel
    void PrefetchBatch(const xr_vector<LoadRequest>& requests, const ozz::animation::Skeleton& skeleton);

    // Load multiple motions, return handles
    xr_vector<MotionLibraryHandle> LoadBatch(const xr_vector<LoadRequest>& requests, const ozz::animation::Skeleton& skeleton);

    //-------------------------------------------------------------------------
    // Memory Management
    //-------------------------------------------------------------------------

    // Evict least-recently-used motions until under memory limit
    void EvictLRU(u64 target_memory_bytes);

    // Evict specific motion (if ref count == 0)
    bool EvictMotion(MotionLibraryHandle handle);

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
    bool ReloadMotion(MotionLibraryHandle handle, const ozz::animation::Skeleton& skeleton);

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
    MotionLibraryHandle DockInternal(const LoadRequest& request, const ozz::animation::Skeleton& skeleton);
    void UpdateMemoryTracking(OzzMotionsValue* value, bool adding);
    void CheckMemoryPressure();
    u64 GenerateHandleID() { return nextHandleID.fetch_add(1, std::memory_order_relaxed); }

    // Eviction helpers
    struct EvictionCandidate
    {
        MotionLibraryHandle handle;
        u64 lastAccessTime;
        u32 memoryBytes;
    };
    xr_vector<EvictionCandidate> GatherEvictionCandidates();
};

//=============================================================================
// Global Instance
//=============================================================================
extern OzzMotionsContainer* g_pOzzMotionsContainer;

//=============================================================================
// RAII Wrapper (Per-Instance Usage)
//=============================================================================
class SharedOzzMotions
{
    MotionLibraryHandle handle_;

    void Destroy()
    {
        if (handle_.IsValid() && g_pOzzMotionsContainer)
        {
            g_pOzzMotionsContainer->Release(handle_);
            handle_.Invalidate();
        }
    }

public:
    SharedOzzMotions() = default;
    ~SharedOzzMotions() { Destroy(); }

    // Move semantics
    SharedOzzMotions(SharedOzzMotions&& other) noexcept : handle_(other.handle_)
    {
        other.handle_.Invalidate();
    }

    SharedOzzMotions& operator=(SharedOzzMotions&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            handle_ = other.handle_;
            other.handle_.Invalidate();
        }
        return *this;
    }

    // Copy semantics (increments ref count via Dock)
    SharedOzzMotions(const SharedOzzMotions& other);
    SharedOzzMotions& operator=(const SharedOzzMotions& other);

    // Creation from global container
    bool Create(shared_str key, const ozz::animation::Skeleton& skeleton)
    {
        Msg("[OzzMotionsContainer] SharedOzzMotions::Create called for: %s", key.c_str());
        Destroy();
        handle_ = g_pOzzMotionsContainer->Dock(key, skeleton);
        bool valid = handle_.IsValid();
        Msg("[OzzMotionsContainer] SharedOzzMotions::Create result: %s (handle=0x%llX)",
            valid ? "SUCCESS" : "FAILED", handle_.id);
        return valid;
    }

    bool Create(const OzzMotionsContainer::LoadRequest& request, const ozz::animation::Skeleton& skeleton)
    {
        Msg("[OzzMotionsContainer] SharedOzzMotions::Create(LoadRequest) called for: %s", request.key.c_str());
        Destroy();
        handle_ = g_pOzzMotionsContainer->Dock(request, skeleton);
        bool valid = handle_.IsValid();
        Msg("[OzzMotionsContainer] SharedOzzMotions::Create(LoadRequest) result: %s (handle=0x%llX)",
            valid ? "SUCCESS" : "FAILED", handle_.id);
        return valid;
    }

    //-------------------------------------------------------------------------
    // Accessors (Bone-Major Layout)
    //-------------------------------------------------------------------------

    // Find motion by name
    OzzMotionsValue::MotionRecord* FindMotion(const xr_string& name) const
    {
        OzzMotionsValue* value = g_pOzzMotionsContainer->Resolve(handle_);
        return value ? value->FindMotion(name) : nullptr;
    }

    // Find motion by index
    OzzMotionsValue::MotionRecord* FindMotion(u16 index) const
    {
        OzzMotionsValue* value = g_pOzzMotionsContainer->Resolve(handle_);
        return value ? value->FindMotion(index) : nullptr;
    }

    // Get ALL motions for a specific bone (returns MotionVec*)
    MotionVec* GetBoneMotions(const shared_str& bone_name) const
    {
        OzzMotionsValue* value = g_pOzzMotionsContainer->Resolve(handle_);
        return value ? value->GetBoneMotions(bone_name) : nullptr;
    }

    // Metadata
    u16 GetMotionCount() const
    {
        const OzzMotionsValue* value = g_pOzzMotionsContainer->Resolve(handle_);
        return value ? value->GetMotionCount() : 0;
    }

    const shared_str& GetSourceFile() const
    {
        static shared_str empty;
        const OzzMotionsValue* value = g_pOzzMotionsContainer->Resolve(handle_);
        return value ? value->sourceFile : empty;
    }

    bool IsValid() const { return handle_.IsValid(); }
    MotionLibraryHandle GetHandle() const { return handle_; }
};

} // namespace XRay::Animation
