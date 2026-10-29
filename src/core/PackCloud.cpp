#include "PackCloud.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#ifndef OMNI_CLOUD_URL
#  define OMNI_CLOUD_URL ""
#endif

namespace {
const QString kModrinth = QStringLiteral("https://api.modrinth.com/v2/");
const qint64 kMaxUpload = 24LL * 1024 * 1024;
const int kMaxUploads = 40;

QString neoforgePrefix(const QString &gameVersion)
{
    const auto parts = gameVersion.split('.');
    if (parts.size() < 2)
        return {};
    if (parts[0] == "1")
        return parts[1] + '.' + parts.value(2, QStringLiteral("0")) + '.';
    return parts[0] + '.' + parts[1] + '.' + parts.value(2, QStringLiteral("0")) + '.';
}
} // namespace

PackCloud::PackCloud(const QString &dataDirectory, QObject *parent)
    : QObject(parent)
    , m_root(dataDirectory)
    , m_network(new QNetworkAccessManager(this))
{
    // OMNI_CLOUD_URL in the environment points a build at another server (a local `wrangler dev`).
    m_base = qEnvironmentVariable("OMNI_CLOUD_URL", QStringLiteral(OMNI_CLOUD_URL)).trimmed();
    while (m_base.endsWith('/'))
        m_base.chop(1);
}

void PackCloud::setState(const QString &state, const QString &detail)
{
    m_state = state;
    m_detail = detail;
    emit stateChanged();
}

void PackCloud::fail(const QString &message)
{
    const auto gameId = m_gameId;
    m_gameId.clear();
    setState({});
    emit published(gameId, false, message, {});
}

