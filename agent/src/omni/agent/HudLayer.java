package omni.agent;

import java.util.*;

// Draws the enabled widgets over the game, and runs the HUD layout editor: drag a widget
// to move it, with snapping to the edges and the centre; scroll over it to resize. All
// geometry is in physical pixels; textures are drawn at 1/guiScale.
final class HudLayer {
    static final double EDGE = 8, SNAP = 8;
    enum Press { NONE, MOVE, DONE }

    private final UiNames names;
    private final OmniClient.Bindings bindings;
    private final Catalog catalog;
    private final Widgets.Source source;
    private final Map<Widgets.Widget, Slot> slots = new HashMap<>();
    private PanelTexture banner;
    private List<Object> bannerDrawn;
    private Widgets.Widget dragging, hovered;
    private double grabX, grabY, guideX = Double.NaN, guideY = Double.NaN;
    private boolean doneHovered;

    private static final class Slot {
        PanelTexture texture;
        Widgets.Face face;
        double unit;
    }
    // Where a widget's body sits on screen, in physical pixels.
    private record Place(double x, double y, double w, double h, double unit) {
        boolean contains(double px, double py) { return px >= x && py >= y && px < x + w && py < y + h; }
    }

    HudLayer(UiNames names, OmniClient.Bindings bindings, Catalog catalog, Widgets.Source source) {
        this.names = names; this.bindings = bindings; this.catalog = catalog; this.source = source;
    }

    boolean any() {
        for (Widgets.Widget w : catalog.widgets.all) if (w.enabled) return true;
        return false;
    }

    // `unit` is the window's px per dp; each widget multiplies it by its own size.
    void draw(Object graphics, double screenW, double screenH, double gui, double unit) throws ReflectiveOperationException {
        for (Widgets.Widget w : catalog.widgets.all) {
            if (!w.enabled) continue;
            Place p = place(w, screenW, screenH, unit);
            Slot s = slots.get(w);
            s.texture.draw(graphics, (p.x - Widgets.MARGIN * p.unit) / gui, (p.y - Widgets.MARGIN * p.unit) / gui, 1 / gui, 1);
        }
    }

    // The editor: the widgets with outlines, guides while snapping, and the banner.
    void drawEditor(Object graphics, double screenW, double screenH, double gui, double unit, double mx, double my)
            throws ReflectiveOperationException {
        OmniClient.fill(graphics, 0, 0, (int) Math.ceil(screenW / gui), (int) Math.ceil(screenH / gui), 0x30080608);
        hovered = null;
        if (dragging == null) {
            for (Widgets.Widget w : catalog.widgets.all)
                if (w.enabled && place(w, screenW, screenH, unit).contains(mx, my)) hovered = w;
        }
        if (!Double.isNaN(guideX)) line(graphics, guideX, 0, guideX, screenH, gui, 0x99E68AA6);
        if (!Double.isNaN(guideY)) line(graphics, 0, guideY, screenW, guideY, gui, 0x99E68AA6);
        draw(graphics, screenW, screenH, gui, unit);
        for (Widgets.Widget w : catalog.widgets.all) {
            if (!w.enabled) continue;
            Place p = place(w, screenW, screenH, unit);
            boolean active = w == dragging || w == hovered;
            outline(graphics, p, gui, active ? 0xE6F3B7CB : 0x59FFFFFF);
        }
        Place b = bannerPlace(screenW, unit);
        doneHovered = done(b).contains(mx, my);
        List<Object> key = List.of(any(), doneHovered, unit);
        if (banner == null) banner = new PanelTexture(names, bindings, "client/hud_banner");
        if (!key.equals(bannerDrawn)) { banner.upload(Widgets.banner(any(), doneHovered, unit)); bannerDrawn = key; }
        banner.draw(graphics, (b.x - 2 * Widgets.MARGIN * unit) / gui, (b.y - 2 * Widgets.MARGIN * unit) / gui, 1 / gui, 1);
    }

    Press press(double screenW, double screenH, double unit, double mx, double my) {
        if (done(bannerPlace(screenW, unit)).contains(mx, my)) return Press.DONE;
        List<Widgets.Widget> order = new ArrayList<>(catalog.widgets.all);
        Collections.reverse(order);
        for (Widgets.Widget w : order) {
            if (!w.enabled) continue;
            Place p = place(w, screenW, screenH, unit);
            if (p.contains(mx, my)) {
                dragging = w;
                grabX = mx - p.x;
                grabY = my - p.y;
                return Press.MOVE;
            }
        }
        return Press.NONE;
    }

