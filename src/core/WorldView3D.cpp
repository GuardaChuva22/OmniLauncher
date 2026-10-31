#include "WorldView3D.h"

#include "EntityModels.h"
#include "OmniLink.h"
#include "WorldData.h"
#include "WorldMap.h"

#include <QDir>
#include <QFile>
#include <QtMath>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

using namespace World3D;

QString s_dataDirectory;
QPointer<OmniLink> s_link;

// Chunks made for the view at once at most, the nearest to its middle.
constexpr int MaxChunks = 1600;
// Chunks made ahead around the view's edges (after those in it), so a pan finds the
// land already there.
constexpr int Preload = 8;
constexpr int MaxPreload = 1200;
// Chunks kept once made, wherever the view goes: a map that stays put as it is
// explored. Past this many quads on the GPU (96 bytes each: about 300 MB), the
// farthest from the view are let go.
constexpr qint64 KeepQuads = 3'200'000;

int chunkX(qint64 key) { return int(key >> 32); }
int chunkZ(qint64 key) { return int(quint32(key)); }
qint64 regionKey(int rx, int rz) { return (qint64(rx) << 32) | quint32(rz); }

QShader loadShader(const QString &name)
{
    QFile file(name);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
}

} // namespace

// The GPU side, on the render thread: a vertex buffer per chunk (solid and translucent
// faces apart), one shared index buffer of quads, and the atlas.
class WorldViewRenderer : public QQuickRhiItemRenderer
{
public:
    ~WorldViewRenderer() override { releaseChunks(); }

protected:
    void initialize(QRhiCommandBuffer *) override
    {
        auto *r = rhi();
        if (!m_atlas) {
            m_atlas.reset(r->newTexture(QRhiTexture::RGBA8, QSize(BlockModels::AtlasSize, BlockModels::AtlasSize), 1,
                                        QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
            m_atlas->create();
            m_sampler.reset(r->newSampler(QRhiSampler::Nearest, QRhiSampler::Linear, QRhiSampler::Linear,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
            m_sampler->create();
            for (auto &frame : m_frames) {
                frame.reset(r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
                frame->create();
            }
            m_entityAtlas.reset(r->newTexture(QRhiTexture::RGBA8, QSize(EntityModels::AtlasSize, EntityModels::AtlasSize)));
            m_entityAtlas->create();
            m_entityBindings.reset(r->newShaderResourceBindings());
            m_entityBindings->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                                         m_frames[0].get()),
                QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_entityAtlas.get(), m_sampler.get()),
            });
            m_entityBindings->create();
            for (int i = 0; i < 2; ++i) {
                m_bindings[i].reset(r->newShaderResourceBindings());
                m_bindings[i]->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                                             m_frames[i].get()),
                    QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_atlas.get(), m_sampler.get()),
                });
                m_bindings[i]->create();
            }
        }
        const auto format = renderTarget()->renderPassDescriptor()->serializedFormat();
        if (m_solid && format == m_format && m_samples == renderTarget()->sampleCount())
            return;
        m_format = format;
        m_samples = renderTarget()->sampleCount();
        const auto vertex = loadShader(QStringLiteral(":/shaders/world.vert.qsb"));
        const auto fragment = loadShader(QStringLiteral(":/shaders/world.frag.qsb"));
        QRhiVertexInputLayout layout;
        layout.setBindings({QRhiVertexInputBinding(sizeof(Vertex))});
        layout.setAttributes({QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, 0),
                              QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2, 12),
                              QRhiVertexInputAttribute(0, 2, QRhiVertexInputAttribute::UNormByte4, 20)});
        for (int i = 0; i < 2; ++i) {
            auto &pipeline = i == 0 ? m_solid : m_translucent;
            pipeline.reset(r->newGraphicsPipeline());
            pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
            pipeline->setVertexInputLayout(layout);
            pipeline->setShaderResourceBindings(m_bindings[i].get());
            pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            pipeline->setSampleCount(m_samples);
            pipeline->setDepthTest(true);
            // Equal depth passes: an overlay (grass sides) drawn after its block shows.
            pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            pipeline->setDepthWrite(i == 0);
            pipeline->setCullMode(QRhiGraphicsPipeline::None);
            if (i == 1) {
                QRhiGraphicsPipeline::TargetBlend blend;
                blend.enable = true;
                blend.srcColor = QRhiGraphicsPipeline::One;
                blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
                blend.srcAlpha = QRhiGraphicsPipeline::One;
                blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
                pipeline->setTargetBlends({blend});
            }
            pipeline->create();
        }
    }

    void synchronize(QQuickRhiItem *rhiItem) override
    {
        auto *item = static_cast<WorldView3D *>(rhiItem);
        if (item->m_dropAll) {
            m_dropAll = true;
            item->m_dropAll = false;
        }
        m_uploads += std::exchange(item->m_uploads, {});
        m_drops += std::exchange(item->m_drops, {});
        if (item->m_models != m_models) {
            m_models = item->m_models;
            m_atlasRevision = 0;
        }
        if (item->m_markersDirty) {
            m_markers = item->m_markers;
            m_mobs = item->m_mobs;
            m_markersDirty = true;
            item->m_markersDirty = false;
        }
        if (EntityModels::shared().revision() != m_entityRevision) {
            m_entityRevision = EntityModels::shared().revision();
            m_entitySkins = EntityModels::shared().atlas();
        }
        m_viewProjection = item->viewProjection();
        m_background = item->m_background;
    }

    void render(QRhiCommandBuffer *cb) override
    {
        auto *r = rhi();
        auto *u = r->nextResourceUpdateBatch();

        if (m_dropAll) {
            releaseChunks();
            m_dropAll = false;
        }
        for (const auto key : std::exchange(m_drops, {})) {
            auto it = m_chunks.find(key);
            if (it != m_chunks.end()) {
                release(*it);
                m_chunks.erase(it);
            }
        }
        qsizetype mostQuads = 0;
        for (const auto &mesh : std::exchange(m_uploads, {})) {
            auto &gpu = m_chunks[chunkKey(mesh->cx, mesh->cz)];
            release(gpu);
            const auto upload = [&](const std::vector<Vertex> &vertices, QRhiBuffer **buffer, int *quads) {
                *quads = int(vertices.size() / 4);
                if (vertices.empty())
                    return;
                *buffer = r->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, quint32(vertices.size() * sizeof(Vertex)));
                (*buffer)->create();
                u->uploadStaticBuffer(*buffer, vertices.data());
                mostQuads = qMax<qsizetype>(mostQuads, *quads);
            };
            upload(mesh->solid, &gpu.solid, &gpu.solidQuads);
            upload(mesh->translucent, &gpu.translucent, &gpu.translucentQuads);
        }
        if (m_markersDirty) {
            m_markersDirty = false;
            const auto fill = [&](QRhiBuffer *&buffer, const std::vector<Vertex> &vertices) {
                const auto bytes = quint32(qMax<size_t>(1, vertices.size()) * sizeof(Vertex));
                if (!buffer || buffer->size() < bytes) {
                    if (buffer)
                        buffer->deleteLater();
                    buffer = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, bytes * 2);
                    buffer->create();
                }
                if (!vertices.empty())
                    u->updateDynamicBuffer(buffer, 0, quint32(vertices.size() * sizeof(Vertex)), vertices.data());
                mostQuads = qMax<qsizetype>(mostQuads, qsizetype(vertices.size() / 4));
            };
            fill(m_markerBuffer, m_markers);
            fill(m_mobBuffer, m_mobs);
        }
        if (!m_entitySkins.isNull()) {
            u->uploadTexture(m_entityAtlas.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(m_entitySkins))));
            m_entitySkins = QImage();
        }
        if (mostQuads > m_indexQuads) {
            // Quads as two triangles each: 0 1 2, 0 2 3.
            m_indexQuads = qMax<qsizetype>(mostQuads, m_indexQuads * 2);
            m_indexQuads = qMax<qsizetype>(m_indexQuads, 4096);
            std::vector<quint32> indices(size_t(m_indexQuads) * 6);
            for (qsizetype q = 0; q < m_indexQuads; ++q) {
                const auto base = quint32(q * 4);
                const quint32 quad[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
                std::copy(quad, quad + 6, indices.begin() + q * 6);
            }
            if (m_indices)
                m_indices->deleteLater();
            m_indices = r->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, quint32(indices.size() * 4));
            m_indices->create();
            u->uploadStaticBuffer(m_indices, indices.data());
        }
        if (m_models) {
            int now = 0;
            const auto cells = m_models->cellsSince(m_atlasRevision, &now);
            if (!cells.isEmpty()) {
                QList<QRhiTextureUploadEntry> entries;
                for (int i = 0; i < cells.size(); ++i) {
                    int x, y;
                    BlockModels::cellOrigin(m_atlasRevision + i, &x, &y);
                    QRhiTextureSubresourceUploadDescription cell(cells[i]);
                    cell.setDestinationTopLeft(QPoint(x, y));
                    entries.append(QRhiTextureUploadEntry(0, 0, cell));
                }
                QRhiTextureUploadDescription description;
                description.setEntries(entries.cbegin(), entries.cend());
                u->uploadTexture(m_atlas.get(), description);
                u->generateMips(m_atlas.get());
                m_atlasRevision = now;
            }
        }
        const auto mvp = r->clipSpaceCorrMatrix() * m_viewProjection;
        const float solidParams[4] = {0.5f, 1.f, 0, 0}, translucentParams[4] = {0.02f, 0.f, 0, 0};
        u->updateDynamicBuffer(m_frames[0].get(), 0, 64, mvp.constData());
        u->updateDynamicBuffer(m_frames[0].get(), 64, 16, solidParams);
        u->updateDynamicBuffer(m_frames[1].get(), 0, 64, mvp.constData());
        u->updateDynamicBuffer(m_frames[1].get(), 64, 16, translucentParams);

        const auto size = renderTarget()->pixelSize();
        cb->beginPass(renderTarget(), m_background, {1.0f, 0}, u);
        if (m_indices) {
            const auto draw = [&](QRhiBuffer *buffer, int quads) {
                if (!buffer || quads <= 0)
                    return;
                const QRhiCommandBuffer::VertexInput input(buffer, 0);
                cb->setVertexInput(0, 1, &input, m_indices, 0, QRhiCommandBuffer::IndexUInt32);
                cb->drawIndexed(quint32(quads * 6));
            };
            cb->setGraphicsPipeline(m_solid.get());
            cb->setViewport(QRhiViewport(0, 0, size.width(), size.height()));
            cb->setShaderResources(m_bindings[0].get());
            for (const auto &gpu : std::as_const(m_chunks))
                draw(gpu.solid, gpu.solidQuads);
            draw(m_markerBuffer, int(m_markers.size() / 4));
            if (!m_mobs.empty()) {
                cb->setShaderResources(m_entityBindings.get());
                draw(m_mobBuffer, int(m_mobs.size() / 4));
            }
            cb->setGraphicsPipeline(m_translucent.get());
            cb->setViewport(QRhiViewport(0, 0, size.width(), size.height()));
            cb->setShaderResources(m_bindings[1].get());
            for (const auto &gpu : std::as_const(m_chunks))
                draw(gpu.translucent, gpu.translucentQuads);
        }
        cb->endPass();
    }

