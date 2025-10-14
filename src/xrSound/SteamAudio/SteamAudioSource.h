#pragma once

#include "xrSound/Sound.h"
#include <phonon.h>

namespace SteamAudio
{

/**
 * @brief RAII wrapper for per-emitter Steam Audio objects
 *
 * Lifecycle: Emitter creation → destruction (NOT playback start/stop!)
 * Thread-safety: Must be accessed only from audio thread
 *
 * Manages:
 * - IPLSource (position and simulation state)
 * - IPLDirectEffect (distance, occlusion, transmission)
 * - IPLBinauralEffect (HRTF-based 3D positioning)
 * - IPLReflectionEffect (reverb and reflections)
 * - IPLPathEffect (indirect sound paths)
 * - Audio buffers for processing
 *
 * Key principle: Create once, reuse by updating parameters, NOT by recreating!
 */
class CSteamAudioSource
{
public:
    /**
     * @brief Create Steam Audio source and effects
     * @param context Steam Audio context
     * @param simulator Scene simulator to add source to
     * @param audioSettings Audio format settings
     * @param hrtf HRTF for binaural effect
     * @param numChannels Number of audio channels (1 for mono, 2 for stereo)
     */
    CSteamAudioSource(IPLContext context, IPLSimulator simulator,
                      const IPLAudioSettings& audioSettings, IPLHRTF hrtf, int numChannels);
    ~CSteamAudioSource();

    // Non-copyable
    CSteamAudioSource(const CSteamAudioSource&) = delete;
    CSteamAudioSource& operator=(const CSteamAudioSource&) = delete;

    /**
     * @brief Update source position and direction (call every frame when playing)
     * @param position World-space position
     * @param ahead Forward direction (normalized)
     * @param up Up direction (normalized)
     * @param right Right direction (normalized)
     */
    void UpdateInputs(const Fvector& position, const Fvector& ahead, const Fvector& up, const Fvector& right);

    /**
     * @brief Apply direct sound effect (distance, occlusion, transmission)
     * @param inputBuffer Interleaved audio data (will be deinterleaved internally)
     * @param outputBuffer Output buffer for processed audio
     * @param numSamples Number of samples per channel
     */
    void ApplyDirectEffect(float* inputBuffer, float* outputBuffer, int numSamples);

    /**
     * @brief Apply binaural HRTF effect for 3D positioning
     * @param inputBuffer Input audio data (deinterleaved)
     * @param outputBuffer Output buffer for processed audio
     * @param listenerPosition Listener position in world space
     * @param listenerAhead Listener forward direction
     * @param listenerUp Listener up direction
     * @param listenerRight Listener right direction
     */
    void ApplyBinauralEffect(float* inputBuffer, float* outputBuffer,
                            const Fvector& listenerPosition, const Fvector& listenerAhead,
                            const Fvector& listenerUp, const Fvector& listenerRight);

    /**
     * @brief Check if this source is initialized and ready
     */
    [[nodiscard]] bool IsValid() const { return m_source != nullptr; }

    /**
     * @brief Get the underlying IPLSource handle
     */
    [[nodiscard]] IPLSource GetSource() const { return m_source; }

    struct DirectMetrics
    {
        float occlusion[3]{}; // Replicated to all bands (old API has single value)
        float transmission[3]{};
        float distanceAttenuation{ 1.0f };
        bool valid{ false };
    };

    [[nodiscard]] const DirectMetrics& GetDirectMetrics() const { return m_directMetrics; }

    /**
     * @brief Refresh direct metrics without applying audio processing.
     * @return true if metrics were updated successfully.
     */
    bool UpdateDirectMetricsOnly();

private:
    void StoreDirectMetrics(const IPLDirectEffectParams& params);

    // Steam Audio handles
    IPLContext m_context{};
    IPLSimulator m_simulator{};
    IPLSource m_source{};
    IPLHRTF m_hrtf{};

    // Effects (created once, reused)
    IPLDirectEffect m_directEffect{};
    IPLBinauralEffect m_binauralEffect{};
    IPLReflectionEffect m_reflectionEffect{};
    IPLPathEffect m_pathEffect{};

    // Audio buffers for processing
    IPLAudioBuffer m_inputBuffer{};
    IPLAudioBuffer m_outputBuffer{};
    IPLAudioBuffer m_binauralOutputBuffer{};

    // Settings
    IPLAudioSettings m_audioSettings{};
    int m_numChannels{ 1 };

    DirectMetrics m_directMetrics{};

    // Cached source position for direction calculation
    IPLVector3 m_sourcePosition{};
};

} // namespace SteamAudio