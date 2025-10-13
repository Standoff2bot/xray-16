#include "stdafx.h"

#include "Include/xrAPI/xrAPI.h"
#include "Common/LevelStructure.hpp"
#include "SoundRender_Core.h"
#include "SoundRender_Source.h"
#include "SoundRender_Emitter.h"

#ifdef USE_STEAMAUDIO
#include "SteamAudio/SteamAudioContext.h"
#endif

// XXX: old SDK functionality
//#if defined(XR_PLATFORM_WINDOWS)
//#define OPENAL
//#include <eax/eax.h>
//#endif

XRSOUND_API Flags32 psSoundFlags =
{
    ss_Hardware | ss_EFX
#ifdef USE_STEAMAUDIO
    // Steam Audio enabled by default if compiled in
    | ss_UseSteamAudio | ss_SteamAudio_HRTF
    // Note: ss_SteamAudio_BakePaths disabled by default to avoid blocking level loads
#endif
};

XRSOUND_API int psSoundTargets = 32;
XRSOUND_API float psSoundOcclusionScale = 0.5f;
XRSOUND_API float psSoundTimeFactor = 1.0f;
XRSOUND_API float psSoundCull = 0.01f;
XRSOUND_API float psSoundRolloff = 0.75f;
XRSOUND_API u32 psSoundModel = 0;
XRSOUND_API float psSoundVEffects = 1.0f;
XRSOUND_API float psSoundVFactor = 1.0f;

XRSOUND_API float psSoundVMusic = 1.f;
XRSOUND_API int psSoundCacheSizeMB = 32;

CSoundRender_Core* SoundRender = nullptr;

CSoundRender_Core::CSoundRender_Core(CSoundManager& p)
    : Parent(p)
{
    bPresent = false;
    s_emitters_u = 0;
    e_current.set_identity();
    e_target.set_identity();
    bReady = false;
    isLocked = false;
    fTimer_Value = Timer.GetElapsed_sec();
    fTimer_Delta = 0.0f;
    fTimerPersistent_Value = TimerPersistent.GetElapsed_sec();
    fTimerPersistent_Delta = 0.0f;
}

#pragma optimize( "", off )
void CSoundRender_Core::_initialize()
{
    Timer.Start();
    TimerPersistent.Start();

    bPresent = true;

#ifdef USE_STEAMAUDIO
    // Initialize Steam Audio with proper settings
    // CRITICAL FIX: Frame size changed from 19200 (400ms latency!) to 1024 (~21ms)
    if (supports_float_pcm && psSoundFlags.test(ss_UseFloat32) && psSoundFlags.test(ss_EFX))
    {
        const bool enableValidation = strstr(Core.Params, "-steamaudio_validate") != nullptr;

        m_steamAudioContext = xr_new<SteamAudio::CSteamAudioContext>();

        constexpr int sampleRate = 48000;
        constexpr int frameSize = 1024;  // FIX: Was 19200 (400ms!), now 1024 (~21ms @ 48kHz)

        if (!m_steamAudioContext->Initialize(sampleRate, frameSize, enableValidation))
        {
            Msg("! SOUND: SteamAudio: Failed to initialize, disabling Steam Audio");
            xr_delete(m_steamAudioContext);
        }
        else
        {
            Msg("* SOUND: SteamAudio: Initialized successfully (sample rate: %d, frame size: %d, latency: %.1fms)",
                sampleRate, frameSize, (frameSize * 1000.0f) / sampleRate);
        }
    }
#endif
    bReady = true;
}
#pragma optimize( "", on )

void CSoundRender_Core::_clear()
{
    bReady = false;

#ifdef USE_STEAMAUDIO
    // RAII: Wrapper automatically releases Steam Audio resources
    xr_delete(m_steamAudioContext);
#endif

    // remove sources
    for (auto& kv : s_sources)
    {
        xr_delete(kv.second);
    }
    s_sources.clear();
}

ISoundScene* CSoundRender_Core::create_scene()
{
    return m_scenes.emplace_back(xr_new<CSoundRender_Scene>());
}

void CSoundRender_Core::destroy_scene(ISoundScene*& sound_scene)
{
    m_scenes.erase(std::remove(m_scenes.begin(), m_scenes.end(), sound_scene), m_scenes.end());
    xr_delete(sound_scene);
}

