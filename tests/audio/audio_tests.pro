QT += testlib
QT -= gui
CONFIG += console testcase c++17 sdk_no_version_check
CONFIG -= app_bundle
TEMPLATE = app
TARGET = audio_tests

AUDIO_DIR = $$PWD/../../app/streaming/audio
INCLUDEPATH += $$AUDIO_DIR

SOURCES += \
    tst_audio.cpp \
    $$AUDIO_DIR/audiodevice.cpp
