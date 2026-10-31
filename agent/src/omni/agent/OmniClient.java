package omni.agent;

import java.io.*;
import java.lang.instrument.Instrumentation;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.HashMap;
import java.util.Map;

// Entry points called by injected game code. None may throw into Minecraft.
// The window and the widgets know nothing about the game; this class feeds them input in
// their own units and draws what they paint.
public final class OmniClient {
    private static UiNames names;
    private static Bindings bindings;
    private static boolean failed, blurred, panelFrame, editing;
    private static float frameDelta;
    private static Object panel, previous;
    private static Path config;
    private static Catalog catalog;
    private static ClientWindow window;
    private static HudLayer hud;
    private static Motion motion;

    // Window drawing: two textures (frame and page), the opening animation and dragging.
    private static PanelTexture chromeTexture, pageTexture;
    private static Object chromeDrawn, pageDrawn;
    private static double drawnScale;
    private static boolean flat, draggingWindow;
    private static double grabX, grabY;
    private static long openedAt, lastDraw;
    private static final Map<Integer, String> keyNames = new HashMap<>();

    private OmniClient() {}

    static void install(String mappings, Instrumentation instrumentation) {
        try {
            names = new UiNames(mappings);
            instrumentation.addTransformer(new UiHooks(names));
        } catch (Throwable error) { disable(error); }
    }

    public static boolean active() { return bindings != null && !failed; }
    // The panel always blurs what is behind it (except while editing the HUD over the game);
    // the Desfoque switch covers every other screen.
    private static boolean blurNow() { return panelFrame ? !editing : catalog.blur; }
    public static int radius(int vanilla) { return active() ? (blurNow() ? (int) Math.round(catalog.blurStrength) : 0) : vanilla; }

    // First game object seen: resolve the game's API, then load the preferences.
    private static boolean ready(Object gameObject, boolean modern) {
        if (failed) return false;
        if (bindings != null) return true;
        try {
            names.loader(gameObject.getClass().getClassLoader());
            Bindings resolved = new Bindings(names, modern);
            config = ((File) names.field(Bindings.MC, "gameDirectory").get(resolved.minecraft)).toPath()
                    .resolve("config/omni-client.properties");
            catalog = new Catalog(resolved.insert, resolved.zoomDefault, OmniClient::editHud);
            try { catalog.load(config); }
            catch (IOException error) { System.err.println("[Omni UI] Could not read preferences: " + error); }
            window = new ClientWindow(catalog, OmniClient::keyName);
            hud = new HudLayer(names, resolved, catalog, resolved);
            try { motion = new Motion(names, modern); }
            catch (Throwable error) { System.err.println("[Omni UI] Chat animation and smooth scrolling unavailable: " + error); }
            bindings = resolved;
            System.err.println("[Omni UI] Ready. " + keyName(catalog.panelKey) + " opens the panel; blur=" + catalog.blur + ", zoom=" + Zoom.enabled);
            return true;
        } catch (Throwable error) { disable(error); return false; }
    }

    public static void beginScreen(Object screen, Object graphics, float delta, boolean modern) {
        try {
            if (!ready(screen, modern)) return;
            blurred = false;
            frameDelta = delta;
            panelFrame = panel != null && screen == panel;
            if (!modern && bindings.level.get(bindings.minecraft) != null) legacyBlur();
        } catch (Throwable error) { disable(error); }
    }

    public static void modernBlur(Object graphics) {
        try {
            if (!active() || !blurNow() || blurred) return;
            bindings.blur.invoke(graphics);
            blurred = true;
        } catch (Throwable error) { disable(error); }
    }

    public static void panoramaBlur() {
        try { if (active() && !bindings.modern) legacyBlur(); }
        catch (Throwable error) { disable(error); }
    }

