#include "GameRunner.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <QWinEventNotifier>
#  include <windows.h>
#endif

namespace {
const QUrl kVersionManifest(QStringLiteral("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"));
const QUrl kJavaRuntimes(QStringLiteral(
    "https://launchermeta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json"));
const QString kAssetsHost = QStringLiteral("https://resources.download.minecraft.net/");
const QString kLibrariesHost = QStringLiteral("https://libraries.minecraft.net/");
const QString kRuntimePlatform = QStringLiteral("windows-x64");
const QString kFabricMeta = QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/");
const QString kNeoForgeVersions = QStringLiteral("https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge");
const QString kForgeMetadata = QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml");
const QString kForgePromotions = QStringLiteral("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json");

bool readJson(const QString &path, QJsonObject &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    out = document.object();
    return true;
}

// Mojang rule lists: the last matching rule decides; no rules means allowed.
bool rulesAllow(const QJsonArray &rules, const QVariantMap &features)
{
    if (rules.isEmpty())
        return true;
    bool allowed = false;
    for (const auto &value : rules) {
        const auto rule = value.toObject();
        bool matches = true;
        if (rule.contains("os")) {
            const auto os = rule.value("os").toObject();
            if (os.contains("name") && os.value("name").toString() != "windows")
                matches = false;
            if (os.contains("arch") && os.value("arch").toString() != "x64"
                && os.value("arch").toString() != "x86_64")
                matches = false;
            if (os.contains("version")) {
                const QRegularExpression pattern(os.value("version").toString());
                if (!pattern.match(QSysInfo::kernelVersion()).hasMatch())
                    matches = false;
            }
        }
        if (rule.contains("features")) {
            const auto wanted = rule.value("features").toObject();
            for (auto it = wanted.begin(); it != wanted.end(); ++it) {
                if (features.value(it.key(), false).toBool() != it.value().toBool())
                    matches = false;
            }
        }
        if (matches)
            allowed = rule.value("action").toString() == "allow";
    }
    return allowed;
}

// "group:artifact:version[:classifier][@extension]" → relative Maven path.
QString mavenPath(const QString &coordinate)
{
    auto name = coordinate;
    QString extension = QStringLiteral("jar");
    const auto at = name.indexOf('@');
    if (at >= 0) {
        extension = name.mid(at + 1);
        name = name.left(at);
    }
    const auto parts = name.split(':');
    if (parts.size() < 3)
        return {};
    auto file = parts[1] + '-' + parts[2];
    if (parts.size() > 3)
        file += '-' + parts[3];
    return QString(parts[0]).replace('.', '/') + '/' + parts[1] + '/' + parts[2] + '/' + file + '.' + extension;
}

// NeoForge builds are numbered after the Minecraft version: 1.21.1 → 21.1.x, 1.20.4 → 20.4.x.
QString neoforgePrefix(const QString &gameVersion)
{
    const auto parts = gameVersion.split('.');
    if (parts.size() < 2 || parts[0] != "1")
        return {};
    return parts[1] + '.' + parts.value(2, QStringLiteral("0")) + '.';
}

bool copyTree(const QString &from, const QString &to)
{
    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    bool ok = true;
    while (it.hasNext()) {
        const auto file = it.next();
        const auto target = QDir(to).filePath(QDir(from).relativeFilePath(file));
        if (QFileInfo::exists(target))
            continue;
        QDir().mkpath(QFileInfo(target).absolutePath());
        ok = QFile::copy(file, target) && ok;
    }
    return ok;
}

// The UUID the game itself derives for offline players ("OfflinePlayer:<name>", MD5, v3).
QString offlineUuid(const QString &name)
{
    auto hash = QCryptographicHash::hash(("OfflinePlayer:" + name).toUtf8(), QCryptographicHash::Md5);
    hash[6] = char((hash[6] & 0x0f) | 0x30);
    hash[8] = char((hash[8] & 0x3f) | 0x80);
    return QString::fromLatin1(hash.toHex());
}

QString substitute(QString text, const QHash<QString, QString> &values)
{
    static const QRegularExpression token(QStringLiteral("\\$\\{([A-Za-z0-9_]+)\\}"));
    QString result;
    qsizetype last = 0;
    auto it = token.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        result += text.mid(last, match.capturedStart() - last);
        const auto key = match.captured(1);
        result += values.contains(key) ? values.value(key) : match.captured(0);
        last = match.capturedEnd();
    }
    result += text.mid(last);
    return result;
}

QStringList expandArguments(const QJsonArray &arguments, const QVariantMap &features,
                            const QHash<QString, QString> &values)
{
    QStringList result;
    for (const auto &entry : arguments) {
        if (entry.isString()) {
            result << substitute(entry.toString(), values);
            continue;
        }
        const auto object = entry.toObject();
        if (!rulesAllow(object.value("rules").toArray(), features))
            continue;
        const auto value = object.value("value");
        if (value.isString())
            result << substitute(value.toString(), values);
        else
            for (const auto &part : value.toArray())
                result << substitute(part.toString(), values);
    }
    return result;
}

// Fabric meta loader list: all builds newest first, plus the newest stable one.
bool parseFabricLoaders(const QByteArray &data, QStringList &versions, QString &recommended)
{
    const auto entries = QJsonDocument::fromJson(data).array();
    versions.clear();
    recommended.clear();
    for (const auto &entry : entries) {
        const auto loader = entry.toObject().value("loader").toObject();
        const auto version = loader.value("version").toString();
        if (version.isEmpty())
            continue;
        versions << version;
        if (recommended.isEmpty() && loader.value("stable").toBool())
            recommended = version;
    }
    if (recommended.isEmpty() && !versions.isEmpty())
        recommended = versions.first();
    return !versions.isEmpty();
}

