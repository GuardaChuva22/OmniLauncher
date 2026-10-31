#pragma once

#include "WorldMap.h"
#include "WorldData.h"

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <vector>

// The game's block models, baked for the 3D map: every block state the map meets is
// looked up in the blockstates and models of the client jar and the game's mods (the
// client wins for its own blocks), its elements turned into quads in a block's 0–1
// space, and its textures packed into one atlas.
namespace World3D {

enum Face : quint8 { Down, Up, North, South, West, East, NoFace };
enum Layer : quint8 { Solid, Cutout, Translucent };
enum Tint : quint8 { NoTint, GrassTint, FoliageTint, WaterTint, FixedTint };

struct Quad
{
    float pos[4][3]; // a block's 0–1 space
    float uv[4][2];  // the atlas, 0–1
    quint8 cull = NoFace;  // left out when the block on that side is opaque
    quint8 light = NoFace; // the side it faces, for its shade; NoFace: not shaded
    quint8 layer = Solid;
    bool tinted = false;
};

struct Block
{
    std::vector<Quad> quads;
    quint8 tint = NoTint; // what colours its tinted quads
    QRgb fixedTint = 0;
    bool air = false;
    bool opaque = false; // a full cube of solid faces: hides the faces that touch it
    bool water = false;  // holds water (water, waterlogged, kelp…)
    bool lava = false;
};

class BlockModels
{
public:
    // The atlas: cells of 32 px, each texture's 16 px in the middle with its own edges
    // wrapped around them, so mipmaps don't bleed one texture into the next.
    static constexpr int AtlasSize = 2048;
    static constexpr int Cell = 32;

    BlockModels(const QString &dataDirectory, const QString &version, const QString &instanceDirectory);
    ~BlockModels();

    // The id of a block state ("minecraft:oak_stairs" and its properties), baked the first
    // time it is asked for. Thread-safe.
    quint16 id(const QString &name, const QVariantMap &properties);
    // Thread-safe for ids already handed out: blocks never move.
    const Block &block(quint16 id) const { return m_blocks[id]; }
    // Water and lava as the map draws them: a still surface (top lowered) or full.
    const Block &fluid(bool lava, bool surface) const { return m_fluids[(lava ? 2 : 0) + (surface ? 1 : 0)]; }

    WorldMap::BiomeColor biome(const QString &id) const;
    // The atlas cells made since `revision` (cells are numbered in order); `*now` gets the
    // revision they bring it to. Thread-safe.
    QList<QImage> cellsSince(int revision, int *now) const;
    static void cellOrigin(int cell, int *x, int *y);

private:
    struct Model
    {
        QJsonArray elements;
        QHash<QString, QString> textures;
    };
    struct Sprite
    {
        int cell = 1;
        quint8 layer = Solid;
    };

    QByteArray read(const QString &path) const;
    Block bake(const QString &name, const QVariantMap &properties);
    const Model &model(const QString &ref);
    Sprite sprite(const QString &texture);
    int addCell(const QImage &texture);
    void bakeElement(const QJsonObject &element, const Model &model, int rotateX, int rotateY, Block &block,
                     bool *fullCube);
    void makeFluids();

    std::unique_ptr<WorldData::Jar> m_client;
    std::vector<std::unique_ptr<WorldData::Jar>> m_mods;
    QHash<QString, WorldData::Jar *> m_index; // asset path → the jar that has it
    QHash<QString, WorldMap::BiomeColor> m_biomes;

    mutable QMutex m_mutex;
    QHash<QString, quint16> m_ids;
    std::vector<Block> m_blocks; // reserved up front: references stay valid
    Block m_fluids[4];
    QHash<QString, QJsonObject> m_states;
    QHash<QString, Model> m_models;
    QHash<QString, Sprite> m_sprites;
    QList<QImage> m_cells;
};

} // namespace World3D
