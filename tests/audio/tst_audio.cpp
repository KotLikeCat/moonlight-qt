#include <QtTest>

#include "audiodevice.h"

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
};

QTEST_GUILESS_MAIN(AudioTests)
#include "tst_audio.moc"
