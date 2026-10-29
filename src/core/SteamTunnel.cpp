#include "SteamTunnel.h"

#include <QHash>
#include <QPointer>
#include <QSettings>
#include <QTimer>

#ifdef OMNI_STEAM
#  include <QBuffer>
#  include <QCoreApplication>
#  include <QDir>
#  include <QElapsedTimer>
#  include <QFileInfo>
#  include <QHostAddress>
#  include <QImage>
#  include <QTcpServer>
#  include <QTcpSocket>

#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>

#  include <steam/steam_api.h>
#  include <steam/isteamnetworkingsockets.h>
#  include <steam/isteamnetworkingutils.h>

#  include <algorithm>
#  include <utility>
#endif

namespace {
#ifdef OMNI_STEAM
const char *const kConnectPrefix = "+omni_host ";
// Above this much unacknowledged data on a Steam connection, stop reading the game's TCP
// side: the socket's buffer fills and TCP slows the sender down (backpressure).
const int kSteamBacklog = 2 * 1024 * 1024;
// Above this much waiting to be written to the local TCP socket, stop taking messages
// from Steam: its receive buffer fills and the relay slows the other side down.
const qint64 kTcpBacklog = 8 * 1024 * 1024;
const qint64 kChunk = 256 * 1024;

QString steamText(const char *text)
{
    return QString::fromUtf8(text ? text : "");
}
#endif
} // namespace

#ifdef OMNI_STEAM

// A Minecraft TCP connection carried over one Steam connection.
struct Link
{
    QPointer<QTcpSocket> tcp;
    bool hosted = false;     // accepted from a friend (this PC hosts)
    bool connected = false;  // the Steam side is up
    bool tcpUp = false;      // the local TCP side is up
    QByteArray toTcp;        // host: what the friend sent before the server's socket connected
};

struct SteamTunnel::Impl
{
    explicit Impl(SteamTunnel *owner)
        : q(owner)
    {
    }

    SteamTunnel *q;
    HSteamListenSocket listen = k_HSteamListenSocket_Invalid;
    HSteamListenSocket listenIp = k_HSteamListenSocket_Invalid; // OMNI_STEAM_LOOPBACK test mode
    int serverPort = 0;
    QHash<HSteamNetConnection, Link> links;
    // Joined: the local port the game connects to, and a control connection that checks the
    // host is reachable first and then stays open for the ping and to notice the host leaving.
    QTcpServer *local = nullptr;
    SteamNetworkingIdentity hostIdentity;
    bool loopback = false;
    HSteamNetConnection probe = k_HSteamNetConnection_Invalid;
    QHash<QString, QString> avatars;
    QElapsedTimer sinceSlow, sinceFriends;

    STEAM_CALLBACK(Impl, onConnectionStatus, SteamNetConnectionStatusChangedCallback_t);
    STEAM_CALLBACK(Impl, onJoinRequested, GameRichPresenceJoinRequested_t);
    STEAM_CALLBACK(Impl, onRichPresence, FriendRichPresenceUpdate_t);
    STEAM_CALLBACK(Impl, onPersona, PersonaStateChange_t);

    ISteamNetworkingSockets *sockets() const { return SteamNetworkingSockets(); }

    HSteamNetConnection connectToHost()
    {
        if (loopback) {
            SteamNetworkingIPAddr address;
            address.SetIPv4(0x7F000001, 27120);
            return sockets()->ConnectByIPAddress(address, 0, nullptr);
        }
        return sockets()->ConnectP2P(hostIdentity, 0, 0, nullptr);
    }

    void openServerSocket(HSteamNetConnection connection)
    {
        auto &link = links[connection];
        auto *tcp = new QTcpSocket(q);
        link.tcp = tcp;
        tcp->setReadBufferSize(kChunk * 2);
        QObject::connect(tcp, &QTcpSocket::connected, q, [this, connection] {
            auto it = links.find(connection);
            if (it == links.end())
                return;
            it->tcpUp = true;
            it->tcp->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            it->tcp->write(it->toTcp);
            it->toTcp.clear();
            q->updatePlayers();
        });
        wireSocket(connection, tcp);
        tcp->connectToHost(QHostAddress::LocalHost, quint16(serverPort));
    }

