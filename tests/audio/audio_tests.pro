QT += testlib
QT -= gui
CONFIG += console testcase c++17 sdk_no_version_check
CONFIG -= app_bundle
TEMPLATE = app
TARGET = audio_tests

AUDIO_DIR = $$PWD/../../app/streaming/audio
INCLUDEPATH += $$AUDIO_DIR

# Opus (Opus round-trip size test)
macx {
    INCLUDEPATH += $$PWD/../../libs/mac/include
    LIBS += -L$$PWD/../../libs/mac/lib -lopus.0
    QMAKE_RPATHDIR += $$PWD/../../libs/mac/lib
}

SOURCES += \
    tst_audio.cpp \
    $$AUDIO_DIR/audiodevice.cpp \
    $$AUDIO_DIR/mic/micframer.cpp
