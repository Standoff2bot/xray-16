#include "stdafx.h"
#include "SteamAudioContext.h"
#include "xrCore/xrCore.h"

#include <cfloat>

namespace SteamAudio
{

CSteamAudioContext::~CSteamAudioContext()
{
    Shutdown();
}

bool CSteamAudioContext::Initialize(int sampleRate, int frameSize, bool enableValidation, bool requestGPU)
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

    m_requestGPU = requestGPU;
    if (m_requestGPU)
    {
        m_gpuActive = InitializeGPU();
        if (m_gpuActive)
        {
            Msg("* SOUND: SteamAudio: GPU acceleration enabled");
        }
        else
        {
            Msg("~ SOUND: SteamAudio: GPU acceleration requested but unavailable, falling back to CPU");
        }
    }
    else
    {
        m_gpuActive = false;
    }

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
    ShutdownGPU();

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

bool CSteamAudioContext::InitializeGPU()
{
    if (!m_context)
        return false;

    IPLOpenCLDeviceSettings deviceSettings{};
    deviceSettings.type = IPL_OPENCLDEVICETYPE_GPU;
    deviceSettings.numCUsToReserve = 0;
    deviceSettings.fractionCUsForIRUpdate = 1.0f;
    deviceSettings.requiresTAN = IPL_FALSE;

    const IPLerror listResult = iplOpenCLDeviceListCreate(m_context, &deviceSettings, &m_openCLDeviceList);
    if (listResult != IPL_STATUS_SUCCESS)
    {
        Msg("~ SOUND: SteamAudio: Failed to enumerate OpenCL devices, error: %d", static_cast<int>(listResult));
        ShutdownGPU();
        return false;
    }

    const IPLint32 deviceCount = iplOpenCLDeviceListGetNumDevices(m_openCLDeviceList);
    if (deviceCount <= 0)
    {
        Msg("~ SOUND: SteamAudio: No compatible OpenCL GPU devices found");
        ShutdownGPU();
        return false;
    }

    IPLint32 chosenIndex = 0;
    IPLfloat32 bestScore = -FLT_MAX;
    for (IPLint32 i = 0; i < deviceCount; ++i)
    {
        IPLOpenCLDeviceDesc desc{};
        iplOpenCLDeviceListGetDeviceDesc(m_openCLDeviceList, i, &desc);
        if (desc.type != IPL_OPENCLDEVICETYPE_GPU)
            continue;
        const IPLfloat32 score = desc.perfScore;
        if (score > bestScore)
        {
            bestScore = score;
            chosenIndex = i;
        }
    }

    IPLOpenCLDeviceDesc chosenDesc{};
    iplOpenCLDeviceListGetDeviceDesc(m_openCLDeviceList, chosenIndex, &chosenDesc);

    const IPLerror deviceResult = iplOpenCLDeviceCreate(m_context, m_openCLDeviceList, chosenIndex, &m_openCLDevice);
    if (deviceResult != IPL_STATUS_SUCCESS)
    {
        Msg("~ SOUND: SteamAudio: Failed to create OpenCL device, error: %d", static_cast<int>(deviceResult));
        ShutdownGPU();
        return false;
    }

    const IPLerror rrResult = iplRadeonRaysDeviceCreate(m_openCLDevice, nullptr, &m_radeonRaysDevice);
    if (rrResult != IPL_STATUS_SUCCESS)
    {
        Msg("~ SOUND: SteamAudio: Failed to create Radeon Rays device, error: %d", static_cast<int>(rrResult));
        ShutdownGPU();
        return false;
    }

    Msg("* SOUND: SteamAudio: Selected GPU '%s' on platform '%s'", chosenDesc.deviceName, chosenDesc.platformName);
    return true;
}

void CSteamAudioContext::ShutdownGPU()
{
    if (m_radeonRaysDevice)
    {
        iplRadeonRaysDeviceRelease(&m_radeonRaysDevice);
        m_radeonRaysDevice = nullptr;
        Msg("* SOUND: SteamAudio: Radeon Rays device released");
    }

    if (m_openCLDevice)
    {
        iplOpenCLDeviceRelease(&m_openCLDevice);
        m_openCLDevice = nullptr;
        Msg("* SOUND: SteamAudio: OpenCL device released");
    }

    if (m_openCLDeviceList)
    {
        iplOpenCLDeviceListRelease(&m_openCLDeviceList);
        m_openCLDeviceList = nullptr;
    }

    m_gpuActive = false;
}

} // namespace SteamAudio
