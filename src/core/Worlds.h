#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>

// The "Mundos" tab: every world (instances/<id>/saves/*) of every game in the library,
// and the world of every server the launcher hosts (servers/<id>), read from its
// level.dat. The map itself (region files, tiles) builds on this.
class Worlds final : public QObject
{
    Q_OBJECT

public:
    explicit Worlds(const QString &dataDirectory, QObject *parent = nullptr);

    // Newest played first. Keys: gameId, gameTitle, folder, path, name, seed (text: it is
    // 64-bit), mode (survival, creative, adventure, spectator), hardcore, lastPlayed (ms),
    // version, dataVersion, mappable (1.18+, what the map reads), icon (file URL or ""),
    // instance (the folder whose mods and packs draw it), server (a hosted server's
    // world: then serverId and serverPort, and name is the server's).
    QVariantList list(const QVariantList &games) const;
    // list() on a worker thread; `listed` brings the latest request's result.
    Q_INVOKABLE void request(const QVariantList &games);
    // What the map view opens a world with: WorldMap::describe().
    Q_INVOKABLE QVariantMap describe(const QString &worldPath, const QString &dimension) const;
    // WorldMap::entities() on the worker thread; `entitiesLoaded` brings them.
    Q_INVOKABLE void requestEntities(const QString &worldPath, const QString &dimension);

    // Follows one world while its map is open: `worldChanged` once the game has finished
    // saving (region and entity files, level.dat, the player's file). The game writes in
    // bursts, so changes settle for 2 s first. While `polling` (a game is running) the
    // dates are also compared every 10 s, in case a watch was lost. "" stops.
    Q_INVOKABLE void watch(const QString &worldPath, const QString &dimension);
    Q_INVOKABLE void setPolling(bool polling);

signals:
    void listed(const QVariantList &worlds);
    void entitiesLoaded(const QString &worldPath, const QString &dimension, const QVariantList &entities);
    void worldChanged(const QString &worldPath, const QString &dimension);

private:
    QStringList watchedFolders() const;
    QHash<QString, qint64> stamps() const;
    void check();

    QString m_root;
    QString m_watchedWorld;
    QString m_watchedDimension;
    QHash<QString, qint64> m_stamps;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QTimer m_poll;
    quint64 m_serial = 0;
    // Last member: destroyed first, it waits for a running list().
    QThreadPool m_pool;
};
