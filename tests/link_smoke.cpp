// The launcher's end of the Omni agent link: a connection without the launch's token is
// dropped, and State, Blocks and Model frames as the agent writes them (Java's DataOutputStream)
// are read.
#include "OmniLink.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QtEndian>

#include <cstdio>
#include <cstring>
#include <functional>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// DataOutputStream's encoding.
class Frame
{
public:
    explicit Frame(quint8 type) : m_type(type) {}
    template<typename T> Frame &number(T v)
    {
        char bytes[sizeof(T)];
        qToBigEndian(v, bytes);
        m_data.append(bytes, sizeof(T));
        return *this;
    }
    Frame &real32(float f) { quint32 bits; std::memcpy(&bits, &f, 4); return number(bits); }
    Frame &real64(double d) { quint64 bits; std::memcpy(&bits, &d, 8); return number(bits); }
    Frame &text(const QString &s) { const auto utf = s.toUtf8(); number(quint16(utf.size())); m_data += utf; return *this; }
    QByteArray bytes() const
    {
        char length[4];
        qToBigEndian(quint32(m_data.size() + 1), length);
        return QByteArray(length, 4) + char(m_type) + m_data;
    }

private:
    quint8 m_type;
    QByteArray m_data;
};

bool waitFor(const std::function<bool()> &done, int ms = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    OmniLink link;
    const auto argument = link.prepare(QStringLiteral("C:/maps/1.21.1.txt"));
    const auto match = QRegularExpression(QStringLiteral(R"(^port=(\d+),token=([0-9a-f]{32}),mappings=(.+)$)")).match(argument);
    check(match.hasMatch(), "agent argument");
    const auto port = quint16(match.captured(1).toUInt());
    const auto token = match.captured(2);

    // Wrong token: dropped, never connected.
    QTcpSocket stranger;
    stranger.connectToHost(QHostAddress::LocalHost, port);
    check(stranger.waitForConnected(3000), "stranger connects");
    stranger.write(Frame(1).number(quint16(OmniLink::Protocol)).text("0123").text("mojang").text("21").bytes());
    check(waitFor([&] { return stranger.state() == QAbstractSocket::UnconnectedState; }), "wrong token dropped");
    check(!link.connected(), "not connected after a wrong token");

    // The game, in two writes split mid-frame.
    QTcpSocket game;
    game.connectToHost(QHostAddress::LocalHost, port);
    check(game.waitForConnected(3000), "game connects");
    const auto hello = Frame(1).number(quint16(OmniLink::Protocol)).text(token).text("obfuscated").text("21.0.4").bytes();
    const auto status = Frame(2).text("ready").bytes();
    auto state = Frame(3).text("Survival").text("minecraft:overworld").number(quint8(1))
                     .real64(12.5).real64(70).real64(-300.25).real32(90.f).number(quint32(2));
    state.real32(14.f).real32(64.f).real32(-290.f).real32(45.f).number(quint8(1)).text("minecraft:zombie");
    state.real32(20.f).real32(65.f).real32(-310.f).real32(-90.f).number(quint8(0)).text("minecraft:cow");
    const auto all = hello + status + state.bytes();
    game.write(all.left(all.size() - 7));
    game.flush();
    check(waitFor([&] { return link.connected() && link.status() == "ready"; }), "hello and status");
    game.write(all.right(7));
    check(waitFor([&] { return link.entities().size() == 2; }), "state read");
    check(link.world() == "Survival" && link.dimension() == "minecraft:overworld", "world and dimension");
    check(qFuzzyCompare(link.player().value("z").toDouble(), -300.25), "player position");
    const auto zombie = link.entities().value(0).toMap();
    check(zombie.value("id") == "minecraft:zombie" && zombie.value("hostile").toBool()
              && qFuzzyCompare(zombie.value("y").toDouble(), 64.0) && qFuzzyCompare(zombie.value("yaw").toDouble(), 45.0),
          "entity");
    check(!link.entities().value(1).toMap().value("hostile").toBool(), "passive entity");

    // A chunk of live blocks: kept by dimension and chunk, and announced.
    QByteArray announced;
    QObject::connect(&link, &OmniLink::blocksReceived, [&announced](const QString &, const QByteArray &payload) { announced = payload; });
    game.write(Frame(4).text("minecraft:overworld").number(qint32(-3)).number(qint32(7)).number(qint32(-4))
                   .number(quint16(1)).number(quint16(1)).text("Block{minecraft:stone}").bytes());
    check(waitFor([&] { return !announced.isEmpty(); }), "blocks read");
    check(link.blocks("minecraft:overworld").value((qint64(-3) << 32) | quint32(7)) == announced, "blocks kept by chunk");

    // How a kind of mob looks: announced with its id.
    QString modelId;
    QObject::connect(&link, &OmniLink::modelReceived, [&modelId](const QString &id, const QByteArray &) { modelId = id; });
    game.write(Frame(5).text("minecraft:cow").number(quint32(0)).number(quint32(0)).bytes());
    check(waitFor([&] { return modelId == "minecraft:cow"; }), "model read");

    // On a server: the address joined, matched against the port of a hosted server.
    game.write(Frame(3).text("@localhost:25567").text("minecraft:overworld").number(quint8(0))
                   .real64(0).real64(0).real64(0).real32(0).number(quint32(0)).bytes());
    check(waitFor([&] { return link.world() == "@localhost:25567"; }), "server address read");
    check(link.onLocalServer(25567) && !link.onLocalServer(25565), "on a local server");
    check(OmniLink::isLocalServer("127.0.0.1", 25565) && OmniLink::isLocalServer("[::1]:25570", 25570)
              && !OmniLink::isLocalServer("mc.hypixel.net:25565", 25565) && !OmniLink::isLocalServer("8.8.8.8", 25565),
          "local addresses");

    // A new launch: a new token; the game that left is forgotten.
    check(link.prepare({}).indexOf(token) < 0, "new token per launch");
    game.disconnectFromHost();
    check(waitFor([&] { return !link.connected(); }), "disconnect");
    check(link.entities().isEmpty() && link.player().isEmpty() && link.blocks("minecraft:overworld").isEmpty(), "cleared on disconnect");

    std::printf(failures ? "%d failure(s)\n" : "link-smoke: all checks passed\n", failures);
    return failures ? 1 : 0;
}