    void wireSocket(HSteamNetConnection connection, QTcpSocket *tcp)
    {
        QObject::connect(tcp, &QTcpSocket::readyRead, q, [this, connection] { fromTcp(connection); });
        QObject::connect(tcp, &QTcpSocket::disconnected, q, [this, connection] {
            fromTcp(connection);
            closeLink(connection, "tcp closed", true);
        });
        QObject::connect(tcp, &QTcpSocket::errorOccurred, q, [this, connection](QAbstractSocket::SocketError error) {
            if (error == QAbstractSocket::RemoteHostClosedError)
                return; // `disconnected` follows
            closeLink(connection, error == QAbstractSocket::ConnectionRefusedError ? "server not running" : "tcp error", true);
        });
    }

    // Game/server bytes → Steam, as fast as the connection drains.
    void fromTcp(HSteamNetConnection connection)
    {
        auto it = links.find(connection);
        if (it == links.end() || !it->connected || !it->tcp)
            return;
        while (it->tcp->bytesAvailable() > 0) {
            SteamNetConnectionRealTimeStatus_t status;
            if (sockets()->GetConnectionRealTimeStatus(connection, &status, 0, nullptr) != k_EResultOK
                || status.m_cbPendingReliable > kSteamBacklog)
                return; // pump() comes back for the rest
            const auto data = it->tcp->read(kChunk);
            if (data.isEmpty())
                return;
            const auto result = sockets()->SendMessageToConnection(connection, data.constData(), uint32(data.size()),
                                                                   k_nSteamNetworkingSend_Reliable, nullptr);
            if (result != k_EResultOK) {
                closeLink(connection, "steam send failed", false);
                return;
            }
        }
    }

    // Steam → game/server.
    void fromSteam(HSteamNetConnection connection)
    {
        auto it = links.find(connection);
        if (it == links.end())
            return;
        if (it->tcp && it->tcp->bytesToWrite() > kTcpBacklog)
            return;
        SteamNetworkingMessage_t *messages[64];
        const int count = sockets()->ReceiveMessagesOnConnection(connection, messages, 64);
        for (int i = 0; i < count; ++i) {
            it = links.find(connection);
            if (it != links.end()) {
                const auto *bytes = static_cast<const char *>(messages[i]->m_pData);
                if (it->hosted && !it->tcp)
                    openServerSocket(connection); // the first byte from a friend opens the server's socket
                it = links.find(connection);
                if (it->tcpUp)
                    it->tcp->write(bytes, messages[i]->m_cbSize);
                else
                    it->toTcp.append(bytes, messages[i]->m_cbSize);
            }
            messages[i]->Release();
        }
    }

    void closeLink(HSteamNetConnection connection, const char *reason, bool linger)
    {
        auto it = links.find(connection);
        if (it == links.end())
            return;
        const auto tcp = it->tcp;
        const bool hosted = it->hosted;
        links.erase(it);
        sockets()->CloseConnection(connection, k_ESteamNetConnectionEnd_App_Min, reason, linger);
        if (tcp) {
            tcp->disconnect(q);
            tcp->disconnectFromHost(); // writes what is buffered first
            tcp->deleteLater();
        }
        if (hosted)
            q->updatePlayers();
    }

    void closeAll(bool hosted)
    {
        const auto connections = links.keys();
        for (const auto connection : connections)
            if (links.value(connection).hosted == hosted)
                closeLink(connection, hosted ? "host stopped sharing" : "left", false);
    }
};

