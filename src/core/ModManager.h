#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>

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

public:
    explicit ModManager(const QString &dataDirectory, QObject *parent = nullptr);

    QVariantList results() const { return m_results; }
    int totalHits() const { return m_totalHits; }
    bool searching() const { return m_searching; }
    QString searchError() const { return m_searchError; }
    QString gameId() const { return m_gameId; }
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

    Q_INVOKABLE void searchPacks(const QString &query, int offset = 0);
    Q_INVOKABLE void preparePack(const QString &projectId, const QString &imageUrl);
    Q_INVOKABLE void installPackFiles(const QString &gameId);
    Q_INVOKABLE void cancelPack();

    // `loader` is the game's loader name ("Fabric"); offset > 0 appends a page.
    Q_INVOKABLE void search(const QString &query, const QString &gameVersion, const QString &loader, int offset = 0);
    Q_INVOKABLE void loadInstalled(const QString &gameId);
    Q_INVOKABLE void install(const QString &projectId, const QString &gameId, const QString &gameVersion,
                             const QString &loader);
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
    void packFinished(const QString &gameId, bool ok, const QString &message);

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
    };

    QNetworkReply *get(const QString &url);
    void resolveProject(Install *job, const QString &projectId, const QString &title);
    void resolveVersion(Install *job, const QString &versionId);
    void installVersion(Install *job, const QJsonObject &version, const QString &title);
    void step(Install *job, int delta);
    QString modsDirectory(const QString &gameId) const;
    QJsonObject readMetadata(const QString &gameId) const;
    bool writeMetadata(const QString &gameId, const QJsonObject &metadata) const;

    QString m_root;
    QNetworkAccessManager *m_network;
    QVariantList m_results;
    int m_totalHits = 0;
    bool m_searching = false;
    QString m_searchError;
    quint64 m_searchSerial = 0;
    QString m_gameId;
    QVariantList m_installed;
    QSet<QString> m_installing;

    void setPackState(const QString &state, double progress = -1);
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
    Downloader *m_packDownloader;
};
