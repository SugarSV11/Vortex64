// Project64 - A Nintendo 64 emulator
// https://www.pj64-emu.com/
// Copyright(C) 2001-2021 Project64
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html

#include "SDLAudio.h"
#include <Project64-audio/AudioMain.h>
#include <Project64-audio/AudioSettings.h>
#include <Project64-audio/trace.h>
#include <SDL2/SDL.h>

SDLAudioDriver::SDLAudioDriver() :
    m_SdlDevice(0),
    m_Volume(100),
    m_DeviceOpen(false)
{
}

SDLAudioDriver::~SDLAudioDriver()
{
}

bool SDLAudioDriver::Initialize()
{
    WriteTrace(TraceAudioDriver, TraceDebug, "Start");
    if (!SoundDriverBase::Initialize())
    {
        WriteTrace(TraceAudioDriver, TraceDebug, "Done (res: false)");
        return false;
    }

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        WriteTrace(TraceAudioDriver, TraceError, "SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s", SDL_GetError());
        WriteTrace(TraceAudioDriver, TraceDebug, "Done (res: false)");
        return false;
    }

    WriteTrace(TraceAudioDriver, TraceDebug, "Done (res: true)");
    return true;
}

void SDLAudioDriver::CloseDevice()
{
    if (m_DeviceOpen)
    {
        SDL_PauseAudioDevice((SDL_AudioDeviceID)m_SdlDevice, 1);
        SDL_CloseAudioDevice((SDL_AudioDeviceID)m_SdlDevice);
        m_SdlDevice = 0;
        m_DeviceOpen = false;
    }
}

void SDLAudioDriver::StopAudio()
{
    WriteTrace(TraceAudioDriver, TraceDebug, "Start");
    CloseDevice();
    WriteTrace(TraceAudioDriver, TraceDebug, "Done");
}

void SDLAudioDriver::StartAudio()
{
    WriteTrace(TraceAudioDriver, TraceDebug, "Start");
    if (m_DeviceOpen)
    {
        SDL_PauseAudioDevice((SDL_AudioDeviceID)m_SdlDevice, 0);
    }
    WriteTrace(TraceAudioDriver, TraceDebug, "Done");
}

void SDLAudioDriver::stAudioCallback(void * userdata, uint8_t * stream, int len)
{
    ((SDLAudioDriver *)userdata)->AudioCallback(stream, len);
}

void SDLAudioDriver::AudioCallback(uint8_t * stream, int len)
{
    LoadAiBuffer(stream, (uint32_t)len);
    // the volume of the plugin's settings (SDL has no volume of its own for the device): 16 bit stereo samples
    const uint32_t Volume = m_Volume.load(std::memory_order_relaxed);
    if (Volume < 100)
    {
        int16_t * Samples = reinterpret_cast<int16_t *>(stream);
        for (int i = 0, n = len / (int)sizeof(int16_t); i < n; i++)
        {
            Samples[i] = (int16_t)((int32_t)Samples[i] * (int32_t)Volume / 100);
        }
    }
}

void SDLAudioDriver::SetFrequency(uint32_t Frequency, uint32_t BufferSize)
{
    WriteTrace(TraceAudioDriver, TraceDebug, "Start (Frequency: 0x%08X)", Frequency);
    StopAudio();

    if (Frequency == 0)
    {
        WriteTrace(TraceAudioDriver, TraceDebug, "Done (Frequency == 0)");
        return;
    }

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0)
    {
        WriteTrace(TraceAudioDriver, TraceWarning, "Audio subsystem not initialized");
        WriteTrace(TraceAudioDriver, TraceDebug, "Done");
        return;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = (int)Frequency;
    want.format = AUDIO_S16LSB;
    want.channels = 2;
    // SDL requires a power-of-two sample count; pick one close to what the
    // core requested (BufferSize is in bytes for a stereo 16-bit segment).
    uint32_t samples = BufferSize > 0 ? (BufferSize / 4) : 1024;
    uint32_t pow2 = 1;
    while (pow2 < samples && pow2 < 8192)
    {
        pow2 <<= 1;
    }
    want.samples = (Uint16)pow2;
    want.callback = stAudioCallback;
    want.userdata = this;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (dev == 0)
    {
        WriteTrace(TraceAudioDriver, TraceError, "SDL_OpenAudioDevice failed: %s", SDL_GetError());
        WriteTrace(TraceAudioDriver, TraceDebug, "Done (res: false)");
        return;
    }

    m_SdlDevice = (uint32_t)dev;
    m_DeviceOpen = true;

    StartAudio();
    WriteTrace(TraceAudioDriver, TraceDebug, "Done");
}

void SDLAudioDriver::SetVolume(uint32_t Volume)
{
    m_Volume = Volume > 100 ? 100 : Volume; // applied in AudioCallback
}
