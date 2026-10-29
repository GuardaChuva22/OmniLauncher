#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Self-update from the GitHub release. Packaged builds carry the commit they were
// built from; the release zip is labelled with its commit by CI. A different label
// means a newer package: download it, unpack beside, and let a small script swap the
// files once the launcher has closed, then start it again.
class UpdateManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled CONSTANT)
    Q_PROPERTY(QString currentCommit READ currentCommit CONSTANT)
    // "" | "checking" | "current" | "available" | "downloading" | "installing" | "error"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString changes READ changes NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)

public:
    explicit UpdateManager(QObject *parent = nullptr);

    bool enabled() const;
    QString currentCommit() const;
    QString state() const { return m_state; }
    QString changes() const { return m_changes; }
    QString error() const { return m_error; }
    double progress() const { return m_progress; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void install();

signals:
    void changed();
    void quitRequested();

private:
    void setState(const QString &state, const QString &error = {});

    QNetworkAccessManager *m_network;
    QString m_state;
    QString m_changes;
    QString m_error;
    QString m_assetUrl;
    QString m_latestCommit;
    double m_progress = -1;
};
