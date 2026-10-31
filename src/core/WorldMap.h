#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <QVariantMap>

// What the map knows of a world besides its blocks: the colours its blocks and biomes
// take, where its regions are, the player, the spawn and the mobs saved in it.
namespace WorldMap {

// A block seen from above and from the front; alpha 0 on top: see through it (plants,
// torches). Tinted faces keep their grey texture and take the colour of the biome.
struct BlockColor
{
    QRgb top = 0;
    QRgb side = 0;
    quint8 tint = 0; // Tint* | TopTinted | SideTinted
};
enum : quint8 { TintGrass = 1, TintFoliage = 2, TintWater = 3, TintKind = 3, TopTinted = 4, SideTinted = 8 };

struct BiomeColor
{
    QRgb grass, foliage, water;
};

struct Palette
{
    QHash<QString, BlockColor> blocks;
    QHash<QString, BiomeColor> biomes;
};

// The blocks and biomes of a version, and of the mods of a game folder: the average of
// each block model's top and side textures, the colours each biome tints grass, leaves
// and water with. Read from the client jar and each mod jar once, then kept in
// cache/worlds/palette (one file per jar).
Palette palette(const QString &dataDirectory, const QString &version, const QString &instanceDirectory);

// The folder of a dimension ("minecraft:overworld"…) holding `kind` ("region",
// "entities"), in the layout of the world's version; "" if it has none.
QString dimensionFolder(const QString &worldPath, const QString &dimension, const QString &kind = QStringLiteral("region"));

// What the map view needs to open a world: regions [{x, z, stamp}], dimensions (those
// with a region folder), version, player {x, y, z, dimension} and spawn {x, y, z}.
QVariantMap describe(const QString &worldPath, const QString &dimension);

// The living entities saved in a dimension: [{x, y, z, id, hostile}], at most `limit`.
QVariantList entities(const QString &worldPath, const QString &dimension, int limit = 4000);

} // namespace WorldMap
