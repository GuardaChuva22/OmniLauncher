#pragma once

#include <QImage>
#include <QQuickRhiItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <QElapsedTimer>

// A player in 3D leaning on a ledge (or, with `stand`, standing on its own): the upper body behind it, arms resting on it, the
// head turning to the cursor. The camera looks level from a little above, so the ledge
// is a front edge at `ledge` px from the top and a top face back to `ledgeBack` px;
// whatever of the player is behind and below it is hidden, so a card drawn under this
// item there (and its top face) reads as the wall the player holds on to. The pose is plain
// properties (degrees and skin pixels) for QML to animate; the head's turn to the
// cursor and its easing are worked out here, a frame at a time while it moves.
class SkinView3D : public QQuickRhiItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SkinView3D)
    // Skin texture (a file URL; its query is ignored). "" draws the default skin of
    // `name` as an offline player (Steve or Alex, as the game picks).
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY sourceChanged)
    // Cape texture (a file URL), drawn on the back; "" for none.
    Q_PROPERTY(QString cape READ cape WRITE setCape NOTIFY sourceChanged)
    // "classic" or "slim" to force the arms; "" reads them from the skin.
    Q_PROPERTY(QString variant READ variant WRITE setVariant NOTIFY skinChanged)
    // Three-pixel arms (Alex).
    Q_PROPERTY(bool slim READ slim NOTIFY skinChanged)
    // The camera, from 0 (a little above the ledge, its front edge at `ledge` px) to 1 (on
    // the player standing in the middle of the item, the skins page); in between while the
    // player jumps from one to the other.
    Q_PROPERTY(qreal stand MEMBER m_stand NOTIFY poseChanged)
    // The ledge hides what of the player is behind and below it.
    Q_PROPERTY(bool wall MEMBER m_wall NOTIFY poseChanged)
    Q_PROPERTY(qreal ledge MEMBER m_ledge NOTIFY poseChanged)
    Q_PROPERTY(qreal ledgeBack READ ledgeBack NOTIFY poseChanged)
    // Pixels per skin pixel, at the player.
    Q_PROPERTY(qreal unit MEMBER m_unit NOTIFY poseChanged)
    // Where the head is on screen at rest, for the pointer area.
    Q_PROPERTY(QPointF headCenter READ headCenter NOTIFY poseChanged)
    // Where the floor under the player is on screen (its feet at rise 0), and how many
    // pixels a skin pixel there takes.
    Q_PROPERTY(qreal ground READ ground NOTIFY poseChanged)
    Q_PROPERTY(qreal groundScale READ groundScale NOTIFY poseChanged)

    // Pose. The whole player: up, toward the viewer, rolled and turned around its hips.
    Q_PROPERTY(qreal rise MEMBER m_rise NOTIFY poseChanged)
    Q_PROPERTY(qreal advance MEMBER m_advance NOTIFY poseChanged)
    Q_PROPERTY(qreal tilt MEMBER m_tilt NOTIFY poseChanged)
    Q_PROPERTY(qreal turn MEMBER m_turn NOTIFY poseChanged)
    // Body: pitch > 0 leans toward the viewer.
    Q_PROPERTY(qreal bodyPitch MEMBER m_bodyPitch NOTIFY poseChanged)
    Q_PROPERTY(qreal bodyRoll MEMBER m_bodyRoll NOTIFY poseChanged)
    // Head: pitch > 0 looks down, yaw > 0 to the viewer's right; added to the cursor's.
    Q_PROPERTY(qreal headPitch MEMBER m_headPitch NOTIFY poseChanged)
    Q_PROPERTY(qreal headYaw MEMBER m_headYaw NOTIFY poseChanged)
    Q_PROPERTY(qreal headRoll MEMBER m_headRoll NOTIFY poseChanged)
    // Arms (right: the player's, on the viewer's left), mirrored for the left one: pitch > 0
    // swings forward and up (90 points at the viewer, 180 straight up), yaw > 0 brings the
    // hand in front of the body, roll > 0 turns the arm toward the head (a hanging hand
    // swings in, a raised one out).
    Q_PROPERTY(qreal rightArmPitch MEMBER m_rightArmPitch NOTIFY poseChanged)
    Q_PROPERTY(qreal rightArmYaw MEMBER m_rightArmYaw NOTIFY poseChanged)
    Q_PROPERTY(qreal rightArmRoll MEMBER m_rightArmRoll NOTIFY poseChanged)
    Q_PROPERTY(qreal leftArmPitch MEMBER m_leftArmPitch NOTIFY poseChanged)
    Q_PROPERTY(qreal leftArmYaw MEMBER m_leftArmYaw NOTIFY poseChanged)
    Q_PROPERTY(qreal leftArmRoll MEMBER m_leftArmRoll NOTIFY poseChanged)
    Q_PROPERTY(qreal rightLegPitch MEMBER m_rightLegPitch NOTIFY poseChanged)
    Q_PROPERTY(qreal leftLegPitch MEMBER m_leftLegPitch NOTIFY poseChanged)
    // How far the cape swings out behind, in degrees.
    Q_PROPERTY(qreal capeSwing MEMBER m_capeSwing NOTIFY poseChanged)
    // How much the head follows the cursor, 0 to 1.
    Q_PROPERTY(qreal follow MEMBER m_follow NOTIFY poseChanged)

