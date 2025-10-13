#pragma once

#include "xrSound/Sound.h"
#include "xrCDB/xrCDB.h"
#include <phonon.h>

namespace SteamAudio
{

class CSteamAudioMaterials;

/**
 * @brief RAII wrapper for Steam Audio scene and simulator
 *
 * Lifecycle: Level load → unload
 * Thread-safety: Create/destroy on main thread, simulator can run on separate thread
 *
 * Manages:
 * - IPLScene (acoustic scene container)
 * - IPLStaticMesh (level geometry for ray tracing)
 * - IPLSimulator (acoustic simulation state)
 * - IPLProbeBatch (for baked reverb/pathing, optional)
 */
class CSteamAudioScene
{
public:
    CSteamAudioScene() = default;
    ~CSteamAudioScene();

    // Non-copyable
    CSteamAudioScene(const CSteamAudioScene&) = delete;
    CSteamAudioScene& operator=(const CSteamAudioScene&) = delete;

    /**
     * @brief Initialize scene and simulator
     * @param context Steam Audio context
     * @param audioSettings Audio format settings
     * @return true if initialization succeeded
     */
    bool Initialize(IPLContext context, const IPLAudioSettings& audioSettings);

    /**
     * @brief Load geometry from X-Ray collision database
     * @param model X-Ray CDB model containing level geometry
     * @param aabb Axis-aligned bounding box for the geometry
     * @param materials Material database for acoustic properties (can be nullptr for GMLib fallback)
     * @param bakePaths Whether to bake path data (can cause long load times)
     * @return true if geometry loaded successfully
     */
    bool LoadGeometry(CDB::MODEL* model, const Fbox& aabb, CSteamAudioMaterials* materials = nullptr, bool bakePaths = false);

    /**
     * @brief Clear all geometry from the scene
     */
    void ClearGeometry();

    /**
     * @brief Update listener position and orientation (call every frame)
     * @param position World-space position
     * @param forward Forward direction (normalized)
     * @param up Up direction (normalized)
     * @param right Right direction (normalized)
     */
    void UpdateListener(const Fvector& position, const Fvector& forward, const Fvector& up, const Fvector& right);

    /**
     * @brief Run acoustic simulation (direct sound)
     * Call this once per frame, preferably on a separate thread
     */
    void RunDirectSimulation();

    /**
     * @brief Commit pending simulator changes (sources added/removed)
     */
    void CommitSimulator();

    /**
     * @brief Check if scene is initialized and ready
     */
    [[nodiscard]] bool IsValid() const { return m_scene != nullptr && m_simulator != nullptr; }

    // Accessors
    [[nodiscard]] IPLScene GetScene() const { return m_scene; }
    [[nodiscard]] IPLSimulator GetSimulator() const { return m_simulator; }
    [[nodiscard]] IPLStaticMesh GetStaticMesh() const { return m_staticMesh; }
    [[nodiscard]] IPLProbeBatch GetProbeBatch() const { return m_probeBatch; }

private:
    bool BakePathData(const Fbox& aabb);

    IPLContext m_context{};
    IPLScene m_scene{};
    IPLStaticMesh m_staticMesh{};
    IPLSimulator m_simulator{};
    IPLProbeBatch m_probeBatch{};
    IPLAudioSettings m_audioSettings{};
};

} // namespace SteamAudio
