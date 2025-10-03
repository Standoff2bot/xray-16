#include "stdafx.h"

#include "OzzBundle.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

namespace XRay
{
namespace Animation
{
namespace
{
constexpr char kOzzxMagic[8] = { 'O', 'Z', 'Z', 'X', 'P', 'A', 'C', 'K' };
constexpr char kBoneMetadataMagic[4] = { 'B', 'M', 'D', 'T' };
constexpr std::uint32_t kBoneMetadataVersion = 1u;
}

static bool ReadFully(std::ifstream& stream, void* buffer, std::size_t size)
{
    stream.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return static_cast<std::size_t>(stream.gcount()) == size;
}

static bool WriteFully(std::ofstream& stream, const void* buffer, std::size_t size)
{
    stream.write(reinterpret_cast<const char*>(buffer), static_cast<std::streamsize>(size));
    return static_cast<bool>(stream);
}

static bool ReadBoneMetadataBlock(std::ifstream& stream, ExtendedBoneMetadataCollection& out_metadata)
{
    const std::streampos checkpoint = stream.tellg();

    char magic[sizeof(kBoneMetadataMagic)] = {};
    if (!ReadFully(stream, magic, sizeof(magic)))
    {
        stream.clear();
        stream.seekg(checkpoint, std::ios::beg);
        out_metadata.clear();
        return true;
    }

    if (std::memcmp(magic, kBoneMetadataMagic, sizeof(kBoneMetadataMagic)) != 0)
    {
        stream.seekg(checkpoint, std::ios::beg);
        out_metadata.clear();
        return true;
    }

    std::uint32_t metadata_version = 0;
    if (!ReadFully(stream, &metadata_version, sizeof(metadata_version)))
        return false;

    if (metadata_version != kBoneMetadataVersion)
    {
        std::cerr << "Unsupported bone metadata version in .ozzx bundle: " << metadata_version << std::endl;
        return false;
    }

    std::uint32_t count = 0;
    if (!ReadFully(stream, &count, sizeof(count)))
        return false;

    out_metadata.clear();
    out_metadata.resize(count);

    for (std::uint32_t index = 0; index < count; ++index)
    {
        auto& entry = out_metadata[index];

        if (!ReadFully(stream, &entry.shape, sizeof(entry.shape)))
            return false;
        if (!ReadFully(stream, &entry.joint, sizeof(entry.joint)))
            return false;
        if (!ReadFully(stream, &entry.mass, sizeof(entry.mass)))
            return false;
        if (!ReadFully(stream, &entry.center_of_mass, sizeof(entry.center_of_mass)))
            return false;
        if (!ReadFully(stream, &entry.rest_length, sizeof(entry.rest_length)))
            return false;
        if (!ReadFully(stream, &entry.dominant_axis, sizeof(entry.dominant_axis)))
            return false;
        if (!ReadFully(stream, &entry.local_aabb_min, sizeof(entry.local_aabb_min)))
            return false;
        if (!ReadFully(stream, &entry.local_aabb_max, sizeof(entry.local_aabb_max)))
            return false;
        if (!ReadFully(stream, &entry.inverse_global_transform, sizeof(entry.inverse_global_transform)))
            return false;
        if (!ReadFully(stream, &entry.inertia_tensor, sizeof(entry.inertia_tensor)))
            return false;
        if (!ReadFully(stream, &entry.volume, sizeof(entry.volume)))
            return false;

        std::uint32_t layers = 0;
        if (!ReadFully(stream, &layers, sizeof(layers)))
            return false;
        entry.collision_layers.assign(layers);

        std::uint8_t ground = 0;
        std::uint8_t weapon = 0;
        if (!ReadFully(stream, &ground, sizeof(ground)) || !ReadFully(stream, &weapon, sizeof(weapon)))
            return false;
        entry.ground_contact_candidate = ground != 0;
        entry.weapon_anchor_candidate = weapon != 0;

        std::uint32_t material_length = 0;
        if (!ReadFully(stream, &material_length, sizeof(material_length)))
            return false;

        xr_string material;
        material.resize(material_length);
        if (material_length > 0)
        {
            if (!ReadFully(stream, material.data(), material_length))
                return false;
        }
        entry.game_material = std::move(material);
    }

    return true;
}

static bool WriteBoneMetadataBlock(std::ofstream& stream, const ExtendedBoneMetadataCollection& metadata)
{
    if (metadata.size() > static_cast<size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        std::cerr << "Too many bones for metadata block in .ozzx bundle: " << metadata.size() << std::endl;
        return false;
    }

    if (!WriteFully(stream, kBoneMetadataMagic, sizeof(kBoneMetadataMagic)))
        return false;

    const std::uint32_t version = kBoneMetadataVersion;
    if (!WriteFully(stream, &version, sizeof(version)))
        return false;

    const std::uint32_t count = static_cast<std::uint32_t>(metadata.size());
    if (!WriteFully(stream, &count, sizeof(count)))
        return false;

    for (const auto& entry : metadata)
    {
        if (!WriteFully(stream, &entry.shape, sizeof(entry.shape)))
            return false;
        if (!WriteFully(stream, &entry.joint, sizeof(entry.joint)))
            return false;
        if (!WriteFully(stream, &entry.mass, sizeof(entry.mass)))
            return false;
        if (!WriteFully(stream, &entry.center_of_mass, sizeof(entry.center_of_mass)))
            return false;
        if (!WriteFully(stream, &entry.rest_length, sizeof(entry.rest_length)))
            return false;
        if (!WriteFully(stream, &entry.dominant_axis, sizeof(entry.dominant_axis)))
            return false;
        if (!WriteFully(stream, &entry.local_aabb_min, sizeof(entry.local_aabb_min)))
            return false;
        if (!WriteFully(stream, &entry.local_aabb_max, sizeof(entry.local_aabb_max)))
            return false;
        if (!WriteFully(stream, &entry.inverse_global_transform, sizeof(entry.inverse_global_transform)))
            return false;
        if (!WriteFully(stream, &entry.inertia_tensor, sizeof(entry.inertia_tensor)))
            return false;
        if (!WriteFully(stream, &entry.volume, sizeof(entry.volume)))
            return false;

        const std::uint32_t layers = entry.collision_layers.get();
        if (!WriteFully(stream, &layers, sizeof(layers)))
            return false;

        const std::uint8_t ground = entry.ground_contact_candidate ? 1u : 0u;
        const std::uint8_t weapon = entry.weapon_anchor_candidate ? 1u : 0u;
        if (!WriteFully(stream, &ground, sizeof(ground)) || !WriteFully(stream, &weapon, sizeof(weapon)))
            return false;

        const xr_string& material = entry.game_material;
        if (material.size() > static_cast<size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            std::cerr << "Bone material string too large for .ozzx bundle" << std::endl;
            return false;
        }

        const std::uint32_t length = static_cast<std::uint32_t>(material.size());
        if (!WriteFully(stream, &length, sizeof(length)))
            return false;
        if (length > 0 && !WriteFully(stream, material.data(), length))
            return false;
    }

    return true;
}

bool ReadOzzxBundle(const std::filesystem::path& path, OzzxBundle& out_bundle)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        std::cerr << "Failed to open .ozzx bundle: " << path << std::endl;
        return false;
    }

