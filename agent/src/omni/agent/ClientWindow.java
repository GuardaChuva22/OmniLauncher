package omni.agent;

import java.awt.Graphics2D;
import java.awt.Shape;
import java.awt.geom.*;
import java.util.*;
import java.util.function.IntFunction;

// The client window: a sidebar of pages, feature cards, and a page of options behind
// each card. It knows nothing about Minecraft: it lays out in dp, answers what is under
// a point, and paints two images. The chrome (frame, sidebar, header) is one image; the
// scrolling page is another, so scrolling only moves a texture and never repaints.
final class ClientWindow {
    static final double W = 760, H = 500, R = 24, SIDE = 204, MARGIN = 36;
    static final double VX = SIDE + 20, VY = 88, VW = W - SIDE - 40, VH = H - VY - 20;
    private static final double CARD_H = 132, ROW_H = 66, GAP = 12, HEADER_H = 104;
    private static final double TRACK_W = 150, TRACK_RIGHT = 26;

    enum Page {
        ALL("Módulos", "Todos os módulos", "Tudo o que o Omni adiciona ao jogo.", Art.Glyph.GRID, null),
        VISUAL("Visual", "Visual", "Aparência das interfaces.", Art.Glyph.SPARKLE, Catalog.VISUAL),
        CAMERA("Câmera", "Câmera", "Zoom e controle da visão.", Art.Glyph.LENS, Catalog.CAMERA),
        CHAT("Chat", "Chat", "Como as mensagens aparecem.", Art.Glyph.CHAT, Catalog.CHAT),
        WIDGETS("Widgets", "Widgets", "Informações na tela durante o jogo.", Art.Glyph.LAYOUT, Catalog.WIDGETS),
        SETTINGS("Configurações", "Configurações", "Preferências do próprio client.", Art.Glyph.SLIDERS, null);
        final String label, title, subtitle, category;
        final Art.Glyph glyph;
        Page(String label, String title, String subtitle, Art.Glyph glyph, String category) {
            this.label = label; this.title = title; this.subtitle = subtitle; this.glyph = glyph; this.category = category;
        }
    }

    enum Zone { NONE, DRAG, NAV, CLOSE, BACK, EDIT_HUD, CARD, CARD_TOGGLE, HEADER_TOGGLE, ROW, SLIDER }
    record Target(Zone zone, Page page, Module module, Option option) {
        static final Target NONE = new Target(Zone.NONE, null, null, null);
        boolean content() { return zone.ordinal() >= Zone.CARD.ordinal(); }
    }
    // What a press asks of the game side.
    enum Press { NONE, CHANGED, CLOSE, EDIT_HUD, DRAG }

    record Box(double x, double y, double w, double h) {
        boolean contains(double px, double py) { return px >= x && py >= y && px < x + w && py < y + h; }
    }
    private enum Kind { CARD, HEADER, SECTION, ROW, NOTE }
    private record Block(Kind kind, Box box, Module module, Option option, String text) {}

    private final Catalog catalog;
    private final IntFunction<String> keyNames;
    Page page = Page.ALL;
    Module detail;
    Target hover = Target.NONE;
    Option listening, sliding;
    private double scroll, scrollTarget;
    private long pageAt = System.nanoTime();
    private final Map<Option, Double> progress = new HashMap<>();
    private List<Block> blocks;
    private double contentH;
    private Object laidOut;

    ClientWindow(Catalog catalog, IntFunction<String> keyNames) {
        this.catalog = catalog;
        this.keyNames = keyNames;
    }

    // ---- State ----

