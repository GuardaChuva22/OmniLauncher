#include "SkinManager.h"

#include "AuthManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QSaveFile>

namespace {
const QString kProfile = QStringLiteral("https://api.minecraftservices.com/minecraft/profile");

QNetworkRequest apiRequest(const QString &url, const QString &token)
{
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Authorization", "Bearer " + token.toLatin1());
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(30000);
    return request;
}

QString fileUrl(const QString &path)
{
    // The query makes images reload when a file with the same name changes.
    return QUrl::fromLocalFile(path).toString() + "?v=" + QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch());
}

bool isSkin(const QImage &image)
{
    return image.width() == 64 && (image.height() == 64 || image.height() == 32);
}

// Alex's arms leave the last two columns of the right arm's texture empty.
bool isSlim(const QImage &image)
{
    if (image.height() != 64)
        return false;
    for (int y = 20; y < 32; ++y)
        for (int x = 54; x < 56; ++x)
            if (qAlpha(image.pixel(x, y)) != 0)
                return false;
    return true;
}
} // namespace

SkinManager::SkinManager(const QString &dataDirectory, AuthManager *auth, QObject *parent)
    : QObject(parent)
    , m_root(QDir(dataDirectory).absolutePath())
    , m_auth(auth)
    , m_network(new QNetworkAccessManager(this))
{
    QFile file(libraryPath() + "/library.json");
    if (file.open(QIODevice::ReadOnly)) {
        for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
            auto entry = value.toObject().toVariantMap();
            const auto path = libraryPath(entry.value("id").toString());
            if (!QFileInfo::exists(path))
                continue;
            entry.insert("source", fileUrl(path));
            m_library.append(entry);
        }
    }
}

QString SkinManager::libraryPath(const QString &id) const
{
    const auto folder = QDir(m_root).filePath("skins/library");
    return id.isEmpty() ? folder : folder + '/' + id + ".png";
}

void SkinManager::setBusy(bool busy)
{
    const bool was = this->busy();
    m_busy = qMax(0, m_busy + (busy ? 1 : -1));
    if (was != this->busy())
        emit busyChanged();
}

void SkinManager::saveLibrary()
{
    QJsonArray entries;
    for (const auto &value : std::as_const(m_library)) {
        auto entry = value.toMap();
        entry.remove("source");
        entries.append(QJsonObject::fromVariantMap(entry));
    }
    QDir().mkpath(libraryPath());
    QSaveFile file(libraryPath() + "/library.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(entries).toJson());
        file.commit();
    }
}

QString SkinManager::addToLibrary(const QByteArray &png, const QString &name, const QString &variant, const QString &url)
{
    const auto id = QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha1).toHex().left(16));
    for (auto &value : m_library) {
        auto entry = value.toMap();
        if (entry.value("id").toString() != id)
            continue;
        if (!url.isEmpty() && entry.value("url").toString() != url) {
            entry.insert("url", url);
            value = entry;
            saveLibrary();
        }
        return id;
    }
    QDir().mkpath(libraryPath());
    QSaveFile file(libraryPath(id));
    if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size() || !file.commit())
        return {};
    QVariantMap entry{{"id", id}, {"name", name}, {"variant", variant},
                      {"added", QDateTime::currentDateTime().toString(Qt::ISODate)},
                      {"source", fileUrl(libraryPath(id))}};
    if (!url.isEmpty())
        entry.insert("url", url);
    m_library.prepend(entry);
    saveLibrary();
    emit libraryChanged();
    return id;
}

QString SkinManager::importSkin(const QUrl &file)
{
    QFile input(file.isLocalFile() ? file.toLocalFile() : file.toString());
    const auto png = input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray();
    const auto image = QImage::fromData(png, "PNG");
    if (!isSkin(image)) {
        emit failed(tr("Essa imagem não é uma skin do Minecraft (um PNG de 64×64 ou 64×32)."));
        return {};
    }
    const auto id = addToLibrary(png, QFileInfo(input.fileName()).completeBaseName(), isSlim(image) ? "slim" : "classic");
    if (id.isEmpty())
        emit failed(tr("Não foi possível guardar a skin."));
    return id;
}

