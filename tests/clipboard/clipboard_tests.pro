QT += testlib
QT -= gui
CONFIG += console testcase c++17 sdk_no_version_check
CONFIG -= app_bundle
TEMPLATE = app
TARGET = clipboard_tests

CLIPBOARD_DIR = $$PWD/../../app/streaming/clipboard
INCLUDEPATH += $$CLIPBOARD_DIR $$CLIPBOARD_DIR/files

SOURCES += \
    tst_clipboard.mm \
    $$CLIPBOARD_DIR/clipboardbundle.cpp \
    $$CLIPBOARD_DIR/clipboardsyncstate.cpp \
    $$CLIPBOARD_DIR/files/manifestbuilder.cpp \
    $$CLIPBOARD_DIR/files/filecodec.cpp \
    $$CLIPBOARD_DIR/macpasteboard.mm

LIBS += -framework AppKit