    void open(Page next) {
        if (next == page && detail == null) return;
        page = next;
        detail = null;
        turnPage();
    }
    void openDetail(Module module) { detail = module; turnPage(); }
    // ESC: leave a feature's page first. False when there is nothing to go back to.
    boolean back() {
        if (detail == null) return false;
        Module from = detail;
        detail = null;
        if (page.category != null && !page.category.equals(from.category())) page = Page.ALL;
        turnPage();
        return true;
    }
    private void turnPage() {
        scroll = scrollTarget = 0;
        listening = sliding = null;
        pageAt = System.nanoTime();
    }
    // 0 → 1 over the page change, eased; the game side fades and slides the page with it.
    double pageProgress() {
        double t = Math.max(0, Math.min(1, (System.nanoTime() - pageAt) / 1e6 / 200));
        return 1 - Math.pow(1 - t, 3);
    }

    double scroll() { return scroll; }
    double maxScroll() { layout(); return Math.max(0, contentH - VH); }
    double contentHeight() { layout(); return contentH; }
    void scrollBy(double amount) { scrollTarget = Math.max(0, Math.min(maxScroll(), scrollTarget - amount * 64)); }

    void tick(double dt) {
        scrollTarget = Math.max(0, Math.min(maxScroll(), scrollTarget));
        scroll = scrollTarget + (scroll - scrollTarget) * Math.exp(-dt * 16);
        if (Math.abs(scroll - scrollTarget) < 0.25) scroll = scrollTarget;
        for (Option o : toggles()) {
            double target = o.on() ? 1 : 0, p = progress.getOrDefault(o, target);
            double next = target + (p - target) * Math.exp(-dt * 14);
            progress.put(o, Math.abs(next - target) < 0.002 ? target : next);
        }
    }
    private List<Option> toggles() {
        List<Option> out = new ArrayList<>();
        for (Module m : catalog.modules) {
            if (m.toggle() != null) out.add(m.toggle());
            for (Option o : m.options()) if (o.kind == Option.Kind.TOGGLE) out.add(o);
        }
        return out;
    }
    private double progress(Option o) { return smooth(progress.getOrDefault(o, o.on() ? 1.0 : 0.0)); }
    private static double smooth(double t) { return t * t * (3 - 2 * t); }

    // ---- Input (window dp) ----

    Target hit(double x, double y) {
        if (new Box(VX, VY, VW, VH).contains(x, y)) return contentHit(x - VX, y - VY + scroll);
        if (closeBox().contains(x, y)) return new Target(Zone.CLOSE, null, null, null);
        if (detail != null && backBox().contains(x, y)) return new Target(Zone.BACK, null, null, null);
        for (Page p : Page.values()) if (nav(p).contains(x, y)) return new Target(Zone.NAV, p, null, null);
        if (edit().contains(x, y)) return new Target(Zone.EDIT_HUD, null, null, null);
        if (new RoundRectangle2D.Double(0, 0, W, H, 2 * R, 2 * R).contains(x, y)) return new Target(Zone.DRAG, null, null, null);
        return Target.NONE;
    }

    private Target contentHit(double x, double y) {
        layout();
        for (Block b : blocks) {
            if (!b.box.contains(x, y)) continue;
            switch (b.kind) {
                case CARD -> {
                    if (b.module.toggle() != null && cardToggle(b.box).contains(x, y))
                        return new Target(Zone.CARD_TOGGLE, null, b.module, null);
                    return new Target(Zone.CARD, null, b.module, null);
                }
                case HEADER -> {
                    if (b.module.toggle() != null && headerToggle(b.box).contains(x, y))
                        return new Target(Zone.HEADER_TOGGLE, null, b.module, null);
                }
                case ROW -> {
                    if (b.option.kind == Option.Kind.SLIDER && track(b.box).contains(x, y))
                        return new Target(Zone.SLIDER, null, null, b.option);
                    return new Target(Zone.ROW, null, null, b.option);
                }
                default -> { }
            }
        }
        return Target.NONE;
    }

