#pragma once

#include "clipboardsyncstate.h"

#include <QByteArray>
#include <QObject>
#include <QVector>

#include "files/manifestbuilder.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <deque>
#include <functional>

class FileServer;
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
    // Thread-safe (called on the moonlight-common-c callback thread): queues a host range request.
    void notifyFileRequest(const uint8_t offerId[16], uint32_t requestId, uint32_t fileIndex, uint64_t offset, uint32_t length);
    // Shows a transient message to the user (an empty string clears it). Invoked on the worker thread;
    // the handler must be thread-safe. Cleared synchronously by shutdownAsync(), so it is never called after that returns.
    using NoticeHandler = std::function<void(const QString&)>;
    void setNoticeHandler(NoticeHandler handler);
    // Stops the worker thread and deletes this object asynchronously. Do not use the pointer afterwards.
    void shutdownAsync();

private:
    explicit ClipboardSync(NvComputer* computer);
    void initInWorker();
    void handle(ClipboardSyncState::Action action);
    void fetch(uint32_t formatsMask);
    void push();
    bool pushFiles(const QStringList& paths, long changeCount);
    void runOrDefer(std::function<void()> work);
    void drainDeferred();
    void finishShutdown();
    void showNotice(const QString& reason);
    bool filesCapable();

    NvComputer* m_Computer;
    NvHTTP* m_Http = nullptr;
    MacPasteboard* m_Pasteboard = nullptr;
    QTimer* m_QuickFetchTimer = nullptr;
    ClipboardSyncState m_State;
    bool m_FetchDisabled = false;
    bool m_PushDisabled = false;
    bool m_FilesDisabled = false;
    FileServer* m_FileServer = nullptr;
    std::mutex m_NoticeMutex;
    NoticeHandler m_NoticeHandler;
    QTimer* m_NoticeTimer = nullptr;
    // Host file-capability tracking (worker thread only).
    bool m_FilesCapUnknown = false;
    bool m_HaveCapRefresh = false;
    std::chrono::steady_clock::time_point m_LastCapRefresh;
    // NvHTTP spins a nested event loop, so queued events (notifications, timers,
    // shutdown) can arrive while a request is in flight; they are deferred.
    bool m_InRequest = false;
    bool m_ShutdownRequested = false;
    bool m_ShutdownFinished = false;
    std::deque<std::function<void()>> m_Deferred;
};
