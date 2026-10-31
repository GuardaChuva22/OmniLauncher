#pragma once

#include <QByteArray>
#include <QList>
#include <QVariant>

// Minecraft's NBT (Java edition, big-endian), read into Qt values: compound → QVariantMap,
// list → QVariantList, byte/short/int → int, long → qint64, float/double, string →
// QString, byte array → QByteArray, int and long arrays → QList<qint32>/QList<qint64>
// (QVariant::fromValue, so the chunk readers can take them without copying each value).
namespace Nbt {

// gzip or zlib data inflated, or the data as it is when it has neither header.
// Empty on a broken stream.
QByteArray decompress(const QByteArray &data);

// The root tag (a compound, by the format) of uncompressed NBT. Invalid on data that
// ends early or carries an unknown tag; a reader never trusts a file the game may be
// writing.
QVariant parse(const QByteArray &data, QString *rootName = nullptr);

// decompress() then parse(): a level.dat, a playerdata/<uuid>.dat.
QVariant readFile(const QString &path);

// A value down a path of compound keys ("Data", "Version", "Name"); invalid if missing.
QVariant at(const QVariant &root, std::initializer_list<const char *> path);

} // namespace Nbt
