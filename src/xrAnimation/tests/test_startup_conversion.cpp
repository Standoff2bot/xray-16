#include "Common/Platform.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "xrCore/xrCore.h"
#include "xrCore/FS.h"
#include "xrCore/FMesh.hpp"

#include "StartupConversionInventory.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace
{
std::string MakeUniqueAlias(const char* prefix)
{
    static std::atomic<int> counter{ 0 };
    return std::string("$") + prefix + "_" + std::to_string(counter.fetch_add(1)) + "$";
}

void WriteBinaryFile(const fs::path& path, const std::vector<uint8_t>& data)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("failed to open file for writing: " + path.string());

    if (!data.empty())
        stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void WriteOgfWithMotionRefs(const fs::path& path, const std::vector<std::string>& references)
{
    std::vector<uint8_t> chunk;
    auto append_u32 = [&chunk](uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            chunk.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    };
    auto append_stringz = [&chunk](const std::string& value)
    {
        chunk.insert(chunk.end(), value.begin(), value.end());
        chunk.push_back(0);
    };

    append_u32(static_cast<uint32_t>(references.size()));
    for (const auto& ref : references)
        append_stringz(ref);

    std::vector<uint8_t> file_data;
    auto append_chunk_header = [&file_data](uint32_t id, uint32_t size)
    {
        for (int shift = 0; shift < 32; shift += 8)
            file_data.push_back(static_cast<uint8_t>((id >> shift) & 0xFF));
        for (int shift = 0; shift < 32; shift += 8)
            file_data.push_back(static_cast<uint8_t>((size >> shift) & 0xFF));
    };

    append_chunk_header(OGF_S_MOTION_REFS2, static_cast<uint32_t>(chunk.size()));
    file_data.insert(file_data.end(), chunk.begin(), chunk.end());

    WriteBinaryFile(path, file_data);
}

void WriteTextFile(const fs::path& path, const std::string& contents)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("failed to open file for writing: " + path.string());
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

fs::file_time_type ToFileTime(const std::chrono::system_clock::time_point& tp)
{
    return fs::file_time_type::clock::now() + (tp - std::chrono::system_clock::now());
}

void SetFileTimestamp(const fs::path& path, const std::chrono::system_clock::time_point& tp)
{
    std::error_code ec;
    fs::last_write_time(path, ToFileTime(tp), ec);
    if (ec)
        throw std::runtime_error("failed to set file time: " + path.string());
}

std::int64_t FileTimestampSeconds(const fs::path& path)
{
    std::error_code ec;
    const auto file_time = fs::last_write_time(path, ec);
    if (ec)
        throw std::runtime_error("failed to read file time: " + path.string());
    using namespace std::chrono;
    const auto system_time = time_point_cast<system_clock::duration>(file_time - fs::file_time_type::clock::now() + system_clock::now());
    return duration_cast<seconds>(system_time.time_since_epoch()).count();
}

std::string CanonicalMotionOutput(const std::string& name)
{
    std::string result = "anims\\";
    result += name;
    result += ".ozz";
    return result;
}

std::string CanonicalizeRelative(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    std::replace(value.begin(), value.end(), '/', '\\');
    while (!value.empty() && (value.front() == '\\' || value.front() == '/'))
        value.erase(value.begin());
    return value;
}

std::string CanonicalizeReference(std::string value)
{
    value = CanonicalizeRelative(std::move(value));
    if (value.size() < 4 || value.substr(value.size() - 4) != ".omf")
        value.append(".omf");
    return value;
}

std::string CanonicalMotionName(std::string value)
{
    value = CanonicalizeReference(std::move(value));
    const auto dot = value.find_last_of('.');
    if (dot != std::string::npos)
        value.erase(dot);
    return value;
}

std::string ToNativePath(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', fs::path::preferred_separator);
    return value;
}

} // namespace

class StartupConversionStageTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        using namespace std::chrono;
        temp_root_ = fs::temp_directory_path() / fs::path("ozz_inventory_" + MakeUniqueAlias("session"));
        meshes_root_ = temp_root_ / "meshes";
        converted_root_ = temp_root_ / "converted";
        actors_root_ = meshes_root_ / "actors";
        skeleton_root_ = converted_root_ / "skeletons" / "actors";
        bundle_root_ = converted_root_ / "bundles" / "actors";
        anim_root_ = converted_root_ / "anims" / "actors";

        fs::create_directories(actors_root_);
        fs::create_directories(skeleton_root_);
        fs::create_directories(bundle_root_);
        fs::create_directories(anim_root_);

        const auto source_time = std::chrono::system_clock::now() - 2min;
        const auto output_time = std::chrono::system_clock::now() - 1min;

        WriteOgfWithMotionRefs(actors_root_ / "test_visual.ogf", { "actors\\test_motion", "actors\\idle_motion" });
        SetFileTimestamp(actors_root_ / "test_visual.ogf", source_time);

        WriteTextFile(actors_root_ / "test_motion.omf", "motion");
        SetFileTimestamp(actors_root_ / "test_motion.omf", source_time);
        WriteTextFile(actors_root_ / "idle_motion.omf", "idle");
        SetFileTimestamp(actors_root_ / "idle_motion.omf", source_time);

        WriteTextFile(skeleton_root_ / "test_visual.ozz", "skeleton");
        SetFileTimestamp(skeleton_root_ / "test_visual.ozz", output_time);

        WriteTextFile(bundle_root_ / "test_visual.ozzx", "bundle");
        SetFileTimestamp(bundle_root_ / "test_visual.ozzx", output_time);

        WriteTextFile(anim_root_ / "test_motion.ozz", "anim");
        SetFileTimestamp(anim_root_ / "test_motion.ozz", output_time);
        WriteTextFile(anim_root_ / "idle_motion.ozz", "anim");
        SetFileTimestamp(anim_root_ / "idle_motion.ozz", output_time);

        mesh_alias_ = MakeUniqueAlias("mesh_stage");
        convert_alias_ = MakeUniqueAlias("converted_stage");

        FS.append_path(mesh_alias_.c_str(), meshes_root_.string().c_str(), nullptr, true);
        FS.append_path(convert_alias_.c_str(), converted_root_.string().c_str(), nullptr, true);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(temp_root_, ec);
    }

    XRay::Animation::InventoryScanConfig BuildConfig() const
    {
        XRay::Animation::InventoryScanConfig config;
        config.visual_roots = { xr_string(mesh_alias_.c_str()) };
        config.motion_roots = { xr_string(mesh_alias_.c_str()) };
        return config;
    }

    XRay::Animation::LegacyAssetInventory BuildInventory() const
    {
        return XRay::Animation::BuildLegacyAssetInventory(BuildConfig());
    }

    XRay::Animation::CacheManifest BuildUpToDateManifest(const XRay::Animation::LegacyAssetInventory& inventory) const
    {
        XRay::Animation::CacheManifest manifest;
        manifest.manifest_version = "1";
        manifest.schema_version = "1";

        const auto* asset = inventory.FindVisual(xr_string("actors\\test_visual"));
        EXPECT_NE(asset, nullptr);
        const auto* source = asset ? asset->PrimarySource() : nullptr;
        EXPECT_NE(source, nullptr);
        if (!asset || !source)
            return manifest;

        XRay::Animation::CacheManifest::VisualEntry entry;
        entry.normalized_identifier = asset->normalized_identifier;
        entry.source_root = source->location.root_alias;
        entry.source_path = source->location.relative_path;
        entry.source_timestamp_seconds = source->location.modified_time_seconds;
        entry.source_size = source->location.file_size;
        entry.skeleton_output_root = xr_string(convert_alias_.c_str());
        entry.skeleton_output_path = xr_string("skeletons\\actors\\test_visual.ozz");
        entry.skeleton_output_timestamp_seconds = FileTimestampSeconds(skeleton_root_ / "test_visual.ozz");
        entry.bundle_output_root = xr_string(convert_alias_.c_str());
        entry.bundle_output_path = xr_string("bundles\\actors\\test_visual.ozzx");
        entry.bundle_output_timestamp_seconds = FileTimestampSeconds(bundle_root_ / "test_visual.ozzx");
        entry.last_status = XRay::Animation::ConversionStatus::Success;

        for (const auto& reference : source->motion_references)
        {
            XRay::Animation::CacheManifest::MotionEntry motion;
            motion.canonical_motion = reference;
            const std::string reference_str(reference.c_str());
            const std::string canonical_name = CanonicalMotionName(reference_str);
            const auto* motion_asset = inventory.FindMotion(xr_string(canonical_name.c_str()));

            if (motion_asset)
            {
                motion.source_root = motion_asset->root_alias;
                motion.source_path = motion_asset->relative_path;
                motion.source_timestamp_seconds = motion_asset->modified_time_seconds;
                motion.source_size = motion_asset->file_size;
            }
            else
            {
                ADD_FAILURE() << "missing motion asset for " << reference_str;
                motion.source_root = xr_string(mesh_alias_.c_str());
                motion.source_path = reference;
                motion.source_timestamp_seconds = 0;
                motion.source_size = 0;
            }

            std::string motion_relative = CanonicalMotionOutput(canonical_name);
            motion.output_root = xr_string(convert_alias_.c_str());
            motion.output_path = xr_string(motion_relative.c_str());
            const auto output_path = converted_root_ / fs::path(ToNativePath(motion_relative));
            motion.output_timestamp_seconds = FileTimestampSeconds(output_path);
            entry.motions.emplace_back(std::move(motion));
        }

        manifest.visuals.emplace(entry.normalized_identifier, std::move(entry));
        return manifest;
    }

    fs::path temp_root_;
    fs::path meshes_root_;
    fs::path converted_root_;
    fs::path actors_root_;
    fs::path skeleton_root_;
    fs::path bundle_root_;
    fs::path anim_root_;
    std::string mesh_alias_;
    std::string convert_alias_;
};

