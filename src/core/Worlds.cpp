#include "Worlds.h"

#include "Nbt.h"
#include "WorldMap.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>

namespace {

// DataVersion of 1.18 (21w37a's world format, kept since): the chunk layout the map reads.
constexpr int firstMappableDataVersion = 2860;

QString modeName(int gameType)
{
    switch (gameType) {
    case 1: return QStringLiteral("creative");
    case 2: return QStringLiteral("adventure");
    case 3: return QStringLiteral("spectator");
    default: return QStringLiteral("survival");
    }
}

// server.properties as key → value (no escapes: the keys read here never have them).
QHash<QString, QString> serverProperties(const QString &file)
{
    QHash<QString, QString> properties;
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return properties;
    for (const auto &raw : QString::fromUtf8(in.readAll()).split('\n')) {
        const auto line = raw.trimmed();
        const auto equals = line.indexOf('=');
        if (line.isEmpty() || line.startsWith('#') || equals <= 0)
            continue;
        properties.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }
    return properties;
}

} // namespace

Worlds::Worlds(const QString &dataDirectory, QObject *parent)
    : QObject(parent)
    , m_root(dataDirectory)
{
    m_pool.setMaxThreadCount(1);
    m_settle.setSingleShot(true);
    m_settle.setInterval(2000);
    m_poll.setInterval(10000);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_settle, &QTimer::timeout, this, &Worlds::check);
    connect(&m_poll, &QTimer::timeout, this, &Worlds::check);
}

QVariantList Worlds::list(const QVariantList &games) const
{
    QVariantList worlds;
    // A world folder, read from its level.dat, with `about` (who it belongs to) added.
    const auto add = [&worlds](const QFileInfo &folder, QVariantMap about, const QString &icon) {
        const QDir dir(folder.absoluteFilePath());
        auto level = Nbt::readFile(dir.filePath("level.dat"));
        // The game keeps the previous save as level.dat_old while it writes the new one.
        if (!level.isValid())
            level = Nbt::readFile(dir.filePath("level.dat_old"));
        const auto data = Nbt::at(level, {"Data"});
        if (!data.isValid())
            return;

        auto seed = Nbt::at(data, {"WorldGenSettings", "seed"});
        if (!seed.isValid())
            seed = Nbt::at(data, {"RandomSeed"});
        const auto dataVersion = Nbt::at(data, {"DataVersion"}).toInt();
        const auto name = Nbt::at(data, {"LevelName"}).toString();

        about.insert({
            {"folder", folder.fileName()},
            {"path", folder.absoluteFilePath()},
            {"seed", seed.isValid() ? QString::number(seed.toLongLong()) : QString()},
            {"mode", modeName(Nbt::at(data, {"GameType"}).toInt())},
            {"hardcore", Nbt::at(data, {"hardcore"}).toInt() != 0},
            {"lastPlayed", Nbt::at(data, {"LastPlayed"}).toLongLong()},
            {"version", Nbt::at(data, {"Version", "Name"}).toString()},
            {"dataVersion", dataVersion},
            {"mappable", dataVersion >= firstMappableDataVersion},
            {"icon", QFileInfo::exists(icon) ? QUrl::fromLocalFile(icon).toString() : QString()},
        });
        if (!about.contains("name"))
            about.insert("name", name.isEmpty() ? folder.fileName() : name);
        worlds.append(about);
    };

    QHash<QString, QString> titles;
    for (const auto &entry : games) {
        const auto game = entry.toMap();
        const auto id = game.value("id").toString();
        titles.insert(id, game.value("title").toString());
        const auto instance = QDir(m_root).filePath("instances/" + id);
        const QDir saves(instance + "/saves");
        for (const auto &folder : saves.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            add(folder,
                {{"gameId", id}, {"gameTitle", game.value("title")}, {"instance", instance}, {"server", false}, {"serverPort", 0}},
                QDir(folder.absoluteFilePath()).filePath("icon.png"));
    }

    // The servers this launcher hosts (servers.json, servers/<id>): their world is where
    // server.properties says. Its blocks come from the game the server was made from
    // (the same mods, and its resource packs), or from the server's own mods without it.
    QFile servers(QDir(m_root).filePath("servers.json"));
    if (servers.open(QIODevice::ReadOnly)) {
        for (const auto &entry : QJsonDocument::fromJson(servers.readAll()).array()) {
            const auto server = entry.toObject();
            const auto id = server.value("id").toString();
            if (id.isEmpty())
                continue;
            const QDir dir(QDir(m_root).filePath("servers/" + id));
            const auto properties = serverProperties(dir.filePath("server.properties"));
            const QFileInfo folder(dir.filePath(properties.value("level-name", "world")));
            if (!folder.isDir())
                continue;
            const auto gameId = server.value("gameId").toString();
            const auto instance = QDir(m_root).filePath("instances/" + gameId);
            const auto port = properties.value("server-port", "25565").toInt();
            add(folder,
                {{"gameId", gameId}, {"gameTitle", titles.value(gameId)}, {"instance", QFileInfo::exists(instance) ? instance : dir.path()},
                 {"server", true}, {"serverId", id}, {"serverPort", port > 0 ? port : 25565},
                 {"name", server.value("name").toString(id)}},
                dir.filePath("server-icon.png"));
        }
    }
    std::sort(worlds.begin(), worlds.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("lastPlayed").toLongLong() > b.toMap().value("lastPlayed").toLongLong();
    });
    return worlds;
}

