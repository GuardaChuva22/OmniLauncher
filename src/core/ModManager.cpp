#include "ModManager.h"
#include "Downloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QDateTime>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>

namespace {
const QString kApi = QStringLiteral("https://api.modrinth.com/v2/");
const QString kMetadataFile = QStringLiteral(".omni-mods.json");

QString jsonList(const QString &value)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact));
}
} // namespace

ModManager::ModManager(const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_root(QDir(dataDirectory).absolutePath()), m_network(new QNetworkAccessManager(this))
    , m_packDownloader(new Downloader(m_network, this))
{
    connect(m_packDownloader, &Downloader::progress, this, [this] {
        const auto total = m_packDownloader->totalBytes();
        setPackState(m_packState, total > 0 ? double(m_packDownloader->receivedBytes()) / double(total) : -1);
    });
    connect(m_packDownloader, &Downloader::finished, this, [this](bool ok, const QString &error) {
        if (m_packState != "downloading")
            return;
        const auto gameId = m_packGameId;
        if (!ok) {
            failPack(error.isEmpty() ? QStringLiteral("Instalação do modpack cancelada.") : error, gameId);
            return;
        }
        // Overrides (configs, resource packs, extra mods) go on top of the downloaded files.
        const QDir instance(QDir(m_root).filePath("instances/" + gameId));
        for (const auto &folder : {QStringLiteral("overrides"), QStringLiteral("client-overrides")}) {
            const QDir source(QDir(m_packDirectory).filePath(folder));
            QDirIterator it(source.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const auto file = it.next();
                const auto target = instance.filePath(source.relativeFilePath(file));
                QDir().mkpath(QFileInfo(target).absolutePath());
                QFile::remove(target);
                QFile::copy(file, target);
            }
        }
        // Remember which files belong to this pack version (for a later version change).
        QJsonArray owned;
        for (const auto &value : m_packIndex.value("files").toArray()) {
            const auto hashes = value.toObject().value("hashes").toObject();
            owned.append(QJsonObject{{"path", QDir::cleanPath(value.toObject().value("path").toString())},
                                     {"hash", hashes.value("sha256").toString(hashes.value("sha1").toString())}});
        }
        for (const auto &folder : {QStringLiteral("overrides"), QStringLiteral("client-overrides")}) {
            const QDir source(QDir(m_packDirectory).filePath(folder));
            QDirIterator it(source.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext())
                owned.append(source.relativeFilePath(it.next()));
        }
        QSaveFile record(instance.filePath(".omni-pack.json"));
        const auto recordData = QJsonDocument(QJsonObject{{"projectId", m_packVersion.value("projectId").toString()},
                                                          {"versionId", m_packVersion.value("versionId").toString()},
                                                          {"customUrl", m_packVersion.value("url").toString()},
                                                          {"version", m_packVersion.value("versionNumber").toString()},
                                                          {"files", owned}}).toJson();
        if (record.open(QIODevice::WriteOnly) && record.write(recordData) == recordData.size())
            record.commit();
        QDir(m_packDirectory).removeRecursively();
        const auto pack = m_packVersion;
        const bool changed = !m_packTargetGame.isEmpty();
        m_packProjectId.clear();
        m_packGameId.clear();
        m_packTargetGame.clear();
        setPackState({});
        emit packFinished(gameId, true,
                          changed ? (pack.value("kind") == "custom" ? QStringLiteral("Modpack atualizado para %1.").arg(pack.value("versionNumber").toString())
                                                                    : QStringLiteral("Versão do modpack alterada."))
                                  : QStringLiteral("Modpack instalado."),
                          pack);
    });
}

void ModManager::setPackState(const QString &state, double progress)
{
    m_packState = state;
    m_packProgress = progress;
    emit packChanged();
}

void ModManager::failPack(const QString &message, const QString &gameId)
{
    if (!m_packDirectory.isEmpty())
        QDir(m_packDirectory).removeRecursively();
    m_packProjectId.clear();
    m_packGameId.clear();
    m_packTargetGame.clear();
    setPackState({});
    emit packFinished(gameId, false, message, {});
}

void ModManager::searchPacks(const QString &query, int offset)
{
    const auto serial = ++m_packSearchSerial;
    QUrlQuery params;
    params.addQueryItem("query", query.trimmed());
    params.addQueryItem("limit", "20");
    params.addQueryItem("offset", QString::number(qMax(0, offset)));
    params.addQueryItem("index", query.trimmed().isEmpty() ? "downloads" : "relevance");
    // Loaders the launcher can start (facet entries in one array are OR'ed).
    const QJsonArray facets{QJsonArray{"project_type:modpack"},
                            QJsonArray{"categories:fabric", "categories:forge", "categories:neoforge"}};
    params.addQueryItem("facets", QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));
    QUrl url(kApi + "search");
    url.setQuery(params);
    m_packSearching = true;
    m_packSearchError.clear();
    if (offset == 0)
        m_packResults.clear();
    emit packResultsChanged();
    auto *reply = get(url.toString());
    connect(reply, &QNetworkReply::finished, this, [this, reply, serial, offset] {
        reply->deleteLater();
        if (serial != m_packSearchSerial)
            return;
        m_packSearching = false;
        if (reply->error() != QNetworkReply::NoError) {
            m_packSearchError = QStringLiteral("Não foi possível falar com o Modrinth. Verifique sua conexão.");
            emit packResultsChanged();
            return;
        }
        const auto root = QJsonDocument::fromJson(reply->readAll()).object();
        if (offset == 0)
            m_packResults.clear();
        for (const auto &value : root.value("hits").toArray()) {
            const auto hit = value.toObject();
            QStringList versions;
            for (const auto &version : hit.value("versions").toArray())
                versions << version.toString();
            m_packResults.append(QVariantMap{
                {"id", hit.value("project_id").toString()},
                {"title", hit.value("title").toString()},
                {"author", hit.value("author").toString()},
                {"description", hit.value("description").toString()},
                {"iconUrl", hit.value("icon_url").toString()},
                {"imageUrl", hit.value("featured_gallery").toString(hit.value("icon_url").toString())},
                {"downloads", hit.value("downloads").toDouble()},
                {"latestVersion", versions.isEmpty() ? QString() : versions.last()},
            });
        }
        m_packTotalHits = root.value("total_hits").toInt();
        emit packResultsChanged();
    });
}

