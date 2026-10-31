#include "BlockModels.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace World3D {

namespace {

using namespace WorldData;

constexpr int CellsPerRow = BlockModels::AtlasSize / BlockModels::Cell;
constexpr int MaxBlocks = 65535;
constexpr float Pi = 3.14159265f;
constexpr WorldMap::BiomeColor plains{qRgb(145, 189, 89), qRgb(119, 171, 47), qRgb(63, 118, 228)};

struct Vec
{
    float x, y, z;
};

constexpr Vec directions[6] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};

Face faceOf(const Vec &v)
{
    for (int i = 0; i < 6; ++i)
        if (std::abs(directions[i].x - v.x) < 0.5f && std::abs(directions[i].y - v.y) < 0.5f && std::abs(directions[i].z - v.z) < 0.5f)
            return Face(i);
    return NoFace;
}

Face faceNamed(const QString &name)
{
    static const QHash<QString, Face> names{{"down", Down},   {"up", Up},     {"north", North}, {"south", South},
                                            {"west", West},   {"east", East}, {"bottom", Down}, {"top", Up}};
    return names.value(name, NoFace);
}

// A variant's turn: quarter turns about x (first), then y, around the block's centre.
Vec turn(Vec p, int x, int y, bool position)
{
    const float c = position ? 8 : 0;
    for (int i = 0; i < (x & 3); ++i)
        p = {p.x, (p.z - c) + c, -(p.y - c) + c};
    for (int i = 0; i < (y & 3); ++i)
        p = {-(p.z - c) + c, p.y, (p.x - c) + c};
    return p;
}

Face turnFace(Face face, int x, int y)
{
    if (face == NoFace)
        return face;
    return faceOf(turn(directions[face], x, y, false));
}

// What a block's tinted faces take: the biome's grass, foliage or water, or a colour of
// their own (spruce and birch leaves, lily pads, redstone).
Tint tintOf(const QString &name, QRgb *fixed)
{
    *fixed = 0;
    if (name.contains("water") || name.contains("bubble_column"))
        return WaterTint;
    if (name.contains("spruce_leaves"))
        *fixed = qRgb(97, 153, 97);
    else if (name.contains("birch_leaves"))
        *fixed = qRgb(128, 167, 85);
    else if (name.contains("lily_pad"))
        *fixed = qRgb(32, 128, 48);
    else if (name.contains("redstone_wire"))
        *fixed = qRgb(150, 20, 10);
    else if (name.contains("stem") && !name.contains("mushroom"))
        *fixed = qRgb(150, 180, 60);
    if (*fixed)
        return FixedTint;
    if (name.contains("leaves") || name.contains("vine"))
        return FoliageTint;
    return GrassTint;
}

QString valueText(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble())
        return QString::number(value.toInteger());
    return value.toString();
}

// 26.x saves leave out the properties at their default ("minecraft:grass_block" for
// snowy=false); the usual defaults, taken for a property the state doesn't name.
bool likelyDefault(const QString &value)
{
    static const QSet<QString> defaults{"false", "none", "north", "y", "bottom", "lower", "straight", "floor",
                                        "foot", "single", "0", "1"};
    return defaults.contains(value);
}

// How well "facing=east,half=bottom" matches the state: -1 not at all, else the number of
// pairs a property left out matches by its likely default ("" matches any).
int variantMatches(const QString &key, const QVariantMap &properties)
{
    if (key.isEmpty() || key == "normal")
        return 0;
    int defaults = 0;
    for (const auto &pair : key.split(',')) {
        const auto equals = pair.indexOf('=');
        if (equals < 0)
            return -1;
        const auto name = pair.left(equals), value = pair.mid(equals + 1);
        if (!properties.contains(name))
            defaults += likelyDefault(value);
        else if (properties.value(name).toString() != value)
            return -1;
    }
    return defaults;
}

bool whenMatches(const QJsonObject &when, const QVariantMap &properties)
{
    if (when.contains("OR")) {
        for (const auto &part : when.value("OR").toArray())
            if (whenMatches(part.toObject(), properties))
                return true;
        return false;
    }
    if (when.contains("AND")) {
        for (const auto &part : when.value("AND").toArray())
            if (!whenMatches(part.toObject(), properties))
                return false;
        return true;
    }
    for (auto it = when.begin(); it != when.end(); ++it) {
        const auto allowed = valueText(it.value()).split('|');
        if (!properties.contains(it.key()) ? std::none_of(allowed.begin(), allowed.end(), likelyDefault)
                                            : !allowed.contains(properties.value(it.key()).toString()))
            return false;
    }
    return true;
}

