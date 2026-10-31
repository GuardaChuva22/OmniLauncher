#include "Wrapped.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPainter>
#include <QPolygonF>
#include <QProcess>
#include <QStandardPaths>
#include <QTransform>
#include <QUrl>
#include <QtMath>

#include <algorithm>
#include <cmath>

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

// Inventory icons, drawn the way the game draws them: a block model's cuboids through its
// "gui" display transform, an item's layers stacked. Icons are 128 px, textures sampled
// nearest so the pixels stay sharp.
constexpr int IconSize = 128;

QJsonObject asset(const QString &dir, const QString &folder, const QString &ref)
{
    return readJson(QDir(dir).filePath("assets/minecraft/" + folder + '/' + ref.section(':', -1) + ".json"));
}

// One frame of a texture: animated ones are a vertical strip of frames.
QImage texture(const QString &dir, const QString &ref)
{
    QImage image(QDir(dir).filePath("assets/minecraft/textures/" + ref + ".png"));
    if (image.isNull())
        return image;
    image = image.convertToFormat(QImage::Format_ARGB32);
    return image.height() > image.width() ? image.copy(0, 0, image.width(), image.width()) : image;
}

QImage shaded(QImage image, double shade, QRgb tint)
{
    const auto channel = [shade](int value, int by) { return qBound(0, int(value * shade * by / 255.0 + 0.5), 255); };
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            line[x] = qRgba(channel(qRed(line[x]), qRed(tint)), channel(qGreen(line[x]), qGreen(tint)),
                            channel(qBlue(line[x]), qBlue(tint)), qAlpha(line[x]));
    }
    return image;
}

// The colour the game multiplies a tinted face or layer by: the item definition's
// (1.21.4+), otherwise the usual biome colour for leaves and plants.
QRgb tintColor(const QJsonArray &tints, const QString &n, int index)
{
    const auto spec = tints.at(index).toObject();
    if (spec.value("type").toString().section(':', -1) == "constant")
        return QRgb(spec.value("value").toInteger()) & 0xFFFFFF;
    if (spec.contains("default"))
        return QRgb(spec.value("default").toInteger()) & 0xFFFFFF;
    if (n.startsWith("leather_"))
        return 0xA06540;
    if (n.contains("birch_leaves"))
        return 0x80A755;
    if (n.contains("spruce_leaves"))
        return 0x619961;
    if (n.contains("leaves") || n.contains("vine"))
        return 0x48B518;
    if (n == "lily_pad")
        return 0x208030;
    if (n.contains("water"))
        return 0x3F76E4;
    return 0x91BD59; // grass
}

// Items whose first layer the game tints before item definitions said so.
bool legacyTinted(const QString &n)
{
    static const QStringList plants{"grass", "short_grass", "tall_grass", "fern", "large_fern", "vine", "lily_pad"};
    return n.startsWith("leather_") || plants.contains(n);
}

struct Model
{
    QJsonObject textures; // merged down the parent chain, the child's first
    QJsonArray elements;
    QJsonObject gui;
    bool generated = false; // a flat item: layer0, layer1…
};

Model loadModel(const QString &dir, QString ref)
{
    Model model;
    for (int depth = 0; depth < 16 && !ref.isEmpty(); ++depth) {
        ref = ref.section(':', -1);
        if (!ref.contains('/'))
            ref.prepend("block/"); // blockstates before 1.13
        if (ref == "builtin/generated") {
            model.generated = true;
            break;
        }
        const auto json = asset(dir, "models", ref);
        if (json.isEmpty())
            break;
        const auto textures = json.value("textures").toObject();
        for (auto it = textures.begin(); it != textures.end(); ++it)
            if (!model.textures.contains(it.key()))
                model.textures.insert(it.key(), it.value().isObject() ? it.value().toObject().value("sprite") : it.value());
        if (model.elements.isEmpty())
            model.elements = json.value("elements").toArray();
        if (model.gui.isEmpty())
            model.gui = json.value("display").toObject().value("gui").toObject();
        ref = json.value("parent").toString();
    }
    return model;
}

QString textureRef(const QJsonObject &textures, QString ref)
{
    for (int i = 0; i < 8 && ref.startsWith('#'); ++i)
        ref = textures.value(ref.mid(1)).toString();
    return ref.startsWith('#') ? QString() : ref.section(':', -1);
}

