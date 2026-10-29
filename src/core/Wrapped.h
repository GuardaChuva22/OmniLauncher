#pragma once

#include <QHash>
#include <QObject>
#include <QVariantMap>

// "Retrospectiva": the statistics Minecraft keeps per world (stats/<uuid>.json, or
// players/stats/ in newer versions) summed over a game's worlds, or over every
// game. Names come from the game's own language file (pt_br or en_us, following the
// interface) and icons are textures taken from client jars into cache/wrapped/<version>/.
class Wrapped final : public QObject
{
    Q_OBJECT

public:
    explicit Wrapped(const QString &dataDirectory, QObject *parent = nullptr);

    // games: the library; gameSettings: AppModel::gameSettings (for launcher playtime).
    // gameId "" covers every game. See the .cpp for the keys of the result.
    Q_INVOKABLE QVariantMap build(const QVariantList &games, const QVariantMap &gameSettings, const QString &gameId);

private:
    QString jar(const QString &version) const;
    QString name(const QString &id, const QString &kind, const QStringList &versions);
    const QHash<QString, QString> &language(const QStringList &versions);

    QString m_root;
    QHash<QString, QString> m_language;
    QString m_languageCode;
};