void SteamTunnel::Impl::onConnectionStatus(SteamNetConnectionStatusChangedCallback_t *event)
{
    const auto connection = event->m_hConn;
    const auto &info = event->m_info;
    const bool incoming = info.m_hListenSocket != k_HSteamListenSocket_Invalid;

    if (connection == probe) {
        if (info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
            // How Steam got through: its relays (SDR), straight to the friend's PC, or this PC.
            const auto flags = info.m_nFlags;
            const auto route = flags & k_nSteamNetworkConnectionInfoFlags_LoopbackBuffers ? "local"
                : flags & k_nSteamNetworkConnectionInfoFlags_Relayed ? "relay" : "direct";
            q->updateJoined({{"state", "ready"}, {"route", QString::fromLatin1(route)}});
        } else if (info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer
                   || info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
            const bool wasReady = q->m_joined.value("state") == "ready";
            sockets()->CloseConnection(connection, 0, nullptr, false);
            probe = k_HSteamNetConnection_Invalid;
            const auto error = wasReady ? tr("O servidor do amigo saiu da Steam.")
                                        : info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer
                                            ? tr("O amigo não está compartilhando esse servidor agora.")
                                            : tr("Não deu para alcançar o amigo pela Steam (%1).").arg(steamText(info.m_szEndDebug));
            q->updateJoined({{"state", "failed"}, {"error", error}});
            emit q->message(error);
        }
        return;
    }

    switch (info.m_eState) {
    case k_ESteamNetworkingConnectionState_Connecting:
        if (incoming) {
            // Only Steam friends, and only while a server is shared.
            const auto remote = info.m_identityRemote.GetSteamID();
            const bool friendOk = info.m_hListenSocket == listenIp || remote == SteamUser()->GetSteamID()
                || SteamFriends()->GetFriendRelationship(remote) == k_EFriendRelationshipFriend;
            if (q->m_hosting.isEmpty() || !friendOk || sockets()->AcceptConnection(connection) != k_EResultOK) {
                sockets()->CloseConnection(connection, k_ESteamNetConnectionEnd_App_Min + 1, "not allowed", false);
                return;
            }
            Link link;
            link.hosted = true;
            links.insert(connection, link);
        }
        break;
    case k_ESteamNetworkingConnectionState_Connected: {
        auto it = links.find(connection);
        if (it == links.end())
            break;
        it->connected = true;
        if (it->hosted)
            q->updatePlayers();
        else
            fromTcp(connection);
        break;
    }
    case k_ESteamNetworkingConnectionState_ClosedByPeer:
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        fromSteam(connection); // what arrived before the close
        if (links.contains(connection))
            closeLink(connection, "closed", false);
        else
            sockets()->CloseConnection(connection, 0, nullptr, false);
        break;
    default:
        break;
    }
}

void SteamTunnel::Impl::onJoinRequested(GameRichPresenceJoinRequested_t *event)
{
    const auto connect = steamText(event->m_rgchConnect);
    if (!connect.startsWith(QString::fromLatin1(kConnectPrefix)))
        return;
    const auto hostId = connect.mid(int(qstrlen(kConnectPrefix))).trimmed();
    emit q->joinRequested(hostId, steamText(SteamFriends()->GetFriendPersonaName(CSteamID(hostId.toULongLong()))));
}

void SteamTunnel::Impl::onRichPresence(FriendRichPresenceUpdate_t *event)
{
    // Only marks the list: rebuilding it here with new presence requests made Steam answer with
    // another update, forever, as soon as a friend had the launcher open (the launcher froze).
    q->m_friendsDirty = true;
    const auto id = QString::number(event->m_steamIDFriend.ConvertToUint64());
    if (q->m_joined.value("hostId") == id)
        q->updateJoined({});
}

void SteamTunnel::Impl::onPersona(PersonaStateChange_t *)
{
    q->m_friendsDirty = true;
}

#else
struct SteamTunnel::Impl
{
};
#endif

SteamTunnel::SteamTunnel(QObject *parent)
    : QObject(parent)
{
#ifdef OMNI_STEAM
    m_pump = new QTimer(this);
    m_pump->setTimerType(Qt::PreciseTimer);
    m_pump->setInterval(100);
    connect(m_pump, &QTimer::timeout, this, &SteamTunnel::pump);
    m_pump->start();
    // Steam opened after the launcher: try again now and then while enabled.
    auto *retry = new QTimer(this);
    retry->setInterval(15000);
    connect(retry, &QTimer::timeout, this, [this] {
        if (m_enabled && !m_ready)
            tryInit();
    });
    retry->start();
#endif
}