// The first plain model an item definition (items/<n>.json, 1.21.4+) can show; none for
// the ones drawn by code (chests, heads, shields).
QJsonObject plainModel(const QJsonObject &node)
{
    if (node.value("model").isString())
        return node;
    if (node.value("type").toString().endsWith("special"))
        return {};
    QJsonArray next;
    for (const auto key : {"model", "fallback", "on_false", "on_true"})
        if (node.value(key).isObject())
            next.append(node.value(key));
    for (const auto key : {"cases", "entries"})
        for (const auto &entry : node.value(key).toArray())
            next.append(entry.toObject().value("model"));
    for (const auto &model : node.value("models").toArray())
        next.append(model);
    for (const auto &candidate : next) {
        const auto found = plainModel(candidate.toObject());
        if (!found.isEmpty())
            return found;
    }
    return {};
}

QString blockstateModel(const QJsonObject &states)
{
    const auto model = [](const QJsonValue &value) {
        return (value.isArray() ? value.toArray().first() : value).toObject().value("model").toString();
    };
    const auto variants = states.value("variants").toObject();
    if (!variants.isEmpty())
        return model(*variants.begin());
    const auto parts = states.value("multipart").toArray();
    return parts.isEmpty() ? QString() : model(parts.first().toObject().value("apply"));
}

struct Icon
{
    QString name;
    Model model;
    QJsonArray tints;
    QStringList textures; // layers of a flat item, or every texture the faces use
};

// The item a block with none of its own drops or is placed from.
QString blockItem(const QString &n)
{
    static const QHash<QString, QString> items{
        {"redstone_wire", "redstone"}, {"carrots", "carrot"}, {"potatoes", "potato"}, {"beetroots", "beetroot"},
        {"cocoa", "cocoa_beans"}, {"sweet_berry_bush", "sweet_berries"}, {"tripwire", "string"},
        {"melon_stem", "melon_seeds"}, {"pumpkin_stem", "pumpkin_seeds"}, {"cave_vines", "glow_berries"},
        {"cave_vines_plant", "glow_berries"}, {"bamboo_sapling", "bamboo"}, {"kelp_plant", "kelp"},
        {"tall_seagrass", "seagrass"}, {"water", "water_bucket"}, {"lava", "lava_bucket"}};
    return items.value(n, QString(n).replace("wall_", "")); // oak_wall_sign → oak_sign, wall_torch → torch
}

// What the inventory shows for an item: its definition (1.21.4+) or its item model; for a
// block with no item (crops, wall torches), the item it comes from, then its first
// blockstate variant.
Icon planIcon(const QString &dir, const QString &n)
{
    Icon icon{n, {}, {}, {}};
    QList<QPair<QString, QJsonArray>> refs;
    for (const auto &item : QStringList{n, blockItem(n)}) {
        const auto definition = plainModel(asset(dir, "items", item));
        if (!definition.isEmpty())
            refs.append({definition.value("model").toString(), definition.value("tints").toArray()});
        refs.append({"item/" + item, {}});
    }
    refs.append({blockstateModel(asset(dir, "blockstates", n)), {}});
    for (const auto &[ref, tints] : refs) {
        if (ref.isEmpty())
            continue;
        const auto model = loadModel(dir, ref);
        if (!model.elements.isEmpty()) {
            for (const auto &element : model.elements) {
                const auto faces = element.toObject().value("faces").toObject();
                for (const auto &face : faces) {
                    const auto texture = textureRef(model.textures, face.toObject().value("texture").toString());
                    if (!texture.isEmpty() && !icon.textures.contains(texture))
                        icon.textures.append(texture);
                }
            }
        } else if (model.generated) {
            for (int i = 0; i < 8 && model.textures.contains(QStringLiteral("layer%1").arg(i)); ++i)
                icon.textures.append(textureRef(model.textures, model.textures.value(QStringLiteral("layer%1").arg(i)).toString()));
        }
        if (!icon.textures.isEmpty()) {
            icon.model = model;
            icon.tints = tints;
            break;
        }
    }
    return icon;
}