void CSoundRender_Core::stop_emitters()
{
    for (const auto& scene : m_scenes)
        scene->stop_emitters();
}

int CSoundRender_Core::pause_emitters(bool pauseState)
{
    int cnt = 0;
    for (const auto& scene : m_scenes)
        cnt += scene->pause_emitters(pauseState);
    return cnt;
}

void CSoundRender_Core::_restart()
{
    env_apply();
}

CSound* CSoundRender_Core::create(pcstr fName, esound_type sound_type, u32 game_type)
{
    if (!bPresent)
        return nullptr;

    string_path fn;
    xr_strcpy(fn, fName);
    if (strext(fn))
        *strext(fn) = 0;

    CSoundRender_Source* handle = i_create_source(fn);
    if (!handle)
        return nullptr;

    auto* snd = xr_new<CSound>(handle);

    snd->g_type = game_type;
    if (game_type == sg_SourceType)
        snd->g_type = snd->handle->game_type();

    snd->s_type = sound_type;

    snd->dwBytesTotal = snd->handle->bytes_total();
    snd->fTimeTotal = snd->handle->length_sec();

    return snd;
}

void CSoundRender_Core::attach_tail(CSound& snd, pcstr fName)
{
    if (!bPresent)
        return;
    string_path fn;
    xr_strcpy(fn, fName);
    if (strext(fn))
        *strext(fn) = 0;
    if (!snd.fn_attached[0].empty() && !snd.fn_attached[1].empty())
    {
#ifndef MASTER_GOLD
        Msg("! 2 file already in queue [%s][%s]", snd.fn_attached[0].c_str(), snd.fn_attached[1].c_str());
#endif
        return;
    }

    const u32 idx = snd.fn_attached[0].empty() ? 0 : 1;

    snd.fn_attached[idx] = fn;

    CSoundRender_Source* s = i_create_source(fn);
    snd.dwBytesTotal += s->bytes_total();
    snd.fTimeTotal += s->length_sec();
    if (snd.feedback)
        ((CSoundRender_Emitter*)snd.feedback)->fTimeToStop += s->length_sec();

    i_destroy_source(s);
}

void CSoundRender_Core::destroy(CSound& S)
{
    if (auto* emitter = (CSoundRender_Emitter*)S.feedback)
    {
        emitter->stop(false);
        VERIFY(S.feedback == nullptr);
    }
    i_destroy_source((CSoundRender_Source*)S.handle);
    S.handle = nullptr;
}

void CSoundRender_Core::env_apply()
{
    /*
    // Force all sounds to change their environment
    // (set their positions to signal changes in environment)
    for (u32 it = 0; it < s_emitters.size(); it++)
    {
        CSoundRender_Emitter* pEmitter = s_emitters[it];
        const CSound_params* pParams = pEmitter->get_params();
        pEmitter->set_position(pParams->position);
    }
    */
    bListenerMoved = true;
}

void CSoundRender_Core::update_listener(const Fvector& P, const Fvector& D, const Fvector& N, const Fvector& R, float dt)
{
    if (!Listener.position.similar(P))
    {
        Listener.position = P;
        bListenerMoved = true;
    }
    Listener.orientation[0] = D;
    Listener.orientation[1] = N;
    Listener.orientation[2] = R;

    if (!psSoundFlags.test(ss_EFX) || !m_effects)
        return;

    // Update effects
    if (bListenerMoved)
    {
        bListenerMoved = false;
        e_target = *(CSoundRender_Environment*)DefaultSoundScene->get_environment(P);
    }

    e_current.lerp(e_current, e_target, fTimer_Delta);

    m_effects->set_listener(e_current);
    m_effects->commit();

#ifdef USE_STEAMAUDIO
    // Listener updates now handled by individual scenes via CSteamAudioScene wrapper
    // See CSoundRender_Scene::update()
#endif
}

void CSoundRender_Core::refresh_sources()
{
    stop_emitters();

    for (const auto& kv : s_sources)
    {
        CSoundRender_Source* s = kv.second;
        s->unload();
        s->load(s->file_name());
    }
}
