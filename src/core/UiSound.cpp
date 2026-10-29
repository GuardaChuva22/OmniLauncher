#include "UiSound.h"

#include <QFile>
#include <QSettings>

#include <cstring>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <mmsystem.h>
#endif

namespace {
const char *const kNames[] = {"hover", "click", "toggle-on", "toggle-off", "open", "close",
                              "page", "success", "launch", "error"};

// Offset of the PCM samples in a 16-bit WAV, or -1.
int dataOffset(const QByteArray &wav, int &size)
{
    if (wav.size() < 44 || !wav.startsWith("RIFF") || wav.mid(8, 4) != "WAVE")
        return -1;
    int at = 12;
    while (at + 8 <= wav.size()) {
        quint32 length = 0;
        std::memcpy(&length, wav.constData() + at + 4, 4);
        if (wav.mid(at, 4) == "data") {
            size = int(qMin<qint64>(length, wav.size() - at - 8));
            return at + 8;
        }
        at += 8 + int(length) + (length & 1);
    }
    return -1;
}
} // namespace

UiSound::UiSound(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Sounds"));
    m_enabled = settings.value(QStringLiteral("enabled"), true).toBool();
    m_volume = qBound(0.0, settings.value(QStringLiteral("volume"), 0.6).toDouble(), 1.0);
    m_hoverEnabled = settings.value(QStringLiteral("hover"), true).toBool();
    for (const auto *name : kNames) {
        QFile file(QStringLiteral(":/qt/qml/OmniLauncher/assets/sounds/%1.wav").arg(QLatin1StringView(name)));
        if (file.open(QIODevice::ReadOnly))
            m_source.insert(QString::fromLatin1(name), file.readAll());
    }
    rebuild();
    m_clock.start();
}

UiSound::~UiSound()
{
#ifdef Q_OS_WIN
    PlaySoundW(nullptr, nullptr, 0);
#endif
}

void UiSound::rebuild()
{
#ifdef Q_OS_WIN
    PlaySoundW(nullptr, nullptr, 0); // nothing may play from a buffer being replaced
#endif
    m_scaled.clear();
    // A gentle curve: the slider's middle is clearly quieter than its end.
    const double gain = m_volume * m_volume;
    for (auto it = m_source.begin(); it != m_source.end(); ++it) {
        QByteArray wav = it.value();
        int size = 0;
        const int offset = dataOffset(wav, size);
        if (offset < 0)
            continue;
        auto *samples = reinterpret_cast<qint16 *>(wav.data() + offset);
        for (int i = 0; i < size / 2; ++i)
            samples[i] = qint16(qBound(-32768.0, samples[i] * gain, 32767.0));
        m_scaled.insert(it.key(), wav);
    }
}

void UiSound::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    QSettings().setValue(QStringLiteral("Sounds/enabled"), enabled);
    emit changed();
}

void UiSound::setVolume(double volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (qFuzzyCompare(volume + 1, m_volume + 1))
        return;
    m_volume = volume;
    QSettings().setValue(QStringLiteral("Sounds/volume"), volume);
    rebuild();
    emit changed();
}

void UiSound::setHoverEnabled(bool enabled)
{
    if (enabled == m_hoverEnabled)
        return;
    m_hoverEnabled = enabled;
    QSettings().setValue(QStringLiteral("Sounds/hover"), enabled);
    emit changed();
}

void UiSound::play(const QString &name)
{
    if (!m_enabled || m_volume <= 0.01)
        return;
    const auto now = m_clock.elapsed();
    if (name == "hover") {
        // Hover ticks never cut another sound, and a sweep across a list stays quiet.
        if (!m_hoverEnabled || now - m_lastAt < 90)
            return;
    }
    const auto it = m_scaled.constFind(name);
    if (it == m_scaled.constEnd())
        return;
    m_lastAt = now;
#ifdef Q_OS_WIN
    PlaySoundW(reinterpret_cast<LPCWSTR>(it->constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#endif
}