void SkinManager::removeSkin(const QString &id)
{
    for (qsizetype i = 0; i < m_library.size(); ++i) {
        if (m_library.at(i).toMap().value("id").toString() != id)
            continue;
        m_library.removeAt(i);
        QFile::remove(libraryPath(id));
        saveLibrary();
        emit libraryChanged();
        return;
    }
}

void SkinManager::renameSkin(const QString &id, const QString &name)
{
    const auto trimmed = name.trimmed();
    for (auto &value : m_library) {
        auto entry = value.toMap();
        if (entry.value("id").toString() != id || trimmed.isEmpty())
            continue;
        entry.insert("name", trimmed.left(40));
        value = entry;
        saveLibrary();
        emit libraryChanged();
        return;
    }
}

QString SkinManager::guessVariant(const QString &source) const
{
    const QUrl url(source);
    return isSlim(QImage(url.isLocalFile() ? url.toLocalFile() : source)) ? QStringLiteral("slim") : QStringLiteral("classic");
}

void SkinManager::fetch(const QString &url, const QString &path, std::function<void()> then)
{
    if (QFileInfo::exists(path)) {
        then();
        return;
    }
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(20000);
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, path, then] {
        reply->deleteLater();
        const auto data = reply->readAll();
        if (reply->error() == QNetworkReply::NoError && data.startsWith("\x89PNG")) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile file(path);
            if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
                file.commit();
        }
        then();
    });
}

void SkinManager::readProfile(const QString &uuid, const QJsonObject &json)
{
    if (m_uuid != uuid) {
        m_uuid = uuid;
        m_profile.clear();
    }
    QVariantMap skin;
    for (const auto &value : json.value("skins").toArray()) {
        const auto object = value.toObject();
        if (object.value("state").toString() == "ACTIVE")
            skin = {{"url", object.value("url").toString()}, {"variant", object.value("variant").toString().toLower()}};
    }
    QVariantList capes;
    for (const auto &value : json.value("capes").toArray()) {
        const auto object = value.toObject();
        capes.append(QVariantMap{{"id", object.value("id").toString()}, {"alias", object.value("alias").toString()},
                                 {"url", object.value("url").toString()}, {"active", object.value("state").toString() == "ACTIVE"}});
    }
    const auto skinUrl = skin.value("url").toString();
    // The game serves an uploaded skin from a URL of its own (and maybe re-encoded): it is
    // the library skin just uploaded, not a new one.
    if (!m_uploading.isEmpty() && !skinUrl.isEmpty()) {
        for (auto &value : m_library) {
            auto entry = value.toMap();
            if (entry.value("id").toString() == m_uploading) {
                entry.insert("url", skinUrl);
                value = entry;
                saveLibrary();
            }
        }
        m_uploading.clear();
    }
    for (const auto &value : std::as_const(m_library)) {
        const auto entry = value.toMap();
        if (!skinUrl.isEmpty() && entry.value("url").toString() == skinUrl) {
            skin.insert("libraryId", entry.value("id"));
            skin.insert("source", entry.value("source"));
        }
    }
    // Sources fill in as the textures arrive.
    m_profile = {{"skin", skin}, {"capes", capes}};
    emit profileChanged();

    if (!skinUrl.isEmpty() && !skin.contains("libraryId")) {
        setBusy(true);
        QNetworkRequest request{QUrl(skinUrl)};
        request.setTransferTimeout(20000);
        auto *reply = m_network->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, uuid, skinUrl] {
            reply->deleteLater();
            setBusy(false);
            const auto png = reply->readAll();
            if (reply->error() != QNetworkReply::NoError || m_uuid != uuid)
                return;
            auto skin = m_profile.value("skin").toMap();
            if (skin.value("url").toString() != skinUrl)
                return;
            const auto id = addToLibrary(png, tr("Skin de %1").arg(QDate::currentDate().toString(QStringLiteral("dd/MM/yy"))),
                                         skin.value("variant").toString(), skinUrl);
            skin.insert("libraryId", id);
            skin.insert("source", id.isEmpty() ? QString() : fileUrl(libraryPath(id)));
            m_profile.insert("skin", skin);
            emit profileChanged();
        });
    }
    for (int i = 0; i < capes.size(); ++i) {
        const auto cape = capes.at(i).toMap();
        const auto id = cape.value("id").toString();
        const auto path = QDir(m_root).filePath("cache/capes/" + id + ".png");
        fetch(cape.value("url").toString(), path, [this, uuid, id, path] {
            if (m_uuid != uuid || !QFileInfo::exists(path))
                return;
            auto capes = m_profile.value("capes").toList();
            for (auto &value : capes) {
                auto cape = value.toMap();
                if (cape.value("id").toString() == id) {
                    cape.insert("source", fileUrl(path));
                    value = cape;
                }
            }
            m_profile.insert("capes", capes);
            emit profileChanged();
        });
    }
}

