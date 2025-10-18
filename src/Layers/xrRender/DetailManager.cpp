// DetailManager.cpp: implementation of the CDetailManager class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#include "DetailManager.h"
#include "GPUGrassPlacement.h"
#include "xrCDB/Intersect.hpp"

#ifdef _EDITOR
#include "ESceneClassList.h"
#include "Scene.h"
#include "SceneObject.h"
#include "IGame_Persistent.h"
#include "Environment.h"
#else
#include "xrEngine/IGame_Persistent.h"
#include "xrEngine/Environment.h"

#include "xrCore/Threading/TaskManager.hpp"

#if defined(XR_ARCHITECTURE_X86) || defined(XR_ARCHITECTURE_X64) || defined(XR_ARCHITECTURE_E2K) || defined(XR_ARCHITECTURE_PPC64)
#include <xmmintrin.h>
#elif defined(XR_ARCHITECTURE_ARM) || defined(XR_ARCHITECTURE_ARM64)
#include "sse2neon/sse2neon.h"
#elif defined(XR_ARCHITECTURE_RISCV)
#include "sse2rvv/sse2rvv.h"
#else
#error Add your platform here
#endif

extern ENGINE_API IGame_Level* g_pGameLevel;
#endif

namespace xray::render::RENDER_NAMESPACE
{
const float dbgOffset = 0.f;
const int dbgItems = 128;

//--------------------------------------------------- Decompression
static int magic4x4[4][4] = {{0, 14, 3, 13}, {11, 5, 8, 6}, {12, 2, 15, 1}, {7, 9, 4, 10}};

extern ECORE_API float r_ssaDISCARD;
extern int ps_r__gpu_culling; // GPU-driven frustum culling toggle
extern int ps_r__detail_radius;

void bwdithermap(int levels, int magic[16][16])
{
    /* Get size of each step */
    float N = 255.0f / (levels - 1);

    /*
     * Expand 4x4 dither pattern to 16x16.  4x4 leaves obvious patterning,
     * and doesn't give us full intensity range (only 17 sublevels).
     *
     * magicfact is (N - 1)/16 so that we get numbers in the matrix from 0 to
     * N - 1: mod N gives numbers in 0 to N - 1, don't ever want all
     * pixels incremented to the next level (this is reserved for the
     * pixel value with mod N == 0 at the next level).
     */

    float magicfact = (N - 1) / 16;
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                for (int l = 0; l < 4; l++)
                {
                    magic[4 * k + i][4 * l + j] =
                        (int)(0.5 + magic4x4[i][j] * magicfact + (magic4x4[k][l] / 16.) * magicfact);
                }
            }
        }
    }
}
//--------------------------------------------------- Decompression

void CDetailManager::SSwingValue::lerp(const SSwingValue& A, const SSwingValue& B, float f)
{
    float fi = 1.f - f;
    amp1 = fi * A.amp1 + f * B.amp1;
    amp2 = fi * A.amp2 + f * B.amp2;
    rot1 = fi * A.rot1 + f * B.rot1;
    rot2 = fi * A.rot2 + f * B.rot2;
    speed = fi * A.speed + f * B.speed;
}
//---------------------------------------------------

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
// XXX stats: add to statistics
CDetailManager::CDetailManager() : xrc("detail manager")
{
    ZoneScoped;

    dtFS = nullptr;
    dtSlots = nullptr;
    soft_Geom = nullptr;
    hw_Geom = nullptr;
    hw_BatchSize = 0;
    b_detail_gpu = nullptr;
    m_time_rot_1 = 0;
    m_time_rot_2 = 0;
    m_time_pos = 0;
    m_global_time_old = 0;

    // KD: variable detail radius
    dm_size = dm_current_size;
    dm_cache_line = dm_current_cache_line;
    dm_cache1_line = dm_current_cache1_line;
    dm_cache_size = dm_current_cache_size;
    dm_fade = dm_current_fade;
    ps_r__Detail_density = ps_current_detail_density;
    ps_current_detail_height = ps_r__Detail_height;
    cache_level1 = (CacheSlot1**)xr_malloc(dm_cache1_line * sizeof(CacheSlot1*));
    for (u32 i = 0; i < dm_cache1_line; ++i)
    {
        cache_level1[i] = (CacheSlot1*)xr_malloc(dm_cache1_line * sizeof(CacheSlot1));
        for (u32 j = 0; j < dm_cache1_line; ++j)
            new(&cache_level1[i][j]) CacheSlot1();
    }
    cache = (Slot***)xr_malloc(dm_cache_line * sizeof(Slot**));
    for (u32 i = 0; i < dm_cache_line; ++i)
        cache[i] = (Slot**)xr_malloc(dm_cache_line * sizeof(Slot*));

    cache_pool = (Slot *)xr_malloc(dm_cache_size * sizeof(Slot));

    for (u32 i = 0; i < dm_cache_size; ++i)
        new(&cache_pool[i]) Slot();
    /*
    CacheSlot1 cache_level1[dm_cache1_line][dm_cache1_line];
    Slot* cache [dm_cache_line][dm_cache_line]; // grid-cache itself
    Slot cache_pool [dm_cache_size]; // just memory for slots
    */
}

