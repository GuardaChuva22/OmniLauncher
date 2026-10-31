#include "ModManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrlQuery>
#include <cstdlib>
#include <cstring>
#include <iostream>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
int checks = 0;
void check(bool value, const char *message)
{
    ++checks;
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class Reply final : public QNetworkReply
{
public:
    Reply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void complete(const QByteArray &body, bool fail = false)
    {
        m_body = body;
        if (fail)
            setError(QNetworkReply::ConnectionRefusedError, "fixture error");
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    void abort() override {}
    qint64 bytesAvailable() const override { return m_body.size() - m_position + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const auto count = qMin(maxSize, qint64(m_body.size()) - m_position);
        if (count <= 0)
            return -1;
        std::memcpy(data, m_body.constData() + m_position, size_t(count));
        m_position += count;
        return count;
    }
private:
    QByteArray m_body;
    qint64 m_position = 0;
};

class Network final : public QNetworkAccessManager
{
public:
    QList<Reply *> replies;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        auto *reply = new Reply(request, this);
        replies.append(reply);
        return reply;
    }
};

QJsonArray facets(Reply *reply)
{
    return QJsonDocument::fromJson(QUrlQuery(reply->url()).queryItemValue("facets", QUrl::FullyDecoded).toUtf8()).array();
}

QByteArray hits(const QStringList &ids, int total)
{
    QJsonArray array;
    for (const auto &id : ids)
        array.append(QJsonObject{{"project_id", id}, {"title", id}});
    return QJsonDocument(QJsonObject{{"hits", array}, {"total_hits", total}}).toJson();
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    Network network;
    ModManager mods(directory.path(), nullptr, &network);

    mods.search(" create & travel ", "1.20.1", "Forge", 0, "mod", "technology", "updated");
    const auto first = network.replies.last();
    check(facets(first) == QJsonArray{QJsonArray{"project_type:mod"}, QJsonArray{"versions:1.20.1"},
                                     QJsonArray{"categories:forge"}, QJsonArray{"categories:technology"}},
          "version, loader and category are independent AND facets");
    check(QUrlQuery(first->url()).queryItemValue("query", QUrl::FullyDecoded) == "create & travel", "query preserved and trimmed");
    check(QUrlQuery(first->url()).queryItemValue("index") == "updated", "explicit sort");
    mods.search("Sodium", "", "", 0, "mod");
    const auto second = network.replies.last();
    check(facets(second) == QJsonArray{QJsonValue(QJsonArray{"project_type:mod"})}, "unchecked compatibility omits facets entirely");
    check(QUrlQuery(second->url()).queryItemValue("index") == "relevance", "automatic relevance for search");
    first->complete(hits({"stale"}, 1));
    check(mods.results().isEmpty() && mods.searching(), "old search response cannot replace newer search");
    second->complete(hits({"a", "b"}, 5));
    check(mods.results().size() == 2 && mods.searchOffset() == 2 && mods.totalHits() == 5, "first page server cursor");
    mods.search("Sodium", "", "", mods.searchOffset(), "mod");
    network.replies.last()->complete(hits({"b", "c"}, 5));
    check(mods.results().size() == 3 && mods.searchOffset() == 4, "duplicate project does not corrupt pagination cursor");
    const auto requestCount = network.replies.size();
    mods.search("Different", "", "", 4, "mod");
    check(network.replies.size() == requestCount, "stale pagination filters rejected");
    mods.search("Sodium", "", "", 4, "mod");
    network.replies.last()->complete({}, true);
    check(!mods.searchError().isEmpty() && mods.searchOffset() == 4 && mods.results().size() == 3, "failed page retains cursor and results for retry");
    mods.search("Sodium", "", "", 4, "mod");
    network.replies.last()->complete(hits({}, 5));
    check(mods.searchOffset() == mods.totalHits(), "empty final page stops pagination");
    mods.search("", "", "iris,optifine", 0, "shader");
    check(facets(network.replies.last()) == QJsonArray{QJsonArray{"project_type:shader"}, QJsonArray{"categories:iris", "categories:optifine"}},
          "shader loaders use OR in one facet group");
    check(mods.totalHits() == 0 && mods.searchOffset() == 0, "new search clears previous totals");
    network.replies.last()->complete("{}");
    check(!mods.searchError().isEmpty() && !mods.searching(), "malformed search response surfaces error");
    mods.search("", "1.21.1", "minecraft", 0, "resourcepack");
    check(facets(network.replies.last()).size() == 2, "resource pack ignores loader facet");
    network.replies.last()->complete(hits({}, 0));

    mods.fetchCategories("mod");
    const auto oldCategories = network.replies.last();
    mods.fetchCategories("shader");
    oldCategories->complete("[{\"project_type\":\"mod\",\"name\":\"technology\"}]");
    check(mods.categoriesLoading() && mods.categories().isEmpty(), "stale categories ignored");
    network.replies.last()->complete("[{\"project_type\":\"mod\",\"name\":\"technology\"},{\"project_type\":\"shader\",\"name\":\"realistic\"}]");
    check(!mods.categoriesLoading() && mods.categories().size() == 1 && mods.categories().first().toMap().value("id") == "realistic",
          "categories filtered by active content kind");

    mods.fetchProjectDetails("old", "1.20.1", "Forge");
    const auto staleDetail = network.replies[network.replies.size() - 2];
    const auto staleVersions = network.replies.last();
    mods.fetchProjectDetails("new", "1.21.1", "Fabric");
    const auto detail = network.replies[network.replies.size() - 2];
    const auto versions = network.replies.last();
    check(mods.projectLoading(), "selection begins loading and disables installation");
    check(QUrlQuery(versions->url()).queryItemValue("loaders", QUrl::FullyDecoded) == "[\"fabric\"]"
              && QUrlQuery(versions->url()).queryItemValue("game_versions", QUrl::FullyDecoded) == "[\"1.21.1\"]",
          "compatibility uses exact installation target");
    staleDetail->complete("{\"id\":\"old\",\"title\":\"Wrong selection\"}");
    staleVersions->complete("[]");
    check(mods.projectDetails().value("id") == "new" && mods.projectLoading(), "stale details and compatibility ignored");
    versions->complete("[{\"version_number\":\"2.0\",\"name\":\"Release\",\"files\":[{\"filename\":\"new.jar\"}]}]");
    check(mods.projectLoading() && mods.projectDetails().value("compatible").toBool(), "parallel compatibility result retained");
    detail->complete("{\"id\":\"new\",\"title\":\"New project\",\"body\":\"# About\",\"loaders\":[\"fabric\"],\"game_versions\":[\"1.21.1\"]}");
    check(!mods.projectLoading() && mods.projectError().isEmpty() && mods.projectDetails().value("compatible").toBool()
              && mods.projectDetails().value("title") == "New project", "details merge without losing compatibility");

    mods.fetchProjectDetails("incompatible", "1.21.1", "Forge");
    network.replies[network.replies.size() - 2]->complete("{\"id\":\"incompatible\"}");
    network.replies.last()->complete("[]");
    check(mods.projectDetails().value("compatibilityKnown").toBool() && !mods.projectDetails().value("compatible").toBool(),
          "no matching version marks project incompatible");
    mods.fetchProjectDetails("error", "1.21.1", "Forge");
    network.replies[network.replies.size() - 2]->complete("{\"id\":\"error\"}");
    network.replies.last()->complete("{}", true);
    check(!mods.projectLoading() && !mods.projectError().isEmpty() && !mods.projectDetails().value("compatibilityKnown").toBool(),
          "network error cannot be mistaken for incompatibility or unlock install");
    mods.fetchProjectDetails("untargeted", "", "");
    network.replies[network.replies.size() - 2]->complete("{\"id\":\"untargeted\"}");
    network.replies.last()->complete("[{\"files\":[{\"filename\":\"any.jar\"}]}]");
    check(!mods.projectDetails().value("compatibilityKnown").toBool() && !mods.projectDetails().value("compatible").toBool(),
          "unfiltered version request cannot unlock installation without a target");
    mods.fetchProjectDetails("shader", "1.21.1", "iris,optifine", "shader");
    check(QUrlQuery(network.replies.last()->url()).queryItemValue("loaders", QUrl::FullyDecoded) == "[\"iris\",\"optifine\"]",
          "shader compatibility checks both supported runtimes");
    network.replies[network.replies.size() - 2]->complete("{\"id\":\"shader\"}");
    network.replies.last()->complete("[{\"files\":[{\"filename\":\"not-a-shader.jar\"}]}]");
    check(!mods.projectDetails().value("compatible").toBool(), "matching version without installable file is incompatible");
    mods.fetchProjectDetails("mixed-releases", "1.21.1", "Fabric");
    network.replies[network.replies.size() - 2]->complete("{\"id\":\"mixed-releases\"}");
    network.replies.last()->complete("[{\"version_number\":\"new\",\"files\":[{\"filename\":\"new.zip\"}]},{\"version_number\":\"old\",\"files\":[{\"filename\":\"old.jar\"}]}]");
    check(!mods.projectDetails().value("compatible").toBool() && mods.projectDetails().value("versionNumber") == "new",
          "compatibility mirrors installer first release instead of falling back to older release");
    mods.fetchProjectDetails("mixed-files", "1.21.1", "Fabric");
    network.replies[network.replies.size() - 2]->complete("{\"id\":\"mixed-files\"}");
    network.replies.last()->complete("[{\"files\":[{\"filename\":\"secondary.jar\"},{\"filename\":\"primary.zip\",\"primary\":true}]}]");
    check(!mods.projectDetails().value("compatible").toBool(), "secondary jar cannot override incompatible primary file");
    mods.fetchProjectDetails("primary-jar", "1.21.1", "Fabric");
    network.replies[network.replies.size() - 2]->complete("{\"id\":\"primary-jar\"}");
    network.replies.last()->complete("[{\"files\":[{\"filename\":\"secondary.zip\"},{\"filename\":\"primary.jar\",\"primary\":true}]}]");
    check(mods.projectDetails().value("compatible").toBool(), "primary jar takes precedence over first secondary file");
    mods.fetchProjectDetails("cleared", "1.21.1", "Forge");
    const auto clearedDetail = network.replies[network.replies.size() - 2];
    const auto clearedVersions = network.replies.last();
    mods.clearProjectDetails();
    clearedDetail->complete("{\"id\":\"cleared\"}");
    clearedVersions->complete("[]");
    check(mods.projectDetails().isEmpty() && !mods.projectLoading(), "closing selection invalidates pending callbacks");

    // User actions must operate on the visible content kind, even after a catalog
    // changes the manager's shared selection. Test the QML call as well as disk state.
    const QDir fixture(directory.filePath("instances/content/mods"));
    QDir().mkpath(fixture.path());
    QDir().mkpath(directory.filePath("instances/content/shaderpacks"));
    auto write = [&](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write content fixture");
    };
    write(fixture.filePath("blur.jar"), "fixture");
    write(fixture.filePath("keep.jar"), "fixture");
    write(fixture.filePath(".omni-mods.json"), "{\"blur.jar\":{\"projectId\":\"blur\"},\"keep.jar\":{\"projectId\":\"keep\"}}");
    mods.loadInstalled("content", "shader");
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("mods", &mods);
    QQmlComponent component(&engine);
    component.setData(R"QML(import QtQml
QtObject {
    function removeVisibleMod() { return mods.remove("content", "blur.jar", "mod") }
})QML", QUrl());
    auto *action = component.create();
    check(action != nullptr, "load QML content action");
    QVariant removed;
    check(QMetaObject::invokeMethod(action, "removeVisibleMod", Q_RETURN_ARG(QVariant, removed)) && removed.toBool(),
          "QML removal reaches backend with explicit kind");
    check(!QFile::exists(fixture.filePath("blur.jar")) && QFile::exists(fixture.filePath("keep.jar")),
          "removal deletes only selected mod while shared selection is shaders");
    QFile record(fixture.filePath(".omni-mods.json"));
    check(record.open(QIODevice::ReadOnly), "read updated content metadata");
    const auto metadata = QJsonDocument::fromJson(record.readAll()).object();
    record.close();
    check(!metadata.contains("blur.jar") && metadata.contains("keep.jar"), "remove only selected mod metadata");
    check(mods.kind() == "shader", "background removal preserves current selection");
    check(!mods.remove("content", "missing.jar", "mod") && !mods.lastError().isEmpty(),
          "failed removal exposes an error instead of silently succeeding");
    check(!mods.remove("content", "../keep.jar", "mod") && QFile::exists(fixture.filePath("keep.jar")),
          "reject removal outside content folder");
    check(mods.setEnabled("content", "keep.jar", false, "mod") && mods.lastError().isEmpty()
              && QFile::exists(fixture.filePath("keep.jar.disabled")), "disable visible mod with shared shader selection");
    check(mods.remove("content", "keep.jar.disabled", "mod") && !QFile::exists(fixture.filePath("keep.jar.disabled")),
          "remove disabled mod");
#ifdef Q_OS_WIN
    write(fixture.filePath("locked.jar"), "fixture");
    const auto lockedPath = QDir::toNativeSeparators(fixture.filePath("locked.jar"));
    const auto locked = CreateFileW(reinterpret_cast<LPCWSTR>(lockedPath.utf16()), GENERIC_READ,
                                   FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "lock jar like a running Java process");
    check(!mods.remove("content", "locked.jar", "mod") && !mods.lastError().isEmpty()
              && QFile::exists(fixture.filePath("locked.jar")), "locked jar reports failure and remains on disk");
    CloseHandle(locked);
    check(mods.remove("content", "locked.jar", "mod") && mods.lastError().isEmpty(), "jar removable after game releases lock");
#endif
    delete action;
    std::cout << "PASS: " << checks << " catalog checks\n";
}