    private static void legacyBlur() throws ReflectiveOperationException {
        if (!blurNow() || blurred) return;
        bindings.blur.invoke(bindings.renderer.get(bindings.minecraft), frameDelta);
        Object target = bindings.renderTarget.invoke(bindings.minecraft);
        bindings.bindWrite.invoke(target, false);
        blurred = true;
    }

    // Every key event, before the game sees it. The panel key toggles the panel; the zoom key zooms.
    public static boolean key(int key, int action) {
        if (!active()) return false;
        try {
            if (key == catalog.panelKey) {
                boolean open = panel != null && bindings.currentScreen() == panel;
                // While a key row waits, the panel key is the answer, not a close.
                if (open && window.listening != null) return false;
                if (action == 1) {
                    if (open) close();
                    else open();
                }
                return true;
            }
            if (key != Zoom.key) return false;
            return Zoom.key(key, action, bindings.currentScreen() == null);
        } catch (Throwable error) { disable(error); return false; }
    }

    private static void open() throws ReflectiveOperationException {
        previous = bindings.currentScreen();
        // Replacing a container screen closes its server-side container.
        // Never restore that stale instance when the panel closes.
        if (names.type("net.minecraft.client.gui.screens.inventory.AbstractContainerScreen").isInstance(previous))
            previous = null;
        if (panel == null) panel = PanelClass.create(names, bindings);
        window.listening = null;
        window.hover = ClientWindow.Target.NONE;
        editing = draggingWindow = false;
        openedAt = System.nanoTime();
        Zoom.disable();
        bindings.setScreen(panel);
    }

    public static void close() {
        try {
            window.listening = null;
            window.release();
            hud.cancel();
            editing = draggingWindow = false;
            if (active()) { Object restore = previous; previous = null; bindings.setScreen(restore); }
        } catch (Throwable error) { disable(error); }
    }

    private static void editHud() {
        editing = true;
        window.listening = null;
        window.hover = ClientWindow.Target.NONE;
    }

    public static boolean panelKey(int key) {
        if (!active()) return false;
        if (window.listening != null) {
            Option option = window.listening;
            window.listening = null;
            // The panel key can't double as another binding, or that row could never be opened again.
            boolean taken = key == catalog.panelKey && !"panelKey".equals(option.id);
            if (key != bindings.escape && !taken) { option.key(key); save(); }
            return true;
        }
        if (key == bindings.escape) {
            if (editing) { editing = false; hud.cancel(); openedAt = System.nanoTime(); }
            else if (!window.back()) close();
        }
        return true;
    }

    public static boolean panelClick(Object screen, double x, double y, int button) {
        try {
            if (!active() || button != bindings.leftButton) return false;
            View v = view(screen);
            if (editing) {
                switch (hud.press(v.physicalWidth(), v.physicalHeight(), v.scale, x * v.gui, y * v.gui)) {
                    case DONE -> { editing = false; openedAt = System.nanoTime(); }
                    case MOVE, NONE -> { }
                }
                return true;
            }
            double wx = windowX(v, x), wy = windowY(v, y);
            switch (window.press(wx, wy)) {
                case CLOSE -> close();
                case EDIT_HUD -> editHud();
                case DRAG -> { draggingWindow = true; grabX = wx; grabY = wy; }
                case CHANGED -> save();
                case NONE -> { }
            }
            return true;
        } catch (Throwable error) { disable(error); return false; }
    }

    public static boolean panelDrag(Object screen, double x, double y, int button) {
        try {
            if (!active() || button != bindings.leftButton) return false;
            View v = view(screen);
            if (editing) hud.drag(v.physicalWidth(), v.physicalHeight(), v.scale, x * v.gui, y * v.gui);
            else if (draggingWindow) {
                // Keep the grabbed point under the mouse.
                catalog.windowX = (x * v.gui - grabX * v.scale - (v.physicalWidth() - ClientWindow.W * v.scale) / 2) / v.scale;
                catalog.windowY = (y * v.gui - grabY * v.scale - (v.physicalHeight() - ClientWindow.H * v.scale) / 2) / v.scale;
                clampWindow(v);
            } else window.slide(windowX(v, x));
            return true;
        } catch (Throwable error) { disable(error); return false; }
    }