private:
    struct GpuChunk
    {
        QRhiBuffer *solid = nullptr;
        QRhiBuffer *translucent = nullptr;
        int solidQuads = 0, translucentQuads = 0;
    };

    static void release(GpuChunk &gpu)
    {
        if (gpu.solid)
            gpu.solid->deleteLater();
        if (gpu.translucent)
            gpu.translucent->deleteLater();
        gpu = {};
    }
    void releaseChunks()
    {
        for (auto &gpu : m_chunks)
            release(gpu);
        m_chunks.clear();
    }

    std::unique_ptr<QRhiTexture> m_atlas;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiBuffer> m_frames[2];
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings[2];
    std::unique_ptr<QRhiGraphicsPipeline> m_solid, m_translucent;
    QList<quint32> m_format;
    int m_samples = 0;
    QRhiBuffer *m_indices = nullptr;
    qsizetype m_indexQuads = 0;
    QRhiBuffer *m_markerBuffer = nullptr;
    QRhiBuffer *m_mobBuffer = nullptr;
    std::unique_ptr<QRhiTexture> m_entityAtlas;
    std::unique_ptr<QRhiShaderResourceBindings> m_entityBindings;
    QImage m_entitySkins; // to upload
    int m_entityRevision = 0;
    QHash<qint64, GpuChunk> m_chunks;

    QList<std::shared_ptr<ChunkMesh>> m_uploads;
    QList<qint64> m_drops;
    bool m_dropAll = false;
    std::shared_ptr<BlockModels> m_models;
    int m_atlasRevision = 0;
    std::vector<Vertex> m_markers, m_mobs;
    bool m_markersDirty = false;
    QMatrix4x4 m_viewProjection;
    QColor m_background;
};

