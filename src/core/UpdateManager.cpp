#include "UpdateManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>

#ifndef OMNI_BUILD_COMMIT
#  define OMNI_BUILD_COMMIT ""
#endif
#ifndef OMNI_AUTO_UPDATE
#  define OMNI_AUTO_UPDATE 0
#endif

namespace {
const QString kRepository = QStringLiteral("GuardaChuva22/OmniLauncher");

QNetworkRequest apiRequest(const QString &url)
{
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniLauncher"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);
    return request;
}

QString quoted(const QString &path)
{
    return "'" + QString(path).replace("'", "''") + "'";
}
} // namespace

UpdateManager::UpdateManager(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
}

bool UpdateManager::enabled() const
{
    return OMNI_AUTO_UPDATE && !QStringLiteral(OMNI_BUILD_COMMIT).isEmpty();
}

QString UpdateManager::currentCommit() const
{
    return QStringLiteral(OMNI_BUILD_COMMIT).left(7);
}

void UpdateManager::setState(const QString &state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit changed();
}

void UpdateManager::check()
{
    if (!enabled() || m_state == "checking" || m_state == "downloading" || m_state == "installing")
        return;
    setState(QStringLiteral("checking"));
    auto *reply = m_network->get(apiRequest(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(kRepository)));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setState(QStringLiteral("error"), tr("Não foi possível verificar atualizações."));
            return;
        }
        QString label;
        for (const auto &value : QJsonDocument::fromJson(reply->readAll()).object().value("assets").toArray()) {
            const auto asset = value.toObject();
            if (asset.value("name").toString().endsWith("-win64.zip")) {
                label = asset.value("label").toString().trimmed();
                m_assetUrl = asset.value("browser_download_url").toString();
            }
        }
        // Unlabelled assets predate commit tracking; nothing to compare against.
        if (label.isEmpty() || m_assetUrl.isEmpty() || label == QStringLiteral(OMNI_BUILD_COMMIT)) {
            setState(QStringLiteral("current"));
            return;
        }
        m_latestCommit = label;
        auto *commit = m_network->get(apiRequest(QStringLiteral("https://api.github.com/repos/%1/commits/%2").arg(kRepository, label)));
        connect(commit, &QNetworkReply::finished, this, [this, commit] {
            commit->deleteLater();
            m_changes = QJsonDocument::fromJson(commit->readAll()).object().value("commit").toObject()
                            .value("message").toString().section('\n', 0, 0);
            setState(QStringLiteral("available"));
            // Unattended update (release testing): OMNI_UPDATE_AUTOINSTALL=1.
            if (qEnvironmentVariableIsSet("OMNI_UPDATE_AUTOINSTALL"))
                install();
        });
    });
}

void UpdateManager::install()
{
    if (m_state != "available" && m_state != "error")
        return;
    if (m_assetUrl.isEmpty()) {
        check();
        return;
    }
    const auto installDir = QCoreApplication::applicationDirPath();
    QTemporaryFile probe(QDir(installDir).filePath("write-test-XXXXXX"));
    if (!probe.open()) {
        setState(QStringLiteral("error"), tr("A pasta do OmniLauncher não permite escrita. Baixe a nova versão manualmente."));
        return;
    }
    probe.close();

    setState(QStringLiteral("downloading"));
    m_progress = 0;
    auto *reply = m_network->get(apiRequest(m_assetUrl));
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_progress = total > 0 ? double(received) / double(total) : -1;
        emit changed();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, installDir] {
        reply->deleteLater();
        const auto data = reply->readAll();
        const auto root = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("OmniLauncher-update");
        QDir(root).removeRecursively();
        QDir().mkpath(root);
        const auto zip = QDir(root).filePath("update.zip");
        QSaveFile file(zip);
        if (reply->error() != QNetworkReply::NoError || data.size() < 1024 * 1024
            || !file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            setState(QStringLiteral("error"), tr("Falha ao baixar a atualização."));
            return;
        }
        setState(QStringLiteral("installing"));
        QProcess tar;
        tar.start(QStandardPaths::findExecutable(QStringLiteral("tar")), {"-xf", zip, "-C", root});
        const auto staged = QDir(root).filePath("OmniLauncher");
        if (!tar.waitForFinished(120000) || tar.exitCode() != 0 || !QFileInfo::exists(QDir(staged).filePath("OmniLauncher.exe"))) {
            setState(QStringLiteral("error"), tr("O pacote da atualização veio incompleto."));
            return;
        }
        // Swap files after we exit, then start the new version.
        const auto script = QDir(root).filePath("apply-update.ps1");
        QFile out(script);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            setState(QStringLiteral("error"), tr("Não foi possível preparar a atualização."));
            return;
        }
        const auto native = [](const QString &path) { return quoted(QDir::toNativeSeparators(path)); };
        out.write(QStringLiteral(
            "$ErrorActionPreference = 'SilentlyContinue'\n"
            "Wait-Process -Id %1 -Timeout 60\n"
            "Start-Sleep -Milliseconds 400\n"
            "Copy-Item -Path (Join-Path %2 '*') -Destination %3 -Recurse -Force\n"
            "Start-Process -FilePath (Join-Path %3 'OmniLauncher.exe')\n"
            "Remove-Item -LiteralPath %4 -Recurse -Force\n")
                      .arg(QCoreApplication::applicationPid())
                      .arg(native(staged), native(installDir), native(root))
                      .toUtf8());
        out.close();
        if (!QProcess::startDetached(QStringLiteral("powershell.exe"),
                                     {"-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", script})) {
            setState(QStringLiteral("error"), tr("Não foi possível iniciar o instalador da atualização."));
            return;
        }
        emit quitRequested();
    });
}