void SteamTunnel::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
#ifdef OMNI_STEAM
    if (enabled) {
        tryInit();
    } else if (d) {
        leave();
        stopHosting();
        d.reset();
        SteamAPI_Shutdown();
        m_ready = false;
        m_personaName.clear();
        m_friends.clear();
        emit friendsChanged();
        emit readyChanged();
    }
#endif
    emit enabledChanged();
}

SteamTunnel::~SteamTunnel()
{
#ifdef OMNI_STEAM
    if (d) {
        leave();
        stopHosting();
        d.reset();
        SteamAPI_Shutdown();
    }
#endif
}

bool SteamTunnel::available() const
{
#ifdef OMNI_STEAM
    // Delay-loaded: without the DLL beside the exe, calling Steam would crash. Only checked
    // here; the DLL is loaded once the connection is turned on (tryInit).
    static const bool found = QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("steam_api64.dll")));
    return found;
#else
    return false;
#endif
}

void SteamTunnel::tryInit()
{
#ifdef OMNI_STEAM
    if (m_ready || !m_enabled || !available())
        return;
    const auto dll = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("steam_api64.dll"));
    if (!LoadLibraryW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(dll).utf16())))
        return;
    // The App ID through the environment, so no steam_appid.txt is needed beside the exe.
    qputenv("SteamAppId", QByteArray::number(OMNI_STEAM_APP_ID));
    qputenv("SteamGameId", QByteArray::number(OMNI_STEAM_APP_ID));
    SteamErrMsg error{};
    if (SteamAPI_InitEx(&error) != k_ESteamAPIInitResult_OK)
        return; // Steam closed or signed out: tried again later
    auto *utils = SteamNetworkingUtils();
    utils->InitRelayNetworkAccess();
    // The defaults (256 KB/s) are for game traffic; joining a world streams chunks.
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMin, 512 * 1024);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMax, 16 * 1024 * 1024);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendBufferSize, 8 * 1024 * 1024);
    // Testing: OMNI_STEAM_RELAY_ONLY=1 skips direct (ICE) routes, so connections go through Valve's relays.
    if (qEnvironmentVariableIsSet("OMNI_STEAM_RELAY_ONLY"))
        utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable,
                                         k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Disable);
    d = std::make_unique<Impl>(this);
    d->sinceSlow.start();
    d->sinceFriends.start();
    m_ready = true;
    m_personaName = steamText(SteamFriends()->GetPersonaName());
    emit readyChanged();
    refreshFriends();
#endif
}

void SteamTunnel::pump()
{
#ifdef OMNI_STEAM
    if (!d)
        return;
    SteamAPI_RunCallbacks();
    const auto connections = d->links.keys();
    for (const auto connection : connections) {
        d->fromSteam(connection);
        d->fromTcp(connection);
    }
    // Every few ms while bytes flow (each tick is latency added to the game), lazily otherwise.
    m_pump->setInterval(d->links.isEmpty() && d->probe == k_HSteamNetConnection_Invalid ? 50 : 3);
    if (d->sinceSlow.elapsed() > 2000) {
        d->sinceSlow.restart();
        if (d->probe != k_HSteamNetConnection_Invalid) {
            SteamNetConnectionRealTimeStatus_t status;
            if (d->sockets()->GetConnectionRealTimeStatus(d->probe, &status, 0, nullptr) == k_EResultOK
                && status.m_nPing != m_joined.value("ping").toInt())
                updateJoined({{"ping", status.m_nPing}});
        }
        if (d->sinceFriends.elapsed() > 30000) {
            d->sinceFriends.restart();
            refreshFriends(); // asks Steam for fresh presence too
        } else if (m_friendsDirty) {
            rebuildFriends(false);
        }
    }
#endif
}

