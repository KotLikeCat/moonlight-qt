#pragma once

#include "micframer.h"

#include <QString>
#include <QStringList>

#include <atomic>

#include "SDL_compat.h"

struct OpusEncoder;

// Captures the microphone (SDL capture device, 48 kHz float mono), Opus-encodes
// 20 ms frames and hands them to LiSendMicrophoneOpusFrame().
//
// The SDL capture callback runs on SDL's audio thread: nothing in here touches
// Qt UI. LiSendMicrophoneOpusFrame() is thread-safe.
//
// Test mode (env ML_MIC_TEST_TONE=1): a 440 Hz sine generator thread replaces the
// capture device. It needs no microphone permission.
class MicStreamer
{
public:
    // preferredDevice: empty = system default input device.
    explicit MicStreamer(const QString& preferredDevice, bool toneMode);
    ~MicStreamer();

    // Creates the encoder and starts capturing (unmuted). Returns false on failure.
    bool start();

    // Mute = capture device closed (OS microphone indicator off).
    void setMuted(bool muted);
    void toggleMute();
    bool isMuted() const { return m_Muted.load(); }

    // True once the host reported it does not support the microphone stream
    bool hasStopped() const { return m_Stopped.load(); }

    static bool isToneModeRequested();
    static QStringList captureDeviceNames();

private:
    // Requires the SDL audio subsystem to be initialized
    static QStringList enumerateCaptureDevices();

    bool openDevice();
    void closeDevice();
    void startToneThread();
    void stopToneThread();

    void feedSamples(const float* samples, int count);
    void encodeAndSend(const float* frame);

    static void SDLCALL captureCallback(void* userdata, Uint8* stream, int len);
    static int SDLCALL toneThreadMain(void* userdata);

    QString m_PreferredDevice;
    bool m_ToneMode;

    bool m_AudioInitialized;
    std::atomic<SDL_AudioDeviceID> m_Device;
    SDL_Thread* m_ToneThread;
    std::atomic<bool> m_ToneThreadRun;
    std::atomic<bool> m_Sending;   // false while muted or stopped
    std::atomic<bool> m_Stopped;
    std::atomic<bool> m_Muted;
    bool m_StoppedLogged = false;

    OpusEncoder* m_Encoder;
    MicFramer m_Framer;
    double m_TonePhase;
    int m_ErrorLogCount;
};