void ModManager::preparePack(const QString &projectId, const QString &imageUrl, const QString &versionId,
                             const QString &targetGameId)
{
    if (!m_packState.isEmpty() || projectId.isEmpty())
        return;
    m_packProjectId = projectId;
    m_packTargetGame = targetGameId;
    setPackState(QStringLiteral("preparing"));
    const auto loaders = QString::fromUtf8(QJsonDocument(QJsonArray{"fabric", "forge", "neoforge"}).toJson(QJsonDocument::Compact));
    const auto url = versionId.isEmpty()
        ? QStringLiteral("%1project/%2/version?loaders=%3").arg(kApi, projectId, QString::fromUtf8(QUrl::toPercentEncoding(loaders)))
        : kApi + "version/" + versionId;
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, imageUrl] {
        reply->deleteLater();
        const auto document = QJsonDocument::fromJson(reply->readAll());
        const auto version = document.isArray() ? document.array().first().toObject() : document.object();
        if (reply->error() != QNetworkReply::NoError || version.isEmpty()) {
            failPack(QStringLiteral("Não foi possível obter uma versão desse modpack."));
            return;
        }
        m_packVersion = {{"projectId", version.value("project_id").toString()}, {"versionId", version.value("id").toString()},
                         {"versionNumber", version.value("version_number").toString()}};
        QJsonObject file;
        for (const auto &value : version.value("files").toArray())
            if (value.toObject().value("primary").toBool() || file.isEmpty())
                file = value.toObject();
        if (!file.value("filename").toString().endsWith(".mrpack")) {
            failPack(QStringLiteral("Esse modpack não tem um arquivo .mrpack."));
            return;
        }
        const auto cache = QDir(m_root).filePath("cache/packs");
        QDir().mkpath(cache);
        const auto archive = QDir(cache).filePath(version.value("id").toString() + ".mrpack");
        m_packDirectory = QDir(cache).filePath(version.value("id").toString());
        const auto expected = file.value("hashes").toObject().value("sha1").toString();
        auto *download = get(file.value("url").toString());
        connect(download, &QNetworkReply::finished, this, [this, download, archive, expected, imageUrl] {
            download->deleteLater();
            const auto data = download->readAll();
            const auto sha1 = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
            QSaveFile out(archive);
            if (download->error() != QNetworkReply::NoError
                || (!expected.isEmpty() && sha1.compare(expected, Qt::CaseInsensitive) != 0)
                || !out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
                failPack(QStringLiteral("Falha ao baixar o modpack."));
                return;
            }
            // .mrpack is a zip; Windows' bundled tar extracts it.
            QDir(m_packDirectory).removeRecursively();
            QDir().mkpath(m_packDirectory);
            auto *tar = new QProcess(this);
            connect(tar, &QProcess::finished, this, [this, tar, archive, imageUrl](int code) {
                tar->deleteLater();
                QFile::remove(archive);
                QFile indexFile(QDir(m_packDirectory).filePath("modrinth.index.json"));
                if (code != 0 || !indexFile.open(QIODevice::ReadOnly)) {
                    failPack(QStringLiteral("O arquivo do modpack está corrompido."));
                    return;
                }
                m_packIndex = QJsonDocument::fromJson(indexFile.readAll()).object();
                const auto dependencies = m_packIndex.value("dependencies").toObject();
                const auto minecraft = dependencies.value("minecraft").toString();
                QString loader;
                QString loaderVersion;
                for (const auto &[key, name] : {std::pair{"fabric-loader", "Fabric"}, std::pair{"neoforge", "NeoForge"},
                                                std::pair{"forge", "Forge"}}) {
                    if (dependencies.contains(key)) {
                        loader = name;
                        loaderVersion = dependencies.value(key).toString();
                        break;
                    }
                }
                if (minecraft.isEmpty() || loader.isEmpty()) {
                    failPack(QStringLiteral("Esse modpack usa um loader que ainda não é suportado."));
                    return;
                }
                const auto emitPrepared = [this, minecraft, loader, loaderVersion](const QString &coverPath) {
                    setPackState(QStringLiteral("waiting"));
                    emit packPrepared({{"title", m_packIndex.value("name").toString()}, {"version", minecraft},
                                       {"loader", loader}, {"loaderVersion", loaderVersion}, {"coverPath", coverPath},
                                       {"gameId", m_packTargetGame}});
                };
                if (imageUrl.isEmpty()) {
                    emitPrepared({});
                    return;
                }
                auto *image = get(imageUrl);
                connect(image, &QNetworkReply::finished, this, [this, image, emitPrepared] {
                    image->deleteLater();
                    const auto bytes = image->readAll();
                    const auto path = QDir(m_packDirectory).filePath("omni-cover");
                    QFile file(path);
                    if (image->error() == QNetworkReply::NoError && !bytes.isEmpty() && file.open(QIODevice::WriteOnly)
                        && file.write(bytes) == bytes.size()) {
                        file.close();
                        emitPrepared(QUrl::fromLocalFile(path).toString());
                    } else {
                        emitPrepared({});
                    }
                });
            });
            tar->start(QStandardPaths::findExecutable("tar"), {"-xf", archive, "-C", m_packDirectory});
        });
    });
}

