#include "EntityModels.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QSaveFile>

namespace World3D {

namespace {

constexpr int Padding = 2;      // between skins, so a filtered edge doesn't take its neighbour's colour
constexpr quint32 MaxQuads = 8192;

QString fileName(const QString &id)
{
    QString name = id;
    for (auto &c : name)
        if (!c.isLetterOrNumber() && c != '_' && c != '-' && c != '.')
            c = '_';
    return name + QStringLiteral(".model");
}

} // namespace

EntityModels &EntityModels::shared()
{
    static EntityModels models;
    return models;
}

void EntityModels::setFolder(const QString &folder)
{
    if (folder == m_folder)
        return;
    m_folder = folder;
    const QDir dir(folder);
    for (const auto &name : dir.entryList({QStringLiteral("*.model")}, QDir::Files)) {
        QFile file(dir.filePath(name));
        if (file.open(QIODevice::ReadOnly))
            add(file.readAll(), false);
    }
}

bool EntityModels::add(const QByteArray &payload, bool keep)
{
    QDataStream in(payload);
    in.setByteOrder(QDataStream::BigEndian);
    in.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint16 idSize = 0;
    in >> idSize;
    QByteArray idBytes(idSize, Qt::Uninitialized);
    in.readRawData(idBytes.data(), idSize);
    const auto id = QString::fromUtf8(idBytes);
    if (m_payloads.value(id) == payload)
        return true; // already here
    quint32 pngSize = 0;
    in >> pngSize;
    if (in.status() != QDataStream::Ok || id.isEmpty() || pngSize > quint32(payload.size()))
        return false;
    QByteArray png(qsizetype(pngSize), Qt::Uninitialized);
    in.readRawData(png.data(), int(pngSize));
    quint32 quads = 0;
    in >> quads;
    QImage skin = QImage::fromData(png, "PNG");
    if (in.status() != QDataStream::Ok || skin.isNull() || quads == 0 || quads > MaxQuads)
        return false;
    const QSize size = skin.size();
    skin = skin.convertToFormat(QImage::Format_RGBA8888);
    if (skin.width() > MaxSkin || skin.height() > MaxSkin)
        skin = skin.scaled(qMin(skin.width(), MaxSkin), qMin(skin.height(), MaxSkin), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    auto model = std::make_shared<Model>();
    model->corners.resize(size_t(quads) * 4);
    float most = 0;
    for (auto &corner : model->corners) {
        float x, y, z;
        in >> x >> y >> z >> corner.u >> corner.v;
        corner.position = QVector3D(x, y, z);
        most = qMax(most, qMax(corner.u, corner.v));
    }
    if (in.status() != QDataStream::Ok)
        return false;
    QRect at;
    if (!place(skin, &at))
        return false;
    // u and v over the skin (in its pixels, should a version send them so) → the atlas.
    const bool pixels = most > 1.01f;
    for (auto &corner : model->corners) {
        const float u = pixels ? corner.u / size.width() : corner.u, v = pixels ? corner.v / size.height() : corner.v;
        corner.u = (at.x() + qBound(0.f, u, 1.f) * at.width()) / AtlasSize;
        corner.v = (at.y() + qBound(0.f, v, 1.f) * at.height()) / AtlasSize;
    }
    m_models.insert(id, model);
    m_payloads.insert(id, payload);
    if (keep && !m_folder.isEmpty() && QDir().mkpath(m_folder)) {
        QSaveFile file(QDir(m_folder).filePath(fileName(id)));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(payload);
            file.commit();
        }
    }
    return true;
}

bool EntityModels::place(const QImage &skin, QRect *at)
{
    if (m_atlas.isNull()) {
        m_atlas = QImage(AtlasSize, AtlasSize, QImage::Format_RGBA8888);
        m_atlas.fill(Qt::transparent);
    }
    const int width = skin.width() + Padding, height = skin.height() + Padding;
    if (m_shelfX + width > AtlasSize) {
        m_shelfX = 0;
        m_shelfY += m_shelfHeight;
        m_shelfHeight = 0;
    }
    if (m_shelfY + height > AtlasSize)
        return false; // full: that kind stays a box
    *at = QRect(m_shelfX, m_shelfY, skin.width(), skin.height());
    QPainter painter(&m_atlas);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(at->topLeft(), skin);
    painter.end();
    m_shelfX += width;
    m_shelfHeight = qMax(m_shelfHeight, height);
    ++m_revision;
    return true;
}

} // namespace World3D