void Worlds::request(const QVariantList &games)
{
    const auto serial = ++m_serial;
    m_pool.start([this, games, serial] {
        const auto worlds = list(games);
        QMetaObject::invokeMethod(this, [this, worlds, serial] {
            if (serial == m_serial)
                emit listed(worlds);
        }, Qt::QueuedConnection);
    });
}

QVariantMap Worlds::describe(const QString &worldPath, const QString &dimension) const
{
    return WorldMap::describe(worldPath, dimension);
}

void Worlds::requestEntities(const QString &worldPath, const QString &dimension)
{
    m_pool.start([this, worldPath, dimension] {
        const auto found = WorldMap::entities(worldPath, dimension);
        QMetaObject::invokeMethod(this, [this, worldPath, dimension, found] {
            emit entitiesLoaded(worldPath, dimension, found);
        }, Qt::QueuedConnection);
    });
}

QStringList Worlds::watchedFolders() const
{
    const QDir world(m_watchedWorld);
    return {WorldMap::dimensionFolder(m_watchedWorld, m_watchedDimension),
            WorldMap::dimensionFolder(m_watchedWorld, m_watchedDimension, QStringLiteral("entities")),
            world.filePath("players/data"), world.filePath("playerdata")};
}

// Size and date of every file the map draws from; a change in either is a new save.
QHash<QString, qint64> Worlds::stamps() const
{
    QHash<QString, qint64> found;
    const auto add = [&found](const QFileInfo &info) {
        found.insert(info.absoluteFilePath(), info.lastModified().toMSecsSinceEpoch() ^ (info.size() << 40));
    };
    add(QFileInfo(QDir(m_watchedWorld).filePath("level.dat")));
    for (const auto &folder : watchedFolders())
        if (!folder.isEmpty())
            for (const auto &info : QDir(folder).entryInfoList({"*.mca", "*.dat"}, QDir::Files))
                add(info);
    return found;
}

void Worlds::check()
{
    if (m_watchedWorld.isEmpty())
        return;
    auto now = stamps();
    if (now == m_stamps)
        return;
    // New files (a region first explored) and level.dat, which the game saves by renaming
    // a new file over it, which ends its watch.
    QStringList added;
    for (auto it = now.cbegin(); it != now.cend(); ++it)
        if (!m_watcher.files().contains(it.key()))
            added.append(it.key());
    if (!added.isEmpty())
        m_watcher.addPaths(added);
    m_stamps = std::move(now);
    emit worldChanged(m_watchedWorld, m_watchedDimension);
}

void Worlds::watch(const QString &worldPath, const QString &dimension)
{
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_settle.stop();
    m_watchedWorld = worldPath;
    m_watchedDimension = dimension;
    m_stamps.clear();
    if (worldPath.isEmpty())
        return;
    // The folders tell of new files; each file, of being written again (on Windows a
    // folder's watch misses edits to the files in it).
    m_stamps = stamps();
    QStringList paths = m_stamps.keys();
    for (const auto &folder : watchedFolders())
        if (!folder.isEmpty() && QFileInfo(folder).isDir())
            paths.append(folder);
    m_watcher.addPaths(paths);
}

void Worlds::setPolling(bool polling)
{
    if (polling) {
        m_poll.start();
    } else if (m_poll.isActive()) {
        m_poll.stop();
        check(); // the game just closed: its last save
    }
}
