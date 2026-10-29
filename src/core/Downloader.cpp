#include "Downloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>

namespace {
constexpr int kParallel = 12;
constexpr int kAttempts = 3;

QString sha1Of(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

QString sha1OfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}
} // namespace

Downloader::Downloader(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network)
{
}

bool Downloader::isPresent(const QString &path, qint64 size)
{
    const QFileInfo info(path);
    return info.isFile() && (size < 0 || info.size() == size);
}

qint64 Downloader::receivedBytes() const
{
    qint64 bytes = m_doneBytes;
    for (const auto &active : m_active)
        bytes += active.received;
    return bytes;
}

void Downloader::start(const QList<Job> &jobs)
{
    cancel();
    ++m_batch;
    m_queue.clear();
    m_totalBytes = m_doneBytes = 0;
    m_total = m_completed = 0;
    QSet<QString> seen;
    for (const auto &job : jobs) {
        const auto key = QDir::cleanPath(job.path).toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        // Without a known size, a small file (version/index JSON) is verified by hash.
        const bool present = job.size >= 0 ? isPresent(job.path, job.size)
            : QFileInfo(job.path).isFile() && (job.sha1.isEmpty() || sha1OfFile(job.path) == job.sha1);
        if (present)
            continue;
        m_queue.append(job);
        m_totalBytes += qMax<qint64>(0, job.size);
    }
    m_total = int(m_queue.size());
    m_running = true;
    if (m_queue.isEmpty()) {
        const auto batch = m_batch;
        QTimer::singleShot(0, this, [this, batch] { if (batch == m_batch) finish(true, {}); });
        return;
    }
    pump();
}

void Downloader::cancel()
{
    if (!m_running)
        return;
    ++m_batch;
    const auto replies = m_active.keys();
    m_active.clear();
    m_queue.clear();
    m_running = false;
    for (auto *reply : replies)
        reply->abort();
    emit finished(false, {});
}

void Downloader::pump()
{
    while (m_running && m_active.size() < kParallel && !m_queue.isEmpty())
        launch(m_queue.takeFirst(), 0);
}

void Downloader::launch(const Job &job, int attempt)
{
    QNetworkRequest request(job.url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniLauncher/0.1"));
    auto *reply = m_network->get(request);
    reply->setProperty("omniBatch", m_batch);
    m_active.insert(reply, Active{job, attempt, 0});
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64) {
        auto it = m_active.find(reply);
        if (it == m_active.end())
            return;
        it->received = received;
        emit progress();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onReplyFinished(reply); });
}

void Downloader::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->property("omniBatch").toULongLong() != m_batch || !m_active.contains(reply))
        return;
    const auto active = m_active.take(reply);
    const auto &job = active.job;

    QString problem;
    QByteArray data;
    if (reply->error() != QNetworkReply::NoError) {
        problem = reply->errorString();
    } else {
        data = reply->readAll();
        if (job.size >= 0 && data.size() != job.size)
            problem = QStringLiteral("tamanho inesperado");
        else if (!job.sha1.isEmpty() && sha1Of(data).compare(job.sha1, Qt::CaseInsensitive) != 0)
            problem = QStringLiteral("arquivo corrompido (SHA-1 não confere)");
        else if (!job.sha256.isEmpty()
                 && QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())
                            .compare(job.sha256, Qt::CaseInsensitive) != 0)
            problem = QStringLiteral("arquivo corrompido (SHA-256 não confere)");
    }
    if (!problem.isEmpty()) {
        if (active.attempt + 1 < kAttempts) {
            launch(job, active.attempt + 1);
            return;
        }
        finish(false, QStringLiteral("Falha ao baixar %1: %2").arg(QFileInfo(job.path).fileName(), problem));
        return;
    }

    QSaveFile file(job.path);
    if (!QDir().mkpath(QFileInfo(job.path).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || file.write(data) != data.size() || !file.commit()) {
        finish(false, QStringLiteral("Não foi possível gravar %1: %2").arg(job.path, file.errorString()));
        return;
    }
    m_doneBytes += data.size();
    ++m_completed;
    emit progress();
    if (m_queue.isEmpty() && m_active.isEmpty())
        finish(true, {});
    else
        pump();
}

void Downloader::finish(bool ok, const QString &error)
{
    if (!m_running)
        return;
    m_running = false;
    if (!ok) {
        ++m_batch;
        const auto replies = m_active.keys();
        m_active.clear();
        m_queue.clear();
        for (auto *reply : replies)
            reply->abort();
    }
    emit finished(ok, error);
}
