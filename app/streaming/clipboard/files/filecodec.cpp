#include "filecodec.h"

#include <QRandomGenerator>

namespace ClipboardFiles {

static void putLe(QByteArray &out, quint64 v, int bytes)
{
    for (int i = 0; i < bytes; i++) {
        out.append(char((v >> (8 * i)) & 0xff));
    }
}

QByteArray encodeManifest(const QByteArray &offerId16, const QVector<Entry> &entries)
{
    QByteArray out;
    out.append("MLCF", 4);
    out.append(char(1));
    out.append(offerId16.left(16).leftJustified(16, '\0', true));
    putLe(out, quint32(entries.size()), 4);
    for (const Entry &e : entries) {
        const QByteArray path = e.relativePath.toUtf8();
        out.append(char(e.isDir ? 2 : 1));
        putLe(out, e.isDir ? 0 : e.size, 8);
        putLe(out, quint64(e.mtimeMs), 8);
        putLe(out, quint16(path.size()), 2);
        out.append(path);
    }
    return out;
}

QByteArray newOfferId()
{
    QByteArray id(16, '\0');
    QRandomGenerator *rng = QRandomGenerator::system();
    for (int i = 0; i < 16; i++) {
        id[i] = char(rng->bounded(256));
    }
    return id;
}

}