WorldView3D::WorldView3D(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
    setSampleCount(4);
    m_pool.setMaxThreadCount(qMax(1, QThread::idealThreadCount() - 1));
    m_resetTimer.setSingleShot(true);
    m_resetTimer.setInterval(0);
    connect(&m_resetTimer, &QTimer::timeout, this, &WorldView3D::reset);
    m_scheduleTimer.setSingleShot(true);
    m_scheduleTimer.setInterval(60);
    connect(&m_scheduleTimer, &QTimer::timeout, this, &WorldView3D::schedule);
    if (s_link) {
        connect(s_link, &OmniLink::blocksReceived, this, &WorldView3D::receiveBlocks);
        connect(s_link, &OmniLink::modelReceived, this, &WorldView3D::rebuildMarkers);
    }
}

WorldView3D::~WorldView3D()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void WorldView3D::setDataDirectory(const QString &path)
{
    s_dataDirectory = path;
    EntityModels::shared().setFolder(QDir(path).filePath(QStringLiteral("cache/worlds/entities")));
}

void WorldView3D::setLink(OmniLink *link)
{
    s_link = link;
    // Kept whether a map is open or not; an open map redraws its mobs after this (its
    // connection comes later).
    QObject::connect(link, &OmniLink::modelReceived, link,
                     [](const QString &, const QByteArray &payload) { EntityModels::shared().add(payload, true); });
}

