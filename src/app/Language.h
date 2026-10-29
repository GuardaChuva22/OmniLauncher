#pragma once

#include <QObject>

class QQmlEngine;
class QTranslator;

// Interface language. The text in the code is Portuguese (Brazil); other languages come
// from assets/i18n/<code>.json (source text → translation), loaded by a QTranslator so
// qsTr()/tr() pick them up. "auto" follows Windows: Portuguese for a Portuguese system,
// English otherwise. Switching retranslates the open interface at once.
class Language final : public QObject
{
    Q_OBJECT
    // "auto" | "pt" | "en"
    Q_PROPERTY(QString language READ language NOTIFY changed)
    // The language in use: "pt" | "en"
    Q_PROPERTY(QString effective READ effective NOTIFY changed)

public:
    explicit Language(QObject *parent = nullptr);

    QString language() const { return m_language; }
    QString effective() const;
    void setEngine(QQmlEngine *engine) { m_engine = engine; }

    Q_INVOKABLE void setLanguage(const QString &language);

signals:
    void changed();

private:
    void apply();

    QString m_language;
    QQmlEngine *m_engine = nullptr;
    QTranslator *m_translator = nullptr;
};
