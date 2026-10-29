#include "Wrapped.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace {
using Counts = QHash<QString, qint64>;

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

// "minecraft:deepslate_diamond_ore" → "deepslate_diamond_ore"
QString bare(const QString &id)
{
    return id.startsWith("minecraft:") ? id.mid(10) : id;
}

QString humanize(const QString &id)
{
    auto text = bare(id).section(':', -1).replace('_', ' ');
    if (!text.isEmpty())
        text[0] = text[0].toUpper();
    return text;
}

QVariantList top(const Counts &counts, int limit)
{
    QList<QPair<QString, qint64>> sorted;
    for (auto it = counts.begin(); it != counts.end(); ++it)
        if (it.value() > 0)
            sorted.append({it.key(), it.value()});
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    QVariantList list;
    for (int i = 0; i < qMin(limit, int(sorted.size())); ++i)
        list.append(QVariantMap{{"id", sorted[i].first}, {"count", sorted[i].second}});
    return list;
}

qint64 sum(const Counts &counts)
{
    qint64 total = 0;
    for (const auto value : counts)
        total += value;
    return total;
}

// Pulls members out of a client jar (a zip) into `into`; members it lacks only make tar complain.
void extract(const QString &jar, const QString &into, const QStringList &members)
{
    if (members.isEmpty() || !QFileInfo::exists(jar))
        return;
    QDir(into).mkpath(".");
    QProcess tar;
    tar.start(QStandardPaths::findExecutable(QStringLiteral("tar")),
              QStringList{"-xf", QDir::toNativeSeparators(jar), "-C", QDir::toNativeSeparators(into)} + members);
    tar.waitForFinished(20000);
}
} // namespace

Wrapped::Wrapped(const QString &dataDirectory, QObject *parent)
    : QObject(parent)
    , m_root(dataDirectory)
{
}

QString Wrapped::jar(const QString &version) const
{
    return QDir(m_root).filePath(QStringLiteral("versions/%1/%1.jar").arg(version));
}

// Minecraft's own names in the interface language (the QLocale default Language sets).
// pt_br.json is an asset object already downloaded; en_us.json only ships inside the jar.
const QHash<QString, QString> &Wrapped::language(const QStringList &versions)
{
    const auto code = QLocale().language() == QLocale::Portuguese ? QStringLiteral("pt_br") : QStringLiteral("en_us");
    if (m_languageCode == code && !m_language.isEmpty())
        return m_language;
    m_languageCode = code;
    m_language.clear();
    const auto load = [this](const QString &path) {
        const auto strings = readJson(path);
        for (auto it = strings.begin(); it != strings.end(); ++it)
            m_language.insert(it.key(), it.value().toString());
        return !m_language.isEmpty();
    };
    if (code == "en_us") {
        const auto member = QStringLiteral("assets/minecraft/lang/en_us.json");
        for (const auto &version : versions) {
            const auto dir = QDir(m_root).filePath("cache/wrapped/" + version);
            if (!QFileInfo::exists(QDir(dir).filePath(member)))
                extract(jar(version), dir, {member});
            if (load(QDir(dir).filePath(member)))
                break;
        }
        return m_language;
    }
    // The newest asset index that knows pt_br.json.
    const QDir indexes(QDir(m_root).filePath("assets/indexes"));
    for (const auto &index : indexes.entryInfoList({"*.json"}, QDir::Files, QDir::Time)) {
        const auto hash = readJson(index.absoluteFilePath()).value("objects").toObject()
                              .value("minecraft/lang/pt_br.json").toObject().value("hash").toString();
        if (hash.size() >= 2 && load(QDir(m_root).filePath("assets/objects/" + hash.left(2) + '/' + hash)))
            break;
    }
    return m_language;
}

QString Wrapped::name(const QString &id, const QString &kind, const QStringList &versions)
{
    const auto &strings = language(versions);
    const auto key = bare(id).replace(':', '.');
    const QStringList order = kind == "entity" ? QStringList{"entity"} : kind == "item" ? QStringList{"item", "block"}
                                                                                       : QStringList{"block", "item"};
    for (const auto &prefix : order) {
        const auto text = strings.value(prefix + ".minecraft." + key);
        if (!text.isEmpty())
            return text;
    }
    return humanize(id);
}

