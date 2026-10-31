#include "OmniLink.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace {

constexpr quint8 Hello = 1, Status = 2, State = 3, Blocks = 4, Model = 5;
// Chunks of live blocks kept for a map opened later.
constexpr size_t MaxChunks = 2048;
constexpr qint64 MaxFrame = 4 * 1024 * 1024;

// The payload of one frame; reading past its end only marks it failed.
class Reader
{
public:
    explicit Reader(const QByteArray &data) : m_data(data) {}
    bool ok() const { return m_ok; }

    template<typename T> T number()
    {
        if (!need(sizeof(T)))
            return T{};
        const auto v = qFromBigEndian<T>(m_data.constData() + m_pos);
        m_pos += sizeof(T);
        return v;
    }
    bool boolean() { return number<quint8>() != 0; }
    float real32() { const auto bits = number<quint32>(); float f; std::memcpy(&f, &bits, 4); return f; }
    double real64() { const auto bits = number<quint64>(); double d; std::memcpy(&d, &bits, 8); return d; }
    QString text()
    {
        const auto size = number<quint16>();
        if (!need(size))
            return {};
        // Modified UTF-8; plain UTF-8 reads ids and folder names right.
        const auto value = QString::fromUtf8(m_data.constData() + m_pos, size);
        m_pos += size;
        return value;
    }

private:
    bool need(qsizetype bytes)
    {
        if (m_pos + bytes > m_data.size())
            m_ok = false;
        return m_ok;
    }

    const QByteArray &m_data;
    qsizetype m_pos = 0;
    bool m_ok = true;
};

} // namespace

OmniLink::OmniLink(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &OmniLink::accept);
}

QString OmniLink::agentJar()
{
    const auto jar = QDir(QCoreApplication::applicationDirPath()).filePath("omni-agent.jar");
    return QFileInfo::exists(jar) ? jar : QString();
}

QString OmniLink::prepare(const QString &mappingsFile)
{
    if (!m_server.isListening() && !m_server.listen(QHostAddress::LocalHost))
        return {};
    QByteArray token(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(token.data()), token.size() / 4);
    m_token = token.toHex();
    auto argument = QStringLiteral("port=%1,token=%2").arg(m_server.serverPort()).arg(QString::fromLatin1(m_token));
    if (!mappingsFile.isEmpty())
        argument += ",mappings=" + QDir::toNativeSeparators(mappingsFile);
    return argument;
}

void OmniLink::accept()
{
    while (auto *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { lost(socket); });
        // Silence or anything but the token: not the game we started.
        QTimer::singleShot(5000, socket, [this, socket] {
            if (socket != m_socket)
                socket->abort();
        });
    }
}

void OmniLink::read(QTcpSocket *socket)
{
    for (;;) {
        if (socket->bytesAvailable() < 5)
            return;
        quint32 length = 0;
        socket->peek(reinterpret_cast<char *>(&length), 4);
        length = qFromBigEndian(length);
        if (length < 1 || length > MaxFrame) {
            socket->abort();
            return;
        }
        if (socket->bytesAvailable() < 4 + qint64(length))
            return;
        socket->skip(4);
        const auto frame = socket->read(length);
        if (!handle(socket, quint8(frame[0]), frame.mid(1))) {
            socket->abort();
            return;
        }
    }
}

