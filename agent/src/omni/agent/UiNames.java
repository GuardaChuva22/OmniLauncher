package omni.agent;

import java.io.IOException;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

// UI mappings are separate from the live map's small, zero-argument name reader.
final class UiNames {
    record Member(String name, String descriptor) {}
    private final Map<String, String> classes = new HashMap<>();
    private final Map<String, String> fields = new HashMap<>();
    private final Map<String, String> methods = new HashMap<>();
    private final boolean identity;
    private ClassLoader loader;

    UiNames(String mappings) throws IOException {
        identity = mappings.isEmpty();
        if (identity) return;
        List<String> lines = Files.readAllLines(Path.of(mappings));
        for (String line : lines) {
            if (!line.isEmpty() && line.charAt(0) != ' ' && !line.startsWith("#") && line.contains(" -> ")) {
                int arrow = line.indexOf(" -> ");
                classes.put(line.substring(0, arrow), line.substring(arrow + 4, line.length() - 1));
            }
        }
        Pattern method = Pattern.compile("(?:\\d+:\\d+:)?(\\S+) (\\S+)\\(([^)]*)\\)(?::\\d+(?::\\d+)?)? -> (\\S+)");
        String owner = "";
        for (String line : lines) {
            if (line.isEmpty() || line.startsWith("#")) continue;
            if (line.charAt(0) != ' ') { owner = line.substring(0, line.indexOf(" -> ")); continue; }
            if (!(owner.startsWith("net.minecraft.client.") || owner.equals("net.minecraft.network.chat.Component")
                    || owner.equals("net.minecraft.resources.ResourceLocation") || owner.equals("net.minecraft.world.entity.Entity")
                    || owner.startsWith("com.mojang.blaze3d."))) continue;
            Matcher match = method.matcher(line.trim());
            if (match.matches()) {
                methods.put(owner + "." + match.group(2) + "(" + match.group(3) + ")", match.group(4));
            } else {
                String[] entry = line.trim().split(" -> ");
                if (entry.length == 2 && !entry[0].contains("("))
                    fields.put(owner + "." + entry[0].substring(entry[0].lastIndexOf(' ') + 1), entry[1]);
            }
        }
    }

    void loader(ClassLoader value) { loader = value; }
    String className(String name) { return identity ? name : classes.getOrDefault(name, name); }
    String internal(String name) { return className(name).replace('.', '/'); }
    String methodName(String owner, String name, String... parameters) {
        return identity ? name : methods.getOrDefault(owner + "." + name + "(" + String.join(",", parameters) + ")", name);
    }
    String fieldName(String owner, String name) { return identity ? name : fields.getOrDefault(owner + "." + name, name); }
    Class<?> type(String name) throws ClassNotFoundException { return Class.forName(className(name), false, loader); }
    Method method(String owner, String name, String... parameters) throws ReflectiveOperationException {
        Class<?>[] types = new Class<?>[parameters.length];
        for (int i = 0; i < types.length; i++) types[i] = switch (parameters[i]) {
            case "int" -> int.class; case "long" -> long.class; case "float" -> float.class;
            case "double" -> double.class; case "boolean" -> boolean.class; default -> type(parameters[i]);
        };
        Class<?> cls = type(owner);
        String mapped = methodName(owner, name, parameters);
        Method result;
        try { result = cls.getMethod(mapped, types); }
        catch (NoSuchMethodException e) { result = cls.getDeclaredMethod(mapped, types); }
        result.setAccessible(true); return result;
    }
    Field field(String owner, String name) throws ReflectiveOperationException {
        Field result = type(owner).getDeclaredField(fieldName(owner, name));
        result.setAccessible(true); return result;
    }
}
