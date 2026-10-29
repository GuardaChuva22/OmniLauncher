#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

// Dedicated Minecraft servers hosted on this PC, created from a library game: same
// Minecraft version, loader and Java, plus the game's server-safe mods and configs.
// Each server lives in <data>/servers/<id>; the list is <data>/servers.json.
class ServerManager final : public QObject
{
    Q_OBJECT
    // id, name, gameId, version, loader, loaderVersion, memoryMb, port, installed, createdAt
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)
    // Per server id: state ("stopped" | "installing" | "starting" | "running" | "stopping" | "failed"),
    // detail, players (list of names), startedAt (epoch ms, 0 when not running)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString lanAddress READ lanAddress CONSTANT)
    Q_PROPERTY(QString publicAddress READ publicAddress NOTIFY publicAddressChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY messageChanged)

public:
    explicit ServerManager(const QString &dataDirectory, QObject *parent = nullptr);
    ~ServerManager() override;

    QVariantList servers() const { return m_servers; }
    QVariantMap status() const { return m_status; }
    QString lanAddress() const;
    QString publicAddress() const { return m_publicAddress; }
    QString lastError() const { return m_lastError; }

    // game: a library entry that has been started once (its Java and loader are on disk).
    // The player must have accepted the Minecraft EULA. Returns the new id, or "" (see lastError).
    Q_INVOKABLE QString create(const QString &name, const QVariantMap &game, int memoryMb, bool eulaAccepted);
    Q_INVOKABLE void install(const QString &id);
    // Copies the game's mods and configs again (after a pack update, say).
    Q_INVOKABLE void syncContent(const QString &id);
    Q_INVOKABLE void start(const QString &id);
    Q_INVOKABLE void stop(const QString &id);
    Q_INVOKABLE void restart(const QString &id);
    Q_INVOKABLE void kill(const QString &id);
    Q_INVOKABLE void sendCommand(const QString &id, const QString &command);
    Q_INVOKABLE QStringList console(const QString &id) const { return m_console.value(id); }
    Q_INVOKABLE void clearConsole(const QString &id);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool setMemory(const QString &id, int memoryMb);
    Q_INVOKABLE bool rename(const QString &id, const QString &name);

    // server.properties as a map; writing keeps every other line.
    Q_INVOKABLE QVariantMap properties(const QString &id) const;
    Q_INVOKABLE bool setServerProperty(const QString &id, const QString &key, const QString &value);
    // whitelist.json / ops.json / banned-players.json names.
    Q_INVOKABLE QStringList playerList(const QString &id, const QString &file) const;

    // The connector: the e4mc mod gives a running server a public *.e4mc.link address
    // (status.tunnel) with no port forwarding. Fabric, Forge and NeoForge only.
    Q_INVOKABLE bool hasConnector(const QString &id) const;
    Q_INVOKABLE void setConnector(const QString &id, bool enabled);

    Q_INVOKABLE void backup(const QString &id);
    // name, path, sizeBytes, createdAt (ISO), newest first.
    Q_INVOKABLE QVariantList backups(const QString &id) const;
    Q_INVOKABLE bool openFolder(const QString &id, const QString &sub = {});
    Q_INVOKABLE void refreshPublicAddress();
    Q_INVOKABLE bool anyRunning() const;
    Q_INVOKABLE void copyText(const QString &text) const;

signals:
    void serversChanged();
    void statusChanged();
    void publicAddressChanged();
    void messageChanged();
    void consoleLine(const QString &id, const QString &line);
    void propertiesChanged(const QString &id);
    void backupsChanged(const QString &id);
    void connectorChanged(const QString &id);
    void message(const QString &text);

private:
    QString serverDir(const QString &id) const;
    QVariantMap serverById(const QString &id) const;
    void updateServer(const QString &id, const QVariantMap &changes);
    void save();
    void setStatus(const QString &id, const QString &state, const QString &detail = {});
    void setPlayers(const QString &id, const QStringList &players);
    void appendConsole(const QString &id, const QString &line);
    void failInstall(const QString &id, const QString &text);
    QNetworkReply *get(const QString &url);
    void fetchTo(const QString &url, const QString &file, std::function<void(bool)> done);
    void installServerJar(const QString &id, const QVariantMap &versionJson);
    void installFabric(const QString &id, const QVariantMap &versionJson);
    void installForge(const QString &id);
    void finishInstall(const QString &id, const QStringList &launch);
    void copyGameContent(const QString &id, std::function<void(int copied, int skipped)> done);
    QString java(const QVariantMap &server) const;
    void parseLine(const QString &id, const QString &line);
    void writeDefaults(const QString &id, const QString &motd, int port);

    QString m_root;
    QNetworkAccessManager *m_network;
    QVariantList m_servers;
    QVariantMap m_status;
    QHash<QString, QStringList> m_console;
    QHash<QString, QString> m_partial;
    QHash<QString, QPointer<QProcess>> m_processes;
    QHash<QString, bool> m_restartAfterStop;
    QString m_publicAddress;
    QString m_lastError;
    void *m_job = nullptr;
};
