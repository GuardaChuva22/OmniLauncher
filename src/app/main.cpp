#include "AppModel.h"
#include "Language.h"
#include "AuthManager.h"
#include "DiscordPresence.h"
#include "ServerManager.h"
#include "UiSound.h"
#include "Wrapped.h"
#include "PackCloud.h"
#include "GameRunner.h"
#include "ModManager.h"
#include "UpdateManager.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSystemTrayIcon>
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

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>
#endif

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

// The window is frameless (QML draws its own title bar), which also drops what Windows
// gives a normal window: Windows 11 rounds the corners again on request, and a
// one-pixel frame extended into the client area brings back the drop shadow.
static void decorateWindow(QWindow *window)
{
#ifdef Q_OS_WIN
    if (!window)
        return;
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    const DWORD round = 2; // DWMWCP_ROUND; DWMWA_WINDOW_CORNER_PREFERENCE (33) is ignored before Windows 11
    DwmSetWindowAttribute(hwnd, 33, &round, sizeof(round));
    const DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
    DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
#else
    Q_UNUSED(window)
#endif
}

int main(int argc, char *argv[])
{
    // QApplication (not QGuiApplication) for the Windows tray icon, which is a widget.
    QApplication app(argc, argv);
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
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/OmniLauncher/assets/brand/app-icon.png")));

    // Before anything builds text: the translator for the chosen language.
    Language language;

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
    DiscordPresence discord;
    ServerManager servers(model.dataDirectory());
    UiSound sfx;
    Wrapped wrapped(model.dataDirectory());
    PackCloud cloud(model.dataDirectory());
    QObject::connect(&updates, &UpdateManager::quitRequested, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    QQmlApplicationEngine engine;
    language.setEngine(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("i18n"), &language);
    engine.rootContext()->setContextProperty(QStringLiteral("launcher"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("runner"), &runner);
    engine.rootContext()->setContextProperty(QStringLiteral("mods"), &mods);
    engine.rootContext()->setContextProperty(QStringLiteral("auth"), &auth);
    engine.rootContext()->setContextProperty(QStringLiteral("updates"), &updates);
    engine.rootContext()->setContextProperty(QStringLiteral("discord"), &discord);
    engine.rootContext()->setContextProperty(QStringLiteral("servers"), &servers);
    engine.rootContext()->setContextProperty(QStringLiteral("sfx"), &sfx);
    engine.rootContext()->setContextProperty(QStringLiteral("wrapped"), &wrapped);
    engine.rootContext()->setContextProperty(QStringLiteral("cloud"), &cloud);

    // While a game runs the launcher can wait in the tray; QML shows/hides the icon
    // and handles `activated` (the menu's "Abrir" reuses that signal).
    QSystemTrayIcon tray(QApplication::windowIcon());
    tray.setToolTip(QStringLiteral("OmniLauncher"));
    QMenu trayMenu;
    QObject::connect(trayMenu.addAction(QStringLiteral("Abrir o OmniLauncher")), &QAction::triggered, &tray,
                     [&tray] { emit tray.activated(QSystemTrayIcon::Trigger); });
    trayMenu.addSeparator();
    QObject::connect(trayMenu.addAction(QStringLiteral("Sair")), &QAction::triggered, &app, &QCoreApplication::quit);
    tray.setContextMenu(&trayMenu);
    engine.rootContext()->setContextProperty(QStringLiteral("tray"), &tray);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("OmniLauncher"), QStringLiteral("Main"));
    if (!engine.rootObjects().isEmpty())
        decorateWindow(qobject_cast<QWindow *>(engine.rootObjects().first()));

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
