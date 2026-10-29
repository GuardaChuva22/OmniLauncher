#include "DiscordPresence.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QUuid>
#include <QtEndian>

#ifndef OMNI_DISCORD_CLIENT_ID
#  define OMNI_DISCORD_CLIENT_ID ""
#endif

namespace {
enum Opcode { Handshake = 0, Frame = 1, Close = 2 };
const QString kDefaultImage = QStringLiteral("https://raw.githubusercontent.com/GuardaChuva22/OmniLauncher/main/assets/brand/app-icon.png");
}

DiscordPresence::DiscordPresence(QObject *parent)
    : QObject(parent)
    , m_clientId(qEnvironmentVariable("OMNI_DISCORD_CLIENT_ID", QStringLiteral(OMNI_DISCORD_CLIENT_ID)).trimmed())
{
}

bool DiscordPresence::ensureConnected()
{
    if (!configured())
        return false;
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState)
        return true;
    if (!m_socket) {
        m_socket = new QLocalSocket(this);
        connect(m_socket, &QLocalSocket::readyRead, this, &DiscordPresence::onReadyRead);
        connect(m_socket, &QLocalSocket::disconnected, this, [this] {
            m_ready = false;
            m_buffer.clear();
            emit connectedChanged();
        });
    }
    // Discord listens on the first free pipe among discord-ipc-0..9.
    for (int i = 0; i < 10; ++i) {
        m_socket->connectToServer(QStringLiteral("discord-ipc-%1").arg(i));
        if (m_socket->waitForConnected(150)) {
            send(Handshake, {{"v", 1}, {"client_id", m_clientId}});
            return true;
        }
        m_socket->abort();
    }
    return false;
}

void DiscordPresence::send(int opcode, const QJsonObject &payload)
{
    const auto body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray frame(8, Qt::Uninitialized);
    qToLittleEndian<qint32>(opcode, frame.data());
    qToLittleEndian<qint32>(qint32(body.size()), frame.data() + 4);
    m_socket->write(frame + body);
    m_socket->flush();
}

void DiscordPresence::onReadyRead()
{
    m_buffer += m_socket->readAll();
    while (m_buffer.size() >= 8) {
        const auto opcode = qFromLittleEndian<qint32>(m_buffer.constData());
        const auto length = qFromLittleEndian<qint32>(m_buffer.constData() + 4);
        if (m_buffer.size() < 8 + length)
            return;
        const auto message = QJsonDocument::fromJson(m_buffer.mid(8, length)).object();
        m_buffer.remove(0, 8 + length);
        if (opcode == Close) {
            m_socket->abort();
            return;
        }
        if (message.value("evt").toString() == "READY" && !m_ready) {
            m_ready = true;
            emit connectedChanged();
            if (m_hasPending)
                sendActivity();
        }
    }
}

void DiscordPresence::sendActivity()
{
    if (!m_ready)
        return;
    QJsonObject args{{"pid", qint64(QCoreApplication::applicationPid())}};
    args.insert("activity", m_activity.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m_activity));
    send(Frame, {{"cmd", "SET_ACTIVITY"}, {"args", args},
                 {"nonce", QUuid::createUuid().toString(QUuid::WithoutBraces)}});
    m_hasPending = false;
}

void DiscordPresence::setActivity(const QString &details, const QString &state, qint64 startEpoch, const QString &imageUrl)
{
    QJsonObject assets{{"large_image", imageUrl.startsWith("https://") ? imageUrl : kDefaultImage},
                       {"large_text", details}, {"small_image", kDefaultImage}, {"small_text", "OmniLauncher"}};
    m_activity = {{"details", details.left(128)}, {"state", state.left(128)},
                  {"timestamps", QJsonObject{{"start", startEpoch}}}, {"assets", assets}};
    m_hasPending = true;
    if (ensureConnected())
        sendActivity();
}

void DiscordPresence::clearActivity()
{
    m_activity = {};
    m_hasPending = true;
    if (m_ready)
        sendActivity();
}
