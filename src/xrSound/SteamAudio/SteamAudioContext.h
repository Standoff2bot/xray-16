#pragma once

#include "xrSound/Sound.h"
#include <phonon.h>

namespace SteamAudio
{
/**
 * @brief RAII wrapper for global Steam Audio context and HRTF
 *
 * Lifecycle: Application startup → shutdown
 * Thread-safety: Initialize on main thread, accessors are thread-safe for reading
 *
 * Manages:
 * - IPLContext (global Steam Audio state)
 * - IPLHRTF (Head-Related Transfer Function for binaural audio)
 * - IPLAudioSettings (sample rate and frame size)
 */
class CSteamAudioContext
{
public:
    CSteamAudioContext() = default;
    ~CSteamAudioContext();

    // Non-copyable
    CSteamAudioContext(const CSteamAudioContext&) = delete;
    CSteamAudioContext& operator=(const CSteamAudioContext&) = delete;

    /**
     * @brief Initialize Steam Audio context and HRTF
     * @param sampleRate Audio sample rate (typically 48000 or 44100)
     * @param frameSize Frame size in samples (512-2048 recommended, affects latency)
     * @param enableValidation Enable validation layer for debugging
     * @return true if initialization succeeded
     */
    bool Initialize(int sampleRate, int frameSize, bool enableValidation = false);

    /**
     * @brief Shutdown and release all Steam Audio resources
     */
    void Shutdown();

    /**
     * @brief Check if Steam Audio is initialized and ready
     */
    [[nodiscard]] bool IsInitialized() const { return m_context != nullptr; }

    // Accessors
    [[nodiscard]] IPLContext GetContext() const { return m_context; }
    [[nodiscard]] IPLHRTF GetHRTF() const { return m_hrtf; }
    [[nodiscard]] const IPLAudioSettings& GetAudioSettings() const { return m_audioSettings; }
    [[nodiscard]] int GetSampleRate() const { return m_audioSettings.samplingRate; }
    [[nodiscard]] int GetFrameSize() const { return m_audioSettings.frameSize; }

private:
    IPLContext m_context{};
    IPLHRTF m_hrtf{};
    IPLAudioSettings m_audioSettings{};
    IPLSIMDLevel m_simdLevel{ IPL_SIMDLEVEL_SSE2 };
};

} // namespace SteamAudio
