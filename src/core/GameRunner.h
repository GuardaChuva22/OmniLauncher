#pragma once

#include "Downloader.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <functional>

class QNetworkAccessManager;
class QProcess;
class QTimer;
class QWinEventNotifier;

// Installs and starts Vanilla Minecraft for a game. One session at a time:
// idle → preparing → downloading → starting → running → idle, or → failed.
// Everything shared between games (versions, libraries, assets, Java runtimes)
// lives once under the data directory; each game gets instances/<gameId>.
class GameRunner final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList versions READ versions NOTIFY versionsChanged)
    Q_PROPERTY(QString latestRelease READ latestRelease NOTIFY versionsChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QString gameId READ gameId NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY progressChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY progressChanged)
    // 0..1 while downloading files; -1 while the duration is unknown.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    // Fabric loader builds for `loaderVersionsFor`, newest first; `recommendedLoader` is the newest stable.
    Q_PROPERTY(QStringList loaderVersions READ loaderVersions NOTIFY loaderVersionsChanged)
    Q_PROPERTY(QString recommendedLoader READ recommendedLoader NOTIFY loaderVersionsChanged)
    Q_PROPERTY(QString loaderVersionsFor READ loaderVersionsFor NOTIFY loaderVersionsChanged)
    Q_PROPERTY(QString loaderVersionsLoader READ loaderVersionsLoader NOTIFY loaderVersionsChanged)
    // Which loaders exist for one Minecraft version: {version, Fabric, Forge, NeoForge} (true/false;
    // a loader still being checked is absent).
    Q_PROPERTY(QVariantMap loaderSupport READ loaderSupport NOTIFY loaderSupportChanged)
    // What went wrong in the last session: gameId, kind ("crash" | "resolution" | "data" | "memory"),
    // title, detail, mods, file. Empty when nothing was found.
    Q_PROPERTY(QVariantMap diagnosis READ diagnosis NOTIFY diagnosisChanged)
    Q_PROPERTY(int systemMemoryMb READ systemMemoryMb CONSTANT)
    // Seconds played in the session that is running now (0 when no game runs); ticks every 30 s.
    Q_PROPERTY(int sessionSeconds READ sessionSeconds NOTIFY sessionTick)
    // Minecraft options (options.txt keys → values) written into every new game before
    // its first start. Keys absent here keep the game's own defaults.
    Q_PROPERTY(QVariantMap gameDefaults READ gameDefaults NOTIFY gameDefaultsChanged)
    Q_PROPERTY(int defaultKeybindCount READ defaultKeybindCount NOTIFY gameDefaultsChanged)

