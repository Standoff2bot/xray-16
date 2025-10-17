#pragma once

#include "DetailFormat.h"
#include "xrCommon/xr_vector.h"
#include "xrCore/Containers/AssociativeVector.hpp"

namespace xray::render::RENDER_NAMESPACE::gpu_grass
{
// Clipmap configuration describing residency rings and tile resolution.
struct ClipmapConfig
{
    u32 ring_count = 3;            // Number of concentric rings around the camera
    float tile_world_size = 32.f;  // World-space length of a tile edge (meters)
    u32 tile_resolution = 256;     // Texel resolution per tile edge for masks/height
    u32 max_tiles_per_ring = 64;   // Safety cap for residency tracking
};

// Integer coordinate identifying a tile within the clipmap hierarchy.
struct TileCoordinate
{
    s32 region_x = 0;  // Coarse region coordinate (multiple tiles)
    s32 region_z = 0;
    u32 ring = 0;      // Clipmap ring index
    u32 local_x = 0;   // Tile coordinate inside the ring grid
    u32 local_z = 0;
};

// Metadata for a compressed tile payload used by GPU placement shaders.
struct TilePayload
{
    TileCoordinate coord;
    u32 slot_offset = 0;        // Offset into the flattened slot table
    u32 slot_count = 0;         // Number of slots encoded in this tile
    u32 palette_offset = 0;     // Offset in bytes into the palette atlas
    u32 palette_bytes = 0;      // Size of palette data for this tile
    u32 height_offset = 0;      // Offset in bytes into height clipmap data
    u32 height_bytes = 0;       // Size of height data for this tile
};

// Flattened slot table entry referencing the original level.details slot.
struct SlotReference
{
    u32 slot_index = 0;     // Index into DetailHeader slot array
    u16 slot_local_x = 0;   // Local slot coordinate inside tile
    u16 slot_local_z = 0;
};

// Offline asset header persisted alongside compressed tile payloads.
struct OfflineAssetHeader
{
    static constexpr u32 kMagic = 0x47525047; // 'GPGR'
    static constexpr u32 kVersion = 1;

    u32 magic = kMagic;
    u32 version = kVersion;
    ClipmapConfig config = {};
    u32 tile_count = 0;
};

// Deterministic seed data used by the GPU placement shader.
struct alignas(16) PlacementSeed
{
    u32 slot_x;
    u32 slot_z;
    u32 base_seed;         // Pre-mixed slot seed used by deterministic hashing
    u64 object_mask;       // Bitmask of detail objects enabled in this slot
    float density_scale;   // Density multiplier extracted from palette
};

// Container describing the full baked dataset ready for runtime upload.
struct OfflineAsset
{
    OfflineAssetHeader header;
    xr_vector<TilePayload> tiles;
    xr_vector<SlotReference> slot_table;
    xr_vector<PlacementSeed> placement_seeds;
    xr_vector<Fvector2> slot_heights; // x = base height, y = height delta
    xr_vector<u8> palette_bytes;
    xr_vector<u8> height_bytes;
};

// Mapping type for quick lookup of baked tile payloads by slot index.
using SlotToTileMap = AssociativeVector<u32, u32>;

struct OfflineBakeInput
{
    const DetailHeader* header = nullptr;
    const DetailSlot* slots = nullptr;
    ClipmapConfig config = {};
};

struct OfflineBakeResult
{
    OfflineAsset asset;
    SlotToTileMap slot_to_tile;
};

class OfflineBaker
{
public:
    OfflineBaker() = default;
    bool Build(const OfflineBakeInput& input, OfflineBakeResult& result);
};

} // namespace xray::render::RENDER_NAMESPACE::gpu_grass