QString formatBytes(qint64 bytes)
{
    return QLocale(QLocale::Portuguese, QLocale::Brazil).formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

#ifdef Q_OS_WIN
BOOL CALLBACK closeWindowsOfProcess(HWND window, LPARAM pid)
{
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner == DWORD(pid) && IsWindowVisible(window))
        PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
}
#endif
} // namespace

GameRunner::GameRunner(const QString &dataDirectory, QObject *parent)
    : QObject(parent)
    , m_root(QDir(dataDirectory).absolutePath())
    , m_network(new QNetworkAccessManager(this))
    , m_downloader(new Downloader(m_network, this))
{
    connect(m_downloader, &Downloader::finished, this, &GameRunner::onBatchFinished);
    connect(m_downloader, &Downloader::progress, this, &GameRunner::onBatchProgress);
    QFile cached(path("meta/version_manifest_v2.json"));
    if (cached.open(QIODevice::ReadOnly))
        loadManifest(cached.readAll());
    refreshVersions();
}

GameRunner::~GameRunner()
{
    // The game is a detached process; closing the launcher leaves it running.
#ifdef Q_OS_WIN
    delete m_processNotifier;
    if (m_processHandle)
        CloseHandle(HANDLE(m_processHandle));
#endif
}

QString GameRunner::path(const QString &relative) const
{
    return QDir(m_root).filePath(relative);
}

QString GameRunner::instanceDirectory(const QString &gameId) const
{
    return QDir::toNativeSeparators(path("instances/" + gameId));
}

// ── Version catalog ─────────────────────────────────────────────────────────

void GameRunner::refreshVersions()
{
    QNetworkRequest request(kVersionManifest);
    request.setTransferTimeout(20000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniLauncher/0.1"));
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return; // Offline: the cached catalog (if any) stays in use.
        const auto data = reply->readAll();
        if (!loadManifest(data))
            return;
        QDir().mkpath(path("meta"));
        QSaveFile file(path("meta/version_manifest_v2.json"));
        if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
            file.commit();
    });
}

void GameRunner::fetchLoaderVersions(const QString &gameVersion, const QString &loader)
{
    if (gameVersion.isEmpty() || loader == "Vanilla")
        return;
    if (loader != "Fabric") {
        loadForgeList(loader, gameVersion, [this, gameVersion, loader](const QStringList &versions, const QString &recommended) {
            m_loaderVersions = versions;
            m_recommendedLoader = recommended;
            m_loaderVersionsFor = gameVersion;
            m_loaderVersionsLoader = loader;
            emit loaderVersionsChanged();
        });
        return;
    }
    const auto cachePath = path("meta/fabric/" + gameVersion + ".json");
    const auto apply = [this, gameVersion](const QByteArray &data) {
        QStringList versions;
        QString recommended;
        if (!parseFabricLoaders(data, versions, recommended))
            versions.clear();
        m_loaderVersions = versions;
        m_recommendedLoader = recommended;
        m_loaderVersionsFor = gameVersion;
        m_loaderVersionsLoader = QStringLiteral("Fabric");
        emit loaderVersionsChanged();
    };
    QFile cached(cachePath);
    if (cached.open(QIODevice::ReadOnly))
        apply(cached.readAll());
    QNetworkRequest request(QUrl(kFabricMeta + gameVersion));
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniLauncher/0.1"));
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cachePath, apply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const auto data = reply->readAll();
        QStringList versions;
        QString recommended;
        if (!parseFabricLoaders(data, versions, recommended))
            return;
        QDir().mkpath(QFileInfo(cachePath).absolutePath());
        QSaveFile file(cachePath);
        if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
            file.commit();
        apply(data);
    });
}

// Forge/NeoForge build list for a Minecraft version, newest first, with the recommended
// build. Cached as {"versions": [...], "recommended": "..."}; the cache answers offline.
void GameRunner::loadForgeList(const QString &loader, const QString &gameVersion,
                               std::function<void(const QStringList &, const QString &)> done)
{
    const auto cachePath = path("meta/" + loader.toLower() + '/' + gameVersion + ".json");
    const auto finish = [this, cachePath, done](const QStringList &versions, const QString &recommended) {
        if (!versions.isEmpty()) {
            QDir().mkpath(QFileInfo(cachePath).absolutePath());
            QSaveFile file(cachePath);
            const auto data = QJsonDocument(QJsonObject{{"versions", QJsonArray::fromStringList(versions)},
                                                        {"recommended", recommended}}).toJson(QJsonDocument::Compact);
            if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
                file.commit();
            done(versions, recommended);
            return;
        }
        QJsonObject cached;
        readJson(cachePath, cached);
        QStringList fallback;
        for (const auto &value : cached.value("versions").toArray())
            fallback << value.toString();
        done(fallback, cached.value("recommended").toString());
    };
    const auto request = [](const QString &url) {
        QNetworkRequest r{QUrl(url)};
        r.setTransferTimeout(15000);
        r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniLauncher/0.1"));
        return r;
    };
    if (loader == "NeoForge") {
        auto *reply = m_network->get(request(kNeoForgeVersions));
        connect(reply, &QNetworkReply::finished, this, [reply, gameVersion, finish] {
            reply->deleteLater();
            const auto prefix = neoforgePrefix(gameVersion);
            QStringList versions;
            QString recommended;
            if (reply->error() == QNetworkReply::NoError && !prefix.isEmpty()) {
                for (const auto &value : QJsonDocument::fromJson(reply->readAll()).object().value("versions").toArray())
                    if (value.toString().startsWith(prefix))
                        versions.prepend(value.toString());
                for (const auto &version : versions)
                    if (!version.contains("beta") && recommended.isEmpty())
                        recommended = version;
                if (recommended.isEmpty() && !versions.isEmpty())
                    recommended = versions.first();
            }
            finish(versions, recommended);
        });
        return;
    }
    auto *reply = m_network->get(request(kForgeMetadata));
    connect(reply, &QNetworkReply::finished, this, [this, reply, gameVersion, finish, request] {
        reply->deleteLater();
        QStringList versions;
        if (reply->error() == QNetworkReply::NoError) {
            static const QRegularExpression entry(QStringLiteral("<version>([^<]+)</version>"));
            const auto prefix = gameVersion + '-';
            auto it = entry.globalMatch(QString::fromUtf8(reply->readAll()));
            while (it.hasNext()) {
                const auto full = it.next().captured(1);
                if (full.startsWith(prefix))
                    versions << full.mid(prefix.size()); // maven-metadata lists newest first
            }
        }
        if (versions.isEmpty()) {
            finish({}, {});
            return;
        }
        auto *promos = m_network->get(request(kForgePromotions));
        connect(promos, &QNetworkReply::finished, this, [promos, gameVersion, versions, finish] {
            promos->deleteLater();
            const auto table = QJsonDocument::fromJson(promos->readAll()).object().value("promos").toObject();
            auto recommended = table.value(gameVersion + "-recommended").toString(table.value(gameVersion + "-latest").toString());
            if (!versions.contains(recommended))
                recommended = versions.first();
            finish(versions, recommended);
        });
    });
}