    public static boolean panelRelease(Object screen, double x, double y, int button) {
        try {
            if (!active() || button != bindings.leftButton) return false;
            boolean changed = editing ? hud.release() : draggingWindow || window.release();
            draggingWindow = false;
            if (changed) save();
            return true;
        } catch (Throwable error) { disable(error); return false; }
    }

    public static boolean panelScroll(Object screen, double x, double y, double amount) {
        try {
            if (!active()) return false;
            View v = view(screen);
            if (editing) { if (hud.scroll(v.physicalWidth(), v.physicalHeight(), v.scale, x * v.gui, y * v.gui, amount)) save(); }
            else window.scrollBy(amount);
            return true;
        } catch (Throwable error) { disable(error); return false; }
    }

    // After the game's HUD: the widgets. Hidden with F1 and while zoomed, like the HUD.
    public static void hud(Object graphics) {
        try {
            if (!ready(graphics, !graphics.getClass().getName().equals(names.className("net.minecraft.client.gui.GuiGraphics")))) return;
            if (editing && panel != null && bindings.currentScreen() == panel) return;
            if (!hud.any() || bindings.hudHidden() || Zoom.hidesHud()) return;
            double gui = bindings.guiScale(), w = bindings.guiWidth() * gui, h = bindings.guiHeight() * gui;
            hud.draw(graphics, w, h, gui, unit(h));
        } catch (Throwable error) { disable(error); }
    }

    // Chat animation and smooth scrolling; a failure turns off only these.
    public static void chatBegin(Object chat, Object graphics) {
        try { if (active() && motion != null) motion.chatBegin(chat, graphics); } catch (Throwable error) { motionFailed(error); }
    }
    public static void chatEnd(Object graphics) {
        try { if (motion != null) motion.chatEnd(graphics); } catch (Throwable error) { motionFailed(error); }
    }
    public static void chatAdded(Object chat) {
        try { if (active() && motion != null) motion.messageAdded(chat); } catch (Throwable error) { motionFailed(error); }
    }
    public static void scrollBefore(Object area) {
        try { if (active() && motion != null) motion.scrollBefore(area); } catch (Throwable error) { motionFailed(error); }
    }
    public static void scrollAfter(Object area) {
        try { if (active() && motion != null) motion.scrollAfter(area); } catch (Throwable error) { motionFailed(error); }
    }
    public static void scrollFrame(Object area) {
        try { if (active() && motion != null) motion.frame(area); } catch (Throwable error) { motionFailed(error); }
    }
    private static void motionFailed(Throwable error) {
        System.err.println("[Omni UI] Chat animation and smooth scrolling disabled: " + error);
        error.printStackTrace(System.err);
        motion = null;
    }

    public static float zoomFov(float fov) { return active() ? (float) Zoom.fov(fov) : fov; }
    // 1.21.1 asks for the world FOV and the hand FOV through the same method; only the world zooms.
    private static boolean worldFov;
    public static void zoomWorld(boolean world) { worldFov = world; }
    public static double zoomFovLegacy(double fov) { return active() && worldFov ? Zoom.fov(fov) : fov; }
    public static double zoomSensitivity() { return active() ? Zoom.sensitivity() : 1; }
    public static boolean zoomScroll(double amount) {
        try { return active() && Zoom.scroll(amount, bindings.currentScreen() == null); }
        catch (Throwable error) { disable(error); return false; }
    }
    // The HUD's own "hidden" flag (F1), read while drawing: zooming hides the hotbar and hand too.
    public static boolean zoomHides(boolean hidden) { return hidden || active() && Zoom.hidesHud(); }

