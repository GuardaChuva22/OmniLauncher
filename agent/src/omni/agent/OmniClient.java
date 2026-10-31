package omni.agent;

import java.io.*;
import java.lang.instrument.Instrumentation;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.Properties;

// Entry points called by injected game code. None may throw into Minecraft.
public final class OmniClient {
    private static UiNames names;
    private static Bindings bindings;
    private static boolean failed, blurred;
    private static float frameDelta;
    private static boolean blurEnabled = true;
    private static Object panel, previous;
    private static boolean focused;
    private static Path config;
    private static double switchAmount = 1;
    private static long lastDraw;

    private OmniClient() {}

    static void install(String mappings, Instrumentation instrumentation) {
        try {
            names = new UiNames(mappings);
            instrumentation.addTransformer(new UiHooks(names));
        } catch (Throwable error) { disable(error); }
    }

    public static boolean active() { return bindings != null && !failed; }
    public static int radius(int vanilla) { return active() ? (blurEnabled ? 7 : 0) : vanilla; }

    public static void beginScreen(Object screen, Object graphics, float delta, boolean modern) {
        try {
            if (failed) return;
            if (bindings == null) {
                names.loader(screen.getClass().getClassLoader());
                Bindings resolved = new Bindings(names, modern);
                config = ((File) names.field(Bindings.MC, "gameDirectory").get(resolved.minecraft)).toPath()
                        .resolve("config/omni-client.properties");
                if (Files.isRegularFile(config)) {
                    Properties values = new Properties();
                    try (Reader reader = Files.newBufferedReader(config)) { values.load(reader); }
                    blurEnabled = Boolean.parseBoolean(values.getProperty("blur", "true"));
                }
                switchAmount = blurEnabled ? 1 : 0;
                bindings = resolved;
                System.err.println("[Omni UI] Ready. INS opens the panel; blur=" + blurEnabled);
            }
            blurred = false;
            frameDelta = delta;
            if (!modern && bindings.level.get(bindings.minecraft) != null) legacyBlur();
        } catch (Throwable error) { disable(error); }
    }

    public static void modernBlur(Object graphics) {
        try {
            if (!active() || !blurEnabled || blurred) return;
            bindings.blur.invoke(graphics);
            blurred = true;
        } catch (Throwable error) { disable(error); }
    }

    public static void panoramaBlur() {
        try { if (active() && !bindings.modern) legacyBlur(); }
        catch (Throwable error) { disable(error); }
    }

    private static void legacyBlur() throws ReflectiveOperationException {
        if (!blurEnabled || blurred) return;
        bindings.blur.invoke(bindings.renderer.get(bindings.minecraft), frameDelta);
        Object target = bindings.renderTarget.invoke(bindings.minecraft);
        bindings.bindWrite.invoke(target, false);
        blurred = true;
    }

    public static boolean key(int key, int action) {
        if (!active() || key != bindings.insert) return false;
        try {
            if (action == 1) {
                if (bindings.currentScreen() == panel && panel != null) close();
                else {
                    previous = bindings.currentScreen();
                    // Replacing a container screen closes its server-side container.
                    // Never restore that stale instance when the panel closes.
                    if (names.type("net.minecraft.client.gui.screens.inventory.AbstractContainerScreen").isInstance(previous))
                        previous = null;
                    if (panel == null) panel = PanelClass.create(names, bindings);
                    focused = false;
                    bindings.setScreen(panel);
                }
            }
            return true;
        } catch (Throwable error) { disable(error); return false; }
    }

    public static void close() {
        try {
            if (active()) { Object restore = previous; previous = null; bindings.setScreen(restore); }
        } catch (Throwable error) { disable(error); }
    }

    public static boolean panelKey(int key) {
        if (!active()) return false;
        if (key == bindings.escape || key == bindings.insert) close();
        else if (key == bindings.tab) focused = true;
        else if ((key == bindings.space || key == bindings.enter || key == bindings.keypadEnter) && focused) toggle();
        return true;
    }

    public static boolean panelClick(Object screen, double x, double y, int button) {
        try {
            Layout l = layout(screen);
            if (button == 0 && inside(x, y, l.x + l.w - 36, l.y + 12, 24, 24)) { close(); return true; }
            if (button == 0 && inside(x, y, l.x + 20, l.y + 88, l.w - 40, 64)) {
                focused = true; toggle(); return true;
            }
        } catch (Throwable error) { disable(error); }
        return false;
    }

