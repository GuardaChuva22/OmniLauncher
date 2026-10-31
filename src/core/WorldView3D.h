#pragma once

#include "ChunkMesher.h"

#include <QColor>
#include <QQuickRhiItem>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

#include <memory>

class OmniLink;

// The 3D map: a world's chunks as meshes of the game's own block models and textures,
// seen from above at an angle (an orthographic, isometric camera that turns freely).
// Chunks are meshed on worker threads around what the camera sees and kept until the
// view moves away; a save of the world re-meshes only the chunks the game wrote, each
// keeping its old mesh until the new one is ready. Nothing is drawn while nothing moves.
// While `live`, the blocks the game has now (the Omni agent, through OmniLink) are drawn
// over the saved ones, each chunk again as soon as a block in it changes.
class WorldView3D : public QQuickRhiItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WorldView3D)
    Q_PROPERTY(QString worldPath READ worldPath WRITE setWorldPath NOTIFY sceneChanged)
    Q_PROPERTY(QString dimension READ dimension WRITE setDimension NOTIFY sceneChanged)
    Q_PROPERTY(QString version READ version WRITE setVersion NOTIFY sceneChanged)
    // The game folder whose mods and packs draw it; "" for the one holding the world
    // (instances/<id>/saves/<world>).
    Q_PROPERTY(QString instance READ instance WRITE setInstance NOTIFY sceneChanged)
    // Blocks above this height are left out, opening what is below (NoCut: none).
    Q_PROPERTY(int cut READ cut WRITE setCut NOTIFY cutChanged)
    Q_PROPERTY(qreal yaw READ yaw WRITE setYaw NOTIFY cameraChanged)
    // Pixels per block.
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY cameraChanged)
    Q_PROPERTY(QVector3D target READ target WRITE setTarget NOTIFY cameraChanged)
    // [{x, y, z, hostile}] and {x, y, z}: drawn as boxes.
    Q_PROPERTY(QVariantList entities READ entities WRITE setEntities NOTIFY markersChanged)
    Q_PROPERTY(QVariantMap player READ player WRITE setPlayer NOTIFY markersChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY backgroundChanged)
    // The game open in this world feeds it its blocks as they change (OmniLink).
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged)
    // Chunks waiting to be meshed.
    Q_PROPERTY(int pending READ pending NOTIFY pendingChanged)

public:
    static constexpr int NoCut = 1000;
    static constexpr qreal Pitch = 30; // degrees above the horizon

    explicit WorldView3D(QQuickItem *parent = nullptr);
    ~WorldView3D() override;

    static void setDataDirectory(const QString &path);
    // Where live blocks come from; set before the first view is made.
    static void setLink(OmniLink *link);

    QString worldPath() const { return m_worldPath; }
    void setWorldPath(const QString &path);
    QString dimension() const { return m_dimension; }
    void setDimension(const QString &dimension);
    QString version() const { return m_version; }
    void setVersion(const QString &version);
    QString instance() const { return m_instance; }
    void setInstance(const QString &instance);
    int cut() const { return m_cut; }
    void setCut(int cut);
    qreal yaw() const { return m_yaw; }
    void setYaw(qreal yaw);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    QVector3D target() const { return m_target; }
    void setTarget(const QVector3D &target);
    QVariantList entities() const { return m_entities; }
    void setEntities(const QVariantList &entities);
    QVariantMap player() const { return m_player; }
    void setPlayer(const QVariantMap &player);
    QColor background() const { return m_background; }
    void setBackground(const QColor &color);
    bool live() const { return m_live; }
    void setLive(bool live);
    int pending() const { return int(m_queue.size()) + m_running; }

    // Moves the view by a drag of (dx, dy) pixels.
    Q_INVOKABLE void pan(qreal dx, qreal dy);
    // Zooms by `factor`, keeping the ground under (x, y) where it is.
    Q_INVOKABLE void zoomAt(qreal factor, qreal x, qreal y);
    // The world was saved: re-mesh the chunks the game wrote again.
    Q_INVOKABLE void refresh();

signals:
    void sceneChanged();
    void cutChanged();
    void cameraChanged();
    void markersChanged();
    void backgroundChanged();
    void pendingChanged();
    void liveChanged();

protected:
    QQuickRhiItemRenderer *createRenderer() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    friend class WorldViewRenderer;

    struct ChunkState
    {
        quint32 stamp = 0;
        int cut = 0;
        bool running = false;
        bool drawn = false;
        int quads = 0; // on the GPU
    };

    QVector3D eyeDirection() const;
    QVector3D screenRight() const;
    QVector3D screenUp() const;
    QVector3D ground(qreal x, qreal y, qreal level) const;
    QMatrix4x4 viewProjection() const;
    void reset();
    void schedule();
    void startJobs();
    void finished(qint64 key, int generation, std::shared_ptr<World3D::ChunkMesh> mesh, quint32 stamp, int cut);
    const QByteArray &header(int rx, int rz);
    void rebuildMarkers();
    void receiveBlocks(const QString &dimension, const QByteArray &payload);
    void applyLive(const QList<QByteArray> &payloads);

    QString m_worldPath, m_dimension, m_version, m_instance;
    int m_cut = NoCut;
    qreal m_yaw = 45;
    qreal m_zoom = 6;
    QVector3D m_target{0, 64, 0};
    QVariantList m_entities;
    QVariantMap m_player;
    QColor m_background{21, 18, 22};
    bool m_live = false;

    // Scene
    int m_generation = 0;
    int m_originX = 0, m_originZ = 0;
    QString m_modelsKey;
    std::shared_ptr<World3D::BlockModels> m_models;
    std::shared_ptr<World3D::ChunkSource> m_source;
    QHash<qint64, QByteArray> m_headers;
    QHash<qint64, ChunkState> m_chunks;
    QSet<qint64> m_wanted;
    QList<qint64> m_queue; // nearest first
    int m_running = 0;
    QThreadPool m_pool;
    QTimer m_resetTimer, m_scheduleTimer;

    // Handed to the renderer at its next sync.
    QList<std::shared_ptr<World3D::ChunkMesh>> m_uploads;
    QList<qint64> m_drops;
    bool m_dropAll = false;
    std::vector<World3D::Vertex> m_markers;
    std::vector<World3D::Vertex> m_mobs; // models, on the entity atlas
    bool m_markersDirty = true;
};
