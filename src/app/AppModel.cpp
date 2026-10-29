#include "AppModel.h"

#include <QColor>
#include <QDateTime>

#include <array>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace {
QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool validImportedCover(const QString &coverId, const QString &coversDirectory)
{
    const QUrl url(coverId);
    if (!url.isLocalFile())
        return false;
    const QFileInfo image(url.toLocalFile());
    const auto suffix = image.suffix().toLower();
    if (QUuid(image.completeBaseName()).isNull()
        || (suffix != "png" && suffix != "jpg" && suffix != "webp"))
        return false;

    // Compare canonical paths where possible, so a symlink cannot escape our cover directory.
    // A removed image still has a valid stored reference; the UI can show its fallback.
    auto directory = QDir(coversDirectory).canonicalPath();
    if (directory.isEmpty())
        directory = QDir::cleanPath(QFileInfo(coversDirectory).absoluteFilePath());
    auto imageDirectory = image.absoluteDir().canonicalPath();
    if (imageDirectory.isEmpty())
        imageDirectory = QDir::cleanPath(image.absolutePath());
    if (imageDirectory.compare(directory, Qt::CaseInsensitive) != 0)
        return false;
    if (image.exists() && QFileInfo(image.canonicalFilePath()).absolutePath()
                             .compare(directory, Qt::CaseInsensitive) != 0)
        return false;
    return true;
}

QString validateGame(const QVariantMap &game, const QString &coversDirectory)
{
    const auto title = game.value("title").toString();
    if (title.trimmed().isEmpty() || title.size() > 72)
        return QStringLiteral("Escolha um nome para o jogo, com até 72 caracteres.");

    static const QRegularExpression versionPattern(QStringLiteral("^[A-Za-z0-9._+\\-]{1,32}$"));
    if (!versionPattern.match(game.value("version").toString()).hasMatch())
        return QStringLiteral("Escolha uma versão válida do Minecraft.");

    static const QSet<QString> loaders = {"Vanilla", "Fabric", "Forge", "NeoForge"};
    if (!loaders.contains(game.value("loader").toString()))
        return QStringLiteral("Escolha Vanilla, Fabric, Forge ou NeoForge.");

    static const QRegularExpression loaderVersionPattern(QStringLiteral("^[A-Za-z0-9._+\\-]{0,48}$"));
    if (!loaderVersionPattern.match(game.value("loaderVersion").toString()).hasMatch())
        return QStringLiteral("Versão do loader inválida.");

    static const QSet<QString> covers = {"meadow", "copper", "dusk", "frost"};
    const auto coverId = game.value("coverId").toString();
    if (!covers.contains(coverId) && !validImportedCover(coverId, coversDirectory))
        return QStringLiteral("Escolha uma capa disponível ou importe uma imagem do computador.");

    return {};
}

// Validated setting value, or an invalid QVariant when the key/value is not accepted.
QVariant cleanGameSetting(const QString &key, const QVariant &value)
{
    if (key == "memoryMb") {
        bool ok = false;
        const int mb = value.toInt(&ok);
        return ok && mb >= 512 && mb <= 65536 ? QVariant(mb) : QVariant();
    }
    if (key == "resolution") {
        static const QRegularExpression pattern(QStringLiteral("^(|\\d{3,5}x\\d{3,5})$"));
        const auto text = value.toString();
        return pattern.match(text).hasMatch() ? QVariant(text) : QVariant();
    }
    if (key == "jvmArgs") {
        const auto text = value.toString().trimmed();
        return text.size() <= 2000 ? QVariant(text) : QVariant();
    }
    if (key == "packIcon") {
        const auto text = value.toString();
        return text.startsWith("file:") && text.size() <= 2048 ? QVariant(text) : QVariant();
    }
    if (key == "packColor") {
        static const QRegularExpression color(QStringLiteral("^#[0-9a-fA-F]{6}$"));
        return color.match(value.toString()).hasMatch() ? value : QVariant();
    }
    if (key == "customPackUrl") {
        const auto text = value.toString().trimmed();
        const bool scheme = text.startsWith("https://") || text.startsWith("http://") || text.startsWith("file:");
        return scheme && text.size() <= 2048 ? QVariant(text) : QVariant();
    }
    if (key == "packVersionNumber" || key == "customPackVersion") {
        const auto text = value.toString().trimmed();
        return !text.isEmpty() && text.size() <= 64 ? QVariant(text) : QVariant();
    }
    if (key == "packProjectId" || key == "packVersionId") {
        static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9]{1,32}$"));
        const auto text = value.toString();
        return pattern.match(text).hasMatch() ? QVariant(text) : QVariant();
    }
    // OmniLauncher Cloud: the pack's share code, and the editor key when this PC can publish it.
    if (key == "cloudCode") {
        static const QRegularExpression pattern(QStringLiteral("^[A-Z0-9]{6}$"));
        return pattern.match(value.toString()).hasMatch() ? value : QVariant();
    }
    if (key == "cloudKey") {
        static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
        return pattern.match(value.toString()).hasMatch() ? value : QVariant();
    }
    if (key == "lastPlayed")
        return QDateTime::fromString(value.toString(), Qt::ISODate).isValid() ? QVariant(value.toString()) : QVariant();
    if (key == "playSeconds") {
        bool ok = false;
        const auto seconds = value.toLongLong(&ok);
        return ok && seconds >= 0 ? QVariant(seconds) : QVariant();
    }
    return {};
}

