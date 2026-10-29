#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

class AppModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList games READ games NOTIFY gamesChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY activeProfileChanged)
    Q_PROPERTY(QVariantMap activeProfile READ activeProfile NOTIFY activeProfileChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // Per-game launch settings and play stats, keyed by game id.
    Q_PROPERTY(QVariantMap gameSettings READ gameSettings NOTIFY gameSettingsChanged)
    // Global defaults: defaultMemoryMb, afterLaunch ("minimize" | "close" | "keep").
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged)

public:
    explicit AppModel(const QString &dataDirectory = {}, QObject *parent = nullptr);

    QVariantList games() const { return m_games; }
    QVariantList profiles() const { return m_profiles; }
    QString activeProfileId() const { return m_activeProfileId; }
    QVariantMap activeProfile() const;
    bool reducedMotion() const { return m_reducedMotion; }
    QString lastError() const { return m_lastError; }
    QVariantMap gameSettings() const { return m_gameSettings; }
    QVariantMap preferences() const;
    QString dataDirectory() const { return m_dataDirectory; }

    // `loaderVersion` pins the mod loader build (e.g. Fabric "0.16.9"); empty means newest stable.
    Q_INVOKABLE QString addGame(const QString &title, const QString &version,
                               const QString &loader, const QString &coverId,
                               const QString &loaderVersion = {});
    Q_INVOKABLE bool updateGame(const QString &id, const QString &title,
                                const QString &version, const QString &loader,
                                const QString &coverId, const QString &loaderVersion = {});
    Q_INVOKABLE bool removeGame(const QString &id);
    Q_INVOKABLE QString importCover(const QUrl &source);
    Q_INVOKABLE bool addOfflineProfile(const QString &username);
    // Adds (or renames) the profile of a signed-in Microsoft account and selects it.
    Q_INVOKABLE bool addMicrosoftProfile(const QString &uuid, const QString &name);
    Q_INVOKABLE bool selectProfile(const QString &id);
    Q_INVOKABLE bool removeProfile(const QString &id);
    Q_INVOKABLE void setReducedMotion(bool enabled);
    // Supported keys: memoryMb (int MB), resolution ("" or "WIDTHxHEIGHT"), jvmArgs (string).
    Q_INVOKABLE bool setGameSetting(const QString &id, const QString &key, const QVariant &value);
    // Effective settings for launching a game: its own values over the global defaults.
    Q_INVOKABLE QVariantMap launchSettings(const QString &id) const;
    Q_INVOKABLE bool setPreference(const QString &key, const QVariant &value);
    Q_INVOKABLE void recordSession(const QString &id, qint64 seconds);
    // Marks a game as installed from a Modrinth modpack (project/version it came from).
    Q_INVOKABLE void setPackSource(const QString &id, const QString &projectId, const QString &versionId,
                                   const QString &versionNumber);
    // Marks a game as a subscribed custom pack (manifest link and installed version).
    Q_INVOKABLE void setCustomPack(const QString &id, const QString &url, const QString &version);
    // Share code (and, for its editors, the editor key) of a pack on OmniLauncher Cloud.
    Q_INVOKABLE void setCloudPack(const QString &id, const QString &code, const QString &editorKey = {});
    // Library tile art for a pack: its logo (copied like a cover) on a color taken from the logo.
    Q_INVOKABLE bool setPackArt(const QString &id, const QString &iconUrl);
    Q_INVOKABLE void clearError();

signals:
    void gamesChanged();
    void profilesChanged();
    void activeProfileChanged();
    void reducedMotionChanged();
    void lastErrorChanged();
    void gameSettingsChanged();
    void preferencesChanged();

private:
    void load();
    bool persist(const QVariantList &games, const QVariantList &profiles,
                 const QString &activeId, bool reducedMotion);
    void setError(const QString &message);

    QVariantList m_games;
    QVariantList m_profiles;
    QVariantMap m_gameSettings;
    QVariantMap m_preferences;
    QString m_dataDirectory;
    QString m_activeProfileId;
    QString m_lastError;
    QString m_filePath;
    QString m_coversDirectory;
    bool m_reducedMotion = false;
    bool m_storageWritable = true;
};
