#include "audiodevice.h"

QString AudioDevice::resolve(const QStringList& available, const QString& preferred)
{
    if (preferred.isEmpty() || !available.contains(preferred, Qt::CaseSensitive)) {
        return QString();
    }
    return preferred;
}