QVariant cleanPreference(const QString &key, const QVariant &value)
{
    if (key == "defaultMemoryMb")
        return cleanGameSetting("memoryMb", value);
    if (key == "afterLaunch") {
        const auto text = value.toString();
        return text == "minimize" || text == "close" || text == "keep" ? QVariant(text) : QVariant();
    }
    return {};
}

// Background for a logo tile. An opaque, square logo continues into its own edge color;
// a logo with transparency sits on a darkened version of its dominant color, so it stays visible.
QString tileColorFor(const QImage &source)
{
    const auto image = source.convertToFormat(QImage::Format_ARGB32).scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    qint64 edgeR = 0, edgeG = 0, edgeB = 0, edgeCount = 0, edgeOpaque = 0;
    QHash<int, std::array<double, 4>> buckets; // weight, r, g, b
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = QColor::fromRgba(image.pixel(x, y));
            const bool edge = x == 0 || y == 0 || x == image.width() - 1 || y == image.height() - 1;
            if (edge) {
                ++edgeCount;
                if (color.alpha() > 240) {
                    ++edgeOpaque;
                    edgeR += color.red(); edgeG += color.green(); edgeB += color.blue();
                }
            }
            if (color.alpha() < 200 || color.valueF() < 0.1 || (color.saturationF() < 0.08 && color.valueF() > 0.92))
                continue;
            const int key = (color.red() >> 5) << 6 | (color.green() >> 5) << 3 | (color.blue() >> 5);
            auto &bucket = buckets[key];
            const double weight = 1.0 + 2.0 * color.saturationF();
            bucket[0] += weight;
            bucket[1] += color.red() * weight; bucket[2] += color.green() * weight; bucket[3] += color.blue() * weight;
        }
    }
    if (edgeCount > 0 && edgeOpaque == edgeCount)
        return QColor(int(edgeR / edgeOpaque), int(edgeG / edgeOpaque), int(edgeB / edgeOpaque)).name();
    std::array<double, 4> best{0, 40, 40, 48};
    for (const auto &bucket : buckets)
        if (bucket[0] > best[0])
            best = bucket;
    QColor dominant = best[0] > 0 ? QColor(int(best[1] / best[0]), int(best[2] / best[0]), int(best[3] / best[0]))
                                  : QColor(40, 40, 48);
    float h, s, v;
    dominant.getHsvF(&h, &s, &v);
    return QColor::fromHsvF(h < 0 ? 0 : h, qMin(s, 0.65f), 0.3f).name();
}

bool validOfflineName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_]{3,16}$"));
    return pattern.match(name).hasMatch();
}

QVariantMap makeGame(const QString &title, const QString &version,
                     const QString &loader, const QString &coverId, const QString &loaderVersion = {})
{
    QVariantMap game{{"id", newId()}, {"title", title.trimmed()}, {"version", version.trimmed()},
                     {"loader", loader}, {"coverId", coverId}};
    if (loader != "Vanilla" && !loaderVersion.trimmed().isEmpty())
        game.insert("loaderVersion", loaderVersion.trimmed());
    return game;
}

