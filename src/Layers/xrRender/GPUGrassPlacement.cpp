#include "stdafx.h"
#pragma hdrstop

#include "GPUGrassPlacement.h"

namespace xray::render::RENDER_NAMESPACE::gpu_grass
{
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

            const TilePayload& payload = m_asset->tiles[evt.tile_index];

            TileResourceSlice slice;
            slice.tile_index = evt.tile_index;
            slice.payload = payload;

            slice.slot_refs = m_asset->slot_table.data() + payload.slot_offset;
            slice.slot_count = payload.slot_count;

            slice.seeds = m_asset->placement_seeds.data() + payload.slot_offset;
            slice.seed_count = payload.slot_count;

            slice.slot_heights = m_asset->slot_heights.data() + payload.slot_offset;

            if (payload.palette_bytes > 0)
            {
                slice.palette_data = m_asset->palette_bytes.data() + payload.palette_offset;
                slice.palette_bytes = payload.palette_bytes;
            }

            if (payload.height_bytes > 0)
            {
                slice.height_data = m_asset->height_bytes.data() + payload.height_offset;
                slice.height_bytes = payload.height_bytes;
            }

            m_pending_loads.emplace_back(slice);
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
