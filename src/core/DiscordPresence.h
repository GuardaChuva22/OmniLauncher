#pragma once

#include <QJsonObject>
#include <QObject>

class QLocalSocket;

// Discord Rich Presence over Discord's local IPC pipe (\\.\pipe\discord-ipc-N): a
// handshake with the app's client ID, then SET_ACTIVITY frames. No SDK needed; when
// Discord is closed, activity calls are simply dropped.
class DiscordPresence final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

public:
    explicit DiscordPresence(QObject *parent = nullptr);

    bool configured() const { return !m_clientId.isEmpty(); }
    bool connected() const { return m_ready; }

    // startEpoch: seconds since 1970 when the session began (shown as elapsed time).
    Q_INVOKABLE void setActivity(const QString &details, const QString &state, qint64 startEpoch,
                                 const QString &imageUrl = {});
    Q_INVOKABLE void clearActivity();

signals:
    void connectedChanged();

private:
    bool ensureConnected();
    void send(int opcode, const QJsonObject &payload);
    void onReadyRead();
    void sendActivity();

    QString m_clientId;
    QLocalSocket *m_socket = nullptr;
    QByteArray m_buffer;
    bool m_ready = false;
    QJsonObject m_activity; // empty = cleared
    bool m_hasPending = false;
};
