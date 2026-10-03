#include "clipboardsync.h"

#include "clipboardbundle.h"
#include "macpasteboard.h"
#include "files/filecodec.h"
#include "files/fileserver.h"

#include "backend/nvcomputer.h"
#include "backend/nvhttp.h"

#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <QtDebug>

namespace {
constexpr int kQuickFetchDelayMs = 300;
constexpr int kSmallTimeoutMs = 5000;
constexpr int kImageTimeoutMs = 20000;
constexpr int kFilePostTimeoutMs = 30000;
constexpr int kManifestTimeoutMs = 20000;
constexpr int kMaxManifestBytes = 32 * 1024 * 1024;
constexpr uint32_t kQuickFormats = ClipboardBundle::FormatText | ClipboardBundle::FormatHtml | ClipboardBundle::FormatRtf;
}

ClipboardSync* ClipboardSync::createForSession(NvComputer* computer, bool enabledInPreferences)
{
    if (!enabledInPreferences || computer->clipboardSyncVersion < 1) {
        return nullptr;
    }
    return new ClipboardSync(computer);
}

ClipboardSync::ClipboardSync(NvComputer* computer)
    : m_Computer(computer)
{
    // Created here, before the pointer is published to the callback thread. Each file
    // worker thread owns its own NvHTTP (created and destroyed on that thread).
    m_FileServer = new FileServer(computer, 4);
    m_FileServer->setSenderFactory([computer]() -> FileServer::Sender {
        auto http = std::make_shared<NvHTTP>(computer);
        return [http](const FileServer::Job& job, const FileServer::Reply& reply) {
            const int status = http->postClipboardFileChunk(job.offerId.toHex(), job.requestId, job.fileIndex,
                                                            job.offset, reply.body, reply.error, kFilePostTimeoutMs);
            if (status != 200 && status != 410) {
                qWarning() << "Clipboard file chunk POST failed with HTTP status" << status;
            }
        };
    });
    QThread* thread = new QThread();
    // We are constructed on a short-lived thread; give the QThread the main thread's
    // affinity so the queued deleteLater from finished() is actually processed.
    thread->moveToThread(QCoreApplication::instance()->thread());
    thread->setObjectName("ClipboardSync");
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    moveToThread(thread);
    thread->start();
    QMetaObject::invokeMethod(this, [this]() { initInWorker(); }, Qt::QueuedConnection);
}

void ClipboardSync::initInWorker()
{
    // NvHTTP's QNetworkAccessManager must be created on the thread that uses it.
    m_Http = new NvHTTP(m_Computer);
    m_Pasteboard = new MacPasteboard();
    m_QuickFetchTimer = new QTimer(this);
    m_QuickFetchTimer->setSingleShot(true);
    m_QuickFetchTimer->setInterval(kQuickFetchDelayMs);
    connect(m_QuickFetchTimer, &QTimer::timeout, this, [this]() {
        runOrDefer([this]() {
            if (!m_FetchDisabled && m_State.hostDataMissing()) {
                fetch(kQuickFormats);
            }
        });
    });
}

void ClipboardSync::notifyHostChanged(uint32_t seq, uint32_t formats)
{
    QMetaObject::invokeMethod(this, [this, seq, formats]() {
        runOrDefer([this, seq, formats]() {
            handle(m_State.onHostChanged(seq, formats));
        });
    }, Qt::QueuedConnection);
}

void ClipboardSync::notifyFocusGained()
{
    QMetaObject::invokeMethod(this, [this]() {
        runOrDefer([this]() {
            handle(m_State.onFocusGained(m_Pasteboard->changeCount(), m_Pasteboard->hasSensitiveData()));
        });
    }, Qt::QueuedConnection);
}

void ClipboardSync::notifyFocusLost()
{
    QMetaObject::invokeMethod(this, [this]() {
        runOrDefer([this]() {
            m_QuickFetchTimer->stop();
            handle(m_State.onFocusLost());
        });
    }, Qt::QueuedConnection);
}

void ClipboardSync::notifyFileRequest(const uint8_t offerId[16], uint32_t requestId, uint32_t fileIndex, uint64_t offset, uint32_t length)
{
    m_FileServer->request(QByteArray(reinterpret_cast<const char*>(offerId), 16), requestId, fileIndex, offset, length);
}

