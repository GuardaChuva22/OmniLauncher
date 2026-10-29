#include "ServerManager.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {
const QString kFabricMeta = QStringLiteral("https://meta.fabricmc.net/v2/versions/");
const QString kModrinth = QStringLiteral("https://api.modrinth.com/v2/");
const int kConsoleLimit = 2000;

QJsonObject readJsonFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// Copies a folder over another one, replacing files that exist.
int copyOver(const QString &from, const QString &to)
{
    int copied = 0;
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto file = it.next();
        const auto target = QDir(to).filePath(QDir(from).relativeFilePath(file));
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::remove(target);
        copied += QFile::copy(file, target) ? 1 : 0;
    }
    return copied;
}

QString neoforgePrefix(const QString &gameVersion)
{
    const auto parts = gameVersion.split('.');
    if (parts.size() < 2)
        return {};
    if (parts[0] == "1")
        return parts[1] + '.' + parts.value(2, QStringLiteral("0")) + '.';
    return parts[0] + '.' + parts[1] + '.' + parts.value(2, QStringLiteral("0")) + '.';
}

// The loader build the game really runs: pinned in the library, or the one the
// launcher installed when the game last started (newest folder wins).
QString installedLoaderVersion(const QString &root, const QString &loader, const QString &version)
{
    QDir versions(QDir(root).filePath("versions"));
    QString prefix, suffix;
    if (loader == "Fabric") {
        prefix = QStringLiteral("fabric-loader-");
        suffix = '-' + version;
    } else if (loader == "Forge") {
        prefix = QStringLiteral("forge-") + version + '-';
    } else if (loader == "NeoForge") {
        prefix = QStringLiteral("neoforge-") + neoforgePrefix(version);
    } else {
        return {};
    }
    const auto entries = versions.entryInfoList({prefix + '*' + suffix}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const auto &entry : entries) {
        auto name = entry.fileName();
        if (!suffix.isEmpty())
            name.chop(suffix.size());
        if (loader == "NeoForge")
            return name.mid(QStringLiteral("neoforge-").size());
        return name.mid(prefix.size());
    }
    return {};
}
} // namespace

ServerManager::ServerManager(const QString &dataDirectory, QObject *parent)
    : QObject(parent)
    , m_root(dataDirectory)
    , m_network(new QNetworkAccessManager(this))
{
    const auto document = QJsonDocument::fromJson([this] {
        QFile file(QDir(m_root).filePath("servers.json"));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }());
    m_servers = document.array().toVariantList();
    for (const auto &entry : std::as_const(m_servers))
        m_status.insert(entry.toMap().value("id").toString(), QVariantMap{{"state", "stopped"}});
#ifdef Q_OS_WIN
    // Servers die with the launcher, even when it crashes: no orphan Java holding the port.
    m_job = CreateJobObjectW(nullptr, nullptr);
    if (m_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
#endif
}

ServerManager::~ServerManager()
{
    // Quitting the launcher: give every server the chance to save its world.
    for (auto it = m_processes.begin(); it != m_processes.end(); ++it) {
        QProcess *process = it.value();
        if (!process || process->state() == QProcess::NotRunning)
            continue;
        process->disconnect(this);
        process->write("stop\n");
        if (!process->waitForFinished(30000))
            process->kill();
    }
#ifdef Q_OS_WIN
    if (m_job)
        CloseHandle(m_job);
#endif
}

QString ServerManager::serverDir(const QString &id) const
{
    return QDir(m_root).filePath("servers/" + id);
}

QVariantMap ServerManager::serverById(const QString &id) const
{
    for (const auto &entry : m_servers) {
        const auto server = entry.toMap();
        if (server.value("id").toString() == id)
            return server;
    }
    return {};
}

void ServerManager::updateServer(const QString &id, const QVariantMap &changes)
{
    for (auto &entry : m_servers) {
        auto server = entry.toMap();
        if (server.value("id").toString() != id)
            continue;
        for (auto it = changes.begin(); it != changes.end(); ++it)
            server.insert(it.key(), it.value());
        entry = server;
        save();
        emit serversChanged();
        return;
    }
}

void ServerManager::save()
{
    QFile file(QDir(m_root).filePath("servers.json"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(QJsonArray::fromVariantList(m_servers)).toJson());
}

void ServerManager::setStatus(const QString &id, const QString &state, const QString &detail)
{
    auto current = m_status.value(id).toMap();
    current.insert("state", state);
    current.insert("detail", detail);
    if (state == "running" && current.value("startedAt").toLongLong() == 0)
        current.insert("startedAt", QDateTime::currentMSecsSinceEpoch());
    if (state != "running" && state != "starting") {
        current.insert("tunnel", QString());
    }
    if (state != "running") {
        current.insert("startedAt", 0);
        if (state != "stopping")
            current.insert("players", QStringList());
    }
    m_status.insert(id, current);
    emit statusChanged();
}

void ServerManager::setPlayers(const QString &id, const QStringList &players)
{
    auto current = m_status.value(id).toMap();
    current.insert("players", players);
    m_status.insert(id, current);
    emit statusChanged();
}

void ServerManager::appendConsole(const QString &id, const QString &line)
{
    auto &lines = m_console[id];
    lines.append(line);
    if (lines.size() > kConsoleLimit)
        lines.remove(0, lines.size() - kConsoleLimit);
    emit consoleLine(id, line);
}

void ServerManager::clearConsole(const QString &id)
{
    m_console.remove(id);
}

void ServerManager::failInstall(const QString &id, const QString &text)
{
    appendConsole(id, QStringLiteral("[OmniLauncher] ") + text);
    setStatus(id, QStringLiteral("failed"), text);
    emit message(text);
}

QNetworkReply *ServerManager::get(const QString &url)
{
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniLauncher/0.1.0 (desktop launcher)"));
    return m_network->get(request);
}

void ServerManager::fetchTo(const QString &url, const QString &file, std::function<void(bool)> done)
{
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [reply, file, done] {
        reply->deleteLater();
        const auto data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || data.isEmpty()) {
            done(false);
            return;
        }
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile out(file);
        done(out.open(QIODevice::WriteOnly | QIODevice::Truncate) && out.write(data) == data.size());
    });
}

