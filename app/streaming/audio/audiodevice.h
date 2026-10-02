#pragma once

#include <QString>
#include <QStringList>

namespace AudioDevice
{
    // Returns `preferred` if it is non-empty and present in `available`
    // (exact match), otherwise an empty string (= system default device).
    QString resolve(const QStringList& available, const QString& preferred);
}