void ClipboardSync::shutdownAsync()
{
    QMetaObject::invokeMethod(this, [this]() {
        m_ShutdownRequested = true;
        if (!m_InRequest) {
            finishShutdown();
        }
        // Otherwise the in-flight request calls finishShutdown() when it returns.
    }, Qt::QueuedConnection);
}

void ClipboardSync::runOrDefer(std::function<void()> work)
{
    if (m_ShutdownRequested) {
        return;
    }
    if (m_InRequest) {
        m_Deferred.push_back(std::move(work));
        return;
    }
    work();
    drainDeferred();
}

void ClipboardSync::drainDeferred()
{
    // Runs outside any request; drained items may start requests, which defer
    // further events into the same queue that this loop keeps consuming.
    while (!m_Deferred.empty() && !m_ShutdownRequested) {
        std::function<void()> work = std::move(m_Deferred.front());
        m_Deferred.pop_front();
        work();
    }
}

void ClipboardSync::finishShutdown()
{
    if (m_ShutdownFinished) {
        return;
    }
    m_ShutdownFinished = true;
    if (m_QuickFetchTimer != nullptr) {
        m_QuickFetchTimer->stop();
    }
    delete m_FileServer;   // joins the file workers
    m_FileServer = nullptr;
    delete m_Http;
    m_Http = nullptr;
    delete m_Pasteboard;
    m_Pasteboard = nullptr;
    m_Deferred.clear();
    QThread* thread = this->thread();
    deleteLater();   // processed when the thread finishes
    thread->quit();
}

void ClipboardSync::handle(ClipboardSyncState::Action action)
{
    switch (action) {
    case ClipboardSyncState::Action::None:
        break;
    case ClipboardSyncState::Action::ScheduleQuickFetch:
        if (!m_FetchDisabled) {
            m_QuickFetchTimer->start();
        }
        break;
    case ClipboardSyncState::Action::FetchFull:
        if (!m_FetchDisabled) {
            fetch(0);
        }
        break;
    case ClipboardSyncState::Action::Push:
        if (!m_PushDisabled) {
            push();
        }
        break;
    }
}

void ClipboardSync::fetch(uint32_t formatsMask)
{
    const bool fullFetch = formatsMask == 0;
    const long changeCountBefore = m_Pasteboard->changeCount();
    m_InRequest = true;
    const NvHTTP::ClipboardResponse response =
            m_Http->getClipboardBundle(formatsMask, fullFetch ? kImageTimeoutMs : kSmallTimeoutMs);
    m_InRequest = false;
    if (m_ShutdownRequested) {
        finishShutdown();
        return;
    }

    if (response.httpStatus == 401 || response.httpStatus == 403) {
        if (!m_FetchDisabled) {
            m_FetchDisabled = true;
            qWarning() << "Clipboard fetch disabled for this session: the host denied clipboard access";
        }
        return;
    }
    if (response.httpStatus == 204) {
        if (response.hasSeq) {
            m_State.onFetchEmpty(response.seq, fullFetch);
        }
        else {
            qWarning() << "Clipboard fetch returned 204 without X-Clipboard-Seq";
        }
        return;
    }
    if (response.httpStatus != 200 || !response.hasSeq) {
        qWarning() << "Clipboard fetch failed with HTTP status" << response.httpStatus;
        return;
    }

    const ClipboardBundle::DecodeResult decoded = ClipboardBundle::decode(response.body);
    if (decoded.error != ClipboardBundle::DecodeError::None) {
        qWarning() << "Clipboard fetch returned an invalid bundle:" << ClipboardBundle::errorName(decoded.error);
        return;
    }

    if (decoded.items.isEmpty()) {
        // Only formats this client does not know: same as an empty reply.
        m_State.onFetchEmpty(response.seq, fullFetch);
        return;
    }
    if (m_Pasteboard->changeCount() != changeCountBefore) {
        // The user copied on the Mac during the request; keep their newer copy.
        m_State.onFetchEmpty(response.seq, true);
        qInfo() << "Clipboard: Mac clipboard changed during fetch, discarding host data";
        return;
    }

    const long changeCount = m_Pasteboard->write(decoded.items);
    const quint32 received = ClipboardBundle::maskOf(decoded.items);
    m_State.onFetchSucceeded(response.seq, received, changeCount, fullFetch);
    qInfo() << "Clipboard: applied host clipboard, formats" << received << "bytes" << response.body.size();
}