    Press press(double x, double y) {
        Target t = hit(x, y);
        if (listening != null) {
            Option was = listening;
            listening = null;
            if (t.zone == Zone.ROW && t.option == was) return Press.NONE;
        }
        switch (t.zone) {
            case NAV -> open(t.page);
            case CLOSE -> { return Press.CLOSE; }
            case BACK -> back();
            case EDIT_HUD -> { return Press.EDIT_HUD; }
            case DRAG -> { return Press.DRAG; }
            case CARD -> {
                if (!t.module.options().isEmpty()) openDetail(t.module);
                else if (t.module.toggle() != null) { t.module.toggle().flip(); return Press.CHANGED; }
            }
            case CARD_TOGGLE, HEADER_TOGGLE -> { t.module.toggle().flip(); return Press.CHANGED; }
            case ROW -> {
                switch (t.option.kind) {
                    case TOGGLE -> { t.option.flip(); return Press.CHANGED; }
                    case KEY -> listening = t.option;
                    case BUTTON -> { t.option.run(); return Press.CHANGED; }
                    case SLIDER -> { }
                }
            }
            case SLIDER -> { sliding = t.option; slide(x); }
            case NONE -> { }
        }
        return Press.NONE;
    }
    void slide(double x) {
        if (sliding == null) return;
        sliding.fraction((x - VX - trackX()) / TRACK_W);
    }
    // True when a slider was being dragged, so the caller saves.
    boolean release() {
        boolean was = sliding != null;
        sliding = null;
        return was;
    }

    // ---- Layout ----

    private static Box closeBox() { return new Box(W - 58, 26, 34, 34); }
    private static Box backBox() { return new Box(SIDE + 18, 28, 34, 34); }
    private static Box nav(Page p) {
        double y = switch (p) {
            case ALL -> 84;
            case VISUAL -> 160;
            case CAMERA -> 202;
            case CHAT -> 244;
            case WIDGETS -> 286;
            case SETTINGS -> H - 114;
        };
        return new Box(14, y, SIDE - 28, 38);
    }
    private static Box edit() { return new Box(14, H - 62, SIDE - 28, 42); }
    private static Box cardToggle(Box c) { return new Box(c.x + c.w - 64, c.y + 16, 56, 40); }
    private static Box headerToggle(Box c) { return new Box(c.x + c.w - 70, c.y + 30, 60, 44); }
    private static double trackX() { return VW - TRACK_RIGHT - TRACK_W; }
    private static Box track(Box row) { return new Box(trackX() - 14, row.y + 8, TRACK_W + 28, row.h - 16); }

    private void layout() {
        Object key = detail != null ? detail : page;
        if (key == laidOut && blocks != null) return;
        laidOut = key;
        blocks = new ArrayList<>();
        double y = 2;
        if (detail != null) {
            blocks.add(new Block(Kind.HEADER, new Box(0, y, VW, HEADER_H), detail, null, null));
            y += HEADER_H + 20;
            if (!detail.options().isEmpty()) {
                blocks.add(new Block(Kind.SECTION, new Box(0, y, VW, 20), null, null, "AJUSTES"));
                y += 26;
                for (Option o : detail.options()) { blocks.add(new Block(Kind.ROW, new Box(0, y, VW, ROW_H), null, o, null)); y += ROW_H + 8; }
            }
        } else if (page == Page.SETTINGS) {
            blocks.add(new Block(Kind.SECTION, new Box(0, y, VW, 20), null, null, "CLIENT"));
            y += 26;
            for (Option o : catalog.settings) { blocks.add(new Block(Kind.ROW, new Box(0, y, VW, ROW_H), null, o, null)); y += ROW_H + 8; }
            y += 12;
            blocks.add(new Block(Kind.SECTION, new Box(0, y, VW, 20), null, null, "SOBRE"));
            y += 26;
            blocks.add(new Block(Kind.NOTE, new Box(0, y, VW, 60), null, null,
                    "O Omni Client é injetado pelo Omni Launcher, sem mod loader. Funciona junto com Fabric e com o vanilla; "
                            + "as preferências ficam na pasta config da instância."));
            y += 60;
        } else {
            if (page == Page.WIDGETS) {
                blocks.add(new Block(Kind.ROW, new Box(0, y, VW, ROW_H + 6), null, catalog.editLayout, null));
                y += ROW_H + 6 + 18;
            }
            List<Module> list = catalog.category(page.category);
            double cw = (VW - GAP) / 2;
            for (int i = 0; i < list.size(); i++) {
                blocks.add(new Block(Kind.CARD, new Box((i % 2) * (cw + GAP), y + (i / 2) * (CARD_H + GAP), cw, CARD_H), list.get(i), null, null));
            }
            y += ((list.size() + 1) / 2) * (CARD_H + GAP) - GAP;
        }
        contentH = Math.max(y + 2, 1);
    }