int indexOfId(const QVariantList &items, const QString &id)
{
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (items.at(i).toMap().value("id").toString() == id)
            return static_cast<int>(i);
    }
    return -1;
}

bool readItems(const QJsonArray &array, bool isGame, const QString &coversDirectory, QVariantList &result)
{
    QSet<QString> ids;
    QSet<QString> offlineNames;
    for (const auto &value : array) {
        if (!value.isObject())
            return false;
        const auto object = value.toObject();
        const auto id = object.value("id");
        if (!id.isString() || QUuid(id.toString()).isNull() || ids.contains(id.toString()))
            return false;
        ids.insert(id.toString());

        const QStringList fields = isGame
            ? QStringList{"title", "version", "loader", "coverId"}
            : QStringList{"name", "type", "avatar"};
        for (const auto &field : fields) {
            if (!object.value(field).isString())
                return false;
        }
        const auto item = object.toVariantMap();
        if (isGame) {
            if (!validateGame(item, coversDirectory).isEmpty())
                return false;
        } else {
            const auto name = item.value("name").toString();
            const auto type = item.value("type").toString();
            static const QRegularExpression uuidPattern(QStringLiteral("^[0-9a-f]{32}$"));
            const bool offline = type == "offline" && item.value("avatar").toString() == "steve";
            const bool microsoft = type == "microsoft" && item.value("avatar").toString() == "skin"
                && uuidPattern.match(item.value("uuid").toString()).hasMatch();
            if ((!offline && !microsoft) || !validOfflineName(name))
                return false;
            if (offline) {
                if (offlineNames.contains(name.toCaseFolded()))
                    return false;
                offlineNames.insert(name.toCaseFolded());
            }
        }
        result.append(item);
    }
    return true;
}
} // namespace

AppModel::AppModel(const QString &dataDirectory, QObject *parent)
    : QObject(parent)
{
    const auto directory = dataDirectory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : dataDirectory;
    m_filePath = QDir(directory).filePath(QStringLiteral("library.json"));
    m_coversDirectory = QDir(directory).absoluteFilePath(QStringLiteral("covers"));
    m_dataDirectory = QDir(directory).absolutePath();
    load();
}

QVariantMap AppModel::activeProfile() const
{
    const auto index = indexOfId(m_profiles, m_activeProfileId);
    return index >= 0 ? m_profiles.at(index).toMap() : QVariantMap{};
}

void AppModel::setError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    emit lastErrorChanged();
}

void AppModel::clearError()
{
    setError({});
}

