#include "Language.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>
#include <QTranslator>

namespace {
// Translations keyed by the source text alone (contexts don't matter for this app).
class JsonTranslator final : public QTranslator
{
public:
    explicit JsonTranslator(const QString &resource, QObject *parent = nullptr)
        : QTranslator(parent)
    {
        QFile file(resource);
        if (!file.open(QIODevice::ReadOnly))
            return;
        const auto strings = QJsonDocument::fromJson(file.readAll()).object();
        for (auto it = strings.begin(); it != strings.end(); ++it)
            if (!it.value().toString().isEmpty())
                m_strings.insert(it.key(), it.value().toString());
    }
    bool isEmpty() const override { return m_strings.isEmpty(); }
    QString translate(const char *, const char *sourceText, const char *, int) const override
    {
        return m_strings.value(QString::fromUtf8(sourceText));
    }

private:
    QHash<QString, QString> m_strings;
};
} // namespace

Language::Language(QObject *parent)
    : QObject(parent)
{
    m_language = QSettings().value(QStringLiteral("language"), QStringLiteral("auto")).toString();
    if (m_language != "pt" && m_language != "en")
        m_language = QStringLiteral("auto");
    apply();
}

QString Language::effective() const
{
    if (m_language != "auto")
        return m_language;
    return QLocale::system().language() == QLocale::Portuguese ? QStringLiteral("pt") : QStringLiteral("en");
}

void Language::setLanguage(const QString &language)
{
    const auto value = language == "pt" || language == "en" ? language : QStringLiteral("auto");
    if (value == m_language)
        return;
    m_language = value;
    QSettings().setValue(QStringLiteral("language"), value);
    apply();
}

void Language::apply()
{
    if (m_translator) {
        QCoreApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }
    const auto code = effective();
    QLocale::setDefault(QLocale(code == "pt" ? QStringLiteral("pt_BR") : QStringLiteral("en_US")));
    if (code != "pt") {
        m_translator = new JsonTranslator(QStringLiteral(":/qt/qml/OmniLauncher/assets/i18n/%1.json").arg(code), this);
        QCoreApplication::installTranslator(m_translator);
    }
    if (m_engine)
        m_engine->retranslate();
    emit changed();
}