    // ---- Painting ----

    // Comparable snapshots: the game side repaints an image only when its snapshot changes.
    Object chromeKey() {
        return List.of(page, Optional.ofNullable(detail).map(Module::id).orElse(""), hover.content() ? Target.NONE : hover);
    }
    Object contentKey() {
        List<Object> key = new ArrayList<>();
        key.add(page);
        key.add(Optional.ofNullable(detail).map(Module::id).orElse(""));
        key.add(hover.content() ? hover : Target.NONE);
        key.add(Optional.ofNullable(listening).map(o -> o.id).orElse(""));
        key.add(Optional.ofNullable(sliding).map(o -> o.id).orElse(""));
        key.addAll(catalog.values());
        for (Option o : toggles()) key.add(Math.round(progress(o) * 500));
        key.add(keyNames.apply(Zoom.key));
        key.add(keyNames.apply(catalog.panelKey));
        return key;
    }

    Art.Image renderChrome(double scale) {
        return Art.render(W + 2 * MARGIN, H + 2 * MARGIN, scale, g -> { g.translate(MARGIN, MARGIN); paintChrome(g); });
    }
    Art.Image renderContent(double scale) {
        layout();
        return Art.render(VW, contentH, scale, this::paintContent);
    }

    // The same window with plain rectangles and the game's font, if textures are unavailable.
    interface Flat {
        void fill(double x, double y, double w, double h, int argb);
        void text(String value, double x, double y, int argb);
    }
    void paintFlat(Flat f) {
        layout();
        f.fill(0, 0, W, H, 0xEE1B171D);
        f.fill(0, 0, SIDE, H, 0x0AFFFFFF);
        f.text("Omni Client", 24, 30, 0xFFF5F1ED);
        for (Page p : Page.values()) {
            Box b = nav(p);
            if (p == page) f.fill(b.x, b.y, b.w, b.h, 0x17FFFFFF);
            f.text(p.label, b.x + 12, b.y + 15, p == page ? 0xFFF5F1ED : 0xFFB0A7AF);
        }
        Box e = edit();
        f.fill(e.x, e.y, e.w, e.h, 0x24E68AA6);
        f.text("Editar HUD", e.x + 12, e.y + 16, 0xFFF3B7CB);
        f.text(detail != null ? "< " + detail.name() : page.title, VX, 40, 0xFFF5F1ED);
        Box c = closeBox();
        f.text("x", c.x + 13, c.y + 12, hover.zone == Zone.CLOSE ? 0xFFF5F1ED : 0xFFB0A7AF);
        for (Block b : blocks) {
            double y = VY + b.box.y - scroll;
            if (y < VY || y + b.box.h > VY + VH) continue;
            boolean over = b.module != null && hover.module == b.module || b.option != null && hover.option == b.option;
            String title, value;
            switch (b.kind) {
                case CARD, HEADER -> { title = b.module.name(); value = b.module.on() ? "Ativado" : "Desativado"; }
                case ROW -> {
                    title = b.option.title;
                    value = switch (b.option.kind) {
                        case TOGGLE -> b.option.on() ? "Ativado" : "Desativado";
                        case KEY -> listening == b.option ? "Pressione uma tecla" : keyNames.apply(b.option.key());
                        case SLIDER -> b.option.formatted();
                        case BUTTON -> b.option.label;
                    };
                }
                default -> { continue; }
            }
            f.fill(VX + b.box.x, y, b.box.w, b.box.h, over ? 0xFF2B252D : 0xFF231F25);
            f.text(title, VX + b.box.x + 10, y + 10, 0xFFF5F1ED);
            f.text(value, VX + b.box.x + 10, y + 24, 0xFFB0A7AF);
        }
    }

