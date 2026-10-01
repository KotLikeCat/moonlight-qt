#pragma once

#include "clipboardsyncstate.h"

#include <QObject>

#include <cstdint>

class MacPasteboard;
class NvComputer;
class NvHTTP;
class QTimer;

// Clipboard sync for one streaming session (macOS only).
// Lives on its own worker thread; public methods are thread-safe and never block.
class ClipboardSync : public QObject
{
    Q_OBJECT

public:
    // Returns nullptr when sync is disabled in preferences or unsupported by the host.
    static ClipboardSync* createForSession(NvComputer* computer, bool enabledInPreferences);

    void notifyHostChanged(uint32_t seq, uint32_t formats);
    void notifyFocusGained();
    void notifyFocusLost();
    // Stops the worker thread and deletes this object asynchronously. Do not use the pointer afterwards.
    void shutdownAsync();

private:
    explicit ClipboardSync(NvComputer* computer);
    void initInWorker();
    void handle(ClipboardSyncState::Action action);
    void fetch(uint32_t formatsMask);
    void push();
    void disable(const char* reason);

    NvComputer* m_Computer;
    NvHTTP* m_Http = nullptr;
    MacPasteboard* m_Pasteboard = nullptr;
    QTimer* m_QuickFetchTimer = nullptr;
    ClipboardSyncState m_State;
    bool m_Disabled = false;
};