bool GameRunner::loadManifest(const QByteArray &data)
{
    const auto root = QJsonDocument::fromJson(data).object();
    const auto entries = root.value("versions").toArray();
    if (entries.isEmpty())
        return false;
    QStringList releases;
    QHash<QString, QPair<QUrl, QString>> index;
    for (const auto &value : entries) {
        const auto entry = value.toObject();
        const auto id = entry.value("id").toString();
        index.insert(id, {QUrl(entry.value("url").toString()), entry.value("sha1").toString()});
        if (entry.value("type").toString() == "release")
            releases << id;
    }
    m_versionIndex = index;
    m_releases = releases;
    m_latestRelease = root.value("latest").toObject().value("release").toString();
    emit versionsChanged();
    return true;
}

// ── Session plumbing ────────────────────────────────────────────────────────

void GameRunner::setState(const QString &state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void GameRunner::setStage(const QString &stage, double progress, const QString &detail)
{
    m_stage = stage;
    m_progress = progress;
    m_detail = detail;
    emit progressChanged();
}

void GameRunner::fail(const QString &message)
{
    m_next = nullptr;
    m_error = message;
    setState(QStringLiteral("failed"));
}

void GameRunner::runBatch(const QList<Downloader::Job> &jobs, const QString &stage, bool determinate,
                          std::function<void()> next)
{
    m_next = std::move(next);
    m_determinate = determinate;
    if (determinate)
        setState(QStringLiteral("downloading"));
    setStage(stage, determinate ? 0 : -1);
    m_progressClock.start();
    m_downloader->start(jobs);
}

void GameRunner::onBatchFinished(bool ok, const QString &error)
{
    if (!busy())
        return; // Cancelled or already failed.
    auto next = std::exchange(m_next, nullptr);
    if (!ok) {
        fail(error.isEmpty() ? QStringLiteral("O download foi interrompido.")
                             : error + QStringLiteral("\nVerifique sua conexão e tente de novo."));
        return;
    }
    if (next)
        next();
}

void GameRunner::onBatchProgress()
{
    if (!m_determinate || m_progressClock.elapsed() < 80)
        return;
    m_progressClock.restart();
    const auto total = m_downloader->totalBytes();
    const auto received = m_downloader->receivedBytes();
    m_progress = total > 0 ? qBound(0.0, double(received) / double(total), 1.0) : 0;
    m_detail = QStringLiteral("%1 de %2 arquivos · %3 de %4")
                   .arg(QLocale().toString(m_downloader->completedFiles()),
                        QLocale().toString(m_downloader->totalFiles()),
                        formatBytes(received), formatBytes(total));
    emit progressChanged();
}

bool GameRunner::launch(const QVariantMap &game, const QString &playerName, const QVariantMap &settings)
{
    if (busy() || running())
        return false;
    m_gameId = game.value("id").toString();
    m_versionId = game.value("version").toString();
    m_playerName = playerName;
    m_settings = settings;
    m_error.clear();
    m_libraries.clear();
    m_version = {};
    setState(QStringLiteral("preparing"));
    setStage(QStringLiteral("Preparando"));

    m_loader = game.value("loader").toString();
    m_loaderVersion = game.value("loaderVersion").toString();
    if (m_loader != "Vanilla" && m_loader != "Fabric" && m_loader != "Forge" && m_loader != "NeoForge") {
        fail(QStringLiteral("%1 ainda não é suportado.").arg(m_loader));
        return true;
    }
    if (playerName.isEmpty()) {
        fail(QStringLiteral("Escolha um Perfil para iniciar."));
        return true;
    }
    stepVersionJson();
    return true;
}

void GameRunner::cancel()
{
    if (!busy())
        return;
    m_next = nullptr;
    setState(QStringLiteral("idle"));
    if (m_processor)
        m_processor->kill();
    m_downloader->cancel();
}

void GameRunner::dismiss()
{
    if (m_state == "failed") {
        m_error.clear();
        setState(QStringLiteral("idle"));
    }
}

// ── Steps ───────────────────────────────────────────────────────────────────

void GameRunner::stepVersionJson()
{
    const auto jsonPath = path(QStringLiteral("versions/%1/%1.json").arg(m_versionId));
    if (!m_versionIndex.contains(m_versionId)) {
        // Offline with an installed version still works; otherwise there is nothing to fetch.
        if (QFileInfo::exists(jsonPath)) {
            stepParseVersion();
            return;
        }
        fail(m_versionIndex.isEmpty()
                 ? QStringLiteral("Não foi possível obter a lista de versões da Mojang. Verifique sua conexão.")
                 : QStringLiteral("A versão %1 não existe no catálogo da Mojang.").arg(m_versionId));
        return;
    }
    const auto entry = m_versionIndex.value(m_versionId);
    runBatch({{entry.first, jsonPath, entry.second, -1}}, QStringLiteral("Buscando a versão %1").arg(m_versionId),
             false, [this] { stepParseVersion(); });
}

void GameRunner::stepParseVersion()
{
    if (!readJson(path(QStringLiteral("versions/%1/%1.json").arg(m_versionId)), m_version)) {
        fail(QStringLiteral("As informações da versão %1 estão corrompidas.").arg(m_versionId));
        return;
    }
    if (m_version.contains("inheritsFrom")) {
        fail(QStringLiteral("Versões derivadas ainda não são suportadas."));
        return;
    }
    if (m_loader == "Forge" || m_loader == "NeoForge") {
        if (!m_loaderVersion.isEmpty()) {
            stepForgeInstaller();
            return;
        }
        setStage(QStringLiteral("Buscando o %1").arg(m_loader));
        loadForgeList(m_loader, m_versionId, [this](const QStringList &, const QString &recommended) {
            if (!busy())
                return;
            if (recommended.isEmpty()) {
                fail(QStringLiteral("O %1 não tem versão para o Minecraft %2.").arg(m_loader, m_versionId));
                return;
            }
            m_loaderVersion = recommended;
            stepForgeInstaller();
        });
        return;
    }
    if (m_loader == "Fabric") {
        if (!m_loaderVersion.isEmpty()) {
            stepFabricProfile(); // Pinned build: the loader list is not needed.
            return;
        }
        // Unpinned (older games): newest stable from a list refreshed at most daily.
        const QFileInfo list(path("meta/fabric/" + m_versionId + ".json"));
        if (list.exists() && list.lastModified().secsTo(QDateTime::currentDateTime()) > 86400)
            QFile::remove(list.filePath());
        runBatch({{QUrl(kFabricMeta + m_versionId), path("meta/fabric/" + m_versionId + ".json"), {}, -1}},
                 QStringLiteral("Buscando o Fabric"), false, [this] { stepFabricProfile(); });
        return;
    }
    stepAssetsAndJava();
}

void GameRunner::stepFabricProfile()
{
    QString loaderVersion = m_loaderVersion;
    QFile listFile(path("meta/fabric/" + m_versionId + ".json"));
    if (loaderVersion.isEmpty() && listFile.open(QIODevice::ReadOnly)) {
        QStringList versions;
        parseFabricLoaders(listFile.readAll(), versions, loaderVersion);
        listFile.close();
    }
    if (loaderVersion.isEmpty()) {
        listFile.remove();
        fail(QStringLiteral("O Fabric não tem suporte para o Minecraft %1.").arg(m_versionId));
        return;
    }
    const auto profileId = QStringLiteral("fabric-loader-%1-%2").arg(loaderVersion, m_versionId);
    const auto profilePath = path(QStringLiteral("versions/%1/%1.json").arg(profileId));
    runBatch({{QUrl(kFabricMeta + m_versionId + '/' + loaderVersion + "/profile/json"), profilePath, {}, -1}},
             QStringLiteral("Buscando o Fabric %1").arg(loaderVersion), false, [this, profilePath] {
        QJsonObject profile;
        if (!readJson(profilePath, profile)) {
            QFile::remove(profilePath);
            fail(QStringLiteral("O perfil do Fabric veio inválido. Tente de novo."));
            return;
        }
        mergeLoaderProfile(profile);
        stepAssetsAndJava();
    });
}

// Loader profile over vanilla: its libraries first, its main class, its arguments appended.
void GameRunner::mergeLoaderProfile(const QJsonObject &profile)
{
    auto libraries = profile.value("libraries").toArray();
    for (const auto &library : m_version.value("libraries").toArray())
        libraries.append(library);
    m_version.insert("libraries", libraries);
    m_version.insert("mainClass", profile.value("mainClass").toString());
    auto arguments = m_version.value("arguments").toObject();
    const auto extra = profile.value("arguments").toObject();
    for (const auto &key : {QStringLiteral("game"), QStringLiteral("jvm")}) {
        auto list = arguments.value(key).toArray();
        for (const auto &value : extra.value(key).toArray())
            list.append(value);
        if (!list.isEmpty() || arguments.contains(key))
            arguments.insert(key, list);
    }
    if (!arguments.isEmpty())
        m_version.insert("arguments", arguments);
}

// ── Forge / NeoForge ───────────────────────────────────────────────────────
// The official installer jar carries a version profile, an install profile with
// "processors" (Java tools that build the patched client), and bundled libraries.

void GameRunner::stepForgeInstaller()
{
    const bool neo = m_loader == "NeoForge";
    const auto full = neo ? m_loaderVersion : m_versionId + '-' + m_loaderVersion;
    m_forgeInstallId = (neo ? QStringLiteral("neoforge-") : QStringLiteral("forge-")) + full;
    const auto url = neo
        ? QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/neoforge-%1-installer.jar").arg(full)
        : QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/%1/forge-%1-installer.jar").arg(full);
    m_forgeInstaller = path("cache/installers/" + m_forgeInstallId + "-installer.jar");
    m_forgeDir = path("cache/installers/" + m_forgeInstallId);
    runBatch({{QUrl(url), m_forgeInstaller, {}, -1}}, QStringLiteral("Baixando o %1 %2").arg(m_loader, m_loaderVersion),
             false, [this] { stepForgeExtract(); });
}

void GameRunner::stepForgeExtract()
{
    QDir(m_forgeDir).removeRecursively();
    QDir().mkpath(m_forgeDir);
    QProcess tar;
    tar.start(QStandardPaths::findExecutable(QStringLiteral("tar")), {QStringLiteral("-xf"), m_forgeInstaller, QStringLiteral("-C"), m_forgeDir});
    QJsonObject loaderVersion;
    const bool extracted = tar.waitForFinished(60000) && tar.exitCode() == 0
        && readJson(QDir(m_forgeDir).filePath("install_profile.json"), m_forgeProfile);
    const auto versionJson = m_forgeProfile.value("json").toString(QStringLiteral("/version.json"));
    if (!extracted || !readJson(QDir(m_forgeDir).filePath(versionJson.mid(versionJson.startsWith('/') ? 1 : 0)), loaderVersion)
        || !m_forgeProfile.contains("processors")) {
        QFile::remove(m_forgeInstaller);
        fail(QStringLiteral("O instalador do %1 %2 não pôde ser lido (versões antigas do Forge não são suportadas).")
                 .arg(m_loader, m_loaderVersion));
        return;
    }
    // Libraries bundled inside the installer go straight into the shared library folder.
    copyTree(QDir(m_forgeDir).filePath("maven"), path("libraries"));
    mergeLoaderProfile(loaderVersion);
    m_forgeNeedsProcessors = !QFileInfo::exists(path("versions/" + m_forgeInstallId + "/.omni-installed"));
    stepAssetsAndJava();
}

QString GameRunner::forgeToken(const QString &argument) const
{
    const auto library = [this](const QString &coordinate) {
        return QDir::toNativeSeparators(path("libraries/" + mavenPath(coordinate)));
    };
    if (argument.startsWith('[') && argument.endsWith(']'))
        return library(argument.mid(1, argument.size() - 2));
    static const QRegularExpression token(QStringLiteral("\\{([A-Z_]+)\\}"));
    const QHash<QString, QString> builtins{
        {"SIDE", QStringLiteral("client")},
        {"MINECRAFT_JAR", QDir::toNativeSeparators(path(QStringLiteral("versions/%1/%1.jar").arg(m_versionId)))},
        {"MINECRAFT_VERSION", m_versionId},
        {"ROOT", QDir::toNativeSeparators(m_root)},
        {"INSTALLER", QDir::toNativeSeparators(m_forgeInstaller)},
        {"LIBRARY_DIR", QDir::toNativeSeparators(path("libraries"))},
    };
    const auto data = m_forgeProfile.value("data").toObject();
    QString result;
    qsizetype last = 0;
    auto it = token.globalMatch(argument);
    while (it.hasNext()) {
        const auto match = it.next();
        result += argument.mid(last, match.capturedStart() - last);
        const auto key = match.captured(1);
        QString value = builtins.value(key);
        if (!builtins.contains(key)) {
            const auto raw = data.value(key).toObject().value("client").toString();
            if (raw.startsWith('[') && raw.endsWith(']'))
                value = library(raw.mid(1, raw.size() - 2));
            else if (raw.startsWith('\'') && raw.endsWith('\''))
                value = raw.mid(1, raw.size() - 2);
            else if (raw.startsWith('/'))
                value = QDir::toNativeSeparators(QDir(m_forgeDir).filePath(raw.mid(1)));
            else
                value = raw;
        }
        result += value;
        last = match.capturedEnd();
    }
    result += argument.mid(last);
    return result;
}

void GameRunner::runNextProcessor()
{
    const auto processors = m_forgeProfile.value("processors").toArray();
    while (m_processorIndex < processors.size()) {
        const auto sides = processors.at(m_processorIndex).toObject().value("sides").toArray();
        if (sides.isEmpty() || sides.contains(QStringLiteral("client")))
            break;
        ++m_processorIndex;
    }
    if (m_processorIndex >= processors.size()) {
        QDir().mkpath(path("versions/" + m_forgeInstallId));
        QFile marker(path("versions/" + m_forgeInstallId + "/.omni-installed"));
        if (marker.open(QIODevice::WriteOnly))
            marker.write("ok");
        m_forgeNeedsProcessors = false;
        stepPrepareAndStart();
        return;
    }
    setStage(QStringLiteral("Instalando o %1 (%2 de %3)").arg(m_loader).arg(m_processorIndex + 1).arg(processors.size()));

    const auto processor = processors.at(m_processorIndex).toObject();
    const auto jar = path("libraries/" + mavenPath(processor.value("jar").toString()));
    QStringList classpath{QDir::toNativeSeparators(jar)};
    for (const auto &entry : processor.value("classpath").toArray())
        classpath << QDir::toNativeSeparators(path("libraries/" + mavenPath(entry.toString())));

    // Main class from the jar manifest (continuation lines start with a space).
    QProcess manifest;
    manifest.start(QStandardPaths::findExecutable(QStringLiteral("tar")),
                   {QStringLiteral("-xOf"), jar, QStringLiteral("META-INF/MANIFEST.MF")});
    manifest.waitForFinished(15000);
    const auto text = QString::fromUtf8(manifest.readAllStandardOutput()).replace("\r\n ", "").replace("\n ", "");
    static const QRegularExpression mainClass(QStringLiteral("^Main-Class:\\s*(\\S+)"), QRegularExpression::MultilineOption);
    const auto main = mainClass.match(text).captured(1);
    if (main.isEmpty()) {
        fail(QStringLiteral("O instalador do %1 está incompleto (%2).").arg(m_loader, QFileInfo(jar).fileName()));
        return;
    }
    QStringList arguments{QStringLiteral("-cp"), classpath.join(';'), main};
    for (const auto &value : processor.value("args").toArray())
        arguments << forgeToken(value.toString());

    auto *process = new QProcess(this);
    m_processor = process;
    process->setWorkingDirectory(m_forgeDir);
    process->setProcessChannelMode(QProcess::MergedChannels);
    const auto log = path("cache/installers/" + m_forgeInstallId + ".log");
    process->setStandardOutputFile(log, m_processorIndex == 0 ? QIODevice::Truncate : QIODevice::Append);
    connect(process, &QProcess::finished, this, [this, process, log](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (!busy())
            return;
        if (status != QProcess::NormalExit || code != 0) {
            m_errorLog = log;
            fail(QStringLiteral("A instalação do %1 falhou na etapa %2. Use \"Ver log\" para detalhes.")
                     .arg(m_loader).arg(m_processorIndex + 1));
            return;
        }
        ++m_processorIndex;
        runNextProcessor();
    });
    process->start(path("runtimes/" + m_javaComponent + "/bin/java.exe"), arguments);
}

void GameRunner::stepAssetsAndJava()
{
    m_javaComponent = m_version.value("javaVersion").toObject().value("component").toString(QStringLiteral("jre-legacy"));

    QList<Downloader::Job> jobs;
    const auto assetIndex = m_version.value("assetIndex").toObject();
    jobs.append({QUrl(assetIndex.value("url").toString()),
                 path("assets/indexes/" + assetIndex.value("id").toString() + ".json"),
                 assetIndex.value("sha1").toString(), assetIndex.value("size").toInteger(-1)});
    // The runtime catalog has no checksum; it is refreshed only when missing.
    jobs.append({kJavaRuntimes, path("runtimes/all.json"), {}, -1});
    runBatch(jobs, QStringLiteral("Preparando arquivos"), false, [this] { stepRuntimeManifest(); });
}

void GameRunner::stepRuntimeManifest()
{
    QJsonObject all;
    if (!readJson(path("runtimes/all.json"), all)) {
        QFile::remove(path("runtimes/all.json"));
        fail(QStringLiteral("O catálogo de Java da Mojang veio inválido. Tente de novo."));
        return;
    }
    const auto candidates = all.value(kRuntimePlatform).toObject().value(m_javaComponent).toArray();
    if (candidates.isEmpty()) {
        fail(QStringLiteral("A Mojang não oferece o Java \"%1\" para Windows.").arg(m_javaComponent));
        return;
    }
    const auto manifest = candidates.first().toObject().value("manifest").toObject();
    runBatch({{QUrl(manifest.value("url").toString()), path("runtimes/" + m_javaComponent + ".manifest.json"),
               manifest.value("sha1").toString(), manifest.value("size").toInteger(-1)}},
             QStringLiteral("Preparando o Java"), false, [this] { stepCollectFiles(); });
}

void GameRunner::stepCollectFiles()
{
    QList<Downloader::Job> jobs;
    const QVariantMap features;

    // Client
    const auto client = m_version.value("downloads").toObject().value("client").toObject();
    jobs.append({QUrl(client.value("url").toString()), path(QStringLiteral("versions/%1/%1.jar").arg(m_versionId)),
                 client.value("sha1").toString(), client.value("size").toInteger(-1)});

    // Libraries: classpath artifacts, plus legacy native archives to extract.
    for (const auto &value : m_version.value("libraries").toArray()) {
        const auto library = value.toObject();
        if (!rulesAllow(library.value("rules").toArray(), features))
            continue;
        const auto downloads = library.value("downloads").toObject();
        const auto name = library.value("name").toString();
        if (downloads.contains("artifact")) {
            const auto artifact = downloads.value("artifact").toObject();
            const auto relative = artifact.value("path").toString(mavenPath(name));
            // An empty URL means the file comes from the loader installer or its processors.
            if (!artifact.value("url").toString().isEmpty())
                jobs.append({QUrl(artifact.value("url").toString()), path("libraries/" + relative),
                             artifact.value("sha1").toString(), artifact.value("size").toInteger(-1)});
            m_libraries.append({path("libraries/" + relative), false, {}});
        } else if (!downloads.contains("classifiers") && !name.isEmpty()) {
            const auto relative = mavenPath(name);
            jobs.append({QUrl(library.value("url").toString(kLibrariesHost) + relative), path("libraries/" + relative),
                         library.value("sha1").toString(), library.value("size").toInteger(-1)});
            m_libraries.append({path("libraries/" + relative), false, {}});
        }
        const auto natives = library.value("natives").toObject();
        if (natives.contains("windows")) {
            const auto classifier = natives.value("windows").toString().replace("${arch}", "64");
            const auto archive = downloads.value("classifiers").toObject().value(classifier).toObject();
            if (!archive.isEmpty()) {
                const auto relative = archive.value("path").toString();
                jobs.append({QUrl(archive.value("url").toString()), path("libraries/" + relative),
                             archive.value("sha1").toString(), archive.value("size").toInteger(-1)});
                QStringList exclude;
                for (const auto &entry : library.value("extract").toObject().value("exclude").toArray())
                    exclude << entry.toString();
                m_libraries.append({path("libraries/" + relative), true, exclude});
            }
        }
    }

    // Logging configuration
    const auto logging = m_version.value("logging").toObject().value("client").toObject().value("file").toObject();
    if (!logging.isEmpty())
        jobs.append({QUrl(logging.value("url").toString()), path("assets/log_configs/" + logging.value("id").toString()),
                     logging.value("sha1").toString(), logging.value("size").toInteger(-1)});

    // Assets
    QJsonObject assetIndex;
    const auto indexId = m_version.value("assetIndex").toObject().value("id").toString();
    if (!readJson(path("assets/indexes/" + indexId + ".json"), assetIndex)) {
        fail(QStringLiteral("O índice de recursos do jogo está corrompido."));
        return;
    }
    const auto objects = assetIndex.value("objects").toObject();
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        const auto object = it.value().toObject();
        const auto hash = object.value("hash").toString();
        if (hash.size() < 2)
            continue;
        const auto relative = hash.left(2) + '/' + hash;
        jobs.append({QUrl(kAssetsHost + relative), path("assets/objects/" + relative), hash,
                     object.value("size").toInteger(-1)});
    }

    // Java runtime
    QJsonObject runtime;
    if (!readJson(path("runtimes/" + m_javaComponent + ".manifest.json"), runtime)) {
        fail(QStringLiteral("O manifesto do Java está corrompido."));
        return;
    }
    const auto runtimeRoot = "runtimes/" + m_javaComponent + '/';
    const auto files = runtime.value("files").toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const auto entry = it.value().toObject();
        const auto type = entry.value("type").toString();
        if (type == "directory") {
            QDir().mkpath(path(runtimeRoot + it.key()));
        } else if (type == "file") {
            const auto raw = entry.value("downloads").toObject().value("raw").toObject();
            jobs.append({QUrl(raw.value("url").toString()), path(runtimeRoot + it.key()),
                         raw.value("sha1").toString(), raw.value("size").toInteger(-1)});
        }
    }

    // Tools the Forge/NeoForge processors need (first installation only).
    if (m_forgeNeedsProcessors) {
        for (const auto &value : m_forgeProfile.value("libraries").toArray()) {
            const auto artifact = value.toObject().value("downloads").toObject().value("artifact").toObject();
            if (!artifact.value("url").toString().isEmpty())
                jobs.append({QUrl(artifact.value("url").toString()), path("libraries/" + artifact.value("path").toString()),
                             artifact.value("sha1").toString(), artifact.value("size").toInteger(-1)});
        }
    }

    runBatch(jobs, QStringLiteral("Baixando arquivos do jogo"), true, [this] { stepPrepareAndStart(); });
}

