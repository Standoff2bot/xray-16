#include "stdafx.h"

#include "SoundRender_Emitter.h"
#include "SoundRender_Core.h"
#include "SoundRender_Scene.h"
#include "SoundRender_Source.h"

#include "xrCore/Threading/TaskManager.hpp"

#ifdef USE_STEAMAUDIO
#include "SteamAudio/SteamAudioSource.h"
#include "SteamAudio/SteamAudioScene.h"
#include "SteamAudio/SteamAudioContext.h"
#endif

extern u32 psSoundModel;
extern float psSoundVEffects;

void CSoundRender_Emitter::set_position(const Fvector& pos)
{
    if (source()->channels_num() == 1
#ifdef USE_STEAMAUDIO
        || m_steamAudioSource
#endif
        )
        p_source.position = pos;
    else
        p_source.position.set(0, 0, 0);

    bMoved = true;

#ifdef USE_STEAMAUDIO
    UpdateSteamAudioInputs();
#endif
}

void CSoundRender_Emitter::set_frequency(float scale)
{
    VERIFY(_valid(scale));
    if (_valid(scale))
        p_source.freq = scale;
}

// Перемотка звука на заданную секунду [rewind snd to target time] --#SM+#--
void CSoundRender_Emitter::set_time(float t)
{
    VERIFY2(get_length_sec() >= t, "set_time: time is bigger than length of sound");
    clamp(t, 0.0f, get_length_sec());
    fTimeToRewind = t;
}

CSoundRender_Emitter::CSoundRender_Emitter(CSoundRender_Scene* s)
    : scene(s),
      priority_scale(1.f),
      smooth_volume(1.f),
      occluder_volume(1.f),
      fade_volume(1.f),
      m_current_state(stStopped),
      bMoved(true),
      marker(0xabababab)
{
    // NOTE: Steam Audio source created lazily in start() when we have audio format info
}

CSoundRender_Emitter::~CSoundRender_Emitter()
{
#ifdef USE_STEAMAUDIO
    // RAII: Wrapper automatically releases Steam Audio resources
    xr_delete(m_steamAudioSource);
#endif

    // try to release dependencies, events, for example
    Event_ReleaseOwner();
    wait_prefill();
}

#ifdef USE_STEAMAUDIO
void CSoundRender_Emitter::UpdateSteamAudioInputs()
{
    if (!m_steamAudioSource || !psSoundFlags.test(ss_EFX) || !psSoundFlags.test(ss_UseSteamAudio) || is_2D())
        return;

    Fvector ahead;
    ahead.sub(SoundRender->listener_position(), p_source.position);
    if (ahead.square_magnitude() < EPS_S)
        ahead.set(0.f, 0.f, 1.f);
    else
        ahead.normalize();

    Fvector up{ 0.f, 1.f, 0.f };
    if (_abs(ahead.dotproduct(up)) > 0.999f)
        up.set(0.f, 0.f, 1.f);

    Fvector right;
    right.crossproduct(up, ahead);
    if (right.square_magnitude() < EPS_S)
        right.set(1.f, 0.f, 0.f);
    else
        right.normalize();

    up.crossproduct(ahead, right);
    up.normalize();
    right.normalize();

    m_steamAudioSource->UpdateInputs(p_source.position, ahead, up, right);
}
#endif

//////////////////////////////////////////////////////////////////////
void CSoundRender_Emitter::Event_ReleaseOwner()
{
    if (!owner_data)
        return;

    auto& events = scene->get_events();

    for (u32 it = 0; it < events.size(); it++)
    {
        if (owner_data == events[it].first)
        {
            events.erase(events.begin() + it);
            it--;
        }
    }
}

void CSoundRender_Emitter::Event_Propagade()
{
    fTimeToPropagade += ::Random.randF(s_f_def_event_pulse - 0.030f, s_f_def_event_pulse + 0.030f);
    if (!owner_data)
        return;
    if (!owner_data->g_type)
        return;
    if (!owner_data->g_object)
        return;
    if (!scene->get_events_handler())
        return;

    VERIFY(_valid(p_source.volume));
    // Calculate range
    const float clip = p_source.max_ai_distance * p_source.volume;
    const float range = std::min(p_source.max_ai_distance, clip);
    if (range < 0.1f)
        return;

    // Inform objects
    scene->get_events().emplace_back(owner_data, range);
}

