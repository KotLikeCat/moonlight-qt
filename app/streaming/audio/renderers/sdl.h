#pragma once

#include "renderer.h"
#include "SDL_compat.h"

#include <QString>

class SdlAudioRenderer : public IAudioRenderer
{
public:
    SdlAudioRenderer(QString preferredDevice = QString());

    virtual ~SdlAudioRenderer();

    virtual bool prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION* opusConfig);

    virtual void* getAudioBuffer(int* size);

    virtual bool submitAudio(int bytesWritten);

    virtual AudioFormat getAudioBufferFormat();

    // True when a non-empty preferred device was requested but not used
    bool preferredDeviceMissing() const { return m_PreferredDeviceMissing; }

private:
    SDL_AudioDeviceID m_AudioDevice;
    void* m_AudioBuffer;
    Uint32 m_FrameSize;
    Uint32 m_FrameDurationMs;
    QString m_PreferredDevice;
    bool m_PreferredDeviceMissing;
};