    char magic[sizeof(kOzzxMagic)] = {};
    if (!ReadFully(stream, magic, sizeof(magic)))
    {
        std::cerr << "Failed to read bundle magic from: " << path << std::endl;
        return false;
    }

    if (std::memcmp(magic, kOzzxMagic, sizeof(kOzzxMagic)) != 0)
    {
        std::cerr << "Unexpected bundle magic in: " << path << std::endl;
        return false;
    }

    std::uint32_t version = 0;
    std::uint32_t skeleton_size = 0;
    std::uint32_t mesh_size = 0;

    if (!ReadFully(stream, &version, sizeof(version)) ||
        !ReadFully(stream, &skeleton_size, sizeof(skeleton_size)) ||
        !ReadFully(stream, &mesh_size, sizeof(mesh_size)))
    {
        std::cerr << "Failed to read bundle header from: " << path << std::endl;
        return false;
    }

    out_bundle.version = version;
    out_bundle.skeleton.resize(skeleton_size);
    out_bundle.mesh.resize(mesh_size);

    if (!ReadFully(stream, out_bundle.skeleton.data(), out_bundle.skeleton.size()))
    {
        std::cerr << "Failed to read skeleton payload from: " << path << std::endl;
        return false;
    }

    if (!ReadFully(stream, out_bundle.mesh.data(), out_bundle.mesh.size()))
    {
        std::cerr << "Failed to read mesh payload from: " << path << std::endl;
        return false;
    }