void AppModel::load()
{
    QFile file(m_filePath);
    if (!file.exists()) {
        m_games = {
            makeGame(QStringLiteral("Vanilla"), "1.21.1", "Vanilla", "meadow"),
            makeGame(QStringLiteral("Create com amigos"), "1.20.1", "Fabric", "copper"),
            makeGame(QStringLiteral("Além do horizonte"), "1.21.1", "NeoForge", "dusk"),
            makeGame(QStringLiteral("Better Adventures"), "1.20.1", "Forge", "frost")
        };
        persist(m_games, m_profiles, m_activeProfileId, m_reducedMotion);
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_storageWritable = false;
        setError(QStringLiteral("Não foi possível ler sua biblioteca. O arquivo foi preservado: %1")
                     .arg(file.errorString()));
        return;
    }
    // A library should remain small. Refuse an unexpected large file before parsing it.
    if (file.size() > 4 * 1024 * 1024) {
        m_storageWritable = false;
        setError(QStringLiteral("O arquivo da biblioteca é maior que o esperado e foi preservado."));
        return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto root = document.object();
    QVariantList games;
    QVariantList profiles;
    const auto activeId = root.value("activeProfileId").toString();
    const bool valid = parseError.error == QJsonParseError::NoError && document.isObject()
        && root.value("schemaVersion").toInt(-1) == 1
        && root.value("games").isArray() && root.value("profiles").isArray()
        && root.value("activeProfileId").isString() && root.value("reducedMotion").isBool()
        && readItems(root.value("games").toArray(), true, m_coversDirectory, games)
        && readItems(root.value("profiles").toArray(), false, m_coversDirectory, profiles)
        && (activeId.isEmpty() || indexOfId(profiles, activeId) >= 0);
    if (!valid) {
        m_storageWritable = false;
        setError(QStringLiteral("A biblioteca não pôde ser carregada. O arquivo original foi preservado; "
                                "recupere-o ou renomeie library.json antes de salvar alterações."));
        return;
    }
    m_games = games;
    m_profiles = profiles;
    m_activeProfileId = activeId;
    m_reducedMotion = root.value("reducedMotion").toBool();

    // Settings are optional and read leniently: an unknown or invalid value is
    // dropped instead of blocking the whole library.
    const auto storedSettings = root.value("gameSettings").toObject();
    for (auto it = storedSettings.begin(); it != storedSettings.end(); ++it) {
        if (indexOfId(m_games, it.key()) < 0 || !it.value().isObject())
            continue;
        QVariantMap clean;
        const auto values = it.value().toObject().toVariantMap();
        for (auto value = values.begin(); value != values.end(); ++value) {
            const auto cleaned = cleanGameSetting(value.key(), value.value());
            if (cleaned.isValid())
                clean.insert(value.key(), cleaned);
        }
        if (!clean.isEmpty())
            m_gameSettings.insert(it.key(), clean);
    }
    const auto storedPreferences = root.value("preferences").toObject().toVariantMap();
    for (auto it = storedPreferences.begin(); it != storedPreferences.end(); ++it) {
        const auto cleaned = cleanPreference(it.key(), it.value());
        if (cleaned.isValid())
            m_preferences.insert(it.key(), cleaned);
    }
}

bool AppModel::persist(const QVariantList &games, const QVariantList &profiles,
                       const QString &activeId, bool reducedMotion)
{
    if (!m_storageWritable) {
        setError(QStringLiteral("O arquivo original da biblioteca foi preservado. "
                                "Recupere-o ou renomeie library.json antes de salvar alterações."));
        return false;
    }
    const auto parentDirectory = QFileInfo(m_filePath).absolutePath();
    if (!QDir().mkpath(parentDirectory)) {
        setError(QStringLiteral("Não foi possível criar a pasta dos dados do OmniLauncher."));
        return false;
    }
    const QJsonObject root{
        {"schemaVersion", 1}, {"games", QJsonArray::fromVariantList(games)},
        {"profiles", QJsonArray::fromVariantList(profiles)},
        {"activeProfileId", activeId}, {"reducedMotion", reducedMotion},
        {"gameSettings", QJsonObject::fromVariantMap(m_gameSettings)},
        {"preferences", QJsonObject::fromVariantMap(m_preferences)}
    };
    const auto data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        setError(QStringLiteral("Não foi possível salvar suas alterações: %1").arg(file.errorString()));
        return false;
    }
    clearError();
    return true;
}

QString AppModel::addGame(const QString &title, const QString &version,
                          const QString &loader, const QString &coverId, const QString &loaderVersion)
{
    clearError();
    const auto game = makeGame(title, version, loader, coverId, loaderVersion);
    const auto error = validateGame(game, m_coversDirectory);
    if (!error.isEmpty()) {
        setError(error);
        return {};
    }
    auto games = m_games;
    games.append(game);
    if (!persist(games, m_profiles, m_activeProfileId, m_reducedMotion))
        return {};
    m_games = games;
    emit gamesChanged();
    return game.value("id").toString();
}

bool AppModel::updateGame(const QString &id, const QString &title, const QString &version,
                          const QString &loader, const QString &coverId, const QString &loaderVersion)
{
    clearError();
    const auto index = indexOfId(m_games, id);
    if (index < 0) {
        setError(QStringLiteral("Esse jogo não está mais na biblioteca."));
        return false;
    }
    auto game = makeGame(title, version, loader, coverId, loaderVersion);
    game.insert("id", id);
    const auto error = validateGame(game, m_coversDirectory);
    if (!error.isEmpty()) {
        setError(error);
        return false;
    }
    auto games = m_games;
    games[index] = game;
    if (!persist(games, m_profiles, m_activeProfileId, m_reducedMotion))
        return false;
    m_games = games;
    emit gamesChanged();
    return true;
}