void WorldView3D::setLive(bool live)
{
    if (live == m_live)
        return;
    m_live = live;
    emit liveChanged();
    if (!m_source)
        return;
    // Off: back to what was saved, chunk by chunk as their meshes are made again.
    if (live && s_link)
        applyLive(s_link->blocks(m_dimension).values());
    else
        m_source->clearLive();
    m_scheduleTimer.start();
}

void WorldView3D::receiveBlocks(const QString &dimension, const QByteArray &payload)
{
    if (m_live && dimension == m_dimension && m_source)
        applyLive({payload});
}

// Read on the workers; each chunk is made again with its neighbours, whose edges
// it shades.
void WorldView3D::applyLive(const QList<QByteArray> &payloads)
{
    if (payloads.isEmpty())
        return;
    const int generation = m_generation;
    const auto dimension = m_dimension;
    m_pool.start([this, source = m_source, models = m_models, payloads, generation, dimension] {
        QList<qint64> keys;
        for (const auto &payload : payloads) {
            const auto chunk = LiveChunk::parse(payload, *models);
            if (!chunk || chunk->dimension != dimension)
                continue;
            source->setLive(chunk);
            keys.append(chunkKey(chunk->cx, chunk->cz));
        }
        QMetaObject::invokeMethod(this, [this, generation, keys] {
            if (generation != m_generation)
                return;
            for (const auto key : keys) {
                const int cx = chunkX(key), cz = chunkZ(key);
                for (const auto near : {chunkKey(cx - 1, cz), chunkKey(cx + 1, cz), chunkKey(cx, cz - 1), chunkKey(cx, cz + 1)}) {
                    const auto it = m_chunks.find(near);
                    if (it != m_chunks.end() && !it->running)
                        it->stamp = 0;
                }
            }
            m_scheduleTimer.start();
        }, Qt::QueuedConnection);
    });
}

QQuickRhiItemRenderer *WorldView3D::createRenderer()
{
    return new WorldViewRenderer;
}

