package omni.agent;

import java.awt.Graphics2D;
import java.time.LocalTime;
import java.util.ArrayList;
import java.util.List;

// HUD widgets: small glass pills drawn over the game. Each one turns live data into a
// Face (an icon and text segments); a widget is redrawn only when its face changes.
// Positions are fractions of the free space, so widgets keep to their edge or centre
// at any window size.
final class Widgets {
    static final double BODY_H = 30, PAD = 12, MARGIN = 10;

    record Seg(String text, boolean muted) {}
    record Face(Art.Glyph glyph, List<Seg> segs) {}

    // What a widget may read from the game; empty values when there is no world.
    interface Source {
        int fps();
        double[] position();
        float yaw();
    }

    abstract static class Widget {
        final String id, name, detail;
        final Art.Glyph glyph;
        final double defaultX, defaultY;
        boolean enabled;
        double scale = 1, x, y;
        Widget(String id, String name, String detail, Art.Glyph glyph, double x, double y) {
            this.id = id; this.name = name; this.detail = detail; this.glyph = glyph;
            this.defaultX = this.x = x; this.defaultY = this.y = y;
        }
        abstract Face face(Source source);
        List<Option> options() { return List.of(); }
        void reset() { x = defaultX; y = defaultY; scale = 1; }
    }

    static final class Clock extends Widget {
        boolean seconds;
        Clock() { super("clock", "Relógio", "A hora do seu computador, sempre à vista.", Art.Glyph.CLOCK, 1, 0); }
        @Override Face face(Source source) {
            LocalTime t = LocalTime.now();
            String text = seconds ? String.format("%02d:%02d:%02d", t.getHour(), t.getMinute(), t.getSecond())
                    : String.format("%02d:%02d", t.getHour(), t.getMinute());
            return new Face(glyph, List.of(new Seg(text, false)));
        }
        @Override List<Option> options() {
            return List.of(Option.toggle("widget.clock.seconds", "Mostrar segundos", "Exibe também os segundos.",
                    () -> seconds, v -> seconds = v));
        }
    }

    static final class Fps extends Widget {
        Fps() { super("fps", "FPS", "Quadros por segundo do jogo.", Art.Glyph.GAUGE, 0, 0.1); }
        @Override Face face(Source source) {
            return new Face(glyph, List.of(new Seg(Integer.toString(source.fps()), false), new Seg("FPS", true)));
        }
    }

    static final class Coordinates extends Widget {
        boolean direction = true;
        Coordinates() { super("coords", "Coordenadas", "Sua posição no mundo, e para onde está olhando.", Art.Glyph.PIN, 0, 0); }
        @Override Face face(Source source) {
            double[] p = source.position();
            List<Seg> segs = new ArrayList<>();
            String[] axes = {"X", "Y", "Z"};
            for (int i = 0; i < 3; i++) {
                segs.add(new Seg(axes[i], true));
                segs.add(new Seg(p == null ? "—" : Long.toString((long) Math.floor(p[i])), false));
            }
            if (direction && p != null) segs.add(new Seg(facing(source.yaw()), true));
            return new Face(glyph, segs);
        }
        @Override List<Option> options() {
            return List.of(Option.toggle("widget.coords.direction", "Mostrar direção", "Norte, sul, leste ou oeste.",
                    () -> direction, v -> direction = v));
        }
        // Minecraft's yaw: 0 faces south, 90 west, 180 north, 270 east.
        static String facing(float yaw) {
            String[] names = {"S", "SO", "O", "NO", "N", "NE", "L", "SE"};
            int i = (int) Math.floorMod(Math.round(yaw / 45.0), 8);
            return names[i];
        }
    }

    final List<Widget> all = List.of(new Coordinates(), new Fps(), new Clock());

    // Image of a widget: the body plus a margin for its shadow, at `scale` px per dp.
    static Art.Image render(Face face, double scale) {
        double[] size = new double[1];
        Art.render(1, 1, 1, g -> size[0] = bodyWidth(g, face));
        double w = size[0];
        return Art.render(w + 2 * MARGIN, BODY_H + 2 * MARGIN, scale, g -> paint(g, face, w));
    }

    private static double bodyWidth(Graphics2D g, Face face) {
        double w = PAD + 14 + 8;
        for (int i = 0; i < face.segs().size(); i++) {
            Seg s = face.segs().get(i);
            w += Art.width(g, s.text(), Art.SEMIBOLD, s.muted() ? 11 : 13, 0) + (i == 0 ? 0 : gap(face, i));
        }
        return Math.ceil(w + PAD);
    }
    private static double gap(Face face, int i) {
        // Labels sit close to their values; value-label pairs are spaced apart.
        return face.segs().get(i).muted() && i > 0 && !face.segs().get(i - 1).muted() ? 5 : face.segs().get(i).muted() ? 10 : 5;
    }

    private static void paint(Graphics2D g, Face face, double w) {
        Art.glass(g, MARGIN, MARGIN, w, BODY_H, BODY_H / 2, Art.argb(0xD61B171D), MARGIN);
        Art.icon(g, face.glyph(), MARGIN + PAD, MARGIN + (BODY_H - 14) / 2, 14, Art.ACCENT_TEXT);
        double x = MARGIN + PAD + 14 + 8, base = MARGIN + BODY_H / 2 + 4.6;
        for (int i = 0; i < face.segs().size(); i++) {
            Seg s = face.segs().get(i);
            if (i > 0) x += gap(face, i);
            double size = s.muted() ? 11 : 13;
            Art.text(g, s.text(), Art.SEMIBOLD, size, s.muted() ? Art.MUTED : Art.TEXT, x, base - (s.muted() ? 0.5 : 0), 0);
            x += Art.width(g, s.text(), Art.SEMIBOLD, size, 0);
        }
    }

    // The bar shown at the top while the HUD layout is being edited.
    static final double BANNER_W = 520, BANNER_H = 56;
    static final double DONE_W = 104;
    static Art.Image banner(boolean any, boolean doneHovered, double scale) {
        return Art.render(BANNER_W + 2 * MARGIN * 2, BANNER_H + 2 * MARGIN * 2, scale, g -> {
            double x = MARGIN * 2, y = MARGIN * 2;
            Art.glass(g, x, y, BANNER_W, BANNER_H, BANNER_H / 2, Art.argb(0xE01B171D), MARGIN * 2);
            Art.icon(g, Art.Glyph.MOVE, x + 20, y + 18, 20, Art.ACCENT_TEXT);
            Art.text(g, "Editando o HUD", Art.SEMIBOLD, 14, Art.TEXT, x + 54, y + 24, -0.1);
            Art.text(g, any ? "Arraste para mover, role o mouse para redimensionar."
                            : "Nenhum widget ativo. Ative em Widgets.", Art.REGULAR, 11.5, Art.MUTED, x + 54, y + 41, 0);
            Art.button(g, "Concluir", x + BANNER_W - 12, y + BANNER_H / 2, true, doneHovered);
        });
    }
}