QNetworkReply *PackCloud::request(const QString &method, const QString &path, const QByteArray &body, const QString &contentType)
{
    QNetworkRequest request{QUrl(path.startsWith("http") ? path : m_base + path)};
    request.setTransferTimeout(120000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniLauncher/0.1.0 (desktop launcher)"));
    if (!contentType.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    if (!m_key.isEmpty() && !path.startsWith("http"))
        request.setRawHeader("Authorization", "Bearer " + m_key.toLatin1());
    return m_network->sendCustomRequest(request, method.toLatin1(), body);
}

QString PackCloud::codeFrom(const QString &text) const
{
    static const QRegularExpression inLink(QStringLiteral("(?:/p/|omni-editor:)([A-Za-z0-9]{6})"));
    if (const auto match = inLink.match(text); match.hasMatch())
        return match.captured(1).toUpper();
    auto code = text.toUpper();
    code.remove(QRegularExpression(QStringLiteral("[^A-Z0-9]")));
    return code.size() == 6 ? code : QString();
}

QString PackCloud::packUrl(const QString &code) const
{
    return m_base.isEmpty() || code.isEmpty() ? QString() : m_base + "/p/" + code;
}

QString PackCloud::editorLink(const QString &code, const QString &key) const
{
    return QStringLiteral("omni-editor:%1:%2").arg(code, key);
}

QString PackCloud::keyFrom(const QString &text) const
{
    static const QRegularExpression key(QStringLiteral("omni-editor:[A-Za-z0-9]{6}:([0-9a-f]{64})"));
    const auto match = key.match(text.trimmed());
    return match.hasMatch() ? match.captured(1) : QString();
}

QString PackCloud::loaderVersion() const
{
    const auto pinned = m_game.value("loaderVersion").toString();
    if (!pinned.isEmpty())
        return pinned;
    // Unpinned games use what the launcher installed when the game last started.
    const auto loader = m_game.value("loader").toString();
    const auto version = m_game.value("version").toString();
    const QDir versions(QDir(m_root).filePath("versions"));
    if (loader == "Fabric") {
        for (const auto &name : versions.entryList({"fabric-loader-*-" + version}, QDir::Dirs, QDir::Time))
            return name.mid(14, name.size() - 14 - version.size() - 1);
    } else if (loader == "Forge") {
        for (const auto &name : versions.entryList({"forge-" + version + "-*"}, QDir::Dirs, QDir::Time))
            return name.mid(7 + version.size());
    } else if (loader == "NeoForge") {
        for (const auto &name : versions.entryList({"neoforge-" + neoforgePrefix(version) + '*'}, QDir::Dirs, QDir::Time))
            return name.mid(9);
    }
    return {};
}

// ── Publish ────────────────────────────────────────────────────────────────

void PackCloud::publish(const QVariantMap &game, const QVariantMap &settings, const QString &name,
                        const QString &version, const QString &changelog)
{
    if (!m_state.isEmpty())
        return;
    m_gameId = game.value("id").toString();
    if (!configured()) {
        fail(QStringLiteral("A nuvem do OmniLauncher ainda não foi configurada nesta versão."));
        return;
    }
    m_game = game;
    m_code = settings.value("cloudCode").toString();
    m_key = settings.value("cloudKey").toString();
    m_name = name.trimmed();
    m_version = version.trimmed();
    m_changelog = changelog.trimmed().left(2000);
    m_files = {};
    m_local.clear();
    m_bySha1.clear();
    m_uploads.clear();
    m_overrides = {};
    m_overridesFile = {};
    if (!m_code.isEmpty() && m_key.isEmpty()) {
        fail(QStringLiteral("Você segue esse modpack, mas não é editor dele. Peça o link de editor a quem criou."));
        return;
    }
    if (game.value("loader").toString() == "Vanilla") {
        fail(QStringLiteral("Modpacks precisam de Fabric, Forge ou NeoForge."));
        return;
    }
    if (m_name.isEmpty() || m_version.isEmpty()) {
        fail(QStringLiteral("Dê um nome e uma versão ao modpack."));
        return;
    }
    if (loaderVersion().isEmpty()) {
        fail(QStringLiteral("Inicie o jogo uma vez antes de publicar: o pack usa a versão do loader instalada."));
        return;
    }

    setState(QStringLiteral("scanning"), QStringLiteral("Lendo os mods do jogo"));
    const QDir instance(QDir(m_root).filePath("instances/" + m_gameId));
    for (const auto &[folder, pattern] : {std::pair{"mods", "*.jar"}, std::pair{"resourcepacks", "*.zip"}, std::pair{"shaderpacks", "*.zip"}}) {
        const QDir dir(instance.filePath(QString::fromLatin1(folder)));
        for (const auto &info : dir.entryInfoList({QString::fromLatin1(pattern)}, QDir::Files)) {
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly))
                continue;
            QCryptographicHash sha1(QCryptographicHash::Sha1), sha256(QCryptographicHash::Sha256);
            while (!file.atEnd()) {
                const auto chunk = file.read(1 << 20);
                sha1.addData(chunk);
                sha256.addData(chunk);
            }
            Upload upload{info.absoluteFilePath(), QString::fromLatin1(sha256.result().toHex()),
                          QString::fromLatin1(folder) + '/' + info.fileName(), info.size()};
            m_bySha1.insert(QString::fromLatin1(sha1.result().toHex()), upload);
        }
    }
    if (m_bySha1.isEmpty()) {
        fail(QStringLiteral("Esse jogo não tem mods para virar modpack."));
        return;
    }

    // Configs travel as one zip ("overrides"), applied over the game on install and update.
    QStringList folders;
    for (const auto &folder : {QStringLiteral("config"), QStringLiteral("defaultconfigs"), QStringLiteral("kubejs")})
        if (instance.exists(folder))
            folders.append(folder);
    if (!folders.isEmpty()) {
        const auto archive = QDir(m_root).filePath("cache/cloud/" + m_gameId + "-configs.zip");
        QDir().mkpath(QFileInfo(archive).absolutePath());
        QFile::remove(archive);
        QProcess tar;
        tar.setWorkingDirectory(instance.absolutePath());
        tar.start(QStandardPaths::findExecutable(QStringLiteral("tar")), QStringList{"-a", "-c", "-f", archive} + folders);
        QFile zip(archive);
        if (!tar.waitForFinished(120000) || tar.exitCode() != 0 || !zip.open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("Não foi possível juntar as configurações do jogo."));
            return;
        }
        if (zip.size() > kMaxUpload) {
            fail(QStringLiteral("As configurações passam de 24 MB. Tire arquivos grandes da pasta config."));
            return;
        }
        QCryptographicHash sha256(QCryptographicHash::Sha256);
        sha256.addData(&zip);
        m_overridesFile = {archive, QString::fromLatin1(sha256.result().toHex()), {}, zip.size()};
    }
    resolveOnModrinth();
}

void PackCloud::resolveOnModrinth()
{
    setState(QStringLiteral("scanning"), QStringLiteral("Procurando os mods no Modrinth"));
    QJsonArray hashes;
    for (auto it = m_bySha1.begin(); it != m_bySha1.end(); ++it)
        hashes.append(it.key());
    QNetworkRequest req{QUrl(kModrinth + "version_files")};
    req.setTransferTimeout(60000);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniLauncher/0.1.0 (desktop launcher)"));
    auto *reply = m_network->post(req, QJsonDocument(QJsonObject{{"hashes", hashes}, {"algorithm", "sha1"}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("Não foi possível falar com o Modrinth."));
            return;
        }
        const auto versions = QJsonDocument::fromJson(reply->readAll()).object();
        for (auto it = m_bySha1.begin(); it != m_bySha1.end(); ++it) {
            QJsonObject found;
            for (const auto &value : versions.value(it.key()).toObject().value("files").toArray())
                if (value.toObject().value("hashes").toObject().value("sha1").toString() == it.key())
                    found = value.toObject();
            if (!found.isEmpty()) {
                m_files.append(QJsonObject{{"path", it->path}, {"url", found.value("url")}, {"size", found.value("size")},
                                           {"sha1", it.key()}});
            } else {
                if (it->size > kMaxUpload) {
                    fail(QStringLiteral("%1 não está no Modrinth e passa de 24 MB.").arg(QFileInfo(it->path).fileName()));
                    return;
                }
                m_uploads.append(*it);
            }
        }
        if (!m_overridesFile.file.isEmpty())
            m_uploads.append(m_overridesFile);
        if (m_uploads.size() > kMaxUploads) {
            fail(QStringLiteral("Mais de %1 arquivos fora do Modrinth. Prefira as versões do Modrinth desses mods.").arg(kMaxUploads));
            return;
        }
        ensurePack();
    });
}

