package omni.agent;

import java.io.BufferedReader;
import java.io.IOException;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;

// Mojang names to the names the running game uses. Without a mappings file (26.1+, which
// is not obfuscated, or a loader that runs on Mojang names) they are the same; otherwise
// the official ProGuard file is read, keeping only the classes asked for.
final class Names {
    private final Map<String, String> classes = new HashMap<>();
    // "class.member" and "class.member()" → runtime name
    private final Map<String, String> members = new HashMap<>();
    private final boolean identity;
    private ClassLoader loader;

    Names(String mappingsFile, Set<String> wanted) throws IOException {
        identity = mappingsFile == null || mappingsFile.isEmpty();
        if (identity)
            return;
        try (BufferedReader reader = Files.newBufferedReader(Paths.get(mappingsFile), StandardCharsets.UTF_8)) {
            String current = null;
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.isEmpty() || line.charAt(0) == '#')
                    continue;
                if (line.charAt(0) != ' ') {
                    // "net.minecraft.client.Minecraft -> fgo:"
                    final int arrow = line.indexOf(" -> ");
                    final String name = arrow < 0 ? "" : line.substring(0, arrow);
                    current = wanted.contains(name) ? name : null;
                    if (current != null)
                        classes.put(name, line.substring(arrow + 4, line.length() - 1));
                    continue;
                }
                if (current == null)
                    continue;
                // "    1:5:net.minecraft.client.Minecraft getInstance() -> Q"
                // "    net.minecraft.client.player.LocalPlayer player -> t"
                final String entry = line.trim();
                final int arrow = entry.indexOf(" -> ");
                if (arrow < 0)
                    continue;
                final String left = entry.substring(0, arrow);
                final String runtime = entry.substring(arrow + 4);
                final int space = left.lastIndexOf(' ');
                // "getChunk(int,int)": overloads stay apart by their parameters.
                final String member = left.substring(space + 1);
                members.putIfAbsent(current + '.' + member, runtime);
            }
        }
    }

    void setLoader(ClassLoader loader) {
        this.loader = loader;
    }

    String className(String mojang) {
        return identity ? mojang : classes.get(mojang);
    }

    Class<?> type(String mojang) throws ReflectiveOperationException {
        final String name = className(mojang);
        if (name == null)
            throw new ClassNotFoundException(mojang);
        return Class.forName(name, false, loader);
    }

    // A method declared in `owner`; game classes among its parameters must be among the
    // classes asked for, as the mappings name them by their Mojang names.
    Method method(String owner, String name, Class<?>... parameters) throws ReflectiveOperationException {
        final StringBuilder signature = new StringBuilder(name).append('(');
        for (int i = 0; i < parameters.length; ++i)
            signature.append(i == 0 ? "" : ",").append(mojangName(parameters[i]));
        final String runtime = identity ? name : members.get(owner + '.' + signature + ')');
        if (runtime == null)
            throw new NoSuchMethodException(owner + '.' + name);
        final Method method = type(owner).getDeclaredMethod(runtime, parameters);
        method.setAccessible(true);
        return method;
    }

    private String mojangName(Class<?> type) {
        final String runtime = type.getName();
        if (identity || type.isPrimitive() || runtime.startsWith("java."))
            return runtime;
        for (Map.Entry<String, String> entry : classes.entrySet())
            if (entry.getValue().equals(runtime))
                return entry.getKey();
        return runtime;
    }

    // A field declared in `owner`, or null if it has none by that name.
    Field fieldOrNull(String owner, String name) {
        try {
            return field(owner, name);
        } catch (ReflectiveOperationException | LinkageError missing) {
            return null;
        }
    }

    Field field(String owner, String name) throws ReflectiveOperationException {
        final String runtime = identity ? name : members.get(owner + '.' + name);
        if (runtime == null)
            throw new NoSuchFieldException(owner + '.' + name);
        final Field field = type(owner).getDeclaredField(runtime);
        field.setAccessible(true);
        return field;
    }
}