void SkinManager::call(const QString &uuid, Send send, const QString &success, std::function<void()> then)
{
    setBusy(true);
    m_auth->withSession(uuid, [this, uuid, send, success, then](const QString &error, const QVariantMap &account) {
        if (!error.isEmpty()) {
            setBusy(false);
            m_uploading.clear();
            emit failed(error);
            return;
        }
        auto *reply = send(account.value("accessToken").toString());
        connect(reply, &QNetworkReply::finished, this, [this, reply, uuid, success, then] {
            reply->deleteLater();
            setBusy(false);
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto json = QJsonDocument::fromJson(reply->readAll()).object();
            if (status < 200 || status >= 300) {
                m_uploading.clear();
                emit failed(status == 429 ? tr("Muitas mudanças seguidas. Espere um minuto e tente de novo.")
                            : status == 401 ? tr("A sessão da conta expirou. Entre na conta Microsoft novamente.")
                            : status == 400 && !json.value("errorMessage").toString().isEmpty()
                                ? tr("O Minecraft recusou: %1").arg(json.value("errorMessage").toString())
                            : status == 0 ? tr("Sem conexão com os serviços do Minecraft.")
                            : tr("Os serviços do Minecraft responderam com um erro (%1).").arg(status));
                return;
            }
            if (json.contains("skins")) {
                readProfile(uuid, json);
                for (const auto &value : json.value("skins").toArray())
                    if (value.toObject().value("state").toString() == "ACTIVE")
                        m_auth->updateSkin(uuid, value.toObject().value("url").toString());
            } else if (!success.isEmpty()) {
                load(uuid);
            }
            if (!success.isEmpty())
                emit done(success);
            if (then)
                then();
        });
    });
}

void SkinManager::load(const QString &uuid)
{
    if (uuid.isEmpty())
        return;
    call(uuid, [this](const QString &token) { return m_network->get(apiRequest(kProfile, token)); }, {});
}

