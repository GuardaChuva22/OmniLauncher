#include "ChunkMesher.h"
#include "Nbt.h"
#include "WorldMap.h"
#include "Worlds.h"

#include <QGuiApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtEndian>
#include <zlib.h>

#include <iostream>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

// A tiny NBT writer: just enough to build a level.dat.
struct Writer
{
    QByteArray out;
    template<typename T> void put(T v) { char b[sizeof(T)]; qToBigEndian(v, b); out.append(b, sizeof(T)); }
    void str(const char *s) { put<quint16>(quint16(qstrlen(s))); out.append(s); }
    void tag(quint8 type, const char *name) { put<quint8>(type); str(name); }
};

QByteArray levelDat(const char *name, qint64 seed)
{
    Writer w;
    w.tag(10, "");
    w.tag(10, "Data");
    w.tag(8, "LevelName"); w.str(name);
    w.tag(3, "GameType"); w.put<qint32>(1);
    w.tag(1, "hardcore"); w.put<qint8>(0);
    w.tag(4, "LastPlayed"); w.put<qint64>(1761800000000);
    w.tag(3, "DataVersion"); w.put<qint32>(3955);
    w.tag(10, "Version"); w.tag(8, "Name"); w.str("1.21.1"); w.put<quint8>(0);
    w.tag(10, "WorldGenSettings"); w.tag(4, "seed"); w.put<qint64>(seed); w.put<quint8>(0);
    w.tag(9, "Tags"); w.put<quint8>(3); w.put<qint32>(2); w.put<qint32>(7); w.put<qint32>(-7);
    w.tag(12, "Packed"); w.put<qint32>(2); w.put<qint64>(-1); w.put<qint64>(42);
    w.tag(5, "Ratio"); w.put<float>(0.5f);
    // A mixed list (1.21.5+): a string wrapped as {"": â€¦} next to a real compound.
    w.tag(9, "Mixed"); w.put<quint8>(10); w.put<qint32>(2);
    w.tag(8, ""); w.str("minecraft:stone"); w.put<quint8>(0);
    w.tag(8, "Name"); w.str("minecraft:oak_log"); w.put<quint8>(0);
    w.put<quint8>(0); // end Data
    w.put<quint8>(0); // end root
    return w.out;
}

QByteArray compress(const QByteArray &data, int windowBits)
{
    z_stream s{};
    deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY);
    QByteArray out(int(deflateBound(&s, uLong(data.size()))), Qt::Uninitialized);
    s.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    s.avail_in = uInt(data.size());
    s.next_out = reinterpret_cast<Bytef *>(out.data());
    s.avail_out = uInt(out.size());
    deflate(&s, Z_FINISH);
    out.resize(qsizetype(s.total_out));
    deflateEnd(&s);
    return out;
}