void ModManager::installPackFiles(const QString &gameId)
{
    if (m_packState != "waiting" || gameId.isEmpty())
        return;
    m_packGameId = gameId;
    const QDir instance(QDir(m_root).filePath("instances/" + gameId));
    // Updating a pack the player already has: zip the worlds first, like the old launcher did.
    if (instance.exists(".omni-pack.json") && !QDir(instance.filePath("saves")).isEmpty()) {
        QDir().mkpath(instance.filePath("backups"));
        const auto archive = instance.filePath(QStringLiteral("backups/saves-antes-de-%1-%2.zip")
                                                   .arg(m_packVersion.value("versionNumber").toString().replace(QRegularExpression("[^A-Za-z0-9._-]"), "_"),
                                                        QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss")));
        setPackState(QStringLiteral("backing-up"));
        auto *tar = new QProcess(this);
        tar->setWorkingDirectory(instance.path());
        connect(tar, &QProcess::finished, this, [this, tar, gameId](int code) {
            tar->deleteLater();
            if (code != 0) {
                failPack(QStringLiteral("Não foi possível fazer o backup dos mundos antes de atualizar."), gameId);
                return;
            }
            m_packState = QStringLiteral("waiting");
            m_packGameId.clear();
            installPackFilesNow(gameId);
        });
        tar->start(QStandardPaths::findExecutable(QStringLiteral("tar")), {"-a", "-c", "-f", archive, "saves"});
        return;
    }
    installPackFilesNow(gameId);
}

void ModManager::installPackFilesNow(const QString &gameId)
{
    m_packGameId = gameId;
    const QDir instance(QDir(m_root).filePath("instances/" + gameId));
    // Changing versions: files of the previous pack version that are gone or changed are
    // removed; identical ones stay (no re-download). Worlds and the player's own additions stay.
    QHash<QString, QString> wanted; // path → hash
    for (const auto &value : m_packIndex.value("files").toArray()) {
        const auto hashes = value.toObject().value("hashes").toObject();
        wanted.insert(QDir::cleanPath(value.toObject().value("path").toString()),
                      hashes.value("sha256").toString(hashes.value("sha1").toString()));
    }
    QFile previous(instance.filePath(".omni-pack.json"));
    if (previous.open(QIODevice::ReadOnly)) {
        for (const auto &value : QJsonDocument::fromJson(previous.readAll()).object().value("files").toArray()) {
            const auto object = value.toObject();
            const auto relative = QDir::cleanPath(value.isString() ? value.toString() : object.value("path").toString());
            const auto hash = object.value("hash").toString();
            if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.startsWith("..") || relative.contains(':'))
                continue;
            if (!hash.isEmpty() && wanted.value(relative) == hash)
                continue;
            QFile::remove(instance.filePath(relative));
            QFile::remove(instance.filePath(relative + ".disabled"));
        }
        previous.close();
    }
    QList<Downloader::Job> jobs;
    for (const auto &value : m_packIndex.value("files").toArray()) {
        const auto file = value.toObject();
        if (file.value("env").toObject().value("client").toString() == "unsupported")
            continue;
        // Paths come from the pack: keep them inside the game folder.
        const auto relative = QDir::cleanPath(file.value("path").toString());
        if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.startsWith("..") || relative.contains(':'))
            continue;
        const auto downloads = file.value("downloads").toArray();
        if (downloads.isEmpty())
            continue;
        const auto hashes = file.value("hashes").toObject();
        jobs.append({QUrl(downloads.first().toString()), instance.filePath(relative), hashes.value("sha1").toString(),
                     file.value("fileSize").toInteger(-1), hashes.value("sha256").toString()});
    }
    setPackState(QStringLiteral("downloading"), 0);
    m_packDownloader->start(jobs);
}

void ModManager::cancelPack()
{
    if (m_packState == "downloading")
        m_packDownloader->cancel();
    else if (!m_packState.isEmpty())
        failPack(QStringLiteral("Instalação do modpack cancelada."));
}

QNetworkReply *ModManager::get(const QString &url)
{
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    // Modrinth asks API clients for an identifying User-Agent.
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniLauncher/0.1.0 (desktop launcher)"));
    return m_network->get(request);
}

QString ModManager::modsDirectory(const QString &gameId) const
{
    return QDir(m_root).filePath("instances/" + gameId + "/mods");
}

