#include "stdafx.h"

#include "StartupConversionInventory.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "xrCore/FMesh.hpp"
#include "xrCore/FS.h"
#include "xrCore/Log.h"

#include "Layers/xrRender/ModelNaming.h"

#include "../../Externals/ozz-animation/src/animation/offline/gltf/extern/json.hpp"

using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace XRay
{
namespace Animation
{
namespace
{
struct ChunkView
{
    const std::byte* data = nullptr;
    size_t size = 0;
};

struct BinaryReader
{
    const std::byte* data = nullptr;
    size_t size = 0;
    size_t offset = 0;

    template <class T>
    T Read()
    {
        if (offset + sizeof(T) > size)
            throw std::runtime_error("unexpected end of chunk while reading data");
        T value;
        std::memcpy(&value, data + offset, sizeof(T));
        offset += sizeof(T);
        return value;
    }

    xr_string ReadStringZ()
    {
        const auto* begin = data + offset;
        const auto* end = data + size;
        const auto* cursor = begin;
        while (cursor < end && *reinterpret_cast<const char*>(cursor) != '\0')
            ++cursor;

        if (cursor == end)
            throw std::runtime_error("unterminated string in chunk");

        xr_string value(reinterpret_cast<const char*>(begin), static_cast<size_t>(cursor - begin));
        offset += static_cast<size_t>(cursor - begin) + 1;
        return value;
    }
};

xr_string TrimCopy(const xr_string& value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == xr_string::npos)
        return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

xr_string ToLowerCopy(xr_string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

xr_string CanonicalizeRelativePath(xr_string value)
{
    value = ToLowerCopy(value);
    std::replace(value.begin(), value.end(), '/', '\\');
    while (!value.empty() && (value.front() == '\\' || value.front() == '/'))
        value.erase(value.begin());
    return value;
}

xr_string CanonicalizeMotionReference(xr_string value)
{
    value = CanonicalizeRelativePath(std::move(value));
    if (value.empty())
        return value;
    if (value.size() < 4 || value.substr(value.size() - 4) != ".omf")
        value.append(".omf");
    return value;
}

xr_string CanonicalizeMotionName(const xr_string& value)
{
    xr_string canonical = CanonicalizeMotionReference(value);
    const auto dot = canonical.find_last_of('.');
    if (dot != xr_string::npos)
        canonical.erase(dot);
    return canonical;
}

std::unordered_map<u32, ChunkView> ParseChunks(const std::byte* data, size_t size)
{
    std::unordered_map<u32, ChunkView> chunks;
    size_t offset = 0;

    while (offset + sizeof(u32) * 2 <= size)
    {
        u32 id = 0;
        u32 chunk_size = 0;
        std::memcpy(&id, data + offset, sizeof(u32));
        offset += sizeof(u32);
        std::memcpy(&chunk_size, data + offset, sizeof(u32));
        offset += sizeof(u32);

        if (offset > size || chunk_size > size - offset)
            throw std::runtime_error("chunk extends past buffer bounds");

        chunks.emplace(id, ChunkView{ data + offset, chunk_size });
        offset += chunk_size;
    }

    return chunks;
}

std::vector<std::byte> ReadFileBytes(const xr_string& root_alias, const xr_string& relative_path)
{
    IReader* reader = FS.r_open(root_alias.c_str(), relative_path.c_str());
    if (!reader)
        return {};

    std::vector<std::byte> buffer(static_cast<size_t>(reader->length()));
    if (!buffer.empty())
        std::memcpy(buffer.data(), reader->pointer(), buffer.size());
    FS.r_close(reader);
    return buffer;
}

xr_vector<xr_string> SplitMotionReferenceList(const xr_string& combined)
{
    xr_vector<xr_string> result;
    size_t cursor = 0;
    while (cursor < combined.size())
    {
        const size_t separator = combined.find(',', cursor);
        const size_t length = separator == xr_string::npos ? combined.size() - cursor : separator - cursor;
        xr_string token = combined.substr(cursor, length);
        token = TrimCopy(token);
        if (!token.empty())
            result.emplace_back(std::move(token));
        if (separator == xr_string::npos)
            break;
        cursor = separator + 1;
    }
    return result;
}

xr_vector<xr_string> ExtractMotionReferences(const xr_string& root_alias, const xr_string& relative_path)
{
    xr_vector<xr_string> references;
    try
    {
        auto bytes = ReadFileBytes(root_alias, relative_path);
        if (bytes.empty())
            return references;

        const auto chunks = ParseChunks(bytes.data(), bytes.size());

        if (const auto refs2_it = chunks.find(OGF_S_MOTION_REFS2); refs2_it != chunks.end())
        {
            BinaryReader reader{ refs2_it->second.data, refs2_it->second.size };
            const u32 count = reader.Read<u32>();
            references.reserve(count);
            for (u32 index = 0; index < count; ++index)
            {
                xr_string entry = reader.ReadStringZ();
                entry = TrimCopy(entry);
                if (!entry.empty())
                    references.emplace_back(std::move(entry));
            }
        }
        else if (const auto refs_it = chunks.find(OGF_S_MOTION_REFS); refs_it != chunks.end())
        {
            BinaryReader reader{ refs_it->second.data, refs_it->second.size };
            if (reader.size > 0)
            {
                xr_string combined = reader.ReadStringZ();
                auto tokens = SplitMotionReferenceList(combined);
                references.insert(references.end(), tokens.begin(), tokens.end());
            }
        }
    }
    catch (const std::exception& ex)
    {
        Msg("! [ozz] Failed to read motion refs from %s:%s (%s)", root_alias.c_str(), relative_path.c_str(), ex.what());
        references.clear();
    }

    for (auto& ref : references)
        ref = CanonicalizeMotionReference(ref);

    return references;
}

LegacyAssetLocation MakeLocation(const xr_string& root_alias, const FS_File& file)
{
    LegacyAssetLocation location;
    location.root_alias = root_alias;
    location.relative_path = CanonicalizeRelativePath(file.name);
    location.file_size = file.size >= 0 ? static_cast<std::int64_t>(file.size) : 0;
    location.modified_time_seconds = file.time_write >= 0 ? static_cast<std::int64_t>(file.time_write) : 0;
    location.stored_in_vfs = (file.attrib & FS_File::flVFS) != 0;
    return location;
}

std::int64_t FileTimeToUnixSeconds(const fs::file_time_type& file_time)
{
    using namespace std::chrono;
    const auto system_time = time_point_cast<system_clock::duration>(file_time - fs::file_time_type::clock::now() + system_clock::now());
    return duration_cast<seconds>(system_time.time_since_epoch()).count();
}

std::optional<std::int64_t> QueryFileTimestampSeconds(const xr_string& root_alias, const xr_string& relative_path)
{
    if (relative_path.empty())
        return std::nullopt;

    string_path resolved;
    if (!FS.update_path(resolved, root_alias.c_str(), relative_path.c_str(), false))
        return std::nullopt;

    std::error_code ec;
    const auto path = fs::path(resolved);
    if (!fs::exists(path, ec) || ec)
        return std::nullopt;

    const auto time = fs::last_write_time(path, ec);
    if (ec)
        return std::nullopt;

    return FileTimeToUnixSeconds(time);
}

bool ValidateOutputFreshness(const xr_string& root_alias,
    const xr_string& relative_path,
    std::int64_t recorded_output_timestamp,
    std::int64_t source_timestamp,
    xr_string& out_reason)
{
    const auto actual_timestamp = QueryFileTimestampSeconds(root_alias, relative_path);
    if (!actual_timestamp)
    {
        out_reason = xr_string("missing output ") + relative_path;
        return false;
    }

    if (*actual_timestamp < source_timestamp)
    {
        out_reason = xr_string("output older than source: ") + relative_path;
        return false;
    }

    if (recorded_output_timestamp > 0 && *actual_timestamp < recorded_output_timestamp)
    {
        out_reason = xr_string("output timestamp regressed: ") + relative_path;
        return false;
    }

    return true;
}

ConversionStatus ParseStatus(const std::string& value)
{
    if (value == "success")
        return ConversionStatus::Success;
    if (value == "failed")
        return ConversionStatus::Failed;
    if (value == "skipped")
        return ConversionStatus::Skipped;
    return ConversionStatus::Unknown;
}

std::string StatusToString(ConversionStatus status)
{
    switch (status)
    {
    case ConversionStatus::Success: return "success";
    case ConversionStatus::Failed: return "failed";
    case ConversionStatus::Skipped: return "skipped";
    default: return "unknown";
    }
}

} // namespace

const LegacyVisualSource* LegacyVisualAsset::PrimarySource() const
{
    if (sources.empty())
        return nullptr;
    return &sources.front();
}

LegacyVisualSource* LegacyVisualAsset::PrimarySource()
{
    if (sources.empty())
        return nullptr;
    return &sources.front();
}

const LegacyVisualSource* LegacyVisualAsset::FindSource(const xr_string& root_alias, const xr_string& relative_path) const
{
    const xr_string canonical_root = ToLowerCopy(root_alias);
    const xr_string canonical_path = CanonicalizeRelativePath(relative_path);
    for (const auto& source : sources)
    {
        if (ToLowerCopy(source.location.root_alias) == canonical_root && source.location.relative_path == canonical_path)
            return &source;
    }
    return nullptr;
}

const LegacyVisualAsset* LegacyAssetInventory::FindVisual(const xr_string& normalized_identifier) const
{
    const auto it = visual_lookup.find(normalized_identifier);
    if (it == visual_lookup.end())
        return nullptr;
    return &visuals[it->second];
}

LegacyVisualAsset* LegacyAssetInventory::FindVisual(const xr_string& normalized_identifier)
{
    const auto it = visual_lookup.find(normalized_identifier);
    if (it == visual_lookup.end())
        return nullptr;
    return &visuals[it->second];
}

const LegacyMotionAsset* LegacyAssetInventory::FindMotion(const xr_string& canonical_name) const
{
    const auto it = motion_lookup.find(canonical_name);
    if (it == motion_lookup.end())
        return nullptr;
    return &motions[it->second];
}

const CacheManifest::MotionEntry* CacheManifest::VisualEntry::FindMotion(const xr_string& canonical_motion) const
{
    const xr_string canonical = CanonicalizeMotionReference(canonical_motion);
    for (const auto& motion : motions)
        if (motion.canonical_motion == canonical)
            return &motion;
    return nullptr;
}

CacheManifest::MotionEntry* CacheManifest::VisualEntry::FindMotion(const xr_string& canonical_motion)
{
    const xr_string canonical = CanonicalizeMotionReference(canonical_motion);
    for (auto& motion : motions)
        if (motion.canonical_motion == canonical)
            return &motion;
    return nullptr;
}

const CacheManifest::VisualEntry* CacheManifest::FindVisual(const xr_string& normalized_identifier) const
{
    const auto it = visuals.find(normalized_identifier);
    if (it == visuals.end())
        return nullptr;
    return &it->second;
}

CacheManifest::VisualEntry* CacheManifest::FindVisual(const xr_string& normalized_identifier)
{
    const auto it = visuals.find(normalized_identifier);
    if (it == visuals.end())
        return nullptr;
    return &it->second;
}

bool ManifestSkipOptions::IsForced(const xr_string& normalized_identifier) const
{
    if (force_full_rebuild)
        return true;
    return forced_visuals.find(normalized_identifier) != forced_visuals.end();
}

LegacyAssetInventory BuildLegacyAssetInventory(const InventoryScanConfig& config)
{
    LegacyAssetInventory inventory;

    auto ensure_visual_entry = [&](const xr_string& identifier) -> LegacyVisualAsset& {
        const auto it = inventory.visual_lookup.find(identifier);
        if (it != inventory.visual_lookup.end())
            return inventory.visuals[it->second];

        inventory.visuals.emplace_back();
        auto& asset = inventory.visuals.back();
        asset.normalized_identifier = identifier;
        inventory.visual_lookup.emplace(identifier, inventory.visuals.size() - 1);
        return asset;
    };

    for (const auto& root : config.visual_roots)
    {
        FS_FileSet files;
        FS.file_list(files, root.c_str(), FS_ListFiles, "*.ogf");

        for (const auto& file : files)
        {
            const auto location = MakeLocation(root, file);
            const xr_string normalized = xray::render::detail::NormalizeModelIdentifier(location.relative_path.c_str());
            if (normalized.empty())
                continue;

            auto& asset = ensure_visual_entry(normalized);
            LegacyVisualSource source;
            source.location = location;
            source.motion_references = ExtractMotionReferences(root, source.location.relative_path);
            asset.sources.emplace_back(std::move(source));
        }
    }

    std::unordered_set<xr_string> unique_motion_sources;

    for (const auto& root : config.motion_roots)
    {
        FS_FileSet files;
        FS.file_list(files, root.c_str(), FS_ListFiles, "*.omf");

        for (const auto& file : files)
        {
            LegacyMotionAsset motion;
            motion.root_alias = ToLowerCopy(root);
            motion.relative_path = CanonicalizeRelativePath(file.name);
            motion.canonical_name = CanonicalizeMotionName(motion.relative_path);
            motion.file_size = file.size >= 0 ? static_cast<std::int64_t>(file.size) : 0;
            motion.modified_time_seconds = file.time_write >= 0 ? static_cast<std::int64_t>(file.time_write) : 0;
            motion.stored_in_vfs = (file.attrib & FS_File::flVFS) != 0;

            xr_string unique_key = motion.root_alias;
            unique_key.append("|");
            unique_key.append(motion.relative_path);
            if (!unique_motion_sources.insert(unique_key).second)
                continue;

            if (inventory.motion_lookup.find(motion.canonical_name) != inventory.motion_lookup.end())
                continue;

            inventory.motion_lookup.emplace(motion.canonical_name, inventory.motions.size());
            inventory.motions.emplace_back(std::move(motion));
        }
    }

    return inventory;
}

LegacyAssetInventory BuildDefaultLegacyAssetInventory()
{
    InventoryScanConfig config;
    config.visual_roots = { "$level$", "$game_meshes$" };
    config.motion_roots = { "$level$", "$game_meshes$" };
    return BuildLegacyAssetInventory(config);
}

CacheManifest LoadCacheManifest(const fs::path& manifest_path)
{
    CacheManifest manifest;

    std::error_code ec;
    if (!fs::exists(manifest_path, ec) || ec)
        return manifest;

    std::ifstream stream(manifest_path);
    if (!stream)
    {
        Msg("! [ozz] Failed to open manifest %s", manifest_path.string().c_str());
        return manifest;
    }

    try
    {
        Json json;
        stream >> json;

        manifest.schema_version = xr_string(json.value("schema_version", std::string("1")).c_str());
        manifest.manifest_version = xr_string(json.value("manifest_version", std::string("1")).c_str());
        manifest.converter_version = xr_string(json.value("converter_version", std::string()).c_str());
        manifest.converter_build_id = xr_string(json.value("converter_build_id", std::string()).c_str());
        manifest.last_updated_seconds = json.value("last_updated_seconds", 0ll);

        if (auto visuals = json.find("visuals"); visuals != json.end() && visuals->is_array())
        {
            for (const auto& visual_json : *visuals)
            {
                CacheManifest::VisualEntry entry;
                entry.normalized_identifier = xr_string(visual_json.value("normalized_identifier", std::string()));
                if (entry.normalized_identifier.empty())
                    continue;

                entry.source_root = xr_string(visual_json.value("source_root", std::string()));
                entry.source_path = CanonicalizeRelativePath(xr_string(visual_json.value("source_path", std::string())));
                entry.source_timestamp_seconds = visual_json.value("source_timestamp_seconds", 0ll);
                entry.source_size = visual_json.value("source_size", 0ll);
                entry.skeleton_output_root = xr_string(visual_json.value("skeleton_output_root", std::string()));
                entry.skeleton_output_path = CanonicalizeRelativePath(xr_string(visual_json.value("skeleton_output_path", std::string())));
                entry.skeleton_output_timestamp_seconds = visual_json.value("skeleton_output_timestamp_seconds", 0ll);
                entry.bundle_output_root = xr_string(visual_json.value("bundle_output_root", std::string()));
                entry.bundle_output_path = CanonicalizeRelativePath(xr_string(visual_json.value("bundle_output_path", std::string())));
                entry.bundle_output_timestamp_seconds = visual_json.value("bundle_output_timestamp_seconds", 0ll);
                entry.last_error = xr_string(visual_json.value("last_error", std::string()));
                entry.last_duration_milliseconds = visual_json.value("last_duration_milliseconds", 0ll);
                entry.converter_version = xr_string(visual_json.value("converter_version", std::string()));
                entry.converter_build_id = xr_string(visual_json.value("converter_build_id", std::string()));
                entry.last_status = ParseStatus(visual_json.value("last_status", std::string()));

                if (auto motions = visual_json.find("motions"); motions != visual_json.end() && motions->is_array())
                {
                    for (const auto& motion_json : *motions)
                    {
                        CacheManifest::MotionEntry motion;
                        motion.canonical_motion = CanonicalizeMotionReference(xr_string(motion_json.value("canonical_motion", std::string())));
                        motion.source_root = xr_string(motion_json.value("source_root", std::string()));
                        motion.source_path = CanonicalizeRelativePath(xr_string(motion_json.value("source_path", std::string())));
                        motion.source_timestamp_seconds = motion_json.value("source_timestamp_seconds", 0ll);
                        motion.source_size = motion_json.value("source_size", 0ll);
                        motion.output_root = xr_string(motion_json.value("output_root", std::string()));
                        motion.output_path = CanonicalizeRelativePath(xr_string(motion_json.value("output_path", std::string())));
                        motion.output_timestamp_seconds = motion_json.value("output_timestamp_seconds", 0ll);
                        entry.motions.emplace_back(std::move(motion));
                    }
                }

                manifest.visuals.emplace(entry.normalized_identifier, std::move(entry));
            }
        }
    }
    catch (const std::exception& ex)
    {
        Msg("! [ozz] Failed to parse manifest %s (%s)", manifest_path.string().c_str(), ex.what());
    }

    return manifest;
}

void SaveCacheManifest(const CacheManifest& manifest, const fs::path& manifest_path)
{
    Json json;
    json["schema_version"] = manifest.schema_version.c_str();
    json["manifest_version"] = manifest.manifest_version.c_str();
    json["converter_version"] = manifest.converter_version.c_str();
    json["converter_build_id"] = manifest.converter_build_id.c_str();
    json["last_updated_seconds"] = manifest.last_updated_seconds;

    Json visuals = Json::array();
    for (const auto& pair : manifest.visuals)
    {
        const auto& entry = pair.second;
        Json visual;
        visual["normalized_identifier"] = entry.normalized_identifier.c_str();
        visual["source_root"] = entry.source_root.c_str();
        visual["source_path"] = entry.source_path.c_str();
        visual["source_timestamp_seconds"] = entry.source_timestamp_seconds;
        visual["source_size"] = entry.source_size;
        visual["skeleton_output_root"] = entry.skeleton_output_root.c_str();
        visual["skeleton_output_path"] = entry.skeleton_output_path.c_str();
        visual["skeleton_output_timestamp_seconds"] = entry.skeleton_output_timestamp_seconds;
        visual["bundle_output_root"] = entry.bundle_output_root.c_str();
        visual["bundle_output_path"] = entry.bundle_output_path.c_str();
        visual["bundle_output_timestamp_seconds"] = entry.bundle_output_timestamp_seconds;
        visual["last_status"] = StatusToString(entry.last_status);
        visual["last_error"] = entry.last_error.c_str();
        visual["last_duration_milliseconds"] = entry.last_duration_milliseconds;
        visual["converter_version"] = entry.converter_version.c_str();
        visual["converter_build_id"] = entry.converter_build_id.c_str();

        Json motions = Json::array();
        for (const auto& motion : entry.motions)
        {
            Json motion_json;
            motion_json["canonical_motion"] = motion.canonical_motion.c_str();
            motion_json["source_root"] = motion.source_root.c_str();
            motion_json["source_path"] = motion.source_path.c_str();
            motion_json["source_timestamp_seconds"] = motion.source_timestamp_seconds;
            motion_json["source_size"] = motion.source_size;
            motion_json["output_root"] = motion.output_root.c_str();
            motion_json["output_path"] = motion.output_path.c_str();
            motion_json["output_timestamp_seconds"] = motion.output_timestamp_seconds;
            motions.push_back(std::move(motion_json));
        }
        visual["motions"] = std::move(motions);
        visuals.push_back(std::move(visual));
    }

    json["visuals"] = std::move(visuals);

    const auto parent = manifest_path.parent_path();
    std::error_code ec;
    if (!parent.empty())
        fs::create_directories(parent, ec);

    std::ofstream stream(manifest_path);
    if (!stream)
    {
        Msg("! [ozz] Failed to open manifest for writing: %s", manifest_path.string().c_str());
        return;
    }

    stream << json.dump(2);
}

SkipDecision EvaluateSkipDecision(const LegacyVisualAsset& asset,
    const CacheManifest& manifest,
    const ManifestSkipOptions& options,
    const LegacyAssetInventory& inventory)
{
    SkipDecision decision;

    if (options.IsForced(asset.normalized_identifier))
    {
        decision.should_skip = false;
        decision.reason = "forced rebuild";
        return decision;
    }

    const auto* manifest_entry = manifest.FindVisual(asset.normalized_identifier);
    if (!manifest_entry)
    {
        decision.should_skip = false;
        decision.reason = "manifest entry missing";
        return decision;
    }

    if (manifest_entry->last_status != ConversionStatus::Success)
    {
        if (options.force_failed_rebuild)
        {
            decision.should_skip = false;
            decision.reason = "prior conversion failed";
            return decision;
        }

        decision.should_skip = false;
        decision.reason = "prior conversion did not succeed";
        return decision;
    }

    const auto* source = asset.FindSource(manifest_entry->source_root, manifest_entry->source_path);
    if (!source)
    {
        decision.should_skip = false;
        decision.reason = "source mapping changed";
        return decision;
    }

    if (source->location.modified_time_seconds > manifest_entry->source_timestamp_seconds)
    {
        decision.should_skip = false;
        decision.reason = "source file newer than manifest";
        return decision;
    }

    if (source->location.file_size != manifest_entry->source_size)
    {
        decision.should_skip = false;
        decision.reason = "source size changed";
        return decision;
    }

    xr_string freshness_reason;
    if (!ValidateOutputFreshness(manifest_entry->skeleton_output_root,
            manifest_entry->skeleton_output_path,
            manifest_entry->skeleton_output_timestamp_seconds,
            source->location.modified_time_seconds,
            freshness_reason))
    {
        decision.should_skip = false;
        decision.reason = freshness_reason;
        return decision;
    }

    if (!ValidateOutputFreshness(manifest_entry->bundle_output_root,
            manifest_entry->bundle_output_path,
            manifest_entry->bundle_output_timestamp_seconds,
            source->location.modified_time_seconds,
            freshness_reason))
    {
        decision.should_skip = false;
        decision.reason = freshness_reason;
        return decision;
    }

    for (const auto& reference : source->motion_references)
    {
        const auto* motion_entry = manifest_entry->FindMotion(reference);
        if (!motion_entry)
        {
            decision.should_skip = false;
            decision.reason = xr_string("manifest missing motion ") + reference;
            return decision;
        }

        const auto* motion_asset = inventory.FindMotion(CanonicalizeMotionName(reference));
        if (motion_asset)
        {
            if (motion_asset->modified_time_seconds > motion_entry->source_timestamp_seconds)
            {
                decision.should_skip = false;
                decision.reason = xr_string("motion newer than manifest: ") + reference;
                return decision;
            }

            if (motion_asset->file_size != motion_entry->source_size)
            {
                decision.should_skip = false;
                decision.reason = xr_string("motion size changed: ") + reference;
                return decision;
            }
        }

        if (!ValidateOutputFreshness(motion_entry->output_root,
                motion_entry->output_path,
                motion_entry->output_timestamp_seconds,
                motion_asset ? motion_asset->modified_time_seconds : manifest_entry->source_timestamp_seconds,
                freshness_reason))
        {
            decision.should_skip = false;
            decision.reason = freshness_reason;
            return decision;
        }
    }

    decision.should_skip = true;
    decision.reason = "manifest outputs up-to-date";
    return decision;
}

} // namespace Animation
} // namespace XRay
