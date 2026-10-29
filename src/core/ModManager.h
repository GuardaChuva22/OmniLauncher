#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>

#include <functional>

class Downloader;
class QNetworkAccessManager;
class QNetworkReply;

// Modrinth catalog search and per-game mod management. Mods live in
// instances/<gameId>/mods; .omni-mods.json there remembers where each file
// came from (project, version, title, icon) so the list can show more than a
// filename. A mod is disabled by renaming it to <name>.jar.disabled.
class ModManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(int totalHits READ totalHits NOTIFY resultsChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY resultsChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY resultsChanged)
    Q_PROPERTY(QString gameId READ gameId NOTIFY installedChanged)
    // Which content the installed list shows: "mod" | "shader" | "resourcepack".
    Q_PROPERTY(QString kind READ kind NOTIFY installedChanged)
    Q_PROPERTY(QVariantList installed READ installed NOTIFY installedChanged)
    Q_PROPERTY(QStringList installedProjects READ installedProjects NOTIFY installedChanged)
    Q_PROPERTY(QStringList installing READ installing NOTIFY installingChanged)
    // Modpacks (.mrpack): catalog, then a two-step install. preparePack() downloads and
    // reads the pack and emits packPrepared(); the caller creates the game and calls
    // installPackFiles(gameId). packState: "" | "preparing" | "waiting" | "downloading".
    Q_PROPERTY(QVariantList packResults READ packResults NOTIFY packResultsChanged)
    Q_PROPERTY(int packTotalHits READ packTotalHits NOTIFY packResultsChanged)
    Q_PROPERTY(bool packSearching READ packSearching NOTIFY packResultsChanged)
    Q_PROPERTY(QString packSearchError READ packSearchError NOTIFY packResultsChanged)
    Q_PROPERTY(QString packState READ packState NOTIFY packChanged)
    Q_PROPERTY(QString packProjectId READ packProjectId NOTIFY packChanged)
    Q_PROPERTY(double packProgress READ packProgress NOTIFY packChanged)
    // Versions of one project for a picker (mods or packs): id, versionNumber, name,
    // gameVersions, loaders, date, type. Filled by fetchProjectVersions().
    Q_PROPERTY(QVariantList versionChoices READ versionChoices NOTIFY versionChoicesChanged)
    Q_PROPERTY(bool versionChoicesLoading READ versionChoicesLoading NOTIFY versionChoicesChanged)
    // Update check over the installed mods (identified on Modrinth by file hash).
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY installedChanged)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY installedChanged)
    // Custom packs (subscribed by link): newest published version per game id.
    Q_PROPERTY(QVariantMap customLatest READ customLatest NOTIFY customLatestChanged)

public:
    explicit ModManager(const QString &dataDirectory, QObject *parent = nullptr);

    QVariantList results() const { return m_results; }
    int totalHits() const { return m_totalHits; }
    bool searching() const { return m_searching; }
    QString searchError() const { return m_searchError; }
    QString gameId() const { return m_gameId; }
    QString kind() const { return m_kind; }
    // True when a jar starting with one of the prefixes sits in the game's mods folder.
    Q_INVOKABLE bool hasModFile(const QString &gameId, const QStringList &prefixes) const;
    QVariantList installed() const { return m_installed; }
    QStringList installedProjects() const;
    QStringList installing() const { return QStringList(m_installing.begin(), m_installing.end()); }
    QVariantList packResults() const { return m_packResults; }
    int packTotalHits() const { return m_packTotalHits; }
    bool packSearching() const { return m_packSearching; }
    QString packSearchError() const { return m_packSearchError; }
    QString packState() const { return m_packState; }
    QString packProjectId() const { return m_packProjectId; }
    double packProgress() const { return m_packProgress; }
    QVariantList versionChoices() const { return m_versionChoices; }
    bool versionChoicesLoading() const { return m_versionChoicesLoading; }
    bool checkingUpdates() const { return m_checkingUpdates; }
    int updateCount() const;
    QVariantMap customLatest() const { return m_customLatest; }

    // Custom pack from a manifest URL (https or file). targetGameId set: update that game.
    // Same two-step flow as Modrinth packs: packPrepared() → caller creates/updates the game
    // → installPackFiles(gameId).
    Q_INVOKABLE void prepareCustom(const QString &url, const QString &targetGameId = {});
    Q_INVOKABLE void checkCustom(const QString &gameId, const QString &url);
    // Downloads the Modrinth logo of a pack game; emits packIconReady with a local file URL.
    Q_INVOKABLE void fetchPackIcon(const QString &gameId, const QString &projectId);

    // Empty gameVersion/loader lists every version (used for modpacks).
    Q_INVOKABLE void fetchProjectVersions(const QString &projectId, const QString &gameVersion = {},
                                          const QString &loader = {});
    Q_INVOKABLE void checkUpdates(const QString &gameId, const QString &gameVersion, const QString &loader);
    // Installs a specific version of a project (update or change), replacing its current file.
    Q_INVOKABLE void installVersionOf(const QString &projectId, const QString &versionId, const QString &gameId,
                                      const QString &gameVersion, const QString &loader,
                                      const QString &kind = QStringLiteral("mod"));
    Q_INVOKABLE void updateAll(const QString &gameId, const QString &gameVersion, const QString &loader);

    Q_INVOKABLE void searchPacks(const QString &query, int offset = 0);
    // versionId empty: newest. targetGameId set: change the version of that installed pack game.
    Q_INVOKABLE void preparePack(const QString &projectId, const QString &imageUrl, const QString &versionId = {},
                                 const QString &targetGameId = {});
    Q_INVOKABLE void installPackFiles(const QString &gameId);
    Q_INVOKABLE void cancelPack();

    // `loader`: the game's loader for mods; for shaders/resource packs, the Modrinth loaders
    // of that content ("iris,optifine" / "minecraft"), comma-separated. offset > 0 appends a page.
    Q_INVOKABLE void search(const QString &query, const QString &gameVersion, const QString &loader, int offset = 0,
                            const QString &kind = QStringLiteral("mod"));
    // kind empty keeps the current one.
    Q_INVOKABLE void loadInstalled(const QString &gameId, const QString &kind = {});
    Q_INVOKABLE void install(const QString &projectId, const QString &gameId, const QString &gameVersion,
                             const QString &loader, const QString &kind = QStringLiteral("mod"));
    Q_INVOKABLE bool setEnabled(const QString &gameId, const QString &file, bool enabled);
    Q_INVOKABLE bool remove(const QString &gameId, const QString &file);

