#include <QtTest>

#include "audiodevice.h"
#include "mic/micframer.h"

#include <opus.h>
#include <cmath>
#include <vector>

class AudioTests : public QObject
{
    Q_OBJECT

private slots:
    void returnsPreferredWhenPresent()
    {
        QStringList available = {"MacBook Speakers", "AirPods"};
        QCOMPARE(AudioDevice::resolve(available, "AirPods"), QString("AirPods"));
    }

    void emptyWhenMissing()
    {
        QStringList available = {"MacBook Speakers"};
        QCOMPARE(AudioDevice::resolve(available, "AirPods"), QString());
    }

    void emptyWhenPreferenceEmpty()
    {
        QStringList available = {"MacBook Speakers", ""};
        QCOMPARE(AudioDevice::resolve(available, ""), QString());
    }

    void exactMatchOnly()
    {
        QStringList available = {"AirPods Pro"};
        QCOMPARE(AudioDevice::resolve(available, "AirPods"), QString());
        QCOMPARE(AudioDevice::resolve(available, "airpods pro"), QString());
        QCOMPARE(AudioDevice::resolve(available, "AirPods Pro "), QString());
    }

    void nonAscii()
    {
        QStringList available = {"Колонки", "Наушники AirPods"};
        QCOMPARE(AudioDevice::resolve(available, "Наушники AirPods"), QString("Наушники AirPods"));
    }

    // ---- MicFramer ----

    static std::vector<float> ramp(int count, int start)
    {
        std::vector<float> v(count);
        for (int i = 0; i < count; i++) v[i] = (float)(start + i);
        return v;
    }

    // Feeds `total` ramp samples in chunks of `chunk`, verifies every emitted
    // frame is exactly 960 samples of the continuous ramp.
    static void runFramer(int chunk, int total, int expectedFrames, int expectedPending)
    {
        MicFramer framer;
        int frames = 0;
        bool contiguous = true;
        int fed = 0;
        while (fed < total) {
            int n = qMin(chunk, total - fed);
            std::vector<float> data = ramp(n, fed);
            framer.push(data.data(), n, [&](const float* f) {
                for (int i = 0; i < MicFramer::FrameSamples; i++) {
                    if (f[i] != (float)(frames * MicFramer::FrameSamples + i)) contiguous = false;
                }
                frames++;
            });
            fed += n;
        }
        QVERIFY(contiguous);
        QCOMPARE(frames, expectedFrames);
        QCOMPARE(framer.pending(), expectedPending);
    }

    void framerChunk1() { runFramer(1, 2000, 2, 80); }
    void framerChunk333() { runFramer(333, 2000, 2, 80); }
    void framerChunk960() { runFramer(960, 1920, 2, 0); }
    void framerChunk2000() { runFramer(2000, 2000, 2, 80); }
    void framerChunk2000Twice() { runFramer(2000, 4000, 4, 160); }

    void framerRemainderKeptAcrossPushes()
    {
        MicFramer framer;
        int frames = 0;
        std::vector<float> a = ramp(959, 0);
        framer.push(a.data(), 959, [&](const float*) { frames++; });
        QCOMPARE(frames, 0);
        QCOMPARE(framer.pending(), 959);
        float one = 1.f;
        framer.push(&one, 1, [&](const float*) { frames++; });
        QCOMPARE(frames, 1);
        QCOMPARE(framer.pending(), 0);
    }

    void framerReset()
    {
        MicFramer framer;
        std::vector<float> a = ramp(500, 0);
        framer.push(a.data(), 500, [](const float*) {});
        framer.reset();
        QCOMPARE(framer.pending(), 0);
    }

    // ---- Opus ----

    void opusSineFrameFitsWireLimit()
    {
        int err = 0;
        OpusEncoder* enc = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &err);
        QVERIFY(enc != nullptr);
        QCOMPARE(err, OPUS_OK);
        opus_encoder_ctl(enc, OPUS_SET_BITRATE(48000));
        opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(5));

        const double pi = 3.14159265358979323846;
        float pcm[960];
        unsigned char out[200];
        for (int frame = 0; frame < 5; frame++) {
            for (int i = 0; i < 960; i++) {
                pcm[i] = 0.5f * (float)std::sin(2 * pi * 1000.0 * (frame * 960 + i) / 48000.0);
            }
            int n = opus_encode_float(enc, pcm, 960, out, sizeof(out));
            QVERIFY(n > 0);
            QVERIFY(n <= 200);
        }
        opus_encoder_destroy(enc);
    }
};

QTEST_GUILESS_MAIN(AudioTests)
#include "tst_audio.moc"