QJsonObject firstOf(const QJsonValue &value)
{
    return (value.isArray() ? value.toArray().first() : value).toObject();
}

} // namespace

BlockModels::BlockModels(const QString &dataDirectory, const QString &version, const QString &instanceDirectory)
{
    m_blocks.reserve(MaxBlocks + 1);
    const auto client = clientJar(dataDirectory, version);
    if (!client.isEmpty()) {
        m_client = std::make_unique<Jar>(client);
        for (const auto &name : m_client->names())
            if (name.startsWith("assets/"))
                m_index.insert(name, m_client.get());
    }
    // Mods add their own assets; the client's stay as they are.
    const QDir mods(QDir(instanceDirectory).filePath("mods"));
    for (const auto &info : mods.entryInfoList({"*.jar"}, QDir::Files)) {
        auto jar = std::make_unique<Jar>(info.absoluteFilePath());
        for (const auto &name : jar->names())
            if (name.startsWith("assets/") && !m_index.contains(name))
                m_index.insert(name, jar.get());
        m_mods.push_back(std::move(jar));
    }
    m_biomes = WorldMap::palette(dataDirectory, version, instanceDirectory).biomes;

    // Cell 0: white, for what has no texture (the markers); cell 1: grey, for a texture
    // that can't be found.
    QImage white(16, 16, QImage::Format_RGBA8888);
    white.fill(Qt::white);
    addCell(white);
    QImage grey(16, 16, QImage::Format_RGBA8888);
    grey.fill(QColor(140, 140, 140));
    addCell(grey);

    Block air;
    air.air = true;
    m_blocks.push_back(air);
    m_ids.insert(QStringLiteral("minecraft:air"), 0);
    makeFluids();
}

BlockModels::~BlockModels() = default;

QByteArray BlockModels::read(const QString &path) const
{
    auto *jar = m_index.value(path);
    return jar ? jar->read(path) : QByteArray();
}

WorldMap::BiomeColor BlockModels::biome(const QString &id) const
{
    return m_biomes.value(id, plains);
}

void BlockModels::cellOrigin(int cell, int *x, int *y)
{
    *x = (cell % CellsPerRow) * Cell;
    *y = (cell / CellsPerRow) * Cell;
}

QList<QImage> BlockModels::cellsSince(int revision, int *now) const
{
    QMutexLocker lock(&m_mutex);
    *now = int(m_cells.size());
    return revision >= m_cells.size() ? QList<QImage>() : m_cells.mid(revision);
}

int BlockModels::addCell(const QImage &texture)
{
    if (m_cells.size() >= CellsPerRow * CellsPerRow)
        return 1;
    // Fully clear pixels take the average colour, so mipmaps don't darken cut-out edges.
    auto image = texture.convertToFormat(QImage::Format_RGBA8888);
    long r = 0, g = 0, b = 0, n = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const auto *p = image.constScanLine(y) + x * 4;
            if (p[3] > 0) {
                r += p[0], g += p[1], b += p[2], ++n;
            }
        }
    if (n > 0 && n < 256)
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                auto *p = image.scanLine(y) + x * 4;
                if (p[3] == 0)
                    p[0] = uchar(r / n), p[1] = uchar(g / n), p[2] = uchar(b / n);
            }
    QImage cell(Cell, Cell, QImage::Format_RGBA8888);
    for (int y = 0; y < Cell; ++y)
        for (int x = 0; x < Cell; ++x)
            std::memcpy(cell.scanLine(y) + x * 4, image.constScanLine((y - 8) & 15) + ((x - 8) & 15) * 4, 4);
    m_cells.append(cell);
    return int(m_cells.size()) - 1;
}

