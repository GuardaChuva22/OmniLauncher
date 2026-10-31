#pragma once

#include "Nbt.h"

#include <QFile>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// Reading what the game leaves on disk: its jars (block models, textures, biomes) and the
// region files of a world. Shared by the world list, the map's colours and its 3D view.
namespace WorldData {

QByteArray inflateRaw(const QByteArray &data, qsizetype expected);

// A jar read in place: its central directory indexed, members inflated on demand. Not
// thread-safe (one file position).
class Jar
{
public:
    explicit Jar(const QString &path);

    QStringList names() const { return m_entries.keys(); }
    bool contains(const QString &name) const { return m_entries.contains(name); }
    QByteArray read(const QString &name);

private:
    struct Entry
    {
        quint16 method;
        quint32 compressed, size, offset;
    };
    QFile m_file;
    QHash<QString, Entry> m_entries;
};

// Assets looked up through a mod jar first, then the client's (where models' parents are).
struct Assets
{
    QList<Jar *> jars;

    QByteArray read(const QString &path) const
    {
        for (auto *jar : jars)
            if (jar->contains(path))
                return jar->read(path);
        return {};
    }
    QJsonObject json(const QString &path) const;
};

// "create:block/zinc_ore" → assets/create/<folder>/block/zinc_ore<suffix>.
QString assetPath(QString ref, const char *folder, const char *suffix);

// The client jar of a version; the newest installed one when that version isn't here.
QString clientJar(const QString &root, const QString &version);

// Packed values of a heightmap or a palette index array: each long holds 64/bits values,
// none crossing into the next long.
inline int packed(const QList<qint64> &longs, int index, int bits)
{
    const int perLong = 64 / bits;
    const auto at = index / perLong;
    if (at >= longs.size())
        return 0;
    return int((quint64(longs[at]) >> ((index % perLong) * bits)) & ((1ull << bits) - 1));
}

int bitsFor(qsizetype size, int least);

// One chunk of a region file held in memory, decompressed; empty if it has none.
QByteArray chunkData(const QByteArray &region, int index);

// One chunk read straight from a region file (its header entry, then its sectors).
QByteArray readChunk(const QString &mcaPath, int index);

// The 8 KiB header of a region file: where each chunk is, and when it was last saved.
QByteArray regionHeader(const QString &mcaPath);
inline bool hasChunk(const QByteArray &header, int index)
{
    return header.size() >= 8192 && (quint32(quint8(header[index * 4])) | quint8(header[index * 4 + 1]) | quint8(header[index * 4 + 2])) != 0;
}
inline quint32 chunkStamp(const QByteArray &header, int index)
{
    if (header.size() < 8192)
        return 0;
    const auto *p = reinterpret_cast<const uchar *>(header.constData() + 4096 + index * 4);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3];
}

template<typename Visit>
void eachChunk(const QString &mcaPath, Visit visit)
{
    QFile file(mcaPath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto region = file.readAll();
    if (region.size() < 8192)
        return;
    for (int index = 0; index < 1024; ++index) {
        const auto chunk = Nbt::parse(chunkData(region, index));
        if (chunk.isValid())
            visit(index, chunk);
    }
}

} // namespace WorldData
