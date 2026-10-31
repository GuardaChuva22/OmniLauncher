#include "WorldMap.h"

#include "Nbt.h"
#include "WorldData.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <memory>

namespace {

using namespace WorldMap;
using namespace WorldData;

constexpr BiomeColor plainsBiome{qRgb(145, 189, 89), qRgb(119, 171, 47), qRgb(63, 118, 228)};
// Bumped when the palette changes, so what was kept is made again.
constexpr int PaletteFormat = 1;

QRgb multiply(QRgb color, QRgb tint)
{
    return qRgba(qRed(color) * qRed(tint) / 255, qGreen(color) * qGreen(tint) / 255, qBlue(color) * qBlue(tint) / 255, qAlpha(color));
}

// Alpha-weighted average of a texture's first frame; alpha 0 if it has no opaque pixel.
QRgb average(const Assets &assets, const QString &ref)
{
    auto image = QImage::fromData(assets.read(assetPath(ref, "textures", ".png")));
    if (image.isNull())
        return 0;
    image = image.convertToFormat(QImage::Format_ARGB32);
    const int size = qMin(image.width(), image.height()); // animated: a vertical strip
    double r = 0, g = 0, b = 0, weight = 0;
    for (int y = 0; y < size; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < size; ++x) {
            const double a = qAlpha(line[x]) / 255.0;
            r += qRed(line[x]) * a;
            g += qGreen(line[x]) * a;
            b += qBlue(line[x]) * a;
            weight += a;
        }
    }
    return weight > 0 ? qRgb(int(r / weight), int(g / weight), int(b / weight)) : 0;
}

// The model a block shows standing upright: a log's "axis=y", else its first variant.
QString stateModel(const QJsonObject &states)
{
    const auto model = [](const QJsonValue &value) {
        return (value.isArray() ? value.toArray().first() : value).toObject().value("model").toString();
    };
    const auto variants = states.value("variants").toObject();
    for (auto it = variants.begin(); it != variants.end(); ++it)
        if (it.key().contains("axis=y"))
            return model(it.value());
    if (!variants.isEmpty())
        return model(*variants.begin());
    const auto parts = states.value("multipart").toArray();
    return parts.isEmpty() ? QString() : model(parts.first().toObject().value("apply"));
}

// What tints a block: the biome's grass, foliage or water, or (spruce, birch, lily pads)
// a colour of its own, applied right away.
quint8 tintKind(const QString &name, QRgb *fixed)
{
    *fixed = 0;
    if (name.contains("water"))
        return TintWater;
    if (name.contains("spruce_leaves"))
        *fixed = qRgb(97, 153, 97);
    else if (name.contains("birch_leaves"))
        *fixed = qRgb(128, 167, 85);
    else if (name.contains("lily_pad"))
        *fixed = qRgb(32, 128, 48);
    if (*fixed)
        return 0;
    if (name.contains("leaves") || name.contains("vine"))
        return TintFoliage;
    return TintGrass;
}

BlockColor blockColor(const Assets &assets, const QString &name, QString ref)
{
    QJsonObject textures;
    QJsonArray elements;
    for (int depth = 0; depth < 16 && !ref.isEmpty(); ++depth) {
        const auto json = assets.json(assetPath(ref, "models", ".json"));
        if (json.isEmpty())
            break;
        const auto own = json.value("textures").toObject();
        for (auto it = own.begin(); it != own.end(); ++it)
            if (!textures.contains(it.key()))
                textures.insert(it.key(), it.value().isObject() ? it.value().toObject().value("sprite") : it.value());
        if (elements.isEmpty())
            elements = json.value("elements").toArray();
        ref = json.value("parent").toString();
    }
    const auto resolve = [&textures](QString texture) {
        for (int i = 0; i < 8 && texture.startsWith('#'); ++i)
            texture = textures.value(texture.mid(1)).toString();
        return texture.startsWith('#') ? QString() : texture;
    };
    QRgb fixed;
    const auto kind = tintKind(name, &fixed);
    const auto finish = [&](BlockColor color, bool topTinted, bool sideTinted) {
        if (fixed) {
            if (topTinted)
                color.top = multiply(color.top, fixed);
            if (sideTinted)
                color.side = multiply(color.side, fixed);
        } else if (topTinted || sideTinted) {
            color.tint = kind | (topTinted ? TopTinted : 0) | (sideTinted ? SideTinted : 0);
        }
        return color;
    };

    // Fluids have no elements: their still texture, which the game tints (water) or not.
    if (elements.isEmpty()) {
        if (!name.contains("water") && !name.contains("lava"))
            return {};
        const auto color = average(assets, resolve("#particle"));
        const bool water = name.contains("water");
        return finish({color, color, 0}, water, water);
    }
    BlockColor color;
    bool topTinted = false, sideTinted = false;
    for (const auto &element : elements) {
        const auto faces = element.toObject().value("faces").toObject();
        const auto up = faces.value("up").toObject();
        if (!qAlpha(color.top)) {
            color.top = average(assets, resolve(up.value("texture").toString()));
            topTinted = up.contains("tintindex");
        }
        for (const auto *sideName : {"north", "south", "east", "west"}) {
            const auto side = faces.value(QLatin1StringView(sideName)).toObject();
            if (qAlpha(color.side) || side.isEmpty())
                continue;
            color.side = average(assets, resolve(side.value("texture").toString()));
            sideTinted = side.contains("tintindex");
        }
    }
    if (!qAlpha(color.top))
        return {}; // no top face: plants, torches, rails
    if (!qAlpha(color.side)) {
        color.side = color.top;
        sideTinted = topTinted;
    }
    return finish(color, topTinted, sideTinted);
}