void WorldView3D::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickRhiItem::geometryChange(newGeometry, oldGeometry);
    m_scheduleTimer.start();
}

void WorldView3D::setWorldPath(const QString &path)
{
    if (path == m_worldPath)
        return;
    m_worldPath = path;
    emit sceneChanged();
    m_resetTimer.start();
}

void WorldView3D::setDimension(const QString &dimension)
{
    if (dimension == m_dimension)
        return;
    m_dimension = dimension;
    emit sceneChanged();
    m_resetTimer.start();
}

void WorldView3D::setInstance(const QString &instance)
{
    if (instance == m_instance)
        return;
    m_instance = instance;
    emit sceneChanged();
    m_resetTimer.start();
}

void WorldView3D::setVersion(const QString &version)
{
    if (version == m_version)
        return;
    m_version = version;
    emit sceneChanged();
    m_resetTimer.start();
}

void WorldView3D::setCut(int cut)
{
    if (cut == m_cut)
        return;
    m_cut = cut;
    emit cutChanged();
    m_scheduleTimer.start();
}

void WorldView3D::setYaw(qreal yaw)
{
    if (qFuzzyCompare(yaw, m_yaw))
        return;
    m_yaw = yaw;
    emit cameraChanged();
    update();
    m_scheduleTimer.start();
}

void WorldView3D::setZoom(qreal zoom)
{
    zoom = qBound(1.5, zoom, 64.0);
    if (qFuzzyCompare(zoom, m_zoom))
        return;
    m_zoom = zoom;
    emit cameraChanged();
    update();
    m_scheduleTimer.start();
}

void WorldView3D::setTarget(const QVector3D &target)
{
    if (target == m_target)
        return;
    m_target = target;
    emit cameraChanged();
    update();
    m_scheduleTimer.start();
}

void WorldView3D::setEntities(const QVariantList &entities)
{
    m_entities = entities;
    rebuildMarkers();
    emit markersChanged();
}

void WorldView3D::setPlayer(const QVariantMap &player)
{
    m_player = player;
    rebuildMarkers();
    emit markersChanged();
}

void WorldView3D::setBackground(const QColor &color)
{
    if (color == m_background)
        return;
    m_background = color;
    emit backgroundChanged();
    update();
}

QVector3D WorldView3D::eyeDirection() const
{
    const auto p = qDegreesToRadians(Pitch), y = qDegreesToRadians(m_yaw);
    return QVector3D(float(std::cos(p) * std::sin(y)), float(std::sin(p)), float(std::cos(p) * std::cos(y)));
}

QVector3D WorldView3D::screenRight() const
{
    const auto y = qDegreesToRadians(m_yaw);
    return QVector3D(float(std::cos(y)), 0, float(-std::sin(y)));
}

QVector3D WorldView3D::screenUp() const
{
    return QVector3D::crossProduct(screenRight(), -eyeDirection());
}

// The point at `height` that pixel (x, y) of the view shows.
QVector3D WorldView3D::ground(qreal x, qreal y, qreal level) const
{
    const auto eye = eyeDirection();
    const auto p = m_target + screenRight() * float((x - width() / 2) / m_zoom) - screenUp() * float((y - height() / 2) / m_zoom);
    const float t = (p.y() - float(level)) / eye.y();
    return p - eye * t;
}

QMatrix4x4 WorldView3D::viewProjection() const
{
    const float w = float(width() / (2 * m_zoom)), h = float(height() / (2 * m_zoom));
    QMatrix4x4 projection;
    projection.ortho(-w, w, -h, h, 1, 6000);
    const QVector3D target = m_target - QVector3D(float(m_originX), 0, float(m_originZ));
    QMatrix4x4 view;
    view.lookAt(target + eyeDirection() * 3000, target, QVector3D(0, 1, 0));
    return projection * view;
}

void WorldView3D::pan(qreal dx, qreal dy)
{
    const auto y = qDegreesToRadians(m_yaw);
    const QVector3D forward(float(-std::sin(y)), 0, float(-std::cos(y)));
    setTarget(m_target - screenRight() * float(dx / m_zoom) + forward * float(dy / (m_zoom * std::sin(qDegreesToRadians(Pitch)))));
}

