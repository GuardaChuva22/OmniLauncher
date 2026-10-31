#include "Nbt.h"

#include <QFile>
#include <QtEndian>
#include <cstring>
#include <zlib.h>

namespace {

enum Tag : quint8 { End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray };

class Reader
{
public:
    explicit Reader(const QByteArray &data) : m_data(data) {}

    bool failed() const { return m_failed; }

    QVariant value(quint8 tag, int depth)
    {
        // The format caps nesting at 512; deeper data is broken or hostile.
        if (m_failed || depth > 512)
            return fail();
        switch (tag) {
        case Byte: return need(1) ? QVariant(int(qint8(m_data[m_pos++]))) : fail();
        case Short: return number<qint16>();
        case Int: return number<qint32>();
        case Long: return QVariant::fromValue(raw<qint64>());
        case Float: { const auto bits = raw<quint32>(); float f; std::memcpy(&f, &bits, 4); return m_failed ? fail() : QVariant(f); }
        case Double: { const auto bits = raw<quint64>(); double d; std::memcpy(&d, &bits, 8); return m_failed ? fail() : QVariant(d); }
        case ByteArray: {
            const auto size = raw<qint32>();
            if (size < 0 || !need(size))
                return fail();
            const auto bytes = m_data.mid(m_pos, size);
            m_pos += size;
            return bytes;
        }
        case String: return string();
        case List: {
            const auto type = raw<quint8>();
            const auto size = raw<qint32>();
            if (m_failed || size < 0 || (size > 0 && type == End) || type > LongArray)
                return fail();
            QVariantList list;
            list.reserve(qMin(size, 1 << 16));
            for (qint32 i = 0; i < size && !m_failed; ++i) {
                auto item = value(type, depth + 1);
                // A list mixing types (1.21.5+) is written as compounds, each value that is
                // not one wrapped in a compound with the single key "".
                if (type == Compound) {
                    const auto map = item.toMap();
                    if (map.size() == 1 && map.contains(QString()))
                        item = map.value(QString());
                }
                list.append(item);
            }
            return m_failed ? fail() : QVariant(list);
        }
        case Compound: {
            QVariantMap map;
            for (;;) {
                const auto type = raw<quint8>();
                if (m_failed || type > LongArray)
                    return fail();
                if (type == End)
                    return map;
                const auto key = string();
                map.insert(key, value(type, depth + 1));
            }
        }
        case IntArray: return array<qint32>();
        case LongArray: return array<qint64>();
        default: return fail();
        }
    }

    QString string()
    {
        const auto size = raw<quint16>();
        if (m_failed || !need(size))
            return fail(), QString();
        // Modified UTF-8; plain UTF-8 reads it right for everything a world name holds.
        const auto text = QString::fromUtf8(m_data.constData() + m_pos, size);
        m_pos += size;
        return text;
    }

    template<typename T> T raw()
    {
        if (!need(sizeof(T)))
            return fail(), T{};
        const auto v = qFromBigEndian<T>(m_data.constData() + m_pos);
        m_pos += sizeof(T);
        return v;
    }

private:
    template<typename T> QVariant number()
    {
        const auto v = raw<T>();
        return m_failed ? fail() : QVariant(int(v));
    }

    template<typename T> QVariant array()
    {
        const auto size = raw<qint32>();
        if (m_failed || size < 0 || !need(qint64(size) * sizeof(T)))
            return fail();
        QList<T> values(size);
        for (qint32 i = 0; i < size; ++i)
            values[i] = qFromBigEndian<T>(m_data.constData() + m_pos + qsizetype(i) * sizeof(T));
        m_pos += qsizetype(size) * sizeof(T);
        return QVariant::fromValue(values);
    }

    bool need(qint64 bytes)
    {
        if (bytes < 0 || m_pos + bytes > m_data.size())
            m_failed = true;
        return !m_failed;
    }

    QVariant fail()
    {
        m_failed = true;
        return {};
    }

    const QByteArray &m_data;
    qsizetype m_pos = 0;
    bool m_failed = false;
};

} // namespace

namespace Nbt {

QByteArray decompress(const QByteArray &data)
{
    const auto b0 = data.size() > 1 ? quint8(data[0]) : 0, b1 = data.size() > 1 ? quint8(data[1]) : 0;
    const bool gzip = b0 == 0x1f && b1 == 0x8b;
    const bool zlib = (b0 & 0x0f) == 8 && ((b0 << 8) | b1) % 31 == 0;
    if (!gzip && !zlib)
        return data;

    z_stream stream{};
    if (inflateInit2(&stream, 15 + 32) != Z_OK) // 32: detect gzip or zlib
        return {};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    QByteArray out;
    char buffer[64 * 1024];
    int status;
    do {
        stream.next_out = reinterpret_cast<Bytef *>(buffer);
        stream.avail_out = sizeof buffer;
        status = ::inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            inflateEnd(&stream);
            return {};
        }
        out.append(buffer, qsizetype(sizeof buffer - stream.avail_out));
        // A world file is never near this; stop a zip bomb before it fills memory.
        if (out.size() > 256 * 1024 * 1024) {
            inflateEnd(&stream);
            return {};
        }
    } while (status != Z_STREAM_END && stream.avail_in > 0);
    inflateEnd(&stream);
    return status == Z_STREAM_END ? out : QByteArray();
}

QVariant parse(const QByteArray &data, QString *rootName)
{
    Reader reader(data);
    const auto tag = reader.raw<quint8>();
    if (reader.failed() || tag != Compound)
        return {};
    const auto name = reader.string();
    auto root = reader.value(Compound, 0);
    if (reader.failed())
        return {};
    if (rootName)
        *rootName = name;
    return root;
}

QVariant readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return parse(decompress(file.readAll()));
}

QVariant at(const QVariant &root, std::initializer_list<const char *> path)
{
    QVariant current = root;
    for (const auto *key : path) {
        if (current.typeId() != QMetaType::QVariantMap)
            return {};
        const auto map = current.toMap();
        const auto it = map.constFind(QString::fromLatin1(key));
        if (it == map.cend())
            return {};
        current = *it;
    }
    return current;
}

} // namespace Nbt