bool AppModel::removeGame(const QString &id)
{
    clearError();
    const auto index = indexOfId(m_games, id);
    if (index < 0) {
        setError(QStringLiteral("Esse jogo não está mais na biblioteca."));
        return false;
    }
    auto games = m_games;
    games.removeAt(index);
    const auto previousSettings = m_gameSettings;
    m_gameSettings.remove(id);
    if (!persist(games, m_profiles, m_activeProfileId, m_reducedMotion)) {
        m_gameSettings = previousSettings;
        return false;
    }
    m_games = games;
    emit gamesChanged();
    if (previousSettings.contains(id))
        emit gameSettingsChanged();
    return true;
}

QString AppModel::importCover(const QUrl &source)
{
    clearError();
    if (!source.isLocalFile()) {
        setError(QStringLiteral("Escolha uma imagem salva no computador."));
        return {};
    }
    QFile file(source.toLocalFile());
    constexpr qint64 maxBytes = 20 * 1024 * 1024;
    if (!file.open(QIODevice::ReadOnly) || file.size() < 1 || file.size() > maxBytes) {
        setError(QStringLiteral("Escolha uma imagem PNG, JPG ou WebP de até 20 MB."));
        return {};
    }
    QImageReader reader(&file);
    const auto format = reader.format().toLower();
    const auto size = reader.size();
    if ((format != "png" && format != "jpeg" && format != "webp")
        || !size.isValid() || size.width() > 8192 || size.height() > 8192
        || qint64(size.width()) * size.height() > 24 * 1024 * 1024) {
        setError(QStringLiteral("Use uma imagem PNG, JPG ou WebP de até 8192 px e 24 megapixels."));
        return {};
    }
    // Decode once before copying: a plausible image header alone is not sufficient.
    if (reader.read().isNull()) {
        setError(QStringLiteral("Essa imagem não pôde ser aberta. Escolha outro arquivo."));
        return {};
    }
    if (!file.seek(0) || !QDir().mkpath(m_coversDirectory)) {
        setError(QStringLiteral("Não foi possível preparar a cópia da capa."));
        return {};
    }
    const auto bytes = file.read(maxBytes + 1);
    if (bytes.isEmpty() || bytes.size() > maxBytes || file.error() != QFileDevice::NoError) {
        setError(QStringLiteral("Não foi possível ler a imagem selecionada."));
        return {};
    }
    const auto extension = format == "jpeg" ? QStringLiteral("jpg") : QString::fromLatin1(format);
    const auto destination = QDir(m_coversDirectory).filePath(newId() + "." + extension);
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        setError(QStringLiteral("Não foi possível salvar uma cópia da capa: %1").arg(output.errorString()));
        return {};
    }
    return QUrl::fromLocalFile(destination).toString();
}

bool AppModel::addOfflineProfile(const QString &username)
{
    clearError();
    const auto name = username.trimmed();
    if (!validOfflineName(name)) {
        setError(QStringLiteral("Use de 3 a 16 caracteres: letras, números ou sublinhado (_)."));
        return false;
    }
    for (const auto &value : m_profiles) {
        if (value.toMap().value("name").toString().compare(name, Qt::CaseInsensitive) == 0) {
            setError(QStringLiteral("Já existe um perfil com esse nome."));
            return false;
        }
    }
    const auto id = newId();
    auto profiles = m_profiles;
    profiles.append(QVariantMap{{"id", id}, {"name", name}, {"type", "offline"}, {"avatar", "steve"}});
    if (!persist(m_games, profiles, id, m_reducedMotion))
        return false;
    m_profiles = profiles;
    m_activeProfileId = id;
    emit profilesChanged();
    emit activeProfileChanged();
    return true;
}

bool AppModel::selectProfile(const QString &id)
{
    clearError();
    if (indexOfId(m_profiles, id) < 0) {
        setError(QStringLiteral("Esse perfil não está mais disponível."));
        return false;
    }
    if (m_activeProfileId == id)
        return true;
    if (!persist(m_games, m_profiles, id, m_reducedMotion))
        return false;
    m_activeProfileId = id;
    emit activeProfileChanged();
    return true;
}

