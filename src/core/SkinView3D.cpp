#include "SkinView3D.h"

#include "WorldData.h"

#include <QCryptographicHash>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMatrix4x4>
#include <QPainter>
#include <QQuickWindow>
#include <QUrl>
#include <QtMath>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>

namespace {

QString s_dataDirectory;

// The camera: this far in front of the player and a little above the ledge, looking
// level (verticals stay vertical, the ledge's top shows as a thin face).
constexpr float Distance = 40.f;
constexpr float Focus = 4.f; // depth (z) of the plane `unit` is measured at
constexpr float Elevation = 12.f;
constexpr float CameraZ = Focus + Distance;
constexpr float CameraY = SkinView3D::LedgeTop + Elevation;
constexpr int MaxQuads = 96;
// The texture: the skin (64×64) over the cape (64×32).
constexpr float AtlasHeight = 96;

// Where the camera is: its height, and the pixels from the item's top to its horizon.
// Leaning (stand 0), the ledge's front edge falls at `ledge`; standing (1), the player's
// middle is the item's.
struct Camera
{
    float y;
    qreal horizon;
};

constexpr float ShowcaseMiddle = 4.f; // feet at -12, head's top at 20
constexpr float ShowcaseElevation = 7.f;

Camera camera(qreal stand, qreal ledge, qreal unit, qreal height)
{
    const qreal leanY = CameraY, leanHorizon = ledge - Elevation * unit * Distance / (CameraZ - SkinView3D::LedgeFront);
    const qreal standY = ShowcaseMiddle + ShowcaseElevation, standHorizon = height / 2 - ShowcaseElevation * unit * Distance / CameraZ;
    return {float(leanY + (standY - leanY) * stand), leanHorizon + (standHorizon - leanHorizon) * stand};
}

QPointF project(const QVector3D &p, const Camera &camera, qreal unit, qreal width)
{
    const qreal scale = unit * Distance / (CameraZ - p.z());
    return {width / 2 + p.x() * scale, camera.horizon - (p.y() - camera.y) * scale};
}

struct Vertex
{
    float x, y, z;
    float u, v;
    quint32 color;
};

QShader loadShader(const QString &name)
{
    QFile file(name);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
}

// The game's own conversion of a 64×32 skin: the left arm and leg made from the right
// ones, each face flipped.
QImage fromLegacy(const QImage &legacy)
{
    QImage skin(64, 64, QImage::Format_RGBA8888);
    skin.fill(Qt::transparent);
    {
        QPainter painter(&skin);
        painter.drawImage(0, 0, legacy);
    }
    const auto copy = [&](int x, int y, int dx, int dy, int w, int h) {
        const auto part = skin.copy(x, y, w, h).mirrored(true, false);
        QPainter painter(&skin);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(x + dx, y + dy, part);
    };
    copy(4, 16, 16, 32, 4, 4);
    copy(8, 16, 16, 32, 4, 4);
    copy(0, 20, 24, 32, 4, 12);
    copy(4, 20, 16, 32, 4, 12);
    copy(8, 20, 8, 32, 4, 12);
    copy(12, 20, 16, 32, 4, 12);
    copy(44, 16, -8, 32, 4, 4);
    copy(48, 16, -8, 32, 4, 4);
    copy(40, 20, 0, 32, 4, 12);
    copy(44, 20, -8, 32, 4, 12);
    copy(48, 20, -16, 32, 4, 12);
    copy(52, 20, -8, 32, 4, 12);
    return skin;
}

// Alex's arms leave the last two columns of the right arm's texture empty.
bool isSlim(const QImage &skin)
{
    for (int y = 20; y < 32; ++y)
        for (int x = 54; x < 56; ++x)
            if (qAlpha(skin.pixel(x, y)) != 0)
                return false;
    return true;
}

// As the game does: the inner layer is opaque whatever the file says.
void setNoAlpha(QImage &skin)
{
    for (const auto &area : {QRect(0, 0, 32, 16), QRect(0, 16, 64, 16), QRect(16, 48, 32, 16)})
        for (int y = area.top(); y <= area.bottom(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(skin.scanLine(y));
            for (int x = area.left(); x <= area.right(); ++x)
                line[x] |= 0xff000000u;
        }
}

// An offline player's default skin, as the game picks it: from the hash of the UUID the
// game makes of "OfflinePlayer:<name>".
bool defaultIsAlex(const QString &name)
{
    auto md5 = QCryptographicHash::hash(("OfflinePlayer:" + name).toUtf8(), QCryptographicHash::Md5);
    md5[6] = char((md5[6] & 0x0f) | 0x30);
    md5[8] = char((md5[8] & 0x3f) | 0x80);
    quint64 msb = 0, lsb = 0;
    for (int i = 0; i < 8; ++i) {
        msb = (msb << 8) | quint8(md5[i]);
        lsb = (lsb << 8) | quint8(md5[i + 8]);
    }
    const auto hilo = msb ^ lsb;
    const auto hash = qint32(quint32(hilo >> 32) ^ quint32(hilo));
    return (hash & 1) == 1;
}

// Steve or Alex from the newest game jar there is; a plain Steve-like stand-in without one.
QImage defaultSkin(bool alex)
{
    static QImage cache[2];
    auto &skin = cache[alex];
    if (!skin.isNull())
        return skin;
    QList<QFileInfo> jars;
    const QDir versions(QDir(s_dataDirectory).filePath(QStringLiteral("versions")));
    for (const auto &dir : versions.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QFileInfo jar(versions.filePath(dir + '/' + dir + ".jar"));
        if (jar.exists())
            jars.append(jar);
    }
    std::sort(jars.begin(), jars.end(), [](const QFileInfo &a, const QFileInfo &b) { return a.lastModified() > b.lastModified(); });
    const QStringList paths = alex
        ? QStringList{QStringLiteral("assets/minecraft/textures/entity/player/slim/alex.png"), QStringLiteral("assets/minecraft/textures/entity/alex.png")}
        : QStringList{QStringLiteral("assets/minecraft/textures/entity/player/wide/steve.png"), QStringLiteral("assets/minecraft/textures/entity/steve.png")};
    for (const auto &info : std::as_const(jars).first(qMin<qsizetype>(jars.size(), 6))) {
        WorldData::Jar jar(info.filePath());
        for (const auto &path : paths) {
            const auto image = QImage::fromData(jar.read(path));
            if (image.width() == 64 && (image.height() == 64 || image.height() == 32))
                return skin = image.height() == 32 ? fromLegacy(image) : image.convertToFormat(QImage::Format_RGBA8888);
        }
    }
    skin = QImage(64, 64, QImage::Format_RGBA8888);
    skin.fill(Qt::transparent);
    QPainter painter(&skin);
    const QColor hair(0x2b, 0x1e, 0x0d), face(0xb4, 0x84, 0x6d), shirt(0x00, 0xa8, 0xa8), sleeve(0x00, 0x8f, 0x8f);
    painter.fillRect(0, 0, 32, 16, face);            // head
    painter.fillRect(8, 0, 8, 8, hair);              // top
    painter.fillRect(0, 8, 32, 2, hair);             // hairline
    painter.fillRect(9, 12, 2, 1, Qt::white);        // eyes
    painter.fillRect(10, 12, 1, 1, QColor(0x52, 0x3d, 0x89));
    painter.fillRect(13, 12, 2, 1, Qt::white);
    painter.fillRect(13, 12, 1, 1, QColor(0x52, 0x3d, 0x89));
    painter.fillRect(16, 16, 24, 16, shirt);         // body
    for (const auto &arm : {QPoint(40, 16), QPoint(32, 48)}) {
        painter.fillRect(arm.x(), arm.y(), 16, 16, face);
        painter.fillRect(arm.x(), arm.y() + 4, 16, 5, sleeve);
    }
    return skin;
}

// A box with the game's texture layout: the strip of faces right, front, left, back under
// the top and bottom, from (u, v), for a box w × h × d texels (v counts down the whole
// texture, the cape's from 64).
void addBox(std::vector<Vertex> &out, const QMatrix4x4 &m, QVector3D from, QVector3D to, float inflate,
            int u, int v, int w, int h, int d)
{
    from -= QVector3D(inflate, inflate, inflate);
    to += QVector3D(inflate, inflate, inflate);
    const float x0 = from.x(), y0 = from.y(), z0 = from.z(), x1 = to.x(), y1 = to.y(), z1 = to.z();
    // Light from above, to the viewer's right and in front.
    static const QVector3D light = QVector3D(0.45f, 0.85f, 0.55f).normalized();
    const auto face = [&](QVector3D a, QVector3D b, QVector3D c, QVector3D e, QVector3D normal,
                          float u0, float v0, float u1, float v1) {
        const auto n = m.mapVector(normal).normalized();
        const float shade = std::clamp(0.6f + 0.4f * QVector3D::dotProduct(n, light) + 0.08f * qMax(0.f, n.y()), 0.45f, 1.f);
        const auto level = quint32(std::lround(shade * 255));
        const quint32 color = level | (level << 8) | (level << 16) | 0xff000000u;
        // A hair inside each texel edge, so nearest sampling never reaches the next face.
        constexpr float inset = 0.01f;
        u0 += u0 < u1 ? inset : -inset;
        u1 += u0 < u1 ? -inset : inset;
        v0 += inset;
        v1 -= inset;
        const QVector3D corners[4] = {m.map(a), m.map(b), m.map(c), m.map(e)};
        const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        for (int i = 0; i < 4; ++i)
            out.push_back({corners[i].x(), corners[i].y(), corners[i].z(), uv[i][0] / 64.f, uv[i][1] / AtlasHeight, color});
    };
    // Corners top-left, top-right, bottom-right, bottom-left as the face is seen.
    face({x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}, {0, 0, 1}, u + d, v + d, u + d + w, v + d + h);             // front
    face({x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}, {0, 0, -1}, u + 2 * d + w, v + d, u + 2 * d + 2 * w, v + d + h); // back
    face({x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}, {-1, 0, 0}, u, v + d, u + d, v + d + h);                     // right
    face({x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}, {1, 0, 0}, u + d + w, v + d, u + 2 * d + w, v + d + h);     // left
    face({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, {0, 1, 0}, u + d, v, u + d + w, v + d);                     // top
    face({x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}, {0, -1, 0}, u + d + w, v, u + d + 2 * w, v + d);           // bottom
}

void buildPlayer(std::vector<Vertex> &out, const SkinView3D::Pose &p, bool slim)
{
    QMatrix4x4 root;
    root.translate(0, float(p.rise), float(p.advance));
    root.rotate(float(p.turn), 0, 1, 0);
    root.rotate(float(p.tilt), 0, 0, 1);
    QMatrix4x4 body = root;
    body.rotate(float(p.bodyRoll), 0, 0, 1);
    body.rotate(float(p.bodyPitch), 1, 0, 0);
    addBox(out, body, {-4, 0, -2}, {4, 12, 2}, 0, 16, 16, 8, 12, 4);
    addBox(out, body, {-4, 0, -2}, {4, 12, 2}, 0.25f, 16, 32, 8, 12, 4);

    QMatrix4x4 head = body;
    head.translate(0, 12, 0);
    head.rotate(float(p.headYaw), 0, 1, 0);
    head.rotate(float(p.headPitch), 1, 0, 0);
    head.rotate(float(p.headRoll), 0, 0, 1);
    addBox(out, head, {-4, 0, -4}, {4, 8, 4}, 0, 0, 0, 8, 8, 8);
    addBox(out, head, {-4, 0, -4}, {4, 8, 4}, 0.5f, 32, 0, 8, 8, 8);

    const int width = slim ? 3 : 4;
    const float pivotY = slim ? 9.5f : 10.f;
    for (const bool right : {true, false}) {
        QMatrix4x4 arm = body;
        const float side = right ? -1.f : 1.f;
        arm.translate(5 * side, pivotY, 0);
        arm.rotate(float(right ? p.rightArmRoll : p.leftArmRoll) * -side, 0, 0, 1);
        arm.rotate(float(right ? p.rightArmYaw : p.leftArmYaw) * -side, 0, 1, 0);
        arm.rotate(float(-(right ? p.rightArmPitch : p.leftArmPitch)), 1, 0, 0);
        const QVector3D from(right ? -width + 1.f : -1.f, -10, -2), to(right ? 1.f : width - 1.f, 2, 2);
        addBox(out, arm, from, to, 0, right ? 40 : 32, right ? 16 : 48, width, 12, 4);
        addBox(out, arm, from, to, 0.25f, right ? 40 : 48, right ? 32 : 48, width, 12, 4);
    }

    for (const bool right : {true, false}) {
        QMatrix4x4 leg = root;
        leg.translate(right ? -2.f : 2.f, 0, 0);
        leg.rotate(float(-(right ? p.rightLegPitch : p.leftLegPitch)), 1, 0, 0);
        addBox(out, leg, {-2, -12, -2}, {2, 0, 2}, 0, right ? 0 : 16, right ? 16 : 48, 4, 12, 4);
        addBox(out, leg, {-2, -12, -2}, {2, 0, 2}, 0.25f, 0, right ? 32 : 48, 4, 12, 4);
    }

    if (p.cape) {
        // Hung from the shoulders, its outside (the texture's left half) facing back.
        QMatrix4x4 cape = body;
        cape.translate(0, 12, -2);
        cape.rotate(float(p.capeSwing), 1, 0, 0);
        cape.rotate(180, 0, 1, 0);
        addBox(out, cape, {-5, -16, 0}, {5, 0, 1}, 0, 0, 64, 10, 16, 1);
    }
}

} // namespace

// The GPU side: the player rebuilt each frame (a few dozen quads) behind a depth-only
// wall, the ledge, that hides what is below it.
class SkinViewRenderer : public QQuickRhiItemRenderer
{
protected:
    void initialize(QRhiCommandBuffer *) override
    {
        auto *r = rhi();
        if (!m_texture) {
            m_texture.reset(r->newTexture(QRhiTexture::RGBA8, QSize(64, int(AtlasHeight))));
            m_texture->create();
            m_sampler.reset(r->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
            m_sampler->create();
            m_frame.reset(r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
            m_frame->create();
            m_vertices.reset(r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, MaxQuads * 4 * sizeof(Vertex)));
            m_vertices->create();
            m_indices.reset(r->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, MaxQuads * 6 * sizeof(quint16)));
            m_indices->create();
            m_bindings.reset(r->newShaderResourceBindings());
            m_bindings->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                                         m_frame.get()),
                QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_texture.get(), m_sampler.get()),
            });
            m_bindings->create();
            m_indicesPending = true;
        }
        const auto format = renderTarget()->renderPassDescriptor()->serializedFormat();
        if (m_model && format == m_format && m_samples == renderTarget()->sampleCount())
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
        for (const bool wall : {true, false}) {
            auto &pipeline = wall ? m_wallPipeline : m_model;
            pipeline.reset(r->newGraphicsPipeline());
            pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
            pipeline->setVertexInputLayout(layout);
            pipeline->setShaderResourceBindings(m_bindings.get());
            pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            pipeline->setSampleCount(m_samples);
            pipeline->setDepthTest(true);
            pipeline->setDepthWrite(true);
            pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            pipeline->setCullMode(QRhiGraphicsPipeline::None);
            if (wall) {
                QRhiGraphicsPipeline::TargetBlend blend;
                blend.colorWrite = {};
                pipeline->setTargetBlends({blend});
            }
            pipeline->create();
        }
    }

    void synchronize(QQuickRhiItem *rhiItem) override
    {
        auto *item = static_cast<SkinView3D *>(rhiItem);
        if (item->m_skinDirty) {
            item->m_skinDirty = false;
            m_skin = item->m_skin;
        }
        m_pose = item->pose();
        m_slim = item->slim();
        m_ledge = float(item->m_ledge);
        m_stand = float(item->m_stand);
        m_wall = item->m_wall;
        m_unit = float(qMax<qreal>(0.5, item->m_unit));
        m_dpr = float(item->window() ? item->window()->effectiveDevicePixelRatio() : 1);
    }

    void render(QRhiCommandBuffer *cb) override
    {
        auto *u = rhi()->nextResourceUpdateBatch();
        if (m_indicesPending) {
            m_indicesPending = false;
            std::vector<quint16> indices;
            for (int q = 0; q < MaxQuads; ++q) {
                const auto base = quint16(q * 4);
                indices.insert(indices.end(), {base, quint16(base + 1), quint16(base + 2), base, quint16(base + 2), quint16(base + 3)});
            }
            u->uploadStaticBuffer(m_indices.get(), indices.data());
        }
        if (!m_skin.isNull()) {
            u->uploadTexture(m_texture.get(), m_skin);
            m_skin = QImage();
        }

        // The ledge first, its front and top: depth only, so the player behind it is never
        // drawn. Its texel is the face's, always opaque.
        std::vector<Vertex> vertices;
        vertices.reserve(MaxQuads * 4);
        constexpr float far = 400, top = SkinView3D::LedgeTop, front = SkinView3D::LedgeFront, back = SkinView3D::LedgeBack;
        for (const auto &corner : {QVector3D(-far, top, front), QVector3D(far, top, front), QVector3D(far, -far, front), QVector3D(-far, -far, front),
                                   QVector3D(-far, top, back), QVector3D(far, top, back), QVector3D(far, top, front), QVector3D(-far, top, front)})
            vertices.push_back({corner.x(), corner.y(), corner.z(), 12.5f / 64, 12.5f / AtlasHeight, 0xffffffffu});
        buildPlayer(vertices, m_pose, m_slim);
        const auto quads = int(qMin<size_t>(vertices.size() / 4, MaxQuads));
        u->updateDynamicBuffer(m_vertices.get(), 0, quint32(quads * 4 * sizeof(Vertex)), vertices.data());

        // Framed off-axis around the horizon, so the ledge's front edge falls at `ledge` px.
        const auto size = renderTarget()->pixelSize();
        const float w = float(size.width()) / m_dpr, h = float(size.height()) / m_dpr;
        const auto eye = camera(m_stand, m_ledge, m_unit, h);
        const auto above = float(eye.horizon);
        constexpr float near = 4;
        const float scale = near / (Distance * m_unit);
        QMatrix4x4 projection;
        projection.frustum(-w / 2 * scale, w / 2 * scale, -(h - above) * scale, above * scale, near, 200);
        QMatrix4x4 view;
        view.translate(0, -eye.y, -CameraZ);
        const auto mvp = rhi()->clipSpaceCorrMatrix() * projection * view;
        const float params[4] = {0.5f, 1.f, 0, 0};
        u->updateDynamicBuffer(m_frame.get(), 0, 64, mvp.constData());
        u->updateDynamicBuffer(m_frame.get(), 64, 16, params);

        cb->beginPass(renderTarget(), Qt::transparent, {1.0f, 0}, u);
        const QRhiCommandBuffer::VertexInput input(m_vertices.get(), 0);
        cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
        if (m_wall) {
            cb->setGraphicsPipeline(m_wallPipeline.get());
            cb->setShaderResources(m_bindings.get());
            cb->setVertexInput(0, 1, &input, m_indices.get(), 0, QRhiCommandBuffer::IndexUInt16);
            cb->drawIndexed(2 * 6);
        }
        cb->setGraphicsPipeline(m_model.get());
        cb->setShaderResources(m_bindings.get());
        cb->setVertexInput(0, 1, &input, m_indices.get(), 0, QRhiCommandBuffer::IndexUInt16);
        cb->drawIndexed(quint32((quads - 2) * 6), 1, 2 * 6);
        cb->endPass();
    }