    private void paintChrome(Graphics2D g) {
        Art.glass(g, 0, 0, W, H, R, Art.TINT, MARGIN);
        Shape clip = g.getClip();
        g.clip(new RoundRectangle2D.Double(0, 0, W, H, 2 * R, 2 * R));
        g.setColor(Art.argb(0x0AFFFFFF));
        g.fill(new Rectangle2D.Double(0, 0, SIDE, H));
        g.setColor(Art.LINE);
        g.fill(new Rectangle2D.Double(SIDE, 0, 1, H));
        g.setClip(clip);

        // The modular Omni mark, as in the launcher.
        double mx = 26, my = 30;
        g.setColor(Art.ACCENT);
        Art.round(g, mx, my, 10, 10, 3.5);
        Art.round(g, mx + 12, my, 10, 10, 3.5);
        Art.round(g, mx, my + 12, 10, 10, 3.5);
        g.setColor(Art.ACCENT_TEXT);
        Art.round(g, mx + 12, my + 12, 10, 10, 3.5);
        Art.text(g, "Omni", Art.SEMIBOLD, 17, Art.TEXT, 60, 40, -0.2);
        Art.text(g, "Client do jogo", Art.REGULAR, 11.5, Art.MUTED, 60, 56, 0);

        navItem(g, Page.ALL, catalog.modules.size());
        Art.text(g, "CATEGORIAS", Art.SEMIBOLD, 10.5, Art.FAINT, 28, 150, 1.1);
        navItem(g, Page.VISUAL, catalog.category(Catalog.VISUAL).size());
        navItem(g, Page.CAMERA, catalog.category(Catalog.CAMERA).size());
        navItem(g, Page.CHAT, catalog.category(Catalog.CHAT).size());
        navItem(g, Page.WIDGETS, catalog.category(Catalog.WIDGETS).size());
        navItem(g, Page.SETTINGS, -1);

        Box e = edit();
        boolean overEdit = hover.zone == Zone.EDIT_HUD;
        g.setColor(Art.argb(overEdit ? 0x38E68AA6 : 0x24E68AA6));
        Art.round(g, e.x, e.y, e.w, e.h, 14);
        Art.icon(g, Art.Glyph.MOVE, e.x + 14, e.y + 12, 18, Art.ACCENT_TEXT);
        Art.text(g, "Editar HUD", Art.SEMIBOLD, 13.5, Art.ACCENT_TEXT, e.x + 42, e.y + 26, 0);

        // Header: back button on a feature's page, title and subtitle, close.
        double tx = VX + 2;
        String title = detail != null ? detail.name() : page.title;
        String subtitle = detail != null ? detail.category() : page.subtitle;
        if (detail != null) {
            Box b = backBox();
            roundButton(g, b, Art.Glyph.BACK, hover.zone == Zone.BACK);
            tx = b.x + b.w + 12;
        }
        Art.text(g, title, Art.SEMIBOLD, 21, Art.TEXT, tx, 50, -0.3);
        Art.text(g, subtitle, Art.REGULAR, 12.5, Art.MUTED, tx, 70, 0);
        roundButton(g, closeBox(), Art.Glyph.CLOSE, hover.zone == Zone.CLOSE);
    }

