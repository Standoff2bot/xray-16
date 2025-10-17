#include "stdafx.h"
#pragma hdrstop

#include "GPUGrassData.h"

#include "xrCommon/xr_map.h"
#include "xrCDB/Intersect.hpp"
#include "xrCDB/xrXRC.h"
#include "xrEngine/IGame_Level.h"
#include "xrMaterialSystem/GameMtlLib.h"
#include "xrCore/Threading/ParallelFor.hpp"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <limits>

extern ENGINE_API IGame_Level* g_pGameLevel;

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

inline u16 QuantizeHeight(float height)
{
    const float normalized = (height + kHeightQuantOffset) / kHeightQuantStep;
    const float clamped = std::clamp(normalized, 0.f, 65535.f);
    return static_cast<u16>(clamped + 0.5f);
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
    const u32 tile_resolution = std::max<u32>(1, config.tile_resolution);
    const u32 tile_sample_count = tile_resolution * tile_resolution;

    xr_vector<xr_vector<u8>> tile_palette(tile_count);
    xr_vector<xr_vector<u8>> tile_heights(tile_count);

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
    xr_vector<u32> tile_valid_samples(tile_count, 0);
    xr_vector<float> tile_min_height(tile_count, FLT_MAX);
    xr_vector<float> tile_max_height(tile_count, -FLT_MAX);

    struct TileBakeContext
    {
        ClipmapConfig config;
        s32 tile_span;
        u32 palette_layers;
        u32 layer_stride;
        u32 tile_palette_bytes;
        u32 tile_resolution;
        u32 tile_sample_count;
        const DetailHeader* header;
        const DetailSlot* slots;
        xr_vector<std::pair<s32, s32>>* tile_keys;
        xr_vector<const TileAccumulator*>* tile_entries;
        xr_vector<u32>* tile_offsets;
        xr_vector<xr_vector<u8>>* tile_palette;
        xr_vector<xr_vector<u8>>* tile_heights;
        xr_vector<u32>* slot_tile_indices;
        xr_vector<float>* tile_times;
        xr_vector<u32>* tile_seeds;
        xr_vector<u32>* tile_valid_samples;
        xr_vector<float>* tile_min_height;
        xr_vector<float>* tile_max_height;
        OfflineBakeResult* result;
    };

    TileBakeContext context = {
        config,
        tile_span,
        palette_layers,
        layer_stride,
        tile_palette_bytes,
        tile_resolution,
        tile_sample_count,
        input.header,
        input.slots,
        &tile_keys,
        &tile_entries,
        &tile_offsets,
        &tile_palette,
        &tile_heights,
        &slot_tile_indices,
        &tile_times,
        &tile_seeds,
        &tile_valid_samples,
        &tile_min_height,
        &tile_max_height,
        &result
    };

    auto ProcessTileRange = [](TileBakeContext& ctx, const TaskRange<size_t>& range)
    {
        auto& config = ctx.config;
        auto& tile_keys = *ctx.tile_keys;
        auto& tile_entries = *ctx.tile_entries;
        auto& tile_offsets = *ctx.tile_offsets;
        auto& tile_palette = *ctx.tile_palette;
        auto& tile_heights = *ctx.tile_heights;
        auto& slot_tile_indices = *ctx.slot_tile_indices;
        auto& tile_times = *ctx.tile_times;
        auto& tile_seeds = *ctx.tile_seeds;
        auto& tile_valid_samples = *ctx.tile_valid_samples;
        auto& tile_min_height = *ctx.tile_min_height;
        auto& tile_max_height = *ctx.tile_max_height;
        auto& result = *ctx.result;

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
            result.asset.tiles[idx].palette_bytes = ctx.tile_palette_bytes;

            const u32 offset = tile_offsets[idx];
            const size_t count = accumulator.slots.size();

            tile_palette[idx].assign(ctx.tile_palette_bytes, 0);

            xr_vector<int> slot_lookup(ctx.tile_span * ctx.tile_span, -1);
            xr_vector<float> slot_min(ctx.tile_span * ctx.tile_span, FLT_MAX);
            xr_vector<float> slot_max(ctx.tile_span * ctx.tile_span, -FLT_MAX);

            float tile_min_y = FLT_MAX;
            float tile_max_y = -FLT_MAX;

            u8* palette_base = tile_palette[idx].data();

            for (size_t local = 0; local < count; ++local)
            {
                const SlotReference& slot_ref = accumulator.slots[local];
                const u32 slot_table_index = offset + static_cast<u32>(local);
                result.asset.slot_table[slot_table_index] = slot_ref;
                slot_tile_indices[slot_ref.slot_index] = static_cast<u32>(idx);

                const u32 lut_index = slot_ref.slot_local_z * ctx.tile_span + slot_ref.slot_local_x;
                slot_lookup[lut_index] = static_cast<int>(slot_table_index);

                int sx, sz;
                ctx.header->slot_x_z(slot_ref.slot_index, sx, sz);

                const DetailSlot& slot = ctx.slots[slot_ref.slot_index];
                PlacementSeed seed = {};
                seed.slot_x = static_cast<u32>(sx);
                seed.slot_z = static_cast<u32>(sz);
                seed.base_seed = MixSeed(seed.slot_x, seed.slot_z);
                seed.object_mask = BuildObjectMask(slot);
                seed.density_scale = ComputeDensityScale(slot);

                result.asset.placement_seeds[slot_table_index] = seed;

                const float slot_base = slot.r_ybase();
                const float slot_top = slot_base + slot.r_yheight();
                tile_min_y = std::min(tile_min_y, slot_base);
                tile_max_y = std::max(tile_max_y, slot_top);

                const u32 texel_index = slot_ref.slot_local_z * ctx.tile_span + slot_ref.slot_local_x;
                for (u32 obj = 0; obj < ctx.palette_layers; ++obj)
                {
                    const u8 detail_id = slot.r_id(obj);
                    u8* dest = palette_base + (obj * ctx.layer_stride + texel_index * 4);
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

            if (tile_min_y == FLT_MAX)
            {
                tile_min_y = -10.f;
                tile_max_y = 10.f;
            }
            else
            {
                tile_min_y -= 1.f;
                tile_max_y += 5.f;
            }

            xr_vector<u16> height_quant(ctx.tile_sample_count, QuantizeHeight(tile_min_y));
            float local_min_height = FLT_MAX;
            float local_max_height = -FLT_MAX;
            u32 valid_samples = 0;
            if (g_pGameLevel)
            {
                const CDB::MODEL* model = g_pGameLevel->ObjectSpace.GetStaticModel();
                const CDB::TRI* tris = g_pGameLevel->ObjectSpace.GetStaticTris();
                const Fvector* verts = g_pGameLevel->ObjectSpace.GetStaticVerts();

                if (model && tris && verts)
                {
                    Fbox tile_bounds;
                    const float tile_origin_x = static_cast<float>(key.first * ctx.tile_span) * DETAIL_SLOT_SIZE;
                    const float tile_origin_z = static_cast<float>(key.second * ctx.tile_span) * DETAIL_SLOT_SIZE;
                    tile_bounds.vMin.set(tile_origin_x, tile_min_y, tile_origin_z);
                    tile_bounds.vMax.set(tile_origin_x + config.tile_world_size, tile_max_y, tile_origin_z + config.tile_world_size);

                    Fvector center, dims;
                    tile_bounds.get_CD(center, dims);

                    xrXRC tile_query;
                    tile_query.box_query(CDB::OPT_FULL_TEST, model, center, dims);

                    const u32 tri_count = tile_query.r_count();
                    if (tri_count > 0)
                    {
                        const auto* results = tile_query.r_begin();
                        const float step = config.tile_world_size / static_cast<float>(ctx.tile_resolution);
                        const float ray_start = tile_max_y + 0.1f;
                        const Fvector dir = {0.f, -1.f, 0.f};

                        for (u32 z = 0; z < ctx.tile_resolution; ++z)
                        {
                            for (u32 x = 0; x < ctx.tile_resolution; ++x)
                            {
                                const u32 sample_index = z * ctx.tile_resolution + x;

                                Fvector origin;
                                origin.x = tile_origin_x + (static_cast<float>(x) + 0.5f) * step;
                                origin.z = tile_origin_z + (static_cast<float>(z) + 0.5f) * step;
                                origin.y = ray_start;

                                float best_y = -FLT_MAX;
                                for (u32 tri_idx = 0; tri_idx < tri_count; ++tri_idx)
                                {
                                    const CDB::TRI& tri = tris[results[tri_idx].id];
                                    SGameMtl* mtl = GMLib.GetMaterialByIdx(tri.material);
                                    if (mtl && mtl->Flags.test(SGameMtl::flPassable))
                                        continue;

                                    Fvector tv[3] = {verts[tri.verts[0]], verts[tri.verts[1]], verts[tri.verts[2]]};
                                    float r_u, r_v, r_range;
                                    if (CDB::TestRayTri(origin, dir, tv, r_u, r_v, r_range, TRUE) && r_range >= 0.f)
                                    {
                                        const float hit_y = origin.y - r_range;
                                        if (hit_y > best_y)
                                            best_y = hit_y;
                                    }
                                }

                                float final_height;
                                if (best_y <= -FLT_MAX * 0.5f)
                                {
                                    final_height = tile_min_y;
                                }
                                else
                                {
                                    final_height = best_y;
                                    ++valid_samples;
                                }

                                local_min_height = std::min(local_min_height, final_height);
                                local_max_height = std::max(local_max_height, final_height);
                                height_quant[sample_index] = QuantizeHeight(final_height);

                                const float norm_x = (static_cast<float>(x) + 0.5f) / static_cast<float>(ctx.tile_resolution);
                                const float norm_z = (static_cast<float>(z) + 0.5f) / static_cast<float>(ctx.tile_resolution);
                                const u32 slot_x = std::min<u32>(ctx.tile_span - 1, static_cast<u32>(norm_x * ctx.tile_span));
                                const u32 slot_z = std::min<u32>(ctx.tile_span - 1, static_cast<u32>(norm_z * ctx.tile_span));
                                const u32 lut_index = slot_z * ctx.tile_span + slot_x;
                                const int slot_table_index = slot_lookup[lut_index];
                                if (slot_table_index >= 0)
                                {
                                    slot_min[lut_index] = std::min(slot_min[lut_index], final_height);
                                    slot_max[lut_index] = std::max(slot_max[lut_index], final_height);
                                }
                            }
                        }
                    }
                    else
                    {
                        local_min_height = tile_min_y;
                        local_max_height = tile_max_y;
                    }
                }
            }

            if (local_min_height == FLT_MAX)
                local_min_height = tile_min_y;
            if (local_max_height == -FLT_MAX)
                local_max_height = tile_max_y;

            tile_valid_samples[idx] = valid_samples;
            tile_min_height[idx] = local_min_height;
            tile_max_height[idx] = local_max_height;

            tile_heights[idx].resize(height_quant.size() * sizeof(u16));
            std::memcpy(tile_heights[idx].data(), height_quant.data(), tile_heights[idx].size());
            result.asset.tiles[idx].height_bytes = static_cast<u32>(tile_heights[idx].size());

            for (size_t local = 0; local < count; ++local)
            {
                const SlotReference& slot_ref = accumulator.slots[local];
                const u32 lut_index = slot_ref.slot_local_z * ctx.tile_span + slot_ref.slot_local_x;
                const int slot_table_index = slot_lookup[lut_index];
                const DetailSlot& slot = ctx.slots[slot_ref.slot_index];
                const float fallback_base = slot.r_ybase();
                const float fallback_top = fallback_base + slot.r_yheight();

                auto& height_info = result.asset.slot_heights[offset + local];
                if (slot_table_index >= 0 && slot_min[lut_index] != FLT_MAX && slot_max[lut_index] != -FLT_MAX)
                {
                    height_info.min_height = slot_min[lut_index];
                    height_info.max_height = slot_max[lut_index];
                }
                else
                {
                    height_info.min_height = fallback_base;
                    height_info.max_height = fallback_top;
                }
            }

            tile_times[idx] = tile_timer.GetElapsed_sec() * 1000.f;
            tile_seeds[idx] = static_cast<u32>(count);
        }
    };

    xr_parallel_for(TaskRange<size_t>(0, tile_count), [&context, &ProcessTileRange](const TaskRange<size_t>& range)
    {
        ProcessTileRange(context, range);
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
    size_t height_total = 0;
    for (size_t idx = 0; idx < tile_count; ++idx)
    {
        palette_total += tile_palette[idx].size();
        height_total += tile_heights[idx].size();
    }

    result.asset.palette_bytes.resize(palette_total);
    result.asset.height_bytes.resize(height_total);

    size_t palette_offset = 0;
    size_t height_offset = 0;
    for (size_t idx = 0; idx < tile_count; ++idx)
    {
        TilePayload& payload = result.asset.tiles[idx];
        payload.palette_offset = static_cast<u32>(palette_offset);
        payload.height_offset = static_cast<u32>(height_offset);

        if (!tile_palette[idx].empty())
        {
            std::memcpy(result.asset.palette_bytes.data() + palette_offset, tile_palette[idx].data(), tile_palette[idx].size());
            palette_offset += tile_palette[idx].size();
        }

        if (!tile_heights[idx].empty())
        {
            std::memcpy(result.asset.height_bytes.data() + height_offset, tile_heights[idx].data(), tile_heights[idx].size());
            height_offset += tile_heights[idx].size();
        }
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

    u64 total_valid = 0;
    float min_tile_height = FLT_MAX;
    float max_tile_height = -FLT_MAX;
    for (size_t idx = 0; idx < tile_count; ++idx)
    {
        total_valid += tile_valid_samples[idx];
        min_tile_height = std::min(min_tile_height, tile_min_height[idx]);
        max_tile_height = std::max(max_tile_height, tile_max_height[idx]);
    }

    if (tile_count == 0)
    {
        max_tile_time = 0.f;
        max_tile_seeds = 0;
        max_tile_x = max_tile_z = 0;
        min_tile_height = max_tile_height = 0.f;
    }

    float slot_min_height = FLT_MAX;
    float slot_max_height = -FLT_MAX;
    for (const auto& h : result.asset.slot_heights)
    {
        slot_min_height = std::min(slot_min_height, h.min_height);
        slot_max_height = std::max(slot_max_height, h.max_height);
    }
    if (result.asset.slot_heights.empty())
        slot_min_height = slot_max_height = 0.f;

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
    Msg("* [GPUGrass] Height memory: %zu bytes (%.2f MB)", height_total, height_total / (1024.f * 1024.f));
    Msg("* [GPUGrass] Tile height range: [%.2f .. %.2f], valid samples: %llu",
        min_tile_height,
        max_tile_height,
        total_valid);
    Msg("* [GPUGrass] Slot height range: [%.2f .. %.2f]", slot_min_height, slot_max_height);
    Msg("* [GPUGrass] OfflineBaker::Build time: %.3f ms", timer.GetElapsed_sec() * 1000.f);

    return true;
}

} // namespace xray::render::RENDER_NAMESPACE::gpu_grass