BlockModels::Sprite BlockModels::sprite(const QString &texture)
{
    const auto known = m_sprites.constFind(texture);
    if (known != m_sprites.cend())
        return *known;
    Sprite sprite;
    auto image = QImage::fromData(read(assetPath(texture, "textures", ".png")));
    if (!image.isNull()) {
        const int side = qMin(image.width(), image.height()); // animated: a vertical strip
        image = image.copy(0, 0, side, side).convertToFormat(QImage::Format_RGBA8888);
        if (side != 16)
            image = image.scaled(16, 16, Qt::IgnoreAspectRatio, side > 16 ? Qt::SmoothTransformation : Qt::FastTransformation);
        int clear = 0, partial = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const auto a = image.constScanLine(y)[x * 4 + 3];
                clear += a < 128;
                partial += a > 0 && a < 250;
            }
        sprite.layer = partial > 24 ? Translucent : clear > 0 ? Cutout : Solid;
        sprite.cell = addCell(image);
    }
    m_sprites.insert(texture, sprite);
    return sprite;
}

const BlockModels::Model &BlockModels::model(const QString &ref)
{
    const auto known = m_models.constFind(ref);
    if (known != m_models.cend())
        return *known;
    Model model;
    QString next = ref;
    for (int depth = 0; depth < 16 && !next.isEmpty(); ++depth) {
        const auto json = QJsonDocument::fromJson(read(assetPath(next, "models", ".json"))).object();
        if (json.isEmpty())
            break;
        const auto textures = json.value("textures").toObject();
        for (auto it = textures.begin(); it != textures.end(); ++it)
            if (!model.textures.contains(it.key()))
                model.textures.insert(it.key(), it.value().isObject() ? it.value().toObject().value("sprite").toString()
                                                                      : it.value().toString());
        if (model.elements.isEmpty())
            model.elements = json.value("elements").toArray();
        next = json.value("parent").toString();
    }
    return *m_models.insert(ref, model);
}