    private void navItem(Graphics2D g, Page p, int count) {
        Box b = nav(p);
        boolean selected = p == page, over = hover.zone == Zone.NAV && hover.page == p;
        if (selected || over) {
            g.setColor(selected ? Art.argb(0x17FFFFFF) : Art.argb(0x0DFFFFFF));
            Art.round(g, b.x, b.y, b.w, b.h, 12);
        }
        if (selected) {
            g.setColor(Art.ACCENT);
            Art.round(g, b.x, b.y + 11, 3, 16, 1.5);
        }
        Art.icon(g, p.glyph, b.x + 14, b.y + 11, 16, selected ? Art.ACCENT_TEXT : Art.MUTED);
        Art.text(g, p.label, Art.SEMIBOLD, 13.5, selected || over ? Art.TEXT : Art.MUTED, b.x + 40, b.y + 24, 0);
        if (count >= 0) {
            String n = Integer.toString(count);
            Art.text(g, n, Art.SEMIBOLD, 11.5, Art.FAINT, b.x + b.w - 14 - Art.width(g, n, Art.SEMIBOLD, 11.5, 0), b.y + 24, 0);
        }
    }

    private static void roundButton(Graphics2D g, Box b, Art.Glyph glyph, boolean over) {
        if (over) {
            g.setColor(Art.FILL_HOVER);
            g.fill(new Ellipse2D.Double(b.x, b.y, b.w, b.h));
        }
        Art.icon(g, glyph, b.x + b.w / 2 - 8, b.y + b.h / 2 - 8, 16, over ? Art.TEXT : Art.MUTED);
    }

    private void paintContent(Graphics2D g) {
        for (Block b : blocks) {
            switch (b.kind) {
                case CARD -> card(g, b);
                case HEADER -> header(g, b);
                case SECTION -> Art.text(g, b.text, Art.SEMIBOLD, 10.5, Art.FAINT, b.box.x + 4, b.box.y + 13, 1.1);
                case ROW -> row(g, b);
                case NOTE -> {
                    List<String> lines = Art.wrap(g, b.text, Art.REGULAR, 12.5, b.box.w - 8, 3);
                    for (int i = 0; i < lines.size(); i++) Art.text(g, lines.get(i), Art.REGULAR, 12.5, Art.MUTED, b.box.x + 4, b.box.y + 14 + i * 19, 0);
                }
            }
        }
    }

    private void card(Graphics2D g, Block b) {
        Box c = b.box;
        Module m = b.module;
        boolean over = (hover.zone == Zone.CARD || hover.zone == Zone.CARD_TOGGLE) && hover.module == m;
        double on = m.toggle() == null ? 1 : progress(m.toggle());
        g.setColor(over ? Art.FILL_HOVER : Art.FILL);
        Art.round(g, c.x, c.y, c.w, c.h, 18);
        if (on > 0.01) {
            g.setColor(Art.alpha(Art.ACCENT, 0.22 * on));
            g.setStroke(new java.awt.BasicStroke(1f));
            g.draw(new RoundRectangle2D.Double(c.x + 0.5, c.y + 0.5, c.w - 1, c.h - 1, 35, 35));
        }
        tile(g, m.glyph(), c.x + 16, c.y + 16, 40, on);
        if (m.toggle() != null) Art.toggle(g, c.x + c.w - 16 - 42, c.y + 24, on, hover.zone == Zone.CARD_TOGGLE && hover.module == m);
        Art.text(g, m.name(), Art.SEMIBOLD, 15, Art.TEXT, c.x + 16, c.y + 82, -0.1);
        if (!m.options().isEmpty()) {
            double nx = c.x + 16 + Art.width(g, m.name(), Art.SEMIBOLD, 15, -0.1) + 4;
            Art.icon(g, Art.Glyph.CHEVRON, nx, c.y + 70, 14, over ? Art.TEXT : Art.FAINT);
        }
        List<String> lines = Art.wrap(g, m.detail(), Art.REGULAR, 12, c.w - 32, 2);
        for (int i = 0; i < lines.size(); i++) Art.text(g, lines.get(i), Art.REGULAR, 12, Art.MUTED, c.x + 16, c.y + 102 + i * 17, 0);
    }

