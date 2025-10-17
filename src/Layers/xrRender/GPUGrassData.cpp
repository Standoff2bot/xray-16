#include "stdafx.h"
#pragma hdrstop

#include "GPUGrassData.h"

#include "xrCommon/xr_map.h"
#include "xrCore/Threading/ParallelFor.hpp"
#include "xrCDB/Intersect.hpp"
#include "xrCDB/xrXRC.h"
#include "xrMaterialSystem/GameMtlLib.h"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <limits>
#include <memory>

#ifndef _EDITOR
extern ENGINE_API IGame_Level* g_pGameLevel;
extern ECORE_API float ps_r__Detail_density;
#endif

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
            u32 index = 0;
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
            const float clamped = std::clamp(normalized, 0.f, 65534.f);
            return static_cast<u16>(clamped + 0.5f);
        }

        inline u32 PlacementHash(u32 a, u32 b, u32 c, u32 seed)
        {
            u32 h = seed;
            h ^= a * 1664525u;
            h ^= b * 1013904223u;
            h ^= c * 2246822519u;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            return h;
        }

        inline float Random01(u32 hash)
        {
            return (hash & 0x00FFFFFFu) / 16777216.0f;
        }

        inline float RandomSigned(u32 hash, float amplitude)
        {
            return (Random01(hash) * 2.0f - 1.0f) * amplitude;
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

        xr_map<std::pair<s32, s32>, std::unique_ptr<TileAccumulator>> tile_accumulators;

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

            auto& accumulatorPtr = tile_accumulators[tile_key];
            if (!accumulatorPtr)
                accumulatorPtr = std::make_unique<TileAccumulator>();
            TileAccumulator& accumulator = *accumulatorPtr;

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
        result.asset.slot_object_ids.resize(total_slots * dm_obj_in_slot);

        const u32 palette_layers = dm_obj_in_slot;
        const u32 tile_texels = tile_span * tile_span;
        const u32 layer_stride = tile_texels * 4; // RGBA per texel
        const u32 tile_palette_bytes = layer_stride * palette_layers;

        xr_vector<xr_vector<u8>> tile_palette(tile_count);
        xr_vector<xr_vector<u8>> tile_heights(tile_count);

        xr_vector<const TileAccumulator*> tile_entries;
        tile_entries.reserve(tile_count);
        xr_vector<std::pair<s32, s32>> tile_keys;
        tile_keys.reserve(tile_count);
        for (const auto& entry : tile_accumulators)
        {
            tile_keys.emplace_back(entry.first);
            tile_entries.emplace_back(entry.second.get());
            entry.second->index = static_cast<u32>(tile_entries.size() - 1);
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

        const float density_value = (ps_r__Detail_density > 0.f) ? ps_r__Detail_density : DETAIL_SLOT_SIZE;
        const float jitter_amplitude = density_value / 1.7f;
        const u32 sample_dim = static_cast<u32>(std::ceil(DETAIL_SLOT_SIZE / density_value)) + 1u;
        const u32 samples_per_slot = sample_dim * sample_dim;

        struct TileBakeContext
        {
            ClipmapConfig config;
            s32 tile_span;
            u32 palette_layers;
            u32 layer_stride;
            u32 tile_palette_bytes;
            float density;
            float jitter_amplitude;
            u32 sample_dim;
            u32 samples_per_slot;
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
            density_value,
            jitter_amplitude,
            sample_dim,
            samples_per_slot,
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

        result.asset.sample_dim = sample_dim;
        result.asset.samples_per_slot = samples_per_slot;

        auto ProcessTileRange = [](TileBakeContext& ctx, const TaskRange<size_t>& range)
            {
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

                const u32 sample_dim = ctx.sample_dim;
                const u32 samples_per_slot = ctx.samples_per_slot;
                const float jitter_amplitude = ctx.jitter_amplitude;
                const bool bake_height_samples = (samples_per_slot > 0);

                Fvector ray_dir;
                ray_dir.set(0.f, -1.f, 0.f);

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
                    const int tile_origin_slot_x = key.first * ctx.tile_span;
                    const int tile_origin_slot_z = key.second * ctx.tile_span;
                    payload.world_origin_x = ctx.header->slot_min_x(tile_origin_slot_x);
                    payload.world_origin_z = ctx.header->slot_min_z(tile_origin_slot_z);
                    result.asset.tiles[idx] = payload;
                    result.asset.tiles[idx].palette_bytes = ctx.tile_palette_bytes;

                    const u32 offset = tile_offsets[idx];
                    const size_t count = accumulator.slots.size();

                    tile_palette[idx].assign(ctx.tile_palette_bytes, 0);
                    tile_heights[idx].assign(count * samples_per_slot * sizeof(u16), 0);

                    const size_t slot_cell_count = ctx.tile_span * ctx.tile_span;
                    xr_vector<int> slot_lookup(slot_cell_count, -1);
                    xr_vector<float> slot_sample_min(slot_cell_count, FLT_MAX);
                    xr_vector<float> slot_sample_max(slot_cell_count, -FLT_MAX);

                    float tile_min_y = FLT_MAX;
                    float tile_max_y = -FLT_MAX;
                    u32 tile_valid_count = 0;
                    u32 tile_debug_logged_hits = 0;

                    u8* palette_base = tile_palette[idx].data();
                    u16* slot_height_buffer = (!tile_heights[idx].empty() && bake_height_samples)
                        ? reinterpret_cast<u16*>(tile_heights[idx].data())
                        : nullptr;

                    for (size_t local = 0; local < count; ++local)
                    {
                        const SlotReference& slot_ref = accumulator.slots[local];
                        const u32 slot_table_index = offset + static_cast<u32>(local);
                        result.asset.slot_table[slot_table_index] = slot_ref;
                        slot_tile_indices[slot_ref.slot_index] = static_cast<u32>(idx);

                        int sx, sz;
                        ctx.header->slot_x_z(slot_ref.slot_index, sx, sz);

                        const DetailSlot& slot = ctx.slots[slot_ref.slot_index];
                        const s32 world_sx = static_cast<s32>(sx) - static_cast<s32>(ctx.header->x_offs());
                        const s32 world_sz = static_cast<s32>(sz) - static_cast<s32>(ctx.header->z_offs());

                        PlacementSeed seed = {};
                        seed.slot_x = sx;
                        seed.slot_z = sz;
                        seed.base_seed = MixSeed(static_cast<u32>(seed.slot_x), static_cast<u32>(seed.slot_z));
                        const u64 mask = BuildObjectMask(slot);
                        seed.object_mask_low = static_cast<u32>(mask & 0xFFFFFFFFu);
                        seed.object_mask_high = static_cast<u32>((mask >> 32) & 0xFFFFFFFFu);
                        seed.density_scale = ComputeDensityScale(slot);
                        seed.c_hemi = slot.r_qclr(slot.c_hemi, 15);
                        seed.c_sun = slot.r_qclr(slot.c_dir, 15);
                        const float slot_world_min_x = ctx.header->slot_min_x(sx);
                        const float slot_world_min_z = ctx.header->slot_min_z(sz);
                        seed.world_base_x = slot_world_min_x;
                        seed.world_base_z = slot_world_min_z;
                        seed.pad0 = seed.pad1 = 0.f;

                        result.asset.placement_seeds[slot_table_index] = seed;

                        const float slot_base = slot.r_ybase();
                        const float slot_top = slot_base + slot.r_yheight();

                        const u32 object_base = slot_table_index * ctx.palette_layers;
                        for (u32 objIdx = 0; objIdx < ctx.palette_layers; ++objIdx)
                            result.asset.slot_object_ids[object_base + objIdx] = slot.r_id(objIdx);

                        const u32 lut_index = slot_ref.slot_local_z * ctx.tile_span + slot_ref.slot_local_x;
                        slot_lookup[lut_index] = static_cast<int>(slot_table_index);

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

                        u32 slot_valid_samples = 0;
                        float slot_min_value = FLT_MAX;
                        float slot_max_value = -FLT_MAX;

                        u16* sample_write = slot_height_buffer ? slot_height_buffer + local * samples_per_slot : nullptr;

                        const bool has_level = (g_pGameLevel != nullptr);
                        const CDB::RESULT* slot_results = nullptr;
                        CDB::TRI* tris = nullptr;
                        Fvector* verts = nullptr;
                        size_t tri_count = 0;

                        if (has_level)
                        {
                            Fvector bC, bD;
                            bC.set(
                                slot_world_min_x + DETAIL_SLOT_SIZE * 0.5f,
                                slot_base + (slot_top - slot_base) * 0.5f,
                                slot_world_min_z + DETAIL_SLOT_SIZE * 0.5f);
                            bD.set(DETAIL_SLOT_SIZE * 0.5f + EPS_L, (slot_top - slot_base) * 0.5f + EPS_L, DETAIL_SLOT_SIZE * 0.5f + EPS_L);

                            static thread_local CDB::COLLIDER xrc_thread;
                            xrc_thread.box_query(CDB::OPT_FULL_TEST, g_pGameLevel->ObjectSpace.GetStaticModel(), bC, bD);
                            tri_count = xrc_thread.r_count();
                            if (tri_count > 0)
                            {
                                slot_results = xrc_thread.r_begin();
                                tris = g_pGameLevel->ObjectSpace.GetStaticTris();
                                verts = g_pGameLevel->ObjectSpace.GetStaticVerts();
                            }
                        }

                        if (sample_write && tri_count > 0 && tris && verts)
                        {
                            for (u32 sample_z = 0; sample_z < sample_dim; ++sample_z)
                            {
                                for (u32 sample_x = 0; sample_x < sample_dim; ++sample_x)
                                {
                                    const u32 sample_index = sample_z * sample_dim + sample_x;
                                    const float fx = (sample_dim > 1u) ? float(sample_x) / float(sample_dim - 1u) : 0.0f;
                                    const float fz = (sample_dim > 1u) ? float(sample_z) / float(sample_dim - 1u) : 0.0f;

                                    const u32 base_hash = PlacementHash(static_cast<u32>(seed.slot_x), static_cast<u32>(seed.slot_z), sample_index, seed.base_seed);
                                    const u32 jitter_hash = PlacementHash(base_hash, 2u, 0u, seed.base_seed ^ 0x68E31DA4u);
                                    const float jitter_x = RandomSigned(jitter_hash, jitter_amplitude);
                                    const float jitter_z = RandomSigned(PlacementHash(jitter_hash, 3u, 0u, seed.base_seed ^ 0x1B56C4E9u), jitter_amplitude);

                                    const float world_x = slot_world_min_x + fx * DETAIL_SLOT_SIZE + jitter_x;
                                    const float world_z = slot_world_min_z + fz * DETAIL_SLOT_SIZE + jitter_z;

                                    Fvector origin;
                                    origin.set(world_x, slot_top, world_z);

                                    float best_height = slot_base - 5.f;
                                    bool hit_found = false;

                                    float r_u, r_v, r_range;
                                    for (size_t tid = 0; tid < tri_count; ++tid)
                                    {
                                        const CDB::TRI& tri = tris[slot_results[tid].id];
                                        SGameMtl* material = GMLib.GetMaterialByIdx(tri.material);
                                        if (material->Flags.test(SGameMtl::flPassable))
                                            continue;

                                        Fvector Tv[3] = {verts[tri.verts[0]], verts[tri.verts[1]], verts[tri.verts[2]]};
                                        if (CDB::TestRayTri(origin, ray_dir, Tv, r_u, r_v, r_range, TRUE))
                                        {
                                            if (r_range >= 0.f)
                                            {
                                                const float y_test = origin.y - r_range;
                                                if (y_test > best_height)
                                                {
                                                    best_height = y_test;
                                                    hit_found = true;
                                                }
                                            }
                                        }
                                    }

                                    if (!hit_found || best_height < slot_base)
                                    {
                                        sample_write[sample_index] = kInvalidHeightSample;
                                        continue;
                                    }

                                    sample_write[sample_index] = QuantizeHeight(best_height);
                                    slot_min_value = std::min(slot_min_value, best_height);
                                    slot_max_value = std::max(slot_max_value, best_height);
                                    ++slot_valid_samples;
                                    ++tile_valid_count;

                                    if (tile_debug_logged_hits < 8)
                                    {
                                        Msg("~ [GPUGrass] Tile (%d,%d) slot(%d,%d) sample(%u,%u) world(%.2f, %.2f) height %.3f",
                                            key.first,
                                            key.second,
                                            world_sx,
                                            world_sz,
                                            sample_x,
                                            sample_z,
                                            world_x,
                                            world_z,
                                            best_height);
                                        ++tile_debug_logged_hits;
                                    }
                                }
                            }
                        }
                        else if (sample_write)
                        {
                            std::fill(sample_write, sample_write + samples_per_slot, kInvalidHeightSample);
                        }

                        if (slot_valid_samples > 0)
                        {
                            slot_sample_min[lut_index] = std::min(slot_sample_min[lut_index], slot_min_value);
                            slot_sample_max[lut_index] = std::max(slot_sample_max[lut_index], slot_max_value);
                            tile_min_y = std::min(tile_min_y, slot_min_value);
                            tile_max_y = std::max(tile_max_y, slot_max_value);
                        }
                        else
                        {
                            slot_sample_min[lut_index] = std::min(slot_sample_min[lut_index], slot_base);
                            slot_sample_max[lut_index] = std::max(slot_sample_max[lut_index], slot_top);
                            tile_min_y = std::min(tile_min_y, slot_base);
                            tile_max_y = std::max(tile_max_y, slot_top);
                        }
                    }

                    tile_valid_samples[idx] = tile_valid_count;
                    tile_min_height[idx] = (tile_min_y != FLT_MAX) ? tile_min_y : -10.f;
                    tile_max_height[idx] = (tile_max_y != -FLT_MAX) ? tile_max_y : 10.f;
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
                        const float min_val = slot_sample_min[lut_index];
                        const float max_val = slot_sample_max[lut_index];
                        if (slot_table_index >= 0 && min_val != FLT_MAX && max_val != -FLT_MAX)
                        {
                            height_info.min_height = min_val;
                            height_info.max_height = max_val;
                        }
                        else
                        {
                            height_info.min_height = fallback_base;
                            height_info.max_height = fallback_top;
                        }
                    }

                    tile_times[idx] = tile_timer.GetElapsed_sec() * 1000.f;
                    tile_seeds[idx] = static_cast<u32>(count);

                    if (tile_valid_count > 0)
                    {
                        const size_t total_samples = count * samples_per_slot;
                        Msg("~ [GPUGrass] Tile (%d,%d) valid samples %u / %zu",
                            key.first,
                            key.second,
                            tile_valid_count,
                            total_samples);
                    }
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