signals:
    void resultsChanged();
    void installedChanged();
    void installingChanged();
    void installFinished(const QString &projectId, bool ok, const QString &message);
    void packResultsChanged();
    void packChanged();
    // info: title, version, loaderVersion, coverPath (local file URL or "").
    void packPrepared(const QVariantMap &info);
    // pack: projectId, versionId, versionNumber (for the game's record).
    void packFinished(const QString &gameId, bool ok, const QString &message, const QVariantMap &pack);
    void versionChoicesChanged();
    void customLatestChanged();
    void packIconReady(const QString &gameId, const QString &fileUrl);

private:
    struct Install {
        QString rootProject;
        QString gameId;
        QString gameVersion;
        QString loader;
        QSet<QString> seenProjects;
        int pending = 0;
        int added = 0;
        QString error;
        QString kind = QStringLiteral("mod");
    };

    QNetworkReply *get(const QString &url);
    QNetworkReply *post(const QString &url, const QJsonObject &body);
    void fillTitles(const QString &gameId, const QString &kind, std::function<void()> done);
    void resolveProject(Install *job, const QString &projectId, const QString &title);
    void resolveVersion(Install *job, const QString &versionId);
    void installVersion(Install *job, const QJsonObject &version, const QString &title);
    void step(Install *job, int delta);
    // Folder of a content kind (empty: the current one).
    QString modsDirectory(const QString &gameId, const QString &kind = {}) const;
    QJsonObject readMetadata(const QString &gameId, const QString &kind = {}) const;
    bool writeMetadata(const QString &gameId, const QJsonObject &metadata, const QString &kind = {}) const;

    QString m_root;
    QNetworkAccessManager *m_network;
    QVariantList m_results;
    int m_totalHits = 0;
    bool m_searching = false;
    QString m_searchError;
    quint64 m_searchSerial = 0;
    QString m_gameId;
    QVariantList m_installed;
    QString m_kind = QStringLiteral("mod");
    QSet<QString> m_installing;

    void setPackState(const QString &state, double progress = -1);
    void installPackFilesNow(const QString &gameId);
    void failPack(const QString &message, const QString &gameId = {});
    QVariantList m_packResults;
    int m_packTotalHits = 0;
    bool m_packSearching = false;
    QString m_packSearchError;
    quint64 m_packSearchSerial = 0;
    QString m_packState;
    QString m_packProjectId;
    double m_packProgress = -1;
    QString m_packDirectory;
    QString m_packGameId;
    QJsonObject m_packIndex;
    QString m_packTargetGame;
    QVariantMap m_packVersion;
    QVariantList m_versionChoices;
    bool m_versionChoicesLoading = false;
    quint64 m_versionSerial = 0;
    bool m_checkingUpdates = false;
    QVariantMap m_customLatest;
    Downloader *m_packDownloader;
};