    if (!ReadBoneMetadataBlock(stream, out_bundle.bone_metadata))
    {
        std::cerr << "Failed to read bone metadata block from: " << path << std::endl;
        return false;
    }

    out_bundle.motion_refs.clear();

    std::uint32_t motion_ref_count = 0;
    if (!ReadFully(stream, &motion_ref_count, sizeof(motion_ref_count)))
    {
        std::cerr << "Failed to read motion reference count from: " << path << std::endl;
        return false;
    }

    if (motion_ref_count > 0)
    {
        out_bundle.motion_refs.reserve(motion_ref_count);
        for (std::uint32_t index = 0; index < motion_ref_count; ++index)
        {
            std::uint32_t length = 0;
            if (!ReadFully(stream, &length, sizeof(length)))
            {
                std::cerr << "Failed to read motion reference length from: " << path << std::endl;
                return false;
            }

            xr_string entry;
            entry.resize(length, '\0');

            if (length > 0)
            {
                if (!ReadFully(stream, entry.data(), length))
                {
                    std::cerr << "Failed to read motion reference data from: " << path << std::endl;
                    return false;
                }
            }

            out_bundle.motion_refs.emplace_back(std::move(entry));
        }
    }

    return true;
}

bool WriteOzzxBundle(const std::filesystem::path& path, const OzzxBundle& bundle)
{
    if (bundle.skeleton.size() > std::numeric_limits<std::uint32_t>::max())
    {
        std::cerr << "Skeleton payload too large for .ozzx bundle: " << bundle.skeleton.size() << std::endl;
        return false;
    }

    if (bundle.mesh.size() > std::numeric_limits<std::uint32_t>::max())
    {
        std::cerr << "Mesh payload too large for .ozzx bundle: " << bundle.mesh.size() << std::endl;
        return false;
    }

    std::error_code ec;
    if (const auto parent = path.parent_path(); !parent.empty())
        std::filesystem::create_directories(parent, ec);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        std::cerr << "Failed to open bundle for writing: " << path << std::endl;
        return false;
    }

    if (!WriteFully(stream, kOzzxMagic, sizeof(kOzzxMagic)))
    {
        std::cerr << "Failed to write bundle magic: " << path << std::endl;
        return false;
    }

    const std::uint32_t version = 1u;

    const std::uint32_t skeleton_size = static_cast<std::uint32_t>(bundle.skeleton.size());
    const std::uint32_t mesh_size = static_cast<std::uint32_t>(bundle.mesh.size());
    const std::uint32_t motion_ref_count = static_cast<std::uint32_t>(bundle.motion_refs.size());

    if (!WriteFully(stream, &version, sizeof(version)) ||
        !WriteFully(stream, &skeleton_size, sizeof(skeleton_size)) ||
        !WriteFully(stream, &mesh_size, sizeof(mesh_size)))
    {
        std::cerr << "Failed to write bundle header: " << path << std::endl;
        return false;
    }

    if (!bundle.skeleton.empty() && !WriteFully(stream, bundle.skeleton.data(), bundle.skeleton.size()))
    {
        std::cerr << "Failed to write skeleton payload: " << path << std::endl;
        return false;
    }

    if (!bundle.mesh.empty() && !WriteFully(stream, bundle.mesh.data(), bundle.mesh.size()))
    {
        std::cerr << "Failed to write mesh payload: " << path << std::endl;
        return false;
    }

    if (!WriteBoneMetadataBlock(stream, bundle.bone_metadata))
    {
        std::cerr << "Failed to write bone metadata block: " << path << std::endl;
        return false;
    }

    if (!WriteFully(stream, &motion_ref_count, sizeof(motion_ref_count)))
    {
        std::cerr << "Failed to write motion reference count: " << path << std::endl;
        return false;
    }

    for (const auto& reference : bundle.motion_refs)
    {
        const std::uint32_t length = static_cast<std::uint32_t>(reference.size());
        if (!WriteFully(stream, &length, sizeof(length)))
        {
            std::cerr << "Failed to write motion reference length: " << path << std::endl;
            return false;
        }

        if (length > 0)
        {
            if (!WriteFully(stream, reference.data(), length))
            {
                std::cerr << "Failed to write motion reference data: " << path << std::endl;
                return false;
            }
        }
    }

    return true;
}
} // namespace Animation
} // namespace XRay
