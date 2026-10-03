#include "manifestbuilder.h"

#include <QDir>
#include <QFileInfo>
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

    bool add(const QFileInfo &info, const QString &rel)
    {
        const QString name = info.fileName().normalized(QString::NormalizationForm_C);
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
        std::sort(children.begin(), children.end(), [](const QFileInfo &a, const QFileInfo &b) {
            return a.fileName().normalized(QString::NormalizationForm_C) <
                   b.fileName().normalized(QString::NormalizationForm_C);
        });
        for (const QFileInfo &child : children) {
            if (!visit(child, path)) {
                return false;
            }
        }
        return true;
    }

    // Returns false only on a fatal (limit) error.
    bool visit(const QFileInfo &info, const QString &rel)
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
        return add(info, rel);
    }
};

}

BuildResult buildManifest(const QStringList &topLevelPaths, const Limits &limits)
{
    BuildResult result;
    Walker walker{limits, result};
    for (const QString &p : topLevelPaths) {
        if (!walker.visit(QFileInfo(p), QString())) {
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
