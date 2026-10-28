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
        QDir(m_packDirectory).removeRecursively();
        m_packProjectId.clear();
        m_packGameId.clear();
        setPackState({});
        emit packFinished(gameId, true, QStringLiteral("Modpack instalado."));
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
    setPackState({});
    emit packFinished(gameId, false, message);
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

void ModManager::preparePack(const QString &projectId, const QString &imageUrl)
{
    if (!m_packState.isEmpty() || projectId.isEmpty())
        return;
    m_packProjectId = projectId;
    setPackState(QStringLiteral("preparing"));
    const auto loaders = QString::fromUtf8(QJsonDocument(QJsonArray{"fabric", "forge", "neoforge"}).toJson(QJsonDocument::Compact));
    const auto url = QStringLiteral("%1project/%2/version?loaders=%3")
                         .arg(kApi, projectId, QString::fromUtf8(QUrl::toPercentEncoding(loaders)));
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, imageUrl] {
        reply->deleteLater();
        const auto versions = QJsonDocument::fromJson(reply->readAll()).array();
        if (reply->error() != QNetworkReply::NoError || versions.isEmpty()) {
            failPack(QStringLiteral("Não foi possível obter uma versão desse modpack."));
            return;
        }
        const auto version = versions.first().toObject();
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
                                       {"loader", loader}, {"loaderVersion", loaderVersion}, {"coverPath", coverPath}});
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
        jobs.append({QUrl(downloads.first().toString()), instance.filePath(relative),
                     file.value("hashes").toObject().value("sha1").toString(), file.value("fileSize").toInteger(-1)});
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
                                           {"versionNumber", entry.value("versionNumber").toString()}});
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
