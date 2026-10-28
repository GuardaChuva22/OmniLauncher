#include "AuthManager.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <wincred.h>
#endif

#ifndef OMNI_MSA_CLIENT_ID
#  define OMNI_MSA_CLIENT_ID ""
#endif

namespace {
const QString kAuthorize = QStringLiteral("https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize");
const QString kToken = QStringLiteral("https://login.microsoftonline.com/consumers/oauth2/v2.0/token");
const QString kScope = QStringLiteral("XboxLive.signin offline_access");

QString randomUrlSafe(int bytes)
{
    QByteArray data(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(data.data()), bytes / 4);
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString credentialTarget(const QString &uuid)
{
    return QStringLiteral("OmniLauncher/msa/") + uuid;
}

QString xboxError(const QJsonObject &reply)
{
    switch (qint64(reply.value("XErr").toDouble())) {
    case 2148916233: return QStringLiteral("Essa conta Microsoft não tem um perfil Xbox. Entre uma vez em xbox.com e tente de novo.");
    case 2148916235: return QStringLiteral("O Xbox Live não está disponível no seu país.");
    case 2148916236:
    case 2148916237: return QStringLiteral("Essa conta precisa de verificação de idade no Xbox antes de jogar.");
    case 2148916238: return QStringLiteral("Contas de menores precisam ser adicionadas a uma Família Microsoft por um adulto.");
    default: return QStringLiteral("O Xbox Live recusou o acesso a essa conta.");
    }
}
} // namespace

AuthManager::AuthManager(const QString &dataDirectory, QObject *parent)
    : QObject(parent)
    , m_root(QDir(dataDirectory).absolutePath())
    , m_clientId(qEnvironmentVariable("OMNI_MSA_CLIENT_ID", QStringLiteral(OMNI_MSA_CLIENT_ID)).trimmed())
    , m_network(new QNetworkAccessManager(this))
{
}

void AuthManager::log(const QString &line) const
{
    QDir().mkpath(QDir(m_root).filePath("logs"));
    QFile file(QDir(m_root).filePath("logs/auth.log"));
    if (file.open(QIODevice::Append | QIODevice::Text))
        file.write((QDateTime::currentDateTime().toString(Qt::ISODate) + "  " + line + '\n').toUtf8());
}

void AuthManager::failSignIn(const QString &message)
{
    log("sign-in failed: " + message);
    m_lastError = message;
    setStage({});
    emit busyChanged();
    emit signInFailed(message);
}

void AuthManager::setStage(const QString &stage)
{
    if (m_stage == stage)
        return;
    m_stage = stage;
    emit busyChanged();
}

// ── Browser sign-in ────────────────────────────────────────────────────────

void AuthManager::signIn()
{
    if (!configured()) {
        failSignIn(QStringLiteral("O login Microsoft ainda não foi configurado neste build (falta o client ID do app)."));
        return;
    }
    if (busy())
        return;
    m_lastError.clear();
    emit busyChanged();
    auto *server = new QTcpServer(this);
    if (!server->listen(QHostAddress::LocalHost, 0)) {
        server->deleteLater();
        failSignIn(QStringLiteral("Não foi possível abrir a porta local para receber o login."));
        return;
    }
    m_server = server;
    connect(server, &QTcpServer::newConnection, this, &AuthManager::onLoopbackRequest);
    m_verifier = randomUrlSafe(64);
    m_state = randomUrlSafe(16);
    m_redirectUri = QStringLiteral("http://localhost:%1").arg(server->serverPort());
    const auto challenge = QString::fromLatin1(QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256)
                                                   .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    QUrlQuery query;
    query.addQueryItem("client_id", m_clientId);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("redirect_uri", m_redirectUri);
    query.addQueryItem("scope", kScope);
    query.addQueryItem("state", m_state);
    query.addQueryItem("code_challenge", challenge);
    query.addQueryItem("code_challenge_method", "S256");
    query.addQueryItem("prompt", "select_account");
    QUrl url(kAuthorize);
    url.setQuery(query);
    setStage(QStringLiteral("Continue o login no navegador"));
    log(QStringLiteral("sign-in started, redirect %1").arg(m_redirectUri));
    QDesktopServices::openUrl(url);
    // Abandon a sign-in left open in the browser.
    QTimer::singleShot(5 * 60 * 1000, this, [this, server] {
        if (m_server == server) {
            cancel();
            failSignIn(QStringLiteral("O login expirou. Tente de novo."));
        }
    });
}

void AuthManager::cancel()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    setStage({});
}

