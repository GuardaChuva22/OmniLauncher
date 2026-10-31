import java.io.*;
import java.nio.file.*;
import java.util.zip.*;

// Keep ASM private to the agent, independent of any mod loader's ASM version.
public final class Relocate {
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[1]);
        try (ZipInputStream zip = new ZipInputStream(Files.newInputStream(Path.of(args[0])))) {
            for (ZipEntry e; (e = zip.getNextEntry()) != null;) {
                String name = e.getName();
                if (!name.startsWith("org/objectweb/asm/") || !name.endsWith(".class")) continue;
                byte[] bytes = zip.readAllBytes();
                ByteArrayOutputStream buffer = new ByteArrayOutputStream();
                try (DataInputStream in = new DataInputStream(new ByteArrayInputStream(bytes));
                     DataOutputStream out = new DataOutputStream(buffer)) {
                    out.writeInt(in.readInt()); out.writeShort(in.readUnsignedShort());
                    out.writeShort(in.readUnsignedShort());
                    int count = in.readUnsignedShort(); out.writeShort(count);
                    for (int i = 1; i < count; i++) {
                        int tag = in.readUnsignedByte(); out.writeByte(tag);
                        if (tag == 1) out.writeUTF(in.readUTF().replace("org/objectweb/asm", "omni/agent/internal/asm")
                                .replace("org.objectweb.asm", "omni.agent.internal.asm"));
                        else {
                            int length = switch (tag) {
                                case 3, 4, 9, 10, 11, 12, 17, 18 -> 4;
                                case 5, 6 -> 8;
                                case 7, 8, 16, 19, 20 -> 2;
                                case 15 -> 3;
                                default -> throw new IOException("Unknown constant: " + tag);
                            };
                            out.write(in.readNBytes(length));
                            if (tag == 5 || tag == 6) i++;
                        }
                    }
                    out.write(in.readAllBytes());
                }
                Path target = root.resolve(name.replace("org/objectweb/asm", "omni/agent/internal/asm"));
                Files.createDirectories(target.getParent()); Files.write(target, buffer.toByteArray());
            }
        }
    }
}