QString ServerManager::lanAddress() const
{
    // The adapter with a default route usually has a private IPv4; virtual ones are skipped.
    static const QStringList virtualNames{"virtual", "vmware", "vbox", "hyper-v", "vethernet", "loopback",
                                          "hamachi", "radmin", "zerotier", "tailscale", "wsl"};
    for (const auto &interface : QNetworkInterface::allInterfaces()) {
        const auto flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const auto name = interface.humanReadableName().toLower();
        if (std::any_of(virtualNames.begin(), virtualNames.end(), [&](const QString &v) { return name.contains(v); }))
            continue;
        for (const auto &entry : interface.addressEntries()) {
            const auto ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol && ip.isPrivateUse())
                return ip.toString();
        }
    }
    return QStringLiteral("127.0.0.1");
}

void ServerManager::refreshPublicAddress()
{
    auto *reply = get(QStringLiteral("https://api.ipify.org"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const auto text = QString::fromLatin1(reply->readAll()).trimmed();
        if (reply->error() == QNetworkReply::NoError && !QHostAddress(text).isNull() && text != m_publicAddress) {
            m_publicAddress = text;
            emit publicAddressChanged();
        }
    });
}

bool ServerManager::anyRunning() const
{
    for (const auto &process : m_processes)
        if (process && process->state() != QProcess::NotRunning)
            return true;
    return false;
}

void ServerManager::copyText(const QString &text) const
{
    QGuiApplication::clipboard()->setText(text);
}

// ── Create and install ─────────────────────────────────────────────────────

QString ServerManager::create(const QString &name, const QVariantMap &game, int memoryMb, bool eulaAccepted)
{
    m_lastError.clear();
    const auto fail = [this](const QString &error) {
        m_lastError = error;
        emit messageChanged();
        return QString();
    };
    const auto title = name.trimmed();
    const auto version = game.value("version").toString();
    const auto loader = game.value("loader", QStringLiteral("Vanilla")).toString();
    if (!eulaAccepted)
        return fail(QStringLiteral("É preciso aceitar o EULA do Minecraft para hospedar um servidor."));
    if (title.isEmpty() || title.size() > 48)
        return fail(QStringLiteral("Dê ao servidor um nome de até 48 letras."));
    if (version.isEmpty())
        return fail(QStringLiteral("Escolha um jogo."));
    if (!QFileInfo::exists(QDir(m_root).filePath(QStringLiteral("versions/%1/%1.json").arg(version))))
        return fail(QStringLiteral("Inicie o jogo uma vez antes: o servidor usa a mesma versão e o mesmo Java."));
    auto loaderVersion = loader == "Vanilla" ? QString() : game.value("loaderVersion").toString();
    if (loader != "Vanilla" && loaderVersion.isEmpty())
        loaderVersion = installedLoaderVersion(m_root, loader, version);
    if (loader != "Vanilla" && loaderVersion.isEmpty())
        return fail(QStringLiteral("Inicie o jogo uma vez antes: o servidor usa a mesma versão do %1.").arg(loader));

    // First free port from 25565, so two servers can run side by side.
    QList<int> used;
    for (const auto &entry : std::as_const(m_servers))
        used.append(entry.toMap().value("port").toInt());
    int port = 25565;
    while (used.contains(port))
        ++port;

    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QDir().mkpath(serverDir(id));
    QVariantMap server{{"id", id}, {"name", title}, {"gameId", game.value("id")}, {"version", version},
                       {"loader", loader}, {"loaderVersion", loaderVersion},
                       {"memoryMb", qBound(1024, memoryMb, 65536)}, {"port", port}, {"installed", false},
                       {"createdAt", QDateTime::currentDateTime().toString(Qt::ISODate)}};
    m_servers.append(server);
    save();
    m_status.insert(id, QVariantMap{{"state", "stopped"}});
    writeDefaults(id, title, port);
    emit serversChanged();
    emit statusChanged();
    install(id);
    return id;
}