public:
    struct Pose
    {
        qreal rise = 0, advance = 0, tilt = 0, turn = 0;
        qreal bodyPitch = 0, bodyRoll = 0;
        qreal headPitch = 0, headYaw = 0, headRoll = 0;
        qreal rightArmPitch = 0, rightArmYaw = 0, rightArmRoll = 0;
        qreal leftArmPitch = 0, leftArmYaw = 0, leftArmRoll = 0;
        qreal rightLegPitch = 0, leftLegPitch = 0, capeSwing = 0;
        bool cape = false;
    };

    // Model space, in skin pixels: hips at the origin, facing +z (the viewer).
    static constexpr float LedgeTop = 5.f;
    static constexpr float LedgeFront = 9.f;
    static constexpr float LedgeBack = 3.f;

    explicit SkinView3D(QQuickItem *parent = nullptr);

    // Where the game versions are, for the default skins in their jars.
    static void setDataDirectory(const QString &path);

    QString source() const { return m_source; }
    void setSource(const QString &source);
    QString name() const { return m_name; }
    void setName(const QString &name);
    QString cape() const { return m_cape; }
    void setCape(const QString &cape);
    QString variant() const { return m_variant; }
    void setVariant(const QString &variant);
    bool slim() const { return m_variant.isEmpty() ? m_slim : m_variant == QLatin1String("slim"); }
    QPointF headCenter() const;
    qreal ledgeBack() const;
    qreal ground() const;
    qreal groundScale() const;

signals:
    void sourceChanged();
    void skinChanged();
    void poseChanged();

protected:
    QQuickRhiItemRenderer *createRenderer() override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    friend class SkinViewRenderer;
    void loadSkin();
    void loadCape();
    void compose();
    void tick();

    QString m_source, m_name, m_cape, m_variant;
    QImage m_skinImage, m_capeImage;
    // The skin over the cape (64×96), as the renderer uploads it.
    QImage m_skin;
    qreal m_stand = 0;
    bool m_wall = true;
    bool m_slim = false;
    bool m_skinDirty = false;
    qreal m_ledge = 0, m_unit = 6;
    Pose pose() const;
    qreal m_rise = 0, m_advance = 0, m_tilt = 0, m_turn = 0;
    qreal m_bodyPitch = 0, m_bodyRoll = 0;
    qreal m_headPitch = 0, m_headYaw = 0, m_headRoll = 0;
    qreal m_rightArmPitch = 0, m_rightArmYaw = 0, m_rightArmRoll = 0;
    qreal m_leftArmPitch = 0, m_leftArmYaw = 0, m_leftArmRoll = 0;
    qreal m_rightLegPitch = 0, m_leftLegPitch = 0, m_capeSwing = 6;
    qreal m_follow = 0;
    // The head's turn to the cursor, eased toward where it points.
    qreal m_lookYaw = 0, m_lookPitch = 0;
    QPoint m_cursor{-1, -1};
    QTimer m_ticker;
    QElapsedTimer m_clock;
};