void GameRunner::stepPrepareAndStart()
{
    setState(QStringLiteral("starting"));
    setStage(QStringLiteral("Iniciando o Minecraft"));

    const auto java = path("runtimes/" + m_javaComponent + "/bin/javaw.exe");
    if (!QFileInfo::exists(java)) {
        fail(QStringLiteral("O Java baixado está incompleto (javaw.exe não encontrado)."));
        return;
    }
    if (m_forgeNeedsProcessors) {
        m_processorIndex = 0;
        runNextProcessor();
        return;
    }
    const auto gameDir = path("instances/" + m_gameId);
    const auto nativesDir = path("natives/" + m_versionId);
    QDir().mkpath(gameDir);
    QDir().mkpath(nativesDir);

    // Legacy (pre-1.19) native archives are unpacked; newer LWJGL extracts itself.
    QStringList classpath;
    for (const auto &library : m_libraries) {
        if (!library.nativeArchive) {
            if (!classpath.contains(QDir::toNativeSeparators(library.path)))
                classpath << QDir::toNativeSeparators(library.path);
            continue;
        }
        const auto tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
        if (tar.isEmpty()) {
            fail(QStringLiteral("Esta versão precisa extrair bibliotecas nativas, e o tar do Windows não foi encontrado."));
            return;
        }
        QStringList arguments{QStringLiteral("-xf"), library.path, QStringLiteral("-C"), nativesDir};
        for (const auto &exclude : library.extractExclude)
            arguments << QStringLiteral("--exclude=%1*").arg(exclude);
        QProcess extract;
        extract.start(tar, arguments);
        if (!extract.waitForFinished(30000) || extract.exitCode() != 0) {
            fail(QStringLiteral("Não foi possível extrair %1.").arg(QFileInfo(library.path).fileName()));
            return;
        }
    }
    classpath << QDir::toNativeSeparators(path(QStringLiteral("versions/%1/%1.jar").arg(m_versionId)));

    // Very old asset layouts read loose files instead of the hashed object store.
    const auto indexId = m_version.value("assetIndex").toObject().value("id").toString();
    QString gameAssets = path("assets");
    QJsonObject assetIndex;
    readJson(path("assets/indexes/" + indexId + ".json"), assetIndex);
    const bool virtualAssets = assetIndex.value("virtual").toBool();
    const bool resourceAssets = assetIndex.value("map_to_resources").toBool();
    if (virtualAssets || resourceAssets) {
        gameAssets = resourceAssets ? QDir(gameDir).filePath("resources") : path("assets/virtual/" + indexId);
        const auto objects = assetIndex.value("objects").toObject();
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            const auto hash = it.value().toObject().value("hash").toString();
            const auto target = QDir(gameAssets).filePath(it.key());
            if (QFileInfo::exists(target))
                continue;
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile::copy(path("assets/objects/" + hash.left(2) + '/' + hash), target);
        }
    }

    const auto resolution = m_settings.value("resolution").toString().split('x');
    const bool customResolution = resolution.size() == 2;
    const QVariantMap features{{"has_custom_resolution", customResolution}};
    const auto uuid = offlineUuid(m_playerName);
    const QHash<QString, QString> values{
        {"auth_player_name", m_playerName},
        {"version_name", m_versionId},
        // Paths reach the game in Windows form: log4j, for one, reads "C:/..." as a URL scheme.
        {"game_directory", QDir::toNativeSeparators(gameDir)},
        {"assets_root", QDir::toNativeSeparators(path("assets"))},
        {"game_assets", QDir::toNativeSeparators(gameAssets)},
        {"assets_index_name", indexId},
        {"auth_uuid", uuid},
        {"auth_access_token", QStringLiteral("0")},
        {"auth_session", QStringLiteral("0")},
        {"clientid", QString()},
        {"auth_xuid", QString()},
        {"user_type", QStringLiteral("legacy")},
        {"user_properties", QStringLiteral("{}")},
        {"version_type", m_version.value("type").toString(QStringLiteral("release"))},
        {"natives_directory", QDir::toNativeSeparators(nativesDir)},
        {"launcher_name", QStringLiteral("OmniLauncher")},
        {"launcher_version", QCoreApplication::applicationVersion()},
        {"classpath", classpath.join(';')},
        {"classpath_separator", QStringLiteral(";")},
        {"library_directory", QDir::toNativeSeparators(path("libraries"))},
        {"resolution_width", customResolution ? resolution[0] : QString()},
        {"resolution_height", customResolution ? resolution[1] : QString()},
    };

    const int memory = qMax(512, m_settings.value("memoryMb").toInt());
    QStringList arguments{QStringLiteral("-Xmx%1M").arg(memory), QStringLiteral("-Xms%1M").arg(qMin(memory, 1024))};
    arguments << QProcess::splitCommand(m_settings.value("jvmArgs").toString());

    const auto modern = m_version.value("arguments").toObject();
    if (modern.contains("jvm")) {
        arguments << expandArguments(modern.value("jvm").toArray(), features, values);
    } else {
        arguments << substitute(QStringLiteral("-Djava.library.path=${natives_directory}"), values)
                  << QStringLiteral("-XX:HeapDumpPath=MojangTricksIntelDriversForPerformance_javaw.exe_minecraft.exe.heapdump")
                  << QStringLiteral("-cp") << values.value("classpath");
    }
    const auto logging = m_version.value("logging").toObject().value("client").toObject();
    const auto logConfig = path("assets/log_configs/" + logging.value("file").toObject().value("id").toString());
    if (!logging.isEmpty() && QFileInfo::exists(logConfig))
        arguments << substitute(logging.value("argument").toString(), {{"path", QDir::toNativeSeparators(logConfig)}});

    arguments << m_version.value("mainClass").toString();
    if (modern.contains("game")) {
        arguments << expandArguments(modern.value("game").toArray(), features, values);
    } else {
        for (const auto &part : m_version.value("minecraftArguments").toString().split(' ', Qt::SkipEmptyParts))
            arguments << substitute(part, values);
        if (customResolution)
            arguments << "--width" << resolution[0] << "--height" << resolution[1];
    }

    if (!startProcess(java, arguments))
        return;
}