void SteamTunnel::updatePlayers()
{
#ifdef OMNI_STEAM
    if (m_hosting.isEmpty())
        return;
    QStringList players;
    for (auto it = d->links.begin(); it != d->links.end(); ++it) {
        if (!it->hosted || !it->tcp)
            continue; // control connections are not players
        SteamNetConnectionInfo_t info;
        if (d->sockets()->GetConnectionInfo(it.key(), &info)) {
            const auto name = steamText(SteamFriends()->GetFriendPersonaName(info.m_identityRemote.GetSteamID()));
            if (!players.contains(name))
                players.append(name);
        }
    }
    if (players != m_hosting.value("players").toStringList()) {
        m_hosting.insert("players", players);
        emit hostingChanged();
    }
#endif
}

void SteamTunnel::host(const QVariantMap &server, const QVariantMap &game)
{
#ifdef OMNI_STEAM
    if (!d)
        return;
    const auto id = server.value("id").toString();
    if (m_hosting.value("serverId") != id)
        stopHosting();
    if (d->listen == k_HSteamListenSocket_Invalid)
        d->listen = d->sockets()->CreateListenSocketP2P(0, 0, nullptr);
    if (qEnvironmentVariableIsSet("OMNI_STEAM_LOOPBACK") && d->listenIp == k_HSteamListenSocket_Invalid) {
        SteamNetworkingIPAddr address;
        address.SetIPv4(0x7F000001, 27120);
        d->listenIp = d->sockets()->CreateListenSocketIP(address, 0, nullptr);
    }
    d->serverPort = server.value("port").toInt();
    m_hosting = {{"serverId", id}, {"name", server.value("name")}, {"version", server.value("version")},
                 {"loader", server.value("loader")}, {"port", d->serverPort}, {"players", QStringList()},
                 {"game", game.value("title")}, {"pack", game.value("pack")}};
    auto *friends = SteamFriends();
    const auto me = QString::number(SteamUser()->GetSteamID().ConvertToUint64());
    friends->SetRichPresence("connect", (kConnectPrefix + me).toUtf8().constData());
    friends->SetRichPresence("status", tr("Hospedando %1 no OmniLauncher").arg(server.value("name").toString()).toUtf8().constData());
    friends->SetRichPresence("omni_server", server.value("name").toString().toUtf8().constData());
    friends->SetRichPresence("omni_version", server.value("version").toString().toUtf8().constData());
    friends->SetRichPresence("omni_loader", server.value("loader").toString().toUtf8().constData());
    friends->SetRichPresence("omni_game", game.value("title").toString().left(120).toUtf8().constData());
    friends->SetRichPresence("omni_pack", game.value("pack").toString().toUtf8().constData());
    emit hostingChanged();
    rebuildFriends(false);
#else
    Q_UNUSED(server)
    Q_UNUSED(game)
#endif
}

void SteamTunnel::stopHosting()
{
#ifdef OMNI_STEAM
    if (!d)
        return;
    d->closeAll(true);
    for (auto *socket : {&d->listen, &d->listenIp}) {
        if (*socket != k_HSteamListenSocket_Invalid)
            d->sockets()->CloseListenSocket(*socket);
        *socket = k_HSteamListenSocket_Invalid;
    }
    if (m_hosting.isEmpty())
        return;
    m_hosting.clear();
    SteamFriends()->ClearRichPresence();
    emit hostingChanged();
    rebuildFriends(false);
#endif
}

bool SteamTunnel::sharing(const QString &serverId) const
{
    return QSettings().value(QStringLiteral("steam/share/") + serverId, true).toBool();
}

void SteamTunnel::setSharing(const QString &serverId, bool enabled)
{
    QSettings().setValue(QStringLiteral("steam/share/") + serverId, enabled);
    emit sharingChanged();
}

bool SteamTunnel::invite(const QString &friendId)
{
#ifdef OMNI_STEAM
    if (!d || m_hosting.isEmpty())
        return false;
    const auto me = QString::number(SteamUser()->GetSteamID().ConvertToUint64());
    return SteamFriends()->InviteUserToGame(CSteamID(friendId.toULongLong()), (kConnectPrefix + me).toUtf8().constData());
#else
    Q_UNUSED(friendId)
    return false;
#endif
}