void AuthManager::onLoopbackRequest()
{
    if (!m_server)
        return;
    auto *socket = m_server->nextPendingConnection();
    if (!socket)
        return;
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        const auto request = QString::fromLatin1(socket->readAll());
        const auto target = request.section(' ', 1, 1);
        const QUrlQuery params(QUrl(target).query());
        const bool isCallback = params.hasQueryItem("code") || params.hasQueryItem("error");
        const QByteArray page = isCallback
            ? QByteArrayLiteral("<!doctype html><meta charset=utf-8><title>OmniLauncher</title>"
                                "<body style=\"background:#151216;color:#F5F1ED;font:16px system-ui;display:grid;place-items:center;height:90vh\">"
                                "<div>Pronto! Pode voltar ao OmniLauncher.</div>")
            : QByteArrayLiteral("");
        socket->write((isCallback ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n")
                      + QByteArrayLiteral("Content-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: ")
                      + QByteArray::number(page.size()) + "\r\n\r\n" + page);
        socket->disconnectFromHost();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        if (!isCallback || !m_server)
            return;
        cancel();
        log(QStringLiteral("browser callback: %1").arg(params.hasQueryItem("code") ? "code received"
                                                         : "error " + params.queryItemValue("error")));
        if (params.hasQueryItem("error") || params.queryItemValue("state") != m_state) {
            failSignIn(params.queryItemValue("error") == "access_denied"
                                  ? QStringLiteral("O login foi cancelado.")
                                  : QStringLiteral("O login Microsoft não foi concluído."));
            return;
        }
        setStage(QStringLiteral("Conectando sua conta"));
        exchangeToken({{"client_id", m_clientId}, {"scope", kScope}, {"grant_type", "authorization_code"},
                       {"code", params.queryItemValue("code", QUrl::FullyDecoded)}, {"redirect_uri", m_redirectUri},
                       {"code_verifier", m_verifier}},
                      [this](const QString &error, const QVariantMap &tokens) {
            if (!error.isEmpty()) {
                failSignIn(error);
                return;
            }
            log("microsoft token ok");
            const auto refresh = tokens.value("refresh_token").toString();
            minecraftLogin(tokens.value("access_token").toString(), [this, refresh](const QString &error, const QVariantMap &mc) {
                if (!error.isEmpty()) {
                    failSignIn(error);
                    return;
                }
                setStage({});
                const auto uuid = mc.value("uuid").toString();
                log("signed in as " + mc.value("name").toString());
                storeRefreshToken(uuid, refresh);
                m_sessions.insert(uuid, {mc.value("accessToken").toString(), mc.value("xuid").toString(),
                                         mc.value("name").toString(),
                                         QDateTime::currentDateTimeUtc().addSecs(mc.value("expiresIn").toLongLong())});
                downloadSkin(uuid, mc.value("skinUrl").toString());
                emit signedIn({{"uuid", uuid}, {"name", mc.value("name")}});
            });
        });
    });
}

// ── Token chain ────────────────────────────────────────────────────────────

void AuthManager::exchangeToken(const QMap<QString, QString> &form, Done done)
{
    QUrlQuery body;
    for (auto it = form.begin(); it != form.end(); ++it)
        body.addQueryItem(it.key(), QString::fromLatin1(QUrl::toPercentEncoding(it.value())));
    QNetworkRequest request{QUrl(kToken)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(20000);
    auto *reply = m_network->post(request, body.toString(QUrl::FullyEncoded).toLatin1());
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        const auto json = QJsonDocument::fromJson(reply->readAll()).object();
        log(QStringLiteral("token endpoint: HTTP %1 %2 %3")
                .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                .arg(json.value("error").toString(), json.value("error_description").toString().section('\n', 0, 0).left(200)));
        if (json.contains("access_token")) {
            done({}, json.toVariantMap());
            return;
        }
        if (json.value("error").toString() == "invalid_grant") {
            done(QStringLiteral("A sessão Microsoft expirou. Entre na conta novamente."), {});
            return;
        }
        done(reply->error() != QNetworkReply::NoError && json.isEmpty()
                 ? QStringLiteral("Não foi possível falar com a Microsoft. Verifique sua conexão.")
                 : QStringLiteral("A Microsoft recusou o login: %1").arg(json.value("error_description").toString().section('\n', 0, 0)),
             {});
    });
}