bool GameRunner::startProcess(const QString &java, const QStringList &arguments)
{
    const auto gameDir = path("instances/" + m_gameId);
    QDir().mkpath(QDir(gameDir).filePath("logs"));
    m_outputLog = QDir(gameDir).filePath("logs/omnilauncher-output.log");
    m_errorLog = QDir(gameDir).filePath("logs/omnilauncher-error.log");

    // Always detached, so the game survives the launcher closing. On Windows the
    // process handle is still watched to know when the session ends.
    QProcess process;
    process.setProgram(java);
    process.setArguments(arguments);
    process.setWorkingDirectory(gameDir);
    process.setStandardOutputFile(m_outputLog);
    process.setStandardErrorFile(m_errorLog);
    qint64 pid = 0;
    if (!process.startDetached(&pid)) {
        fail(QStringLiteral("Não foi possível iniciar o Java: %1").arg(process.errorString()));
        return false;
    }
    m_pid = pid;
    m_playClock.start();
    const bool closeLauncher = m_settings.value("afterLaunch").toString() == "close";

#ifdef Q_OS_WIN
    if (!closeLauncher) {
        const HANDLE handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                                          FALSE, DWORD(pid));
        if (handle) {
            m_processHandle = handle;
            m_processNotifier = new QWinEventNotifier(handle, this);
            connect(m_processNotifier, &QWinEventNotifier::activated, this, [this] {
                DWORD code = 0;
                GetExitCodeProcess(HANDLE(m_processHandle), &code);
                onProcessExited(int(code));
            });
            setState(QStringLiteral("running"));
            emit gameStarted(false);
            return true;
        }
    }
