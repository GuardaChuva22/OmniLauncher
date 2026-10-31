package omni.bridge;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

// Only this dependency-free bridge is visible through the bootstrap loader. Game
// loaders never need to resolve the agent implementation or its private dependencies.
public final class Hooks {
    private static MethodHandle begin, blur, panorama, active, radius, key, draw, panelKey, click, close;
    private static volatile boolean ready;
    private Hooks() {}

    public static void initialize(Class<?> implementation) throws ReflectiveOperationException {
        begin = bind(implementation, "beginScreen", void.class, Object.class, Object.class, float.class, boolean.class);
        blur = bind(implementation, "modernBlur", void.class, Object.class);
        panorama = bind(implementation, "panoramaBlur", void.class);
        active = bind(implementation, "active", boolean.class);
        radius = bind(implementation, "radius", int.class, int.class);
        key = bind(implementation, "key", boolean.class, int.class, int.class);
        draw = bind(implementation, "draw", void.class, Object.class, Object.class, int.class, int.class);
        panelKey = bind(implementation, "panelKey", boolean.class, int.class);
        click = bind(implementation, "panelClick", boolean.class, Object.class, double.class, double.class, int.class);
        close = bind(implementation, "close", void.class);
        ready = true;
    }
    private static MethodHandle bind(Class<?> owner, String name, Class<?> result, Class<?>... args)
            throws ReflectiveOperationException {
        return MethodHandles.publicLookup().findStatic(owner, name, MethodType.methodType(result, args));
    }
    private static void failed(Throwable error) {
        if (ready) System.err.println("[Omni UI] Bridge disabled: " + error);
        ready = false;
    }
    public static void beginScreen(Object screen, Object graphics, float delta, boolean modern) {
        try { if (ready) begin.invokeExact(screen, graphics, delta, modern); }
        catch (Throwable e) { failed(e); }
    }
    public static void modernBlur(Object graphics) {
        try { if (ready) blur.invokeExact(graphics); } catch (Throwable e) { failed(e); }
    }
    public static void panoramaBlur() {
        try { if (ready) panorama.invokeExact(); } catch (Throwable e) { failed(e); }
    }
    public static boolean active() {
        try { return ready && (boolean) active.invokeExact(); } catch (Throwable e) { failed(e); return false; }
    }
    public static int radius(int vanilla) {
        try { return ready ? (int) radius.invokeExact(vanilla) : vanilla; }
        catch (Throwable e) { failed(e); return vanilla; }
    }
    public static boolean key(int code, int action) {
        try { return ready && (boolean) key.invokeExact(code, action); }
        catch (Throwable e) { failed(e); return false; }
    }
    public static void draw(Object screen, Object graphics, int x, int y) {
        try { if (ready) draw.invokeExact(screen, graphics, x, y); } catch (Throwable e) { failed(e); }
    }
    public static boolean panelKey(int code) {
        try { return ready && (boolean) panelKey.invokeExact(code); }
        catch (Throwable e) { failed(e); return false; }
    }
    public static boolean panelClick(Object screen, double x, double y, int button) {
        try { return ready && (boolean) click.invokeExact(screen, x, y, button); }
        catch (Throwable e) { failed(e); return false; }
    }
    public static void close() {
        try { if (ready) close.invokeExact(); } catch (Throwable e) { failed(e); }
    }
}