    private static void save() {
        try { catalog.save(config); }
        catch (IOException error) { System.err.println("[Omni UI] Could not save preferences: " + error); }
    }

    // Window geometry in GUI units, the GUI scale, and the window's px per dp.
    private record View(int width, int height, double gui, double scale) {
        double physicalWidth() { return width * gui; }
        double physicalHeight() { return height * gui; }
    }
    private static View view(Object screen) throws ReflectiveOperationException {
        int width = bindings.width.getInt(screen), height = bindings.height.getInt(screen);
        double gui = bindings.guiScale();
        double w = width * gui, h = height * gui;
        double scale = Math.min(unit(h), Math.min(w * 0.94 / (ClientWindow.W + 2 * ClientWindow.MARGIN),
                h * 0.94 / (ClientWindow.H + 2 * ClientWindow.MARGIN)));
        return new View(width, height, gui, Math.max(0.5, scale));
    }
    private static double unit(double physicalHeight) { return Math.max(1, Math.min(3, physicalHeight / 900)); }

    // The window's top-left in physical pixels; the saved offset is in dp from the centre.
    private static double originX(View v) { return (v.physicalWidth() - ClientWindow.W * v.scale) / 2 + catalog.windowX * v.scale; }
    private static double originY(View v) { return (v.physicalHeight() - ClientWindow.H * v.scale) / 2 + catalog.windowY * v.scale; }
    private static double windowX(View v, double guiX) { return (guiX * v.gui - originX(v)) / v.scale; }
    private static double windowY(View v, double guiY) { return (guiY * v.gui - originY(v)) / v.scale; }
    private static void clampWindow(View v) {
        double mx = Math.max(0, (v.physicalWidth() - ClientWindow.W * v.scale) / 2 / v.scale);
        double my = Math.max(0, (v.physicalHeight() - ClientWindow.H * v.scale) / 2 / v.scale);
        catalog.windowX = Math.max(-mx, Math.min(mx, catalog.windowX));
        catalog.windowY = Math.max(-my, Math.min(my, catalog.windowY));
    }

    public static void draw(Object screen, Object graphics, int mouseX, int mouseY) {
        try {
            if (!active()) return;
            long now = System.nanoTime();
            double dt = lastDraw == 0 ? 0 : Math.min(0.1, (now - lastDraw) / 1e9);
            lastDraw = now;
            View v = view(screen);
            double[] mouse = bindings.mouse(mouseX, mouseY);
            if (editing) {
                hud.drawEditor(graphics, v.physicalWidth(), v.physicalHeight(), v.gui, v.scale, mouse[0] * v.gui, mouse[1] * v.gui);
                return;
            }
            window.tick(dt);
            clampWindow(v);
            if (!draggingWindow && window.sliding == null) window.hover = window.hit(windowX(v, mouse[0]), windowY(v, mouse[1]));
            // Opening: fade in, and grow from 96.5% like the launcher's dialogs.
            double ms = (now - openedAt) / 1e6;
            double fade = 1 - Math.pow(1 - clamp(ms / 180), 3);
            double grow = 0.965 + 0.035 * (1 - Math.pow(1 - clamp(ms / 260), 5));
            fill(graphics, 0, 0, v.width, v.height, ((int) Math.round(0x59 * fade) << 24) | 0x080608);
            if (!flat) {
                try { drawSmooth(graphics, v, fade, grow); return; }
                catch (Throwable error) {
                    flat = true;
                    System.err.println("[Omni UI] Smooth panel unavailable, using the game font: " + error);
                }
            }
            drawFlat(graphics, v);
        } catch (Throwable error) { disable(error); }
    }

