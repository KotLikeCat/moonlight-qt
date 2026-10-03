#pragma once

#include <QByteArray>
#include <QVector>

#include "manifestbuilder.h"

namespace ClipboardFiles {

// MLCF v1 manifest, little-endian.
QByteArray encodeManifest(const QByteArray &offerId16, const QVector<Entry> &entries);

// 16 random bytes from the system RNG.
QByteArray newOfferId();

}