void BlockModels::bakeElement(const QJsonObject &element, const Model &model, int rotateX, int rotateY, Block &block,
                              bool *fullCube)
{
    const auto from = element.value("from").toArray(), to = element.value("to").toArray();
    if (from.size() != 3 || to.size() != 3)
        return;
    const Vec a{float(from[0].toDouble()), float(from[1].toDouble()), float(from[2].toDouble())};
    const Vec b{float(to[0].toDouble()), float(to[1].toDouble()), float(to[2].toDouble())};
    const auto rotation = element.value("rotation").toObject();
    const float angle = float(rotation.value("angle").toDouble()) * Pi / 180.f;
    const auto axis = rotation.value("axis").toString();
    const auto origin = rotation.value("origin").toArray();
    const Vec pivot{float(origin.at(0).toDouble(8)), float(origin.at(1).toDouble(8)), float(origin.at(2).toDouble(8))};
    const float rescale = rotation.value("rescale").toBool() && std::abs(std::cos(angle)) > 0.01f ? 1.f / std::cos(angle) : 1.f;
    const bool shade = element.value("shade").toBool(true);
    *fullCube = angle == 0 && a.x == 0 && a.y == 0 && a.z == 0 && b.x == 16 && b.y == 16 && b.z == 16;

    const auto resolve = [&model](QString texture) {
        for (int i = 0; i < 8 && texture.startsWith('#'); ++i)
            texture = model.textures.value(texture.mid(1));
        return texture.startsWith('#') ? QString() : texture;
    };

    const auto faces = element.value("faces").toObject();
    for (auto it = faces.begin(); it != faces.end(); ++it) {
        const auto face = faceNamed(it.key());
        if (face == NoFace)
            continue;
        const auto spec = it.value().toObject();
        // Corners from the face's top left, as its texture lies seen from outside.
        Vec corners[4];
        float uv[4];
        switch (face) {
        case Down: corners[0] = {a.x, a.y, b.z}, corners[1] = {a.x, a.y, a.z}, corners[2] = {b.x, a.y, a.z}, corners[3] = {b.x, a.y, b.z};
            uv[0] = a.x, uv[1] = 16 - b.z, uv[2] = b.x, uv[3] = 16 - a.z; break;
        case Up: corners[0] = {a.x, b.y, a.z}, corners[1] = {a.x, b.y, b.z}, corners[2] = {b.x, b.y, b.z}, corners[3] = {b.x, b.y, a.z};
            uv[0] = a.x, uv[1] = a.z, uv[2] = b.x, uv[3] = b.z; break;
        case North: corners[0] = {b.x, b.y, a.z}, corners[1] = {b.x, a.y, a.z}, corners[2] = {a.x, a.y, a.z}, corners[3] = {a.x, b.y, a.z};
            uv[0] = 16 - b.x, uv[1] = 16 - b.y, uv[2] = 16 - a.x, uv[3] = 16 - a.y; break;
        case South: corners[0] = {a.x, b.y, b.z}, corners[1] = {a.x, a.y, b.z}, corners[2] = {b.x, a.y, b.z}, corners[3] = {b.x, b.y, b.z};
            uv[0] = a.x, uv[1] = 16 - b.y, uv[2] = b.x, uv[3] = 16 - a.y; break;
        case West: corners[0] = {a.x, b.y, a.z}, corners[1] = {a.x, a.y, a.z}, corners[2] = {a.x, a.y, b.z}, corners[3] = {a.x, b.y, b.z};
            uv[0] = a.z, uv[1] = 16 - b.y, uv[2] = b.z, uv[3] = 16 - a.y; break;
        default: corners[0] = {b.x, b.y, b.z}, corners[1] = {b.x, a.y, b.z}, corners[2] = {b.x, a.y, a.z}, corners[3] = {b.x, b.y, a.z};
            uv[0] = 16 - b.z, uv[1] = 16 - b.y, uv[2] = 16 - a.z, uv[3] = 16 - a.y; break;
        }
        const auto given = spec.value("uv").toArray();
        if (given.size() == 4)
            for (int i = 0; i < 4; ++i)
                uv[i] = float(given[i].toDouble());

        const auto texture = sprite(resolve(spec.value("texture").toString()));
        int cellX, cellY;
        cellOrigin(texture.cell, &cellX, &cellY);
        const float cornerUv[4][2] = {{uv[0], uv[1]}, {uv[0], uv[3]}, {uv[2], uv[3]}, {uv[2], uv[1]}};
        const int uvTurn = (spec.value("rotation").toInt() / 90) & 3;

        Quad quad;
        for (int i = 0; i < 4; ++i) {
            Vec p = corners[i];
            if (angle != 0) {
                p = {p.x - pivot.x, p.y - pivot.y, p.z - pivot.z};
                const float c = std::cos(angle), s = std::sin(angle);
                if (axis == "x")
                    p = {p.x, (p.y * c - p.z * s) * rescale, (p.y * s + p.z * c) * rescale};
                else if (axis == "y")
                    p = {(p.x * c + p.z * s) * rescale, p.y, (-p.x * s + p.z * c) * rescale};
                else
                    p = {(p.x * c - p.y * s) * rescale, (p.x * s + p.y * c) * rescale, p.z};
                p = {p.x + pivot.x, p.y + pivot.y, p.z + pivot.z};
            }
            p = turn(p, rotateX, rotateY, true);
            quad.pos[i][0] = p.x / 16, quad.pos[i][1] = p.y / 16, quad.pos[i][2] = p.z / 16;
            const auto *t = cornerUv[(i + uvTurn) & 3];
            quad.uv[i][0] = (cellX + 8 + t[0]) / AtlasSize;
            quad.uv[i][1] = (cellY + 8 + t[1]) / AtlasSize;
        }
        quad.cull = turnFace(faceNamed(spec.value("cullface").toString()), rotateX, rotateY);
        quad.light = shade ? turnFace(face, rotateX, rotateY) : NoFace;
        quad.layer = texture.layer;
        quad.tinted = spec.contains("tintindex");
        if (texture.layer != Solid || !spec.contains("cullface"))
            *fullCube = false;
        block.quads.push_back(quad);
    }
    if (faces.size() < 6)
        *fullCube = false;
}