    private static void toggle() {
        blurEnabled = !blurEnabled;
        try {
            Files.createDirectories(config.getParent());
            Path temp = Files.createTempFile(config.getParent(), ".omni-client-", ".tmp");
            try {
                Files.writeString(temp, "# Omni in-game client\nblur=" + blurEnabled + "\n");
                try { Files.move(temp, config, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE); }
                catch (AtomicMoveNotSupportedException e) { Files.move(temp, config, StandardCopyOption.REPLACE_EXISTING); }
            } finally { Files.deleteIfExists(temp); }
        } catch (IOException error) { System.err.println("[Omni UI] Could not save blur preference: " + error); }
    }

    private record Layout(int x, int y, int w, int h) {}
    private static Layout layout(Object screen) throws IllegalAccessException {
        int width = bindings.width.getInt(screen), height = bindings.height.getInt(screen);
        int w = Math.min(408, width - 16), h = Math.min(224, height - 16);
        return new Layout((width - w) / 2, (height - h) / 2, w, h);
    }
    private static boolean inside(double x, double y, int left, int top, int width, int height) {
        return x >= left && y >= top && x < left + width && y < top + height;
    }

    public static void draw(Object screen, Object graphics, int mouseX, int mouseY) {
        try {
            if (!active()) return;
            Layout l = layout(screen);
            int x = l.x, y = l.y, w = l.w, h = l.h;
            int sw = bindings.width.getInt(screen), sh = bindings.height.getInt(screen);
            fill(graphics, 0, 0, sw, sh, 0x55080608);
            rounded(graphics, x - 2, y + 3, w + 4, h, 13, 0x35000000);
            rounded(graphics, x, y, w, h, 12, 0xff494049);
            rounded(graphics, x + 1, y + 1, w - 2, h - 2, 11, 0xf5151216);
            // Modular Omni mark, using the launcher's rose/graphite/porcelain palette.
            rounded(graphics, x + 20, y + 20, 10, 10, 3, 0xffe68aa6);
            rounded(graphics, x + 32, y + 20, 10, 10, 3, 0xffe68aa6);
            rounded(graphics, x + 20, y + 32, 10, 10, 3, 0xffe68aa6);
            rounded(graphics, x + 32, y + 32, 10, 10, 3, 0xfff3b7cb);
            text(graphics, "omni", x + 52, y + 22, 0xfff5f1ed);
            text(graphics, "CLIENT", x + 52, y + 36, 0xffb0a7af);
            boolean overClose = inside(mouseX, mouseY, x + w - 36, y + 12, 24, 24);
            rounded(graphics, x + w - 36, y + 12, 24, 24, 6, overClose ? 0xff393139 : 0xff211d23);
            text(graphics, "x", x + w - 27, y + 20, overClose ? 0xfff5f1ed : 0xffb0a7af);
            fill(graphics, x + 20, y + 64, x + w - 20, y + 65, 0xff352d35);
            text(graphics, "APARENCIA", x + 20, y + 74, 0xffb0a7af);
            boolean hover = inside(mouseX, mouseY, x + 20, y + 88, w - 40, 64);
            rounded(graphics, x + 19, y + 87, w - 38, 66, 9, focused ? 0xfff3b7cb : 0xff393139);
            rounded(graphics, x + 20, y + 88, w - 40, 64, 8, hover ? 0xff2b252d : 0xff211d23);
            text(graphics, "Desfoque das interfaces", x + 32, y + 101, 0xfff5f1ed);
            text(graphics, "Fundo suave em todas as telas.", x + 32, y + 117, 0xffb0a7af);
            text(graphics, blurEnabled ? "Ativado" : "Desativado", x + 32, y + 132, blurEnabled ? 0xfff3b7cb : 0xffb0a7af);
            long now = System.nanoTime();
            double dt = lastDraw == 0 ? 1 : Math.min(1, (now - lastDraw) / 1e9);
            lastDraw = now;
            switchAmount += ((blurEnabled ? 1 : 0) - switchAmount) * Math.min(1, dt * 16);
            int sx = x + w - 74, sy = y + 110;
            rounded(graphics, sx, sy, 40, 22, 11, blurEnabled ? 0xffe68aa6 : 0xff645962);
            rounded(graphics, sx + 3 + (int) Math.round(18 * switchAmount), sy + 3, 16, 16, 8,
                    blurEnabled ? 0xff151216 : 0xfff5f1ed);
            text(graphics, "INS / ESC", x + 20, y + h - 25, 0xfff3b7cb);
            String hint = "fechar";
            text(graphics, hint, x + 82, y + h - 25, 0xffb0a7af);
            String keyboard = "TAB  selecionar";
            text(graphics, keyboard, x + w - 20 - bindings.textWidth(keyboard), y + h - 25, 0xffb0a7af);
        } catch (Throwable error) { disable(error); }
    }