    private void header(Graphics2D g, Block b) {
        Box c = b.box;
        Module m = b.module;
        double on = m.toggle() == null ? 1 : progress(m.toggle());
        g.setColor(Art.FILL);
        Art.round(g, c.x, c.y, c.w, c.h, 20);
        tile(g, m.glyph(), c.x + 20, c.y + 24, 56, on);
        Art.text(g, m.name(), Art.SEMIBOLD, 18, Art.TEXT, c.x + 92, c.y + 45, -0.2);
        List<String> lines = Art.wrap(g, m.detail(), Art.REGULAR, 12.5, c.w - 92 - 90, 2);
        for (int i = 0; i < lines.size(); i++) Art.text(g, lines.get(i), Art.REGULAR, 12.5, Art.MUTED, c.x + 92, c.y + 66 + i * 18, 0);
        if (m.toggle() != null) Art.toggle(g, c.x + c.w - 20 - 42, c.y + 40, on, hover.zone == Zone.HEADER_TOGGLE);
    }

    private static void tile(Graphics2D g, Art.Glyph glyph, double x, double y, double size, double on) {
        g.setColor(Art.mix(Art.argb(0x0FFFFFFF), Art.argb(0x29E68AA6), on));
        Art.round(g, x, y, size, size, size * 0.3);
        double s = size * 0.5;
        Art.icon(g, glyph, x + (size - s) / 2, y + (size - s) / 2, s, Art.mix(Art.MUTED, Art.ACCENT_TEXT, on));
    }

    private void row(Graphics2D g, Block b) {
        Box r = b.box;
        Option o = b.option;
        boolean over = hover.option == o && (hover.zone == Zone.ROW || hover.zone == Zone.SLIDER);
        boolean clickable = o.kind != Option.Kind.SLIDER;
        g.setColor(over && clickable ? Art.FILL_HOVER : Art.FILL);
        Art.round(g, r.x, r.y, r.w, r.h, 18);
        if (listening == o) {
            g.setColor(Art.alpha(Art.ACCENT, 0.6));
            g.setStroke(new java.awt.BasicStroke(1.5f));
            g.draw(new RoundRectangle2D.Double(r.x + 0.75, r.y + 0.75, r.w - 1.5, r.h - 1.5, 34.5, 34.5));
        }
        double cy = r.y + r.h / 2, right = r.x + r.w - 18, left;
        switch (o.kind) {
            case TOGGLE -> { Art.toggle(g, right - 42, cy - 12, progress(o), over); left = right - 42; }
            case KEY -> left = right - Art.keycap(g, listening == o ? "Pressione…" : keyNames.apply(o.key()), right, cy, listening == o, over);
            case SLIDER -> {
                Art.slider(g, trackX(), cy, TRACK_W, o.fraction(), hover.zone == Zone.SLIDER && over, sliding == o);
                String v = o.formatted();
                left = trackX() - 18 - Art.width(g, v, Art.SEMIBOLD, 13, 0);
                Art.text(g, v, Art.SEMIBOLD, 13, Art.ACCENT_TEXT, left, cy + 4.5, 0);
            }
            case BUTTON -> left = right - Art.button(g, o.label, right, cy, o == catalog.editLayout, over);
            default -> left = right;
        }
        double textW = left - r.x - 18 - 16;
        Art.text(g, o.title, Art.SEMIBOLD, 14, Art.TEXT, r.x + 18, cy - 3, -0.1);
        String detailText = listening == o ? "Pressione a nova tecla. ESC cancela." : o.detail;
        List<String> lines = Art.wrap(g, detailText, Art.REGULAR, 12, textW, 1);
        if (!lines.isEmpty()) Art.text(g, lines.get(0), Art.REGULAR, 12, Art.MUTED, r.x + 18, cy + 15, 0);
    }
}