QJsonObject ModManager::readMetadata(const QString &gameId) const
{
    QFile file(QDir(modsDirectory(gameId)).filePath(kMetadataFile));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool ModManager::writeMetadata(const QString &gameId, const QJsonObject &metadata) const
{
    QDir().mkpath(modsDirectory(gameId));
    QSaveFile file(QDir(modsDirectory(gameId)).filePath(kMetadataFile));
    const auto data = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QStringList ModManager::installedProjects() const
{
    QStringList projects;
    for (const auto &value : m_installed) {
        const auto project = value.toMap().value("projectId").toString();
        if (!project.isEmpty())
            projects << project;
    }
    return projects;
}

// ── Catalog ────────────────────────────────────────────────────────────────

void ModManager::search(const QString &query, const QString &gameVersion, const QString &loader, int offset)
{
    const auto serial = ++m_searchSerial;
    QUrlQuery params;
    params.addQueryItem("query", query.trimmed());
    params.addQueryItem("limit", "20");
    params.addQueryItem("offset", QString::number(qMax(0, offset)));
    params.addQueryItem("index", query.trimmed().isEmpty() ? "downloads" : "relevance");
    const QJsonArray facets{QJsonArray{"project_type:mod"}, QJsonArray{"categories:" + loader.toLower()},
                            QJsonArray{"versions:" + gameVersion}};
    params.addQueryItem("facets", QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));
    QUrl url(kApi + "search");
    url.setQuery(params);

    m_searching = true;
    m_searchError.clear();
    if (offset == 0)
        m_results.clear();
    emit resultsChanged();

    auto *reply = get(url.toString());
    connect(reply, &QNetworkReply::finished, this, [this, reply, serial, offset] {
        reply->deleteLater();
        if (serial != m_searchSerial)
            return; // A newer search replaced this one.
        m_searching = false;
        if (reply->error() != QNetworkReply::NoError) {
            m_searchError = QStringLiteral("Não foi possível falar com o Modrinth. Verifique sua conexão.");
            emit resultsChanged();
            return;
        }
        const auto root = QJsonDocument::fromJson(reply->readAll()).object();
        if (offset == 0)
            m_results.clear();
        for (const auto &value : root.value("hits").toArray()) {
            const auto hit = value.toObject();
            m_results.append(QVariantMap{
                {"id", hit.value("project_id").toString()},
                {"title", hit.value("title").toString()},
                {"author", hit.value("author").toString()},
                {"description", hit.value("description").toString()},
                {"iconUrl", hit.value("icon_url").toString()},
                {"downloads", hit.value("downloads").toDouble()},
            });
        }
        m_totalHits = root.value("total_hits").toInt();
        emit resultsChanged();
    });
}

// ── Installation ───────────────────────────────────────────────────────────

void ModManager::install(const QString &projectId, const QString &gameId, const QString &gameVersion,
                         const QString &loader)
{
    if (projectId.isEmpty() || gameId.isEmpty() || m_installing.contains(projectId))
        return;
    m_installing.insert(projectId);
    emit installingChanged();
    auto *job = new Install{projectId, gameId, gameVersion, loader.toLower(), {}, 0, 0, {}};
    step(job, +1); // Held until the root project is resolved.
    resolveProject(job, projectId, {});
    step(job, -1);
}

void ModManager::resolveProject(Install *job, const QString &projectId, const QString &)
{
    if (job->seenProjects.contains(projectId))
        return;
    job->seenProjects.insert(projectId);
    // Dependencies already present in this game are left as they are.
    if (projectId != job->rootProject) {
        const auto metadata = readMetadata(job->gameId);
        for (auto it = metadata.begin(); it != metadata.end(); ++it) {
            if (it.value().toObject().value("projectId").toString() == projectId)
                return;
        }
    }
    step(job, +1);
    const auto url = QStringLiteral("%1project/%2/version?loaders=%3&game_versions=%4")
                         .arg(kApi, projectId, QString::fromUtf8(QUrl::toPercentEncoding(jsonList(job->loader))),
                              QString::fromUtf8(QUrl::toPercentEncoding(jsonList(job->gameVersion))));
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, job, projectId] {
        reply->deleteLater();
        const auto versions = QJsonDocument::fromJson(reply->readAll()).array();
        if (reply->error() != QNetworkReply::NoError) {
            job->error = QStringLiteral("Não foi possível falar com o Modrinth.");
        } else if (versions.isEmpty()) {
            if (job->error.isEmpty())
                job->error = projectId == job->rootProject
                    ? QStringLiteral("Esse mod não tem versão para o Minecraft %1 com %2.").arg(job->gameVersion, job->loader)
                    : QStringLiteral("Uma dependência obrigatória não tem versão compatível.");
        } else {
            installVersion(job, versions.first().toObject(), {});
        }
        step(job, -1);
    });
}

void ModManager::resolveVersion(Install *job, const QString &versionId)
{
    step(job, +1);
    auto *reply = get(kApi + "version/" + versionId);
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        reply->deleteLater();
        const auto version = QJsonDocument::fromJson(reply->readAll()).object();
        const auto project = version.value("project_id").toString();
        if (reply->error() != QNetworkReply::NoError || project.isEmpty()) {
            job->error = QStringLiteral("Não foi possível obter uma dependência do mod.");
        } else if (!job->seenProjects.contains(project)) {
            job->seenProjects.insert(project);
            installVersion(job, version, {});
        }
        step(job, -1);
    });
}