CDetailManager::~CDetailManager()
{
    ZoneScoped;

    for (u32 i = 0; i < dm_cache_size; ++i)
        cache_pool[i].~Slot();
    xr_free(cache_pool);

    for (u32 i = 0; i < dm_cache_line; ++i)
        xr_free(cache[i]);
    xr_free(cache);

    for (u32 i = 0; i < dm_cache1_line; ++i)
    {
        for (u32 j = 0; j < dm_cache1_line; ++j)
            cache_level1[i][j].~CacheSlot1();
        xr_free(cache_level1[i]);
    }
    xr_free(cache_level1);
}

#ifndef _EDITOR

/*
void dump(CDetailManager::vis_list& lst)
{
    for (int i = 0; i<lst.size(); i++)
    {
        Msg("%8x / %8x / %8x",  lst[i]._M_start, lst[i]._M_finish, lst[i]._M_end_of_storage._M_data);
    }
}
*/
void CDetailManager::Load()
{
    ZoneScoped;

    // Open file stream
    if (!FS.exist("$level$", "level.details"))
    {
        dtFS = nullptr;
        return;
    }

    string_path fn;
    FS.update_path(fn, "$level$", "level.details");
    dtFS = FS.r_open(fn);

    // Header
    dtFS->r_chunk_safe(0, &dtH, sizeof(dtH));
    R_ASSERT(dtH.version() == DETAIL_VERSION);
    u32 m_count = dtH.object_count();
    objects.reserve(m_count);

    // Models
    IReader* m_fs = dtFS->open_chunk(1);
    for (u32 m_id = 0; m_id < m_count; m_id++)
    {
        CDetail* dt = xr_new<CDetail>();
        IReader* S = m_fs->open_chunk(m_id);
        dt->Load(S);
        objects.push_back(dt);
        S->close();
    }
    m_fs->close();

    // Get pointer to database (slots)
    IReader* m_slots = dtFS->open_chunk(2);
    dtSlots = (DetailSlot*)m_slots->pointer();
    m_slots->close();

    // Initialize 'vis' and 'cache'
    for (u32 i = 0; i < 3; ++i)
        m_visibles[i].resize(objects.size());
    cache_Initialize();

    // Make dither matrix
    bwdithermap(2, dither);

    // Hardware specific optimizations
    if (UseVS())
        hw_Load();
    else
        soft_Load();

    // Initialize GPU compute manager (always - can be toggled at runtime with r__gpu_culling)
    m_compute_manager = xr_new<DetailComputeManager>();
    m_compute_manager->Initialize(10000000); // Max 10M instances for maximum grass density

    // Set geometry info for GPU rendering (index count from first object)
    if (!objects.empty())
    {
        m_compute_manager->SetGeometryInfo(objects[0]->number_indices);
        Msg("* [DetailManager] GPU geometry: %u indices", objects[0]->number_indices);

        xr_vector<DetailObjectGPU> detail_objects;
        detail_objects.reserve(objects.size());
        for (const CDetail* detail : objects)
        {
            DetailObjectGPU info = {};
            info.bbox_min = detail->bv_bb.vMin;
            info.min_scale = detail->m_fMinScale;
            info.bbox_max = detail->bv_bb.vMax;
            info.max_scale = detail->m_fMaxScale;
            info.radius = detail->bv_sphere.R;
            info.flags = detail->m_Flags.get();
            info.base_vis_id = detail->m_Flags.is(DO_NO_WAVING) ? 0u : 1u;
            detail_objects.emplace_back(info);
        }
        m_compute_manager->UploadDetailObjects(detail_objects);
    }

    Msg("* [DetailManager] GPU compute manager initialized (toggle with r__gpu_culling)");

    BuildGPUGrassOfflineData();

    // swing desc
    // normal
    swing_desc[0].amp1 = pSettings->r_float("details", "swing_normal_amp1");
    swing_desc[0].amp2 = pSettings->r_float("details", "swing_normal_amp2");
    swing_desc[0].rot1 = pSettings->r_float("details", "swing_normal_rot1");
    swing_desc[0].rot2 = pSettings->r_float("details", "swing_normal_rot2");
    swing_desc[0].speed = pSettings->r_float("details", "swing_normal_speed");
    // fast
    swing_desc[1].amp1 = pSettings->r_float("details", "swing_fast_amp1");
    swing_desc[1].amp2 = pSettings->r_float("details", "swing_fast_amp2");
    swing_desc[1].rot1 = pSettings->r_float("details", "swing_fast_rot1");
    swing_desc[1].rot2 = pSettings->r_float("details", "swing_fast_rot2");
    swing_desc[1].speed = pSettings->r_float("details", "swing_fast_speed");
}
#endif

