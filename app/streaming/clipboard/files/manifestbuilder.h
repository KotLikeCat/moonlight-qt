#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace ClipboardFiles {

struct Entry {
    bool isDir;
    quint64 size;
    qint64 mtimeMs;
    QString relativePath;  // NFC, '/' separated
    QString absolutePath;
};

struct Limits {
    int maxEntries = 100000;
    int maxPathBytes = 1024;
    int maxComponentUtf16 = 255;
};

struct BuildResult {
    QVector<Entry> entries;
    QStringList skipped;
    QString error;  // non-empty: do not offer
};

// Walks the selection pre-order (directories before children, children sorted by name).
// Symlinks are not followed (skipped), .DS_Store is skipped.
BuildResult buildManifest(const QStringList &topLevelPaths, const Limits &limits = {});

}