void SteamTunnel::join(const QString &hostId)
{
#ifdef OMNI_STEAM
    if (!d)
        return;
    // Already connected to that host: keep the tunnel (and its address) as it is.
    if (m_joined.value("hostId") == hostId && m_joined.value("state") != "failed") {
        updateJoined({});
        return;
    }
    leave();
    d->loopback = hostId == "loopback";
    if (!d->loopback) {
        d->hostIdentity.Clear();
        d->hostIdentity.SetSteamID64(hostId.toULongLong());
        SteamFriends()->RequestFriendRichPresence(CSteamID(hostId.toULongLong()));
    }
    d->local = new QTcpServer(this);
    // The same local port for a host every time, so the address saved in Minecraft's server
    // list keeps working; any free port if that one is taken.
    const auto preferred = quint16(25600 + hostId.toULongLong() % 400);
    if (!d->local->listen(QHostAddress::LocalHost, preferred) && !d->local->listen(QHostAddress::LocalHost, 0)) {
        emit message(tr("Não foi possível abrir a porta local do túnel."));
        delete d->local;
        d->local = nullptr;
        return;
    }
    connect(d->local, &QTcpServer::newConnection, this, [this] {
        while (d->local && d->local->hasPendingConnections()) {
            auto *tcp = d->local->nextPendingConnection();
            tcp->setReadBufferSize(kChunk * 2);
            tcp->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            const auto connection = d->connectToHost();
            if (connection == k_HSteamNetConnection_Invalid) {
                tcp->abort();
                tcp->deleteLater();
                continue;
            }
            Link link;
            link.tcp = tcp;
            link.tcpUp = true;
            d->links.insert(connection, link);
            d->wireSocket(connection, tcp);
        }
    });
    d->probe = d->connectToHost();
    m_joined = {{"hostId", hostId}, {"state", "connecting"}, {"ping", 0},
                {"address", QStringLiteral("127.0.0.1:%1").arg(d->local->serverPort())}};
    updateJoined({});
#else
    Q_UNUSED(hostId)
#endif
}

void SteamTunnel::leave()
{
#ifdef OMNI_STEAM
    if (!d)
        return;
    d->closeAll(false);
    if (d->probe != k_HSteamNetConnection_Invalid)
        d->sockets()->CloseConnection(d->probe, 0, "left", false);
    d->probe = k_HSteamNetConnection_Invalid;
    if (d->local) {
        d->local->close();
        d->local->deleteLater();
        d->local = nullptr;
    }
    if (!m_joined.isEmpty()) {
        m_joined.clear();
        emit joinedChanged();
    }
#endif
}

// Fills the host's name and shared server from its rich presence, then merges `changes`.
void SteamTunnel::updateJoined(const QVariantMap &changes)
{
#ifdef OMNI_STEAM
    if (m_joined.isEmpty())
        return;
    const auto hostId = m_joined.value("hostId").toString();
    if (hostId == "loopback" && !m_hosting.isEmpty()) {
        m_joined.insert("hostName", m_personaName);
        m_joined.insert("server", m_hosting.value("name"));
        m_joined.insert("version", m_hosting.value("version"));
        m_joined.insert("loader", m_hosting.value("loader"));
        m_joined.insert("game", m_hosting.value("game"));
        m_joined.insert("pack", m_hosting.value("pack"));
    } else if (hostId != "loopback") {
        const CSteamID host(hostId.toULongLong());
        auto *friends = SteamFriends();
        m_joined.insert("hostName", steamText(friends->GetFriendPersonaName(host)));
        for (const auto &[key, field] : {std::pair{"omni_server", "server"}, std::pair{"omni_version", "version"},
                                         std::pair{"omni_loader", "loader"}, std::pair{"omni_game", "game"},
                                         std::pair{"omni_pack", "pack"}}) {
            const auto value = steamText(friends->GetFriendRichPresence(host, key));
            if (!value.isEmpty())
                m_joined.insert(QString::fromLatin1(field), value);
        }
    }
    for (auto it = changes.begin(); it != changes.end(); ++it)
        m_joined.insert(it.key(), it.value());
    emit joinedChanged();
#else
    Q_UNUSED(changes)
#endif
}