QImage drawLayers(const QList<QImage> &layers)
{
    QImage canvas(IconSize, IconSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    for (const auto &layer : layers)
        painter.drawImage(canvas.rect(), layer);
    return canvas;
}

struct Vec
{
    double x, y, z;
};

Vec turn(const Vec &v, QChar axis, double degrees)
{
    const auto radians = qDegreesToRadians(degrees);
    const auto c = std::cos(radians), s = std::sin(radians);
    if (axis == 'x')
        return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
    if (axis == 'y')
        return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
    return {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

Vec vec(const QJsonValue &value, double fallback)
{
    const auto a = value.toArray();
    return {a.at(0).toDouble(fallback), a.at(1).toDouble(fallback), a.at(2).toDouble(fallback)};
}

// The cuboids seen through the gui transform (model units, 16 to a block, centred) and
// `yaw` degrees more about the vertical, each face's texture mapped onto its
// parallelogram, painted back to front `supersample` times larger and scaled down.
QImage drawModel(const QString &dir, const Icon &icon, double yaw = 0, int size = IconSize, int supersample = 4)
{
    const auto &gui = icon.model.gui;
    const auto spin = gui.isEmpty() ? Vec{30, 225, 0} : vec(gui.value("rotation"), 0);
    const auto shift = vec(gui.value("translation"), 0);
    const auto scale = gui.isEmpty() ? Vec{0.625, 0.625, 0.625} : vec(gui.value("scale"), 1);
    const auto orient = [&](Vec v) { return turn(turn(turn(v, 'z', spin.z), 'y', spin.y + yaw), 'x', spin.x); };

    // Corners as bits (x, y, z: 0 from, 1 to), counter-clockwise from the texture's top left.
    struct Side { const char *name; Vec normal; int corners[4]; };
    static const Side sides[] = {{"north", {0, 0, -1}, {3, 1, 0, 2}}, {"south", {0, 0, 1}, {6, 4, 5, 7}},
                                 {"west", {-1, 0, 0}, {2, 0, 4, 6}},  {"east", {1, 0, 0}, {7, 5, 1, 3}},
                                 {"up", {0, 1, 0}, {2, 6, 7, 3}},     {"down", {0, -1, 0}, {4, 0, 1, 5}}};
    struct Face { QPolygonF screen, source; double depth; QImage image; };
    QList<Face> faces;
    QHash<QString, QImage> textures;
    const double canvas = size * supersample, unit = canvas / 16;

    for (const auto &value : icon.model.elements) {
        const auto element = value.toObject();
        const auto from = vec(element.value("from"), 0), to = vec(element.value("to"), 16);
        const auto rotation = element.value("rotation").toObject();
        const auto origin = vec(rotation.value("origin"), 8);
        QList<QPair<QChar, double>> turns;
        if (rotation.contains("axis"))
            turns.append({rotation.value("axis").toString().at(0), rotation.value("angle").toDouble()});
        else
            for (const auto axis : {QChar('x'), QChar('y'), QChar('z')})
                if (rotation.contains(QString(axis)))
                    turns.append({axis, rotation.value(QString(axis)).toDouble()});
        const auto rotate = [&](Vec v, bool normal) {
            if (!normal)
                v = {v.x - origin.x, v.y - origin.y, v.z - origin.z};
            for (const auto &[axis, angle] : turns) {
                v = turn(v, axis, angle);
                const auto c = std::cos(qDegreesToRadians(angle));
                if (!normal && rotation.value("rescale").toBool() && std::abs(c) > 0.01) {
                    v.x /= axis == 'x' ? 1 : c;
                    v.y /= axis == 'y' ? 1 : c;
                    v.z /= axis == 'z' ? 1 : c;
                }
            }
            return normal ? v : Vec{v.x + origin.x, v.y + origin.y, v.z + origin.z};
        };
        const auto elementFaces = element.value("faces").toObject();
        for (const auto &side : sides) {
            const auto face = elementFaces.value(side.name).toObject();
            if (face.isEmpty())
                continue;
            const auto normal = orient(rotate(side.normal, true));
            if (normal.z <= 1e-3)
                continue; // facing away
            const auto ref = textureRef(icon.model.textures, face.value("texture").toString());
            if (!textures.contains(ref))
                textures.insert(ref, texture(dir, ref));
            const auto image = textures.value(ref);
            if (image.isNull())
                return {};

            const double defaults[6][4] = {{16 - to.x, 16 - to.y, 16 - from.x, 16 - from.y}, {from.x, 16 - to.y, to.x, 16 - from.y},
                                           {from.z, 16 - to.y, to.z, 16 - from.y},          {16 - to.z, 16 - to.y, 16 - from.z, 16 - from.y},
                                           {from.x, from.z, to.x, to.z},                    {from.x, 16 - to.z, to.x, 16 - from.z}};
            const auto *uv = defaults[&side - sides];
            const auto given = face.value("uv").toArray();
            const double custom[4] = {given.at(0).toDouble(), given.at(1).toDouble(), given.at(2).toDouble(), given.at(3).toDouble()};
            if (given.size() == 4)
                uv = custom;
            const auto px = image.width() / 16.0;
            const QPointF uvCorners[4] = {{uv[0] * px, uv[1] * px}, {uv[0] * px, uv[3] * px}, {uv[2] * px, uv[3] * px}, {uv[2] * px, uv[1] * px}};
            const auto quarter = (face.value("rotation").toInt() / 90 % 4 + 4) % 4;

            Face drawn{{}, {}, 0, {}};
            for (int i = 0; i < 4; ++i) {
                const auto bits = side.corners[i];
                const auto corner = rotate({bits & 1 ? to.x : from.x, bits & 2 ? to.y : from.y, bits & 4 ? to.z : from.z}, false);
                auto v = orient({(corner.x - 8) * scale.x, (corner.y - 8) * scale.y, (corner.z - 8) * scale.z});
                v = {v.x + shift.x, v.y + shift.y, v.z + shift.z};
                drawn.screen << QPointF(canvas / 2 + v.x * unit, canvas / 2 - v.y * unit);
                drawn.source << uvCorners[(i + quarter) % 4];
                drawn.depth += v.z / 4;
            }
            // Lit from the upper left, as in the inventory: top brightest, right side darkest.
            const auto shade = qBound(0.4, 0.787 + 0.246 * normal.y - 0.1414 * normal.x, 1.0);
            drawn.image = shaded(image, shade, face.contains("tintindex") ? tintColor(icon.tints, icon.name, face.value("tintindex").toInt()) : 0xFFFFFF);
            faces.append(drawn);
        }
    }
    if (faces.isEmpty())
        return {};
    std::stable_sort(faces.begin(), faces.end(), [](const Face &a, const Face &b) { return a.depth < b.depth - 1e-6; });

    QImage image(int(canvas), int(canvas), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    for (const auto &face : faces) {
        QTransform transform;
        if (!QTransform::quadToQuad(face.source, face.screen, transform))
            continue; // a zero-width uv
        QBrush brush(face.image);
        brush.setTransform(transform);
        painter.setBrush(brush);
        painter.drawPolygon(face.screen);
    }
    painter.end();
    return image.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// A block model turning about its vertical axis: SpinFrames frames, SpinColumns to a row,
// for the Retrospectiva to step through.
constexpr int SpinFrames = 90, SpinColumns = 10, SpinSize = 96;

QImage drawSpin(const QString &dir, const Icon &icon)
{
    QImage sheet(SpinSize * SpinColumns, SpinSize * ((SpinFrames + SpinColumns - 1) / SpinColumns), QImage::Format_ARGB32_Premultiplied);
    sheet.fill(Qt::transparent);
    QPainter painter(&sheet);
    for (int i = 0; i < SpinFrames; ++i) {
        const auto frame = drawModel(dir, icon, 360.0 * i / SpinFrames, SpinSize, 3);
        if (frame.isNull())
            return {};
        painter.drawImage((i % SpinColumns) * SpinSize, (i / SpinColumns) * SpinSize, frame);
    }
    painter.end();
    return sheet;
}

QString spinFile(const QString &icon) // icons/stone.png → icons/stone.spin.png
{
    return icon.chopped(4) + ".spin.png";
}

QImage drawIcon(const QString &dir, const Icon &icon)
{
    if (!icon.model.elements.isEmpty())
        return drawModel(dir, icon);
    QList<QImage> layers;
    for (int i = 0; i < icon.textures.size(); ++i) {
        auto image = texture(dir, icon.textures[i]);
        if (image.isNull())
            return {};
        if (i < icon.tints.size() || (icon.tints.isEmpty() && i == 0 && legacyTinted(icon.name)))
            image = shaded(image, 1, tintColor(icon.tints, icon.name, i));
        layers.append(image);
    }
    return layers.isEmpty() ? QImage() : drawLayers(layers);
}
} // namespace

Wrapped::Wrapped(const QString &dataDirectory, QObject *parent)
    : QObject(parent)
    , m_root(dataDirectory)
{
    m_pool.setMaxThreadCount(1);
}

void Wrapped::request(const QVariantList &games, const QVariantMap &gameSettings, const QString &gameId)
{
    const auto serial = ++m_serial;
    m_pool.start([this, games, gameSettings, gameId, serial] {
        const auto info = build(games, gameSettings, gameId);
        QMetaObject::invokeMethod(this, [this, info, serial] {
            if (serial == m_serial)
                emit built(info);
        }, Qt::QueuedConnection);
    });
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
// mined, topMined [{id, name, count, icon, spin}], mobKills, topKilled, deaths, topKilledBy,
// crafted, topCrafted, used, topUsed, extras {jumps, fish, bred, trades, sleeps,
// enchants, chests, hearts, playerKills, raids, cakes}, topWorld {name, game, seconds},
// topGame {title, seconds}. spin {source, frames, columns}: only on icons drawn from a 3D
// model, a sheet of it turning.
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

    // Icons: drawn once per version into cache/wrapped/<version>/icons/ from what the client
    // jar holds (models, blockstates and item definitions taken out whole, textures as
    // needed): blocks as the inventory shows them, items from their layers, mobs as their
    // spawn egg. Anything without a model falls back to a texture of the same name.
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
    const auto cache = [this](const QString &version) { return QDir(m_root).filePath("cache/wrapped/" + version); };
    const auto iconFile = [&](const QString &version, const QString &id, const QString &kind) {
        return QDir(cache(version)).filePath(QStringLiteral("icons/") + (kind == "entity" ? "entity/" : "") + bare(id) + ".png");
    };
    const auto findIcon = [&](const QString &id, const QString &kind) {
        for (const auto &version : versions)
            if (QFileInfo::exists(iconFile(version, id, kind)))
                return iconFile(version, id, kind);
        return QString();
    };
    QList<QPair<QVariantList *, QString>> lists{{&topMined, "block"}, {&topKilled, "entity"}, {&topKilledBy, "entity"},
                                                {&topCrafted, "item"}, {&topUsed, "item"}};
    // Only entries with no icon in any cache yet send tar into the jars, newest guess first.
    QList<QPair<QString, QString>> unresolved;
    for (const auto &[list, kind] : lists)
        for (const auto &entry : *list) {
            const auto id = entry.toMap().value("id").toString();
            if (!candidates(id, kind).isEmpty() && findIcon(id, kind).isEmpty())
                unresolved.append({id, kind});
        }
    for (const auto &version : versions) {
        if (unresolved.isEmpty())
            break;
        const auto dir = cache(version);
        const auto marker = QDir(dir).filePath(".models");
        if (!QFileInfo::exists(marker)) {
            extract(jar(version), dir, {"assets/minecraft/models", "assets/minecraft/blockstates", "assets/minecraft/items"});
            QFile(marker).open(QIODevice::WriteOnly);
        }
        QList<Icon> plans;
        QStringList members;
        for (const auto &[id, kind] : unresolved) {
            plans.append(kind == "entity" ? Icon{bare(id), {}, {}, {}} : planIcon(dir, bare(id)));
            for (const auto &ref : plans.last().textures + candidates(id, kind))
                if (!QFileInfo::exists(QDir(dir).filePath("assets/minecraft/textures/" + ref + ".png")))
                    members.append("assets/minecraft/textures/" + ref + ".png");
        }
        members.removeDuplicates();
        extract(jar(version), dir, members);
        for (int i = 0; i < unresolved.size(); ++i) {
            const auto &[id, kind] = unresolved[i];
            auto image = drawIcon(dir, plans[i]);
            const auto turning = !image.isNull() && !plans[i].model.elements.isEmpty();
            for (const auto &candidate : candidates(id, kind)) {
                if (!image.isNull())
                    break;
                const auto flat = texture(dir, candidate);
                if (!flat.isNull())
                    image = drawLayers({flat});
            }
            if (image.isNull())
                continue;
            const auto file = iconFile(version, id, kind);
            QDir().mkpath(QFileInfo(file).path());
            image.save(file);
            if (turning)
                drawSpin(dir, plans[i]).save(spinFile(file));
        }
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
            if (!icon.isEmpty() && QFileInfo::exists(spinFile(icon)))
                item.insert("spin", QVariantMap{{"source", QUrl::fromLocalFile(spinFile(icon)).toString()},
                                                {"frames", SpinFrames}, {"columns", SpinColumns}});
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