private:
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiBuffer> m_frame, m_vertices, m_indices;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_wallPipeline, m_model;
    QVector<quint32> m_format;
    int m_samples = 0;
    bool m_indicesPending = false;
    QImage m_skin;
    SkinView3D::Pose m_pose;
    bool m_slim = false;
    float m_ledge = 0, m_unit = 6, m_dpr = 1, m_stand = 0;
    bool m_wall = true;
};

SkinView3D::SkinView3D(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
    setSampleCount(4);
    setAlphaBlending(true);
    m_ticker.setInterval(16);
    m_ticker.setTimerType(Qt::PreciseTimer);
    connect(&m_ticker, &QTimer::timeout, this, &SkinView3D::tick);
    connect(this, &SkinView3D::poseChanged, this, &QQuickItem::update);
    loadSkin();
}

void SkinView3D::setDataDirectory(const QString &path)
{
    s_dataDirectory = path;
}

void SkinView3D::setSource(const QString &source)
{
    if (source == m_source)
        return;
    m_source = source;
    emit sourceChanged();
    loadSkin();
}

void SkinView3D::setName(const QString &name)
{
    if (name == m_name)
        return;
    m_name = name;
    emit sourceChanged();
    if (m_source.isEmpty())
        loadSkin();
}

void SkinView3D::setCape(const QString &cape)
{
    if (cape == m_cape)
        return;
    m_cape = cape;
    emit sourceChanged();
    loadCape();
}