void ModManager::installVersion(Install *job, const QJsonObject &version, const QString &)
{
    for (const auto &value : version.value("dependencies").toArray()) {
        const auto dependency = value.toObject();
        if (dependency.value("dependency_type").toString() != "required")
            continue;
        const auto versionId = dependency.value("version_id").toString();
        const auto projectId = dependency.value("project_id").toString();
        if (!versionId.isEmpty() && !job->seenProjects.contains(projectId))
            resolveVersion(job, versionId);
        else if (!projectId.isEmpty())
            resolveProject(job, projectId, {});
    }

    const auto files = version.value("files").toArray();
    QJsonObject file;
    for (const auto &value : files) {
        if (value.toObject().value("primary").toBool()) {
            file = value.toObject();
            break;
        }
    }
    if (file.isEmpty() && !files.isEmpty())
        file = files.first().toObject();
    const auto fileName = QFileInfo(file.value("filename").toString()).fileName();
    if (fileName.isEmpty() || !fileName.endsWith(".jar", Qt::CaseInsensitive)) {
        job->error = QStringLiteral("O Modrinth não forneceu um arquivo .jar para esse mod.");
        return;
    }
    const auto expectedSha1 = file.value("hashes").toObject().value("sha1").toString();
    const QJsonObject entry{{"projectId", version.value("project_id").toString()},
                            {"versionId", version.value("id").toString()},
                            {"versionNumber", version.value("version_number").toString()}};

    step(job, +1);
    auto *reply = get(file.value("url").toString());
    connect(reply, &QNetworkReply::finished, this, [this, reply, job, fileName, expectedSha1, entry] {
        reply->deleteLater();
        const auto data = reply->readAll();
        const auto sha1 = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
        if (reply->error() != QNetworkReply::NoError || data.isEmpty()
            || (!expectedSha1.isEmpty() && sha1.compare(expectedSha1, Qt::CaseInsensitive) != 0)) {
            job->error = QStringLiteral("Falha ao baixar %1.").arg(fileName);
            step(job, -1);
            return;
        }
        const QDir mods(modsDirectory(job->gameId));
        QDir().mkpath(mods.path());
        QSaveFile out(mods.filePath(fileName));
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
            job->error = QStringLiteral("Não foi possível gravar %1.").arg(fileName);
            step(job, -1);
            return;
        }
        // Replace an older file of the same project (an update), keeping its title/icon.
        auto metadata = readMetadata(job->gameId);
        auto updated = entry;
        for (const auto &key : metadata.keys()) {
            const auto old = metadata.value(key).toObject();
            if (key != fileName && old.value("projectId") == entry.value("projectId")) {
                QFile::remove(mods.filePath(key));
                QFile::remove(mods.filePath(key + ".disabled"));
                updated.insert("title", old.value("title"));
                updated.insert("iconUrl", old.value("iconUrl"));
                metadata.remove(key);
            }
        }
        metadata.insert(fileName, updated);
        writeMetadata(job->gameId, metadata);
        ++job->added;
        step(job, -1);
    });
}

void ModManager::step(Install *job, int delta)
{
    job->pending += delta;
    if (job->pending > 0)
        return;

    // Done: fill in titles and icons for new entries, then report.
    auto metadata = readMetadata(job->gameId);
    QStringList missing;
    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        const auto entry = it.value().toObject();
        if (entry.value("title").toString().isEmpty() && !entry.value("projectId").toString().isEmpty())
            missing << entry.value("projectId").toString();
    }
    const auto finish = [this, job] {
        m_installing.remove(job->rootProject);
        emit installingChanged();
        if (job->gameId == m_gameId)
            loadInstalled(m_gameId);
        const bool ok = job->error.isEmpty();
        emit installFinished(job->rootProject, ok,
                             ok ? (job->added > 1 ? QStringLiteral("Mod instalado com %1 dependência(s).").arg(job->added - 1)
                                                  : QStringLiteral("Mod instalado."))
                                : job->error);
        delete job;
    };
    if (missing.isEmpty()) {
        finish();
        return;
    }
    QJsonArray ids;
    for (const auto &id : missing)
        ids.append(id);
    auto *reply = get(kApi + "projects?ids="
                      + QString::fromUtf8(QUrl::toPercentEncoding(QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact)))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, job, finish] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QHash<QString, QJsonObject> projects;
            for (const auto &value : QJsonDocument::fromJson(reply->readAll()).array())
                projects.insert(value.toObject().value("id").toString(), value.toObject());
            auto metadata = readMetadata(job->gameId);
            for (const auto &key : metadata.keys()) {
                auto entry = metadata.value(key).toObject();
                const auto project = projects.value(entry.value("projectId").toString());
                if (entry.value("title").toString().isEmpty() && !project.isEmpty()) {
                    entry.insert("title", project.value("title"));
                    entry.insert("iconUrl", project.value("icon_url"));
                    metadata.insert(key, entry);
                }
            }
            writeMetadata(job->gameId, metadata);
        }
        finish();
    });
}

// ── Installed mods ─────────────────────────────────────────────────────────

void ModManager::loadInstalled(const QString &gameId)
{
    m_gameId = gameId;
    m_installed.clear();
    if (!gameId.isEmpty()) {
        const auto metadata = readMetadata(gameId);
        const QDir mods(modsDirectory(gameId));
        for (const auto &file : mods.entryList({"*.jar", "*.jar.disabled"}, QDir::Files, QDir::Name)) {
            const bool enabled = file.endsWith(".jar", Qt::CaseInsensitive);
            const auto base = enabled ? file : file.chopped(int(QStringLiteral(".disabled").size()));
            const auto entry = metadata.value(base).toObject();
            auto title = entry.value("title").toString();
            if (title.isEmpty())
                title = base.chopped(4);
            m_installed.append(QVariantMap{{"file", file}, {"enabled", enabled}, {"title", title},
                                           {"projectId", entry.value("projectId").toString()},
                                           {"iconUrl", entry.value("iconUrl").toString()},
                                           {"versionNumber", entry.value("versionNumber").toString()},
                                           {"versionId", entry.value("versionId").toString()},
                                           {"updateVersionId", entry.value("updateVersionId").toString()},
                                           {"updateVersionNumber", entry.value("updateVersionNumber").toString()}});
        }
        std::sort(m_installed.begin(), m_installed.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value("title").toString().compare(b.toMap().value("title").toString(), Qt::CaseInsensitive) < 0;
        });
    }
    emit installedChanged();
}