#endif
    setState(QStringLiteral("idle"));
    emit gameStarted(true);
    return true;
}

void GameRunner::onProcessExited(int exitCode)
{
    const auto seconds = m_playClock.elapsed() / 1000;
#ifdef Q_OS_WIN
    if (m_processNotifier) {
        m_processNotifier->setEnabled(false);
        m_processNotifier->deleteLater();
        m_processNotifier = nullptr;
    }
    if (m_processHandle) {
        CloseHandle(HANDLE(m_processHandle));
        m_processHandle = nullptr;
    }
#endif
    m_pid = 0;
    emit gameExited(m_gameId, seconds, exitCode);
    if (exitCode != 0) {
        const auto tail = logTail();
        fail(QStringLiteral("O Minecraft fechou com erro (código %1).").arg(exitCode)
             + (tail.isEmpty() ? QString() : "\n\n" + tail));
    } else {
        setState(QStringLiteral("idle"));
    }
}

void GameRunner::stopGame()
{
#ifdef Q_OS_WIN
    if (!running() || !m_pid)
        return;
    // Ask nicely first (the game saves and closes), then force after a grace period.
    EnumWindows(closeWindowsOfProcess, LPARAM(m_pid));
    const auto pid = m_pid;
    QTimer::singleShot(10000, this, [this, pid] {
        if (running() && m_pid == pid && m_processHandle)
            TerminateProcess(HANDLE(m_processHandle), 1);
    });
#endif
}