public:
    explicit GameRunner(const QString &dataDirectory, QObject *parent = nullptr);
    ~GameRunner() override;

    QStringList versions() const { return m_releases; }
    QString latestRelease() const { return m_latestRelease; }
    QString state() const { return m_state; }
    bool busy() const { return m_state == "preparing" || m_state == "downloading" || m_state == "starting"; }
    bool running() const { return m_state == "running"; }
    QString gameId() const { return m_gameId; }
    QString error() const { return m_error; }
    QString stage() const { return m_stage; }
    QString detail() const { return m_detail; }
    double progress() const { return m_progress; }
    QStringList loaderVersions() const { return m_loaderVersions; }
    QString recommendedLoader() const { return m_recommendedLoader; }
    QString loaderVersionsFor() const { return m_loaderVersionsFor; }
    QString loaderVersionsLoader() const { return m_loaderVersionsLoader; }
    QVariantMap loaderSupport() const { return m_loaderSupport; }
    Q_INVOKABLE void checkLoaderSupport(const QString &gameVersion);
    // Mojang version types: "release", "snapshot", "old_beta", "old_alpha".
    Q_INVOKABLE QStringList versionsOfType(const QString &type) const { return m_versionsByType.value(type); }
    Q_INVOKABLE QString versionType(const QString &version) const { return m_versionTypes.value(version, QStringLiteral("release")); }
    QVariantMap diagnosis() const { return m_diagnosis; }
    int systemMemoryMb() const;
    int sessionSeconds() const { return running() ? int(m_playClock.elapsed() / 1000) : 0; }
    QVariantMap gameDefaults() const { return m_gameDefaults; }
    int defaultKeybindCount() const;
    Q_INVOKABLE void setGameDefault(const QString &key, const QString &value);
    Q_INVOKABLE void resetGameDefaults();
    // Copies the known options, all sound levels and all keybinds from a game's options.txt.
    // Returns how many values were taken, or -1 when the game has none yet.
    Q_INVOKABLE int importGameDefaults(const QString &gameId);
    // Writes the defaults into an existing game's options.txt (other lines are kept).
    Q_INVOKABLE bool applyGameDefaults(const QString &gameId);

    // Looks at the newest crash report and the game log of a game (since = epoch ms, 0 = any).
    Q_INVOKABLE QVariantMap diagnose(const QString &gameId, qint64 since = 0) const;
    Q_INVOKABLE void clearDiagnosis();
    Q_INVOKABLE bool openFile(const QString &path);
    // Suggested heap for a game: grows with its mod count, capped by this PC's memory.
    Q_INVOKABLE int recommendedMemory(const QString &gameId, const QString &loader) const;

    // Worlds of a game: folder, name, lastPlayed (ISO), sizeBytes, icon (file URL or "").
    Q_INVOKABLE QVariantList worlds(const QString &gameId) const;
    Q_INVOKABLE void backupWorld(const QString &gameId, const QString &folder);
    Q_INVOKABLE bool trashWorld(const QString &gameId, const QString &folder);
    Q_INVOKABLE bool openWorldsFolder(const QString &gameId, const QString &folder = {});

    // Screenshots of every game, newest first: url, path, gameId, takenAt (ISO), name.
    Q_INVOKABLE QVariantList screenshots() const;
    Q_INVOKABLE bool copyImage(const QString &path);
    Q_INVOKABLE bool trashFile(const QString &path);

    // `game` is a library entry; `settings` comes from AppModel::launchSettings().
    // `account` (Microsoft): uuid, accessToken, xuid, clientId. Empty means offline.
    Q_INVOKABLE bool launch(const QVariantMap &game, const QString &playerName, const QVariantMap &settings,
                            const QVariantMap &account = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void stopGame();
    Q_INVOKABLE void refreshVersions();
    // `loader`: "Fabric", "Forge" or "NeoForge".
    Q_INVOKABLE void fetchLoaderVersions(const QString &gameVersion, const QString &loader = QStringLiteral("Fabric"));
    Q_INVOKABLE QString instanceDirectory(const QString &gameId) const;
    Q_INVOKABLE bool openGameFolder(const QString &gameId);
    Q_INVOKABLE bool openModsFolder(const QString &gameId);
    Q_INVOKABLE bool openDataFolder();
    Q_INVOKABLE bool openLog();

signals:
    void versionsChanged();
    void stateChanged();
    void progressChanged();
    void loaderVersionsChanged();
    void loaderSupportChanged();
    void diagnosisChanged();
    void sessionTick();
    void gameDefaultsChanged();
    void worldsChanged(const QString &gameId);
    void worldBackupFinished(const QString &gameId, bool ok, const QString &message);
    // `detached`: the launcher is not tracking the process (it may now quit).
    void gameStarted(bool detached);
    void gameExited(const QString &gameId, qint64 seconds, int exitCode);

private:
    struct Library {
        QString path;
        bool nativeArchive = false;
        QStringList extractExclude;
    };

    void setState(const QString &state);
    void setStage(const QString &stage, double progress = -1, const QString &detail = {});
    void fail(const QString &message);
    void runBatch(const QList<Downloader::Job> &jobs, const QString &stage, bool determinate,
                  std::function<void()> next);
    void onBatchFinished(bool ok, const QString &error);
    void onBatchProgress();

    bool loadManifest(const QByteArray &data);
    void stepVersionJson();
    void stepParseVersion();
    void stepFabricProfile();
    void mergeLoaderProfile(const QJsonObject &profile);
    void loadForgeList(const QString &loader, const QString &gameVersion,
                       std::function<void(const QStringList &, const QString &)> done);
    void stepForgeInstaller();
    void stepForgeExtract();
    void runNextProcessor();
    QString forgeToken(const QString &argument) const;
    void stepAssetsAndJava();
    void stepRuntimeManifest();
    void stepCollectFiles();
    void stepPrepareAndStart();
    bool startProcess(const QString &java, const QStringList &arguments);
    void onProcessExited(int exitCode);
    QString logTail() const;

    QString path(const QString &relative) const;

    QString m_root;
    QNetworkAccessManager *m_network;
    Downloader *m_downloader;
    QStringList m_releases;
    QString m_latestRelease;
    QHash<QString, QPair<QUrl, QString>> m_versionIndex;

    QString m_state = QStringLiteral("idle");
    QString m_stage;
    QString m_detail;
    QString m_error;
    double m_progress = -1;
    bool m_determinate = false;
    QElapsedTimer m_progressClock;
    std::function<void()> m_next;

    // Session
    QString m_gameId;
    QString m_versionId;
    QString m_loader;
    QString m_loaderVersion;
    QStringList m_loaderVersions;
    QString m_recommendedLoader;
    QString m_loaderVersionsFor;
    QString m_loaderVersionsLoader;
    QVariantMap m_loaderSupport;
    QHash<QString, QStringList> m_versionsByType;
    QHash<QString, QString> m_versionTypes;
    // Forge/NeoForge installer state for the current session.
    QJsonObject m_forgeProfile;
    QString m_forgeInstallId;
    QString m_forgeInstaller;
    QString m_forgeDir;
    bool m_forgeNeedsProcessors = false;
    int m_processorIndex = 0;
    QPointer<QProcess> m_processor;
    QVariantMap m_diagnosis;
    QVariantMap m_gameDefaults;
    void saveGameDefaults();
    bool writeOptions(const QString &file) const;
    qint64 m_sessionStart = 0;
    QTimer *m_sessionTimer = nullptr;
    QString m_playerName;
    QVariantMap m_settings;
    QVariantMap m_account;
    QJsonObject m_version;
    QString m_javaComponent;
    QList<Library> m_libraries;
    QString m_outputLog;
    QString m_errorLog;

    // Running game
    QElapsedTimer m_playClock;
    QWinEventNotifier *m_processNotifier = nullptr;
    void *m_processHandle = nullptr;
    qint64 m_pid = 0;
};
