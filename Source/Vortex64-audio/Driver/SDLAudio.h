// Project64 - A Nintendo 64 emulator
// https://www.pj64-emu.com/
// Copyright(C) 2001-2021 Project64
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html

#pragma once
#include <atomic>
#include "SoundBase.h"

// Linux desktop audio backend, implemented on top of SDL2's audio device
// API. SDL2 pulls PCM data from us via a callback running on its own
// internal thread, so unlike DirectSoundDriver we don't need to manage an
// explicit polling thread ourselves.
class SDLAudioDriver :
    public SoundDriverBase
{
public:
    SDLAudioDriver();
    ~SDLAudioDriver();

    bool Initialize();
    void StopAudio();
    void StartAudio();
    void SetFrequency(uint32_t Frequency, uint32_t BufferSize);
    void SetVolume(uint32_t Volume);
    bool IsPlaying() const override
    {
        return m_DeviceOpen;
    }

private:
    static void stAudioCallback(void * userdata, uint8_t * stream, int len);
    void AudioCallback(uint8_t * stream, int len);

    void CloseDevice();

    uint32_t m_SdlDevice; // SDL_AudioDeviceID (kept opaque to avoid pulling SDL2/SDL.h into this header)
    std::atomic<uint32_t> m_Volume; // set by the configuration, read by the audio thread
    bool m_DeviceOpen;
};