void CSoundRender_Emitter::switch_to_2D()
{
    b2D = true;
    set_priority(100.f);
}

void CSoundRender_Emitter::switch_to_3D()
{
    b2D = false;
}

u32 CSoundRender_Emitter::play_time()
{
    if (m_current_state == stPlaying || m_current_state == stPlayingLooped || m_current_state == stSimulating ||
        m_current_state == stSimulatingLooped)
        return iFloor((SoundRender->fTimer_Value - fTimeStarted) * 1000.0f);
    return 0;
}

void CSoundRender_Emitter::set_cursor(u32 p)
{
    m_stream_cursor = p;

    if (owner_data._get() && owner_data->fn_attached[0].size())
    {
        u32 bt = ((CSoundRender_Source*)owner_data->handle)->bytes_total();
        if (m_stream_cursor >= m_cur_handle_cursor + bt)
        {
            SoundRender->i_destroy_source((CSoundRender_Source*)owner_data->handle);
            owner_data->handle = SoundRender->i_create_source(owner_data->fn_attached[0].c_str());
            owner_data->fn_attached[0] = owner_data->fn_attached[1];
            owner_data->fn_attached[1] = "";
            m_cur_handle_cursor = get_cursor(true);
        }
    }
}

u32 CSoundRender_Emitter::get_cursor(bool b_absolute) const
{
    if (b_absolute)
        return m_stream_cursor;
    VERIFY(m_stream_cursor - m_cur_handle_cursor >= 0);
    return m_stream_cursor - m_cur_handle_cursor;
}

void CSoundRender_Emitter::move_cursor(int offset)
{
    set_cursor(get_cursor(true) + offset);
}

void CSoundRender_Emitter::fill_data(void* dest, u32 offset, u32 size)
{
    if (!ovf)
        ovf = source()->open();
    source()->decompress(dest, offset, size, ovf);
}

void CSoundRender_Emitter::fill_block(void* ptr, u32 size)
{
    ZoneScoped;

    // Msg			("stream: %10s - [%X]:%d, p=%d, t=%d",*source->fname,ptr,size,position,source->dwBytesTotal);
    u8* dest = (u8*)(ptr);
    const u32 dwBytesTotal = get_bytes_total();

    if ((get_cursor(true) + size) > dwBytesTotal)
    {
        // We are reaching the end of data, what to do?
        switch (m_current_state)
        {
        case stPlaying:
        { // Fill as much data as we can, zeroing remainder
            if (get_cursor(true) >= dwBytesTotal)
            {
                // ??? We requested the block after remainder - just zero
                memset(dest, 0, size);
            }
            else
            {
                // Calculate remainder
                const u32 sz_data = dwBytesTotal - get_cursor(true);
                const u32 sz_zero = (get_cursor(true) + size) - dwBytesTotal;
                VERIFY(size == (sz_data + sz_zero));
                fill_data(dest, get_cursor(false), sz_data);
                memset(dest + sz_data, 0, sz_zero);
            }
            move_cursor(size);
        }
        break;
        case stPlayingLooped:
        {
            u32 hw_position = 0;
            do
            {
                u32 sz_data = dwBytesTotal - get_cursor(true);
                const u32 sz_write = std::min(size - hw_position, sz_data);
                fill_data(dest + hw_position, get_cursor(true), sz_write);
                hw_position += sz_write;
                move_cursor(sz_write);
                set_cursor(get_cursor(true) % dwBytesTotal);
            } while (0 != (size - hw_position));
        }
        break;
        default: FATAL("SOUND: Invalid emitter state"); break;
        }
    }
    else
    {
        const u32 bt_handle = ((CSoundRender_Source*)owner_data->handle)->bytes_total();
        if (get_cursor(true) + size > m_cur_handle_cursor + bt_handle)
        {
            R_ASSERT(owner_data->fn_attached[0].size());

            u32 rem = 0;
            if ((m_cur_handle_cursor + bt_handle) > get_cursor(true))
            {
                rem = (m_cur_handle_cursor + bt_handle) - get_cursor(true);

#ifdef DEBUG
                Msg("reminder from prev source %d", rem);
#endif // #ifdef DEBUG
                fill_data(dest, get_cursor(false), rem);
                move_cursor(rem);
            }
#ifdef DEBUG
            Msg("recurce from next source %d", size - rem);
#endif // #ifdef DEBUG
            fill_block(dest + rem, size - rem);
        }
        else
        {
            // Everything OK, just stream
            fill_data(dest, get_cursor(false), size);
            move_cursor(size);
        }
    }
}

