#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>

#include "xrCommon/xr_string.h"
#include "xrCommon/xr_vector.h"
#include "xrCommon/xr_unordered_map.h"

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
    xr_vector<xr_string> motion_roots;
};

LegacyAssetInventory BuildLegacyAssetInventory(const InventoryScanConfig& config);
LegacyAssetInventory BuildDefaultLegacyAssetInventory();

enum class ConversionStatus : std::uint8_t
{
    Unknown = 0,
    Success,
    Failed,
    Skipped
};

struct CacheManifest
{
    struct MotionEntry
    {
        xr_string canonical_motion;
        xr_string source_root;
        xr_string source_path;
        std::int64_t source_timestamp_seconds = 0;
        std::int64_t source_size = 0;
        xr_string output_root;
        xr_string output_path;
        std::int64_t output_timestamp_seconds = 0;
    };

    struct VisualEntry
    {
        xr_string normalized_identifier;
        xr_string source_root;
        xr_string source_path;
        std::int64_t source_timestamp_seconds = 0;
        std::int64_t source_size = 0;
        xr_string skeleton_output_root;
        xr_string skeleton_output_path;
        std::int64_t skeleton_output_timestamp_seconds = 0;
        xr_string bundle_output_root;
        xr_string bundle_output_path;
        std::int64_t bundle_output_timestamp_seconds = 0;
        ConversionStatus last_status = ConversionStatus::Unknown;
        xr_string last_error;
        std::int64_t last_duration_milliseconds = 0;
        xr_string converter_version;
        xr_string converter_build_id;
        xr_vector<MotionEntry> motions;

        [[nodiscard]] const MotionEntry* FindMotion(const xr_string& canonical_motion) const;
        [[nodiscard]] MotionEntry* FindMotion(const xr_string& canonical_motion);
    };

    xr_string schema_version = "1";
    xr_string manifest_version = "1";
    xr_string converter_version;
    xr_string converter_build_id;
    std::int64_t last_updated_seconds = 0;
    xr_unordered_map<xr_string, VisualEntry> visuals;

    [[nodiscard]] const VisualEntry* FindVisual(const xr_string& normalized_identifier) const;
    [[nodiscard]] VisualEntry* FindVisual(const xr_string& normalized_identifier);
};

struct ManifestSkipOptions
{
    bool force_full_rebuild = false;
    bool force_failed_rebuild = false;
    std::unordered_set<xr_string> forced_visuals;

    [[nodiscard]] bool IsForced(const xr_string& normalized_identifier) const;
};

struct SkipDecision
{
    bool should_skip = false;
    xr_string reason;
};

CacheManifest LoadCacheManifest(const std::filesystem::path& manifest_path);
void SaveCacheManifest(const CacheManifest& manifest, const std::filesystem::path& manifest_path);

SkipDecision EvaluateSkipDecision(const LegacyVisualAsset& asset,
    const CacheManifest& manifest,
    const ManifestSkipOptions& options,
    const LegacyAssetInventory& inventory);

} // namespace Animation
} // namespace XRay

