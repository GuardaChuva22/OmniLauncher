#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// OmniLauncher Cloud (cloud/worker.js): turns a library game into a shared modpack
// and publishes new versions of it. Mods found on Modrinth are referenced by URL
// and hash; other files (configs zip, jars that aren't on Modrinth) are uploaded.
// Friends follow the pack by its code, through ModManager's custom manifest path.
class PackCloud final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString baseUrl READ baseUrl CONSTANT)
    Q_PROPERTY(bool configured READ configured CONSTANT)
    // "" | "scanning" | "uploading" | "publishing"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY stateChanged)
    Q_PROPERTY(QString gameId READ gameId NOTIFY stateChanged)

public:
    explicit PackCloud(const QString &dataDirectory, QObject *parent = nullptr);

    QString baseUrl() const { return m_base; }
    bool configured() const { return !m_base.isEmpty(); }
    QString state() const { return m_state; }
    QString detail() const { return m_detail; }
    QString gameId() const { return m_gameId; }

    // game: library entry; settings: its gameSettings (cloudCode/cloudKey when already published).
    Q_INVOKABLE void publish(const QVariantMap &game, const QVariantMap &settings, const QString &name,
                             const QString &version, const QString &changelog);
    // A six-letter code from whatever the player pasted (code, link or editor link), or "".
    Q_INVOKABLE QString codeFrom(const QString &text) const;
    Q_INVOKABLE QString packUrl(const QString &code) const;
    // Editor links carry the editor key: whoever has one can publish the pack.
    Q_INVOKABLE QString editorLink(const QString &code, const QString &key) const;
    Q_INVOKABLE QString keyFrom(const QString &text) const;

signals:
    void stateChanged();
    // On success: code, editorKey, version (pack.version), url (manifest to follow).
    void published(const QString &gameId, bool ok, const QString &message, const QVariantMap &result);

private:
    struct Upload {
        QString file;
        QString sha256;
        QString path;   // manifest path ("mods/x.jar"), empty for the overrides zip
        qint64 size = 0;
    };
    void setState(const QString &state, const QString &detail = {});
    void fail(const QString &message);
    QNetworkReply *request(const QString &method, const QString &path, const QByteArray &body = {},
                           const QString &contentType = {});
    void resolveOnModrinth();
    void ensurePack();
    void uploadNext();
    void sendManifest();
    QString loaderVersion() const;

    QString m_root;
    QString m_base;
    QNetworkAccessManager *m_network;
    QString m_state;
    QString m_detail;
    QString m_gameId;

    // Current publish
    QVariantMap m_game;
    QString m_code;
    QString m_key;
    QString m_name;
    QString m_version;
    QString m_changelog;
    QJsonArray m_files;
    QList<Upload> m_local;     // files not found on Modrinth (sha1 → local file)
    QHash<QString, Upload> m_bySha1;
    QList<Upload> m_uploads;
    QJsonObject m_overrides;
    Upload m_overridesFile;
};