void SkinView3D::setVariant(const QString &variant)
{
    if (variant == m_variant)
        return;
    m_variant = variant;
    emit skinChanged();
    update();
}

void SkinView3D::loadSkin()
{
    QImage image;
    if (!m_source.isEmpty()) {
        const QUrl url(m_source);
        image.load(url.isLocalFile() ? url.toLocalFile() : m_source);
    }
    if (image.width() != 64 || (image.height() != 64 && image.height() != 32))
        image = defaultSkin(defaultIsAlex(m_name));
    else if (image.height() == 32)
        image = fromLegacy(image);
    m_skinImage = image.convertToFormat(QImage::Format_RGBA8888);
    m_slim = isSlim(m_skinImage);
    setNoAlpha(m_skinImage);
    compose();
}

// Capes are 64×32 (or the old 22×17, in its corner); bigger ones are scaled down.
void SkinView3D::loadCape()
{
    QImage image;
    if (!m_cape.isEmpty()) {
        const QUrl url(m_cape);
        image.load(url.isLocalFile() ? url.toLocalFile() : m_cape);
    }
    if (image.width() > 64)
        image = image.scaled(64, image.height() * 64 / image.width());
    m_capeImage = image.isNull() ? QImage() : image.convertToFormat(QImage::Format_RGBA8888);
    compose();
}

