QT += testlib
QT -= gui
CONFIG += console testcase c++17 sdk_no_version_check
CONFIG -= app_bundle
TEMPLATE = app
TARGET = clipboard_tests

CLIPBOARD_DIR = $$PWD/../../app/streaming/clipboard
INCLUDEPATH += $$CLIPBOARD_DIR

SOURCES += \
    tst_clipboard.mm \
    $$CLIPBOARD_DIR/clipboardbundle.cpp

LIBS += -framework AppKit