bool ModManager::setEnabled(const QString &gameId, const QString &file, bool enabled)
{
    const QDir mods(modsDirectory(gameId));
    const bool isEnabled = file.endsWith(".jar", Qt::CaseInsensitive);
    if (QFileInfo(file).fileName() != file || isEnabled == enabled)
        return isEnabled == enabled;
    const auto target = enabled ? file.chopped(int(QStringLiteral(".disabled").size())) : file + ".disabled";
    const bool ok = QFile::rename(mods.filePath(file), mods.filePath(target));
    loadInstalled(gameId);
    return ok;
}

bool ModManager::remove(const QString &gameId, const QString &file)
{
    if (QFileInfo(file).fileName() != file)
        return false;
    const QDir mods(modsDirectory(gameId));
    const bool ok = QFile::remove(mods.filePath(file));
    if (ok) {
        auto metadata = readMetadata(gameId);
        metadata.remove(file.endsWith(".disabled") ? file.chopped(int(QStringLiteral(".disabled").size())) : file);
        writeMetadata(gameId, metadata);
    }
    loadInstalled(gameId);
    return ok;
}

// ── Versions, updates ──────────────────────────────────────────────────────

QNetworkReply *ModManager::post(const QString &url, const QJsonObject &body)
{
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(30000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniLauncher/0.1.0 (desktop launcher)"));
    return m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

int ModManager::updateCount() const
{
    int count = 0;
    for (const auto &value : m_installed)
        count += value.toMap().value("updateVersionId").toString().isEmpty() ? 0 : 1;
    return count;
}

void ModManager::fetchProjectVersions(const QString &projectId, const QString &gameVersion, const QString &loader)
{
    const auto serial = ++m_versionSerial;
    m_versionChoices.clear();
    m_versionChoicesLoading = true;
    emit versionChoicesChanged();
    QUrlQuery query;
    if (!loader.isEmpty())
        query.addQueryItem("loaders", jsonList(loader.toLower()));
    if (!gameVersion.isEmpty())
        query.addQueryItem("game_versions", jsonList(gameVersion));
    QUrl url(kApi + "project/" + projectId + "/version");
    url.setQuery(query);
    auto *reply = get(url.toString(QUrl::FullyEncoded));
    connect(reply, &QNetworkReply::finished, this, [this, reply, serial] {
        reply->deleteLater();
        if (serial != m_versionSerial)
            return;
        QVariantList choices;
        for (const auto &value : QJsonDocument::fromJson(reply->readAll()).array()) {
            const auto version = value.toObject();
            QStringList games;
            for (const auto &game : version.value("game_versions").toArray())
                games << game.toString();
            QStringList loaders;
            for (const auto &loader : version.value("loaders").toArray())
                loaders << loader.toString();
            choices.append(QVariantMap{
                {"id", version.value("id").toString()},
                {"versionNumber", version.value("version_number").toString()},
                {"name", version.value("name").toString()},
                {"gameVersions", games.size() > 3 ? games.first() + " – " + games.last() : games.join(", ")},
                {"loaders", loaders.join(", ")},
                {"date", version.value("date_published").toString().left(10)},
                {"type", version.value("version_type").toString()},
            });
        }
        m_versionChoices = choices;
        m_versionChoicesLoading = false;
        emit versionChoicesChanged();
    });
}

void ModManager::fillTitles(const QString &gameId, std::function<void()> done)
{
    const auto metadata = readMetadata(gameId);
    QJsonArray missing;
    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        const auto entry = it.value().toObject();
        if (entry.value("title").toString().isEmpty() && !entry.value("projectId").toString().isEmpty())
            missing.append(entry.value("projectId").toString());
    }
    if (missing.isEmpty()) {
        done();
        return;
    }
    auto *reply = get(kApi + "projects?ids="
                      + QString::fromUtf8(QUrl::toPercentEncoding(QString::fromUtf8(QJsonDocument(missing).toJson(QJsonDocument::Compact)))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, gameId, done] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QHash<QString, QJsonObject> projects;
            for (const auto &value : QJsonDocument::fromJson(reply->readAll()).array())
                projects.insert(value.toObject().value("id").toString(), value.toObject());
            auto metadata = readMetadata(gameId);
            for (const auto &key : metadata.keys()) {
                auto entry = metadata.value(key).toObject();
                const auto project = projects.value(entry.value("projectId").toString());
                if (entry.value("title").toString().isEmpty() && !project.isEmpty()) {
                    entry.insert("title", project.value("title"));
                    entry.insert("iconUrl", project.value("icon_url"));
                    metadata.insert(key, entry);
                }
            }
            writeMetadata(gameId, metadata);
        }
        done();
    });
}