void WorldView3D::zoomAt(qreal factor, qreal x, qreal y)
{
    const auto before = ground(x, y, m_target.y());
    setZoom(m_zoom * factor);
    const auto after = ground(x, y, m_target.y());
    setTarget(m_target + (before - after));
}

void WorldView3D::refresh()
{
    m_headers.clear();
    schedule();
}

void WorldView3D::reset()
{
    ++m_generation;
    m_pool.clear();
    m_queue.clear();
    m_running = 0;
    m_chunks.clear();
    m_headers.clear();
    m_wanted.clear();
    m_uploads.clear();
    m_drops.clear();
    m_dropAll = true;
    m_source.reset();
    update();
    if (m_worldPath.isEmpty() || m_version.isEmpty())
        return;

    // Vertices are kept near this point, where floats are precise.
    m_originX = int(std::floor(m_target.x() / 16)) * 16;
    m_originZ = int(std::floor(m_target.z() / 16)) * 16;
    rebuildMarkers();
    const auto folder = WorldMap::dimensionFolder(m_worldPath, m_dimension);
    // The game folder (instances/<id>/saves/<world> without one): its mods bring their
    // own blocks.
    const auto instance = m_instance.isEmpty() ? QDir::cleanPath(m_worldPath + "/../..") : m_instance;
    const auto key = m_version + '|' + instance;
    if (key == m_modelsKey && m_models) {
        m_source = std::make_shared<ChunkSource>(m_models, folder);
        if (m_live && s_link)
            applyLive(s_link->blocks(m_dimension).values());
        schedule();
        return;
    }
    const int generation = m_generation;
    const auto version = m_version;
    ++m_running;
    emit pendingChanged();
    m_pool.start([this, generation, version, instance, folder, key] {
        auto models = std::make_shared<BlockModels>(s_dataDirectory, version, instance);
        QMetaObject::invokeMethod(this, [this, generation, models, folder, key] {
            if (generation != m_generation)
                return;
            --m_running;
            m_models = models;
            m_modelsKey = key;
            m_source = std::make_shared<ChunkSource>(models, folder);
            if (m_live && s_link)
                applyLive(s_link->blocks(m_dimension).values());
            schedule();
        }, Qt::QueuedConnection);
    });
}

const QByteArray &WorldView3D::header(int rx, int rz)
{
    const auto key = regionKey(rx, rz);
    auto it = m_headers.find(key);
    if (it == m_headers.end()) {
        const auto folder = WorldMap::dimensionFolder(m_worldPath, m_dimension);
        it = m_headers.insert(key, WorldData::regionHeader(QDir(folder).filePath(QStringLiteral("r.%1.%2.mca").arg(rx).arg(rz))));
    }
    return *it;
}