    private static void fill(Object g, int x, int y, int right, int bottom, int color) throws ReflectiveOperationException {
        bindings.fill.invoke(g, x, y, right, bottom, color);
    }
    private static void rounded(Object g, int x, int y, int w, int h, int radius, int color) throws ReflectiveOperationException {
        radius = Math.max(0, Math.min(radius, Math.min(w, h) / 2));
        fill(g, x, y + radius, x + w, y + h - radius, color);
        for (int row = 0; row < radius; row++) {
            double dy = radius - row - 0.5;
            int inset = (int) Math.ceil(radius - Math.sqrt(radius * radius - dy * dy));
            fill(g, x + inset, y + row, x + w - inset, y + row + 1, color);
            fill(g, x + inset, y + h - row - 1, x + w - inset, y + h - row, color);
        }
    }
    private static void text(Object g, String value, int x, int y, int color) throws ReflectiveOperationException {
        bindings.text.invoke(g, bindings.font.get(bindings.minecraft), value, x, y, color, false);
    }

    private static void disable(Throwable error) {
        if (!failed) {
            System.err.println("[Omni UI] Disabled: " + error);
            error.printStackTrace(System.err);
            try {
                if (bindings != null && panel != null && bindings.currentScreen() == panel)
                    bindings.setScreen(previous);
            } catch (Throwable ignored) { }
        }
        failed = true;
    }

    static final class Bindings {
        static final String MC = "net.minecraft.client.Minecraft", S = UiHooks.SCREEN;
        static final String FONT = "net.minecraft.client.gui.Font", GUI = "net.minecraft.client.gui.Gui";
        final boolean modern;
        final Object minecraft;
        final Field font, width, height, level, renderer, screenField, guiField;
        final Method fill, text, fontWidth, blur, renderTarget, bindWrite, screenGet, screenSet;
        final Class<?> screenType;
        final int insert, escape, tab, space, enter, keypadEnter;
        Bindings(UiNames n, boolean modern) throws ReflectiveOperationException {
            this.modern = modern;
            minecraft = n.method(MC, "getInstance").invoke(null);
            screenType = n.type(S);
            font = n.field(MC, "font"); level = n.field(MC, "level"); renderer = n.field(MC, "gameRenderer");
            width = n.field(S, "width"); height = n.field(S, "height");
            String graphics = modern ? "net.minecraft.client.gui.GuiGraphicsExtractor" : "net.minecraft.client.gui.GuiGraphics";
            fill = n.method(graphics, "fill", "int", "int", "int", "int", "int");
            text = n.method(graphics, modern ? "text" : "drawString", FONT, "java.lang.String", "int", "int", "int", "boolean");
            fontWidth = n.method(FONT, "width", "java.lang.String");
            String input = "com.mojang.blaze3d.platform.InputConstants";
            insert = n.field(input, "KEY_INSERT").getInt(null);
            escape = n.field(input, "KEY_ESCAPE").getInt(null);
            tab = n.field(input, "KEY_TAB").getInt(null);
            space = n.field(input, "KEY_SPACE").getInt(null);
            enter = n.field(input, "KEY_RETURN").getInt(null);
            keypadEnter = n.field(input, "KEY_NUMPADENTER").getInt(null);
            if (modern) {
                guiField = n.field(MC, "gui"); screenField = null;
                screenGet = n.method(GUI, "screen"); screenSet = n.method(GUI, "setScreen", S);
                blur = n.method(graphics, "blurBeforeThisStratum");
                renderTarget = bindWrite = null;
            } else {
                guiField = null; screenField = n.field(MC, "screen"); screenGet = null;
                screenSet = n.method(MC, "setScreen", S);
                blur = n.method("net.minecraft.client.renderer.GameRenderer", "processBlurEffect", "float");
                renderTarget = n.method(MC, "getMainRenderTarget");
                bindWrite = n.method("com.mojang.blaze3d.pipeline.RenderTarget", "bindWrite", "boolean");
            }
        }
        Object currentScreen() throws ReflectiveOperationException {
            return modern ? screenGet.invoke(guiField.get(minecraft)) : screenField.get(minecraft);
        }
        void setScreen(Object value) throws ReflectiveOperationException {
            screenSet.invoke(modern ? guiField.get(minecraft) : minecraft, value);
        }
        int textWidth(String value) throws ReflectiveOperationException { return (Integer) fontWidth.invoke(font.get(minecraft), value); }
    }
}