QString GameRunner::logTail() const
{
    QStringList lines;
    for (const auto &file : {m_errorLog, m_outputLog}) {
        QFile log(file);
        if (!log.open(QIODevice::ReadOnly))
            continue;
        const auto size = log.size();
        log.seek(qMax<qint64>(0, size - 4096));
        const auto text = QString::fromLocal8Bit(log.readAll());
        lines = text.split('\n', Qt::SkipEmptyParts);
        if (!lines.isEmpty())
            break;
    }
    if (lines.size() > 6)
        lines = lines.mid(lines.size() - 6);
    for (auto &line : lines)
        line = line.trimmed();
    return lines.join('\n');
}

// ── Folders ─────────────────────────────────────────────────────────────────

bool GameRunner::openGameFolder(const QString &gameId)
{
    if (gameId.isEmpty())
        return false;
    const auto directory = path("instances/" + gameId);
    QDir().mkpath(directory);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

bool GameRunner::openModsFolder(const QString &gameId)
{
    if (gameId.isEmpty())
        return false;
    const auto directory = path("instances/" + gameId + "/mods");
    QDir().mkpath(directory);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

bool GameRunner::openDataFolder()
{
    QDir().mkpath(m_root);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(m_root));
}

bool GameRunner::openLog()
{
    if (m_gameId.isEmpty())
        return false;
    // Prefer the game's own log; fall back to what the launcher captured.
    const auto gameLog = path("instances/" + m_gameId + "/logs/latest.log");
    for (const auto &candidate : {gameLog, m_errorLog, m_outputLog}) {
        if (!candidate.isEmpty() && QFileInfo(candidate).size() > 0)
            return QDesktopServices::openUrl(QUrl::fromLocalFile(candidate));
    }
    return false;
}
