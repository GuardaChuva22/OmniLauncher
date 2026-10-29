#include "AppModel.h"
#include "AuthManager.h"
#include "GameRunner.h"
#include "ModManager.h"
#include "UpdateManager.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUrl>
#include <QTimer>

// %APPDATA%\OmniLauncher. Earlier builds used %APPDATA%\OmniLauncher\OmniLauncher
// (organization + application name); that folder is moved up once, keeping downloads.
static QString defaultDataDirectory()
{
    const auto legacy = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).absolutePath();
    const auto target = QFileInfo(legacy).absolutePath();
    if (!QDir(legacy).exists() || QFile::exists(QDir(target).filePath(QStringLiteral("library.json"))))
        return target;
    const QDir from(legacy);
    for (const auto &entry : from.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
        if (!QFileInfo::exists(QDir(target).filePath(entry)))
            QDir().rename(from.filePath(entry), QDir(target).filePath(entry));
    }
    // Imported covers are stored as absolute file URLs: point them at the new folder.
    QFile library(QDir(target).filePath(QStringLiteral("library.json")));
    if (library.open(QIODevice::ReadWrite)) {
        auto text = QString::fromUtf8(library.readAll());
        text.replace(QUrl::fromLocalFile(from.filePath(QStringLiteral("covers"))).toString(),
                     QUrl::fromLocalFile(QDir(target).filePath(QStringLiteral("covers"))).toString());
        library.resize(0);
        library.write(text.toUtf8());
    }
    QDir().rmdir(legacy); // Only succeeds when everything moved.
    return target;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QCoreApplication::setOrganizationName(QStringLiteral("OmniLauncher"));
    QCoreApplication::setApplicationName(QStringLiteral("OmniLauncher"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // Brand typeface (Manrope, OFL) and icon ship inside the QML module resources.
    for (const auto *weight : {"Regular", "Medium", "SemiBold", "Bold"}) {
        const auto path = QStringLiteral(":/qt/qml/OmniLauncher/assets/fonts/Manrope-%1.ttf").arg(QLatin1StringView(weight));
        if (QFontDatabase::addApplicationFont(path) < 0)
            qWarning("Não foi possível carregar a fonte %s.", qPrintable(path));
    }
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/OmniLauncher/assets/brand/app-icon.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OmniLauncher — protótipo da experiência desktop"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("Salva uma captura após abrir a interface."), QStringLiteral("arquivo")});
    parser.addOption({QStringLiteral("quit-after"), QStringLiteral("Encerra após o número de milissegundos informado."), QStringLiteral("ms")});
    parser.addOption({QStringLiteral("data-dir"), QStringLiteral("Pasta alternativa de dados para testes isolados."), QStringLiteral("pasta")});
    parser.addOption({QStringLiteral("window-size"), QStringLiteral("Dimensões da janela para revisão visual (ex.: 1000x680)."), QStringLiteral("larguraxaltura")});
    parser.process(app);

    if (parser.isSet(QStringLiteral("data-dir"))) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir(parser.value(QStringLiteral("data-dir"))).absolutePath());
    }

    AppModel model(parser.isSet(QStringLiteral("data-dir")) ? parser.value(QStringLiteral("data-dir"))
                                                            : defaultDataDirectory());
    GameRunner runner(model.dataDirectory());
    ModManager mods(model.dataDirectory());
    AuthManager auth(model.dataDirectory());
    UpdateManager updates;
    QObject::connect(&updates, &UpdateManager::quitRequested, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("launcher"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("runner"), &runner);
    engine.rootContext()->setContextProperty(QStringLiteral("mods"), &mods);
    engine.rootContext()->setContextProperty(QStringLiteral("auth"), &auth);
    engine.rootContext()->setContextProperty(QStringLiteral("updates"), &updates);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("OmniLauncher"), QStringLiteral("Main"));

    if (parser.isSet(QStringLiteral("window-size"))) {
        const auto match = QRegularExpression(QStringLiteral("^(\\d{3,4})x(\\d{3,4})$")).match(parser.value(QStringLiteral("window-size")));
        auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!match.hasMatch() || !window) return 2;
        window->resize(qMax(1000, match.captured(1).toInt()), qMax(680, match.captured(2).toInt()));
    }

    if (parser.isSet(QStringLiteral("screenshot"))) {
        const auto screenshotPath = QFileInfo(parser.value(QStringLiteral("screenshot"))).absoluteFilePath();
        QTimer::singleShot(1400, &app, [&engine, screenshotPath] {
            auto *window = engine.rootObjects().isEmpty()
                ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!window || !QDir().mkpath(QFileInfo(screenshotPath).absolutePath())
                || !window->grabWindow().save(screenshotPath)) {
                qWarning("Não foi possível salvar a captura da interface.");
                QCoreApplication::exit(2);
            }
        });
    }
    if (parser.isSet(QStringLiteral("quit-after"))) {
        bool valid = false;
        const auto milliseconds = parser.value(QStringLiteral("quit-after")).toInt(&valid);
        if (!valid || milliseconds < 1) {
            qWarning("--quit-after precisa ser um número positivo de milissegundos.");
            return 2;
        }
        QTimer::singleShot(milliseconds, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
