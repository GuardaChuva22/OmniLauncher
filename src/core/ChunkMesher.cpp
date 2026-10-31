#include "ChunkMesher.h"

#include "Nbt.h"
#include "WorldData.h"

#include <QDir>
#include <QMutexLocker>
#include <QtEndian>

#include <algorithm>
#include <climits>
#include <cstring>

namespace World3D {

namespace {

using namespace WorldData;

// Chunks kept decoded: enough for the meshes being made and their neighbours.
constexpr int CacheSize = 768;

constexpr int dx[6] = {0, 0, 0, 0, -1, 1};
constexpr int dy[6] = {-1, 1, 0, 0, 0, 0};
constexpr int dz[6] = {0, 0, -1, 1, 0, 0};
// The game's own shading of each side: lit from above, the east and west darker.
constexpr float shades[7] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f, 1.0f};
// Ambient occlusion: a corner with none to three opaque blocks closing in on it.
constexpr float aoLevels[4] = {1.0f, 0.8f, 0.66f, 0.52f};
// The least light a face gets: dark caves seen from above, and caves a cut opens lit
// enough to read.
constexpr float Darkest = 0.3f;
constexpr float DarkestCut = 0.62f;

// A light level (0 to 15) as the game turns it into brightness.
float brightness(float level)
{
    const float f = level / 15.f;
    return f / (3.f - 2.f * f);
}

QString stateName(const QVariant &entry, QVariantMap *properties)
{
    if (entry.typeId() == QMetaType::QString)
        return entry.toString(); // 26.x: a state without properties is just its id
    const auto state = entry.toMap();
    *properties = state.value("Properties", state.value("properties")).toMap();
    return state.value("Name", state.value("id")).toString();
}

quint32 pack(float r, float g, float b, float shade)
{
    const auto c = [shade](float v) { return quint32(qBound(0.f, v * shade, 255.f) + 0.5f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | 0xFF000000u;
}

} // namespace

namespace {

// What Java's DataOutputStream wrote: big endian, strings as writeUTF.
class JavaReader
{
public:
    explicit JavaReader(const QByteArray &data) : m_data(data) {}
    bool ok() const { return m_ok; }
    template<typename T> T number()
    {
        if (m_pos + qsizetype(sizeof(T)) > m_data.size()) {
            m_ok = false;
            return T();
        }
        T value;
        std::memcpy(&value, m_data.constData() + m_pos, sizeof(T));
        m_pos += sizeof(T);
        return qFromBigEndian(value);
    }
    QString text()
    {
        const auto size = number<quint16>();
        if (!m_ok || m_pos + size > m_data.size()) {
            m_ok = false;
            return {};
        }
        const auto text = QString::fromUtf8(m_data.constData() + m_pos, size);
        m_pos += size;
        return text;
    }

private:
    const QByteArray &m_data;
    qsizetype m_pos = 0;
    bool m_ok = true;
};

// "Block{minecraft:oak_log}[axis=y]" (how the game prints a block state) → name, properties.
QString parseState(const QString &state, QVariantMap *properties)
{
    QString name = state;
    QString rest;
    if (state.startsWith(QLatin1String("Block{"))) {
        const auto close = state.indexOf('}');
        name = state.mid(6, close - 6);
        rest = state.mid(close + 1);
    } else if (const auto open = state.indexOf('['); open >= 0) {
        name = state.left(open);
        rest = state.mid(open);
    }
    if (rest.startsWith('[') && rest.endsWith(']'))
        for (const auto &pair : rest.mid(1, rest.size() - 2).split(',', Qt::SkipEmptyParts)) {
            const auto equals = pair.indexOf('=');
            if (equals > 0)
                properties->insert(pair.left(equals), pair.mid(equals + 1));
        }
    return name;
}

} // namespace

std::shared_ptr<const LiveChunk> LiveChunk::parse(const QByteArray &payload, BlockModels &models)
{
    JavaReader in(payload);
    auto chunk = std::make_shared<LiveChunk>();
    chunk->dimension = in.text();
    chunk->cx = in.number<qint32>();
    chunk->cz = in.number<qint32>();
    chunk->minSectionY = in.number<qint32>();
    const int count = in.number<quint16>();
    if (!in.ok() || count > 256)
        return nullptr;
    chunk->sections.resize(count);
    std::vector<quint16> palette;
    for (auto &section : chunk->sections) {
        const int size = in.number<quint16>();
        palette.clear();
        for (int i = 0; i < size && in.ok(); ++i) {
            QVariantMap properties;
            const auto name = parseState(in.text(), &properties);
            palette.push_back(models.id(name, properties));
        }
        if (size == 1)
            section.uniform = palette.front();
        if (size > 1) {
            section.ids.resize(4096);
            for (int i = 0; i < 4096 && in.ok(); ++i) {
                const int index = size <= 256 ? in.number<quint8>() : in.number<quint16>();
                section.ids[i] = index < size ? palette[index] : 0;
            }
        }
        // u8 kind: 0 none, 1 one level (u8), 2 nibbles (2048 bytes, low one first).
        for (auto *light : {&section.sky, &section.block}) {
            const int kind = in.number<quint8>();
            if (kind == 1) {
                light->assign(1, in.number<quint8>() & 15);
            } else if (kind == 2) {
                light->resize(4096);
                for (int i = 0; i < 4096 && in.ok(); i += 2) {
                    const auto pair = in.number<quint8>();
                    (*light)[i] = pair & 15;
                    (*light)[i + 1] = pair >> 4;
                }
            }
        }
        if (!in.ok())
            return nullptr;
    }
    return chunk;
}

WorldMap::BiomeColor ChunkBlocks::biome(int x, int y, int z) const
{
    y = qBound(minY, y, maxY());
    const auto &section = sections[(y - minY) >> 4];
    if (section.biomes.empty())
        return {qRgb(145, 189, 89), qRgb(119, 171, 47), qRgb(63, 118, 228)};
    if (section.biomeCells.empty())
        return section.biomes.front();
    const auto index = section.biomeCells[(((y & 15) >> 2) << 4) | ((z >> 2) << 2) | (x >> 2)];
    return section.biomes[index < section.biomes.size() ? index : 0];
}

ChunkSource::ChunkSource(std::shared_ptr<BlockModels> models, const QString &regionFolder)
    : m_models(std::move(models))
    , m_folder(regionFolder)
{
}

std::shared_ptr<const ChunkBlocks> ChunkSource::decode(const QByteArray &nbt, quint32 stamp)
{
    const auto chunk = Nbt::parse(nbt);
    if (!Nbt::at(chunk, {"Status"}).toString().endsWith("full"))
        return nullptr; // still being generated
    auto blocks = std::make_shared<ChunkBlocks>();
    blocks->stamp = stamp;
    const auto list = Nbt::at(chunk, {"sections"}).toList();
    int low = INT_MAX, high = INT_MIN;
    for (const auto &entry : list) {
        const int y = qint8(Nbt::at(entry, {"Y"}).toInt());
        low = qMin(low, y);
        high = qMax(high, y);
    }
    if (low > high)
        return nullptr;
    blocks->minY = low * 16;
    blocks->sections.resize(high - low + 1);

    for (const auto &entry : list) {
        auto &section = blocks->sections[qint8(Nbt::at(entry, {"Y"}).toInt()) - low];
        const auto palette = Nbt::at(entry, {"block_states", "palette"}).toList();
        std::vector<quint16> ids;
        for (const auto &state : palette) {
            QVariantMap properties;
            const auto name = stateName(state, &properties);
            ids.push_back(m_models->id(name, properties));
        }
        if (ids.size() == 1) {
            section.uniform = ids.front();
        } else if (ids.size() > 1) {
            const auto data = Nbt::at(entry, {"block_states", "data"}).value<QList<qint64>>();
            const int bits = bitsFor(qsizetype(ids.size()), 4);
            section.ids.resize(4096);
            for (int i = 0; i < 4096; ++i) {
                const auto index = packed(data, i, bits);
                section.ids[i] = index < int(ids.size()) ? ids[index] : 0;
            }
        }
        const auto biomes = Nbt::at(entry, {"biomes", "palette"}).toList();
        for (const auto &biome : biomes)
            section.biomes.push_back(m_models->biome(biome.toString()));
        if (biomes.size() > 1) {
            const auto data = Nbt::at(entry, {"biomes", "data"}).value<QList<qint64>>();
            const int bits = bitsFor(biomes.size(), 1);
            section.biomeCells.resize(64);
            for (int i = 0; i < 64; ++i)
                section.biomeCells[i] = quint8(packed(data, i, bits));
        }
        const auto sky = Nbt::at(entry, {"SkyLight"}).toByteArray(), block = Nbt::at(entry, {"BlockLight"}).toByteArray();
        if (sky.size() == 2048 || block.size() == 2048) {
            section.light.resize(4096);
            for (int i = 0; i < 4096; ++i) {
                const int shift = (i & 1) * 4;
                const int s = sky.size() == 2048 ? (quint8(sky[i >> 1]) >> shift) & 15 : 15;
                const int b = block.size() == 2048 ? (quint8(block[i >> 1]) >> shift) & 15 : 0;
                section.light[i] = quint8(s << 4 | b);
            }
        }
    }

    const auto surface = Nbt::at(chunk, {"Heightmaps", "WORLD_SURFACE"}).value<QList<qint64>>();
    blocks->minSurface = blocks->minY;
    if (!surface.isEmpty()) {
        const int bits = 64 / ((256 + int(surface.size()) - 1) / int(surface.size()));
        int lowest = INT_MAX;
        blocks->surface.resize(256);
        for (int i = 0; i < 256; ++i) {
            const int height = packed(surface, i, bits);
            blocks->surface[i] = qint16(blocks->minY + height);
            lowest = qMin(lowest, height);
        }
        blocks->minSurface = blocks->minY + lowest - 1;
    }
    return blocks;
}

std::shared_ptr<const ChunkBlocks> ChunkSource::chunk(int cx, int cz)
{
    const auto key = chunkKey(cx, cz);
    quint32 savedStamp = 0;
    const auto file = saved(cx, cz, &savedStamp);
    std::shared_ptr<const LiveChunk> live;
    quint32 version = 0;
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_live.constFind(key);
        if (it == m_live.cend())
            return file;
        if (it->merged && it->mergedStamp == savedStamp)
            return it->merged;
        live = it->chunk;
        version = it->version;
    }
    const auto merged = merge(file.get(), *live, stampWith(savedStamp, version));
    QMutexLocker lock(&m_mutex);
    const auto it = m_live.find(key);
    if (it != m_live.end() && it->version == version) {
        it->merged = merged;
        it->mergedStamp = savedStamp;
    }
    return merged;
}

std::shared_ptr<const ChunkBlocks> ChunkSource::merge(const ChunkBlocks *file, const LiveChunk &live, quint32 stamp) const
{
    auto blocks = std::make_shared<ChunkBlocks>();
    blocks->stamp = stamp;
    blocks->minY = live.minSectionY * 16;
    blocks->sections.resize(live.sections.size());
    for (size_t i = 0; i < live.sections.size(); ++i) {
        auto &section = blocks->sections[i];
        section.uniform = live.sections[i].uniform;
        section.ids = live.sections[i].ids;
        // Biomes don't come live: the saved ones, where there are some.
        const int y = blocks->minY + int(i) * 16;
        if (file && y >= file->minY && y <= file->maxY()) {
            const auto &old = file->sections[(y - file->minY) >> 4];
            section.biomes = old.biomes;
            section.biomeCells = old.biomeCells;
            section.light = old.light;
        }
    }
    // The surface from the blocks themselves; anything above it is in full sky now, even
    // what was dug out since the save, unless the game sent the sky there.
    blocks->surface.assign(256, qint16(blocks->minY));
    int lowest = INT_MAX;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            for (int y = blocks->maxY(); y >= blocks->minY; --y) {
                const auto id = blocks->at(x, y, z);
                if (id != 0 && !m_models->block(id).air) {
                    blocks->surface[(z << 4) | x] = qint16(y + 1);
                    break;
                }
            }
            lowest = qMin(lowest, blocks->surface[(z << 4) | x] - 1);
        }
    blocks->minSurface = lowest;