std::pair<u8*, size_t> CSoundRender_Emitter::obtain_block()
{
    wait_prefill();
    const std::pair result = { temp_buf[current_block].data(), temp_buf[current_block].size() };
    ++current_block;
    if (current_block >= sdef_target_count_prefill)
        current_block = 0;
    --filled_blocks;
#ifdef USE_STEAMAUDIO
    // Apply Steam Audio direct effect (occlusion, distance, transmission) using RAII wrapper
    const auto data_info = source()->data_info();
    const bool isFloatFormat = data_info.format == SoundFormat::Float32;
    const bool allowSteamAudio = psSoundFlags.test(ss_EFX) && psSoundFlags.test(ss_UseSteamAudio) && m_steamAudioSource && isFloatFormat && !is_2D();
    const int numSamples = (data_info.channels && isFloatFormat) ? int(result.second / (data_info.channels * sizeof(float))) : 0;
    float* directBuffer = reinterpret_cast<float*>(result.first);

    if (allowSteamAudio && numSamples > 0)
    {
        // Wrapper handles deinterleaving, effect application, and reinterleaving
        m_steamAudioSource->ApplyDirectEffect(directBuffer, directBuffer, numSamples);
    }

    const bool monoFloatSource = isFloatFormat && data_info.channels == 1;
    const bool wantsStereoOutput = m_steamAudioStereoOutput && monoFloatSource && !is_2D();
    const bool useHRTF = allowSteamAudio && psSoundFlags.test(ss_SteamAudio_HRTF);

    if (wantsStereoOutput)
    {
        if (numSamples <= 0)
            return result;

        const size_t stereoSamples = static_cast<size_t>(numSamples) * 2;
        m_steamAudioBinauralBuffer.resize(stereoSamples);
        float* stereoOut = m_steamAudioBinauralBuffer.data();

        if (useHRTF)
        {
            const auto& listener = SoundRender->listener_params();
            m_steamAudioSource->ApplyBinauralEffect(
                directBuffer,
                stereoOut,
                listener.position,
                listener.orientation[0],
                listener.orientation[1],
                listener.orientation[2]);
        }
        else
        {
            // Duplicate mono channel into stereo
            for (int i = 0; i < numSamples; ++i)
            {
                const float sample = directBuffer[i];
                stereoOut[(i << 1) + 0] = sample;
                stereoOut[(i << 1) + 1] = sample;
            }
        }

        return {
            reinterpret_cast<u8*>(stereoOut),
            stereoSamples * sizeof(float)
        };
    }
#endif
    return result;
}

void CSoundRender_Emitter::fill_all_blocks()
{
    current_block = 0;
    for (size_t i = 0; i < sdef_target_count_prefill; ++i)
        fill_block(temp_buf[i].data(), temp_buf[i].size());
    filled_blocks = sdef_target_count_prefill;
}

void CSoundRender_Emitter::dispatch_prefill()
{
    wait_prefill();
    if (filled_blocks >= sdef_target_count_prefill)
        return;

    const auto task = &TaskScheduler->AddTask([this]
    {
        size_t next_block_to_fill = (current_block + filled_blocks) % sdef_target_count_prefill;

        while (filled_blocks < sdef_target_count_prefill)
        {
            auto& block = temp_buf[next_block_to_fill];

            fill_block(block.data(), block.size());

            next_block_to_fill = (next_block_to_fill + 1) % sdef_target_count_prefill;
            filled_blocks++;
        }

        prefill_task.store(nullptr, std::memory_order_release);
    });

    prefill_task.store(task, std::memory_order_release);
}

void CSoundRender_Emitter::wait_prefill() const
{
    if (const auto task = prefill_task.load(std::memory_order_acquire))
        TaskScheduler->Wait(*task);
}

u32 CSoundRender_Emitter::get_bytes_total() const
{
    return owner_data->dwBytesTotal;
}

float CSoundRender_Emitter::get_length_sec() const
{
    return owner_data->fTimeTotal;
}
