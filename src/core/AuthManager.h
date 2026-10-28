#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;

// Microsoft account sign-in for Minecraft: browser OAuth (authorization code + PKCE,
// loopback redirect) → Xbox Live → XSTS → Minecraft services → ownership and profile.
// The launcher never sees the password. Only the Microsoft refresh token is kept,
// in the Windows Credential Manager; Minecraft tokens stay in memory.
class AuthManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY busyChanged)
    // Last sign-in failure, kept until the next attempt so it can't be missed.
    Q_PROPERTY(QString lastError READ lastError NOTIFY busyChanged)
    // Bumped whenever a cached skin changes, so avatar bindings re-read skinSource().
    Q_PROPERTY(int skinRevision READ skinRevision NOTIFY skinsUpdated)

public:
    explicit AuthManager(const QString &dataDirectory, QObject *parent = nullptr);

    bool configured() const { return !m_clientId.isEmpty(); }
    bool busy() const { return !m_stage.isEmpty(); }
    QString stage() const { return m_stage; }
    QString lastError() const { return m_lastError; }
    QString clientId() const { return m_clientId; }
    int skinRevision() const { return m_skinRevision; }

    Q_INVOKABLE void signIn();
    Q_INVOKABLE void cancel();
    // Fresh Minecraft session for a saved Microsoft profile (refreshing tokens as needed).
    Q_INVOKABLE void requestSession(const QString &uuid);
    Q_INVOKABLE void forget(const QString &uuid);
    // Local skin file (file URL) for the avatar, or "" when not cached yet.
    Q_INVOKABLE QString skinSource(const QString &uuid) const;

signals:
    void busyChanged();
    void signedIn(const QVariantMap &profile); // uuid, name
    void signInFailed(const QString &message);
    // account: uuid, name, accessToken, xuid, clientId
    void sessionReady(const QString &uuid, const QVariantMap &account);
    void sessionFailed(const QString &uuid, const QString &message);
    void skinChanged(const QString &uuid);
    void skinsUpdated();

private:
    struct Session {
        QString accessToken;
        QString xuid;
        QString name;
        QDateTime expires;
    };
    using Done = std::function<void(const QString &error, const QVariantMap &result)>;

    void setStage(const QString &stage);
    void failSignIn(const QString &message);
    // Diagnostics in <data>/logs/auth.log: steps and HTTP status only, never tokens.
    void log(const QString &line) const;
    void onLoopbackRequest();
    void exchangeToken(const QMap<QString, QString> &form, Done done);
    void minecraftLogin(const QString &msAccessToken, Done done);
    QNetworkReply *postJson(const QString &url, const QJsonObject &body, const QString &bearer = {});
    void downloadSkin(const QString &uuid, const QString &url);
    QString skinPath(const QString &uuid) const;

    static bool storeRefreshToken(const QString &uuid, const QString &token);
    static QString loadRefreshToken(const QString &uuid);
    static void deleteRefreshToken(const QString &uuid);

    QString m_root;
    QString m_clientId;
    QNetworkAccessManager *m_network;
    QPointer<QTcpServer> m_server;
    QString m_verifier;
    QString m_state;
    QString m_redirectUri;
    QString m_stage;
    QHash<QString, Session> m_sessions;
    int m_skinRevision = 0;
    QString m_lastError;
};
