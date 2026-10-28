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

public:
    explicit AppModel(const QString &dataDirectory = {}, QObject *parent = nullptr);

    QVariantList games() const { return m_games; }
    QVariantList profiles() const { return m_profiles; }
    QString activeProfileId() const { return m_activeProfileId; }
    QVariantMap activeProfile() const;
    bool reducedMotion() const { return m_reducedMotion; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE QString addGame(const QString &title, const QString &version,
                               const QString &loader, const QString &coverId);
    Q_INVOKABLE bool updateGame(const QString &id, const QString &title,
                                const QString &version, const QString &loader,
                                const QString &coverId);
    Q_INVOKABLE bool removeGame(const QString &id);
    Q_INVOKABLE QString importCover(const QUrl &source);
    Q_INVOKABLE bool addOfflineProfile(const QString &username);
    Q_INVOKABLE bool selectProfile(const QString &id);
    Q_INVOKABLE bool removeProfile(const QString &id);
    Q_INVOKABLE void setReducedMotion(bool enabled);
    Q_INVOKABLE void clearError();

signals:
    void gamesChanged();
    void profilesChanged();
    void activeProfileChanged();
    void reducedMotionChanged();
    void lastErrorChanged();

private:
    void load();
    bool persist(const QVariantList &games, const QVariantList &profiles,
                 const QString &activeId, bool reducedMotion);
    void setError(const QString &message);

    QVariantList m_games;
    QVariantList m_profiles;
    QString m_activeProfileId;
    QString m_lastError;
    QString m_filePath;
    QString m_coversDirectory;
    bool m_reducedMotion = false;
    bool m_storageWritable = true;
};