Block BlockModels::bake(const QString &name, const QVariantMap &properties)
{
    Block block;
    const auto shortName = name.section(':', 1);
    if (shortName == "air" || shortName == "cave_air" || shortName == "void_air") {
        block.air = true;
        return block;
    }
    static const QSet<QString> underwater{"bubble_column", "kelp", "kelp_plant", "seagrass", "tall_seagrass"};
    block.water = name == "minecraft:water" || underwater.contains(shortName) || properties.value("waterlogged").toString() == "true";
    block.lava = name == "minecraft:lava";
    if (name == "minecraft:water" || block.lava || shortName == "bubble_column") {
        block.air = !block.water && !block.lava;
        return block;
    }
    block.tint = tintOf(name, &block.fixedTint);

    auto states = m_states.value(name);
    if (!m_states.contains(name)) {
        states = QJsonDocument::fromJson(read(assetPath(name, "blockstates", ".json").replace("/block/", "/"))).object();
        m_states.insert(name, states);
    }
    // Mods that stand in for a vanilla block make its blockstate at runtime, named after
    // it ("visualworkbench:minecraft/crafting_table"): the block it stands in for.
    if (states.isEmpty() && shortName.count('/') == 1) {
        const auto standIn = shortName.section('/', 0, 0) + ':' + shortName.section('/', 1);
        if (standIn != name)
            return bake(standIn, properties);
    }
    QList<QJsonObject> parts;
    const auto variants = states.value("variants").toObject();
    int best = -1;
    QJsonValue chosen;
    for (auto it = variants.begin(); it != variants.end(); ++it) {
        const int score = variantMatches(it.key(), properties);
        if (score > best)
            best = score, chosen = it.value();
    }
    if (best >= 0)
        parts.append(firstOf(chosen));
    for (const auto &part : states.value("multipart").toArray()) {
        const auto object = part.toObject();
        if (!object.contains("when") || whenMatches(object.value("when").toObject(), properties))
            parts.append(firstOf(object.value("apply")));
    }

    bool full = false;
    for (const auto &part : parts) {
        const auto &baked = model(part.value("model").toString());
        const int x = part.value("x").toInt() / 90, y = part.value("y").toInt() / 90;
        for (const auto &element : baked.elements) {
            bool cube = false;
            bakeElement(element.toObject(), baked, x, y, block, &cube);
            full = full || cube;
        }
        // Drawn by the game as an entity (chests, beds, shulker boxes): a box of its
        // particle texture, so they are still there.
        if (baked.elements.isEmpty() && !baked.textures.value("particle").isEmpty()
            && (shortName.contains("chest") || shortName.contains("shulker_box") || shortName.endsWith("_bed"))) {
            const bool bed = shortName.endsWith("_bed");
            Model box;
            box.textures.insert("all", baked.textures.value("particle"));
            QJsonObject faces;
            for (const auto *side : {"down", "up", "north", "south", "west", "east"})
                faces.insert(QLatin1StringView(side), QJsonObject{{"texture", "#all"}});
            const QJsonObject element{{"from", QJsonArray{bed ? 0 : 1, 0, bed ? 0 : 1}},
                                      {"to", QJsonArray{bed ? 16 : 15, bed ? 9 : 14, bed ? 16 : 15}},
                                      {"faces", faces}};
            bool cube = false;
            bakeElement(element, box, 0, 0, block, &cube);
        }
    }
    // Opaque: one of its elements is a whole cube of solid faces, each culled by its side.
    block.opaque = full;
    return block;
}

quint16 BlockModels::id(const QString &name, const QVariantMap &properties)
{
    QString key = name;
    if (!properties.isEmpty()) {
        key += '[';
        for (auto it = properties.cbegin(); it != properties.cend(); ++it)
            key += it.key() + '=' + it.value().toString() + ',';
        key[key.size() - 1] = ']';
    }
    QMutexLocker lock(&m_mutex);
    const auto known = m_ids.constFind(key);
    if (known != m_ids.cend())
        return *known;
    if (m_blocks.size() >= MaxBlocks)
        return 0;
    m_blocks.push_back(bake(name, properties));
    const auto id = quint16(m_blocks.size() - 1);
    m_ids.insert(key, id);
    return id;
}

void BlockModels::makeFluids()
{
    for (int i = 0; i < 4; ++i) {
        const bool lava = i >= 2, surface = i & 1;
        Model model;
        model.textures.insert("all", lava ? "minecraft:block/lava_still" : "minecraft:block/water_still");
        QJsonObject faces;
        for (const auto *side : {"down", "up", "north", "south", "west", "east"})
            faces.insert(QLatin1StringView(side), QJsonObject{{"texture", "#all"}, {"cullface", side}, {"tintindex", 0}});
        const QJsonObject element{{"from", QJsonArray{0, 0, 0}}, {"to", QJsonArray{16, surface ? 14 : 16, 16}}, {"faces", faces}};
        bool cube = false;
        bakeElement(element, model, 0, 0, m_fluids[i], &cube);
        m_fluids[i].tint = lava ? NoTint : WaterTint;
        m_fluids[i].water = !lava;
        m_fluids[i].lava = lava;
        for (auto &quad : m_fluids[i].quads) {
            quad.tinted = !lava;
            quad.layer = lava ? Solid : Translucent;
        }
    }
}

} // namespace World3D
