#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QString>
#include <QVector3D>

#include <memory>
#include <vector>

namespace World3D {

// How each kind of mob looks, as the Omni agent sent it (a Model frame, see OmniLink):
// the quads of its model and its skin. Kept on disk, so the map still has them with the
// game closed; the skins share one atlas. Used on the GUI thread only.
class EntityModels
{
public:
    static constexpr int AtlasSize = 1024;
    static constexpr int MaxSkin = 256; // a bigger skin is drawn at this size

    struct Corner
    {
        QVector3D position; // the model's pixels, y down, as the game has them
        float u, v;         // in the atlas
    };
    struct Model
    {
        std::vector<Corner> corners; // 4 a quad
    };

    static EntityModels &shared();

    // Where models are kept: loads the ones there.
    void setFolder(const QString &folder);
    // A Model payload; false when it can't be read or its skin has no room. Kept on disk
    // when `keep`.
    bool add(const QByteArray &payload, bool keep);
    std::shared_ptr<const Model> find(const QString &id) const { return m_models.value(id); }

    const QImage &atlas() const { return m_atlas; }
    int revision() const { return m_revision; } // goes up with each skin placed

private:
    bool place(const QImage &skin, QRect *at);

    QString m_folder;
    QHash<QString, std::shared_ptr<const Model>> m_models;
    QHash<QString, QByteArray> m_payloads;
    QImage m_atlas;
    int m_revision = 0;
    int m_shelfX = 0, m_shelfY = 0, m_shelfHeight = 0;
};

} // namespace World3D
