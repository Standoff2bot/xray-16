#include "stdafx.h"
#include "SteamAudioSource.h"
#include "xrCore/xrCore.h"

#include <cstring>

namespace SteamAudio
{

CSteamAudioSource::CSteamAudioSource(IPLContext context, IPLSimulator simulator,
                                     const IPLAudioSettings& audioSettings, IPLHRTF hrtf, int numChannels)
    : m_context(context)
    , m_simulator(simulator)
    , m_hrtf(hrtf)
    , m_audioSettings(audioSettings)
    , m_numChannels(numChannels)
{
    if (!m_context || !m_simulator)
    {
        Msg("! SOUND: SteamAudio: Cannot create source - invalid context or simulator");
        return;
    }

    // Create the source (position/simulation state)
    IPLSourceSettings sourceSettings{ IPL_SIMULATIONFLAGS_DIRECT };
    IPLerror result = iplSourceCreate(m_simulator, &sourceSettings, &m_source);
    if (result != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create source, error: %d", static_cast<int>(result));
        return;
    }

    // Add source to simulator
    iplSourceAdd(m_source, m_simulator);

    // Create direct effect (distance attenuation, occlusion, transmission)
    IPLDirectEffectSettings directSettings{ m_numChannels };
    result = iplDirectEffectCreate(m_context, &m_audioSettings, &directSettings, &m_directEffect);
    if (result != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create direct effect, error: %d", static_cast<int>(result));
    }

    // Create binaural effect (HRTF-based 3D positioning)
    if (m_hrtf)
    {
        IPLBinauralEffectSettings binauralSettings{ m_hrtf };
        result = iplBinauralEffectCreate(m_context, &m_audioSettings, &binauralSettings, &m_binauralEffect);
        if (result != IPL_STATUS_SUCCESS)
        {
            Msg("! SOUND: SteamAudio: Failed to create binaural effect, error: %d", static_cast<int>(result));
        }
    }

    // Create reflection effect (reverb)
    IPLReflectionEffectSettings reflectionSettings{
        IPL_REFLECTIONEFFECTTYPE_CONVOLUTION,
        m_audioSettings.frameSize * 2,
        4
    };
    result = iplReflectionEffectCreate(m_context, &m_audioSettings, &reflectionSettings, &m_reflectionEffect);
    if (result != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create reflection effect, error: %d", static_cast<int>(result));
    }

    // Create path effect (indirect sound paths)
    IPLPathEffectSettings pathSettings{ 1, IPL_TRUE, {}, m_hrtf };
    result = iplPathEffectCreate(m_context, &m_audioSettings, &pathSettings, &m_pathEffect);
    if (result != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create path effect, error: %d", static_cast<int>(result));
    }

    // Allocate audio buffers
    iplAudioBufferAllocate(m_context, m_numChannels, m_audioSettings.frameSize, &m_inputBuffer);
    iplAudioBufferAllocate(m_context, m_numChannels, m_audioSettings.frameSize, &m_outputBuffer);
    iplAudioBufferAllocate(m_context, 2, m_audioSettings.frameSize, &m_binauralOutputBuffer); // Stereo output for HRTF
}

CSteamAudioSource::~CSteamAudioSource()
{
    // Free audio buffers
    if (m_binauralOutputBuffer.data)
        iplAudioBufferFree(m_context, &m_binauralOutputBuffer);
    if (m_outputBuffer.data)
        iplAudioBufferFree(m_context, &m_outputBuffer);
    if (m_inputBuffer.data)
        iplAudioBufferFree(m_context, &m_inputBuffer);

    // Release effects (reference-counted)
    if (m_pathEffect)
        iplPathEffectRelease(&m_pathEffect);
    if (m_reflectionEffect)
        iplReflectionEffectRelease(&m_reflectionEffect);
    if (m_binauralEffect)
        iplBinauralEffectRelease(&m_binauralEffect);
    if (m_directEffect)
        iplDirectEffectRelease(&m_directEffect);

    // Remove source from simulator and release
    if (m_source)
    {
        iplSourceRemove(m_source, m_simulator);
        iplSourceRelease(&m_source);
    }
}

void CSteamAudioSource::UpdateInputs(const Fvector& position, const Fvector& ahead, const Fvector& up, const Fvector& right)
{
    if (!m_source)
        return;

    // Setup source inputs (position, orientation, directivity)
    IPLSimulationInputs inputs{};
    inputs.flags = static_cast<IPLSimulationFlags>(
        IPL_SIMULATIONFLAGS_DIRECT
        // Can add IPL_SIMULATIONFLAGS_REFLECTIONS, IPL_SIMULATIONFLAGS_PATHING later
    );
    inputs.directFlags = static_cast<IPLDirectSimulationFlags>(0);

    // Source orientation in Steam Audio coordinate system
    m_sourcePosition = reinterpret_cast<const IPLVector3&>(position);
    inputs.source.origin = m_sourcePosition;
    inputs.source.ahead = reinterpret_cast<const IPLVector3&>(ahead);
    inputs.source.up = reinterpret_cast<const IPLVector3&>(up);
    inputs.source.right = reinterpret_cast<const IPLVector3&>(right);

    // Directivity (omnidirectional by default)
    inputs.directivity.dipoleWeight = 0.0f;
    inputs.directivity.dipolePower = 0.0f;

    // Distance attenuation (can be customized)
    inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT;

    // Air absorption
    inputs.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;

    // Occlusion (will be computed by simulator)
    inputs.occlusionType = IPL_OCCLUSIONTYPE_RAYCAST;
    inputs.occlusionRadius = 1.0f; // Radius for partial occlusion
    inputs.numOcclusionSamples = 32;

    // Transmission (sound through walls)
    // Note: transmissionType doesn't exist in IPLSimulationInputs, only in IPLDirectEffectParams

    // Initialize optional fields
    inputs.reverbScale[0] = inputs.reverbScale[1] = inputs.reverbScale[2] = 1.0f;
    inputs.hybridReverbTransitionTime = 1.0f;
    inputs.hybridReverbOverlapPercent = 0.25f;
    inputs.baked = IPL_FALSE;
    inputs.bakedDataIdentifier = {};
    inputs.pathingProbes = nullptr;
    inputs.visRadius = 1.0f;
    inputs.visThreshold = 0.1f;
    inputs.visRange = 1000.0f;
    inputs.pathingOrder = 0;
    inputs.enableValidation = IPL_FALSE;
    inputs.findAlternatePaths = IPL_FALSE;
    inputs.numTransmissionRays = 16;
    inputs.deviationModel = nullptr;

    // Update the source
    iplSourceSetInputs(m_source, inputs.flags, &inputs);
}

void CSteamAudioSource::StoreDirectMetrics(const IPLDirectEffectParams& params)
{
    m_directMetrics.valid = true;
    m_directMetrics.distanceAttenuation = params.distanceAttenuation;

    // Old Steam Audio API: occlusion is single float, replicate to all bands
    m_directMetrics.occlusion[0] = params.occlusion;
    m_directMetrics.occlusion[1] = params.occlusion;
    m_directMetrics.occlusion[2] = params.occlusion;

    std::memcpy(m_directMetrics.transmission, params.transmission, sizeof(m_directMetrics.transmission));
}

void CSteamAudioSource::ApplyDirectEffect(float* inputBuffer, float* outputBuffer, int numSamples)
{
    m_directMetrics.valid = false;

    if (!m_directEffect || !m_source)
    {
        // No processing available, just copy input to output
        if (inputBuffer != outputBuffer)
            memcpy(outputBuffer, inputBuffer, numSamples * m_numChannels * sizeof(float));
        return;
    }

    // Get simulation outputs from the source
    IPLSimulationOutputs outputs{};
    outputs.direct.flags = static_cast<IPLDirectEffectFlags>(
        IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION |
        IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION |
        IPL_DIRECTEFFECTFLAGS_APPLYDIRECTIVITY |
        IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION |
        IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION
    );
    iplSourceGetOutputs(m_source, IPL_SIMULATIONFLAGS_DIRECT, &outputs);

    // Cache direct metrics for other systems (occlusion / transmission)
    StoreDirectMetrics(outputs.direct);

    // Deinterleave input buffer
    iplAudioBufferDeinterleave(m_context, inputBuffer, &m_inputBuffer);

    // Apply direct effect
    iplDirectEffectApply(m_directEffect, &outputs.direct, &m_inputBuffer, &m_outputBuffer);

    // Interleave output buffer
    iplAudioBufferInterleave(m_context, &m_outputBuffer, outputBuffer);
}

void CSteamAudioSource::ApplyBinauralEffect(float* inputBuffer, float* outputBuffer,
                                           const Fvector& listenerPosition, const Fvector& listenerAhead,
                                           const Fvector& listenerUp, const Fvector& listenerRight)
{
    if (!m_binauralEffect || !m_source)
    {
        // No HRTF processing available, just copy input to output
        if (inputBuffer != outputBuffer)
            memcpy(outputBuffer, inputBuffer, m_audioSettings.frameSize * 2 * sizeof(float)); // Stereo output
        return;
    }

    // Calculate direction from listener to source
    IPLVector3 direction;
    direction.x = m_sourcePosition.x - listenerPosition.x;
    direction.y = m_sourcePosition.y - listenerPosition.y;
    direction.z = m_sourcePosition.z - listenerPosition.z;

    // Normalize direction
    const float length = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (length > 0.0001f)
    {
        direction.x /= length;
        direction.y /= length;
        direction.z /= length;
    }
    else
    {
        // Source and listener at same position, use forward direction
        direction = reinterpret_cast<const IPLVector3&>(listenerAhead);
    }

    // Setup binaural effect parameters
    IPLBinauralEffectParams binauralParams{};
    binauralParams.direction = direction;
    binauralParams.interpolation = IPL_HRTFINTERPOLATION_NEAREST; // or IPL_HRTFINTERPOLATION_BILINEAR for quality
    binauralParams.spatialBlend = 1.0f; // Full spatial audio
    binauralParams.hrtf = m_hrtf;

    // Setup peaking EQ (for HRTF-based distance attenuation)
    binauralParams.peakDelays = nullptr;

    // Deinterleave input if not already done
    iplAudioBufferDeinterleave(m_context, inputBuffer, &m_inputBuffer);

    // Apply binaural effect
    iplBinauralEffectApply(m_binauralEffect, &binauralParams, &m_inputBuffer, &m_binauralOutputBuffer);

    // Interleave stereo output
    iplAudioBufferInterleave(m_context, &m_binauralOutputBuffer, outputBuffer);
}

bool CSteamAudioSource::UpdateDirectMetricsOnly()
{
    m_directMetrics.valid = false;

    if (!m_source)
        return false;

    IPLSimulationOutputs outputs{};
    outputs.direct.flags = static_cast<IPLDirectEffectFlags>(
        IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION |
        IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION |
        IPL_DIRECTEFFECTFLAGS_APPLYDIRECTIVITY |
        IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION |
        IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION
    );
    iplSourceGetOutputs(m_source, IPL_SIMULATIONFLAGS_DIRECT, &outputs);
    StoreDirectMetrics(outputs.direct);
    return m_directMetrics.valid;
}

} // namespace SteamAudio