    // Light as the game has it now, where it sent some (it keeps none for much of the open
    // sky); what it left out comes from the save, or from the surface: full sky above it,
    // dark below.
    for (size_t i = 0; i < live.sections.size(); ++i) {
        const auto &now = live.sections[i];
        auto &light = blocks->sections[i].light;
        if (light.empty() && now.sky.empty() && now.block.empty())
            continue;
        const bool fromFile = !light.empty();
        if (!fromFile)
            light.resize(4096);
        const int minY = blocks->minY + int(i) * 16;
        for (int cell = 0; cell < 4096; ++cell) {
            int sky = light[cell] >> 4, block = light[cell] & 15;
            if (!now.sky.empty())
                sky = now.sky[now.sky.size() == 1 ? 0 : cell];
            else if (!fromFile || minY + (cell >> 8) >= blocks->surface[cell & 255])
                sky = minY + (cell >> 8) >= blocks->surface[cell & 255] ? 15 : 0;
            if (!now.block.empty())
                block = now.block[now.block.size() == 1 ? 0 : cell];
            light[cell] = quint8(sky << 4 | block);
        }
    }
    return blocks;
}

void ChunkSource::setLive(std::shared_ptr<const LiveChunk> chunk)
{
    QMutexLocker lock(&m_mutex);
    auto &live = m_live[chunkKey(chunk->cx, chunk->cz)];
    live.chunk = std::move(chunk);
    live.version = ++m_liveVersion;
    live.merged.reset();
}

