#include "micstreamer.h"

#include "../audiodevice.h"

#include <opus.h>
#include <Limelight.h>

#include <cmath>
#include <cstdlib>

#define MIC_SAMPLE_RATE 48000
#define MIC_BITRATE 48000
#define MIC_COMPLEXITY 5
#define MIC_MAX_PACKET 200
#define MIC_TONE_HZ 440.0
#define MIC_TONE_AMPLITUDE 0.3

MicStreamer::MicStreamer(const QString& preferredDevice, bool toneMode)
    : m_PreferredDevice(preferredDevice),
      m_ToneMode(toneMode),
      m_AudioInitialized(false),
      m_Device(0),
      m_ToneThread(nullptr),
      m_ToneThreadRun(false),
      m_Sending(false),
      m_Stopped(false),
      m_Muted(false),
      m_Encoder(nullptr),
      m_TonePhase(0),
      m_ErrorLogCount(0)
{
}

MicStreamer::~MicStreamer()
{
    // Stop producing frames before tearing anything down
    m_Sending = false;
    closeDevice();
    stopToneThread();

    if (m_Encoder != nullptr) {
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
    }

    // Paired with the Init in start(); SDL refcounts, so this is safe even if
    // the audio renderer holds the subsystem as well.
    if (m_AudioInitialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_AudioInitialized = false;
    }
}

bool MicStreamer::isToneModeRequested()
{
    const char* v = SDL_getenv("ML_MIC_TEST_TONE");
    return v != nullptr && QString::fromUtf8(v) == QStringLiteral("1");
}

QStringList MicStreamer::captureDeviceNames()
{
    QStringList names;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s",
                     SDL_GetError());
        return names;
    }

    names = enumerateCaptureDevices();

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return names;
}

QStringList MicStreamer::enumerateCaptureDevices()
{
    QStringList names;
    int count = SDL_GetNumAudioDevices(1);
    for (int i = 0; i < count; i++) {
        const char* name = SDL_GetAudioDeviceName(i, 1);
        if (name != nullptr) {
            names.append(QString::fromUtf8(name));
        }
    }
    return names;
}

bool MicStreamer::start()
{
    int err = 0;
    m_Encoder = opus_encoder_create(MIC_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &err);
    if (m_Encoder == nullptr || err != OPUS_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Microphone: opus_encoder_create() failed: %d", err);
        m_Encoder = nullptr;
        return false;
    }
    opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(MIC_BITRATE));
    opus_encoder_ctl(m_Encoder, OPUS_SET_COMPLEXITY(MIC_COMPLEXITY));

    if (m_ToneMode) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Microphone: test tone mode (440 Hz), capture device not used");
        m_Muted = false;
        m_Sending = true;
        startToneThread();
        return true;
    }

    // Held for the streamer's lifetime so mute/unmute doesn't churn the subsystem
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Microphone: SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s",
                     SDL_GetError());
        return false;
    }
    m_AudioInitialized = true;

    m_Muted = false;
    if (!openDevice()) {
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Microphone: streaming (unmuted)");
    return true;
}

bool MicStreamer::openDevice()
{
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = MIC_SAMPLE_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = MicFramer::FrameSamples;
    want.callback = captureCallback;
    want.userdata = this;

    QByteArray deviceName;
    if (!m_PreferredDevice.isEmpty()) {
        QString resolved = AudioDevice::resolve(enumerateCaptureDevices(), m_PreferredDevice);
        if (resolved.isEmpty()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Microphone: preferred device \"%s\" is not connected, using system default",
                        m_PreferredDevice.toUtf8().constData());
        }
        else {
            deviceName = resolved.toUtf8();
        }
    }

    m_Device = 0;
    if (!deviceName.isEmpty()) {
        m_Device = SDL_OpenAudioDevice(deviceName.constData(), 1, &want, &have, 0);
        if (m_Device == 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Microphone: failed to open \"%s\": %s. Retrying with system default",
                        deviceName.constData(), SDL_GetError());
        }
    }
    if (m_Device == 0) {
        m_Device = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);
    }
    if (m_Device == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Microphone: failed to open capture device: %s", SDL_GetError());
        return false;
    }

    m_Framer.reset();
    m_Sending = true;
    SDL_PauseAudioDevice(m_Device, 0);
    return true;
}

void MicStreamer::closeDevice()
{
    SDL_AudioDeviceID dev = m_Device.load();
    if (dev != 0) {
        // SDL_CloseAudioDevice waits for an in-flight callback to finish
        SDL_PauseAudioDevice(dev, 1);
        SDL_CloseAudioDevice(dev);
        m_Device = 0;
    }
}