void CDetailManager::Unload()
{
    ZoneScoped;

    // Shutdown GPU compute manager
    if (m_compute_manager)
    {
        m_compute_manager->Shutdown();
        xr_delete(m_compute_manager);
    }

    if (UseVS())
        hw_Unload();
    else
        soft_Unload();

    for (CDetail* detailObject : objects)
    {
        detailObject->Unload();
        xr_delete(detailObject);
    }

    objects.clear();
    m_visibles[0].clear();
    m_visibles[1].clear();
    m_visibles[2].clear();
    FS.r_close(dtFS);
}

void CDetailManager::BuildGPUGrassOfflineData()
{
    if (!g_pGameLevel)
    {
        Msg("! [DetailManager] GPU grass offline bake skipped (no active game level)");
        m_gpu_grass_asset = {};
        m_gpu_slot_tile_map.clear();
        return;
    }

    gpu_grass::OfflineBakeInput input = {};
    input.header = &dtH;
    input.slots = dtSlots;
    input.config.tile_world_size = 32.f;
    input.config.tile_resolution = 24;
    input.config.ring_count = 3;
    input.config.max_tiles_per_ring = 24;

    xr_vector<gpu_grass::OfflineAsset::DetailObjectRecord> object_records;
    object_records.reserve(objects.size());
    for (const CDetail* detail : objects)
    {
        gpu_grass::OfflineAsset::DetailObjectRecord record = {};
        record.bbox_min = detail->bv_bb.vMin;
        record.min_scale = detail->m_fMinScale;
        record.bbox_max = detail->bv_bb.vMax;
        record.max_scale = detail->m_fMaxScale;
        record.radius = detail->bv_sphere.R;
        record.flags = detail->m_Flags.get();
        record.base_vis_id = detail->m_Flags.is(DO_NO_WAVING) ? 0u : 1u;
        object_records.emplace_back(record);
    }
    input.detail_objects = &object_records;

    gpu_grass::OfflineBakeResult result;
    gpu_grass::OfflineBaker baker;
    if (baker.Build(input, result))
    {
        m_gpu_grass_asset = std::move(result.asset);
        m_gpu_slot_tile_map = std::move(result.slot_to_tile);
        m_gpu_residency.Initialize(&m_gpu_grass_asset);
        m_gpu_placement.Initialize(&m_gpu_grass_asset);
        if (m_compute_manager)
        {
            m_compute_manager->EnsureTileStateCapacity(m_gpu_grass_asset.header.tile_count);

            const auto& cfg = m_gpu_grass_asset.header.config;
            const float clip_radius = cfg.tile_world_size * (cfg.ring_count + 1.f);
            const float desired_radius = std::max(float(ps_r__detail_radius), clip_radius);
            m_compute_manager->SetFadeRadius(desired_radius);
            Msg("* [DetailManager] GPU grass fade radius set to %.2f m (clip %.2f m, detail %.2f m)",
                desired_radius,
                clip_radius,
                float(ps_r__detail_radius));
        }
        m_gpu_instance_list_dirty = true;

        float min_height = FLT_MAX;
        float max_height = -FLT_MAX;
        for (const auto& info : m_gpu_grass_asset.slot_heights)
        {
            min_height = std::min(min_height, info.min_height);
            max_height = std::max(max_height, info.max_height);
        }
        if (m_gpu_grass_asset.slot_heights.empty())
            min_height = max_height = 0.f;

        const size_t palette_bytes = m_gpu_grass_asset.palette_bytes.size();
        const size_t height_bytes = m_gpu_grass_asset.height_bytes.size();
        Msg("* [DetailManager] GPU grass offline bake generated %u tiles (%zu slots)",
            m_gpu_grass_asset.header.tile_count,
            size_t(m_gpu_grass_asset.slot_table.size()));
        Msg("* [DetailManager] GPU grass memory: palette %.2f MB, height %.2f MB",
            palette_bytes / (1024.f * 1024.f),
            height_bytes / (1024.f * 1024.f));
        Msg("* [DetailManager] GPU grass slot height range: [%.2f .. %.2f]", min_height, max_height);

        const u32 samples_per_slot = m_gpu_grass_asset.samples_per_slot;
        bool mismatched_tile = false;
        if (samples_per_slot > 0)
        {
            for (const auto& tile : m_gpu_grass_asset.tiles)
            {
                const size_t expected_tile_bytes = size_t(tile.slot_count) * size_t(samples_per_slot) * sizeof(u16);
                if (tile.height_bytes != expected_tile_bytes)
                {
                    Msg("! [DetailManager] Tile (%d,%d) has unexpected height byte size: %u (expected %zu)",
                        tile.coord.region_x,
                        tile.coord.region_z,
                        tile.height_bytes,
                        expected_tile_bytes);
                    mismatched_tile = true;
                    break;
                }
            }

            const size_t expected_total = size_t(samples_per_slot) * m_gpu_grass_asset.slot_table.size() * sizeof(u16);
            if (!mismatched_tile && expected_total != height_bytes)
            {
                Msg("! [DetailManager] Height byte total mismatch: %zu (expected %zu)",
                    height_bytes,
                    expected_total);
            }
        }
    }
    else
    {
        m_gpu_grass_asset = {};
        m_gpu_slot_tile_map.clear();
        m_gpu_residency.Reset();
        m_gpu_placement.Reset();
        Msg("! [DetailManager] GPU grass offline bake failed");
    }
}