void ChunkSource::clearLive()
{
    QMutexLocker lock(&m_mutex);
    m_live.clear();
}

QHash<qint64, quint32> ChunkSource::liveVersions()
{
    QMutexLocker lock(&m_mutex);
    QHash<qint64, quint32> versions;
    versions.reserve(m_live.size());
    for (auto it = m_live.cbegin(); it != m_live.cend(); ++it)
        versions.insert(it.key(), it->version);
    return versions;
}

std::shared_ptr<const ChunkBlocks> ChunkSource::saved(int cx, int cz, quint32 *savedStamp)
{
    const int rx = cx >> 5, rz = cz >> 5;
    const int index = (cz & 31) * 32 + (cx & 31);
    const auto mca = QDir(m_folder).filePath(QStringLiteral("r.%1.%2.mca").arg(rx).arg(rz));
    const auto header = regionHeader(mca);
    *savedStamp = 0;
    if (!hasChunk(header, index))
        return nullptr;
    const auto stamp = chunkStamp(header, index);
    *savedStamp = stamp;
    const auto key = chunkKey(cx, cz);
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_cache.find(key);
        if (it != m_cache.end() && it->stamp == stamp) {
            m_ages.splice(m_ages.begin(), m_ages, it->age);
            return it->blocks;
        }
    }
    const auto blocks = decode(readChunk(mca, index), stamp);
    QMutexLocker lock(&m_mutex);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        m_ages.erase(it->age);
        m_cache.erase(it);
    }
    m_ages.push_front(key);
    m_cache.insert(key, {blocks, stamp, m_ages.begin()});
    while (m_cache.size() > CacheSize) {
        m_cache.remove(m_ages.back());
        m_ages.pop_back();
    }
    return blocks;
}

