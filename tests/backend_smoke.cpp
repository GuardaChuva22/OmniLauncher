#include "AppModel.h"
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

int checks = 0;
void check(bool value, const char *message)
{
    ++checks;
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("OmniBackendSmoke");
    QTemporaryDir directory;
    check(directory.isValid(), "temporary directory");
    AppModel model(directory.path());
    check(model.lastError().isEmpty(), "first-run storage");
    check(model.games().size() == 4, "seed library only on first run");
    check(model.profiles().isEmpty() && model.activeProfile().isEmpty(), "no invented account");
    const auto newGame = model.addGame("  Teste local  ", "1.21.1", "Fabric", "meadow");
    check(!newGame.isEmpty(), "game created");
    check(model.games().last().toMap().value("title") == "Teste local", "trim game name");
    check(model.addGame("Invalid", "wrong version", "Fabric", "meadow").isEmpty(), "invalid version rejected");
    check(model.games().size() == 5, "failed mutation does not change collection");
    check(model.updateGame(newGame, "Atualizado", "1.20.1", "Forge", "copper"), "edit game");
    check(model.games().last().toMap().value("id") == newGame, "edit preserves game identity");
    check(!model.addOfflineProfile("ab"), "short player name rejected");
    check(!model.addOfflineProfile("player with spaces"), "spaces rejected in player name");
    check(model.addOfflineProfile(" Player_1 "), "offline profile created");
    const auto firstProfile = model.activeProfileId();
    check(model.activeProfile().value("name") == "Player_1", "new profile selected");
    check(!model.addOfflineProfile("player_1"), "case-insensitive duplicate rejected");
    check(model.addOfflineProfile("Player_2"), "second profile created");
    const auto secondProfile = model.activeProfileId();
    check(firstProfile != secondProfile, "distinct profiles");
    check(model.selectProfile(firstProfile), "fast switch");
    check(model.games().size() == 5, "switching profiles does not duplicate games");
    model.setReducedMotion(true);
    AppModel reloaded(directory.path());
    check(reloaded.lastError().isEmpty(), "saved JSON loads");
    check(reloaded.activeProfileId() == firstProfile, "selection persisted");
    check(reloaded.reducedMotion(), "motion preference persisted");
    check(reloaded.games().last().toMap().value("title") == "Atualizado", "game edit persisted");

    const auto sourcePath = directory.filePath("original.png");
    QImage cover(64, 96, QImage::Format_RGB32);
    cover.fill(Qt::darkGreen);
    check(cover.save(sourcePath), "create source cover");
    check(reloaded.addGame("External", "1.21.1", "Vanilla", QUrl::fromLocalFile(sourcePath).toString()).isEmpty(), "external cover URL requires import");
    const auto imported = reloaded.importCover(QUrl::fromLocalFile(sourcePath));
    check(!imported.isEmpty() && QFile::exists(QUrl(imported).toLocalFile()), "cover copied into owned directory");
    check(QFile::exists(sourcePath), "original cover remains untouched");
    check(!reloaded.addGame("Minha capa", "1.21.1", "Vanilla", imported).isEmpty(), "imported cover accepted");
    AppModel customCoverReloaded(directory.path());
    check(customCoverReloaded.lastError().isEmpty() && customCoverReloaded.games().last().toMap().value("coverId") == imported, "custom cover persisted");
    check(QFile::remove(QUrl(imported).toLocalFile()), "remove cached image fixture");
    AppModel missingCover(directory.path());
    check(missingCover.lastError().isEmpty() && missingCover.games().size() == 6, "missing cover does not corrupt library");
    for (const auto *format : {"JPG", "WEBP"}) {
        const auto source = directory.filePath(QString("original.") + QString::fromLatin1(format).toLower());
        check(cover.save(source, format), "create JPEG/WebP fixture");
        const auto importedFormat = reloaded.importCover(QUrl::fromLocalFile(source));
        check(!importedFormat.isEmpty(), "JPEG/WebP decoded and imported");
        check(QFile::exists(source), "JPEG/WebP source preserved");
    }
    check(reloaded.importCover(QUrl("https://example.com/image.png")).isEmpty(), "remote import refused");
    QFile fake(directory.filePath("fake.png"));
    check(fake.open(QIODevice::WriteOnly) && fake.write("not an image") > 0, "create malformed image fixture");
    fake.close();
    check(reloaded.importCover(QUrl::fromLocalFile(fake.fileName())).isEmpty(), "malformed image refused");

    // Launch settings: per-game values over global defaults, validated and persisted.
    const auto settingsGame = reloaded.games().first().toMap().value("id").toString();
    check(reloaded.launchSettings(settingsGame).value("memoryMb") == 4096, "default memory");
    check(reloaded.setPreference("defaultMemoryMb", 6144), "set default memory");
    check(reloaded.launchSettings(settingsGame).value("memoryMb") == 6144, "game follows global default");
    check(reloaded.setGameSetting(settingsGame, "memoryMb", 3072), "set game memory");
    check(reloaded.setGameSetting(settingsGame, "resolution", "1920x1080"), "set resolution");
    check(!reloaded.setGameSetting(settingsGame, "resolution", "big"), "invalid resolution refused");
    check(!reloaded.setGameSetting(settingsGame, "memoryMb", 64), "tiny memory refused");
    check(!reloaded.setGameSetting(settingsGame, "javaPath", "C:/x"), "unknown setting refused");
    check(!reloaded.setPreference("afterLaunch", "explode"), "invalid preference refused");
    check(reloaded.setPreference("afterLaunch", "keep"), "set after-launch preference");
    reloaded.recordSession(settingsGame, 125);
    reloaded.recordSession(settingsGame, 5);
    AppModel settingsReloaded(directory.path());
    const auto saved = settingsReloaded.launchSettings(settingsGame);
    check(saved.value("memoryMb") == 3072 && saved.value("resolution") == "1920x1080", "game settings persisted");
    check(saved.value("afterLaunch") == "keep", "preference persisted");
    check(settingsReloaded.gameSettings().value(settingsGame).toMap().value("playSeconds").toLongLong() == 130, "play time accumulates");
    check(!settingsReloaded.gameSettings().value(settingsGame).toMap().value("lastPlayed").toString().isEmpty(), "last session recorded");

    check(reloaded.removeProfile(firstProfile) && reloaded.activeProfileId() == secondProfile, "active profile removal selects remaining");
    check(reloaded.removeProfile(secondProfile) && reloaded.activeProfile().isEmpty(), "last profile removal");
    for (const auto &game : reloaded.games())
        check(reloaded.removeGame(game.toMap().value("id").toString()), "remove game");
    AppModel empty(directory.path());
    check(empty.games().isEmpty() && empty.profiles().isEmpty(), "empty saved library is not reseeded");

    QTemporaryDir corruptDirectory;
    QFile corrupt(corruptDirectory.filePath("library.json"));
    const QByteArray badJson("{\"schemaVersion\":1, broken");
    check(corrupt.open(QIODevice::WriteOnly) && corrupt.write(badJson) == badJson.size(), "write malformed fixture");
    corrupt.close();
    AppModel broken(corruptDirectory.path());
    check(!broken.lastError().isEmpty(), "malformed JSON reported");
    check(broken.addGame("Preserve", "1.21.1", "Vanilla", "meadow").isEmpty(), "refuse overwriting malformed library");
    check(!broken.addOfflineProfile("Player_3"), "profile mutation also preserves malformed library");
    check(corrupt.open(QIODevice::ReadOnly) && corrupt.readAll() == badJson, "original bytes preserved");

    QTemporaryDir blockedDirectory;
    QFile block(blockedDirectory.filePath("not-a-directory"));
    check(block.open(QIODevice::WriteOnly), "create blocked storage fixture");
    block.close();
    AppModel blocked(block.fileName());
    check(!blocked.lastError().isEmpty(), "storage creation failure reported");
    check(blocked.addGame("Fail save", "1.21.1", "Vanilla", "meadow").isEmpty(), "write failure prevents apparent success");
    check(blocked.games().size() == 4, "write failure preserves current library");

    std::cout << "PASS: " << checks << " backend checks\n";
}