void ServerManager::writeDefaults(const QString &id, const QString &motd, int port)
{
    const QDir dir(serverDir(id));
    QFile eula(dir.filePath("eula.txt"));
    if (eula.open(QIODevice::WriteOnly | QIODevice::Truncate))
        eula.write(QStringLiteral("# EULA do Minecraft (https://aka.ms/MinecraftEULA) aceito no OmniLauncher em %1\neula=true\n")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
    if (QFileInfo::exists(dir.filePath("server.properties")))
        return;
    QFile properties(dir.filePath("server.properties"));
    if (properties.open(QIODevice::WriteOnly)) {
        // Offline profiles can join: the launcher's players are not all Microsoft accounts.
        properties.write(QStringLiteral("motd=%1\nserver-port=%2\nonline-mode=false\nmax-players=10\n")
                             .arg(QString(motd).replace('\n', ' '), QString::number(port)).toUtf8());
    }
}

QString ServerManager::java(const QVariantMap &server) const
{
    const auto version = readJsonFile(QDir(m_root).filePath(QStringLiteral("versions/%1/%1.json").arg(server.value("version").toString())));
    const auto component = version.value("javaVersion").toObject().value("component").toString(QStringLiteral("jre-legacy"));
    return QDir(m_root).filePath("runtimes/" + component + "/bin/java.exe");
}

void ServerManager::install(const QString &id)
{
    const auto server = serverById(id);
    const auto state = m_status.value(id).toMap().value("state").toString();
    if (server.isEmpty() || state == "installing" || state == "starting" || state == "running" || state == "stopping")
        return;
    setStatus(id, QStringLiteral("installing"), QStringLiteral("Preparando"));
    appendConsole(id, QStringLiteral("[OmniLauncher] Preparando o servidor %1 %2").arg(server.value("loader").toString(),
                                                                                          server.value("version").toString()));
    if (!QFileInfo::exists(java(server))) {
        failInstall(id, QStringLiteral("O Java deste jogo não está instalado. Inicie o jogo uma vez e tente de novo."));
        return;
    }
    const auto versionJson = readJsonFile(QDir(m_root).filePath(
        QStringLiteral("versions/%1/%1.json").arg(server.value("version").toString()))).toVariantMap();
    const auto loader = server.value("loader").toString();
    if (loader == "Forge" || loader == "NeoForge")
        installForge(id);
    else if (loader == "Fabric")
        installFabric(id, versionJson);
    else
        installServerJar(id, versionJson);
}

void ServerManager::installServerJar(const QString &id, const QVariantMap &versionJson)
{
    const auto url = versionJson.value("downloads").toMap().value("server").toMap().value("url").toString();
    if (url.isEmpty()) {
        failInstall(id, QStringLiteral("A Mojang não publica servidor para esta versão."));
        return;
    }
    setStatus(id, QStringLiteral("installing"), QStringLiteral("Baixando o servidor do Minecraft"));
    fetchTo(url, QDir(serverDir(id)).filePath("server.jar"), [this, id](bool ok) {
        if (!ok) {
            failInstall(id, QStringLiteral("Não foi possível baixar o servidor do Minecraft."));
            return;
        }
        finishInstall(id, {QStringLiteral("-jar"), QStringLiteral("server.jar")});
    });
}

void ServerManager::installFabric(const QString &id, const QVariantMap &versionJson)
{
    // Fabric's server launcher runs the vanilla server.jar beside it.
    const auto url = versionJson.value("downloads").toMap().value("server").toMap().value("url").toString();
    if (url.isEmpty()) {
        failInstall(id, QStringLiteral("A Mojang não publica servidor para esta versão."));
        return;
    }
    setStatus(id, QStringLiteral("installing"), QStringLiteral("Baixando o servidor do Minecraft"));
    fetchTo(url, QDir(serverDir(id)).filePath("server.jar"), [this, id](bool ok) {
        if (!ok) {
            failInstall(id, QStringLiteral("Não foi possível baixar o servidor do Minecraft."));
            return;
        }
        setStatus(id, QStringLiteral("installing"), QStringLiteral("Baixando o Fabric"));
        auto *reply = get(kFabricMeta + "installer");
        connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
            reply->deleteLater();
            QString installer;
            for (const auto &entry : QJsonDocument::fromJson(reply->readAll()).array()) {
                if (entry.toObject().value("stable").toBool()) {
                    installer = entry.toObject().value("version").toString();
                    break;
                }
            }
            const auto server = serverById(id);
            if (installer.isEmpty() || server.isEmpty()) {
                failInstall(id, QStringLiteral("Não foi possível falar com o Fabric."));
                return;
            }
            const auto url = kFabricMeta + QStringLiteral("loader/%1/%2/%3/server/jar")
                                               .arg(server.value("version").toString(), server.value("loaderVersion").toString(), installer);
            fetchTo(url, QDir(serverDir(id)).filePath("fabric-server-launch.jar"), [this, id](bool ok) {
                if (!ok) {
                    failInstall(id, QStringLiteral("Não foi possível baixar o servidor do Fabric."));
                    return;
                }
                finishInstall(id, {QStringLiteral("-jar"), QStringLiteral("fabric-server-launch.jar")});
            });
        });
    });
}

