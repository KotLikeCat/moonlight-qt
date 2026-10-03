#include "fileserver.h"

#include <QFile>
#include <QFileInfo>
#include <QThread>

namespace {
constexpr quint32 kMaxRangeBytes = 4u * 1024 * 1024;
}

FileServer::FileServer(int workers)
    : m_WorkerCount(workers < 1 ? 1 : workers)
{
}

FileServer::~FileServer()
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Stopping = true;
        m_Queue.clear();
    }
    m_Cond.notify_all();
    for (QThread* t : m_Threads) {
        t->wait();
        delete t;
    }
}

void FileServer::setSenderFactory(SenderFactory factory)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Factory = std::move(factory);
}

void FileServer::setSenderForTests(Sender sender)
{
    auto shared = std::make_shared<Sender>(std::move(sender));
    setSenderFactory([shared]() -> Sender { return *shared; });
}

void FileServer::setOffer(const QByteArray& offerId16, const QVector<ClipboardFiles::Entry>& entries)
{
    auto copy = std::make_shared<const QVector<ClipboardFiles::Entry>>(entries);
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_OfferId = offerId16;
    m_Entries = std::move(copy);
}

void FileServer::clearOffer()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_OfferId.clear();
    m_Entries.reset();
}

void FileServer::request(const QByteArray& offerId16, quint32 requestId, quint32 fileIndex, quint64 offset, quint32 length)
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Stopping) {
            return;
        }
        startWorkersLocked();
        m_Queue.push_back(Job{offerId16, requestId, fileIndex, offset, length});
    }
    m_Cond.notify_one();
}

void FileServer::startWorkersLocked()
{
    if (m_Started) {
        return;
    }
    m_Started = true;
    for (int i = 0; i < m_WorkerCount; i++) {
        QThread* t = QThread::create([this]() { workerMain(); });
        t->setObjectName(QStringLiteral("ClipboardFiles%1").arg(i));
        m_Threads.push_back(t);
        t->start();
    }
}

void FileServer::workerMain()
{
    SenderFactory factory;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        factory = m_Factory;
    }
    Sender sender = factory ? factory() : Sender();

    for (;;) {
        Job job;
        std::shared_ptr<const QVector<ClipboardFiles::Entry>> entries;
        bool known = false;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Cond.wait(lock, [this]() { return m_Stopping || !m_Queue.empty(); });
            if (m_Stopping) {
                return;
            }
            job = std::move(m_Queue.front());
            m_Queue.pop_front();
            entries = m_Entries;
            known = entries && m_OfferId == job.offerId;
        }

        Reply reply;
        if (!known) {
            reply.error = "gone";
        }
        else {
            reply = readRange(*entries, job.fileIndex, job.offset, job.length);
        }
        if (sender) {
            sender(job, reply);
        }
    }
}

FileServer::Reply FileServer::readRange(const QVector<ClipboardFiles::Entry>& entries, quint32 fileIndex, quint64 offset, quint32 length)
{
    Reply reply;
    if (length == 0 || length > kMaxRangeBytes) {
        reply.error = "io";
        return reply;
    }
    if (fileIndex >= quint32(entries.size()) || entries[int(fileIndex)].isDir) {
        reply.error = "io";
        return reply;
    }
    const ClipboardFiles::Entry& entry = entries[int(fileIndex)];

    const QFileInfo info(entry.absolutePath);
    if (!info.exists() || !info.isFile() ||
            quint64(info.size()) != entry.size ||
            info.lastModified().toMSecsSinceEpoch() != entry.mtimeMs) {
        reply.error = "changed";
        return reply;
    }

    QFile file(entry.absolutePath);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(qint64(offset))) {
        reply.error = "io";
        return reply;
    }
    QByteArray data(qsizetype(length), Qt::Uninitialized);
    qint64 total = 0;
    while (total < qint64(length)) {
        const qint64 n = file.read(data.data() + total, qint64(length) - total);
        if (n < 0) {
            reply.error = "io";
            return reply;
        }
        if (n == 0) {
            break;  // EOF
        }
        total += n;
    }
    data.truncate(qsizetype(total));
    reply.body = data;
    return reply;
}
