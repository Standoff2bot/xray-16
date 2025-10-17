#include "stdafx.h"
#pragma hdrstop

#include "GPUGrassResidency.h"

#include <cmath>

namespace xray::render::RENDER_NAMESPACE::gpu_grass
{
namespace
{
TileKey MakeTileKey(s32 tile_x, s32 tile_z)
{
    TileKey key;
    key.x = tile_x;
    key.z = tile_z;
    return key;
}
} // namespace

void ResidencyManager::Initialize(const OfflineAsset* asset)
{
    m_asset = asset;
    Reset();

    if (!m_asset)
        return;

    m_config = m_asset->header.config;
    m_tile_span = std::max<u32>(1, static_cast<u32>(std::round(m_config.tile_world_size / DETAIL_SLOT_SIZE)));

    m_tile_lookup.clear();
    for (u32 idx = 0; idx < m_asset->tiles.size(); ++idx)
    {
        const TilePayload& payload = m_asset->tiles[idx];
        TileKey key = MakeTileKey(static_cast<s32>(payload.coord.region_x), static_cast<s32>(payload.coord.region_z));
        m_tile_lookup.emplace(key, idx);
    }
}

void ResidencyManager::Reset()
{
    m_active_tiles.clear();
    m_pending_events.clear();
}

void ResidencyManager::Update(const Fvector& camera_position)
{
    if (!m_asset)
        return;

    m_pending_events.clear();

    const float world_x = camera_position.x;
    const float world_z = camera_position.z;
    const s32 slot_x = static_cast<s32>(std::floor(world_x / DETAIL_SLOT_SIZE));
    const s32 slot_z = static_cast<s32>(std::floor(world_z / DETAIL_SLOT_SIZE));
    const s32 tile_x = FloorDiv(slot_x, static_cast<s32>(m_tile_span));
    const s32 tile_z = FloorDiv(slot_z, static_cast<s32>(m_tile_span));

    xr_map<TileKey, u32> desired_tiles;
    const s32 ring_extent = static_cast<s32>(m_config.ring_count);
    for (s32 dz = -ring_extent; dz <= ring_extent; ++dz)
    {
        for (s32 dx = -ring_extent; dx <= ring_extent; ++dx)
        {
            const TileKey key = MakeTileKey(tile_x + dx, tile_z + dz);
            auto lookup_it = m_tile_lookup.find(key);
            if (lookup_it == m_tile_lookup.end())
                continue;

            desired_tiles.emplace(key, lookup_it->second);
        }
    }

    // Load new tiles
    for (const auto& [key, tile_index] : desired_tiles)
    {
        if (m_active_tiles.find(key) != m_active_tiles.end())
            continue;

        m_active_tiles.emplace(key, tile_index);
        TileEvent evt = {};
        evt.type = TileEvent::Type::Load;
        evt.tile_index = tile_index;
        evt.key = key;
        m_pending_events.emplace_back(evt);
    }

    // Unload tiles no longer needed
    xr_vector<TileKey> tiles_to_remove;
    for (const auto& [key, active_index] : m_active_tiles)
    {
        if (desired_tiles.find(key) != desired_tiles.end())
            continue;

        TileEvent evt = {};
        evt.type = TileEvent::Type::Unload;
        evt.tile_index = active_index;
        evt.key = key;
        m_pending_events.emplace_back(evt);

        tiles_to_remove.emplace_back(key);
    }

    for (const TileKey& key : tiles_to_remove)
        m_active_tiles.erase(key);
}

xr_vector<u32> ResidencyManager::GetResidentTileIndices() const
{
    xr_vector<u32> indices;
    indices.reserve(m_active_tiles.size());
    for (const auto& [key, index] : m_active_tiles)
        indices.emplace_back(index);
    return indices;
}

bool ResidencyManager::IsTileResident(u32 tile_index) const
{
    for (const auto& [key, index] : m_active_tiles)
        if (index == tile_index)
            return true;
    return false;
}

s32 ResidencyManager::FloorDiv(s32 value, s32 divisor)
{
    VERIFY(divisor > 0);
    if (value >= 0)
        return value / divisor;

    return -static_cast<s32>((static_cast<u32>(-value) + divisor - 1) / divisor);
}

} // namespace xray::render::RENDER_NAMESPACE::gpu_grass