void MicStreamer::setMuted(bool muted)
{
    if (m_Stopped) {
        if (!m_StoppedLogged) {
            m_StoppedLogged = true;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Microphone: the host does not accept microphone audio, mute toggle ignored");
        }
        return;
    }
    if (muted == m_Muted.load()) {
        return;
    }
    m_Muted = muted;

    if (m_ToneMode) {
        m_Sending = !muted;
    }
    else if (muted) {
        m_Sending = false;
        closeDevice();
    }
    else {
        // The device is closed here so nothing is encoding; drop stale
        // predictor state from before the mute
        if (m_Encoder != nullptr) {
            opus_encoder_ctl(m_Encoder, OPUS_RESET_STATE);
        }
        if (!openDevice()) {
            // Could not reopen: stay muted so the next toggle retries
            m_Muted = true;
        }
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Microphone: %s", m_Muted ? "muted" : "unmuted");
}

void MicStreamer::toggleMute()
{
    setMuted(!m_Muted);
}

void MicStreamer::captureCallback(void* userdata, Uint8* stream, int len)
{
    auto* self = static_cast<MicStreamer*>(userdata);
    self->feedSamples(reinterpret_cast<const float*>(stream), len / (int)sizeof(float));
}

void MicStreamer::feedSamples(const float* samples, int count)
{
    if (!m_Sending.load() || m_Stopped.load()) {
        return;
    }

    m_Framer.push(samples, count, [this](const float* frame) {
        encodeAndSend(frame);
    });
}

void MicStreamer::encodeAndSend(const float* frame)
{
    if (m_Stopped.load()) {
        return;
    }

    unsigned char packet[MIC_MAX_PACKET];
    int len = opus_encode_float(m_Encoder, frame, MicFramer::FrameSamples, packet, sizeof(packet));
    if (len <= 0) {
        if (m_ErrorLogCount++ < 5) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Microphone: opus_encode_float() failed: %d", len);
        }
        return;
    }

    int ret = LiSendMicrophoneOpusFrame(packet, len);
    if (ret == -3) {
        // Host/protocol doesn't support the microphone stream: stop for this session.
        // (SDL_PauseAudioDevice is safe from the callback; closing the device is not,
        // so the device is closed by the destructor.)
        if (!m_Stopped.exchange(true)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Microphone: host does not support microphone passthrough, stopping for this session");
            m_Sending = false;
            SDL_AudioDeviceID dev = m_Device.load();
            if (dev != 0) {
                SDL_PauseAudioDevice(dev, 1);
            }
        }
    }
    // -2 (not connected) and -1 (send failure): drop silently
}

void MicStreamer::startToneThread()
{
    m_ToneThreadRun = true;
    m_ToneThread = SDL_CreateThread(toneThreadMain, "MicTone", this);
    if (m_ToneThread == nullptr) {
        m_ToneThreadRun = false;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Microphone: failed to create test tone thread: %s", SDL_GetError());
    }
}

void MicStreamer::stopToneThread()
{
    if (m_ToneThread != nullptr) {
        m_ToneThreadRun = false;
        SDL_WaitThread(m_ToneThread, nullptr);
        m_ToneThread = nullptr;
    }
}

int MicStreamer::toneThreadMain(void* userdata)
{
    auto* self = static_cast<MicStreamer*>(userdata);
    const double pi = 3.14159265358979323846;
    const double step = 2.0 * pi * MIC_TONE_HZ / MIC_SAMPLE_RATE;

    float frame[MicFramer::FrameSamples];
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 next = SDL_GetPerformanceCounter();
    const Uint64 period = freq / 50; // 20 ms

    while (self->m_ToneThreadRun.load()) {
        for (int i = 0; i < MicFramer::FrameSamples; i++) {
            frame[i] = (float)(MIC_TONE_AMPLITUDE * std::sin(self->m_TonePhase));
            self->m_TonePhase += step;
            if (self->m_TonePhase > 2.0 * pi) {
                self->m_TonePhase -= 2.0 * pi;
            }
        }

        if (self->m_Sending.load()) {
            self->encodeAndSend(frame);
        }

        next += period;
        Uint64 now = SDL_GetPerformanceCounter();
        if (next > now) {
            SDL_Delay((Uint32)((next - now) * 1000 / freq));
        }
        else if (now - next > period * 5) {
            next = now; // fell far behind, don't burst
        }
    }

    return 0;
}
