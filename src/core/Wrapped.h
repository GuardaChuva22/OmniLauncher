#pragma once

#include <QHash>
#include <QObject>
#include <QThreadPool>
#include <QVariantMap>

// "Retrospectiva": the statistics Minecraft keeps per world (stats/<uuid>.json, or
// players/stats/ in newer versions) summed over a game's worlds, or over every
// game. Names come from the game's own language file (pt_br or en_us, following the
// interface) and icons are drawn from the models and textures in the client jars, as the
// inventory shows them, into cache/wrapped/<version>/icons/.
class Wrapped final : public QObject
{
    Q_OBJECT

public:
    explicit Wrapped(const QString &dataDirectory, QObject *parent = nullptr);

    // games: the library; gameSettings: AppModel::gameSettings (for launcher playtime).
    // gameId "" covers every game. See the .cpp for the keys of the result.
    QVariantMap build(const QVariantList &games, const QVariantMap &gameSettings, const QString &gameId);
    // build() on a worker thread (it reads every world's stats and, the first time, takes
    // models out of jars and draws icons); `built` brings the latest request's result.
    Q_INVOKABLE void request(const QVariantList &games, const QVariantMap &gameSettings, const QString &gameId);

signals:
    void built(const QVariantMap &info);

private:
    QString jar(const QString &version) const;
    QString name(const QString &id, const QString &kind, const QStringList &versions);
    const QHash<QString, QString> &language(const QStringList &versions);

    QString m_root;
    QHash<QString, QString> m_language;
    QString m_languageCode;
    quint64 m_serial = 0;
    // One build at a time. Last member: destroyed first, it waits for a running build.
    QThreadPool m_pool;
};