    private static void drawSmooth(Object graphics, View v, double fade, double grow) throws ReflectiveOperationException {
        if (chromeTexture == null) {
            chromeTexture = new PanelTexture(names, bindings, "client/window");
            pageTexture = new PanelTexture(names, bindings, "client/page");
        }
        double s = v.scale;
        Object chromeKey = window.chromeKey(), pageKey = window.contentKey();
        if (!chromeKey.equals(chromeDrawn) || s != drawnScale) { chromeTexture.upload(window.renderChrome(s)); chromeDrawn = chromeKey; }
        if (!pageKey.equals(pageDrawn) || s != drawnScale) { pageTexture.upload(window.renderContent(s)); pageDrawn = pageKey; }
        drawnScale = s;

        // Physical pixels; the opening growth scales everything around the window's centre.
        double ox = originX(v), oy = originY(v), cx = ox + ClientWindow.W * s / 2, cy = oy + ClientWindow.H * s / 2;
        double gui = v.gui;
        chromeTexture.draw(graphics, (cx + (ox - ClientWindow.MARGIN * s - cx) * grow) / gui,
                (cy + (oy - ClientWindow.MARGIN * s - cy) * grow) / gui, grow / gui, fade);

        double page = window.pageProgress();
        double vx = ox + ClientWindow.VX * s, vy = oy + ClientWindow.VY * s;
        double px = cx + (vx - cx) * grow, py = cy + (vy + ((1 - page) * 12 - window.scroll()) * s - cy) * grow;
        int x0 = (int) Math.ceil((cx + (vx - cx) * grow) / gui), y0 = (int) Math.ceil((cy + (vy - cy) * grow) / gui);
        int x1 = (int) Math.floor((cx + (vx + ClientWindow.VW * s - cx) * grow) / gui);
        int y1 = (int) Math.floor((cy + (vy + ClientWindow.VH * s - cy) * grow) / gui);
        bindings.scissor(graphics, x0, y0, x1, y1);
        try { pageTexture.draw(graphics, px / gui, py / gui, grow / gui, fade * page); }
        finally { bindings.unscissor(graphics); }

        // Scrollbar in the gutter right of the page.
        double max = window.maxScroll();
        if (max > 0) {
            double track = ClientWindow.VH, thumb = Math.max(36, track * track / window.contentHeight());
            double ty = ClientWindow.VY + window.scroll() / max * (track - thumb);
            double tx = ox + (ClientWindow.VX + ClientWindow.VW + 7) * s;
            int alpha = (int) Math.round(0x40 * fade);
            fill(graphics, (int) Math.round(tx / gui), (int) Math.round((oy + ty * s) / gui),
                    (int) Math.round(tx / gui) + Math.max(1, (int) Math.round(3 * s / gui)), (int) Math.round((oy + (ty + thumb) * s) / gui),
                    alpha << 24 | 0xFFFFFF);
        }
    }

    // The same layout with the game's font, in case the texture path is unavailable.
    private static void drawFlat(Object graphics, View v) throws ReflectiveOperationException {
        double f = v.scale / v.gui, ox = originX(v) / v.gui, oy = originY(v) / v.gui;
        ReflectiveOperationException[] failure = new ReflectiveOperationException[1];
        window.paintFlat(new ClientWindow.Flat() {
            @Override public void fill(double x, double y, double w, double h, int argb) {
                try {
                    OmniClient.fill(graphics, (int) Math.round(ox + x * f), (int) Math.round(oy + y * f),
                            (int) Math.round(ox + (x + w) * f), (int) Math.round(oy + (y + h) * f), argb);
                } catch (ReflectiveOperationException e) { failure[0] = e; }
            }
            @Override public void text(String value, double x, double y, int argb) {
                try { OmniClient.text(graphics, value, (int) Math.round(ox + x * f), (int) Math.round(oy + y * f), argb); }
                catch (ReflectiveOperationException e) { failure[0] = e; }
            }
        });
        if (failure[0] != null) throw failure[0];
    }

    private static String keyName(int key) {
        return keyNames.computeIfAbsent(key, k -> {
            try { return bindings.keyName(k); }
            catch (Throwable error) { return "#" + k; }
        });
    }

