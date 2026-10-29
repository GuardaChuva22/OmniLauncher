#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>

// Short interface sounds (assets/sounds/*.wav, made with earshot), played through
// Windows' PlaySound from memory: no audio dependency, one sound at a time, which
// suits clicks. Volume is applied to the samples; settings live in QSettings.
class UiSound final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(bool hoverEnabled READ hoverEnabled WRITE setHoverEnabled NOTIFY changed)

public:
    explicit UiSound(QObject *parent = nullptr);
    ~UiSound() override;

    bool enabled() const { return m_enabled; }
    double volume() const { return m_volume; }
    bool hoverEnabled() const { return m_hoverEnabled; }
    void setEnabled(bool enabled);
    void setVolume(double volume);
    void setHoverEnabled(bool enabled);

    // hover, click, toggle-on, toggle-off, open, close, page, success, launch, error
    Q_INVOKABLE void play(const QString &name);

signals:
    void changed();

private:
    void rebuild();

    bool m_enabled = true;
    double m_volume = 0.6;
    bool m_hoverEnabled = true;
    QHash<QString, QByteArray> m_source;
    QHash<QString, QByteArray> m_scaled;
    QElapsedTimer m_clock;
    qint64 m_lastAt = -1000;
};
