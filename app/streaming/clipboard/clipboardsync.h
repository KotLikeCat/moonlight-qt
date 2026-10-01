#pragma once

#include "clipboardsyncstate.h"

#include <QObject>

#include <cstdint>
#include <deque>
#include <functional>

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
    void runOrDefer(std::function<void()> work);
    void drainDeferred();
    void finishShutdown();

    NvComputer* m_Computer;
    NvHTTP* m_Http = nullptr;
    MacPasteboard* m_Pasteboard = nullptr;
    QTimer* m_QuickFetchTimer = nullptr;
    ClipboardSyncState m_State;
    bool m_FetchDisabled = false;
    bool m_PushDisabled = false;
    // NvHTTP spins a nested event loop, so queued events (notifications, timers,
    // shutdown) can arrive while a request is in flight; they are deferred.
    bool m_InRequest = false;
    bool m_ShutdownRequested = false;
    bool m_ShutdownFinished = false;
    std::deque<std::function<void()>> m_Deferred;
};
