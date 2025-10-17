#pragma once

#include "GPUGrassData.h"
#include "GPUGrassResidency.h"
#include "xrCommon/xr_vector.h"

namespace xray::render::RENDER_NAMESPACE::gpu_grass
{
struct TileResourceSlice
{
    u32 tile_index = 0;
    TilePayload payload{};

    u32 tile_span = 0;
    u32 tile_resolution = 0;
    float tile_world_size = 0.f;

    const PlacementSeed* seeds = nullptr;
    u32 seed_count = 0;

    const SlotReference* slot_refs = nullptr;
    u32 slot_count = 0;

    const OfflineAsset::SlotHeightInfo* slot_heights = nullptr;

    const u8* palette_data = nullptr;
    u32 palette_bytes = 0;

    const u8* object_ids = nullptr; // 4 entries per slot (id0..id3)

    const u8* height_data = nullptr;
    u32 height_bytes = 0;
};

TileResourceSlice BuildTileSlice(const OfflineAsset& asset, u32 tile_index);

class PlacementStreamingContext
{
public:
    PlacementStreamingContext() = default;

    void Initialize(const OfflineAsset* asset);
    void Reset();

    bool IsInitialized() const { return m_asset != nullptr; }

    void EnqueueEvents(const xr_vector<TileEvent>& events);

    const xr_vector<TileResourceSlice>& PendingLoads() const { return m_pending_loads; }
    const xr_vector<u32>& PendingUnloads() const { return m_pending_unloads; }

    void Clear();

private:
    const OfflineAsset* m_asset = nullptr;
    xr_vector<TileResourceSlice> m_pending_loads;
    xr_vector<u32> m_pending_unloads;
};

} // namespace xray::render::RENDER_NAMESPACE::gpu_grass
