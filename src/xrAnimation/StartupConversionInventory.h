#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "xrCommon/xr_string.h"
#include "xrCommon/xr_unordered_map.h"
#include "xrCommon/xr_vector.h"

namespace XRay
{
namespace Animation
{
struct LegacyAssetLocation
{
    xr_string root_alias;
    xr_string relative_path;
    std::int64_t file_size = 0;
    std::int64_t modified_time_seconds = 0;
    bool stored_in_vfs = false;
};

struct LegacyVisualSource
{
    LegacyAssetLocation location;
    xr_vector<xr_string> motion_references;
};

struct LegacyVisualAsset
{
    xr_string normalized_identifier;
    xr_vector<LegacyVisualSource> sources;

    [[nodiscard]] const LegacyVisualSource* PrimarySource() const;
    [[nodiscard]] LegacyVisualSource* PrimarySource();
    [[nodiscard]] const LegacyVisualSource* FindSource(const xr_string& root_alias, const xr_string& relative_path) const;
};

struct LegacyMotionAsset
{
    xr_string canonical_name;
    xr_string relative_path;
    xr_string root_alias;
    std::int64_t file_size = 0;
    std::int64_t modified_time_seconds = 0;
    bool stored_in_vfs = false;
};

struct LegacyAssetInventory
{
    xr_vector<LegacyVisualAsset> visuals;
    xr_vector<LegacyMotionAsset> motions;
    xr_unordered_map<xr_string, std::size_t> visual_lookup;
    xr_unordered_map<xr_string, std::size_t> motion_lookup;

    [[nodiscard]] const LegacyVisualAsset* FindVisual(const xr_string& normalized_identifier) const;
    [[nodiscard]] LegacyVisualAsset* FindVisual(const xr_string& normalized_identifier);
    [[nodiscard]] const LegacyMotionAsset* FindMotion(const xr_string& canonical_name) const;
};

struct InventoryScanConfig
{
    xr_vector<xr_string> visual_roots;
};

LegacyAssetInventory BuildLegacyAssetInventory(const InventoryScanConfig& config);
LegacyAssetInventory BuildDefaultLegacyAssetInventory();

[[nodiscard]] xr_string ComputeLegacyAssetInventoryDigest(const LegacyAssetInventory& inventory);

[[nodiscard]] xr_string LoadInventoryDigestFromConfig(const std::filesystem::path& config_path);
bool StoreInventoryDigestInConfig(const std::filesystem::path& config_path, const xr_string& digest);

[[nodiscard]] xr_string LoadInventoryDigestFromUserConfig();
bool StoreInventoryDigestInUserConfig(const xr_string& digest);

inline constexpr char kInventoryDigestSection[] = "ozz_startup_conversion";
inline constexpr char kInventoryDigestKey[] = "inventory_digest";

} // namespace Animation
} // namespace XRay
