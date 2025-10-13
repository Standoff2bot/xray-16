#include "stdafx.h"
#include "SteamAudioContext.h"
#include "xrCore/xrCore.h"

namespace SteamAudio
{

CSteamAudioContext::~CSteamAudioContext()
{
    Shutdown();
}

bool CSteamAudioContext::Initialize(int sampleRate, int frameSize, bool enableValidation)
{
    if (IsInitialized())
    {
        Msg("! SOUND: SteamAudio: Context already initialized");
        return false;
    }

    // Detect best available SIMD level
    m_simdLevel = IPL_SIMDLEVEL_SSE2;
    if (CPU::HasAVX512F)
        m_simdLevel = IPL_SIMDLEVEL_AVX512;
    else if (CPU::HasAVX2)
        m_simdLevel = IPL_SIMDLEVEL_AVX2;
    else if (CPU::HasAVX)
        m_simdLevel = IPL_SIMDLEVEL_AVX;
    else if (CPU::HasSSE42)
        m_simdLevel = IPL_SIMDLEVEL_SSE4;

    Msg("* SOUND: SteamAudio: Using SIMD level: %d", static_cast<int>(m_simdLevel));

    // Setup context flags
    const IPLContextFlags flags = enableValidation ? IPL_CONTEXTFLAGS_VALIDATION : IPLContextFlags{};

    // Create context settings with custom memory allocator and logging
    IPLContextSettings contextSettings{
        STEAMAUDIO_VERSION,
        // Logging callback
        [](IPLLogLevel level, const char* message)
        {
            // Filter out known false-positive warnings
            if (0 == xr_strcmp(message, "Warning: setInputs: invalid IPLfloat32: (&inputs->directivity)->dipoleWeight = 0.000000\n"))
                return;
            if (0 == xr_strcmp(message, "Warning: apply: invalid IPLTransmissionType: params->flags = 31\n"))
                return;

            char mark = '\0';
            switch (level)
            {
            case IPL_LOGLEVEL_INFO:    mark = '*'; break;
            case IPL_LOGLEVEL_WARNING: mark = '~'; break;
            case IPL_LOGLEVEL_ERROR:   mark = '!'; break;
            case IPL_LOGLEVEL_DEBUG:   mark = '#'; break;
            }
            Msg("%c SOUND: SteamAudio: %s", mark, message);
        },
        // Memory allocation callback
        [](IPLsize size, IPLsize alignment)
        {
            return Memory.mem_alloc(size, alignment);
        },
        // Memory free callback
        [](void* memoryBlock)
        {
            Memory.mem_free(memoryBlock);
        },
        m_simdLevel,
        flags,
    };

    // Create the context
    const IPLerror contextResult = iplContextCreate(&contextSettings, &m_context);
    if (contextResult != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create context, error: %d", static_cast<int>(contextResult));
        return false;
    }

    Msg("* SOUND: SteamAudio: Context created successfully");

    // Store audio settings
    m_audioSettings.samplingRate = sampleRate;
    m_audioSettings.frameSize = frameSize;

    // Create HRTF
    IPLHRTFSettings hrtfSettings{
        IPL_HRTFTYPE_DEFAULT,
        nullptr, nullptr, 0,
        1.0f, IPL_HRTFNORMTYPE_NONE
    };

    const IPLerror hrtfResult = iplHRTFCreate(m_context, &m_audioSettings, &hrtfSettings, &m_hrtf);
    if (hrtfResult != IPL_STATUS_SUCCESS)
    {
        Msg("! SOUND: SteamAudio: Failed to create HRTF, error: %d", static_cast<int>(hrtfResult));
        Shutdown();
        return false;
    }

    Msg("* SOUND: SteamAudio: HRTF created successfully (sample rate: %d, frame size: %d)",
        sampleRate, frameSize);

    return true;
}

void CSteamAudioContext::Shutdown()
{
    if (m_hrtf)
    {
        iplHRTFRelease(&m_hrtf);
        m_hrtf = nullptr;
        Msg("* SOUND: SteamAudio: HRTF released");
    }

    if (m_context)
    {
        iplContextRelease(&m_context);
        m_context = nullptr;
        Msg("* SOUND: SteamAudio: Context released");
    }
}

} // namespace SteamAudio
