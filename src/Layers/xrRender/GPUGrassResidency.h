#pragma once

#include "DetailFormat.h"
#include "GPUGrassData.h"
#include "xrCommon/xr_map.h"
#include "xrCommon/xr_vector.h"

namespace xray::render::RENDER_NAMESPACE::gpu_grass
{
struct TileKey
{
    s32 x = 0;
    s32 z = 0;

    bool operator<(const TileKey& other) const
    {
        if (x == other.x)
            return z < other.z;
        return x < other.x;
    }

    bool operator==(const TileKey& other) const
    {
        return x == other.x && z == other.z;
    }
};

struct TileEvent
{
    enum class Type : u32
    {
        Load,
        Unload
    };

    Type type = Type::Load;
    u32 tile_index = 0;
    TileKey key = {};
};

class ResidencyManager
{
public:
    ResidencyManager() = default;

    void Initialize(const OfflineAsset* asset);
    void Reset();

    void Update(const Fvector& camera_position);

    const xr_vector<TileEvent>& PendingEvents() const { return m_pending_events; }
    void ClearPendingEvents() { m_pending_events.clear(); }

    xr_vector<u32> GetResidentTileIndices() const;
    bool IsTileResident(u32 tile_index) const;

    ClipmapConfig GetConfig() const { return m_config; }

private:
    static s32 FloorDiv(s32 value, s32 divisor);

    const OfflineAsset* m_asset = nullptr;
    ClipmapConfig m_config = {};
    u32 m_tile_span = 1;

    xr_map<TileKey, u32> m_tile_lookup;
    xr_map<TileKey, u32> m_active_tiles;
    xr_vector<TileEvent> m_pending_events;
};

} // namespace xray::render::RENDER_NAMESPACE::gpu_grass