void CDetailManager::UpdateGPUGrassResidency(const Fvector& camera_position)
{
    m_gpu_residency.Update(camera_position);
    const auto& events = m_gpu_residency.PendingEvents();
    if (!events.empty())
    {
        size_t loads = 0;
        size_t unloads = 0;
        for (const auto& evt : events)
        {
            if (evt.type == gpu_grass::TileEvent::Type::Load)
                ++loads;
            else if (evt.type == gpu_grass::TileEvent::Type::Unload)
                ++unloads;
        }
        Msg("* [DetailManager] GPU grass residency update: +%zu / -%zu tiles", loads, unloads);
        m_gpu_instance_list_dirty = true;
    }
    m_gpu_placement.EnqueueEvents(events);
    const auto& pendingLoads = m_gpu_placement.PendingLoads();
    const auto& pendingUnloads = m_gpu_placement.PendingUnloads();
    if (!pendingLoads.empty() || !pendingUnloads.empty())
    {
        Msg("* [DetailManager] GPU grass placement queue: loads=%zu unloads=%zu",
            pendingLoads.size(), pendingUnloads.size());
    }
    m_gpu_residency.ClearPendingEvents();
}

void CDetailManager::BuildGPUInstanceList(CBackend& cmd_list)
{
    ZoneScoped;

    static bool s_last_cpu = false;

    auto fallback_to_cpu = [&](const char* reason)
    {
        if (!s_last_cpu)
            Msg("! [DetailManager] GPU grass falling back to CPU instance build (%s)", reason);
        s_last_cpu = true;
        BuildGPUInstanceListCPU();
    };

    if (!ps_r__gpu_culling)
    {
        fallback_to_cpu("ps_r__gpu_culling disabled");
        return;
    }

    if (!m_compute_manager)
    {
        fallback_to_cpu("compute manager unavailable");
        return;
    }

    if (m_gpu_grass_asset.tiles.empty())
    {
        fallback_to_cpu("no baked tiles loaded");
        return;
    }

    s_last_cpu = false;

    xr_vector<u32> resident_indices = m_gpu_residency.GetResidentTileIndices();
    if (resident_indices.empty())
    {
        static u32 s_last_empty_frame = u32(-1);
        if (Device.dwFrame != s_last_empty_frame)
        {
            Msg("~ [DetailManager] GPU grass residency empty - skipping placement this frame");
            s_last_empty_frame = Device.dwFrame;
        }
        m_compute_manager->ResetInstanceAllocator(cmd_list);
        m_compute_manager->FinalizePlacement(cmd_list);
        return;
    }

    const auto& pendingLoads = m_gpu_placement.PendingLoads();
    const auto& pendingUnloads = m_gpu_placement.PendingUnloads();

    if (!pendingLoads.empty() || !pendingUnloads.empty())
    {
        m_compute_manager->ProcessPlacementTiles(cmd_list, pendingLoads, pendingUnloads);
        m_compute_manager->FinalizePlacement(cmd_list);
        m_gpu_placement.Clear();
    }
}

