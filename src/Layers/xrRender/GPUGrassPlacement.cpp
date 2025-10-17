#include "stdafx.h"
#pragma hdrstop

#include "GPUGrassPlacement.h"
#include "GPUGrassData.h"
#include "DetailFormat.h"

#include <algorithm>

namespace xray::render::RENDER_NAMESPACE::gpu_grass
{
TileResourceSlice BuildTileSlice(const OfflineAsset& asset, u32 tile_index)
{
    TileResourceSlice slice;
    if (tile_index >= asset.tiles.size())
        return slice;

    const TilePayload& payload = asset.tiles[tile_index];
    slice.tile_index = tile_index;
    slice.payload = payload;
    slice.tile_span = std::max<u32>(1, static_cast<u32>(asset.header.config.tile_world_size / DETAIL_SLOT_SIZE + 0.5f));
    slice.tile_world_size = asset.header.config.tile_world_size;
    slice.sample_dim = asset.sample_dim;
    slice.samples_per_slot = asset.samples_per_slot;
    slice.world_origin_x = payload.world_origin_x;
    slice.world_origin_z = payload.world_origin_z;
    slice.slot_refs = asset.slot_table.data() + payload.slot_offset;
    slice.slot_count = payload.slot_count;
    slice.seeds = asset.placement_seeds.data() + payload.slot_offset;
    slice.seed_count = payload.slot_count;
    slice.slot_heights = asset.slot_heights.data() + payload.slot_offset;
    slice.object_ids = asset.slot_object_ids.data() + payload.slot_offset * 4;

    if (payload.palette_bytes > 0)
    {
        slice.palette_data = asset.palette_bytes.data() + payload.palette_offset;
        slice.palette_bytes = payload.palette_bytes;
    }

    if (payload.height_bytes > 0)
    {
        slice.height_data = asset.height_bytes.data() + payload.height_offset;
        slice.height_bytes = payload.height_bytes;
    }

    return slice;
}

void PlacementStreamingContext::Initialize(const OfflineAsset* asset)
{
    m_asset = asset;
    Clear();
}

void PlacementStreamingContext::Reset()
{
    m_asset = nullptr;
    Clear();
}

void PlacementStreamingContext::EnqueueEvents(const xr_vector<TileEvent>& events)
{
    if (!m_asset)
        return;

    for (const TileEvent& evt : events)
    {
        if (evt.type == TileEvent::Type::Load)
        {
            if (evt.tile_index >= m_asset->tiles.size())
                continue;

            m_pending_loads.emplace_back(BuildTileSlice(*m_asset, evt.tile_index));
        }
        else if (evt.type == TileEvent::Type::Unload)
        {
            m_pending_unloads.emplace_back(evt.tile_index);
        }
    }
}

void PlacementStreamingContext::Clear()
{
    m_pending_loads.clear();
    m_pending_unloads.clear();
}

} // namespace xray::render::RENDER_NAMESPACE::gpu_grass
