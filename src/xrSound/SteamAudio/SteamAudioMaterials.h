#pragma once

#include "xrCore/xrCore.h"
#include <phonon.h>

class IReader;

namespace SteamAudio
{

/**
 * @brief Material properties database for Steam Audio
 *
 * Maps X-Ray material names to Steam Audio acoustic properties.
 * Supports loading from config files with fallback to defaults.
 *
 * Steam Audio material properties:
 * - Low/Mid/High Frequency Absorption (0.0-1.0): How much sound is absorbed at different frequencies
 * - Scattering (0.0-1.0): How much sound is scattered diffusely vs reflected specularly
 * - Transmission (0.0-1.0): How much sound passes through the material
 */
class CSteamAudioMaterials
{
public:
    CSteamAudioMaterials();
    ~CSteamAudioMaterials();

    /**
     * @brief Load materials from config file
     * @param configPath Path to .ltx config file (e.g., "gamedata\\sounds\\steam_audio_materials.ltx")
     * @return True if loaded successfully, false if file not found (uses defaults)
     */
    bool LoadFromConfig(pcstr configPath);

    /**
     * @brief Get Steam Audio material for given X-Ray material name
     * @param materialName X-Ray material name (e.g., "default", "materials\\wood", "materials\\metal")
     * @return IPLMaterial with acoustic properties (falls back to default if not found)
     */
    IPLMaterial GetMaterial(pcstr materialName) const;

    /**
     * @brief Get the default material (used as fallback)
     */
    IPLMaterial GetDefaultMaterial() const { return m_defaultMaterial; }

    /**
     * @brief Check if a specific material exists in the database
     */
    bool HasMaterial(pcstr materialName) const;

    /**
     * @brief Get number of loaded materials
     */
    size_t GetMaterialCount() const { return m_materials.size(); }

private:
    void LoadDefaults();
    IPLMaterial ParseMaterialFromString(pcstr values) const;

    struct MaterialEntry
    {
        shared_str name;
        IPLMaterial material;
    };

    xr_vector<MaterialEntry> m_materials;
    IPLMaterial m_defaultMaterial;
};

} // namespace SteamAudio