void CDetailManager::BuildGPUInstanceListCPU()
{
    if (!m_compute_manager)
        return;

    m_compute_manager->BeginInstanceUpdate();

    for (u32 _mz = 0; _mz < dm_cache1_line; _mz++)
    {
        for (u32 _mx = 0; _mx < dm_cache1_line; _mx++)
        {
            CacheSlot1& MS = cache_level1[_mz][_mx];
            if (MS.empty)
                continue;

            u32 dwCC = dm_cache1_count * dm_cache1_count;
            for (u32 _i = 0; _i < dwCC; _i++)
            {
                Slot* PS = *MS.slots[_i];
                Slot& S = *PS;

                if (S.empty)
                    continue;

                for (int sp_id = 0; sp_id < dm_obj_in_slot; sp_id++)
                {
                    SlotPart& sp = S.G[sp_id];
                    if (sp.id == DetailSlot::ID_Empty)
                        continue;

                    CDetail& detail = *objects[sp.id];
                    for (SlotItem* item : sp.items)
                    {
                        DetailInstanceGPU gpu_inst = ConvertToGPUInstance(
                            item,
                            sp.id,
                            &detail,
                            S.sx,
                            S.sz);

                        m_compute_manager->AddInstance(gpu_inst);
                    }
                }
            }
        }
    }

    m_compute_manager->EndInstanceUpdate();
}