void ServerManager::installForge(const QString &id)
{
    const auto server = serverById(id);
    const bool neo = server.value("loader").toString() == "NeoForge";
    const auto version = server.value("version").toString();
    const auto loaderVersion = server.value("loaderVersion").toString();
    const auto full = neo ? loaderVersion : version + '-' + loaderVersion;
    const auto installId = (neo ? QStringLiteral("neoforge-") : QStringLiteral("forge-")) + full;
    const auto url = neo
        ? QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/neoforge-%1-installer.jar").arg(full)
        : QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/%1/forge-%1-installer.jar").arg(full);
    // Same cache as the game's install: usually already on disk.
    const auto installer = QDir(m_root).filePath("cache/installers/" + installId + "-installer.jar");

    const auto runInstaller = [this, id, installer, server] {
        setStatus(id, QStringLiteral("installing"), QStringLiteral("Instalando o %1 (pode levar alguns minutos)").arg(server.value("loader").toString()));
        auto *process = new QProcess(this);
        process->setWorkingDirectory(serverDir(id));
        process->setProcessChannelMode(QProcess::MergedChannels);
        connect(process, &QProcess::readyRead, this, [this, process, id] {
            for (const auto &line : QString::fromUtf8(process->readAll()).split('\n', Qt::SkipEmptyParts))
                appendConsole(id, line.trimmed());
        });
        connect(process, &QProcess::finished, this, [this, process, id](int code) {
            process->deleteLater();
            const QDir dir(serverDir(id));
            QStringList launch;
            // Modern Forge and NeoForge: run.bat points at an argument file in libraries/.
            QFile script(dir.filePath("run.bat"));
            if (script.open(QIODevice::ReadOnly)) {
                static const QRegularExpression argsFile(QStringLiteral("@(libraries[/\\\\]\\S+win_args\\.txt)"));
                const auto match = argsFile.match(QString::fromUtf8(script.readAll()));
                if (match.hasMatch())
                    launch = {'@' + match.captured(1)};
            }
            // Older Forge (1.13–1.16): a forge-*.jar beside the installer output.
            if (launch.isEmpty()) {
                for (const auto &jar : dir.entryList({"forge-*.jar"}, QDir::Files))
                    if (!jar.contains("installer"))
                        launch = {QStringLiteral("-jar"), jar};
            }
            if (code != 0 || launch.isEmpty()) {
                failInstall(id, QStringLiteral("O instalador do servidor falhou. Veja o console para os detalhes."));
                return;
            }
            finishInstall(id, launch);
        });
        process->start(java(server), {QStringLiteral("-jar"), QDir::toNativeSeparators(installer), QStringLiteral("--installServer"),
                                      QDir::toNativeSeparators(serverDir(id))});
    };
    if (QFileInfo(installer).size() > 0) {
        runInstaller();
        return;
    }
    setStatus(id, QStringLiteral("installing"), QStringLiteral("Baixando o %1 %2").arg(server.value("loader").toString(), loaderVersion));
    fetchTo(url, installer, [this, id, runInstaller](bool ok) {
        if (!ok) {
            failInstall(id, QStringLiteral("Não foi possível baixar o instalador do servidor."));
            return;
        }
        runInstaller();
    });
}

void ServerManager::finishInstall(const QString &id, const QStringList &launch)
{
    setStatus(id, QStringLiteral("installing"), QStringLiteral("Copiando mods e configurações do jogo"));
    copyGameContent(id, [this, id, launch](int copied, int skipped) {
        updateServer(id, {{"installed", true}, {"launch", launch}});
        if (copied + skipped > 0)
            appendConsole(id, QStringLiteral("[OmniLauncher] %1 mods copiados do jogo, %2 só de cliente deixados de fora.")
                                  .arg(copied).arg(skipped));
        appendConsole(id, QStringLiteral("[OmniLauncher] Servidor pronto. Clique em Iniciar."));
        setStatus(id, QStringLiteral("stopped"), QStringLiteral("Pronto"));
        emit message(QStringLiteral("%1 está pronto.").arg(serverById(id).value("name").toString()));
    });
}

void ServerManager::syncContent(const QString &id)
{
    const auto state = m_status.value(id).toMap().value("state").toString();
    if (serverById(id).isEmpty() || state != "stopped")
        return;
    setStatus(id, QStringLiteral("installing"), QStringLiteral("Copiando mods e configurações do jogo"));
    copyGameContent(id, [this, id](int copied, int skipped) {
        setStatus(id, QStringLiteral("stopped"), QStringLiteral("Pronto"));
        emit message(QStringLiteral("%1 mods sincronizados com o jogo (%2 só de cliente ficaram de fora).").arg(copied).arg(skipped));
    });
}