void ModManager::checkUpdates(const QString &gameId, const QString &gameVersion, const QString &loader)
{
    if (gameId.isEmpty() || m_checkingUpdates)
        return;
    // Hash every jar: Modrinth identifies files by SHA-1, wherever they came from.
    const QDir mods(modsDirectory(gameId));
    QHash<QString, QString> fileByHash;
    QJsonArray hashes;
    for (const auto &file : mods.entryList({"*.jar", "*.jar.disabled"}, QDir::Files)) {
        QFile jar(mods.filePath(file));
        if (!jar.open(QIODevice::ReadOnly))
            continue;
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(&jar);
        const auto sha1 = QString::fromLatin1(hash.result().toHex());
        fileByHash.insert(sha1, file.endsWith(".disabled") ? file.chopped(int(QStringLiteral(".disabled").size())) : file);
        hashes.append(sha1);
    }
    if (hashes.isEmpty())
        return;
    m_checkingUpdates = true;
    emit installedChanged();
    const auto finish = [this, gameId] {
        m_checkingUpdates = false;
        if (gameId == m_gameId)
            loadInstalled(gameId);
        else
            emit installedChanged();
    };
    auto *identify = post(kApi + "version_files", {{"hashes", hashes}, {"algorithm", "sha1"}});
    connect(identify, &QNetworkReply::finished, this,
            [this, identify, gameId, gameVersion, loader, hashes, fileByHash, finish] {
        identify->deleteLater();
        if (identify->error() != QNetworkReply::NoError) {
            finish();
            return;
        }
        auto metadata = readMetadata(gameId);
        const auto known = QJsonDocument::fromJson(identify->readAll()).object();
        for (auto it = known.begin(); it != known.end(); ++it) {
            const auto version = it.value().toObject();
            const auto key = fileByHash.value(it.key());
            auto entry = metadata.value(key).toObject();
            entry.insert("projectId", version.value("project_id"));
            entry.insert("versionId", version.value("id"));
            entry.insert("versionNumber", version.value("version_number"));
            metadata.insert(key, entry);
        }
        writeMetadata(gameId, metadata);
        const QJsonObject request{{"hashes", hashes}, {"algorithm", "sha1"},
                                  {"loaders", QJsonArray{loader.toLower()}}, {"game_versions", QJsonArray{gameVersion}}};
        auto *latest = post(kApi + "version_files/update", request);
        connect(latest, &QNetworkReply::finished, this, [this, latest, gameId, fileByHash, finish] {
            latest->deleteLater();
            if (latest->error() == QNetworkReply::NoError) {
                auto metadata = readMetadata(gameId);
                const auto newest = QJsonDocument::fromJson(latest->readAll()).object();
                for (auto it = fileByHash.begin(); it != fileByHash.end(); ++it) {
                    auto entry = metadata.value(it.value()).toObject();
                    if (entry.isEmpty())
                        continue;
                    const auto version = newest.value(it.key()).toObject();
                    if (!version.isEmpty() && version.value("id").toString() != entry.value("versionId").toString()) {
                        entry.insert("updateVersionId", version.value("id"));
                        entry.insert("updateVersionNumber", version.value("version_number"));
                    } else {
                        entry.remove("updateVersionId");
                        entry.remove("updateVersionNumber");
                    }
                    metadata.insert(it.value(), entry);
                }
                writeMetadata(gameId, metadata);
            }
            fillTitles(gameId, finish);
        });
    });
}

void ModManager::installVersionOf(const QString &projectId, const QString &versionId, const QString &gameId,
                                  const QString &gameVersion, const QString &loader)
{
    if (projectId.isEmpty() || versionId.isEmpty() || gameId.isEmpty() || m_installing.contains(projectId))
        return;
    m_installing.insert(projectId);
    emit installingChanged();
    auto *job = new Install{projectId, gameId, gameVersion, loader.toLower(), {projectId}, 0, 0, {}};
    step(job, +1);
    auto *reply = get(kApi + "version/" + versionId);
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        reply->deleteLater();
        const auto version = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || version.isEmpty())
            job->error = QStringLiteral("Não foi possível obter essa versão do mod.");
        else
            installVersion(job, version, {});
        step(job, -1);
    });
}

void ModManager::updateAll(const QString &gameId, const QString &gameVersion, const QString &loader)
{
    for (const auto &value : m_installed) {
        const auto mod = value.toMap();
        if (!mod.value("updateVersionId").toString().isEmpty())
            installVersionOf(mod.value("projectId").toString(), mod.value("updateVersionId").toString(), gameId, gameVersion, loader);
    }
}

// ── Custom packs (manifest by link) ────────────────────────────────────────
// Manifest v1 (the format of the old Vanilla Ultra Enhanced launcher):
// { "pack": {"name", "version", "id"?}, "minecraft": "26.2", "fabric": {"loader": "0.19.3"},
//   "files": [{"path", "url", "size", "sha256"|"sha1"}],
//   "overrides": {"url", "sha256"?} }   ← optional zip (config/, defaultconfigs/…) applied on top

namespace {
struct CustomManifest {
    QString name;
    QString version;
    QString minecraft;
    QString loader;
    QString loaderVersion;
    QJsonArray files; // in .mrpack index shape
    QString overridesUrl;
    QString overridesSha256;
    QString error;
};

CustomManifest parseCustomManifest(const QByteArray &data)
{
    CustomManifest result;
    const auto root = QJsonDocument::fromJson(data).object();
    const auto pack = root.value("pack").toObject();
    result.name = pack.value("name").toString(root.value("name").toString()).trimmed();
    result.version = pack.value("version").toString(root.value("version").toString()).trimmed();
    result.minecraft = root.value("minecraft").toString().trimmed();
    for (const auto &[key, name] : {std::pair{"fabric", "Fabric"}, std::pair{"neoforge", "NeoForge"}, std::pair{"forge", "Forge"}}) {
        if (root.value(key).isObject()) {
            result.loader = name;
            result.loaderVersion = root.value(key).toObject().value("loader").toString(root.value(key).toObject().value("version").toString());
            break;
        }
    }
    if (result.name.isEmpty() || result.version.isEmpty() || result.minecraft.isEmpty() || result.loader.isEmpty()) {
        result.error = QStringLiteral("Esse link não é um manifesto de modpack válido (faltam nome, versão, Minecraft ou loader).");
        return result;
    }
    for (const auto &value : root.value("files").toArray()) {
        const auto file = value.toObject();
        const auto url = file.value("url").toString();
        if (file.value("path").toString().isEmpty() || !(url.startsWith("https://") || url.startsWith("http://")))
            continue;
        QJsonObject hashes;
        if (file.contains("sha256"))
            hashes.insert("sha256", file.value("sha256").toString().toLower());
        if (file.contains("sha1"))
            hashes.insert("sha1", file.value("sha1").toString().toLower());
        result.files.append(QJsonObject{{"path", file.value("path").toString()}, {"downloads", QJsonArray{url}},
                                        {"hashes", hashes}, {"fileSize", file.value("size")}});
    }
    const auto overrides = root.value("overrides").toObject();
    result.overridesUrl = overrides.value("url").toString();
    result.overridesSha256 = overrides.value("sha256").toString().toLower();
    if (result.files.isEmpty())
        result.error = QStringLiteral("O manifesto não lista nenhum arquivo para baixar.");
    return result;
}
} // namespace

