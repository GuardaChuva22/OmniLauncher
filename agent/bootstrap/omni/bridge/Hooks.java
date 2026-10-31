package omni.bridge;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

// Only this dependency-free bridge is visible through the bootstrap loader. Game
// loaders never need to resolve the agent implementation or its private dependencies.
public final class Hooks {
    private static MethodHandle begin, blur, panorama, active, radius, key, draw, panelKey, click, close;
    private static MethodHandle zoomFov, zoomWorld, zoomFovLegacy, zoomSensitivity, zoomScroll, zoomHides;
    private static MethodHandle release, drag, scroll, hud;
    private static MethodHandle chatBegin, chatEnd, chatAdded, scrollBefore, scrollAfter, scrollFrame;
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
        zoomFov = bind(implementation, "zoomFov", float.class, float.class);
        zoomWorld = bind(implementation, "zoomWorld", void.class, boolean.class);
        zoomFovLegacy = bind(implementation, "zoomFovLegacy", double.class, double.class);
        zoomSensitivity = bind(implementation, "zoomSensitivity", double.class);
        zoomScroll = bind(implementation, "zoomScroll", boolean.class, double.class);
        zoomHides = bind(implementation, "zoomHides", boolean.class, boolean.class);
        release = bind(implementation, "panelRelease", boolean.class, Object.class, double.class, double.class, int.class);
        drag = bind(implementation, "panelDrag", boolean.class, Object.class, double.class, double.class, int.class);
        scroll = bind(implementation, "panelScroll", boolean.class, Object.class, double.class, double.class, double.class);
        hud = bind(implementation, "hud", void.class, Object.class);
        chatBegin = bind(implementation, "chatBegin", void.class, Object.class, Object.class);
        chatEnd = bind(implementation, "chatEnd", void.class, Object.class);
        chatAdded = bind(implementation, "chatAdded", void.class, Object.class);
        scrollBefore = bind(implementation, "scrollBefore", void.class, Object.class);
        scrollAfter = bind(implementation, "scrollAfter", void.class, Object.class);
        scrollFrame = bind(implementation, "scrollFrame", void.class, Object.class);
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
    public static float zoomFov(float fov) {
        try { return ready ? (float) zoomFov.invokeExact(fov) : fov; } catch (Throwable e) { failed(e); return fov; }
    }
    public static void zoomWorld(boolean world) {
        try { if (ready) zoomWorld.invokeExact(world); } catch (Throwable e) { failed(e); }
    }
    public static double zoomFovLegacy(double fov) {
        try { return ready ? (double) zoomFovLegacy.invokeExact(fov) : fov; }
        catch (Throwable e) { failed(e); return fov; }
    }
    public static double zoomSensitivity() {
        try { return ready ? (double) zoomSensitivity.invokeExact() : 1; } catch (Throwable e) { failed(e); return 1; }
    }
    public static boolean zoomScroll(double amount) {
        try { return ready && (boolean) zoomScroll.invokeExact(amount); } catch (Throwable e) { failed(e); return false; }
    }
    public static boolean zoomHides(boolean hidden) {
        try { return ready ? (boolean) zoomHides.invokeExact(hidden) : hidden; } catch (Throwable e) { failed(e); return hidden; }
    }
    public static boolean panelRelease(Object screen, double x, double y, int button) {
        try { return ready && (boolean) release.invokeExact(screen, x, y, button); }
        catch (Throwable e) { failed(e); return false; }
    }
    public static boolean panelDrag(Object screen, double x, double y, int button) {
        try { return ready && (boolean) drag.invokeExact(screen, x, y, button); }
        catch (Throwable e) { failed(e); return false; }
    }
    public static boolean panelScroll(Object screen, double x, double y, double amount) {
        try { return ready && (boolean) scroll.invokeExact(screen, x, y, amount); }
        catch (Throwable e) { failed(e); return false; }
    }
    public static void hud(Object graphics) {
        try { if (ready) hud.invokeExact(graphics); } catch (Throwable e) { failed(e); }
    }
    public static void chatBegin(Object chat, Object graphics) {
        try { if (ready) chatBegin.invokeExact(chat, graphics); } catch (Throwable e) { failed(e); }
    }
    public static void chatEnd(Object graphics) {
        try { if (ready) chatEnd.invokeExact(graphics); } catch (Throwable e) { failed(e); }
    }
    public static void chatAdded(Object chat) {
        try { if (ready) chatAdded.invokeExact(chat); } catch (Throwable e) { failed(e); }
    }
    public static void scrollBefore(Object area) {
        try { if (ready) scrollBefore.invokeExact(area); } catch (Throwable e) { failed(e); }
    }
    public static void scrollAfter(Object area) {
        try { if (ready) scrollAfter.invokeExact(area); } catch (Throwable e) { failed(e); }
    }
    public static void scrollFrame(Object area) {
        try { if (ready) scrollFrame.invokeExact(area); } catch (Throwable e) { failed(e); }
    }
}
