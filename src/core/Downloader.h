#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Parallel file downloader for game files. A file already on disk with the
// expected size is trusted; freshly downloaded bytes are checked against the
// SHA-1 when one is known, then written atomically.
class Downloader final : public QObject
{
    Q_OBJECT

public:
    struct Job {
        QUrl url;
        QString path;
        QString sha1;
        qint64 size = -1;
        QString sha256;
    };

    explicit Downloader(QNetworkAccessManager *network, QObject *parent = nullptr);

    // Replaces any previous batch. Emits finished() exactly once per batch.
    void start(const QList<Job> &jobs);
    void cancel();
    bool running() const { return m_running; }

    qint64 totalBytes() const { return m_totalBytes; }
    qint64 receivedBytes() const;
    int totalFiles() const { return m_total; }
    int completedFiles() const { return m_completed; }

    // True when `path` exists and matches `size` (when known).
    static bool isPresent(const QString &path, qint64 size);

signals:
    void progress();
    void finished(bool ok, const QString &error);

private:
    struct Active {
        Job job;
        int attempt = 0;
        qint64 received = 0;
    };
    void pump();
    void launch(const Job &job, int attempt);
    void onReplyFinished(QNetworkReply *reply);
    void finish(bool ok, const QString &error);

    QNetworkAccessManager *m_network;
    QList<Job> m_queue;
    QHash<QNetworkReply *, Active> m_active;
    qint64 m_totalBytes = 0;
    qint64 m_doneBytes = 0;
    int m_total = 0;
    int m_completed = 0;
    bool m_running = false;
    quint64 m_batch = 0;
};