void ModManager::prepareCustom(const QString &url, const QString &targetGameId)
{
    const auto link = url.trimmed();
    if (!m_packState.isEmpty() || link.isEmpty())
        return;
    if (!(link.startsWith("https://") || link.startsWith("http://") || link.startsWith("file:"))) {
        emit packFinished({}, false, QStringLiteral("Cole um link http(s) para o manifesto do modpack."), {});
        return;
    }
    m_packProjectId = "custom:" + link;
    m_packTargetGame = targetGameId;
    setPackState(QStringLiteral("preparing"));
    auto *reply = get(link);
    connect(reply, &QNetworkReply::finished, this, [this, reply, link] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            failPack(QStringLiteral("Não foi possível baixar o manifesto do modpack."));
            return;
        }
        const auto manifest = parseCustomManifest(reply->readAll());
        if (!manifest.error.isEmpty()) {
            failPack(manifest.error);
            return;
        }
        const auto dependencyKey = manifest.loader == "Fabric" ? QStringLiteral("fabric-loader") : manifest.loader.toLower();
        m_packIndex = QJsonObject{{"name", manifest.name}, {"files", manifest.files},
                                  {"dependencies", QJsonObject{{"minecraft", manifest.minecraft}, {dependencyKey, manifest.loaderVersion}}}};
        m_packVersion = {{"kind", "custom"}, {"url", link}, {"versionNumber", manifest.version}};
        m_packDirectory = QDir(m_root).filePath("cache/packs/custom");
        QDir(m_packDirectory).removeRecursively();
        const QVariantMap info{{"title", manifest.name}, {"version", manifest.minecraft}, {"loader", manifest.loader},
                               {"loaderVersion", manifest.loaderVersion}, {"coverPath", QString()},
                               {"gameId", m_packTargetGame}, {"kind", "custom"}};
        if (manifest.overridesUrl.isEmpty()) {
            setPackState(QStringLiteral("waiting"));
            emit packPrepared(info);
            return;
        }
        // Overrides zip → <pack dir>/overrides, copied over the game after the files download.
        auto *zipReply = get(manifest.overridesUrl);
        connect(zipReply, &QNetworkReply::finished, this, [this, zipReply, info, expected = manifest.overridesSha256] {
            zipReply->deleteLater();
            const auto data = zipReply->readAll();
            const auto sha256 = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
            const auto archive = QDir(m_root).filePath("cache/packs/custom-overrides.zip");
            const auto target = QDir(m_packDirectory).filePath("overrides");
            QDir().mkpath(target);
            QSaveFile out(archive);
            if (zipReply->error() != QNetworkReply::NoError || data.isEmpty() || (!expected.isEmpty() && sha256 != expected)
                || !out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
                failPack(QStringLiteral("Não foi possível baixar as configurações do modpack."));
                return;
            }
            QProcess tar;
            tar.start(QStandardPaths::findExecutable("tar"), {"-xf", archive, "-C", target});
            const bool ok = tar.waitForFinished(120000) && tar.exitCode() == 0;
            QFile::remove(archive);
            if (!ok) {
                failPack(QStringLiteral("O arquivo de configurações do modpack está corrompido."));
                return;
            }
            setPackState(QStringLiteral("waiting"));
            emit packPrepared(info);
        });
    });
}

void ModManager::checkCustom(const QString &gameId, const QString &url)
{
    if (gameId.isEmpty() || url.isEmpty())
        return;
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gameId] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const auto manifest = parseCustomManifest(reply->readAll());
        if (!manifest.error.isEmpty())
            return;
        m_customLatest.insert(gameId, QVariantMap{{"version", manifest.version}, {"minecraft", manifest.minecraft},
                                                  {"files", manifest.files.size()}});
        emit customLatestChanged();
    });
}

void ModManager::fetchPackIcon(const QString &gameId, const QString &projectId)
{
    if (gameId.isEmpty() || projectId.isEmpty())
        return;
    auto *reply = get(kApi + "project/" + projectId);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gameId, projectId] {
        reply->deleteLater();
        const auto iconUrl = QJsonDocument::fromJson(reply->readAll()).object().value("icon_url").toString();
        if (reply->error() != QNetworkReply::NoError || iconUrl.isEmpty())
            return;
        auto *icon = get(iconUrl);
        connect(icon, &QNetworkReply::finished, this, [this, icon, gameId, projectId] {
            icon->deleteLater();
            const auto data = icon->readAll();
            if (icon->error() != QNetworkReply::NoError || data.isEmpty())
                return;
            const auto path = QDir(m_root).filePath("cache/icons/" + projectId);
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile file(path);
            if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit())
                emit packIconReady(gameId, QUrl::fromLocalFile(path).toString());
        });
    });
}