    void drag(double screenW, double screenH, double unit, double mx, double my) {
        if (dragging == null) return;
        Place p = place(dragging, screenW, screenH, unit);
        double edge = EDGE * unit, snap = SNAP * unit;
        double freeW = screenW - p.w - 2 * edge, freeH = screenH - p.h - 2 * edge;
        double x = mx - grabX, y = my - grabY;
        guideX = guideY = Double.NaN;
        double fx = freeW <= 0 ? 0 : (x - edge) / freeW, fy = freeH <= 0 ? 0 : (y - edge) / freeH;
        if (Math.abs(x + p.w / 2 - screenW / 2) < snap) { fx = 0.5; guideX = screenW / 2; }
        else if (Math.abs(x - edge) < snap) fx = 0;
        else if (Math.abs(x + p.w - (screenW - edge)) < snap) fx = 1;
        if (Math.abs(y + p.h / 2 - screenH / 2) < snap) { fy = 0.5; guideY = screenH / 2; }
        else if (Math.abs(y - edge) < snap) fy = 0;
        else if (Math.abs(y + p.h - (screenH - edge)) < snap) fy = 1;
        dragging.x = Catalog.clamp(fx);
        dragging.y = Catalog.clamp(fy);
    }

    // True when a widget was moved, so the caller saves.
    boolean release() {
        boolean was = dragging != null;
        dragging = null;
        guideX = guideY = Double.NaN;
        return was;
    }

    boolean scroll(double screenW, double screenH, double unit, double mx, double my, double amount) {
        for (Widgets.Widget w : catalog.widgets.all) {
            if (!w.enabled || !place(w, screenW, screenH, unit).contains(mx, my)) continue;
            double next = w.scale * (amount > 0 ? 1.1 : 1 / 1.1);
            w.scale = Math.max(0.5, Math.min(2, Math.round(next * 20) / 20.0));
            return true;
        }
        return false;
    }

    void cancel() { release(); hovered = null; }

    // Renders the widget's face when it changed, then works out where its body goes.
    private Place place(Widgets.Widget w, double screenW, double screenH, double unit) {
        double u = unit * w.scale;
        Slot s = slots.computeIfAbsent(w, k -> new Slot());
        try {
            Widgets.Face face = w.face(source);
            if (s.texture == null) s.texture = new PanelTexture(names, bindings, "client/widget_" + w.id);
            if (!face.equals(s.face) || u != s.unit) {
                s.texture.upload(Widgets.render(face, u));
                s.face = face;
                s.unit = u;
            }
        } catch (ReflectiveOperationException e) { throw new IllegalStateException(e); }
        double bw = s.texture.width() - 2 * Widgets.MARGIN * u, bh = s.texture.height() - 2 * Widgets.MARGIN * u;
        double edge = EDGE * unit;
        double x = edge + w.x * Math.max(0, screenW - bw - 2 * edge), y = edge + w.y * Math.max(0, screenH - bh - 2 * edge);
        return new Place(x, y, bw, bh, u);
    }

    private static Place bannerPlace(double screenW, double unit) {
        double w = Widgets.BANNER_W * unit, h = Widgets.BANNER_H * unit;
        return new Place((screenW - w) / 2, 18 * unit, w, h, unit);
    }
    private static Place done(Place banner) {
        double u = banner.unit;
        return new Place(banner.x + banner.w - (12 + Widgets.DONE_W) * u, banner.y + (Widgets.BANNER_H / 2 - 16) * u, Widgets.DONE_W * u, 32 * u, u);
    }

    private static void outline(Object g, Place p, double gui, int color) throws ReflectiveOperationException {
        int x0 = (int) Math.floor((p.x - 3 * p.unit) / gui), y0 = (int) Math.floor((p.y - 3 * p.unit) / gui);
        int x1 = (int) Math.ceil((p.x + p.w + 3 * p.unit) / gui), y1 = (int) Math.ceil((p.y + p.h + 3 * p.unit) / gui);
        OmniClient.fill(g, x0, y0, x1, y0 + 1, color);
        OmniClient.fill(g, x0, y1 - 1, x1, y1, color);
        OmniClient.fill(g, x0, y0 + 1, x0 + 1, y1 - 1, color);
        OmniClient.fill(g, x1 - 1, y0 + 1, x1, y1 - 1, color);
    }
    private static void line(Object g, double x0, double y0, double x1, double y1, double gui, int color) throws ReflectiveOperationException {
        int ax = (int) Math.floor(x0 / gui), ay = (int) Math.floor(y0 / gui);
        OmniClient.fill(g, ax, ay, Math.max(ax + 1, (int) Math.ceil(x1 / gui)), Math.max(ay + 1, (int) Math.ceil(y1 / gui)), color);
    }
}
