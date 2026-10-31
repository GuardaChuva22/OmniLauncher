package omni.agent;

import java.io.InputStream;
import java.util.*;
import omni.agent.internal.asm.*;

// Run against actual vanilla jars/libraries without initializing Minecraft or OpenGL.
public final class UiSmoke extends ClassLoader {
    private final Map<String, byte[]> patched;
    private UiSmoke(ClassLoader parent, Map<String, byte[]> patched) { super(parent); this.patched = patched; }

    // Child-first for every game class, with the patched ones swapped in: the JVM then
    // verifies the injected bytecode against one consistent copy of the game's types.
    @Override protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
        synchronized (getClassLoadingLock(name)) {
            Class<?> type = findLoadedClass(name);
            if (type == null) {
                byte[] bytes = patched == null ? null : patched.get(name);
                boolean game = name.startsWith("net.minecraft.") || name.startsWith("com.mojang.") || name.indexOf('.') < 0;
                if (bytes == null && patched != null && game) {
                    try (InputStream in = getParent().getResourceAsStream(name.replace('.', '/') + ".class")) {
                        if (in != null) bytes = in.readAllBytes();
                    } catch (java.io.IOException e) { throw new ClassNotFoundException(name, e); }
                }
                if (bytes == null) return super.loadClass(name, resolve);
                type = defineClass(name, bytes, 0, bytes.length);
            }
            if (resolve) resolveClass(type);
            return type;
        }
    }

    public static void main(String[] args) throws Exception {
        UiNames names = new UiNames(args.length == 0 ? "" : args[0]);
        ClassLoader game = UiSmoke.class.getClassLoader();
        names.loader(game);
        boolean modern;
        try { names.type("net.minecraft.client.gui.GuiGraphicsExtractor"); modern = true; }
        catch (ClassNotFoundException e) { modern = false; }
        UiHooks hooks = new UiHooks(names);
        Map<String, byte[]> patched = new HashMap<>();
        Map<String, Integer> calls = new TreeMap<>();
        String[] targets = {UiHooks.SCREEN, UiHooks.KEYBOARD, UiHooks.OPTIONS, UiHooks.MOUSE, UiHooks.CAMERA, UiHooks.RENDERER,
                modern ? UiHooks.HUD : UiHooks.GUI, UiHooks.CHAT, UiHooks.LIST, modern ? UiHooks.SCROLL_AREA : UiHooks.LIST};
        for (String target : new LinkedHashSet<>(Arrays.asList(targets))) {
            try (InputStream in = game.getResourceAsStream(names.internal(target) + ".class")) {
                if (in == null) throw new AssertionError("Missing " + target);
                byte[] transformed = hooks.transform(game, names.internal(target), null, null, in.readAllBytes());
                if (transformed == null) throw new AssertionError("Hook failed: " + target);
                patched.put(names.className(target), transformed);
                new ClassReader(transformed).accept(new ClassVisitor(Opcodes.ASM9) {
                    @Override public MethodVisitor visitMethod(int a, String n, String d, String s, String[] ex) {
                        return new MethodVisitor(Opcodes.ASM9) {
                            @Override public void visitMethodInsn(int op, String owner, String name, String desc, boolean itf) {
                                if (owner.equals(UiHooks.CLIENT)) calls.merge(name, 1, Integer::sum);
                            }
                        };
                    }
                }, 0);
            }
        }
        String fov = modern ? "zoomFov" : "zoomFovLegacy";
        for (String entry : new String[]{"beginScreen", "key", "radius", "zoomSensitivity", "zoomScroll", "zoomHides", "hud", "chatBegin", "chatEnd", "chatAdded", "scrollBefore", "scrollAfter", "scrollFrame", fov})
            if (!calls.containsKey(entry)) throw new AssertionError("No " + entry + " hook; found " + calls);

        // Linking runs the verifier over every patched method; one loader per class keeps
        // the game's own types consistent around it.
        for (String type : patched.keySet()) {
            try { Class.forName(type, true, new UiSmoke(game, Map.of(type, patched.get(type)))); }
            catch (VerifyError | ClassFormatError broken) { throw broken; }
            catch (Throwable expected) { /* verified; static init needs the running game */ }
        }

        byte[] screen = PanelClass.bytes(names, modern);
        Class<?> generated = new UiSmoke(game, null).defineClass(null, screen, 0, screen.length);
        generated.getDeclaredMethods();
        generated.getDeclaredConstructors();
        String event = "net.minecraft.client.gui.components.events.GuiEventListener";
        String[] params = modern ? new String[]{"net.minecraft.client.input.MouseButtonEvent", "boolean"}
                : new String[]{"double", "double", "int"};
        String button = "net.minecraft.client.input.MouseButtonEvent";
        String[][] overrides = modern
                ? new String[][]{{"mouseClicked", button, "boolean"}, {"mouseReleased", button}, {"mouseDragged", button, "double", "double"},
                    {"mouseScrolled", "double", "double", "double", "double"}}
                : new String[][]{{"mouseClicked", "double", "double", "int"}, {"mouseReleased", "double", "double", "int"},
                    {"mouseDragged", "double", "double", "int", "double", "double"}, {"mouseScrolled", "double", "double", "double", "double"}};
        for (String[] o : overrides) {
            var mouse = names.method(event, o[0], Arrays.copyOfRange(o, 1, o.length));
            if (generated.getMethod(mouse.getName(), mouse.getParameterTypes()).getDeclaringClass() != generated)
                throw new AssertionError("Mouse override does not match the game: " + o[0]);
        }

        // Every reflective name the client resolves at runtime, and one render of each page and widget.
        OmniClient.Bindings bindings = new OmniClient.Bindings(names, modern);
        new PanelTexture(names, bindings, "client/test");
        new Motion(names, modern);
        Catalog catalog = new Catalog(bindings.insert, bindings.zoomDefault, () -> { });
        ClientWindow window = new ClientWindow(catalog, k -> "#" + k);
        for (ClientWindow.Page page : ClientWindow.Page.values()) {
            window.open(page);
            Art.Image chrome = window.renderChrome(1.5), content = window.renderContent(1.5);
            if (chrome.width() != Art.pixels(ClientWindow.W + 2 * ClientWindow.MARGIN, 1.5) || content.height() < 10)
                throw new AssertionError("Window image size on " + page);
        }
        window.openDetail(catalog.modules.get(1));
        window.renderContent(1.5);
        for (Widgets.Widget w : catalog.widgets.all) Widgets.render(w.face(bindings), 1.5);
        System.out.println("OK: " + calls + ", verified hooks, native panel, bindings and texture ("
                + (modern ? "extraction" : "legacy") + ", left button " + bindings.leftButton + ", zoom key " + bindings.zoomDefault + ")");
    }
}
