#include "WorldData.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QtEndian>
#include <zlib.h>

namespace WorldData {

QByteArray inflateRaw(const QByteArray &data, qsizetype expected)
{
    z_stream stream{};
    if (inflateInit2(&stream, -15) != Z_OK)
        return {};
    QByteArray out(expected, Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    const auto status = ::inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    return status == Z_STREAM_END ? out : QByteArray();
}

Jar::Jar(const QString &path) : m_file(path)
{
    if (!m_file.open(QIODevice::ReadOnly) || m_file.size() < 22)
        return;
    const qint64 tail = qMin<qint64>(m_file.size(), 65557);
    m_file.seek(m_file.size() - tail);
    const auto end = m_file.read(tail);
    const auto at = end.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (at < 0 || at + 22 > end.size())
        return;
    const auto size = qFromLittleEndian<quint32>(end.constData() + at + 12);
    const auto offset = qFromLittleEndian<quint32>(end.constData() + at + 16);
    m_file.seek(offset);
    const auto directory = m_file.read(size);
    for (qsizetype p = 0; p + 46 <= directory.size();) {
        const auto *d = directory.constData() + p;
        if (qFromLittleEndian<quint32>(d) != 0x02014b50)
            break;
        const auto nameLength = qFromLittleEndian<quint16>(d + 28);
        const auto skip = 46 + nameLength + qFromLittleEndian<quint16>(d + 30) + qFromLittleEndian<quint16>(d + 32);
        if (p + 46 + nameLength > directory.size())
            break;
        m_entries.insert(QString::fromUtf8(d + 46, nameLength),
                         {qFromLittleEndian<quint16>(d + 10), qFromLittleEndian<quint32>(d + 20),
                          qFromLittleEndian<quint32>(d + 24), qFromLittleEndian<quint32>(d + 42)});
        p += skip;
    }
}

QByteArray Jar::read(const QString &name)
{
    const auto it = m_entries.constFind(name);
    if (it == m_entries.cend() || it->size > 64 * 1024 * 1024 || !m_file.seek(it->offset))
        return {};
    const auto header = m_file.read(30);
    if (header.size() < 30 || qFromLittleEndian<quint32>(header.constData()) != 0x04034b50)
        return {};
    m_file.seek(it->offset + 30 + qFromLittleEndian<quint16>(header.constData() + 26) + qFromLittleEndian<quint16>(header.constData() + 28));
    const auto data = m_file.read(it->compressed);
    if (it->method == 0)
        return data;
    return it->method == 8 ? inflateRaw(data, it->size) : QByteArray();
}

QJsonObject Assets::json(const QString &path) const
{
    return QJsonDocument::fromJson(read(path)).object();
}

QString assetPath(QString ref, const char *folder, const char *suffix)
{
    if (!ref.contains(':'))
        ref.prepend("minecraft:");
    auto path = ref.section(':', 1);
    if (!path.contains('/'))
        path.prepend("block/");
    return QStringLiteral("assets/%1/%2/%3%4").arg(ref.section(':', 0, 0), QLatin1StringView(folder), path, QLatin1StringView(suffix));
}

QString clientJar(const QString &root, const QString &version)
{
    const QDir versions(QDir(root).filePath("versions"));
    const auto exact = versions.filePath(version + '/' + version + ".jar");
    if (QFileInfo::exists(exact))
        return exact;
    // A world from a version not installed here: the newest installed client's textures.
    for (const auto &info : versions.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
        const auto jar = QDir(info.absoluteFilePath()).filePath(info.fileName() + ".jar");
        if (QFileInfo::exists(jar))
            return jar;
    }
    return {};
}

int bitsFor(qsizetype size, int least)
{
    int bits = least;
    while ((qsizetype(1) << bits) < size)
        ++bits;
    return bits;
}

QByteArray chunkData(const QByteArray &region, int index)
{
    const auto location = qFromBigEndian<quint32>(region.constData() + index * 4);
    const qsizetype offset = qsizetype(location >> 8) * 4096;
    if (offset < 8192 || offset + 5 > region.size())
        return {};
    const auto length = qFromBigEndian<quint32>(region.constData() + offset);
    const auto type = quint8(region[offset + 4]);
    if (length < 1 || offset + 4 + length > region.size() || (type != 1 && type != 2 && type != 3))
        return {}; // empty, cut short, LZ4 (4) or kept in an external .mcc file (|128)
    return Nbt::decompress(region.mid(offset + 5, length - 1));
}

QByteArray readChunk(const QString &mcaPath, int index)
{
    QFile file(mcaPath);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(index * 4))
        return {};
    const auto entry = file.read(4);
    if (entry.size() < 4)
        return {};
    const auto location = qFromBigEndian<quint32>(entry.constData());
    const qint64 offset = qint64(location >> 8) * 4096;
    const qint64 sectors = location & 0xFF;
    if (offset < 8192 || sectors == 0 || !file.seek(offset))
        return {};
    const auto data = file.read(sectors * 4096);
    if (data.size() < 5)
        return {};
    const auto length = qFromBigEndian<quint32>(data.constData());
    const auto type = quint8(data[4]);
    if (length < 1 || 4 + qint64(length) > data.size() || (type != 1 && type != 2 && type != 3))
        return {};
    return Nbt::decompress(data.mid(5, length - 1));
}

QByteArray regionHeader(const QString &mcaPath)
{
    QFile file(mcaPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto header = file.read(8192);
    return header.size() == 8192 ? header : QByteArray();
}

} // namespace WorldData