// Mods marked client-only (on Modrinth, or in their own metadata) stay out: a dedicated
// server crashes on them. Files the launcher copied before are tracked so a sync can
// replace them without touching mods the player added to the server by hand.
void ServerManager::copyGameContent(const QString &id, std::function<void(int, int)> done)
{
    const auto server = serverById(id);
    const QDir instance(QDir(m_root).filePath("instances/" + server.value("gameId").toString()));
    const QDir target(serverDir(id));
    for (const auto &folder : {QStringLiteral("config"), QStringLiteral("defaultconfigs"), QStringLiteral("kubejs")})
        if (instance.exists(folder))
            copyOver(instance.filePath(folder), target.filePath(folder));

    const auto jars = server.value("loader").toString() == "Vanilla"
        ? QStringList() : QDir(instance.filePath("mods")).entryList({"*.jar"}, QDir::Files);
    if (jars.isEmpty()) {
        done(0, 0);
        return;
    }
    QHash<QString, QString> fileByHash;
    QJsonArray hashes;
    for (const auto &jar : jars) {
        QFile file(instance.filePath("mods/" + jar));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(&file);
        const auto hex = QString::fromLatin1(hash.result().toHex());
        fileByHash.insert(hex, jar);
        hashes.append(hex);
    }

    const auto apply = [this, id, instance, target, jars, done](const QSet<QString> &clientOnly, const QSet<QString> &known) {
        QDir().mkpath(target.filePath("mods"));
        const auto trackFile = target.filePath("mods/.omni-synced.json");
        QFile previous(trackFile);
        const auto synced0 = previous.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(previous.readAll()).array() : QJsonArray();
        previous.close();
        for (const auto &old : synced0)
            QFile::remove(target.filePath("mods/" + old.toString()));
        QJsonArray synced;
        int copied = 0, skipped = 0;
        for (const auto &jar : jars) {
            if (serverById(id).value("connectorOff").toBool() && jar.startsWith("e4mc", Qt::CaseInsensitive))
                continue;
            bool client = clientOnly.contains(jar);
            if (!client && !known.contains(jar)) {
                // Not on Modrinth: read the mod's own metadata.
                QProcess tar;
                tar.start(QStandardPaths::findExecutable(QStringLiteral("tar")),
                          {"-xOf", instance.filePath("mods/" + jar), "fabric.mod.json", "quilt.mod.json",
                           "META-INF/mods.toml", "META-INF/neoforge.mods.toml"});
                tar.waitForFinished(10000);
                static const QRegularExpression fabricClient(QStringLiteral("\"environment\"\\s*:\\s*\"client\""));
                static const QRegularExpression forgeClient(QStringLiteral("clientSideOnly\\s*=\\s*true"));
                const auto text = QString::fromUtf8(tar.readAllStandardOutput());
                client = fabricClient.match(text).hasMatch() || forgeClient.match(text).hasMatch();
            }
            if (client) {
                ++skipped;
                appendConsole(id, QStringLiteral("[OmniLauncher] Só de cliente, fora do servidor: ") + jar);
                continue;
            }
            QFile::remove(target.filePath("mods/" + jar));
            if (QFile::copy(instance.filePath("mods/" + jar), target.filePath("mods/" + jar))) {
                synced.append(jar);
                ++copied;
            }
        }
        QFile track(trackFile);
        if (track.open(QIODevice::WriteOnly | QIODevice::Truncate))
            track.write(QJsonDocument(synced).toJson(QJsonDocument::Compact));
        done(copied, skipped);
    };

    QNetworkRequest request{QUrl(kModrinth + "version_files")};
    request.setTransferTimeout(30000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniLauncher/0.1.0 (desktop launcher)"));
    auto *reply = m_network->post(request, QJsonDocument(QJsonObject{{"hashes", hashes}, {"algorithm", "sha1"}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, fileByHash, apply] {
        reply->deleteLater();
        const auto versions = QJsonDocument::fromJson(reply->readAll()).object();
        QHash<QString, QStringList> filesByProject;
        QSet<QString> known;
        for (auto it = versions.begin(); it != versions.end(); ++it) {
            const auto jar = fileByHash.value(it.key());
            if (jar.isEmpty())
                continue;
            filesByProject[it.value().toObject().value("project_id").toString()].append(jar);
            known.insert(jar);
        }
        if (filesByProject.isEmpty()) {
            apply({}, known);
            return;
        }
        QJsonArray ids;
        for (auto it = filesByProject.begin(); it != filesByProject.end(); ++it)
            ids.append(it.key());
        auto *projects = get(kModrinth + "projects?ids=" + QString::fromUtf8(QUrl::toPercentEncoding(
                                 QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact)))));
        connect(projects, &QNetworkReply::finished, this, [projects, filesByProject, known, apply] {
            projects->deleteLater();
            QSet<QString> clientOnly;
            for (const auto &entry : QJsonDocument::fromJson(projects->readAll()).array()) {
                const auto project = entry.toObject();
                if (project.value("server_side").toString() == "unsupported")
                    for (const auto &jar : filesByProject.value(project.value("id").toString()))
                        clientOnly.insert(jar);
            }
            apply(clientOnly, known);
        });
    });
}

// ── Run ────────────────────────────────────────────────────────────────────