void PackCloud::ensurePack()
{
    if (!m_code.isEmpty()) {
        uploadNext();
        return;
    }
    setState(QStringLiteral("publishing"), QStringLiteral("Criando o modpack na nuvem"));
    auto *reply = request(QStringLiteral("POST"), QStringLiteral("/api/packs"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const auto answer = QJsonDocument::fromJson(reply->readAll()).object();
        m_code = answer.value("code").toString();
        m_key = answer.value("editorKey").toString();
        if (m_code.isEmpty() || m_key.isEmpty()) {
            fail(answer.value("error").toString(QStringLiteral("A nuvem do OmniLauncher não respondeu.")));
            return;
        }
        uploadNext();
    });
}

void PackCloud::uploadNext()
{
    if (m_uploads.isEmpty()) {
        sendManifest();
        return;
    }
    const auto upload = m_uploads.takeFirst();
    const auto name = upload.path.isEmpty() ? QStringLiteral("configurações") : QFileInfo(upload.path).fileName();
    setState(QStringLiteral("uploading"), QStringLiteral("Enviando %1").arg(name));
    const auto filePath = QStringLiteral("/p/%1/files/%2").arg(m_code, upload.sha256);
    const auto record = [this, upload](const QString &url) {
        if (upload.path.isEmpty())
            m_overrides = QJsonObject{{"url", url}, {"sha256", upload.sha256}};
        else
            m_files.append(QJsonObject{{"path", upload.path}, {"url", url}, {"size", upload.size}, {"sha256", upload.sha256}});
        uploadNext();
    };
    // Already on the server from an earlier version: nothing to send.
    auto *head = request(QStringLiteral("HEAD"), filePath);
    connect(head, &QNetworkReply::finished, this, [this, head, upload, filePath, record] {
        head->deleteLater();
        if (head->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
            record(m_base + filePath);
            return;
        }
        QFile file(upload.file);
        if (!file.open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("Não foi possível ler %1.").arg(upload.file));
            return;
        }
        auto *put = request(QStringLiteral("PUT"), QStringLiteral("/api/packs/%1/files/%2").arg(m_code, upload.sha256),
                            file.readAll(), QStringLiteral("application/octet-stream"));
        connect(put, &QNetworkReply::finished, this, [this, put, record] {
            put->deleteLater();
            const auto answer = QJsonDocument::fromJson(put->readAll()).object();
            const auto url = answer.value("url").toString();
            if (url.isEmpty()) {
                fail(answer.value("error").toString(QStringLiteral("O envio de um arquivo falhou.")));
                return;
            }
            record(url);
        });
    });
}

void PackCloud::sendManifest()
{
    setState(QStringLiteral("publishing"), QStringLiteral("Publicando a versão %1").arg(m_version));
    QJsonObject manifest{
        {"pack", QJsonObject{{"name", m_name}, {"version", m_version}, {"changelog", m_changelog}}},
        {"minecraft", m_game.value("version").toString()},
        {m_game.value("loader").toString().toLower(), QJsonObject{{"loader", loaderVersion()}}},
        {"files", m_files},
    };
    if (!m_overrides.isEmpty())
        manifest.insert("overrides", m_overrides);
    auto *reply = request(QStringLiteral("PUT"), "/api/packs/" + m_code, QJsonDocument(manifest).toJson(QJsonDocument::Compact),
                          QStringLiteral("application/json"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const auto answer = QJsonDocument::fromJson(reply->readAll()).object();
        if (!answer.contains("version")) {
            fail(answer.value("error").toString(QStringLiteral("A publicação falhou.")));
            return;
        }
        const auto gameId = m_gameId;
        const QVariantMap result{{"code", m_code}, {"editorKey", m_key}, {"version", m_version}, {"url", packUrl(m_code)},
                                 {"files", m_files.size()}};
        m_gameId.clear();
        setState({});
        emit published(gameId, true, QStringLiteral("Versão %1 publicada.").arg(m_version), result);
    });
}