QNetworkReply *AuthManager::postJson(const QString &url, const QJsonObject &body, const QString &bearer)
{
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    if (!bearer.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + bearer.toLatin1());
    request.setTransferTimeout(20000);
    return m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void AuthManager::minecraftLogin(const QString &msAccessToken, Done done)
{
    const QJsonObject xbl{{"Properties", QJsonObject{{"AuthMethod", "RPS"}, {"SiteName", "user.auth.xboxlive.com"},
                                                     {"RpsTicket", "d=" + msAccessToken}}},
                          {"RelyingParty", "http://auth.xboxlive.com"}, {"TokenType", "JWT"}};
    auto *xblReply = postJson(QStringLiteral("https://user.auth.xboxlive.com/user/authenticate"), xbl);
    connect(xblReply, &QNetworkReply::finished, this, [this, xblReply, done] {
        xblReply->deleteLater();
        const auto xblJson = QJsonDocument::fromJson(xblReply->readAll()).object();
        log(QStringLiteral("xbox live: HTTP %1 %2").arg(xblReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                .arg(xblReply->errorString()));
        const auto xblToken = xblJson.value("Token").toString();
        if (xblToken.isEmpty()) {
            done(QStringLiteral("Não foi possível entrar no Xbox Live."), {});
            return;
        }
        const QJsonObject xsts{{"Properties", QJsonObject{{"SandboxId", "RETAIL"}, {"UserTokens", QJsonArray{xblToken}}}},
                               {"RelyingParty", "rp://api.minecraftservices.com/"}, {"TokenType", "JWT"}};
        auto *xstsReply = postJson(QStringLiteral("https://xsts.auth.xboxlive.com/xsts/authorize"), xsts);
        connect(xstsReply, &QNetworkReply::finished, this, [this, xstsReply, done] {
            xstsReply->deleteLater();
            const auto xstsJson = QJsonDocument::fromJson(xstsReply->readAll()).object();
            log(QStringLiteral("xsts: HTTP %1 XErr %2").arg(xstsReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                    .arg(qint64(xstsJson.value("XErr").toDouble())));
            const auto xstsToken = xstsJson.value("Token").toString();
            const auto claims = xstsJson.value("DisplayClaims").toObject().value("xui").toArray().first().toObject();
            if (xstsToken.isEmpty()) {
                done(xboxError(xstsJson), {});
                return;
            }
            const auto uhs = claims.value("uhs").toString();
            const auto xuid = claims.value("xid").toString();
            auto *mcReply = postJson(QStringLiteral("https://api.minecraftservices.com/authentication/login_with_xbox"),
                                     {{"identityToken", QStringLiteral("XBL3.0 x=%1;%2").arg(uhs, xstsToken)}});
            connect(mcReply, &QNetworkReply::finished, this, [this, mcReply, done, xuid] {
                mcReply->deleteLater();
                const auto status = mcReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const auto mcBody = mcReply->readAll();
                const auto mcJson = QJsonDocument::fromJson(mcBody).object();
                log(QStringLiteral("minecraft login_with_xbox: HTTP %1 %2").arg(status)
                        .arg(mcJson.contains("access_token") ? QStringLiteral("ok") : QString::fromUtf8(mcBody.left(300))));
                const auto mcToken = mcJson.value("access_token").toString();
                if (mcToken.isEmpty()) {
                    done(status == 403
                             ? QStringLiteral("A Mojang ainda não liberou este app para login (o client ID precisa de aprovação para a API do Minecraft).")
                             : QStringLiteral("Os serviços do Minecraft recusaram o login."),
                         {});
                    return;
                }
                const auto expiresIn = qint64(mcJson.value("expires_in").toDouble(86400));
                QNetworkRequest profileRequest{QUrl(QStringLiteral("https://api.minecraftservices.com/minecraft/profile"))};
                profileRequest.setRawHeader("Authorization", "Bearer " + mcToken.toLatin1());
                profileRequest.setTransferTimeout(20000);
                auto *profileReply = m_network->get(profileRequest);
                connect(profileReply, &QNetworkReply::finished, this, [this, profileReply, done, mcToken, expiresIn, xuid] {
                    profileReply->deleteLater();
                    log(QStringLiteral("minecraft profile: HTTP %1")
                            .arg(profileReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
                    const auto profile = QJsonDocument::fromJson(profileReply->readAll()).object();
                    const auto uuid = profile.value("id").toString();
                    if (uuid.isEmpty()) {
                        done(profileReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404
                                 ? QStringLiteral("Essa conta não possui o Minecraft: Java Edition.")
                                 : QStringLiteral("Não foi possível ler o perfil do Minecraft."),
                             {});
                        return;
                    }
                    QString skinUrl;
                    for (const auto &value : profile.value("skins").toArray())
                        if (value.toObject().value("state").toString() == "ACTIVE")
                            skinUrl = value.toObject().value("url").toString();
                    done({}, {{"uuid", uuid}, {"name", profile.value("name").toString()}, {"accessToken", mcToken},
                              {"expiresIn", expiresIn}, {"xuid", xuid}, {"skinUrl", skinUrl}});
                });
            });
        });
    });
}

// ── Sessions for launching ─────────────────────────────────────────────────

void AuthManager::requestSession(const QString &uuid)
{
    const auto cached = m_sessions.value(uuid);
    if (!cached.accessToken.isEmpty() && cached.expires > QDateTime::currentDateTimeUtc().addSecs(600)) {
        emit sessionReady(uuid, {{"uuid", uuid}, {"name", cached.name}, {"accessToken", cached.accessToken},
                                 {"xuid", cached.xuid}, {"clientId", m_clientId}});
        return;
    }
    const auto refresh = loadRefreshToken(uuid);
    if (!configured() || refresh.isEmpty()) {
        emit sessionFailed(uuid, QStringLiteral("Entre na conta Microsoft novamente para jogar com esse Perfil."));
        return;
    }
    setStage(QStringLiteral("Verificando sua conta Microsoft"));
    exchangeToken({{"client_id", m_clientId}, {"scope", kScope}, {"grant_type", "refresh_token"}, {"refresh_token", refresh}},
                  [this, uuid](const QString &error, const QVariantMap &tokens) {
        if (!error.isEmpty()) {
            setStage({});
            emit sessionFailed(uuid, error);
            return;
        }
        if (!tokens.value("refresh_token").toString().isEmpty())
            storeRefreshToken(uuid, tokens.value("refresh_token").toString());
        minecraftLogin(tokens.value("access_token").toString(), [this, uuid](const QString &error, const QVariantMap &mc) {
            setStage({});
            if (error.isEmpty() && mc.value("uuid").toString() != uuid) {
                emit sessionFailed(uuid, QStringLiteral("A conta Microsoft salva pertence a outro perfil do Minecraft."));
                return;
            }
            if (!error.isEmpty()) {
                emit sessionFailed(uuid, error);
                return;
            }
            Session session{mc.value("accessToken").toString(), mc.value("xuid").toString(), mc.value("name").toString(),
                            QDateTime::currentDateTimeUtc().addSecs(mc.value("expiresIn").toLongLong())};
            m_sessions.insert(uuid, session);
            downloadSkin(uuid, mc.value("skinUrl").toString());
            emit sessionReady(uuid, {{"uuid", uuid}, {"name", session.name}, {"accessToken", session.accessToken},
                                     {"xuid", session.xuid}, {"clientId", m_clientId}});
        });
    });
}

void AuthManager::forget(const QString &uuid)
{
    m_sessions.remove(uuid);
    deleteRefreshToken(uuid);
    QFile::remove(skinPath(uuid));
}

// ── Skins ──────────────────────────────────────────────────────────────────

QString AuthManager::skinPath(const QString &uuid) const
{
    return QDir(m_root).filePath("skins/" + uuid + ".png");
}

QString AuthManager::skinSource(const QString &uuid) const
{
    const QFileInfo file(skinPath(uuid));
    if (uuid.isEmpty() || !file.exists())
        return {};
    // The query defeats the image cache after a skin update.
    return QUrl::fromLocalFile(file.filePath()).toString() + "?v=" + QString::number(file.lastModified().toSecsSinceEpoch());
}

void AuthManager::downloadSkin(const QString &uuid, const QString &url)
{
    if (url.isEmpty())
        return;
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(20000);
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, uuid] {
        reply->deleteLater();
        const auto data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || !data.startsWith("\x89PNG"))
            return;
        QDir().mkpath(QFileInfo(skinPath(uuid)).absolutePath());
        QSaveFile file(skinPath(uuid));
        if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit()) {
            ++m_skinRevision;
            emit skinChanged(uuid);
            emit skinsUpdated();
        }
    });
}

// ── Credential Manager ─────────────────────────────────────────────────────

bool AuthManager::storeRefreshToken(const QString &uuid, const QString &token)
{
#ifdef Q_OS_WIN
    const auto target = credentialTarget(uuid).toStdWString();
    auto blob = token.toUtf8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target.c_str());
    credential.CredentialBlobSize = DWORD(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"OmniLauncher");
    return CredWriteW(&credential, 0);
#else
    Q_UNUSED(uuid) Q_UNUSED(token)
    return false;
#endif
}

QString AuthManager::loadRefreshToken(const QString &uuid)
{
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(credentialTarget(uuid).toStdWString().c_str(), CRED_TYPE_GENERIC, 0, &credential))
        return {};
    const auto token = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
                                         qsizetype(credential->CredentialBlobSize));
    CredFree(credential);
    return token;
#else
    Q_UNUSED(uuid)
    return {};
#endif
}

void AuthManager::deleteRefreshToken(const QString &uuid)
{
#ifdef Q_OS_WIN
    CredDeleteW(credentialTarget(uuid).toStdWString().c_str(), CRED_TYPE_GENERIC, 0);
#else
    Q_UNUSED(uuid)
#endif
}