TEST_F(StartupConversionStageTest, BuildsInventoryFromCustomAlias)
{
    const auto inventory = BuildInventory();
    ASSERT_EQ(inventory.visuals.size(), 1u);
    ASSERT_EQ(inventory.motions.size(), 2u);

    const auto& asset = inventory.visuals.front();
    EXPECT_EQ(asset.normalized_identifier, xr_string("actors\\test_visual"));
    ASSERT_EQ(asset.sources.size(), 1u);
    const auto& source = asset.sources.front();
    EXPECT_EQ(source.motion_references.size(), 2u);
    EXPECT_NE(std::find(source.motion_references.begin(), source.motion_references.end(), xr_string("actors\\test_motion.omf")), source.motion_references.end());
    EXPECT_NE(std::find(source.motion_references.begin(), source.motion_references.end(), xr_string("actors\\idle_motion.omf")), source.motion_references.end());
}

TEST_F(StartupConversionStageTest, ManifestRoundTripPersistsVisualEntry)
{
    const auto inventory = BuildInventory();
    auto manifest = BuildUpToDateManifest(inventory);
    const auto manifest_path = temp_root_ / "manifest.json";

    XRay::Animation::SaveCacheManifest(manifest, manifest_path);
    const auto loaded = XRay::Animation::LoadCacheManifest(manifest_path);

    const auto* original = manifest.FindVisual(xr_string("actors\\test_visual"));
    ASSERT_NE(original, nullptr);
    const auto* restored = loaded.FindVisual(xr_string("actors\\test_visual"));
    ASSERT_NE(restored, nullptr);

    EXPECT_EQ(restored->source_root, original->source_root);
    EXPECT_EQ(restored->source_path, original->source_path);
    EXPECT_EQ(restored->source_timestamp_seconds, original->source_timestamp_seconds);
    EXPECT_EQ(restored->motions.size(), original->motions.size());
}

TEST_F(StartupConversionStageTest, SkipDecisionAcknowledgesUpToDateOutputs)
{
    auto inventory = BuildInventory();
    auto manifest = BuildUpToDateManifest(inventory);

    const auto* asset = inventory.FindVisual(xr_string("actors\\test_visual"));
    ASSERT_NE(asset, nullptr);

    XRay::Animation::ManifestSkipOptions options;
    const auto decision = XRay::Animation::EvaluateSkipDecision(*asset, manifest, options, inventory);
    const std::string trace_message = std::string("skip_reason: ") + decision.reason.c_str();
    SCOPED_TRACE(trace_message);
    EXPECT_TRUE(decision.should_skip);
}

TEST_F(StartupConversionStageTest, SkipDecisionFailsWhenBundleMissing)
{
    auto inventory = BuildInventory();
    auto manifest = BuildUpToDateManifest(inventory);

    std::error_code ec;
    fs::remove(bundle_root_ / "test_visual.ozzx", ec);

    const auto* asset = inventory.FindVisual(xr_string("actors\\test_visual"));
    ASSERT_NE(asset, nullptr);

    XRay::Animation::ManifestSkipOptions options;
    const auto decision = XRay::Animation::EvaluateSkipDecision(*asset, manifest, options, inventory);
    EXPECT_FALSE(decision.should_skip);
}

TEST_F(StartupConversionStageTest, SkipDecisionHonorsForcedRebuild)
{
    auto inventory = BuildInventory();
    auto manifest = BuildUpToDateManifest(inventory);
    const auto* asset = inventory.FindVisual(xr_string("actors\\test_visual"));
    ASSERT_NE(asset, nullptr);

    XRay::Animation::ManifestSkipOptions options;
    options.force_full_rebuild = true;
    const auto decision = XRay::Animation::EvaluateSkipDecision(*asset, manifest, options, inventory);
    EXPECT_FALSE(decision.should_skip);
}
