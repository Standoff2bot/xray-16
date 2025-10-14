#include "stdafx.h"
#include "SteamAudioScene.h"
#include "SteamAudioMaterials.h"
#include "SteamAudioSource.h"

#include <cstring>
#include "xrCore/xrCore.h"
#include "xrCore/Threading/TaskManager.hpp"
#include "xrCore/_fbox.h" // for Fbox3 definition
#include "xrMaterialSystem/GameMtlLib.h"

namespace SteamAudio
{

CSteamAudioScene::~CSteamAudioScene()
{
    ClearGeometry();

    xr_delete(m_querySource);

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

bool CSteamAudioScene::Initialize(IPLContext context, const IPLAudioSettings& audioSettings, IPLOpenCLDevice openCLDevice, IPLRadeonRaysDevice radeonDevice)
{
    if (!context)
    {
        Msg("! SOUND: SteamAudio: Cannot initialize scene - invalid context");
        return false;
    }

    m_context = context;
    m_audioSettings = audioSettings;

    // Create scene
    const bool useGPU = (openCLDevice != nullptr) && (radeonDevice != nullptr);

    IPLSceneSettings sceneSettings{};
    sceneSettings.type = useGPU ? IPL_SCENETYPE_RADEONRAYS : IPL_SCENETYPE_DEFAULT;
    sceneSettings.closestHitCallback = nullptr;
    sceneSettings.anyHitCallback = nullptr;
    sceneSettings.batchedClosestHitCallback = nullptr;
    sceneSettings.batchedAnyHitCallback = nullptr;
    sceneSettings.userData = nullptr;
    sceneSettings.embreeDevice = nullptr;
    sceneSettings.radeonRaysDevice = radeonDevice;

    IPLerror result = iplSceneCreate(context, &sceneSettings, &m_scene);
    if (result != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create scene, error: %d", static_cast<int>(result));
        return false;
    }

    Msg("* SOUND: SteamAudio: Scene created successfully");

    // Create simulator
    IPLSimulationSettings simulationSettings{};
    simulationSettings.flags = IPL_SIMULATIONFLAGS_DIRECT;
    simulationSettings.sceneType = useGPU ? IPL_SCENETYPE_RADEONRAYS : IPL_SCENETYPE_DEFAULT;
    simulationSettings.reflectionType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
    simulationSettings.maxNumOcclusionSamples = 128;
    simulationSettings.maxNumRays = 128;
    simulationSettings.numDiffuseSamples = 4096;
    simulationSettings.maxDuration = 2.0f;
    simulationSettings.maxOrder = 1;
    simulationSettings.maxNumSources = 32;
    simulationSettings.numThreads = 5;
    simulationSettings.rayBatchSize = 32;
    simulationSettings.numVisSamples = 0;
    simulationSettings.samplingRate = audioSettings.samplingRate;
    simulationSettings.frameSize = audioSettings.frameSize;
    simulationSettings.openCLDevice = openCLDevice;
    simulationSettings.radeonRaysDevice = radeonDevice;
    simulationSettings.tanDevice = nullptr;

    result = iplSimulatorCreate(context, &simulationSettings, &m_simulator);
    if (result != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create simulator, error: %d", static_cast<int>(result));
        iplSceneRelease(&m_scene);
        m_scene = nullptr;
        return false;
    }

    Msg("* SOUND: SteamAudio: Simulator created successfully (%s)", useGPU ? "GPU" : "CPU");

    // Link simulator to scene
    iplSimulatorSetScene(m_simulator, m_scene);
    iplSimulatorCommit(m_simulator);

    return true;
}

bool CSteamAudioScene::LoadGeometry(CDB::MODEL* model, const Fbox& aabb, CSteamAudioMaterials* materialDatabase, bool bakePaths)
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

    // Build material array
    xr_vector<IPLMaterial> materials;
    materials.reserve(GMLib.CountMaterial());

    // If we have a material database, use it to override GMLib materials
    if (materialDatabase)
    {
        Msg("* SOUND: SteamAudio: Using material database (%d materials)", materialDatabase->GetMaterialCount());

        for (const SGameMtl* gameMaterial : GMLib.Materials())
        {
            // Try to get material from database by name
            IPLMaterial material = materialDatabase->GetMaterial(gameMaterial->m_Name.c_str());

            // If not found in database, fall back to GMLib acoustics (if available)
            if (!materialDatabase->HasMaterial(gameMaterial->m_Name.c_str()))
            {
                // Fall back to GMLib's acoustics property
                material = reinterpret_cast<const IPLMaterial&>(gameMaterial->Acoustics);
            }

            materials.push_back(material);
        }
    }
    else
    {
        // No material database - use GMLib acoustics properties directly
        Msg("* SOUND: SteamAudio: Using GMLib acoustics properties");

        for (const SGameMtl* gameMaterial : GMLib.Materials())
        {
            materials.emplace_back(reinterpret_cast<const IPLMaterial&>(gameMaterial->Acoustics));
        }
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

bool CSteamAudioScene::QueryDirect(const Fvector& listenerPos, const Fvector& listenerAhead, const Fvector& listenerUp, const Fvector& listenerRight,
                                   const Fvector& sourcePos, DirectQueryResult& result)
{
    if (!IsValid())
        return false;

    if (!m_querySource)
    {
        m_querySource = xr_new<CSteamAudioSource>(m_context, m_simulator, m_audioSettings, nullptr, 1);
        if (!m_querySource || !m_querySource->IsValid())
        {
            xr_delete(m_querySource);
            return false;
        }
    }

    // Prepare source orientation similar to runtime emitters
    Fvector ahead;
    ahead.sub(listenerPos, sourcePos);
    if (ahead.square_magnitude() < EPS_S)
        ahead.set(0.f, 0.f, 1.f);
    else
        ahead.normalize();

    Fvector up = listenerUp;
    if (up.square_magnitude() < EPS_S)
        up.set(0.f, 1.f, 0.f);
    else
        up.normalize();

    Fvector right;
    right.crossproduct(up, ahead);
    if (right.square_magnitude() < EPS_S)
        right.set(1.f, 0.f, 0.f);
    else
        right.normalize();

    up.crossproduct(ahead, right);
    up.normalize();
    right.normalize();

    m_querySource->UpdateInputs(sourcePos, ahead, up, right);

    iplSimulatorCommit(m_simulator);
    iplSimulatorRunDirect(m_simulator);

    if (!m_querySource->UpdateDirectMetricsOnly())
        return false;

    const auto& metrics = m_querySource->GetDirectMetrics();
    if (!metrics.valid)
        return false;

    std::memcpy(result.occlusion, metrics.occlusion, sizeof(result.occlusion));
    std::memcpy(result.transmission, metrics.transmission, sizeof(result.transmission));
    result.distanceAttenuation = metrics.distanceAttenuation;
    result.valid = true;
    return true;
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
