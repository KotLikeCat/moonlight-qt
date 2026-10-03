#include "manifestbuilder.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace ClipboardFiles {

namespace {

struct Walker {
    const Limits &limits;
    BuildResult &result;

    bool fail(const QString &msg)
    {
        result.error = msg;
        return false;
    }

    // Makes the name unique within its directory level (case-insensitively), as the host requires.
    static QString uniqueName(const QString &name, QSet<QString> &seen)
    {
        QString candidate = name;
        const int dot = name.lastIndexOf(QLatin1Char('.'));
        const QString stem = dot > 0 ? name.left(dot) : name;
        const QString ext = dot > 0 ? name.mid(dot) : QString();
        for (int n = 2; seen.contains(candidate.toCaseFolded()); n++) {
            candidate = stem + QStringLiteral(" (%1)").arg(n) + ext;
        }
        seen.insert(candidate.toCaseFolded());
        return candidate;
    }

    bool add(const QFileInfo &info, const QString &rel, QSet<QString> &seen)
    {
        if (!info.isDir() && info.size() < 0) {
            result.skipped.append(info.absoluteFilePath());
            return true;
        }
        const QString name = uniqueName(info.fileName().normalized(QString::NormalizationForm_C), seen);
        if (name.size() > limits.maxComponentUtf16) {
            return fail(QStringLiteral("name too long: ") + name);
        }
        const QString path = rel.isEmpty() ? name : rel + QLatin1Char('/') + name;
        if (path.toUtf8().size() > limits.maxPathBytes) {
            return fail(QStringLiteral("path too long: ") + path);
        }
        if (result.entries.size() >= limits.maxEntries) {
            return fail(QStringLiteral("too many entries"));
        }

        const bool isDir = info.isDir();
        result.entries.append(Entry{isDir, isDir ? 0 : quint64(info.size()),
                                    info.lastModified().toMSecsSinceEpoch(), path,
                                    info.absoluteFilePath()});
        if (!isDir) {
            return true;
        }

        QDir dir(info.absoluteFilePath());
        QFileInfoList children = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::NoSort);
        QVector<QPair<QString, QFileInfo>> keyed;
        keyed.reserve(children.size());
        for (const QFileInfo &c : children) {
            keyed.append({c.fileName().normalized(QString::NormalizationForm_C), c});
        }
        std::sort(keyed.begin(), keyed.end(),
                  [](const QPair<QString, QFileInfo> &a, const QPair<QString, QFileInfo> &b) {
                      return a.first < b.first;
                  });
        QSet<QString> childSeen;
        for (const auto &child : keyed) {
            if (!visit(child.second, path, childSeen)) {
                return false;
            }
        }
        return true;
    }

    // Returns false only on a fatal (limit) error.
    bool visit(const QFileInfo &info, const QString &rel, QSet<QString> &seen)
    {
        if (info.fileName() == QLatin1String(".DS_Store")) {
            return true;
        }
        if (info.isSymLink()) {
            result.skipped.append(info.absoluteFilePath());
            return true;
        }
        if (!info.exists() || !info.isReadable() || (!info.isFile() && !info.isDir())) {
            result.skipped.append(info.absoluteFilePath());
            return true;
        }
        return add(info, rel, seen);
    }
};

}

BuildResult buildManifest(const QStringList &topLevelPaths, const Limits &limits)
{
    BuildResult result;
    Walker walker{limits, result};
    QSet<QString> topSeen;
    for (const QString &p : topLevelPaths) {
        if (!walker.visit(QFileInfo(QDir::cleanPath(p)), QString(), topSeen)) {
            result.entries.clear();
            return result;
        }
    }
    if (result.entries.isEmpty()) {
        result.error = QStringLiteral("no files");
    }
    return result;
}

}