void WorldView3D::schedule()
{
    if (!m_source || width() <= 0 || height() <= 0)
        return;
    // What the view covers, from a little below the middle of it to well above.
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const qreal level : {qreal(m_target.y()) - 24, qreal(m_target.y()) + 64})
        for (const auto &corner : {QPointF(0, 0), QPointF(width(), 0), QPointF(0, height()), QPointF(width(), height())}) {
            const auto p = ground(corner.x(), corner.y(), level);
            minX = qMin(minX, p.x()), maxX = qMax(maxX, p.x());
            minZ = qMin(minZ, p.z()), maxZ = qMax(maxZ, p.z());
        }
    const auto middle = ground(width() / 2, height() / 2, m_target.y());
    const int x0 = int(std::floor(minX / 16)) - 1, x1 = int(std::floor(maxX / 16)) + 1;
    const int z0 = int(std::floor(minZ / 16)) - 1, z1 = int(std::floor(maxZ / 16)) + 1;
    const int mx = int(std::floor(middle.x() / 16)), mz = int(std::floor(middle.z() / 16));
    const auto distance = [mx, mz](qint64 key) {
        const int dx = chunkX(key) - mx, dz = chunkZ(key) - mz;
        return dx * dx + dz * dz;
    };

    struct Candidate
    {
        qint64 key;
        int distance;
        quint32 stamp;
        bool inView;
    };
    std::vector<Candidate> candidates;
    // A live chunk counts as changed with each version, and may not be saved yet at all.
    const auto live = m_source->liveVersions();
    for (int cz = qMax(z0 - Preload, mz - 72); cz <= qMin(z1 + Preload, mz + 72); ++cz)
        for (int cx = qMax(x0 - Preload, mx - 72); cx <= qMin(x1 + Preload, mx + 72); ++cx) {
            const auto &h = header(cx >> 5, cz >> 5);
            const int index = (cz & 31) * 32 + (cx & 31);
            const auto key = chunkKey(cx, cz);
            const auto version = live.value(key);
            const bool saved = WorldData::hasChunk(h, index);
            if (saved || version)
                candidates.push_back({key, distance(key), ChunkSource::stampWith(saved ? WorldData::chunkStamp(h, index) : 0, version),
                                      cx >= x0 && cx <= x1 && cz >= z0 && cz <= z1});
        }
    // What is in view first, nearest the middle first; then the ring around it.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        return a.inView != b.inView ? a.inView : a.distance < b.distance;
    });
    const auto inView = size_t(std::count_if(candidates.begin(), candidates.end(), [](const Candidate &c) { return c.inView; }));
    candidates.resize(qMin(candidates.size(), qMin(inView, size_t(MaxChunks)) + size_t(MaxPreload)));

    m_wanted.clear();
    m_queue.clear();
    for (const auto &candidate : candidates) {
        m_wanted.insert(candidate.key);
        const auto state = m_chunks.value(candidate.key);
        if (state.running)
            continue;
        const bool current = state.cut == m_cut && state.stamp == candidate.stamp && m_chunks.contains(candidate.key);
        if (!current)
            m_queue.append(candidate.key);
    }
    // Out of view, chunks stay as they were made; one made with another height cut
    // would show the wrong blocks, so it goes. Past the budget, the farthest go too.
    std::vector<std::pair<int, qint64>> kept;
    for (auto it = m_chunks.begin(); it != m_chunks.end();) {
        if (it->running || m_wanted.contains(it.key())) {
            ++it;
        } else if (it->cut != m_cut) {
            if (it->drawn)
                m_drops.append(it.key());
            it = m_chunks.erase(it);
        } else {
            kept.emplace_back(distance(it.key()), it.key());
            ++it;
        }
    }
    qint64 quads = 0;
    for (const auto &state : std::as_const(m_chunks))
        quads += state.quads;
    if (quads > KeepQuads) {
        std::sort(kept.begin(), kept.end(), std::greater<>());
        for (const auto &entry : kept) {
            if (quads <= KeepQuads)
                break;
            const auto key = entry.second;
            const auto state = m_chunks.take(key);
            quads -= state.quads;
            if (state.drawn)
                m_drops.append(key);
        }
    }
    startJobs();
    emit pendingChanged();
    update();
}

void WorldView3D::startJobs()
{
    const int limit = m_pool.maxThreadCount() * 2;
    while (m_running < limit && !m_queue.isEmpty()) {
        const auto key = m_queue.takeFirst();
        auto &state = m_chunks[key];
        if (state.running)
            continue;
        state.running = true;
        ++m_running;
        const int generation = m_generation, cut = m_cut, originX = m_originX, originZ = m_originZ;
        m_pool.start([this, source = m_source, key, generation, cut, originX, originZ] {
            const int cx = chunkX(key), cz = chunkZ(key);
            auto mesh = source->mesh(cx, cz, cut, originX, originZ);
            const auto blocks = mesh ? nullptr : source->chunk(cx, cz);
            const quint32 stamp = mesh ? mesh->stamp : blocks ? blocks->stamp : 0;
            QMetaObject::invokeMethod(this, [this, key, generation, mesh, stamp, cut] {
                finished(key, generation, mesh, stamp, cut);
            }, Qt::QueuedConnection);
        });
    }
}