void ServerManager::start(const QString &id)
{
    const auto server = serverById(id);
    const auto state = m_status.value(id).toMap().value("state").toString();
    if (server.isEmpty() || (state != "stopped" && state != "failed"))
        return;
    if (!server.value("installed").toBool()) {
        install(id);
        return;
    }
    const auto launch = server.value("launch").toStringList();
    const auto memory = server.value("memoryMb").toInt();
    QStringList arguments{QStringLiteral("-Xmx%1M").arg(memory), QStringLiteral("-Xms%1M").arg(qMin(memory, 1024)),
                          QStringLiteral("-Dstdout.encoding=UTF-8"), QStringLiteral("-Dstderr.encoding=UTF-8"),
                          QStringLiteral("-Dlog4j2.formatMsgNoLookups=true")};
    arguments << launch << QStringLiteral("nogui");

    auto *process = new QProcess(this);
    process->setWorkingDirectory(serverDir(id));
    process->setProcessChannelMode(QProcess::MergedChannels);
    m_processes.insert(id, process);
    m_partial.remove(id);
    connect(process, &QProcess::readyRead, this, [this, process, id] {
        static const QRegularExpression ansi(QStringLiteral("\x1B\\[[0-9;?]*[A-Za-z]"));
        auto text = m_partial.take(id) + QString::fromUtf8(process->readAll());
        auto lines = text.split('\n');
        m_partial.insert(id, lines.takeLast()); // incomplete line, finished by the next read
        for (auto line : lines) {
            line.remove(ansi);
            line.remove('\r');
            if (line.isEmpty())
                continue;
            appendConsole(id, line);
            parseLine(id, line);
        }
    });
    connect(process, &QProcess::finished, this, [this, process, id](int code, QProcess::ExitStatus) {
        process->deleteLater();
        m_processes.remove(id);
        const auto state = m_status.value(id).toMap().value("state").toString();
        appendConsole(id, QStringLiteral("[OmniLauncher] O servidor fechou (código %1).").arg(code));
        if (m_restartAfterStop.take(id)) {
            setStatus(id, QStringLiteral("stopped"));
            start(id);
            return;
        }
        const bool clean = state == "stopping" || code == 0;
        const auto detail = m_status.value(id).toMap().value("detail").toString();
        setStatus(id, clean ? QStringLiteral("stopped") : QStringLiteral("failed"),
                  clean ? QString() : detail.startsWith("A porta") ? detail
                                     : QStringLiteral("O servidor fechou com erro. Veja o console."));
    });
    connect(process, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            failInstall(id, QStringLiteral("Não foi possível abrir o Java do servidor."));
    });
    appendConsole(id, QStringLiteral("[OmniLauncher] Iniciando com %1 MB de memória…").arg(memory));
    setStatus(id, QStringLiteral("starting"), QStringLiteral("Carregando"));
    process->start(java(server), arguments);
#ifdef Q_OS_WIN
    if (m_job && process->waitForStarted(5000)) {
        if (HANDLE handle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process->processId()))) {
            AssignProcessToJobObject(m_job, handle);
            CloseHandle(handle);
        }
    }
#endif
}

void ServerManager::parseLine(const QString &id, const QString &line)
{
    static const QRegularExpression done(QStringLiteral("\\]: Done \\(([\\d.,]+)s\\)!"));
    static const QRegularExpression joined(QStringLiteral("\\]: ([A-Za-z0-9_]{1,16}) joined the game"));
    static const QRegularExpression left(QStringLiteral("\\]: ([A-Za-z0-9_]{1,16}) left the game"));
    static const QRegularExpression list(QStringLiteral("There are \\d+ of a max(?: of)? \\d+ players online:(.*)$"));
    static const QRegularExpression progress(QStringLiteral("Preparing spawn area: (\\d+)%"));
    static const QRegularExpression bind(QStringLiteral("FAILED TO BIND TO PORT|Address already in use"));
    static const QRegularExpression tunnel(QStringLiteral("Domain assigned: ([A-Za-z0-9.\\-]+\\.e4mc\\.link)"));

    const auto state = m_status.value(id).toMap().value("state").toString();
    auto players = m_status.value(id).toMap().value("players").toStringList();
    if (auto match = tunnel.match(line); match.hasMatch()) {
        auto current = m_status.value(id).toMap();
        current.insert("tunnel", match.captured(1));
        m_status.insert(id, current);
        emit statusChanged();
        return;
    }
    if (auto match = done.match(line); match.hasMatch()) {
        setStatus(id, QStringLiteral("running"), QStringLiteral("Online em %1 s").arg(match.captured(1)));
        return;
    }
    if (auto match = joined.match(line); match.hasMatch()) {
        if (!players.contains(match.captured(1)))
            players.append(match.captured(1));
        setPlayers(id, players);
        return;
    }
    if (auto match = left.match(line); match.hasMatch()) {
        players.removeAll(match.captured(1));
        setPlayers(id, players);
        return;
    }
    if (auto match = list.match(line); match.hasMatch()) {
        QStringList names;
        for (const auto &name : match.captured(1).split(',', Qt::SkipEmptyParts))
            names.append(name.trimmed());
        setPlayers(id, names);
        return;
    }
    if (state == "starting") {
        if (auto match = progress.match(line); match.hasMatch())
            setStatus(id, state, QStringLiteral("Gerando o mundo: %1%").arg(match.captured(1)));
        else if (bind.match(line).hasMatch())
            setStatus(id, state, QStringLiteral("A porta %1 já está em uso por outro programa.")
                                     .arg(properties(id).value("server-port", "25565").toString()));
    }
}