void CDetailManager::UpdateVisibleM()
{
    ZoneScoped;

    UpdateGPUGrassResidency(EYE);

    for (int i = 0; i != 3; ++i)
        for (auto& vis : m_visibles[i])
            vis.clear();

    CFrustum View;
    View.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);

    float fade_limit = dm_fade;
    fade_limit = fade_limit * fade_limit;
    float fade_start = 1.f;
    fade_start = fade_start * fade_start;
    float fade_range = fade_limit - fade_start;
    float r_ssaCHEAP = 16 * r_ssaDISCARD;

    // Initialize 'vis' and 'cache'
    // Collect objects for rendering
    RImplementation.BasicStats.DetailVisibility.Begin();
    for (u32 _mz = 0; _mz < dm_cache1_line; _mz++)
    {
        for (u32 _mx = 0; _mx < dm_cache1_line; _mx++)
        {
            CacheSlot1& MS = cache_level1[_mz][_mx];
            if (MS.empty)
            {
                continue;
            }
            u32 mask = 0xff;

            u32 res = View.testSphere(MS.vis.sphere.P, MS.vis.sphere.R, mask);

            if (fcvNone == res)
            {
                continue; // invisible-view frustum
            }
            // test slots

            u32 dwCC = dm_cache1_count * dm_cache1_count;

            for (u32 _i = 0; _i < dwCC; _i++)
            {
                Slot* PS = *MS.slots[_i];
                Slot& S = *PS;

                //if (_i+1<dwCC);
                //    _mm_prefetch((char*)*MS.slots[_i+1], _MM_HINT_T1);

                // if slot empty - continue
                if (S.empty)
                {
                    continue;
                }

                // if upper test = fcvPartial - test inner slots
                if (fcvPartial == res)
                {
                    u32 _mask = mask;
                    u32 _res = View.testSphere(S.vis.sphere.P, S.vis.sphere.R, _mask);
                    if (fcvNone == _res)
                    {
                        continue; // invisible-view frustum
                    }
                }
#ifndef _EDITOR
                if (!RImplementation.HOM.visible(S.vis))
                {
                    continue; // invisible-occlusion
                }
#endif
                // Add to visibility structures
                if (Device.dwFrame > S.frame)
                {
                    // Calc fade factor (per slot)
                    float dist_sq = EYE.distance_to_sqr(S.vis.sphere.P);
                    if (dist_sq > fade_limit)
                        continue;
                    float alpha = (dist_sq < fade_start) ? 0.f : (dist_sq - fade_start) / fade_range;
                    float alpha_i = 1.f - alpha;
                    float dist_sq_rcp = 1.f / dist_sq;

                    S.frame = Device.dwFrame + Random.randI(15, 30);
                    for (int sp_id = 0; sp_id < dm_obj_in_slot; sp_id++)
                    {
                        SlotPart& sp = S.G[sp_id];
                        if (sp.id == DetailSlot::ID_Empty)
                            continue;

                        sp.r_items[0].clear();
                        sp.r_items[1].clear();
                        sp.r_items[2].clear();

                        float R = objects[sp.id]->bv_sphere.R;
                        float Rq_drcp = R * R * dist_sq_rcp; // reordered expression for 'ssa' calc

                        for(auto& siIT : sp.items)
                        {
                            SlotItem& Item = *siIT;
                            float scale = Item.scale_calculated = Item.scale * alpha_i;
                            float ssa = scale * scale * Rq_drcp;
                            if (ssa < r_ssaDISCARD)
                            {
                                continue;
                            }
                            u32 vis_id = 0;
                            if (ssa > r_ssaCHEAP)
                                vis_id = Item.vis_ID;

                            sp.r_items[vis_id].push_back(siIT);

                            Item.distance = dist_sq;
                            Item.position = S.vis.sphere.P;
                            // 2 visible[vis_id][sp.id].push_back(&Item);
                        }
                    }
                }
                for (int sp_id = 0; sp_id < dm_obj_in_slot; sp_id++)
                {
                    SlotPart& sp = S.G[sp_id];
                    if (sp.id == DetailSlot::ID_Empty)
                        continue;
                    if (!sp.r_items[0].empty())
                    {
                        m_visibles[0][sp.id].push_back(&sp.r_items[0]);
                    }
                    if (!sp.r_items[1].empty())
                    {
                        m_visibles[1][sp.id].push_back(&sp.r_items[1]);
                    }
                    if (!sp.r_items[2].empty())
                    {
                        m_visibles[2][sp.id].push_back(&sp.r_items[2]);
                    }
                }
            }
        }
    }
    RImplementation.BasicStats.DetailVisibility.End();
}

bool CDetailManager::UseVS() const
{
    return HW.Caps.geometry_major >= 1 && !RImplementation.o.ffp;
}