QRgb parseColor(const QJsonValue &value)
{
    if (value.isString())
        return QColor::fromString(value.toString()).rgb();
    return value.isDouble() ? (QRgb(value.toInteger()) | 0xFF000000) : 0;
}

// A biome's colours: its own, or the grass and foliage colour maps at its temperature
// and downfall, as the game picks them.
BiomeColor biomeColor(const QJsonObject &biome, const QImage &grassMap, const QImage &foliageMap)
{
    const auto effects = biome.value("effects").toObject();
    const double temperature = qBound(0.0, biome.value("temperature").toDouble(0.8), 1.0);
    const double downfall = qBound(0.0, biome.value("downfall").toDouble(0.4), 1.0) * temperature;
    const auto fromMap = [&](const QImage &map, QRgb fallback) {
        if (map.isNull())
            return fallback;
        const int x = qBound(0, int((1.0 - temperature) * (map.width() - 1)), map.width() - 1);
        const int y = qBound(0, int((1.0 - downfall) * (map.height() - 1)), map.height() - 1);
        return map.pixel(x, y) | 0xFF000000;
    };
    BiomeColor color{parseColor(effects.value("grass_color")), parseColor(effects.value("foliage_color")),
                     parseColor(effects.value("water_color"))};
    if (!color.grass)
        color.grass = fromMap(grassMap, plainsBiome.grass);
    if (!color.foliage)
        color.foliage = fromMap(foliageMap, plainsBiome.foliage);
    if (!color.water)
        color.water = plainsBiome.water;
    const auto modifier = effects.value("grass_color_modifier").toString();
    if (modifier == "swamp")
        color.grass = qRgb(106, 112, 57);
    else if (modifier == "dark_forest")
        color.grass = (((color.grass & 0xFEFEFE) + 0x28340A) >> 1) | 0xFF000000;
    return color;
}

// Every block and biome a jar defines, its models and textures looked up in `assets`.
Palette readJar(Jar &jar, const Assets &assets)
{
    Palette palette;
    static const QRegularExpression states(QStringLiteral("^assets/([^/]+)/blockstates/(.+)\\.json$"));
    static const QRegularExpression biomes(QStringLiteral("^data/([^/]+)/worldgen/biome/(.+)\\.json$"));
    const auto grassMap = QImage::fromData(assets.read("assets/minecraft/textures/colormap/grass.png"));
    const auto foliageMap = QImage::fromData(assets.read("assets/minecraft/textures/colormap/foliage.png"));
    for (const auto &name : jar.names()) {
        if (const auto match = states.match(name); match.hasMatch()) {
            const auto id = match.captured(1) + ':' + match.captured(2);
            palette.blocks.insert(id, blockColor(assets, id, stateModel(QJsonDocument::fromJson(jar.read(name)).object())));
        } else if (const auto biome = biomes.match(name); biome.hasMatch()) {
            palette.biomes.insert(biome.captured(1) + ':' + biome.captured(2),
                                  biomeColor(QJsonDocument::fromJson(jar.read(name)).object(), grassMap, foliageMap));
        }
    }
    return palette;
}

QJsonObject savePalette(const Palette &palette)
{
    QJsonObject blocks, biomes;
    for (auto it = palette.blocks.cbegin(); it != palette.blocks.cend(); ++it)
        blocks.insert(it.key(), QJsonArray{qint64(it->top), qint64(it->side), it->tint});
    for (auto it = palette.biomes.cbegin(); it != palette.biomes.cend(); ++it)
        biomes.insert(it.key(), QJsonArray{qint64(it->grass), qint64(it->foliage), qint64(it->water)});
    return {{"blocks", blocks}, {"biomes", biomes}};
}

