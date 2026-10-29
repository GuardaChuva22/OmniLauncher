#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class QTimer;

// Steam friends join the servers hosted in the launcher through Valve's relay network
// (Steam Datagram Relay), with no IP, port forwarding or mod. The host's launcher accepts
// Steam P2P connections from friends and passes each one to the server's local port; the
// friend's launcher opens a local port, starts the matching game connected to it and
// carries every connection the game makes there over Steam. Both need Steam open and
// the launcher running. Discovery is Steam rich presence: friends see the shared server
// in the launcher, in Steam's "Join game" and in invites.
//
// Built only with the Steamworks SDK (OMNI_STEAM); otherwise `available` is false and
// everything is a no-op. App ID 480 is Valve's Spacewar test app: testing only.
class SteamTunnel final : public QObject
{
    Q_OBJECT
    // Built with Steamworks and its DLL found.
    Q_PROPERTY(bool available READ available CONSTANT)
    // Off until turned on: only then does the launcher sign in to Steam (and show up there as
    // playing the App ID's game). Development builds turn it on; released ones in developer mode.
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // Steam is open and signed in.
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString personaName READ personaName NOTIFY readyChanged)
    // The server shared with Steam friends: serverId, name, version, loader, port, players
    // (Steam friends connected now). Empty when nothing is shared.
    Q_PROPERTY(QVariantMap hosting READ hosting NOTIFY hostingChanged)
    // Online Steam friends, those sharing a server first: id, name, avatar (data URL),
    // inLauncher, hosting {server, version, loader, game, pack} (empty when not sharing).
    Q_PROPERTY(QVariantList friends READ friends NOTIFY friendsChanged)
    // The friend's server this PC joined: hostId, hostName, server, version, loader, game,
    // pack, address (127.0.0.1:port for the game), state ("connecting" | "ready" | "failed"),
    // route ("relay" | "direct" | "local"), ping (ms), error. Empty when not joined.
    Q_PROPERTY(QVariantMap joined READ joined NOTIFY joinedChanged)

public:
    explicit SteamTunnel(QObject *parent = nullptr);
    ~SteamTunnel() override;

    bool available() const;
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool ready() const { return m_ready; }
    QString personaName() const { return m_personaName; }
    QVariantMap hosting() const { return m_hosting; }
    QVariantList friends() const { return m_friends; }
    QVariantMap joined() const { return m_joined; }

    // server: id, name, version, loader, port (ServerManager); game: title, pack (the
    // modpack's Modrinth project, if any), so a friend's launcher can pick the same game.
    Q_INVOKABLE void host(const QVariantMap &server, const QVariantMap &game);
    Q_INVOKABLE void stopHosting();
    // Per server, remembered: share it with Steam friends while it runs (default on).
    Q_INVOKABLE bool sharing(const QString &serverId) const;
    Q_INVOKABLE void setSharing(const QString &serverId, bool enabled);
    Q_INVOKABLE bool invite(const QString &friendId);
    Q_INVOKABLE void join(const QString &hostId);
    Q_INVOKABLE void leave();
    Q_INVOKABLE void refreshFriends();
    // Sharing or joined: closing the launcher would cut friends off.
    Q_INVOKABLE bool busy() const { return !m_hosting.isEmpty() || !m_joined.isEmpty(); }

signals:
    void readyChanged();
    void enabledChanged();
    void sharingChanged();
    void hostingChanged();
    void friendsChanged();
    void joinedChanged();
    // A Steam invite was accepted, or "Join game" clicked in Steam.
    void joinRequested(const QString &hostId, const QString &hostName);
    void message(const QString &text);

private:
    struct Impl;
    friend struct Impl;

    void tryInit();
    void pump();
    void updateJoined(const QVariantMap &changes);
    void updatePlayers();

    std::unique_ptr<Impl> d;
    QTimer *m_pump = nullptr;
    bool m_friendsDirty = false;
    bool m_ready = false;
    bool m_enabled = false;
    QString m_personaName;
    QVariantMap m_hosting;
    QVariantList m_friends;
    QVariantMap m_joined;
};
