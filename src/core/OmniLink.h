#pragma once

#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVariantMap>

#include <deque>

// The launcher's end of the Omni agent (agent/), which the launcher hands to the game
// with -javaagent: a TCP server on 127.0.0.1 only, and a new random token for every
// launch; a connection that doesn't open with Hello and that token is dropped.
// Frames: u32 length, u8 type, payload (big endian, strings as Java's writeUTF).
//   1 Hello  {u16 protocol, token, names ("mojang"/"obfuscated"), java version}
//   2 Status {text}: "waiting for the game", "ready", "unsupported: …"
//   3 State  {world (its folder in singleplayer, "@" and the address on a server),
//             dimension, bool player, f64 x y z,
//             f32 yaw, u32 count, count × {f32 x y z, f32 body yaw, bool hostile,
//             type id}}
//   4 Blocks {dimension, i32 chunk x z, i32 lowest section, u16 count, count × {u16
//             palette size, the palette as block states, 4096 indices (u8, or u16 past
//             256) when it holds more than one}}: a chunk as the game has it now, sent
//             when it comes near the player and when a block in it changes
//             and its sky and block light, each u8 kind: 0 none, 1 one level (u8),
//             2 2048 bytes of 4-bit levels} (World3D::LiveChunk reads it)
//   5 Model  {type id, u32 size, PNG, u32 quads, quads × 4 × f32 x y z u v}: how a kind
//             of mob looks, sent once a connection as it is first seen (EntityModels)
class OmniLink final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString status READ status NOTIFY connectedChanged)
    Q_PROPERTY(QString world READ world NOTIFY updated)
    Q_PROPERTY(QString dimension READ dimension NOTIFY updated)
    // {x, y, z, yaw, dimension}, empty without a player
    Q_PROPERTY(QVariantMap player READ player NOTIFY updated)
    // [{x, y, z, yaw, id, hostile}], the living entities near the player
    Q_PROPERTY(QVariantList entities READ entities NOTIFY updated)

public:
    static constexpr int Protocol = 2;

    explicit OmniLink(QObject *parent = nullptr);

    // omni-agent.jar beside the launcher; "" when this build has none.
    static QString agentJar();
    // The agent's argument for a game about to start (a new token each time); "" if the
    // server can't listen. `mappingsFile`: Mojang's names → the game's, for an obfuscated
    // version; "" when the game runs on Mojang's names.
    QString prepare(const QString &mappingsFile);

    bool connected() const { return m_socket != nullptr; }
    QString status() const { return m_status; }
    QString world() const { return m_world; }
    QString dimension() const { return m_dimension; }
    QVariantMap player() const { return m_player; }
    // Whether the game is on a server of this computer listening on `port` (one this
    // launcher hosts): joined by localhost, a loopback address or one of its own.
    Q_INVOKABLE bool onLocalServer(int port) const;
    static bool isLocalServer(const QString &address, int port);
    QVariantList entities() const { return m_entities; }
    // The latest Blocks payload of each chunk of the world the game has open, by
    // dimension and chunk ((x << 32) | z); let go with the world or the game.
    QHash<qint64, QByteArray> blocks(const QString &dimension) const { return m_blocks.value(dimension); }

signals:
    void connectedChanged();
    void updated();
    void blocksReceived(const QString &dimension, const QByteArray &payload);
    void modelReceived(const QString &id, const QByteArray &payload);

private:
    void accept();
    void read(QTcpSocket *socket);
    bool handle(QTcpSocket *socket, quint8 type, const QByteArray &payload);
    void lost(QTcpSocket *socket);

    QTcpServer m_server;
    QByteArray m_token;
    QPointer<QTcpSocket> m_socket; // the greeted one
    QString m_status;
    QString m_world;
    QString m_dimension;
    QVariantMap m_player;
    QVariantList m_entities;
    QHash<QString, QHash<qint64, QByteArray>> m_blocks;
    std::deque<std::pair<QString, qint64>> m_blockOrder; // oldest first
};