void CDetailManager::Render(CBackend& cmd_list)
{
#ifndef _EDITOR
    if (nullptr == dtFS)
        return;
    if (!psDeviceFlags.is(rsDrawDetails))
        return;
#endif

    ZoneScoped;

    if (!ps_r__gpu_culling)
        TaskScheduler->Wait(*m_calc_task);

    RImplementation.BasicStats.DetailRender.Begin();
    g_pGamePersistent->m_pGShaderConstants->m_blender_mode.w = 1.0f; //--#SM+#-- Флаг начала рендера травы [begin of grass render]

    float factor = g_pGamePersistent->Environment().wind_strength_factor;
    swing_current.lerp(swing_desc[0], swing_desc[1], factor);

    cmd_list.set_CullMode(CULL_CCW);
    cmd_list.set_xform_world(Fidentity);

    // GPU compute culling path
    if (ps_r__gpu_culling && m_compute_manager)
    {
        EYE = Device.vCameraPosition;
        UpdateGPUGrassResidency(EYE);

        // Build instance list ONLY when cache changes (not every frame!)
        // This was the major bottleneck - rebuilding millions of instances every frame
        if (m_gpu_instance_list_dirty)
        {
            PIX_EVENT(INSTANCED_GRASS_BUILD_INSTANCES);
            BuildGPUInstanceList(cmd_list);
            m_gpu_instance_list_dirty = false;
        }

        // Dispatch GPU culling compute shader
        m_compute_manager->DispatchCulling(cmd_list, Device.mFullTransform);

        // Render using indirect draws
        // GPU path renders all objects together in 3 draws (one per vis_id)
        // Script shader details_lod_gpu.s uses "normal" function which maps to E[1]
        // (normal→E[0]/E[1], l_point→E[2], l_special→E[4])
        u32 shader_element = 1;

#ifdef USE_DX11
        auto* context = HW.get_context(cmd_list.context_id); // ugly but sets PS and VS properly without clearing constants/globals
        cmd_list.set_Geometry(gpu_Geom);

        for (u32 iPass = 0; iPass < gpu_detail_shader->E[shader_element]->passes.size(); ++iPass)
        {
            cmd_list.set_xform_view(Device.mView);
            cmd_list.set_xform_project(Device.mProject);
            cmd_list.set_Textures(gpu_detail_shader->E[shader_element]->passes[iPass]._get()->T);

            cmd_list.set_Element(gpu_detail_shader->E[shader_element], iPass); // CLEARS CONSTANT BUFFERS AND GLOBALS!? Disabled for now.

            cmd_list.SRVSManager.Apply(cmd_list.context_id);
            cmd_list.ApplyRTandZB();
            cmd_list.ApplyVertexLayout();
            cmd_list.StateManager.Apply();
            cmd_list.GetConstants().flush();
        }
        //context->PSSetShader(gpu_detail_shader->E[shader_element]->passes[0]._get()->ps._get()->sh, nullptr, 0);
        //context->VSSetShader(gpu_detail_shader->E[shader_element]->passes[0]._get()->vs._get()->sh, nullptr, 0);
#endif
        // Render each visibility list (still=0, wave1=1, wave2=2)
        for (u32 vis_id = 0; vis_id < 3; vis_id++)
        {
            m_compute_manager->RenderIndirect(cmd_list, vis_id);
        }
    }
    else // CPU rendering path
    {
        if (UseVS())
            hw_Render(cmd_list);
        else
            soft_Render();
    }

    cmd_list.set_CullMode(CULL_CCW);

    g_pGamePersistent->m_pGShaderConstants->m_blender_mode.w = 0.0f; //--#SM+#-- Флаг конца рендера травы [end of grass render]
    RImplementation.BasicStats.DetailRender.End();
}

void CDetailManager::DispatchMTCalc()
{
    if (ps_r__gpu_culling)
        return;

    m_calc_task = &TaskScheduler->AddTask([this]
    {
#ifndef _EDITOR
        if (nullptr == RImplementation.Details)
            return; // possibly deleted
        if (nullptr == dtFS)
            return;
        if (!psDeviceFlags.is(rsDrawDetails))
            return;
#endif

        ZoneScoped;

        EYE = Device.vCameraPosition;

        const int s_x = iFloor(EYE.x / dm_slot_size + .5f);
        const int s_z = iFloor(EYE.z / dm_slot_size + .5f);

        RImplementation.BasicStats.DetailCache.Begin();
        cache_Update(s_x, s_z, EYE);
        RImplementation.BasicStats.DetailCache.End();

        // CPU culling path (skip if GPU culling enabled)
        UpdateVisibleM();
    });
}

void CDetailManager::details_clear()
{
    // Disable fade, next render will be scene
    fade_distance = 99999;

    if (ps_ssfx_grass_shadows.x <= 0)
        return;

    for (u32 x = 0; x < 3; x++)
    {
        vis_list& list = m_visibles[x];
        for (u32 O = 0; O < objects.size(); O++)
        {
            CDetail & Object = *objects[O];
            xr_vector<SlotItemVec*>&vis = list[O];
            if (!vis.empty())
            {
                vis.erase(vis.begin(), vis.end());
            }
        }
    }
}
} // namespace xray::render::RENDER_NAMESPACE