void ClipboardSync::push()
{
    const long changeCount = m_Pasteboard->changeCount();
    const QStringList filePaths = m_Pasteboard->fileURLs();
    if (!filePaths.isEmpty()) {
        if (!pushFiles(filePaths, changeCount)) {
            m_State.onPushSkipped(changeCount);
        }
        return;
    }
    QVector<ClipboardBundle::Item> items = m_Pasteboard->read();
    if (items.isEmpty() || !ClipboardBundle::fitToLimit(items, ClipboardBundle::MaxBytes)) {
        // Nothing we can send for this pasteboard version; don't retry it.
        m_State.onPushSkipped(changeCount);
        return;
    }

    const bool hasImage = (ClipboardBundle::maskOf(items) & ClipboardBundle::FormatPng) != 0;
    const QByteArray bundle = ClipboardBundle::encode(items);
    m_InRequest = true;
    const int status = m_Http->postClipboardBundle(bundle, hasImage ? kImageTimeoutMs : kSmallTimeoutMs);
    m_InRequest = false;
    if (m_ShutdownRequested) {
        finishShutdown();
        return;
    }
    if (status == 401 || status == 403) {
        if (!m_PushDisabled) {
            m_PushDisabled = true;
            qWarning() << "Clipboard push disabled for this session: the host denied clipboard access";
        }
        return;
    }
    if (status == 413 || status == 400) {
        qWarning() << "Clipboard push rejected by the host with HTTP status" << status << "- not retrying this clipboard";
        m_State.onPushSkipped(changeCount);
        return;
    }
    if (status != 200) {
        qWarning() << "Clipboard push failed with HTTP status" << status;
        return;
    }
    m_State.onPushSucceeded(changeCount);
    qInfo() << "Clipboard: sent Mac clipboard, formats" << ClipboardBundle::maskOf(items) << "bytes" << bundle.size();
}

// Offers the copied files to the host. Returns false when this pasteboard version
// must not be retried (the caller marks it skipped); true when handled (sent, or
// failed in a way that a later focus gain may retry).
bool ClipboardSync::pushFiles(const QStringList& paths, long changeCount)
{
    if (m_Pasteboard->hasSensitiveData() || m_FilesDisabled || !m_Computer->clipboardFilesSupported) {
        return false;
    }

    const ClipboardFiles::BuildResult built = ClipboardFiles::buildManifest(paths);
    if (!built.error.isEmpty() || built.entries.isEmpty()) {
        qWarning() << "Clipboard files not offered:" << (built.error.isEmpty() ? QStringLiteral("nothing to send") : built.error);
        return false;
    }
    if (!built.skipped.isEmpty()) {
        qInfo() << "Clipboard files: skipped" << built.skipped.size() << "unsupported entries";
    }
    const QByteArray offerId = ClipboardFiles::newOfferId();
    const QByteArray manifest = ClipboardFiles::encodeManifest(offerId, built.entries);
    if (manifest.size() > kMaxManifestBytes) {
        qWarning() << "Clipboard files not offered: manifest too large";
        return false;
    }

    // Register the offer first: the host may request ranges before the POST returns.
    m_FileServer->setOffer(offerId, built.entries);
    m_InRequest = true;
    const int status = m_Http->postClipboardFiles(manifest, kManifestTimeoutMs);
    m_InRequest = false;
    if (m_ShutdownRequested) {
        finishShutdown();
        return true;
    }
    if (status == 200) {
        m_State.onPushSucceeded(changeCount);
        qInfo() << "Clipboard: offered" << built.entries.size() << "file entries to the host";
        return true;
    }
    m_FileServer->clearOffer();
    if (status == 401 || status == 403) {
        m_FilesDisabled = true;
        qWarning() << "Clipboard file offers disabled for this session: the host denied file upload";
        return false;
    }
    if (status == 400 || status == 413) {
        qWarning() << "Clipboard file offer rejected by the host with HTTP status" << status;
        return false;
    }
    qWarning() << "Clipboard file offer failed with HTTP status" << status;
    return true;
}
