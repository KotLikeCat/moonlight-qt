#pragma once

#include "clipboardbundle.h"

#include <QString>
#include <QStringList>

// Native NSPasteboard access for clipboard sync (macOS only).
// Not thread-safe: use one instance from one thread.
class MacPasteboard
{
public:
    // Empty name = the general pasteboard; a name gives an isolated pasteboard (tests).
    explicit MacPasteboard(const QString& name = QString());
    ~MacPasteboard();
    MacPasteboard(const MacPasteboard&) = delete;
    MacPasteboard& operator=(const MacPasteboard&) = delete;

    long changeCount() const;
    // True for password-manager style data that must never leave this Mac.
    bool hasSensitiveData() const;
    // Text, HTML, RTF and PNG (TIFF converted), in that order. Empty if the pasteboard holds files.
    QVector<ClipboardBundle::Item> read() const;
    // Local paths of file URLs on the pasteboard (empty if none).
    QStringList fileURLs() const;
    // Replaces the pasteboard contents. Returns the change count after the write.
    long write(const QVector<ClipboardBundle::Item>& items);
    void releaseForTests();

private:
    void* m_Pasteboard; // NSPasteboard*, retained
};