std::shared_ptr<ChunkMesh> ChunkSource::mesh(int cx, int cz, int cut, int originX, int originZ)
{
    const auto center = chunk(cx, cz);
    if (!center)
        return nullptr;
    // The chunks around it, [z][x] from the north west; their edges shade and tint this one.
    std::shared_ptr<const ChunkBlocks> grid[3][3];
    for (int gz = 0; gz < 3; ++gz)
        for (int gx = 0; gx < 3; ++gx)
            grid[gz][gx] = gx == 1 && gz == 1 ? center : chunk(cx + gx - 1, cz + gz - 1);
    // The chunk holding a cell up to 16 blocks out, the cell's x and z made its own.
    const auto chunkAt = [&grid](int &x, int &z) {
        const auto *found = grid[z < 0 ? 0 : z > 15 ? 2 : 1][x < 0 ? 0 : x > 15 ? 2 : 1].get();
        x &= 15, z &= 15;
        return found;
    };
    const auto &models = *m_models;

    const int minY = center->minY;
    const int top = qMin(center->maxY(), cut);
    if (top < minY)
        return nullptr;
    const int height = top - minY + 1;
    const auto cell = [minY](int x, int y, int z) { return ((y - minY) * 16 + z) * 16 + x; };
    const auto opaqueId = [&models](quint16 id) { return models.block(id).opaque; };

    // Cells the light of the sky reaches: from the top (or the cut) down through anything
    // not opaque, and in from the sides above the lowest ground.
    std::vector<quint8> open(size_t(height) * 256, 0);
    std::vector<int> stack;
    const auto seed = [&](int x, int y, int z) {
        const int i = cell(x, y, z);
        if (!open[i] && !opaqueId(center->at(x, y, z))) {
            open[i] = 1;
            stack.push_back(i);
        }
    };
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x)
            seed(x, top, z);
    const int sideFrom = qMax(minY, center->minSurface - 4);
    for (int y = sideFrom; y <= top; ++y)
        for (int i = 0; i < 16; ++i) {
            seed(i, y, 0), seed(i, y, 15), seed(0, y, i), seed(15, y, i);
        }
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const int x = i & 15, z = (i >> 4) & 15, y = (i >> 8) + minY;
        for (int f = 0; f < 6; ++f) {
            const int nx = x + dx[f], ny = y + dy[f], nz = z + dz[f];
            if (nx < 0 || nx > 15 || nz < 0 || nz > 15 || ny < minY || ny > top)
                continue;
            seed(nx, ny, nz);
        }
    }

    // A neighbouring cell: its block (air above the cut), and whether it is in the light.
    const auto neighbour = [&](int x, int y, int z, quint16 *id) -> bool {
        *id = 0;
        if (y > top)
            return true;
        if (y < minY)
            return false;
        if (x >= 0 && x < 16 && z >= 0 && z < 16) {
            *id = center->at(x, y, z);
            return open[cell(x, y, z)];
        }
        const auto *side = chunkAt(x, z);
        if (!side)
            return true; // the edge of what was generated: its sides show
        *id = side->at(x, y, z);
        return !opaqueId(*id) && y >= qMin(side->minSurface, center->minSurface) - 4;
    };
    // Any cell near the chunk: whether it is opaque, and its light (sky << 4 | block).
    // Above the cut is open sky.
    const auto sample = [&](int x, int y, int z, bool *solid) {
        *solid = false;
        if (y > top)
            return 0xF0;
        const auto *at = chunkAt(x, z);
        if (!at)
            return 0xF0;
        *solid = opaqueId(at->at(x, y, z));
        return at->light(x, y, z);
    };

    // Biome tints blended over the 5×5 columns around a block, as the game does; worked out
    // once per column and biome cell (4 blocks high).
    std::vector<WorldMap::BiomeColor> blended(size_t((height + 3) / 4 + 1) * 256);
    std::vector<quint8> blendedDone(blended.size(), 0);
    const auto biomeAt = [&](int x, int y, int z) -> const WorldMap::BiomeColor & {
        const size_t i = size_t((y - minY) >> 2) * 256 + size_t(z * 16 + x);
        if (!blendedDone[i]) {
            int sum[9] = {};
            for (int bz = z - 2; bz <= z + 2; ++bz)
                for (int bx = x - 2; bx <= x + 2; ++bx) {
                    int lx = bx, lz = bz;
                    const auto *at = chunkAt(lx, lz);
                    const auto biome = at ? at->biome(lx, y, lz) : center->biome(x, y, z);
                    const QRgb colors[3] = {biome.grass, biome.foliage, biome.water};
                    for (int c = 0; c < 3; ++c)
                        sum[c * 3] += qRed(colors[c]), sum[c * 3 + 1] += qGreen(colors[c]), sum[c * 3 + 2] += qBlue(colors[c]);
                }
            const auto mean = [&sum](int c) { return qRgb(sum[c * 3] / 25, sum[c * 3 + 1] / 25, sum[c * 3 + 2] / 25); };
            blended[i] = {mean(0), mean(1), mean(2)};
            blendedDone[i] = 1;
        }
        return blended[i];
    };

    auto mesh = std::make_shared<ChunkMesh>();
    mesh->cx = cx;
    mesh->cz = cz;
    mesh->stamp = center->stamp;
    mesh->cut = cut;
    const float baseX = float(cx * 16 - originX), baseZ = float(cz * 16 - originZ);
    const float darkest = top == cut ? DarkestCut : Darkest;

    // Each corner of a face on the side of its block takes the light of the four cells
    // around that corner in front of it, and darkens where blocks close in on it (ambient
    // occlusion); other faces take the light of their own cell.
    const auto add = [&](const Quad &quad, int x, int y, int z, const Block &block) {
        float shade = shades[quad.light];
        // The rock a cut goes through, darker, so what is open below it stands out.
        if (y == top && top == cut && quad.light == Up)
            shade *= 0.55f;
        QRgb tint = qRgb(255, 255, 255);
        if (quad.tinted && block.tint != NoTint) {
            tint = block.fixedTint;
            if (block.tint != FixedTint) {
                const auto &biome = biomeAt(x, y, z);
                tint = block.tint == WaterTint ? biome.water : block.tint == FoliageTint ? biome.foliage : biome.grass;
            }
        }

        float sky[4], torch[4], occlusion[4] = {1, 1, 1, 1};
        const int f = quad.cull;
        if (f == NoFace) {
            bool solid;
            const int light = sample(x, y, z, &solid);
            std::fill(sky, sky + 4, float(light >> 4));
            std::fill(torch, torch + 4, float(light & 15));
        } else {
            // The two directions along the face.
            const int axis = f < North ? 1 : f < West ? 2 : 0;
            const int u = axis == 0 ? 1 : 0, v = axis == 2 ? 1 : 2;
            const int front[3] = {x + dx[f], y + dy[f], z + dz[f]};
            for (int i = 0; i < 4; ++i) {
                int step[2][3] = {};
                step[0][u] = quad.pos[i][u] > 0.5f ? 1 : -1;
                step[1][v] = quad.pos[i][v] > 0.5f ? 1 : -1;
                bool solid[3];
                int light[4];
                bool use[4] = {true, false, false, false};
                light[0] = sample(front[0], front[1], front[2], &solid[0]);
                light[1] = sample(front[0] + step[0][0], front[1] + step[0][1], front[2] + step[0][2], &solid[0]);
                light[2] = sample(front[0] + step[1][0], front[1] + step[1][1], front[2] + step[1][2], &solid[1]);
                light[3] = sample(front[0] + step[0][0] + step[1][0], front[1] + step[0][1] + step[1][1],
                                  front[2] + step[0][2] + step[1][2], &solid[2]);
                use[1] = !solid[0], use[2] = !solid[1], use[3] = !solid[2] && !(solid[0] && solid[1]);
                const int closed = solid[0] && solid[1] ? 3 : int(solid[0]) + int(solid[1]) + int(solid[2]);
                occlusion[i] = aoLevels[closed];
                float s = 0, b = 0;
                int n = 0;
                for (int k = 0; k < 4; ++k)
                    if (use[k])
                        s += float(light[k] >> 4), b += float(light[k] & 15), ++n;
                sky[i] = s / float(n), torch[i] = b / float(n);
            }
        }

        quint32 colors[4];
        float bright[4];
        for (int i = 0; i < 4; ++i) {
            const float day = brightness(sky[i]), lamp = brightness(torch[i]);
            const float r = qMax(darkest, qMax(day, lamp)), g = qMax(darkest, qMax(day, lamp * 0.86f)),
                        b = qMax(darkest, qMax(day, lamp * 0.62f));
            const float k = shade * occlusion[i];
            colors[i] = pack(qRed(tint) * r, qGreen(tint) * g, qBlue(tint) * b, k);
            bright[i] = (r + g + b) * k;
        }
        // Split the quad along the diagonal that keeps the darker corners apart, so the
        // shading of a corner doesn't leak along a seam.
        const int first = bright[0] + bright[2] < bright[1] + bright[3] ? 1 : 0;
        auto &out = quad.layer == Translucent ? mesh->translucent : mesh->solid;
        for (int j = 0; j < 4; ++j) {
            const int i = (j + first) & 3;
            out.push_back({baseX + x + quad.pos[i][0], float(y) + quad.pos[i][1], baseZ + z + quad.pos[i][2], quad.uv[i][0],
                           quad.uv[i][1], colors[i]});
        }
    };

    for (int y = minY; y <= top; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                const auto id = center->at(x, y, z);
                if (id == 0)
                    continue;
                const auto &block = models.block(id);
                if (block.air)
                    continue;
                quint16 near[6];
                bool lit[6];
                bool any = false;
                for (int f = 0; f < 6; ++f) {
                    lit[f] = neighbour(x + dx[f], y + dy[f], z + dz[f], &near[f]);
                    any = any || lit[f];
                }
                if (!any)
                    continue; // sealed in

                for (const auto &quad : block.quads) {
                    if (quad.cull != NoFace) {
                        const auto f = quad.cull;
                        if (!lit[f] || opaqueId(near[f]))
                            continue;
                        // Leaves, glass: the faces between two of the same kind stay out.
                        if (near[f] == id && quad.layer != Solid)
                            continue;
                    }
                    add(quad, x, y, z, block);
                }

                if (block.water || block.lava) {
                    const auto same = [&block, &models](quint16 other) {
                        const auto &b = models.block(other);
                        return block.water ? b.water : b.lava;
                    };
                    const auto &fluid = models.fluid(block.lava, !same(near[Up]) && y != top);
                    for (const auto &quad : fluid.quads) {
                        const auto f = quad.cull;
                        if (!lit[f] || opaqueId(near[f]) || same(near[f]))
                            continue;
                        add(quad, x, y, z, fluid);
                    }
                }
            }
        }
    }
    if (mesh->solid.empty() && mesh->translucent.empty())
        return nullptr;
    return mesh;
}

} // namespace World3D
