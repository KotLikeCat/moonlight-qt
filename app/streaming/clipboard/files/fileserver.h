#pragma once

#include <QByteArray>
#include <QObject>
#include <QVector>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "manifestbuilder.h"

class NvComputer;
class QThread;

// Serves the host's range requests for the currently offered clipboard files.
// request() is thread-safe and never blocks; a small pool of worker threads reads
// the bytes and hands them to a per-worker sender (an NvHTTP POST in the app).
class FileServer : public QObject
{
    Q_OBJECT

public:
    struct Reply {
        QByteArray body;
        QByteArray error;  // "", "gone", "changed" or "io"
    };
    struct Job {
        QByteArray offerId;
        quint32 requestId;
        quint32 fileIndex;
        quint64 offset;
        quint32 length;
    };
    using Sender = std::function<void(const Job&, const Reply&)>;
    // Called once on each worker thread; the returned sender is used (and destroyed) on that thread.
    using SenderFactory = std::function<Sender()>;

    explicit FileServer(NvComputer* computer, int workers = 4);
    ~FileServer();

    void setSenderFactory(SenderFactory factory);
    void setSenderForTests(Sender sender);

    void setOffer(const QByteArray& offerId16, const QVector<ClipboardFiles::Entry>& entries);
    void clearOffer();
    void request(const QByteArray& offerId16, quint32 requestId, quint32 fileIndex, quint64 offset, quint32 length);

    static Reply readRange(const QVector<ClipboardFiles::Entry>& entries, quint32 fileIndex, quint64 offset, quint32 length);

private:
    void workerMain();
    void startWorkersLocked();

    NvComputer* m_Computer;
    int m_WorkerCount;
    std::mutex m_Mutex;
    std::condition_variable m_Cond;
    std::deque<Job> m_Queue;
    bool m_Stopping = false;
    bool m_Started = false;
    SenderFactory m_Factory;
    QByteArray m_OfferId;
    std::shared_ptr<const QVector<ClipboardFiles::Entry>> m_Entries;
    std::vector<QThread*> m_Threads;
};
