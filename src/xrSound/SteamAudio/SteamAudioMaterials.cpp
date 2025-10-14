#include "stdafx.h"
#include "SteamAudioMaterials.h"
#include "xrCore/FS.h"
#include "xrCore/xrCore.h"

#include <algorithm> // for std::clamp

namespace SteamAudio
{

CSteamAudioMaterials::CSteamAudioMaterials()
{
    LoadDefaults();
}

CSteamAudioMaterials::~CSteamAudioMaterials()
{
    m_materials.clear();
}

void CSteamAudioMaterials::LoadDefaults()
{
    // Default material (generic surfaces)
    m_defaultMaterial.absorption[0] = 0.10f; // Low frequency (250-500 Hz)
    m_defaultMaterial.absorption[1] = 0.20f; // Mid frequency (1-2 kHz)
    m_defaultMaterial.absorption[2] = 0.30f; // High frequency (4-8 kHz)
    m_defaultMaterial.scattering = 0.05f;    // Mostly specular reflections
    m_defaultMaterial.transmission[0] = 0.10f; // Low
    m_defaultMaterial.transmission[1] = 0.05f; // Mid
    m_defaultMaterial.transmission[2] = 0.03f; // High

    m_materials.clear();

    // Common X-Ray materials with realistic acoustic properties
    MaterialEntry entry;

    // Concrete (walls, floors in buildings)
    entry.name = "materials\\concrete";
    entry.material.absorption[0] = 0.05f;
    entry.material.absorption[1] = 0.07f;
    entry.material.absorption[2] = 0.08f;
    entry.material.scattering = 0.05f;
    entry.material.transmission[0] = 0.02f;
    entry.material.transmission[1] = 0.01f;
    entry.material.transmission[2] = 0.01f;
    m_materials.push_back(entry);

    // Metal (doors, machinery, containers)
    entry.name = "materials\\metal";
    entry.material.absorption[0] = 0.01f;
    entry.material.absorption[1] = 0.02f;
    entry.material.absorption[2] = 0.02f;
    entry.material.scattering = 0.10f;
    entry.material.transmission[0] = 0.20f;
    entry.material.transmission[1] = 0.15f;
    entry.material.transmission[2] = 0.10f;
    m_materials.push_back(entry);

    // Wood (doors, furniture, floors)
    entry.name = "materials\\wood";
    entry.material.absorption[0] = 0.15f;
    entry.material.absorption[1] = 0.25f;
    entry.material.absorption[2] = 0.30f;
    entry.material.scattering = 0.10f;
    entry.material.transmission[0] = 0.10f;
    entry.material.transmission[1] = 0.08f;
    entry.material.transmission[2] = 0.05f;
    m_materials.push_back(entry);

    // Glass (windows)
    entry.name = "materials\\glass";
    entry.material.absorption[0] = 0.03f;
    entry.material.absorption[1] = 0.02f;
    entry.material.absorption[2] = 0.02f;
    entry.material.scattering = 0.05f;
    entry.material.transmission[0] = 0.90f; // Glass transmits a lot of sound
    entry.material.transmission[1] = 0.85f;
    entry.material.transmission[2] = 0.80f;
    m_materials.push_back(entry);

    // Earth/Dirt (outdoor terrain, underground)
    entry.name = "materials\\earth";
    entry.material.absorption[0] = 0.15f;
    entry.material.absorption[1] = 0.25f;
    entry.material.absorption[2] = 0.40f;
    entry.material.scattering = 0.10f;
    entry.material.transmission[0] = 0.05f;
    entry.material.transmission[1] = 0.03f;
    entry.material.transmission[2] = 0.02f;
    m_materials.push_back(entry);

    // Fabric/Soft materials (curtains, cloth)
    entry.name = "materials\\fabric";
    entry.material.absorption[0] = 0.25f;
    entry.material.absorption[1] = 0.60f;
    entry.material.absorption[2] = 0.80f;
    entry.material.scattering = 0.20f;
    entry.material.transmission[0] = 0.01f;
    entry.material.transmission[1] = 0.01f;
    entry.material.transmission[2] = 0.01f;
    m_materials.push_back(entry);

    // Brick
    entry.name = "materials\\brick";
    entry.material.absorption[0] = 0.03f;
    entry.material.absorption[1] = 0.04f;
    entry.material.absorption[2] = 0.07f;
    entry.material.scattering = 0.05f;
    entry.material.transmission[0] = 0.02f;
    entry.material.transmission[1] = 0.015f;
    entry.material.transmission[2] = 0.01f;
    m_materials.push_back(entry);

    // Asphalt/Pavement
    entry.name = "materials\\asphalt";
    entry.material.absorption[0] = 0.10f;
    entry.material.absorption[1] = 0.12f;
    entry.material.absorption[2] = 0.15f;
    entry.material.scattering = 0.05f;
    entry.material.transmission[0] = 0.03f;
    entry.material.transmission[1] = 0.02f;
    entry.material.transmission[2] = 0.01f;
    m_materials.push_back(entry);

    // Anomaly zones (special: very high absorption, low transmission)
    entry.name = "materials\\anomaly";
    entry.material.absorption[0] = 0.90f;
    entry.material.absorption[1] = 0.90f;
    entry.material.absorption[2] = 0.90f;
    entry.material.scattering = 0.50f; // Highly diffuse
    entry.material.transmission[0] = 0.01f;
    entry.material.transmission[1] = 0.01f;
    entry.material.transmission[2] = 0.01f;
    m_materials.push_back(entry);

    Msg("* [Steam Audio] Loaded %d default materials", m_materials.size());
}

bool CSteamAudioMaterials::LoadFromConfig(pcstr configPath)
{
    string_path fullPath;
    if (!FS.exist(fullPath, "$game_data$", configPath))
    {
        Msg("! [Steam Audio] Material config not found: %s (using defaults)", configPath);
        return false;
    }

    IReader* reader = FS.r_open(fullPath);
    if (!reader)
    {
        Msg("! [Steam Audio] Failed to open material config: %s", fullPath);
        return false;
    }

    // Clear existing materials (except defaults loaded in constructor)
    m_materials.clear();

    string256 sectionName;
    string4096 line;
    MaterialEntry entry;

    while (!reader->eof())
    {
        reader->r_string(line, sizeof(line));
        _strlwr(line); // Convert to lowercase for consistency

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#')
            continue;

        // Parse section headers [materials]
        if (line[0] == '[')
            continue;

        // Parse material entry: material_name = low, mid, high, scattering, trans_low, trans_mid, trans_high
        // Example: materials\wood = 0.15, 0.25, 0.30, 0.10, 0.10, 0.08, 0.05
        pstr equalSign = strchr(line, '=');
        if (!equalSign)
            continue;

        // Extract material name (left side)
        *equalSign = '\0';
        pcstr materialName = line;
        pcstr values = equalSign + 1;

        // Trim whitespace from material name
        while (*materialName == ' ' || *materialName == '\t')
            materialName++;
        pstr nameEnd = const_cast<pstr>(materialName) + xr_strlen(materialName) - 1;
        while (nameEnd > materialName && (*nameEnd == ' ' || *nameEnd == '\t'))
            *nameEnd-- = '\0';

        // Parse values
        entry.name = materialName;
        entry.material = ParseMaterialFromString(values);
        m_materials.push_back(entry);
    }

    FS.r_close(reader);

    Msg("* [Steam Audio] Loaded %d materials from config: %s", m_materials.size(), configPath);
    return true;
}

IPLMaterial CSteamAudioMaterials::ParseMaterialFromString(pcstr values) const
{
    IPLMaterial material = m_defaultMaterial; // Start with default

    float absorption[3] = { 0.1f, 0.2f, 0.3f };
    float scattering = 0.05f;
    float transmission[3] = { 0.1f, 0.05f, 0.03f };

    // Parse: low, mid, high, scattering, trans_low, trans_mid, trans_high
    int parsed = sscanf(values, "%f,%f,%f,%f,%f,%f,%f",
        &absorption[0], &absorption[1], &absorption[2],
        &scattering,
        &transmission[0], &transmission[1], &transmission[2]);

    if (parsed >= 3)
    {
        material.absorption[0] = std::clamp(absorption[0], 0.0f, 1.0f);
        material.absorption[1] = std::clamp(absorption[1], 0.0f, 1.0f);
        material.absorption[2] = std::clamp(absorption[2], 0.0f, 1.0f);
    }
    if (parsed >= 4)
        material.scattering = std::clamp(scattering, 0.0f, 1.0f);
    if (parsed >= 7)
    {
        material.transmission[0] = std::clamp(transmission[0], 0.0f, 1.0f);
        material.transmission[1] = std::clamp(transmission[1], 0.0f, 1.0f);
        material.transmission[2] = std::clamp(transmission[2], 0.0f, 1.0f);
    }

    return material;
}

IPLMaterial CSteamAudioMaterials::GetMaterial(pcstr materialName) const
{
    if (!materialName || materialName[0] == '\0')
        return m_defaultMaterial;

    // Linear search (could optimize with hash map if needed)
    for (const auto& entry : m_materials)
    {
        if (0 == xr_stricmp(entry.name.c_str(), materialName))
            return entry.material;
    }

    // Not found - return default
    return m_defaultMaterial;
}

bool CSteamAudioMaterials::HasMaterial(pcstr materialName) const
{
    if (!materialName || materialName[0] == '\0')
        return false;

    for (const auto& entry : m_materials)
    {
        if (0 == xr_stricmp(entry.name.c_str(), materialName))
            return true;
    }

    return false;
}

} // namespace SteamAudio