void WorldView3D::finished(qint64 key, int generation, std::shared_ptr<ChunkMesh> mesh, quint32 stamp, int cut)
{
    if (generation != m_generation)
        return;
    --m_running;
    auto it = m_chunks.find(key);
    if (it == m_chunks.end())
        it = m_chunks.insert(key, {});
    it->running = false;
    it->stamp = stamp;
    it->cut = cut;
    it->quads = mesh ? int((mesh->solid.size() + mesh->translucent.size()) / 4) : 0;
    if (mesh) {
        m_uploads.append(mesh);
        it->drawn = true;
    } else if (it->drawn) {
        m_drops.append(key);
        it->drawn = false;
    }
    // A newer cut or save came while it was being made: once more.
    if (cut != m_cut && m_wanted.contains(key))
        m_queue.prepend(key);
    startJobs();
    emit pendingChanged();
    update();
}

void WorldView3D::rebuildMarkers()
{
    m_markers.clear();
    m_mobs.clear();
    const float u = float(BlockModels::Cell / 2) / BlockModels::AtlasSize; // cell 0: white
    const auto box = [this, u](QVector3D at, float half, float tall, QColor color) {
        const float x0 = at.x() - m_originX - half, x1 = at.x() - m_originX + half;
        const float z0 = at.z() - m_originZ - half, z1 = at.z() - m_originZ + half;
        const float y0 = at.y(), y1 = at.y() + tall;
        const float corners[6][4][3] = {
            {{x0, y0, z1}, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}}, {{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}},
            {{x1, y1, z0}, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}}, {{x0, y1, z1}, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}},
            {{x0, y1, z0}, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}}, {{x1, y1, z1}, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}}};
        const float shades[6] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};
        for (int f = 0; f < 6; ++f) {
            const auto c = [&](int v) { return quint32(qBound(0, int(v * shades[f]), 255)); };
            const quint32 packed = c(color.red()) | (c(color.green()) << 8) | (c(color.blue()) << 16) | 0xFF000000u;
            for (const auto &corner : corners[f])
                m_markers.push_back({corner[0], corner[1], corner[2], u, u, packed});
        }
    };
    for (const auto &value : std::as_const(m_entities)) {
        const auto entity = value.toMap();
        const QVector3D at(entity.value("x").toFloat(), entity.value("y").toFloat(), entity.value("z").toFloat());
        if (const auto model = EntityModels::shared().find(entity.value("id").toString())) {
            // As the game draws it: turned by 180 degrees less its body's yaw, the model's
            // pixels flipped (x and y) and hung 1.501 blocks above the feet.
            const float turn = qDegreesToRadians(180.f - entity.value("yaw").toFloat());
            const float c = std::cos(turn), s = std::sin(turn);
            const auto place = [&](const QVector3D &p) {
                const float x = -p.x() / 16, y = 1.501f - p.y() / 16, z = p.z() / 16;
                return QVector3D(at.x() - m_originX + x * c + z * s, at.y() + y, at.z() - m_originZ - x * s + z * c);
            };
            const auto &corners = model->corners;
            for (size_t q = 0; q + 3 < corners.size(); q += 4) {
                QVector3D world[4];
                for (int i = 0; i < 4; ++i)
                    world[i] = place(corners[q + i].position);
                // Shaded by where the face looks, as blocks are.
                const auto normal = QVector3D::crossProduct(world[2] - world[0], world[3] - world[1]).normalized();
                const float shade = normal.y() > 0.5f ? 1.f
                                    : normal.y() < -0.5f ? 0.5f
                                    : std::abs(normal.z()) > std::abs(normal.x()) ? 0.8f : 0.6f;
                const auto grey = quint32(shade * 255);
                const quint32 packed = grey | (grey << 8) | (grey << 16) | 0xFF000000u;
                for (int i = 0; i < 4; ++i)
                    m_mobs.push_back({world[i].x(), world[i].y(), world[i].z(), corners[q + i].u, corners[q + i].v, packed});
            }
            continue;
        }
        box(at, 0.35f, 0.9f, entity.value("hostile").toBool() ? QColor(232, 88, 78) : QColor(127, 211, 107));
    }
    if (m_player.contains("x"))
        box(QVector3D(m_player.value("x").toFloat(), m_player.value("y").toFloat(), m_player.value("z").toFloat()), 0.4f, 1.8f,
            QColor(255, 214, 92));
    m_markersDirty = true;
    update();
}
