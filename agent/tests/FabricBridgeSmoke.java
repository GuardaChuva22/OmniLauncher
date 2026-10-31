package omni.agent.test;

import java.nio.file.Path;
import java.util.List;
import net.fabricmc.api.EnvType;
import net.fabricmc.loader.impl.game.GameProvider;

// Exercise the actual Knot isolation policy, without starting Minecraft or any UI.
public final class FabricBridgeSmoke {
    public static final class Callbacks {
        static int calls;
        public static void beginScreen(Object s, Object g, float d, boolean m) { calls++; }
        public static void modernBlur(Object g) { calls++; }
        public static void panoramaBlur() { calls++; }
        public static boolean active() { calls++; return true; }
        public static int radius(int value) { calls++; return value + 1; }
        public static boolean key(int key, int action) { calls++; return key == 260 && action == 1; }
        public static void draw(Object s, Object g, int x, int y) { calls++; }
        public static boolean panelKey(int key) { calls++; return key == 32; }
        public static boolean panelClick(Object s, double x, double y, int b) { calls++; return b == 0; }
        public static void close() { calls++; }
        public static float zoomFov(float fov) { calls++; return fov / 4; }
        public static void zoomWorld(boolean world) { calls++; }
        public static double zoomFovLegacy(double fov) { calls++; return fov / 4; }
        public static double zoomSensitivity() { calls++; return 0.25; }
        public static boolean zoomScroll(double amount) { calls++; return amount > 0; }
        public static boolean zoomHides(boolean hidden) { calls++; return true; }
        public static boolean panelRelease(Object screen, double x, double y, int button) { calls++; return true; }
        public static boolean panelDrag(Object screen, double x, double y, int button) { calls++; return true; }
        public static boolean panelScroll(Object screen, double x, double y, double amount) { calls++; return amount != 0; }
        public static void hud(Object graphics) { calls++; }
        public static void chatBegin(Object chat, Object graphics) { calls++; }
        public static void chatEnd(Object graphics) { calls++; }
        public static void chatAdded(Object chat) { calls++; }
        public static void scrollBefore(Object area) { calls++; }
        public static void scrollAfter(Object area) { calls++; }
        public static void scrollFrame(Object area) { calls++; }
    }

    public static void main(String[] args) throws Exception {
        Class<?> type = Class.forName("net.fabricmc.loader.impl.launch.knot.KnotClassLoader");
        var constructor = type.getDeclaredConstructor(boolean.class, EnvType.class, GameProvider.class);
        constructor.setAccessible(true);
        ClassLoader knot = (ClassLoader) constructor.newInstance(false, EnvType.CLIENT, null);
        var getDelegate = type.getDeclaredMethod("getDelegate"); getDelegate.setAccessible(true);
        Object delegate = getDelegate.invoke(knot);
        var restrict = delegate.getClass().getMethod("setValidParentClassPath", java.util.Collection.class);
        restrict.setAccessible(true); restrict.invoke(delegate, List.of());
        try {
            knot.loadClass("omni.agent.OmniClient");
            throw new AssertionError("Fixture must restrict access to the implementation jar");
        } catch (ClassNotFoundException expected) {
            if (!expected.getMessage().contains("hasn't been exposed")) throw expected;
        }
        Class<?> bridge = Class.forName("omni.bridge.Hooks", true, null);
        if (bridge.getClassLoader() != null || knot.loadClass(bridge.getName()) != bridge)
            throw new AssertionError("The game must see the same bootstrap bridge");
        bridge.getMethod("initialize", Class.class).invoke(null, Callbacks.class);
        var addSource = delegate.getClass().getMethod("addCodeSource", Path.class);
        addSource.setAccessible(true); addSource.invoke(delegate, Path.of(args[0]));
        Class<?> probe = Class.forName("omni.fixture.BridgeGameProbe", true, knot);
        probe.getMethod("run").invoke(null);
        if (Callbacks.calls != 26) throw new AssertionError("Missing callbacks: " + Callbacks.calls);
        System.out.println("OK: Fabric rejects the implementation jar; all 26 UI callbacks work through bootstrap");
    }
}
