package omni.agent;

import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.Socket;

// The connection to the launcher on 127.0.0.1. Frames: u32 length, u8 type, payload
// (big endian, strings as Java's writeUTF). The first frame is Hello with the token
// the launcher gave on the command line; the launcher drops anything else.
final class Link {
    static final int PROTOCOL = 2;
    static final int HELLO = 1, STATUS = 2, STATE = 3, BLOCKS = 4, MODEL = 5;

    private final int port;
    private final String token;
    private final String names;
    private Socket socket;
    private OutputStream out;
    private long nextAttempt;
    private String lastStatus = "";
    // Counts the connections made, so what was sent on an earlier one is sent again.
    private volatile int connections;

    Link(int port, String token, String names) {
        this.port = port;
        this.token = token;
        this.names = names;
    }

    // Connects when needed (at most every 2 s); false while the launcher can't be reached.
    synchronized boolean ready() {
        if (out != null)
            return true;
        final long now = System.currentTimeMillis();
        if (now < nextAttempt)
            return false;
        nextAttempt = now + 2000;
        try {
            socket = new Socket(InetAddress.getLoopbackAddress(), port);
            socket.setTcpNoDelay(true);
            ++connections;
            out = new BufferedOutputStream(socket.getOutputStream());
            final Frame hello = new Frame(HELLO);
            hello.data.writeShort(PROTOCOL);
            hello.data.writeUTF(token);
            hello.data.writeUTF(names);
            hello.data.writeUTF(System.getProperty("java.version", ""));
            write(hello);
            if (!lastStatus.isEmpty()) {
                final Frame status = new Frame(STATUS);
                status.data.writeUTF(lastStatus);
                write(status);
            }
            return out != null;
        } catch (IOException error) {
            close();
            return false;
        }
    }

    int connections() {
        return connections;
    }

    void status(String text) {
        final boolean connected;
        synchronized (this) {
            lastStatus = text.length() > 500 ? text.substring(0, 500) : text;
            connected = out != null;
        }
        // A new connection sends the last status itself.
        if (!ready() || !connected)
            return;
        try {
            final Frame frame = new Frame(STATUS);
            frame.data.writeUTF(lastStatus);
            send(frame);
        } catch (IOException ignored) {
        }
    }

    void send(Frame frame) {
        if (ready())
            write(frame);
    }

    private synchronized void write(Frame frame) {
        if (out == null)
            return;
        try {
            final byte[] payload = frame.bytes.toByteArray();
            final DataOutputStream header = new DataOutputStream(out);
            header.writeInt(payload.length + 1);
            header.writeByte(frame.type);
            out.write(payload);
            out.flush();
        } catch (IOException error) {
            close();
        }
    }

    private synchronized void close() {
        try {
            if (socket != null)
                socket.close();
        } catch (IOException ignored) {
        }
        socket = null;
        out = null;
    }

    static final class Frame {
        final int type;
        final ByteArrayOutputStream bytes = new ByteArrayOutputStream(256);
        final DataOutputStream data = new DataOutputStream(bytes);

        Frame(int type) {
            this.type = type;
        }
    }
}
