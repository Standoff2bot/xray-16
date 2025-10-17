#include "stdafx.h"
#pragma hdrstop

#include "GPUGrassData.h"

#include "xrCommon/xr_map.h"
#include "xrCore/Threading/ParallelFor.hpp"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <limits>

namespace xray::render::RENDER_NAMESPACE::gpu_grass
{
namespace
{
constexpr float kEpsilon = 1e-6f;

inline u32 SnapSlotsPerTile(float tile_world_size)
{
    const float slot_world = DETAIL_SLOT_SIZE;
    if (tile_world_size <= kEpsilon)
        return 1;

    const float slots_per_tile = tile_world_size / slot_world;
    const float snapped = std::ceil(slots_per_tile - kEpsilon);
    return std::max<u32>(1, static_cast<u32>(snapped));
}

inline s32 FloorDiv(s32 value, s32 divisor)
{
    VERIFY(divisor > 0);
    if (value >= 0)
        return value / divisor;

    return -static_cast<s32>((static_cast<u32>(-value) + divisor - 1) / divisor);
}

struct TileAccumulator
{
    xr_vector<SlotReference> slots;
};

inline u8 NibbleToByte(u8 nibble)
{
    return static_cast<u8>((static_cast<u32>(nibble) * 255u + 7u) / 15u);
}

inline u64 BuildObjectMask(DetailSlot slot)
{
    u64 mask = 0;
    for (u32 i = 0; i < dm_obj_in_slot; ++i)
    {
        const u8 id = slot.r_id(i);
        if (id == DetailSlot::ID_Empty)
            continue;
        mask |= (1ull << id);
    }
    return mask;
}

inline float ComputeDensityScale(DetailSlot slot)
{
    float total = 0.f;
    u32 samples = 0;

    for (u32 obj = 0; obj < dm_obj_in_slot; ++obj)
    {
        const DetailPalette& palette = slot.palette[obj];
        const u8 id = slot.r_id(obj);
        if (id == DetailSlot::ID_Empty)
            continue;

        total += float(palette.a0);
        total += float(palette.a1);
        total += float(palette.a2);
        total += float(palette.a3);
        samples += 4;
    }

    if (samples == 0)
        return 0.f;

    const float normalized = total / (float(samples) * 15.f);
    return std::clamp(normalized, 0.f, 1.f);
}

inline u32 MixSeed(u32 slot_x, u32 slot_z)
{
    u32 seed = 0xA511E9B5u;
    seed ^= slot_x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= slot_z + 0x7f4a7c15 + (seed << 7) + (seed >> 3);
    seed ^= (slot_x * 73856093u) ^ (slot_z * 19349663u);
    seed ^= seed >> 16;
    seed *= 0x45d9f3bu;
    seed ^= seed >> 16;
    return seed;
}

} // namespace

bool OfflineBaker::Build(const OfflineBakeInput& input, OfflineBakeResult& result)
{
    CTimer timer;
    timer.Start();

    if (!input.header || !input.slots)
    {
        Msg("! [GPUGrass] OfflineBaker::Build failed: missing header (%p) or slots (%p)", input.header, input.slots);
        return false;
    }

    const u32 total_slots = input.header->slot_count();
    Msg("* [GPUGrass] OfflineBaker::Build start - total slots: %u", total_slots);

    result.asset = {};
    result.slot_to_tile.clear();

    if (total_slots == 0)
    {
        result.asset.header.config = input.config;
        result.asset.header.tile_count = 0;
        Msg("~ [GPUGrass] No slots to process - exiting early");
        return true;
    }

    ClipmapConfig config = input.config;
    config.ring_count = std::max<u32>(1, config.ring_count);
    config.tile_resolution = std::max<u32>(1, config.tile_resolution);
    config.max_tiles_per_ring = std::max<u32>(config.ring_count, config.max_tiles_per_ring);

    const u32 slots_per_tile = SnapSlotsPerTile(config.tile_world_size);
    config.tile_world_size = std::max<float>(DETAIL_SLOT_SIZE, slots_per_tile * DETAIL_SLOT_SIZE);
    const s32 tile_span = static_cast<s32>(slots_per_tile);

    Msg("* [GPUGrass] Config normalized - tiles per span: %u, tile size: %.2f, rings: %u, max tiles/ring: %u",
        slots_per_tile, config.tile_world_size, config.ring_count, config.max_tiles_per_ring);

    xr_map<std::pair<s32, s32>, TileAccumulator> tile_accumulators;

    result.slot_to_tile.reserve(total_slots);
    result.asset.placement_seeds.clear();
    result.asset.placement_seeds.reserve(total_slots);

    for (u32 slot_index = 0; slot_index < total_slots; ++slot_index)
    {
        int sx, sz;
        input.header->slot_x_z(slot_index, sx, sz);

        const s32 tile_x = FloorDiv(sx, tile_span);
        const s32 tile_z = FloorDiv(sz, tile_span);
        const std::pair<s32, s32> tile_key(tile_x, tile_z);

        TileAccumulator& accumulator = tile_accumulators[tile_key];

        SlotReference ref = {};
        ref.slot_index = slot_index;

        const s32 tile_origin_x = tile_x * tile_span;
        const s32 tile_origin_z = tile_z * tile_span;
        ref.slot_local_x = static_cast<u16>(sx - tile_origin_x);
        ref.slot_local_z = static_cast<u16>(sz - tile_origin_z);

        accumulator.slots.emplace_back(ref);
    }

    result.asset.header.magic = OfflineAssetHeader::kMagic;
    result.asset.header.version = OfflineAssetHeader::kVersion;
    result.asset.header.config = config;

    const size_t tile_count = tile_accumulators.size();
    result.asset.tiles.resize(tile_count);
    result.asset.slot_table.resize(total_slots);
    result.asset.placement_seeds.resize(total_slots);
    result.asset.slot_heights.resize(total_slots);

    const u32 palette_layers = dm_obj_in_slot;
    const u32 tile_texels = tile_span * tile_span;
    const u32 layer_stride = tile_texels * 4; // RGBA per texel
    const u32 tile_palette_bytes = layer_stride * palette_layers;

    xr_vector<xr_vector<u8>> tile_palette(tile_count);
    for (size_t i = 0; i < tile_count; ++i)
        tile_palette[i].assign(tile_palette_bytes, 0);

    xr_vector<const TileAccumulator*> tile_entries;
    tile_entries.reserve(tile_count);
    xr_vector<std::pair<s32, s32>> tile_keys;
    tile_keys.reserve(tile_count);
    for (const auto& entry : tile_accumulators)
    {
        tile_keys.emplace_back(entry.first);
        tile_entries.emplace_back(&entry.second);
    }

    xr_vector<u32> tile_offsets(tile_count);
    u32 slot_offset = 0;
    for (size_t i = 0; i < tile_count; ++i)
    {
        tile_offsets[i] = slot_offset;
        slot_offset += static_cast<u32>(tile_entries[i]->slots.size());
    }

    xr_vector<u32> slot_tile_indices(total_slots, u32(-1));
    xr_vector<float> tile_times(tile_count, 0.f);
    xr_vector<u32> tile_seeds(tile_count, 0);

    xr_parallel_for(TaskRange<size_t>(0, tile_count), [&](const TaskRange<size_t>& range)
    {
        for (size_t idx = range.begin(); idx != range.end(); ++idx)
        {
            CTimer tile_timer;
            tile_timer.Start();

            const auto& key = tile_keys[idx];
            const TileAccumulator& accumulator = *tile_entries[idx];

            TilePayload payload = {};
            payload.coord.region_x = key.first;
            payload.coord.region_z = key.second;
            payload.coord.ring = 0;
            payload.coord.local_x = 0;
            payload.coord.local_z = 0;

            payload.slot_offset = tile_offsets[idx];
            payload.slot_count = static_cast<u32>(accumulator.slots.size());
            result.asset.tiles[idx] = payload;
            result.asset.tiles[idx].palette_bytes = tile_palette_bytes;

            const u32 offset = tile_offsets[idx];
            const size_t count = accumulator.slots.size();
            u8* palette_base = tile_palette[idx].data();
            for (size_t local = 0; local < count; ++local)
            {
                const SlotReference& slot_ref = accumulator.slots[local];
                result.asset.slot_table[offset + local] = slot_ref;
                slot_tile_indices[slot_ref.slot_index] = static_cast<u32>(idx);

                int sx, sz;
                input.header->slot_x_z(slot_ref.slot_index, sx, sz);

                const DetailSlot& slot = input.slots[slot_ref.slot_index];
                PlacementSeed seed = {};
                seed.slot_x = static_cast<u32>(sx);
                seed.slot_z = static_cast<u32>(sz);
                seed.base_seed = MixSeed(seed.slot_x, seed.slot_z);
                seed.object_mask = BuildObjectMask(slot);
                seed.density_scale = ComputeDensityScale(slot);

                result.asset.placement_seeds[offset + local] = seed;
                result.asset.slot_heights[offset + local].set(slot.r_ybase(), slot.r_yheight());

                const u32 texel_index = slot_ref.slot_local_z * tile_span + slot_ref.slot_local_x;
                for (u32 obj = 0; obj < palette_layers; ++obj)
                {
                    const u8 detail_id = slot.r_id(obj);
                    u8* dest = palette_base + (obj * layer_stride + texel_index * 4);
                    if (detail_id == DetailSlot::ID_Empty)
                    {
                        dest[0] = dest[1] = dest[2] = dest[3] = 0;
                        continue;
                    }

                    const DetailPalette& pal = slot.palette[obj];
                    dest[0] = NibbleToByte(pal.a0);
                    dest[1] = NibbleToByte(pal.a1);
                    dest[2] = NibbleToByte(pal.a2);
                    dest[3] = NibbleToByte(pal.a3);
                }
            }

            tile_times[idx] = tile_timer.GetElapsed_sec() * 1000.f;
            tile_seeds[idx] = static_cast<u32>(count);
        }
    });

    result.slot_to_tile.clear();
    result.slot_to_tile.reserve(total_slots);
    for (u32 slot_index = 0; slot_index < total_slots; ++slot_index)
    {
        const u32 tile_idx = slot_tile_indices[slot_index];
        if (tile_idx != u32(-1))
        {
            result.slot_to_tile.emplace(slot_index, tile_idx);
        }
    }

    size_t palette_total = 0;
    for (size_t idx = 0; idx < tile_count; ++idx)
        palette_total += tile_palette[idx].size();

    result.asset.palette_bytes.resize(palette_total);
    size_t palette_offset = 0;
    for (size_t idx = 0; idx < tile_count; ++idx)
    {
        TilePayload& payload = result.asset.tiles[idx];
        payload.palette_offset = static_cast<u32>(palette_offset);
        std::memcpy(result.asset.palette_bytes.data() + palette_offset, tile_palette[idx].data(), tile_palette[idx].size());
        palette_offset += tile_palette[idx].size();
    }

    float max_tile_time = -FLT_MAX;
    u32 max_tile_seeds = 0;
    s32 max_tile_x = 0;
    s32 max_tile_z = 0;
    u32 heavy_tiles = 0;

    for (size_t idx = 0; idx < tile_count; ++idx)
    {
        if (tile_times[idx] > 2.0f)
        {
            ++heavy_tiles;
            Msg("~ [GPUGrass] Tile (%d,%d) heavy - seeds: %u, time: %.3f ms",
                tile_keys[idx].first, tile_keys[idx].second, tile_seeds[idx], tile_times[idx]);
        }

        if (tile_times[idx] >= max_tile_time)
        {
            max_tile_time = tile_times[idx];
            max_tile_seeds = tile_seeds[idx];
            max_tile_x = tile_keys[idx].first;
            max_tile_z = tile_keys[idx].second;
        }
    }

    if (tile_count == 0)
    {
        max_tile_time = 0.f;
        max_tile_seeds = 0;
        max_tile_x = max_tile_z = 0;
    }

    float min_base = FLT_MAX;
    float max_base = -FLT_MAX;
    float min_top = FLT_MAX;
    float max_top = -FLT_MAX;
    for (const Fvector2& h : result.asset.slot_heights)
    {
        const float base = h.x;
        const float top = h.x + h.y;
        min_base = std::min(min_base, base);
        max_base = std::max(max_base, base);
        min_top = std::min(min_top, top);
        max_top = std::max(max_top, top);
    }
    if (result.asset.slot_heights.empty())
    {
        min_base = max_base = min_top = max_top = 0.f;
    }

    result.asset.header.tile_count = static_cast<u32>(result.asset.tiles.size());
    Msg("* [GPUGrass] OfflineBaker::Build complete - tiles: %u, seeds: %zu, slot-table entries: %zu",
        result.asset.header.tile_count,
        result.asset.placement_seeds.size(),
        result.asset.slot_table.size());
    Msg("* [GPUGrass] Seed stats - total: %zu, max per tile: %u @ (%d,%d); peak tile time %.3f ms; heavy tile count %u",
        result.asset.placement_seeds.size(),
        max_tile_seeds,
        max_tile_x,
        max_tile_z,
        max_tile_time,
        heavy_tiles);
    Msg("* [GPUGrass] Palette memory: %zu bytes (%.2f MB)", palette_total, palette_total / (1024.f * 1024.f));
    Msg("* [GPUGrass] Height stats - base[%.2f..%.2f], top[%.2f..%.2f]", min_base, max_base, min_top, max_top);
    Msg("* [GPUGrass] OfflineBaker::Build time: %.3f ms", timer.GetElapsed_sec() * 1000.f);

     return true;
}

} // namespace xray::render::RENDER_NAMESPACE::gpu_grass