void SteamTunnel::refreshFriends()
{
    rebuildFriends(true);
}

// The list from what Steam already knows; `requestPresence` also asks it for the rich presence
// of friends in the launcher, which arrives later through onRichPresence.
void SteamTunnel::rebuildFriends(bool requestPresence)
{
#ifdef OMNI_STEAM
    if (!d)
        return;
    m_friendsDirty = false;
    auto *friends = SteamFriends();
    QVariantList list;
    const int count = friends->GetFriendCount(k_EFriendFlagImmediate);
    for (int i = 0; i < count; ++i) {
        const auto id = friends->GetFriendByIndex(i, k_EFriendFlagImmediate);
        if (friends->GetFriendPersonaState(id) == k_EPersonaStateOffline)
            continue;
        FriendGameInfo_t game{};
        const bool inLauncher = friends->GetFriendGamePlayed(id, &game) && game.m_gameID.AppID() == AppId_t(OMNI_STEAM_APP_ID);
        QVariantMap hosting;
        if (inLauncher) {
            if (requestPresence)
                friends->RequestFriendRichPresence(id); // answered by onRichPresence
            const auto server = steamText(friends->GetFriendRichPresence(id, "omni_server"));
            if (!server.isEmpty())
                hosting = {{"server", server}, {"version", steamText(friends->GetFriendRichPresence(id, "omni_version"))},
                           {"loader", steamText(friends->GetFriendRichPresence(id, "omni_loader"))},
                           {"game", steamText(friends->GetFriendRichPresence(id, "omni_game"))},
                           {"pack", steamText(friends->GetFriendRichPresence(id, "omni_pack"))}};
        }
        const auto key = QString::number(id.ConvertToUint64());
        auto avatar = d->avatars.value(key);
        if (avatar.isEmpty()) {
            const int image = friends->GetSmallFriendAvatar(id);
            uint32 width = 0, height = 0;
            if (image > 0 && SteamUtils()->GetImageSize(image, &width, &height) && width > 0) {
                QImage rgba(int(width), int(height), QImage::Format_RGBA8888);
                if (SteamUtils()->GetImageRGBA(image, rgba.bits(), int(rgba.sizeInBytes()))) {
                    QByteArray png;
                    QBuffer buffer(&png);
                    buffer.open(QIODevice::WriteOnly);
                    rgba.save(&buffer, "PNG");
                    avatar = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
                    d->avatars.insert(key, avatar);
                }
            }
        }
        list.append(QVariantMap{{"id", key}, {"name", steamText(friends->GetFriendPersonaName(id))}, {"avatar", avatar},
                                {"inLauncher", inLauncher}, {"hosting", hosting}});
    }
    // Test mode: your own shared server, joinable from this PC.
    if (qEnvironmentVariableIsSet("OMNI_STEAM_LOOPBACK") && !m_hosting.isEmpty())
        list.prepend(QVariantMap{{"id", "loopback"}, {"name", tr("%1 (teste local)").arg(m_personaName)}, {"avatar", QString()},
                                 {"inLauncher", true},
                                 {"hosting", QVariantMap{{"server", m_hosting.value("name")}, {"version", m_hosting.value("version")},
                                                         {"loader", m_hosting.value("loader")}, {"game", m_hosting.value("game")},
                                                         {"pack", m_hosting.value("pack")}}}});
    std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        const auto rank = [](const QVariantMap &f) { return f.value("hosting").toMap().isEmpty() ? (f.value("inLauncher").toBool() ? 1 : 2) : 0; };
        const auto left = a.toMap(), right = b.toMap();
        if (rank(left) != rank(right))
            return rank(left) < rank(right);
        return left.value("name").toString().localeAwareCompare(right.value("name").toString()) < 0;
    });
    if (list != m_friends) {
        m_friends = list;
        emit friendsChanged();
    }
#else
    Q_UNUSED(requestPresence)
#endif
}
