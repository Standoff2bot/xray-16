#include "stdafx.h"
#include "SteamAudioScene.h"
#include "xrCore/xrCore.h"
#include "xrCore/Threading/TaskManager.hpp"
#include "xrMaterialSystem/GameMtlLib.h"

namespace SteamAudio
{

CSteamAudioScene::~CSteamAudioScene()
{
    ClearGeometry();

    if (m_simulator)
    {
        iplSimulatorRelease(&m_simulator);
        m_simulator = nullptr;
        Msg("* SOUND: SteamAudio: Simulator released");
    }

    if (m_scene)
    {
        iplSceneRelease(&m_scene);
        m_scene = nullptr;
        Msg("* SOUND: SteamAudio: Scene released");
    }
}

bool CSteamAudioScene::Initialize(IPLContext context, const IPLAudioSettings& audioSettings)
{
    if (!context)
    {
        Msg("! SOUND: SteamAudio: Cannot initialize scene - invalid context");
        return false;
    }

    m_context = context;
    m_audioSettings = audioSettings;

    // Create scene
    IPLSceneSettings sceneSettings{
        IPL_SCENETYPE_DEFAULT,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, // User data
        nullptr, nullptr
    };

    IPLerror result = iplSceneCreate(context, &sceneSettings, &m_scene);
    if (result != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create scene, error: %d", static_cast<int>(result));
        return false;
    }

    Msg("* SOUND: SteamAudio: Scene created successfully");

    // Create simulator
    IPLSimulationSettings simulationSettings{
        IPL_SIMULATIONFLAGS_DIRECT,  // Start with direct sound only
        IPL_SCENETYPE_DEFAULT,
        IPL_REFLECTIONEFFECTTYPE_CONVOLUTION,
        128,   // Max number of rays
        4096,  // Number of diffuse samples
        32,    // Max number of sources
        2.0f,  // Max duration
        1,     // Max order (ambisonics)
        8,     // Max number of reflection bounces
        2,     // IR update rate
        5,     // Number of threads for simulation
        32,    // Ray batch size
        audioSettings.samplingRate,
        audioSettings.frameSize,
        nullptr, nullptr, nullptr,
    };

    result = iplSimulatorCreate(context, &simulationSettings, &m_simulator);
    if (result != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create simulator, error: %d", static_cast<int>(result));
        iplSceneRelease(&m_scene);
        m_scene = nullptr;
        return false;
    }

    Msg("* SOUND: SteamAudio: Simulator created successfully");

    // Link simulator to scene
    iplSimulatorSetScene(m_simulator, m_scene);
    iplSimulatorCommit(m_simulator);

    return true;
}

bool CSteamAudioScene::LoadGeometry(CDB::MODEL* model, const Fbox& aabb, bool bakePaths)
{
    if (!model || !IsValid())
    {
        Msg("! SOUND: SteamAudio: Cannot load geometry - invalid model or scene");
        return false;
    }

    // Clear existing geometry first
    ClearGeometry();

    // Get vertex and triangle data from CDB model
    const auto tris = model->get_tris();
    const s32  tris_count = static_cast<s32>(model->get_tris_count());
    const auto verts = model->get_verts();
    const s32  verts_count = static_cast<s32>(model->get_verts_count());

    Msg("* SOUND: SteamAudio: Loading geometry - %d vertices, %d triangles", verts_count, tris_count);

    // Convert CDB triangles to Steam Audio format
    auto* temp_tris = xr_alloc<IPLTriangle>(tris_count);
    auto* temp_mat_idx = xr_alloc<IPLint32>(tris_count);

    // Build material array from GameMtlLib
    xr_vector<IPLMaterial> materials;
    materials.reserve(GMLib.CountMaterial());

    for (const SGameMtl* material : GMLib.Materials())
    {
        // Reinterpret cast: SGameMtl::Acoustics should match IPLMaterial layout
        materials.emplace_back(reinterpret_cast<const IPLMaterial&>(material->Acoustics));
    }

    // Convert triangle data
    for (int i = 0; i < tris_count; ++i)
    {
        temp_tris[i] = reinterpret_cast<const IPLTriangle&>(tris[i].verts);
        temp_mat_idx[i] = tris[i].material;
    }

    // Create static mesh
    IPLStaticMeshSettings staticMeshSettings{
        verts_count, tris_count, static_cast<IPLint32>(materials.size()),
        reinterpret_cast<IPLVector3*>(const_cast<Fvector*>(verts)),
        temp_tris,
        temp_mat_idx,
        materials.data()
    };

    IPLerror result = iplStaticMeshCreate(m_scene, &staticMeshSettings, &m_staticMesh);

    // Free temporary buffers
    xr_free(temp_mat_idx);
    xr_free(temp_tris);

    if (result != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create static mesh, error: %d", static_cast<int>(result));
        return false;
    }

    // Add mesh to scene
    iplStaticMeshAdd(m_staticMesh, m_scene);
    Msg("* SOUND: SteamAudio: Static mesh added to scene");

    // Bake path data if requested
    if (bakePaths)
    {
        if (!BakePathData(aabb))
        {
            Msg("~ SOUND: SteamAudio: Path baking failed, continuing without pathing");
        }
    }

    // Commit scene changes
    iplSceneCommit(m_scene);
    Msg("* SOUND: SteamAudio: Scene committed");

    return true;
}

void CSteamAudioScene::ClearGeometry()
{
    // Release probe batch
    if (m_probeBatch)
    {
        iplProbeBatchRelease(&m_probeBatch);
        m_probeBatch = nullptr;
        Msg("* SOUND: SteamAudio: Probe batch released");
    }

    // Remove and release static mesh
    if (m_staticMesh)
    {
        iplStaticMeshRemove(m_staticMesh, m_scene);
        iplStaticMeshRelease(&m_staticMesh);
        m_staticMesh = nullptr;
        Msg("* SOUND: SteamAudio: Static mesh removed");
    }

    // Commit scene after removing geometry
    if (m_scene)
    {
        iplSceneCommit(m_scene);
    }
}

void CSteamAudioScene::UpdateListener(const Fvector& position, const Fvector& forward, const Fvector& up, const Fvector& right)
{
    if (!m_simulator)
        return;

    // Setup listener coordinate space
    IPLCoordinateSpace3 listenerCoordinates{
        reinterpret_cast<const IPLVector3&>(right),
        reinterpret_cast<const IPLVector3&>(up),
        reinterpret_cast<const IPLVector3&>(forward),
        reinterpret_cast<const IPLVector3&>(position)
    };

    // Setup shared simulation inputs
    IPLSimulationSharedInputs sharedInputs{
        listenerCoordinates,
        64,   // Number of rays for occlusion
        8,    // Number of diffuse samples
        2.0f, // Max duration
        1,    // Ambisonics order
        1.0f, // IR scale (for reflections)
        nullptr, nullptr
    };

    // Update simulator with listener info
    iplSimulatorSetSharedInputs(m_simulator, IPL_SIMULATIONFLAGS_DIRECT, &sharedInputs);
}

void CSteamAudioScene::RunDirectSimulation()
{
    if (!m_simulator)
        return;

    // Run direct sound simulation
    iplSimulatorRunDirect(m_simulator);
}

void CSteamAudioScene::CommitSimulator()
{
    if (!m_simulator)
        return;

    // Commit pending changes (sources added/removed)
    iplSimulatorCommit(m_simulator);
}

bool CSteamAudioScene::BakePathData(const Fbox& aabb)
{
    if (!m_scene)
        return false;

    Msg("* SOUND: SteamAudio: Starting path baking...");

    // Generate probe array
    IPLProbeArray probeArray{};
    IPLerror result = iplProbeArrayCreate(m_context, &probeArray);
    if (result != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create probe array, error: %d", static_cast<int>(result));
        return false;
    }

    // Get bounding box transform
    const auto transform = aabb.get_xform();

    // Setup probe generation parameters
    IPLProbeGenerationParams probeParams{
        IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR,
        2.0f, // Spacing between probes
        1.5f, // Height above floor
        reinterpret_cast<const IPLMatrix4x4&>(transform)
    };

    // Generate probes
    iplProbeArrayGenerateProbes(probeArray, m_scene, &probeParams);

    const int numProbes = iplProbeArrayGetNumProbes(probeArray);
    Msg("* SOUND: SteamAudio: Generated %d probes for path baking", numProbes);

    // Setup bake identifier
    constexpr IPLBakedDataIdentifier identifier{
        IPL_BAKEDDATATYPE_PATHING,
        IPL_BAKEDDATAVARIATION_REVERB,
        {},
    };

    // Create probe batch
    result = iplProbeBatchCreate(m_context, &m_probeBatch);
    if (result != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create probe batch, error: %d", static_cast<int>(result));
        iplProbeArrayRelease(&probeArray);  // FIX: Release probe array!
        return false;
    }

    // Add probe array to batch
    iplProbeBatchAddProbeArray(m_probeBatch, probeArray);
    iplProbeBatchCommit(m_probeBatch);

    // Setup path baking parameters
    IPLPathBakeParams pathingBakeParams{
        m_scene, m_probeBatch, identifier,
        2,     // Number of samples per probe
        2.0f,  // Probe sample radius
        0.5f,  // Downsample multiplier
        25.0f, // Path range
        50.0f, // Visibility range
        static_cast<s32>(TaskScheduler->GetWorkersCount())
    };

    // Perform baking with progress callback
    iplPathBakerBake(m_context, &pathingBakeParams,
        [](IPLfloat32 progress, void*)
        {
            if (static_cast<int>(progress * 100) % 10 == 0) // Log every 10%
                Msg("* SOUND: SteamAudio: Path baking progress: %.0f%%", progress * 100.0f);
        },
        nullptr);

    // FIX: Release probe array after baking!
    iplProbeArrayRelease(&probeArray);

    Msg("* SOUND: SteamAudio: Path baking completed");
    return true;
}

} // namespace SteamAudio