void SkinView3D::compose()
{
    m_skin = QImage(64, int(AtlasHeight), QImage::Format_RGBA8888);
    m_skin.fill(Qt::transparent);
    QPainter painter(&m_skin);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(0, 0, m_skinImage);
    if (!m_capeImage.isNull())
        painter.drawImage(0, 64, m_capeImage);
    painter.end();
    m_skinDirty = true;
    emit skinChanged();
    update();
}

SkinView3D::Pose SkinView3D::pose() const
{
    return {m_rise, m_advance, m_tilt, m_turn, m_bodyPitch, m_bodyRoll,
            m_headPitch + m_lookPitch * m_follow, m_headYaw + m_lookYaw * m_follow, m_headRoll,
            m_rightArmPitch, m_rightArmYaw, m_rightArmRoll, m_leftArmPitch, m_leftArmYaw, m_leftArmRoll,
            m_rightLegPitch, m_leftLegPitch, m_capeSwing, !m_capeImage.isNull()};
}

// The middle of the head as drawn with the body's lean (not the head's turn).
QPointF SkinView3D::headCenter() const
{
    const qreal lean = qDegreesToRadians(m_bodyPitch);
    return project(QVector3D(0, float(m_rise + 16 * std::cos(lean)), float(m_advance + 16 * std::sin(lean))),
                   camera(m_stand, m_ledge, m_unit, height()), m_unit, width());
}