    private static double clamp(double t) { return Math.max(0, Math.min(1, t)); }

    static void fill(Object g, int x, int y, int right, int bottom, int color) throws ReflectiveOperationException {
        bindings.fill.invoke(g, x, y, right, bottom, color);
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
        Zoom.disable();
    }

    static final class Bindings implements Widgets.Source {
        static final String MC = "net.minecraft.client.Minecraft", S = UiHooks.SCREEN;
        static final String FONT = "net.minecraft.client.gui.Font", GUI = "net.minecraft.client.gui.Gui";
        static final String INPUT = "com.mojang.blaze3d.platform.InputConstants", WINDOW = "com.mojang.blaze3d.platform.Window";
        static final String ENTITY = "net.minecraft.world.entity.Entity";
        final boolean modern;
        final Object minecraft, keyboard;
        final Field font, width, height, level, renderer, screenField, guiField, mouseHandler, player, hud, options, hideGui;
        final Method fill, text, blur, renderTarget, bindWrite, screenGet, screenSet, window, guiScale, keyLookup, displayName, string;
        final Method scissor, unscissor, guiWidth, guiHeight, screenWidth, xpos, ypos, fps, x, y, z, yaw, hudHidden;
        final Class<?> screenType;
        final int insert, escape, tab, space, enter, keypadEnter, leftButton, zoomDefault;
        Bindings(UiNames n, boolean modern) throws ReflectiveOperationException {
            this.modern = modern;
            minecraft = n.method(MC, "getInstance").invoke(null);
            screenType = n.type(S);
            font = n.field(MC, "font"); level = n.field(MC, "level"); renderer = n.field(MC, "gameRenderer");
            width = n.field(S, "width"); height = n.field(S, "height");
            String graphics = modern ? "net.minecraft.client.gui.GuiGraphicsExtractor" : "net.minecraft.client.gui.GuiGraphics";
            fill = n.method(graphics, "fill", "int", "int", "int", "int", "int");
            text = n.method(graphics, modern ? "text" : "drawString", FONT, "java.lang.String", "int", "int", "int", "boolean");
            scissor = n.method(graphics, "enableScissor", "int", "int", "int", "int");
            unscissor = n.method(graphics, "disableScissor");
            window = n.method(MC, "getWindow");
            guiScale = n.method(WINDOW, "getGuiScale");
            guiWidth = n.method(WINDOW, "getGuiScaledWidth");
            guiHeight = n.method(WINDOW, "getGuiScaledHeight");
            screenWidth = n.method(WINDOW, "getScreenWidth");
            mouseHandler = n.field(MC, "mouseHandler");
            xpos = n.method(UiHooks.MOUSE, "xpos");
            ypos = n.method(UiHooks.MOUSE, "ypos");
            fps = n.method(MC, "getFps");
            player = n.field(MC, "player");
            x = n.method(ENTITY, "getX"); y = n.method(ENTITY, "getY"); z = n.method(ENTITY, "getZ");
            yaw = n.method(ENTITY, "getYRot");
            insert = n.field(INPUT, "KEY_INSERT").getInt(null);
            escape = n.field(INPUT, "KEY_ESCAPE").getInt(null);
            tab = n.field(INPUT, "KEY_TAB").getInt(null);
            space = n.field(INPUT, "KEY_SPACE").getInt(null);
            enter = n.field(INPUT, "KEY_RETURN").getInt(null);
            keypadEnter = n.field(INPUT, "KEY_NUMPADENTER").getInt(null);
            zoomDefault = n.field(INPUT, "KEY_C").getInt(null);
            // GLFW numbers the left button 0; the SDL input of 26.x numbers it 1.
            leftButton = n.field(INPUT, "MOUSE_BUTTON_LEFT").getInt(null);
            // Key names come from the game: KEYSYM (GLFW) or KEYBOARD (SDL), always the first type.
            keyboard = n.type(INPUT + "$Type").getEnumConstants()[0];
            keyLookup = n.method(INPUT + "$Type", "getOrCreate", "int");
            displayName = n.method(INPUT + "$Key", "getDisplayName");
            string = n.method("net.minecraft.network.chat.Component", "getString");
            if (modern) {
                guiField = n.field(MC, "gui"); screenField = null;
                screenGet = n.method(GUI, "screen"); screenSet = n.method(GUI, "setScreen", S);
                blur = n.method(graphics, "blurBeforeThisStratum");
                renderTarget = bindWrite = null;
                // F1 lives in the Hud on 26.x.
                hud = n.field(GUI, "hud");
                hudHidden = n.method(UiHooks.HUD, "isHidden");
                options = hideGui = null;
            } else {
                guiField = null; screenField = n.field(MC, "screen"); screenGet = null;
                screenSet = n.method(MC, "setScreen", S);
                blur = n.method("net.minecraft.client.renderer.GameRenderer", "processBlurEffect", "float");
                renderTarget = n.method(MC, "getMainRenderTarget");
                bindWrite = n.method("com.mojang.blaze3d.pipeline.RenderTarget", "bindWrite", "boolean");
                hud = null; hudHidden = null;
                options = n.field(MC, "options");
                hideGui = n.field(UiHooks.OPTIONS, "hideGui");
            }
        }
        Object currentScreen() throws ReflectiveOperationException {
            return modern ? screenGet.invoke(guiField.get(minecraft)) : screenField.get(minecraft);
        }
        void setScreen(Object value) throws ReflectiveOperationException {
            screenSet.invoke(modern ? guiField.get(minecraft) : minecraft, value);
        }
        double guiScale() throws ReflectiveOperationException {
            return ((Number) guiScale.invoke(window.invoke(minecraft))).doubleValue();
        }
        int guiWidth() throws ReflectiveOperationException { return (int) guiWidth.invoke(window.invoke(minecraft)); }
        int guiHeight() throws ReflectiveOperationException { return (int) guiHeight.invoke(window.invoke(minecraft)); }
        // The mouse in GUI units, unrounded, so hover and drags are as smooth as the cursor.
        double[] mouse(int fallbackX, int fallbackY) {
            try {
                Object w = window.invoke(minecraft), m = mouseHandler.get(minecraft);
                double ratio = (int) guiWidth.invoke(w) / (double) (int) screenWidth.invoke(w);
                return new double[]{(double) xpos.invoke(m) * ratio, (double) ypos.invoke(m) * ratio};
            } catch (Throwable error) { return new double[]{fallbackX, fallbackY}; }
        }
        boolean hudHidden() throws ReflectiveOperationException {
            return modern ? (boolean) hudHidden.invoke(hud.get(guiField.get(minecraft))) : hideGui.getBoolean(options.get(minecraft));
        }
        void scissor(Object g, int x0, int y0, int x1, int y1) throws ReflectiveOperationException { scissor.invoke(g, x0, y0, x1, y1); }
        void unscissor(Object g) throws ReflectiveOperationException { unscissor.invoke(g); }
        String keyName(int key) throws ReflectiveOperationException {
            String name = (String) string.invoke(displayName.invoke(keyLookup.invoke(keyboard, key)));
            return name.length() == 1 ? name.toUpperCase() : name;
        }

        @Override public int fps() {
            try { return (int) fps.invoke(minecraft); } catch (Throwable e) { return 0; }
        }
        @Override public double[] position() {
            try {
                Object p = player.get(minecraft);
                return p == null ? null : new double[]{(double) x.invoke(p), (double) y.invoke(p), (double) z.invoke(p)};
            } catch (Throwable e) { return null; }
        }
        @Override public float yaw() {
            try { Object p = player.get(minecraft); return p == null ? 0 : (float) yaw.invoke(p); } catch (Throwable e) { return 0; }
        }
    }
}