// Adds what `into` lacks: the client's palette is loaded first, so a mod that ships its
// own look for vanilla blocks (3D leaves, resource tweaks) never recolours them.
void loadPalette(const QJsonObject &json, Palette &into)
{
    const auto blocks = json.value("blocks").toObject();
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        if (into.blocks.contains(it.key()))
            continue;
        const auto v = it.value().toArray();
        into.blocks.insert(it.key(), {QRgb(v[0].toInteger()), QRgb(v[1].toInteger()), quint8(v[2].toInt())});
    }
    const auto biomes = json.value("biomes").toObject();
    for (auto it = biomes.begin(); it != biomes.end(); ++it) {
        if (into.biomes.contains(it.key()))
            continue;
        const auto v = it.value().toArray();
        into.biomes.insert(it.key(), {QRgb(v[0].toInteger()), QRgb(v[1].toInteger()), QRgb(v[2].toInteger())});
    }
}

} // namespace

namespace WorldMap {

Palette palette(const QString &dataDirectory, const QString &version, const QString &instanceDirectory)
{
    const QDir cache(QDir(dataDirectory).filePath(QStringLiteral("cache/worlds/palette/v%1").arg(PaletteFormat)));
    cache.mkpath(".");
    const auto client = clientJar(dataDirectory, version);
    std::unique_ptr<Jar> clientArchive;
    const auto load = [&](const QString &jarPath, const QString &file, bool isClient, Palette &into) {
        QFile saved(cache.filePath(file));
        if (saved.open(QIODevice::ReadOnly)) {
            loadPalette(QJsonDocument::fromJson(saved.readAll()).object(), into);
            return;
        }
        if (!clientArchive && !client.isEmpty())
            clientArchive = std::make_unique<Jar>(client);
        std::unique_ptr<Jar> own;
        Assets assets;
        if (!isClient) {
            own = std::make_unique<Jar>(jarPath);
            assets.jars.append(own.get());
        }
        if (clientArchive)
            assets.jars.append(clientArchive.get());
        if (assets.jars.isEmpty())
            return;
        const auto read = readJar(*assets.jars.first(), assets);
        if (saved.open(QIODevice::WriteOnly))
            saved.write(QJsonDocument(savePalette(read)).toJson(QJsonDocument::Compact));
        loadPalette(savePalette(read), into);
    };

    Palette result;
    if (!client.isEmpty())
        load(client, "client-" + QFileInfo(client).completeBaseName() + ".json", true, result);
    // Mods: one file per jar, named by its name, size and date (an update draws anew).
    const QDir mods(QDir(instanceDirectory).filePath("mods"));
    for (const auto &info : mods.entryInfoList({"*.jar"}, QDir::Files)) {
        const auto signature = QCryptographicHash::hash(
            (info.fileName() + '|' + QString::number(info.size()) + '|' + QString::number(info.lastModified().toSecsSinceEpoch())).toUtf8(),
            QCryptographicHash::Sha1).toHex().left(16);
        load(info.absoluteFilePath(), "mod-" + QString::fromLatin1(signature) + ".json", false, result);
    }
    return result;
}

QString dimensionFolder(const QString &worldPath, const QString &dimension, const QString &kind)
{
    const QDir world(worldPath);
    QStringList candidates{"dimensions/" + QString(dimension).replace(':', '/') + '/' + kind}; // 26.1+
    if (dimension == "minecraft:overworld")
        candidates << kind;
    else if (dimension == "minecraft:the_nether")
        candidates << "DIM-1/" + kind;
    else if (dimension == "minecraft:the_end")
        candidates << "DIM1/" + kind;
    for (const auto &candidate : candidates)
        if (QFileInfo(world.filePath(candidate)).isDir())
            return world.filePath(candidate);
    return {};
}

QVariantMap describe(const QString &worldPath, const QString &dimension)
{
    const auto level = Nbt::readFile(QDir(worldPath).filePath("level.dat"));
    const auto data = Nbt::at(level, {"Data"});

    QVariantList dimensions;
    for (const auto *id : {"minecraft:overworld", "minecraft:the_nether", "minecraft:the_end"})
        if (!dimensionFolder(worldPath, QString::fromLatin1(id)).isEmpty())
            dimensions.append(QString::fromLatin1(id));

    QVariantList regions;
    static const QRegularExpression pattern(QStringLiteral("^r\\.(-?\\d+)\\.(-?\\d+)\\.mca$"));
    const QDir folder(dimensionFolder(worldPath, dimension));
    for (const auto &info : folder.entryInfoList({"r.*.mca"}, QDir::Files)) {
        const auto match = pattern.match(info.fileName());
        if (match.hasMatch() && info.size() > 8192) // just the header: no chunk yet
            regions.append(QVariantMap{{"x", match.captured(1).toInt()}, {"z", match.captured(2).toInt()},
                                       {"stamp", info.lastModified().toSecsSinceEpoch()}});
    }

    // Where to look first: the player (singleplayer keeps it in level.dat), else the spawn.
    QVariantMap player;
    auto playerData = Nbt::at(data, {"Player"});
    if (!playerData.isValid()) {
        // 26.1+ keeps it out of level.dat, in players/data/<uuid>.dat (playerdata/ before):
        // the latest one written is who played last.
        for (const auto *place : {"players/data", "playerdata"}) {
            const auto files = QDir(QDir(worldPath).filePath(QString::fromLatin1(place))).entryInfoList({"*.dat"}, QDir::Files, QDir::Time);
            if (!files.isEmpty()) {
                playerData = Nbt::readFile(files.first().absoluteFilePath());
                break;
            }
        }
    }
    const auto pos = Nbt::at(playerData, {"Pos"}).toList();
    auto playerDimension = Nbt::at(playerData, {"Dimension"});
    if (playerDimension.typeId() == QMetaType::Int) // before 1.16: 0, -1, 1
        playerDimension = QVariantMap{{"0", "minecraft:overworld"}, {"-1", "minecraft:the_nether"}, {"1", "minecraft:the_end"}}
                              .value(playerDimension.toString());
    if (pos.size() == 3)
        player = {{"x", pos[0].toDouble()}, {"y", pos[1].toDouble()}, {"z", pos[2].toDouble()},
                  {"dimension", playerDimension.toString().isEmpty() ? QStringLiteral("minecraft:overworld") : playerDimension.toString()}};
    QVariantMap spawn;
    const auto spawnPos = Nbt::at(data, {"spawn", "pos"}).value<QList<qint32>>(); // 1.21.9+
    if (spawnPos.size() == 3)
        spawn = {{"x", spawnPos[0]}, {"y", spawnPos[1]}, {"z", spawnPos[2]}};
    else if (Nbt::at(data, {"SpawnX"}).isValid())
        spawn = {{"x", Nbt::at(data, {"SpawnX"})}, {"y", Nbt::at(data, {"SpawnY"})}, {"z", Nbt::at(data, {"SpawnZ"})}};

    return {{"version", Nbt::at(data, {"Version", "Name"}).toString()},
            {"dimensions", dimensions},
            {"regions", regions},
            {"player", player},
            {"spawn", spawn}};
}

QVariantList entities(const QString &worldPath, const QString &dimension, int limit)
{
    // Monsters drawn red; everything else alive (animals, villagers, golems) green.
    static const QSet<QString> hostile{
        "zombie", "zombie_villager", "husk", "drowned", "skeleton", "stray", "bogged", "wither_skeleton", "creeper",
        "spider", "cave_spider", "enderman", "endermite", "silverfish", "witch", "slime", "magma_cube", "phantom",
        "pillager", "vindicator", "evoker", "ravager", "vex", "illusioner", "blaze", "ghast", "piglin_brute", "hoglin",
        "zoglin", "zombified_piglin", "guardian", "elder_guardian", "shulker", "warden", "breeze", "creaking", "wither",
        "ender_dragon"};
    QVariantList found;
    const QDir folder(dimensionFolder(worldPath, dimension, QStringLiteral("entities")));
    for (const auto &info : folder.entryInfoList({"r.*.mca"}, QDir::Files)) {
        eachChunk(info.absoluteFilePath(), [&](int, const QVariant &chunk) {
            for (const auto &entity : Nbt::at(chunk, {"Entities"}).toList()) {
                const auto map = entity.toMap();
                const auto pos = map.value("Pos").toList();
                if (!map.contains("Health") || pos.size() != 3 || found.size() >= limit)
                    continue; // not alive: items, arrows, boats, frames
                const auto id = map.value("id").toString();
                found.append(QVariantMap{{"x", pos[0].toDouble()}, {"y", pos[1].toDouble()}, {"z", pos[2].toDouble()},
                                         {"yaw", map.value("Rotation").toList().value(0).toDouble()},
                                         {"id", id}, {"hostile", hostile.contains(id.section(':', -1))}});
            }
        });
    }
    return found;
}

} // namespace WorldMap