// A region whose chunk (0, 0) is stone up to y = 63 under one block of water, with the
// rest of the region not generated.
QByteArray regionFile()
{
    Writer w;
    w.tag(10, "");
    w.tag(8, "Status"); w.str("minecraft:full");
    w.tag(3, "yPos"); w.put<qint32>(-4);
    w.tag(10, "Heightmaps");
    const char *heightmaps[]{"WORLD_SURFACE", "OCEAN_FLOOR"};
    for (const auto *map : heightmaps) {
        const quint64 height = qstrcmp(map, "WORLD_SURFACE") == 0 ? 129 : 128; // y + 64 + 1
        w.tag(12, map); w.put<qint32>(37);
        for (int i = 0; i < 37; ++i) {
            quint64 packedLong = 0;
            for (int k = 0; k < 7; ++k)
                packedLong |= height << (k * 9);
            w.put<qint64>(qint64(packedLong));
        }
    }
    w.put<quint8>(0);
    w.tag(9, "sections"); w.put<quint8>(10); w.put<qint32>(2);
    const std::pair<int, const char *> layers[]{{3, "minecraft:stone"}, {4, "minecraft:water"}};
    for (const auto &[y, block] : layers) {
        // Section 3 (y 48â€“63) all stone; section 4 (y 64â€“79) water at its first layer only:
        // that one palette has two entries, so it carries packed data.
        w.tag(1, "Y"); w.put<qint8>(qint8(y));
        w.tag(10, "block_states");
        w.tag(9, "palette"); w.put<quint8>(10); w.put<qint32>(y == 3 ? 1 : 2);
        w.tag(8, y == 3 ? "id" : "Name"); w.str(block); w.put<quint8>(0); // 26.x: {id, properties}
        if (y == 4) {
            w.tag(8, "Name"); w.str("minecraft:air"); w.put<quint8>(0);
            w.tag(12, "data"); w.put<qint32>(256); // 4 bits, 16 per long: layer 0 is 16 longs
            for (int i = 0; i < 256; ++i)
                w.put<qint64>(i < 16 ? 0 : 0x1111111111111111LL);
        }
        w.put<quint8>(0);
        w.put<quint8>(0);
    }
    w.put<quint8>(0);

    const auto chunk = compress(w.out, 15);
    QByteArray region(8192, char(0));
    const quint32 sectors = quint32((chunk.size() + 5 + 4095) / 4096);
    qToBigEndian<quint32>((2u << 8) | sectors, region.data());
    QByteArray body(qsizetype(sectors) * 4096, char(0));
    qToBigEndian<quint32>(quint32(chunk.size() + 1), body.data());
    body[4] = 2;
    body.replace(5, chunk.size(), chunk);
    return region + body;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const auto raw = levelDat("Ilha", -4172144997902289642LL);

    const QList<std::pair<const char *, QByteArray>> encodings{{"plain", raw}, {"gzip", compress(raw, 31)}, {"zlib", compress(raw, 15)}};
    for (const auto &[label, bytes] : encodings) {
        const auto root = Nbt::parse(Nbt::decompress(bytes));
        check(root.isValid(), label);
        check(Nbt::at(root, {"Data", "LevelName"}).toString() == "Ilha", "string");
        check(Nbt::at(root, {"Data", "WorldGenSettings", "seed"}).toLongLong() == -4172144997902289642LL, "long");
        check(Nbt::at(root, {"Data", "Version", "Name"}).toString() == "1.21.1", "nested");
        check(Nbt::at(root, {"Data", "Tags"}).toList() == QVariantList{7, -7}, "list");
        check(Nbt::at(root, {"Data", "Packed"}).value<QList<qint64>>() == QList<qint64>{-1, 42}, "long array");
        check(qFuzzyCompare(Nbt::at(root, {"Data", "Ratio"}).toFloat(), 0.5f), "float");
        check(!Nbt::at(root, {"Data", "Missing"}).isValid(), "missing key");
        const auto mixed = Nbt::at(root, {"Data", "Mixed"}).toList();
        check(mixed.size() == 2 && mixed[0] == "minecraft:stone" && mixed[1].toMap().value("Name") == "minecraft:oak_log", "mixed list");
    }
    for (qsizetype cut = 1; cut < raw.size(); cut += 7)
        check(!Nbt::parse(raw.left(cut)).isValid(), "truncated data rejected");
    check(Nbt::decompress(compress(raw, 31).left(20)).isEmpty(), "broken gzip rejected");

    // Worlds lists a save from its level.dat.
    QTemporaryDir root;
    const QDir saves(root.filePath("instances/g1/saves"));
    QDir().mkpath(saves.filePath("Ilha"));
    QFile file(saves.filePath("Ilha/level.dat"));
    check(file.open(QIODevice::WriteOnly) && file.write(compress(raw, 31)) > 0, "write level.dat");
    file.close();
    QDir().mkpath(saves.filePath("broken"));
    const auto list = Worlds(root.path()).list({QVariantMap{{"id", "g1"}, {"title", "Jogo"}}});
    check(list.size() == 1, "one readable world");
    if (!list.isEmpty()) {
        const auto world = list.first().toMap();
        check(world.value("name") == "Ilha" && world.value("mode") == "creative" && world.value("version") == "1.21.1"
                  && world.value("seed") == "-4172144997902289642" && world.value("mappable").toBool(),
              "world fields");
    }

    // A server the launcher hosts: its world where server.properties says, drawn with
    // the game it was made from.
    {
        QDir().mkpath(root.filePath("servers/s1/mundo"));
        QFile::copy(saves.filePath("Ilha/level.dat"), root.filePath("servers/s1/mundo/level.dat"));
        QFile properties(root.filePath("servers/s1/server.properties"));
        check(properties.open(QIODevice::WriteOnly) && properties.write("#Minecraft server properties\nlevel-name=mundo\nserver-port=25567\n") > 0,
              "write server.properties");
        properties.close();
        QFile servers(root.filePath("servers.json"));
        check(servers.open(QIODevice::WriteOnly) && servers.write(R"([{"id": "s1", "name": "Servidor Zika", "gameId": "g1"}])") > 0,
              "write servers.json");
        servers.close();
        const auto withServer = Worlds(root.path()).list({QVariantMap{{"id", "g1"}, {"title", "Jogo"}}});
        QVariantMap hosted;
        for (const auto &entry : withServer)
            if (entry.toMap().value("server").toBool())
                hosted = entry.toMap();
        check(withServer.size() == 2 && hosted.value("name") == "Servidor Zika" && hosted.value("serverPort") == 25567
                  && hosted.value("gameId") == "g1" && hosted.value("instance").toString().endsWith("instances/g1")
                  && hosted.value("path").toString().endsWith("servers/s1/mundo"),
              "hosted server world");
        QFile::remove(root.filePath("servers.json"));
    }

    // A chunk meshed for the 3D map: with no jar every block is model-less, but water is
    // drawn by the map itself, its top 2/16 below the block above.
    QFile mca(root.filePath("r.0.0.mca"));
    check(mca.open(QIODevice::WriteOnly) && mca.write(regionFile()) > 0, "write region");
    mca.close();
    {
        World3D::ChunkSource source(std::make_shared<World3D::BlockModels>(root.path(), "none", root.path()), root.path());
        const auto blocks = source.chunk(0, 0);
        check(blocks && source.models().block(blocks->at(5, 64, 5)).water && !source.models().block(blocks->at(5, 65, 5)).water,
              "chunk decoded to block ids");
        check(!source.chunk(3, 3), "no chunk where the region has none");
        const auto mesh = source.mesh(0, 0, 1000, 0, 0);
        bool surface = false;
        if (mesh)
            for (const auto &vertex : mesh->translucent)
                surface = surface || (qAbs(vertex.y - 64.875f) < 0.01f && vertex.x >= 0 && vertex.x <= 16);
        check(mesh && surface, "water surface meshed");
        const auto cut = source.mesh(0, 0, 60, 0, 0);
        check(!cut || cut->translucent.empty(), "a cut below the water leaves it out");

        // Live blocks (the agent's Blocks frame, as Java's DataOutputStream writes it): a
        // log where there was stone, and the water gone, over the saved chunk.
        QByteArray payload;
        {
            QDataStream out(&payload, QIODevice::WriteOnly);
            const auto utf = [&out](const QByteArray &text) {
                out << quint16(text.size());
                out.writeRawData(text.constData(), int(text.size()));
            };
            utf("minecraft:overworld");
            out << qint32(0) << qint32(0) << qint32(blocks->minY / 16) << quint16(2);
            out << quint16(2);
            utf("Block{minecraft:air}");
            utf("Block{minecraft:oak_log}[axis=y]");
            for (int i = 0; i < 4096; ++i)
                out << quint8(i == ((1 << 8) | (5 << 4) | 5) ? 1 : 0);
            // Its light: no sky at all, a lamp's 14 beside the log (6, minY + 1, 5).
            out << quint8(1) << quint8(0) << quint8(2);
            for (int i = 0; i < 4096; i += 2)
                out << quint8(i == ((1 << 8) | (5 << 4) | 6) ? 14 : 0);
            out << quint16(0) << quint8(0) << quint8(0); // air, the game had no light for it
        }
        const auto live = World3D::LiveChunk::parse(payload, source.models());
        check(live && live->cx == 0 && live->sections.size() == 2 && live->dimension == "minecraft:overworld", "live chunk read");
        if (live)
            source.setLive(live);
        const auto merged = source.chunk(0, 0);
        const auto log = source.models().id("minecraft:oak_log", {{"axis", "y"}});
        check(merged && merged->at(5, blocks->minY + 1, 5) == log && !source.models().block(merged->at(5, 64, 5)).water
                  && merged->stamp != blocks->stamp && !source.liveVersions().isEmpty(),
              "live blocks over the saved ones");
        check(merged && merged->light(6, blocks->minY + 1, 5) == 14 && merged->light(7, blocks->minY + 1, 5) == 0,
              "live light");
        source.clearLive();
        const auto back = source.chunk(0, 0);
        check(back && source.models().block(back->at(5, 64, 5)).water && back->stamp == blocks->stamp, "back to the saved blocks");
    }
    QDir().mkpath(saves.filePath("Ilha/region"));
    QFile::copy(mca.fileName(), saves.filePath("Ilha/region/r.0.0.mca"));
    const auto described = WorldMap::describe(saves.filePath("Ilha"), "minecraft:overworld");
    check(described.value("regions").toList().size() == 1 && described.value("version") == "1.21.1", "describe");

    // Watching a world: a save (a region written again) is reported once it settles.
    {
        Worlds watcher(root.path());
        int changes = 0;
        QObject::connect(&watcher, &Worlds::worldChanged, [&changes] { ++changes; });
        watcher.watch(saves.filePath("Ilha"), "minecraft:overworld");
        QFile region(saves.filePath("Ilha/region/r.0.0.mca"));
        if (region.open(QIODevice::Append)) {
            region.write(QByteArray(4096, char(0)));
            region.close();
        }
        QElapsedTimer clock;
        clock.start();
        while (changes == 0 && clock.elapsed() < 6000) // the watch alone, no polling
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        check(changes == 1, "a save is reported once");
        watcher.watch("", "");
    }

    if (failures == 0)
        std::cout << "nbt-smoke: ok\n";
    return failures == 0 ? 0 : 1;
}