void ServerManager::stop(const QString &id)
{
    QProcess *process = m_processes.value(id);
    if (!process || process->state() == QProcess::NotRunning)
        return;
    setStatus(id, QStringLiteral("stopping"), QStringLiteral("Salvando o mundo"));
    process->write("stop\n");
    // A server stuck on shutdown is ended after a minute.
    QTimer::singleShot(60000, this, [this, id, guard = QPointer<QProcess>(process)] {
        if (guard && guard->state() != QProcess::NotRunning && m_status.value(id).toMap().value("state") == "stopping")
            guard->kill();
    });
}

void ServerManager::restart(const QString &id)
{
    if (!m_processes.value(id))
        return;
    m_restartAfterStop.insert(id, true);
    stop(id);
}

void ServerManager::kill(const QString &id)
{
    if (QProcess *process = m_processes.value(id)) {
        m_restartAfterStop.remove(id);
        setStatus(id, QStringLiteral("stopping"), QStringLiteral("Forçando o fechamento"));
        process->kill();
    }
}

void ServerManager::sendCommand(const QString &id, const QString &command)
{
    QProcess *process = m_processes.value(id);
    const auto text = command.trimmed();
    if (!process || process->state() != QProcess::Running || text.isEmpty())
        return;
    appendConsole(id, QStringLiteral("> ") + text);
    process->write((text.startsWith('/') ? text.mid(1) : text).toUtf8() + '\n');
    if (text == "stop" || text == "/stop")
        setStatus(id, QStringLiteral("stopping"), QStringLiteral("Salvando o mundo"));
}

// ── Settings ───────────────────────────────────────────────────────────────

bool ServerManager::remove(const QString &id)
{
    if (m_processes.value(id))
        return false;
    for (int i = 0; i < m_servers.size(); ++i) {
        if (m_servers.at(i).toMap().value("id").toString() != id)
            continue;
        // Recycle Bin: a deleted world can still be brought back from Windows.
        if (QFileInfo::exists(serverDir(id)) && !QFile::moveToTrash(serverDir(id)))
            return false;
        m_servers.removeAt(i);
        m_status.remove(id);
        m_console.remove(id);
        save();
        emit serversChanged();
        emit statusChanged();
        return true;
    }
    return false;
}

bool ServerManager::setMemory(const QString &id, int memoryMb)
{
    if (serverById(id).isEmpty())
        return false;
    updateServer(id, {{"memoryMb", qBound(1024, memoryMb, 65536)}});
    return true;
}

bool ServerManager::rename(const QString &id, const QString &name)
{
    const auto title = name.trimmed();
    if (serverById(id).isEmpty() || title.isEmpty() || title.size() > 48)
        return false;
    updateServer(id, {{"name", title}});
    return true;
}

QVariantMap ServerManager::properties(const QString &id) const
{
    QVariantMap values;
    QFile file(QDir(serverDir(id)).filePath("server.properties"));
    if (!file.open(QIODevice::ReadOnly))
        return values;
    for (const auto &raw : QString::fromUtf8(file.readAll()).split('\n')) {
        const auto line = raw.trimmed();
        const auto equals = line.indexOf('=');
        if (line.isEmpty() || line.startsWith('#') || equals < 1)
            continue;
        // Minecraft escapes ':' and '=' in values (Java properties format).
        values.insert(line.left(equals).trimmed(), line.mid(equals + 1).replace("\\:", ":").replace("\\=", "=").replace("\\\\", "\\"));
    }
    return values;
}