void SkinManager::applyLook(const QString &uuid, const QString &skinId, const QString &variant, bool changeCape,
                            const QString &capeId)
{
    const auto capeMessage = capeId.isEmpty() ? tr("Capa escondida.") : tr("Capa trocada.");
    if (skinId.isEmpty()) {
        if (changeCape)
            sendCape(uuid, capeId, capeMessage);
        return;
    }
    QFile file(libraryPath(skinId));
    if (!file.open(QIODevice::ReadOnly)) {
        emit failed(tr("Essa skin não está mais na biblioteca."));
        return;
    }
    const auto png = file.readAll();
    m_uploading = skinId;
    // The skin first, then the cape: one message for the whole look.
    call(uuid, [this, png, variant](const QString &token) {
        auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart variantPart;
        variantPart.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"variant\""));
        variantPart.setBody(variant == "slim" ? "slim" : "classic");
        QHttpPart filePart;
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"file\"; filename=\"skin.png\""));
        filePart.setBody(png);
        multipart->append(variantPart);
        multipart->append(filePart);
        auto *reply = m_network->post(apiRequest(kProfile + "/skins", token), multipart);
        multipart->setParent(reply);
        return reply;
    }, changeCape ? QString() : tr("Skin aplicada. Ela aparece no jogo na próxima vez que você entrar."),
    changeCape ? std::function<void()>([this, uuid, capeId] {
        sendCape(uuid, capeId, tr("Visual aplicado. Ele aparece no jogo na próxima vez que você entrar."));
    }) : std::function<void()>());
}

void SkinManager::resetSkin(const QString &uuid)
{
    call(uuid, [this](const QString &token) { return m_network->deleteResource(apiRequest(kProfile + "/skins/active", token)); },
         tr("Sua skin voltou a ser a padrão."));
}

void SkinManager::sendCape(const QString &uuid, const QString &capeId, const QString &success)
{
    if (capeId.isEmpty()) {
        call(uuid, [this](const QString &token) { return m_network->deleteResource(apiRequest(kProfile + "/capes/active", token)); },
             success);
        return;
    }
    call(uuid, [this, capeId](const QString &token) {
        auto request = apiRequest(kProfile + "/capes/active", token);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        return m_network->put(request, QJsonDocument(QJsonObject{{"capeId", capeId}}).toJson(QJsonDocument::Compact));
    }, success);
}

QImage SkinFrontProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const auto bar = id.lastIndexOf('|');
    const QUrl url(QUrl::fromPercentEncoding(id.left(bar).toUtf8()));
    const bool slim = id.mid(bar + 1) == QLatin1String("slim");
    const QImage skin = QImage(url.isLocalFile() ? url.toLocalFile() : url.toString()).convertToFormat(QImage::Format_RGBA8888);
    QImage front(16, 32, QImage::Format_RGBA8888);
    front.fill(Qt::transparent);
    if (isSkin(skin)) {
        const bool legacy = skin.height() == 32;
        const int arm = slim ? 3 : 4;
        QPainter painter(&front);
        // A part's front at (x, y): the inner layer, then the outer one over it.
        const auto part = [&](QRect inner, QRect outer, QPoint at, bool mirror) {
            const auto draw = [&](QRect from) {
                if (from.isNull())
                    return;
                const auto piece = skin.copy(from);
                painter.drawImage(at, mirror ? piece.mirrored(true, false) : piece);
            };
            draw(inner);
            draw(outer);
        };
        part({8, 8, 8, 8}, {40, 8, 8, 8}, {4, 0}, false);
        part({20, 20, 8, 12}, legacy ? QRect() : QRect(20, 36, 8, 12), {4, 8}, false);
        part({44, 20, arm, 12}, legacy ? QRect() : QRect(44, 36, arm, 12), {4 - arm, 8}, false);
        part(legacy ? QRect(44, 20, 4, 12) : QRect(36, 52, arm, 12), legacy ? QRect() : QRect(52, 52, arm, 12), {12, 8}, legacy);
        part({4, 20, 4, 12}, legacy ? QRect() : QRect(4, 36, 4, 12), {4, 20}, false);
        part(legacy ? QRect(4, 20, 4, 12) : QRect(20, 52, 4, 12), legacy ? QRect() : QRect(4, 52, 4, 12), {8, 20}, legacy);
    }
    const int scale = requestedSize.height() > 0 ? qMax(1, requestedSize.height() / 32) : 8;
    if (size)
        *size = QSize(16 * scale, 32 * scale);
    return front.scaled(16 * scale, 32 * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}