// Result: scope ("game" | "all"), title, games, worlds, sessions, hasStats,
// playSeconds (launcher clock), playTicks (in-game clock),
// distanceCm, travel [{key, label, cm}],
// mined, topMined [{id, name, count, icon}], mobKills, topKilled, deaths, topKilledBy,
// crafted, topCrafted, used, topUsed, extras {jumps, fish, bred, trades, sleeps,
// enchants, chests, hearts, playerKills, raids, cakes}, topWorld {name, game, seconds},
// topGame {title, seconds}.
QVariantMap Wrapped::build(const QVariantList &games, const QVariantMap &gameSettings, const QString &gameId)
{
    QHash<QString, Counts> stats;
    int worldCount = 0, gameCount = 0;
    qint64 launcherSeconds = 0, bestGameSeconds = -1;
    QString title, bestGameTitle;
    QList<QPair<qint64, QString>> played; // (seconds, version) of the games covered
    QVariantMap topWorld;
    qint64 bestWorldTicks = -1;

    for (const auto &entry : games) {
        const auto game = entry.toMap();
        const auto id = game.value("id").toString();
        if (!gameId.isEmpty() && id != gameId)
            continue;
        ++gameCount;
        const auto seconds = gameSettings.value(id).toMap().value("playSeconds").toLongLong();
        launcherSeconds += seconds;
        if (seconds > bestGameSeconds) {
            bestGameSeconds = seconds;
            bestGameTitle = game.value("title").toString();
        }
        played.append({seconds, game.value("version").toString()});
        if (!gameId.isEmpty())
            title = game.value("title").toString();

        const QDir saves(QDir(m_root).filePath("instances/" + id + "/saves"));
        for (const auto &world : saves.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            qint64 worldTicks = 0;
            bool any = false;
            for (const auto &folder : {QStringLiteral("stats"), QStringLiteral("players/stats")}) {
                const QDir dir(QDir(world.absoluteFilePath()).filePath(folder));
                for (const auto &file : dir.entryInfoList({"*.json"}, QDir::Files)) {
                    const auto categories = readJson(file.absoluteFilePath()).value("stats").toObject();
                    for (auto c = categories.begin(); c != categories.end(); ++c) {
                        auto &target = stats[bare(c.key())];
                        const auto values = c.value().toObject();
                        for (auto v = values.begin(); v != values.end(); ++v)
                            target[v.key()] += v.value().toInteger();
                    }
                    const auto custom = categories.value("minecraft:custom").toObject();
                    worldTicks += custom.value("minecraft:play_time").toInteger(custom.value("minecraft:play_one_minute").toInteger());
                    any = true;
                }
            }
            if (!any)
                continue;
            ++worldCount;
            if (worldTicks > bestWorldTicks) {
                bestWorldTicks = worldTicks;
                topWorld = {{"name", world.fileName()}, {"game", game.value("title")}, {"seconds", worldTicks / 20}};
            }
        }
    }

    const auto &custom = stats["custom"];
    const auto c = [&custom](const char *key) { return custom.value(QStringLiteral("minecraft:") + QLatin1StringView(key)); };

    static const QList<QPair<const char *, const char *>> travelKinds{
        {"walk_one_cm", "A pé"}, {"sprint_one_cm", "Correndo"}, {"aviate_one_cm", "De élitros"},
        {"fly_one_cm", "Voando"}, {"swim_one_cm", "Nadando"}, {"boat_one_cm", "De barco"},
        {"horse_one_cm", "A cavalo"}, {"minecart_one_cm", "De carrinho"}, {"crouch_one_cm", "Agachado"},
        {"climb_one_cm", "Escalando"}, {"walk_under_water_one_cm", "Debaixo d'água"}, {"fall_one_cm", "Caindo"}};
    QVariantList travel;
    qint64 distance = 0;
    for (const auto &[key, label] : travelKinds) {
        const auto cm = c(key);
        if (cm <= 0)
            continue;
        distance += cm;
        travel.append(QVariantMap{{"key", QString::fromLatin1(key)}, {"label", tr(label)}, {"cm", cm}});
    }
    std::sort(travel.begin(), travel.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("cm").toLongLong() > b.toMap().value("cm").toLongLong();
    });

    auto topMined = top(stats["mined"], 5);
    auto topKilled = top(stats["killed"], 5);
    auto topKilledBy = top(stats["killed_by"], 3);
    auto topCrafted = top(stats["crafted"], 3);
    auto topUsed = top(stats["used"], 3);

    // Versions to take names and icons from: the games covered, most played first, then
    // every other installed version. A very old one (alpha, beta) has neither the
    // textures nor the names in today's layout, so later versions fill the gaps.
    std::stable_sort(played.begin(), played.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList versions;
    for (const auto &entry : played)
        versions.append(entry.second);
    for (const auto &info : QDir(QDir(m_root).filePath("versions")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time))
        versions.append(info.fileName());
    versions.removeDuplicates();
    versions.removeIf([this](const QString &version) { return !QFileInfo::exists(jar(version)); });

    // Icons: textures pulled from the client jars into cache/wrapped/<version>/, once.
    const auto candidates = [](const QString &id, const QString &kind) {
        const auto n = bare(id);
        if (n.contains(':'))
            return QStringList(); // modded content: not in the vanilla jar
        if (kind == "entity")
            return QStringList{"item/" + n + "_spawn_egg"};
        QStringList list{"item/" + n, "block/" + n, "block/" + n + "_front", "block/" + n + "_side", "block/" + n + "_top"};
        if (kind == "item")
            return list;
        list.move(1, 0);
        return list;
    };
    const auto findIcon = [&](const QString &id, const QString &kind) {
        for (const auto &version : versions)
            for (const auto &candidate : candidates(id, kind)) {
                const auto file = QDir(m_root).filePath("cache/wrapped/" + version + "/assets/minecraft/textures/" + candidate + ".png");
                if (QFileInfo::exists(file))
                    return file;
            }
        return QString();
    };
    QList<QPair<QVariantList *, QString>> lists{{&topMined, "block"}, {&topKilled, "entity"}, {&topKilledBy, "entity"},
                                                {&topCrafted, "item"}, {&topUsed, "item"}};
    // Only entries with no icon in any cache yet send tar into the jars, newest guess first.
    QList<QPair<QString, QString>> unresolved;
    for (const auto &[list, kind] : lists)
        for (const auto &entry : *list)
            if (findIcon(entry.toMap().value("id").toString(), kind).isEmpty())
                unresolved.append({entry.toMap().value("id").toString(), kind});
    for (const auto &version : versions) {
        if (unresolved.isEmpty())
            break;
        QStringList members;
        for (const auto &[id, kind] : unresolved)
            for (const auto &candidate : candidates(id, kind))
                members.append("assets/minecraft/textures/" + candidate + ".png");
        members.removeDuplicates();
        extract(jar(version), QDir(m_root).filePath("cache/wrapped/" + version), members);
        unresolved.removeIf([&](const auto &entry) { return !findIcon(entry.first, entry.second).isEmpty(); });
    }
    for (const auto &[list, kind] : lists) {
        for (auto &entry : *list) {
            auto item = entry.toMap();
            const auto id = item.value("id").toString();
            item.insert("name", name(id, kind, versions));
            const auto icon = findIcon(id, kind);
            if (!icon.isEmpty())
                item.insert("icon", QUrl::fromLocalFile(icon).toString());
            entry = item;
        }
    }

    const auto playTicks = c("play_time") > 0 ? c("play_time") : c("play_one_minute");
    return {
        {"scope", gameId.isEmpty() ? "all" : "game"},
        {"title", gameId.isEmpty() ? tr("Todo o seu Minecraft") : title},
        {"games", gameCount},
        {"worlds", worldCount},
        {"sessions", c("leave_game")},
        {"hasStats", worldCount > 0},
        {"playSeconds", launcherSeconds},
        {"playTicks", playTicks},
        {"distanceCm", distance},
        {"travel", travel},
        {"mined", sum(stats["mined"])},
        {"topMined", topMined},
        {"mobKills", c("mob_kills")},
        {"topKilled", topKilled},
        {"deaths", c("deaths")},
        {"topKilledBy", topKilledBy},
        {"crafted", sum(stats["crafted"])},
        {"topCrafted", topCrafted},
        {"used", sum(stats["used"])},
        {"topUsed", topUsed},
        {"extras", QVariantMap{{"jumps", c("jump")}, {"fish", c("fish_caught")}, {"bred", c("animals_bred")},
                               {"trades", c("traded_with_villager")}, {"sleeps", c("sleep_in_bed")},
                               {"enchants", c("enchant_item")}, {"chests", c("open_chest")},
                               {"hearts", c("damage_dealt") / 20}, {"playerKills", c("player_kills")},
                               {"raids", c("raid_win")}, {"cakes", c("eat_cake_slice")}}},
        {"topWorld", topWorld},
        {"topGame", QVariantMap{{"title", bestGameTitle}, {"seconds", bestGameSeconds}}},
    };
}