bool AppModel::removeProfile(const QString &id)
{
    clearError();
    const auto index = indexOfId(m_profiles, id);
    if (index < 0) {
        setError(QStringLiteral("Esse perfil não está mais disponível."));
        return false;
    }
    auto profiles = m_profiles;
    profiles.removeAt(index);
    const auto nextActive = m_activeProfileId == id
        ? (profiles.isEmpty() ? QString{} : profiles.first().toMap().value("id").toString())
        : m_activeProfileId;
    if (!persist(m_games, profiles, nextActive, m_reducedMotion))
        return false;
    m_profiles = profiles;
    m_activeProfileId = nextActive;
    emit profilesChanged();
    emit activeProfileChanged();
    return true;
}

void AppModel::setReducedMotion(bool enabled)
{
    clearError();
    if (m_reducedMotion == enabled)
        return;
    if (!persist(m_games, m_profiles, m_activeProfileId, enabled))
        return;
    m_reducedMotion = enabled;
    emit reducedMotionChanged();
}

QVariantMap AppModel::preferences() const
{
    QVariantMap result{{"defaultMemoryMb", 4096}, {"afterLaunch", QStringLiteral("minimize")}};
    for (auto it = m_preferences.begin(); it != m_preferences.end(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

QVariantMap AppModel::launchSettings(const QString &id) const
{
    const auto defaults = preferences();
    QVariantMap result{{"memoryMb", defaults.value("defaultMemoryMb")}, {"resolution", QString()},
                       {"jvmArgs", QString()}, {"afterLaunch", defaults.value("afterLaunch")}};
    const auto own = m_gameSettings.value(id).toMap();
    for (auto it = own.begin(); it != own.end(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

bool AppModel::setGameSetting(const QString &id, const QString &key, const QVariant &value)
{
    clearError();
    if (indexOfId(m_games, id) < 0) {
        setError(QStringLiteral("Esse jogo não está mais na biblioteca."));
        return false;
    }
    if (key != "memoryMb" && key != "resolution" && key != "jvmArgs") {
        setError(QStringLiteral("Ajuste desconhecido."));
        return false;
    }
    const auto cleaned = cleanGameSetting(key, value);
    if (!cleaned.isValid()) {
        setError(QStringLiteral("Valor inválido para esse ajuste."));
        return false;
    }
    auto settings = m_gameSettings.value(id).toMap();
    if (settings.value(key) == cleaned)
        return true;
    const auto previous = m_gameSettings;
    settings.insert(key, cleaned);
    m_gameSettings.insert(id, settings);
    if (!persist(m_games, m_profiles, m_activeProfileId, m_reducedMotion)) {
        m_gameSettings = previous;
        return false;
    }
    emit gameSettingsChanged();
    return true;
}

bool AppModel::setPreference(const QString &key, const QVariant &value)
{
    clearError();
    const auto cleaned = cleanPreference(key, value);
    if (!cleaned.isValid()) {
        setError(QStringLiteral("Valor inválido para essa preferência."));
        return false;
    }
    if (preferences().value(key) == cleaned)
        return true;
    const auto previous = m_preferences;
    m_preferences.insert(key, cleaned);
    if (!persist(m_games, m_profiles, m_activeProfileId, m_reducedMotion)) {
        m_preferences = previous;
        return false;
    }
    emit preferencesChanged();
    return true;
}

void AppModel::recordSession(const QString &id, qint64 seconds)
{
    if (indexOfId(m_games, id) < 0)
        return;
    const auto previous = m_gameSettings;
    auto settings = m_gameSettings.value(id).toMap();
    settings.insert("lastPlayed", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    settings.insert("playSeconds", settings.value("playSeconds").toLongLong() + qMax<qint64>(0, seconds));
    m_gameSettings.insert(id, settings);
    if (!persist(m_games, m_profiles, m_activeProfileId, m_reducedMotion)) {
        m_gameSettings = previous;
        return;
    }
    emit gameSettingsChanged();
}

bool AppModel::addMicrosoftProfile(const QString &uuid, const QString &name)
{
    clearError();
    static const QRegularExpression uuidPattern(QStringLiteral("^[0-9a-f]{32}$"));
    if (!uuidPattern.match(uuid).hasMatch() || !validOfflineName(name)) {
        setError(QStringLiteral("A Microsoft retornou um perfil inválido."));
        return false;
    }
    auto profiles = m_profiles;
    QString id;
    for (qsizetype i = 0; i < profiles.size(); ++i) {
        auto profile = profiles.at(i).toMap();
        if (profile.value("type") == "microsoft" && profile.value("uuid") == uuid) {
            id = profile.value("id").toString();
            profile.insert("name", name); // The player may have renamed it.
            profiles[i] = profile;
        }
    }
    if (id.isEmpty()) {
        id = newId();
        profiles.append(QVariantMap{{"id", id}, {"name", name}, {"type", "microsoft"}, {"avatar", "skin"}, {"uuid", uuid}});
    }
    if (!persist(m_games, profiles, id, m_reducedMotion))
        return false;
    m_profiles = profiles;
    m_activeProfileId = id;
    emit profilesChanged();
    emit activeProfileChanged();
    return true;
}

void AppModel::setPackSource(const QString &id, const QString &projectId, const QString &versionId,
                             const QString &versionNumber)
{
    if (indexOfId(m_games, id) < 0)
        return;
    const auto previous = m_gameSettings;
    auto settings = m_gameSettings.value(id).toMap();
    const QList<QPair<QString, QString>> values{{"packProjectId", projectId}, {"packVersionId", versionId},
                                                 {"packVersionNumber", versionNumber}};
    for (const auto &[key, value] : values) {
        const auto cleaned = cleanGameSetting(key, value);
        if (cleaned.isValid())
            settings.insert(key, cleaned);
    }
    m_gameSettings.insert(id, settings);
    if (!persist(m_games, m_profiles, m_activeProfileId, m_reducedMotion)) {
        m_gameSettings = previous;
        return;
    }
    emit gameSettingsChanged();
}

void AppModel::setCustomPack(const QString &id, const QString &url, const QString &version)
{
    if (indexOfId(m_games, id) < 0)
        return;
    const auto previous = m_gameSettings;
    auto settings = m_gameSettings.value(id).toMap();
    const auto cleanUrl = cleanGameSetting("customPackUrl", url);
    const auto cleanVersion = cleanGameSetting("customPackVersion", version);
    if (!cleanUrl.isValid() || !cleanVersion.isValid())
        return;
    settings.insert("customPackUrl", cleanUrl);
    settings.insert("customPackVersion", cleanVersion);
    m_gameSettings.insert(id, settings);
    if (!persist(m_games, m_profiles, m_activeProfileId, m_reducedMotion)) {
        m_gameSettings = previous;
        return;
    }
    emit gameSettingsChanged();
}

void AppModel::setCloudPack(const QString &id, const QString &code, const QString &editorKey)
{
    if (indexOfId(m_games, id) < 0)
        return;
    const auto previous = m_gameSettings;
    auto settings = m_gameSettings.value(id).toMap();
    const auto cleanCode = cleanGameSetting("cloudCode", code);
    if (!cleanCode.isValid())
        return;
    settings.insert("cloudCode", cleanCode);
    const auto cleanKey = cleanGameSetting("cloudKey", editorKey);
    if (cleanKey.isValid())
        settings.insert("cloudKey", cleanKey);
    m_gameSettings.insert(id, settings);
    if (!persist(m_games, m_profiles, m_activeProfileId, m_reducedMotion)) {
        m_gameSettings = previous;
        return;
    }
    emit gameSettingsChanged();
}

bool AppModel::setPackArt(const QString &id, const QString &iconUrl)
{
    if (indexOfId(m_games, id) < 0)
        return false;
    const auto imported = importCover(QUrl(iconUrl));
    if (imported.isEmpty())
        return false;
    const QImage logo(QUrl(imported).toLocalFile());
    if (logo.isNull())
        return false;
    const auto previous = m_gameSettings;
    auto settings = m_gameSettings.value(id).toMap();
    settings.insert("packIcon", imported);
    settings.insert("packColor", tileColorFor(logo));
    m_gameSettings.insert(id, settings);
    if (!persist(m_games, m_profiles, m_activeProfileId, m_reducedMotion)) {
        m_gameSettings = previous;
        return false;
    }
    emit gameSettingsChanged();
    return true;
}