bool ServerManager::setServerProperty(const QString &id, const QString &key, const QString &value)
{
    static const QRegularExpression keyPattern(QStringLiteral("^[a-z0-9.\\-]{1,64}$"));
    if (serverById(id).isEmpty() || !keyPattern.match(key).hasMatch())
        return false;
    QFile file(QDir(serverDir(id)).filePath("server.properties"));
    QStringList lines;
    if (file.open(QIODevice::ReadOnly)) {
        lines = QString::fromUtf8(file.readAll()).split('\n');
        file.close();
    }
    auto clean = QString(value).replace('\n', ' ').replace('\r', ' ');
    const auto entry = key + '=' + clean;
    bool replaced = false;
    for (auto &line : lines) {
        if (line.trimmed().startsWith(key + '=')) {
            line = entry;
            replaced = true;
        }
    }
    if (!replaced) {
        while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
            lines.removeLast();
        lines.append(entry);
        lines.append(QString());
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(lines.join('\n').toUtf8());
    file.close();
    if (key == "server-port")
        updateServer(id, {{"port", clean.toInt()}});
    if (key == "motd" && serverById(id).value("name").toString().isEmpty())
        updateServer(id, {{"name", clean}});
    emit propertiesChanged(id);
    return true;
}

QStringList ServerManager::playerList(const QString &id, const QString &file) const
{
    static const QStringList allowed{"whitelist.json", "ops.json", "banned-players.json"};
    QStringList names;
    if (!allowed.contains(file))
        return names;
    QFile json(QDir(serverDir(id)).filePath(file));
    if (json.open(QIODevice::ReadOnly))
        for (const auto &entry : QJsonDocument::fromJson(json.readAll()).array())
            names.append(entry.toObject().value("name").toString());
    return names;
}

// ── Connector ──────────────────────────────────────────────────────────────

bool ServerManager::hasConnector(const QString &id) const
{
    return !QDir(QDir(serverDir(id)).filePath("mods")).entryList({"e4mc*.jar"}, QDir::Files).isEmpty();
}

void ServerManager::setConnector(const QString &id, bool enabled)
{
    const auto server = serverById(id);
    const auto loader = server.value("loader").toString();
    if (server.isEmpty() || loader == "Vanilla")
        return;
    const QDir mods(QDir(serverDir(id)).filePath("mods"));
    if (!enabled) {
        for (const auto &jar : mods.entryList({"e4mc*.jar"}, QDir::Files))
            QFile::remove(mods.filePath(jar));
        updateServer(id, {{"connectorOff", true}});
        emit connectorChanged(id);
        if (m_processes.value(id))
            emit message(QStringLiteral("Conector desligado. Reinicie o servidor para aplicar."));
        return;
    }
    updateServer(id, {{"connectorOff", false}});
    if (hasConnector(id)) {
        emit connectorChanged(id);
        return;
    }
    const auto query = QStringLiteral("project/e4mc/version?loaders=%1&game_versions=%2")
                           .arg(QString::fromUtf8(QUrl::toPercentEncoding("[\"" + loader.toLower() + "\"]")),
                                QString::fromUtf8(QUrl::toPercentEncoding("[\"" + server.value("version").toString() + "\"]")));
    auto *reply = get(kModrinth + query);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, mods] {
        reply->deleteLater();
        const auto versions = QJsonDocument::fromJson(reply->readAll()).array();
        QJsonObject file;
        for (const auto &entry : versions.isEmpty() ? QJsonArray() : versions.first().toObject().value("files").toArray())
            if (file.isEmpty() || entry.toObject().value("primary").toBool())
                file = entry.toObject();
        const auto name = QFileInfo(file.value("filename").toString()).fileName();
        if (name.isEmpty() || !name.startsWith("e4mc", Qt::CaseInsensitive)) {
            emit message(QStringLiteral("O conector ainda não tem versão para este Minecraft e loader."));
            emit connectorChanged(id);
            return;
        }
        fetchTo(file.value("url").toString(), mods.filePath(name), [this, id](bool ok) {
            emit message(ok ? (m_processes.value(id) ? QStringLiteral("Conector instalado. Reinicie o servidor para gerar o endereço.")
                                                     : QStringLiteral("Conector instalado. O endereço aparece quando o servidor iniciar."))
                            : QStringLiteral("Não foi possível baixar o conector."));
            emit connectorChanged(id);
        });
    });
}

// ── Backups and folders ────────────────────────────────────────────────────

void ServerManager::backup(const QString &id)
{
    if (serverById(id).isEmpty())
        return;
    const auto world = properties(id).value("level-name", "world").toString();
    const QDir dir(serverDir(id));
    if (!dir.exists(world)) {
        emit message(QStringLiteral("O servidor ainda não tem mundo: inicie-o uma vez."));
        return;
    }
    const auto zip = [this, id, world] {
        const QDir dir(serverDir(id));
        dir.mkpath("backups");
        const auto archive = dir.filePath(QStringLiteral("backups/%1-%2.zip")
                                              .arg(world, QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss")));
        auto *tar = new QProcess(this);
        tar->setWorkingDirectory(dir.absolutePath());
        connect(tar, &QProcess::finished, this, [this, tar, id, archive](int code) {
            tar->deleteLater();
            emit message(code == 0 ? QStringLiteral("Backup salvo: %1").arg(QFileInfo(archive).fileName())
                                   : QStringLiteral("Não foi possível fazer o backup do mundo."));
            emit backupsChanged(id);
        });
        tar->start(QStandardPaths::findExecutable(QStringLiteral("tar")), {"-a", "-c", "--exclude", "session.lock", "-f", archive, world});
    };
    // A running server first writes everything to disk.
    QProcess *process = m_processes.value(id);
    if (process && m_status.value(id).toMap().value("state") == "running") {
        sendCommand(id, QStringLiteral("save-all flush"));
        QTimer::singleShot(4000, this, zip);
        emit message(QStringLiteral("Salvando o mundo para o backup…"));
    } else {
        zip();
    }
}

QVariantList ServerManager::backups(const QString &id) const
{
    QVariantList list;
    const QDir dir(QDir(serverDir(id)).filePath("backups"));
    for (const auto &entry : dir.entryInfoList({"*.zip"}, QDir::Files, QDir::Time))
        list.append(QVariantMap{{"name", entry.fileName()}, {"path", entry.absoluteFilePath()},
                                {"sizeBytes", entry.size()}, {"createdAt", entry.lastModified().toString(Qt::ISODate)}});
    return list;
}

bool ServerManager::openFolder(const QString &id, const QString &sub)
{
    if (serverById(id).isEmpty() || sub.contains(".."))
        return false;
    const auto directory = QDir(serverDir(id)).filePath(sub);
    QDir().mkpath(directory);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}
