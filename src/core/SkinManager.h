#pragma once

#include <QObject>
#include <QQuickImageProvider>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class AuthManager;
class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;

// Skins and capes of a Microsoft profile, through the official Minecraft services API:
// the profile's skin and owned capes, uploading a skin, going back to the default one,
// choosing the cape. Skins the player has had or imported are kept in a local library
// (<data>/skins/library), so they can go back to one later.
class SkinManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // Whose profile `profile` is.
    Q_PROPERTY(QString uuid READ uuid NOTIFY profileChanged)
    // {skin: {url, source, variant, libraryId}, capes: [{id, alias, source, active}]}; source is
    // a local file URL.
    Q_PROPERTY(QVariantMap profile READ profile NOTIFY profileChanged)
    // [{id, name, source, variant, added, url (the game's, once worn)}], the newest first.
    Q_PROPERTY(QVariantList library READ library NOTIFY libraryChanged)

public:
    SkinManager(const QString &dataDirectory, AuthManager *auth, QObject *parent = nullptr);

    bool busy() const { return m_busy > 0; }
    QString uuid() const { return m_uuid; }
    QVariantMap profile() const { return m_profile; }
    QVariantList library() const { return m_library; }

    Q_INVOKABLE void load(const QString &uuid);
    // Adds a skin file (64×64 or 64×32 PNG) to the library; its id, or "" (see `failed`).
    Q_INVOKABLE QString importSkin(const QUrl &file);
    Q_INVOKABLE void removeSkin(const QString &id);
    Q_INVOKABLE void renameSkin(const QString &id, const QString &name);
    // Puts on a look: the library skin `skinId` ("" keeps the skin) with `variant` ("classic"
    // or "slim"), and, with `changeCape`, the cape `capeId` ("" hides it).
    Q_INVOKABLE void applyLook(const QString &uuid, const QString &skinId, const QString &variant, bool changeCape,
                               const QString &capeId);
    Q_INVOKABLE void resetSkin(const QString &uuid);
    // "slim" or "classic", read from the texture.
    Q_INVOKABLE QString guessVariant(const QString &source) const;

signals:
    void busyChanged();
    void profileChanged();
    void libraryChanged();
    void failed(const QString &message);
    void done(const QString &message);

private:
    using Send = std::function<QNetworkReply *(const QString &token)>;
    // Runs a call with a fresh token; the profile it answers with (or a new read of it)
    // replaces `profile`. `then` runs after it succeeds.
    void call(const QString &uuid, Send send, const QString &success, std::function<void()> then = {});
    void sendCape(const QString &uuid, const QString &capeId, const QString &success);
    void readProfile(const QString &uuid, const QJsonObject &json);
    void fetch(const QString &url, const QString &path, std::function<void()> then);
    QString addToLibrary(const QByteArray &png, const QString &name, const QString &variant, const QString &url = {});
    void saveLibrary();
    void setBusy(bool busy);
    QString libraryPath(const QString &id = {}) const;

    QString m_root;
    AuthManager *m_auth;
    QNetworkAccessManager *m_network;
    int m_busy = 0;
    QString m_uuid;
    // The library skin being uploaded.
    QString m_uploading;
    QVariantMap m_profile;
    QVariantList m_library;
};

// "image://skinfront/<skin file URL, percent-encoded>|<classic or slim>": the skin's front,
// flat, as a 16×32 texel figure scaled up with sharp pixels (the library's thumbnails).
class SkinFrontProvider final : public QQuickImageProvider
{
public:
    SkinFrontProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