bool OmniLink::handle(QTcpSocket *socket, quint8 type, const QByteArray &payload)
{
    Reader in(payload);
    if (socket != m_socket) {
        if (type != Hello)
            return false;
        const auto protocol = in.number<quint16>();
        const auto token = in.text().toLatin1();
        if (!in.ok() || protocol != Protocol || m_token.isEmpty() || token != m_token)
            return false;
        // The game we started: it takes the place of any earlier one.
        if (m_socket)
            m_socket->abort();
        m_socket = socket;
        m_status.clear();
        emit connectedChanged();
        return true;
    }

    if (type == Status) {
        m_status = in.text();
        emit connectedChanged();
        return in.ok();
    }
    if (type == Blocks) {
        const auto dimension = in.text();
        const auto cx = in.number<qint32>(), cz = in.number<qint32>();
        if (!in.ok())
            return false;
        const qint64 key = (qint64(cx) << 32) | quint32(cz);
        auto &chunks = m_blocks[dimension];
        if (!chunks.contains(key))
            m_blockOrder.emplace_back(dimension, key);
        chunks.insert(key, payload);
        while (m_blockOrder.size() > MaxChunks) {
            m_blocks[m_blockOrder.front().first].remove(m_blockOrder.front().second);
            m_blockOrder.pop_front();
        }
        emit blocksReceived(dimension, payload);
        return true;
    }
    if (type == Model) {
        const auto id = in.text();
        if (!in.ok() || id.isEmpty())
            return false;
        emit modelReceived(id, payload);
        return true;
    }
    if (type != State)
        return true; // newer frames an older launcher doesn't know

    const auto world = in.text();
    const auto dimension = in.text();
    const bool hasPlayer = in.boolean();
    const auto x = in.real64(), y = in.real64(), z = in.real64();
    const auto yaw = in.real32();
    const auto count = in.number<quint32>();
    if (!in.ok() || count > MaxFrame / 18)
        return false;
    QVariantList entities;
    entities.reserve(count);
    for (quint32 i = 0; i < count && in.ok(); ++i) {
        const auto ex = in.real32(), ey = in.real32(), ez = in.real32(), eyaw = in.real32();
        const bool hostile = in.boolean();
        entities.append(QVariantMap{{"x", ex}, {"y", ey}, {"z", ez}, {"yaw", eyaw}, {"id", in.text()}, {"hostile", hostile}});
    }
    if (!in.ok())
        return false;
    if (world != m_world) {
        m_blocks.clear();
        m_blockOrder.clear();
    }
    m_world = world;
    m_dimension = dimension;
    m_player = hasPlayer ? QVariantMap{{"x", x}, {"y", y}, {"z", z}, {"yaw", yaw}, {"dimension", dimension}} : QVariantMap();
    m_entities = std::move(entities);
    emit updated();
    return true;
}

bool OmniLink::onLocalServer(int port) const
{
    return m_world.startsWith('@') && isLocalServer(m_world.mid(1), port);
}

bool OmniLink::isLocalServer(const QString &address, int port)
{
    // "host", "host:port", "[v6]:port"; no port is the game's 25565.
    QString host = address.trimmed();
    int joined = 25565;
    if (host.startsWith('[')) {
        const auto close = host.indexOf(']');
        if (close < 0)
            return false;
        if (host.mid(close + 1).startsWith(':'))
            joined = host.mid(close + 2).toInt();
        host = host.mid(1, close - 1);
    } else if (host.count(':') == 1) {
        joined = host.section(':', 1).toInt();
        host = host.section(':', 0, 0);
    }
    if (joined != port)
        return false;
    if (host.isEmpty() || host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress ip(host);
    if (ip.isNull())
        return false;
    if (ip.isLoopback() || ip == QHostAddress(QHostAddress::AnyIPv4) || ip == QHostAddress(QHostAddress::AnyIPv6))
        return true;
    const auto own = QNetworkInterface::allAddresses();
    return std::any_of(own.cbegin(), own.cend(), [&ip](const QHostAddress &mine) { return mine.isEqual(ip); });
}

void OmniLink::lost(QTcpSocket *socket)
{
    socket->deleteLater();
    if (socket != m_socket)
        return;
    m_socket = nullptr;
    m_status.clear();
    m_world.clear();
    m_dimension.clear();
    m_player.clear();
    m_entities.clear();
    m_blocks.clear();
    m_blockOrder.clear();
    emit connectedChanged();
    emit updated();
}