qreal SkinView3D::ground() const
{
    return project(QVector3D(0, -12, 0), camera(m_stand, m_ledge, m_unit, height()), m_unit, width()).y();
}

qreal SkinView3D::groundScale() const
{
    return m_unit * Distance / CameraZ;
}

qreal SkinView3D::ledgeBack() const
{
    return project(QVector3D(0, LedgeTop, LedgeBack), camera(0, m_ledge, m_unit, height()), m_unit, width()).y();
}

void SkinView3D::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickRhiItem::itemChange(change, value);
    if (change == ItemSceneChange || change == ItemVisibleHasChanged) {
        if (window() && isVisible()) {
            m_clock.start();
            m_ticker.start();
        } else {
            m_ticker.stop();
        }
    }
}

// The head eases toward the cursor while the window is active, and back to the front
// otherwise; a frame is drawn only while it moves.
void SkinView3D::tick()
{
    const qreal dt = qMin<qreal>(0.1, m_clock.restart() / 1000.0);
    qreal yaw = 0, pitch = 0;
    if (window() && window()->isActive()) {
        const auto cursor = mapFromGlobal(QCursor::pos());
        const auto head = headCenter();
        // As if the cursor were on a plane this far in front of the head, in pixels.
        constexpr qreal depth = 170;
        // Turned with the body: the head turns from where it faces.
        const qreal world = qRadiansToDegrees(std::atan2(cursor.x() - head.x(), depth));
        yaw = std::clamp(std::remainder(world - m_turn, 360.0), -58.0, 58.0);
        pitch = std::clamp(qRadiansToDegrees(std::atan2(cursor.y() - head.y(), depth)), -40.0, 22.0);
    }
    const qreal ease = 1 - std::exp(-dt * 9);
    const qreal dy = (yaw - m_lookYaw) * ease, dp = (pitch - m_lookPitch) * ease;
    if (std::abs(dy) < 0.005 && std::abs(dp) < 0.005)
        return;
    m_lookYaw += dy;
    m_lookPitch += dp;
    if (m_follow > 0)
        update();
}

QQuickRhiItemRenderer *SkinView3D::createRenderer()
{
    return new SkinViewRenderer;
}
