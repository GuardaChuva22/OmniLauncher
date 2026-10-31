#pragma once

#include "BlockModels.h"

#include <QHash>
#include <QMutex>
#include <QString>

#include <list>
#include <memory>
#include <vector>

// Chunks of a world turned into meshes for the 3D map. A chunk's blocks become block ids
// (BlockModels) once and are kept a while, since each mesh also looks at the edges of
// the eight chunks around it. Only faces that can be seen are made: a face against an
// opaque block is left out, and so is everything in the dark, sealed off from the sky
// (caves no opening leads to). Faces are lit by the light the game saved, smoothed over
// their corners with ambient occlusion, and tinted by the biomes blended around them.
namespace World3D {

struct Vertex
{
    float x, y, z;
    float u, v;
    quint32 color; // RGBA, the shade and biome tint
};

struct ChunkBlocks
{
    struct Section
    {
        quint16 uniform = 0;      // the one block of a section holding a single kind
        std::vector<quint16> ids; // 4096 ids (y, z, x) when it holds more
        std::vector<WorldMap::BiomeColor> biomes;
        std::vector<quint8> biomeCells; // 64 indices into `biomes`, 4×4×4 blocks each
        std::vector<quint8> light;      // 4096 (sky << 4 | block) as the game saved them, if it did
    };
    int minY = 0;
    int minSurface = 0; // the lowest ground of its columns
    quint32 stamp = 0;  // when the game saved it
    std::vector<Section> sections;
    std::vector<qint16> surface; // 256 (z, x): the height above each column's highest block

    int maxY() const { return minY + int(sections.size()) * 16 - 1; }
    quint16 at(int x, int y, int z) const
    {
        if (y < minY || y > maxY())
            return 0;
        const auto &section = sections[(y - minY) >> 4];
        return section.ids.empty() ? section.uniform : section.ids[((y & 15) << 8) | (z << 4) | x];
    }
    // The light of a cell, (sky << 4 | block). A section the game left without light is in
    // full sky above the ground and dark below it.
    int light(int x, int y, int z) const
    {
        if (y > maxY())
            return 0xF0;
        if (y >= minY) {
            const auto &section = sections[(y - minY) >> 4];
            if (!section.light.empty())
                return section.light[((y & 15) << 8) | (z << 4) | x];
        }
        return surface.empty() || y >= surface[(z << 4) | x] ? 0xF0 : 0;
    }
    WorldMap::BiomeColor biome(int x, int y, int z) const;
};

// A chunk's blocks and light as the game has them now, sent by the Omni agent (a Blocks
// frame, see OmniLink); laid over what was saved until the map lets it go.
struct LiveChunk
{
    struct Section
    {
        quint16 uniform = 0;
        std::vector<quint16> ids; // 4096 (y, z, x) when it holds more than one kind
        // Sky and block light: empty when the game had none for it, one level all through,
        // or 4096 levels (y, z, x).
        std::vector<quint8> sky, block;
    };
    QString dimension;
    int cx = 0, cz = 0;
    int minSectionY = 0;
    std::vector<Section> sections;

    // Null when the frame is cut short or malformed.
    static std::shared_ptr<const LiveChunk> parse(const QByteArray &payload, BlockModels &models);
};

struct ChunkMesh
{
    int cx = 0, cz = 0;
    quint32 stamp = 0;
    int cut = 0;
    std::vector<Vertex> solid;       // solid and cut-out faces
    std::vector<Vertex> translucent; // water, ice, stained glass
};

class ChunkSource
{
public:
    ChunkSource(std::shared_ptr<BlockModels> models, const QString &regionFolder);

    // A chunk's blocks, live ones over saved ones; null if it doesn't exist or isn't
    // fully generated. Thread-safe.
    std::shared_ptr<const ChunkBlocks> chunk(int cx, int cz);
    // The mesh of a chunk for a height cut (a big number: none), its vertices relative
    // to (originX, originZ). Null when the chunk has nothing to draw.
    std::shared_ptr<ChunkMesh> mesh(int cx, int cz, int cut, int originX, int originZ);
    BlockModels &models() { return *m_models; }

    // Live blocks over the saved ones, until clearLive(). Thread-safe.
    void setLive(std::shared_ptr<const LiveChunk> chunk);
    void clearLive();
    // Which live version each chunk has (none: not there), to tell when a chunk changed.
    QHash<qint64, quint32> liveVersions();
    // A chunk's stamp with its live version over the saved one's.
    static quint32 stampWith(quint32 saved, quint32 live) { return live ? saved ^ (live * 2654435761u) ^ 0x5A5A5A5Au : saved; }

private:
    // The chunk as saved, and its stamp in the region (0: not there).
    std::shared_ptr<const ChunkBlocks> saved(int cx, int cz, quint32 *stamp);
    std::shared_ptr<const ChunkBlocks> decode(const QByteArray &nbt, quint32 stamp);
    std::shared_ptr<const ChunkBlocks> merge(const ChunkBlocks *file, const LiveChunk &live, quint32 stamp) const;

    std::shared_ptr<BlockModels> m_models;
    QString m_folder;
    QMutex m_mutex;
    struct Entry
    {
        std::shared_ptr<const ChunkBlocks> blocks;
        quint32 stamp;
        std::list<qint64>::iterator age;
    };
    QHash<qint64, Entry> m_cache;
    std::list<qint64> m_ages; // most recent first
    struct Live
    {
        std::shared_ptr<const LiveChunk> chunk;
        quint32 version = 0;
        std::shared_ptr<const ChunkBlocks> merged; // with the saved chunk of `mergedStamp`
        quint32 mergedStamp = 0;
    };
    QHash<qint64, Live> m_live;
    quint32 m_liveVersion = 0;
};

inline qint64 chunkKey(int cx, int cz)
{
    return (qint64(cx) << 32) | quint32(cz);
}

} // namespace World3D
