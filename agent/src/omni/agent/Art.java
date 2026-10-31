package omni.agent;

import java.awt.*;
import java.awt.font.TextAttribute;
import java.awt.geom.*;
import java.awt.image.*;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;

// Drawing kit shared by the client window and the HUD widgets: the launcher's palette
// (qml/Theme.qml), Manrope, and controls drawn the way the launcher's QML draws them.
// Everything is laid out in dp and rendered once per change at the real pixel density.
final class Art {
    enum Glyph { GRID, SPARKLE, LENS, LAYOUT, SLIDERS, BLUR, CLOCK, GAUGE, PIN, BACK, CLOSE, MOVE, CHEVRON, CHAT, SCROLL }

    static final Color TEXT = rgb(0xF5F1ED), MUTED = rgb(0xB0A7AF), FAINT = rgb(0x7B7279);
    static final Color ACCENT = rgb(0xE68AA6), ACCENT_TEXT = rgb(0xF3B7CB), INK = rgb(0x151216);
    static final Color TINT = argb(0xC71B171D), FILL = argb(0x0CFFFFFF), FILL_HOVER = argb(0x14FFFFFF);
    static final Color LINE = argb(0x1FFFFFFF), LINE_STRONG = argb(0x24FFFFFF);

    static final Font REGULAR = font("Manrope-Medium.ttf", Font.PLAIN), SEMIBOLD = font("Manrope-SemiBold.ttf", Font.BOLD);

    record Image(int[] argb, int width, int height) {}

    private Art() {}

    static int pixels(double dp, double scale) { return (int) Math.ceil(dp * scale); }

    // Paints `painter` (in dp) into a w x h dp image at `scale` pixels per dp.
    static Image render(double w, double h, double scale, Consumer<Graphics2D> painter) {
        int pw = Math.max(1, pixels(w, scale)), ph = Math.max(1, pixels(h, scale));
        BufferedImage image = new BufferedImage(pw, ph, BufferedImage.TYPE_INT_ARGB);
        Graphics2D g = image.createGraphics();
        try {
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
            g.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING, RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
            g.setRenderingHint(RenderingHints.KEY_FRACTIONALMETRICS, RenderingHints.VALUE_FRACTIONALMETRICS_ON);
            g.setRenderingHint(RenderingHints.KEY_STROKE_CONTROL, RenderingHints.VALUE_STROKE_PURE);
            g.setRenderingHint(RenderingHints.KEY_RENDERING, RenderingHints.VALUE_RENDER_QUALITY);
            g.scale(scale, scale);
            painter.accept(g);
        } finally { g.dispose(); }
        return new Image(((DataBufferInt) image.getRaster().getDataBuffer()).getData(), pw, ph);
    }

    // A rounded glass body with a soft shadow outside it only, so the shadow never darkens
    // the see-through tint, plus the launcher's top sheen and hairline border.
    static void glass(Graphics2D g, double x, double y, double w, double h, double r, Color tint, double shadow) {
        RoundRectangle2D body = new RoundRectangle2D.Double(x, y, w, h, 2 * r, 2 * r);
        if (shadow > 0) {
            Area outside = new Area(new Rectangle2D.Double(x - shadow, y - shadow, w + 2 * shadow, h + 2 * shadow + 8));
            outside.subtract(new Area(body));
            Shape clip = g.getClip();
            g.clip(outside);
            int steps = (int) Math.round(shadow * 0.7);
            for (int i = steps; i >= 1; i--) {
                double t = 1 - i / (steps + 1.0);
                g.setColor(new Color(0, 0, 0, (int) Math.round(255 * 0.022 * t * t)));
                g.fill(new RoundRectangle2D.Double(x - i, y - i + 7, w + 2 * i, h + 2 * i, 2 * r + 2 * i, 2 * r + 2 * i));
            }
            g.setClip(clip);
        }
        g.setColor(tint);
        g.fill(body);
        g.setPaint(new GradientPaint(0, (float) y, argb(0x0FFFFFFF), 0, (float) (y + Math.min(h, 160) * 0.5), argb(0x00FFFFFF)));
        g.fill(body);
        g.setColor(LINE);
        g.setStroke(new BasicStroke(1f));
        g.draw(new RoundRectangle2D.Double(x + 0.5, y + 0.5, w - 1, h - 1, 2 * r - 1, 2 * r - 1));
    }

    static void text(Graphics2D g, String value, Font font, double size, Color color, double x, double baseline, double tracking) {
        g.setFont(derive(font, size, tracking));
        g.setColor(color);
        g.drawString(value, (float) x, (float) baseline);
    }
    static double width(Graphics2D g, String value, Font font, double size, double tracking) {
        return g.getFontMetrics(derive(font, size, tracking)).getStringBounds(value, g).getWidth();
    }
    private static Font derive(Font font, double size, double tracking) {
        Font derived = font.deriveFont((float) size);
        return tracking == 0 ? derived : derived.deriveFont(Map.of(TextAttribute.TRACKING, tracking / size));
    }

    // Word-wraps to at most `lines` lines, ending the last one with an ellipsis if cut.
    static List<String> wrap(Graphics2D g, String value, Font font, double size, double max, int lines) {
        List<String> out = new ArrayList<>();
        String line = "";
        String[] words = value.split(" ");
        for (int i = 0; i < words.length; i++) {
            String next = line.isEmpty() ? words[i] : line + " " + words[i];
            if (width(g, next, font, size, 0) <= max || line.isEmpty()) { line = next; continue; }
            if (out.size() == lines - 1) {
                String rest = line;
                while (!rest.isEmpty() && width(g, rest + "…", font, size, 0) > max) rest = rest.substring(0, rest.length() - 1).trim();
                out.add(rest + "…");
                return out;
            }
            out.add(line);
            line = words[i];
        }
        if (!line.isEmpty()) out.add(line);
        return out;
    }

    static void round(Graphics2D g, double x, double y, double w, double h, double r) {
        g.fill(new RoundRectangle2D.Double(x, y, w, h, 2 * r, 2 * r));
    }

    // Same motion as the launcher's ToggleSwitch: one progress drives knob, track and glow.
    static void toggle(Graphics2D g, double x, double y, double p, boolean hovered) {
        if (p > 0.01) {
            for (int i = 6; i >= 1; i--) {
                g.setColor(alpha(ACCENT, (hovered ? 0.075 : 0.05) * p * (1 - i / 7.0)));
                g.fill(new RoundRectangle2D.Double(x - i, y - i, 42 + 2 * i, 24 + 2 * i, 24 + 2 * i, 24 + 2 * i));
            }
        }
        g.setColor(mix(argb(hovered ? 0x2EFFFFFF : 0x21FFFFFF), ACCENT, p));
        g.fill(new RoundRectangle2D.Double(x, y, 42, 24, 24, 24));
        g.setColor(mix(rgb(0xE6E8E7), INK, p * 0.92));
        g.fill(new Ellipse2D.Double(x + 3 + 18 * p, y + 3, 18, 18));
    }

    // A key label in a small cap, right-aligned at `right`; returns its width.
    static double keycap(Graphics2D g, String label, double right, double cy, boolean listening, boolean hovered) {
        double w = Math.max(44, width(g, label, SEMIBOLD, 13, 0) + 24), h = 30, x = right - w, y = cy - h / 2;
        g.setColor(listening ? argb(0x26E68AA6) : hovered ? argb(0x1FFFFFFF) : FILL_HOVER);
        g.fill(new RoundRectangle2D.Double(x, y, w, h, 16, 16));
        g.setColor(listening ? argb(0x99E68AA6) : LINE_STRONG);
        g.setStroke(new BasicStroke(1f));
        g.draw(new RoundRectangle2D.Double(x + 0.5, y + 0.5, w - 1, h - 1, 15, 15));
        text(g, label, SEMIBOLD, 13, listening ? ACCENT_TEXT : TEXT, x + (w - width(g, label, SEMIBOLD, 13, 0)) / 2, y + 19.5, 0);
        return w;
    }

    static void slider(Graphics2D g, double x, double cy, double w, double fraction, boolean hovered, boolean active) {
        double f = Math.max(0, Math.min(1, fraction));
        g.setColor(argb(0x21FFFFFF));
        g.fill(new RoundRectangle2D.Double(x, cy - 2, w, 4, 4, 4));
        g.setColor(ACCENT);
        g.fill(new RoundRectangle2D.Double(x, cy - 2, w * f, 4, 4, 4));
        double kx = x + w * f, r = active ? 9 : hovered ? 8.5 : 8;
        if (hovered || active) {
            g.setColor(alpha(ACCENT, 0.18));
            g.fill(new Ellipse2D.Double(kx - r - 5, cy - r - 5, 2 * r + 10, 2 * r + 10));
        }
        g.setColor(rgb(0xF5F1ED));
        g.fill(new Ellipse2D.Double(kx - r, cy - r, 2 * r, 2 * r));
    }

    // A pill button; returns its width. `primary` uses the accent like the launcher's main action.
    static double button(Graphics2D g, String label, double right, double cy, boolean primary, boolean hovered) {
        double w = width(g, label, SEMIBOLD, 13, 0) + 32, h = 32, x = right - w, y = cy - h / 2;
        if (primary) g.setColor(hovered ? ACCENT_TEXT : ACCENT);
        else g.setColor(hovered ? argb(0x24FFFFFF) : argb(0x17FFFFFF));
        g.fill(new RoundRectangle2D.Double(x, y, w, h, h, h));
        text(g, label, SEMIBOLD, 13, primary ? INK : TEXT, x + 16, y + 20.5, 0);
        return w;
    }

    // Line icons on a `size` dp square, in the launcher's rounded stroke style.
    static void icon(Graphics2D g, Glyph glyph, double x, double y, double s, Color color) {
        g.setColor(color);
        float stroke = (float) Math.max(1.2, s * 0.09);
        g.setStroke(new BasicStroke(stroke, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
        double cx = x + s / 2, cy = y + s / 2;
        switch (glyph) {
            case GRID -> {
                double c = (s - s * 0.16) / 2;
                for (int i = 0; i < 4; i++) round(g, x + (i % 2) * (c + s * 0.16), y + (i / 2) * (c + s * 0.16), c, c, c * 0.3);
            }
            case SPARKLE -> {
                g.fill(star(x + s * 0.42, y + s * 0.55, s * 0.4));
                g.fill(star(x + s * 0.8, y + s * 0.18, s * 0.17));
            }
            case LENS -> {
                double r = s * 0.29, lx = x + s * 0.42, ly = y + s * 0.42;
                g.draw(new Ellipse2D.Double(lx - r, ly - r, 2 * r, 2 * r));
                g.draw(new Line2D.Double(lx + r * 0.72, ly + r * 0.72, x + s * 0.88, y + s * 0.88));
            }
            case LAYOUT -> {
                double i = stroke / 2;
                g.draw(new RoundRectangle2D.Double(x + i + s * 0.06, y + i + s * 0.12, s * 0.88 - stroke, s * 0.76 - stroke, s * 0.22, s * 0.22));
                g.draw(new Line2D.Double(x + s * 0.42, y + s * 0.12, x + s * 0.42, y + s * 0.88));
                g.draw(new Line2D.Double(x + s * 0.42, y + s * 0.5, x + s * 0.94, y + s * 0.5));
            }
            case SLIDERS -> {
                double[] rows = {0.24, 0.5, 0.76}, knobs = {0.32, 0.7, 0.46};
                for (int i = 0; i < 3; i++) {
                    g.draw(new Line2D.Double(x + s * 0.1, y + s * rows[i], x + s * 0.9, y + s * rows[i]));
                    double r = s * 0.11;
                    g.fill(new Ellipse2D.Double(x + s * knobs[i] - r, y + s * rows[i] - r, 2 * r, 2 * r));
                }
            }
            case BLUR -> {
                g.fill(new Ellipse2D.Double(cx - s * 0.16, cy - s * 0.16, s * 0.32, s * 0.32));
                g.setColor(alpha(color, color.getAlpha() / 255.0 * 0.6));
                g.draw(new Ellipse2D.Double(cx - s * 0.3, cy - s * 0.3, s * 0.6, s * 0.6));
                g.setColor(alpha(color, color.getAlpha() / 255.0 * 0.3));
                g.draw(new Ellipse2D.Double(cx - s * 0.44, cy - s * 0.44, s * 0.88, s * 0.88));
            }
            case CLOCK -> {
                double r = s * 0.42;
                g.draw(new Ellipse2D.Double(cx - r, cy - r, 2 * r, 2 * r));
                g.draw(new Line2D.Double(cx, cy, cx, cy - r * 0.6));
                g.draw(new Line2D.Double(cx, cy, cx + r * 0.45, cy + r * 0.2));
            }
            case GAUGE -> {
                double r = s * 0.42, gy = cy + s * 0.08;
                g.draw(new Arc2D.Double(cx - r, gy - r, 2 * r, 2 * r, -20, 220, Arc2D.OPEN));
                double a = Math.toRadians(40);
                g.draw(new Line2D.Double(cx, gy, cx + Math.cos(a) * r * 0.7, gy - Math.sin(a) * r * 0.7));
                double d = s * 0.08;
                g.fill(new Ellipse2D.Double(cx - d, gy - d, 2 * d, 2 * d));
            }
            case PIN -> {
                double r = s * 0.32, py = y + s * 0.38;
                Path2D pin = new Path2D.Double();
                pin.moveTo(cx, y + s * 0.95);
                pin.curveTo(cx - r * 0.5, y + s * 0.72, cx - r, py + r * 0.55, cx - r, py);
                pin.curveTo(cx - r, py - r * 0.6, cx - r * 0.55, py - r, cx, py - r);
                pin.curveTo(cx + r * 0.55, py - r, cx + r, py - r * 0.6, cx + r, py);
                pin.curveTo(cx + r, py + r * 0.55, cx + r * 0.5, y + s * 0.72, cx, y + s * 0.95);
                pin.closePath();
                Area area = new Area(pin);
                double h = r * 0.42;
                area.subtract(new Area(new Ellipse2D.Double(cx - h, py - h, 2 * h, 2 * h)));
                g.fill(area);
            }
            case BACK -> {
                Path2D p = new Path2D.Double();
                p.moveTo(x + s * 0.6, y + s * 0.2); p.lineTo(x + s * 0.3, y + s * 0.5); p.lineTo(x + s * 0.6, y + s * 0.8);
                g.draw(p);
            }
            case CHEVRON -> {
                Path2D p = new Path2D.Double();
                p.moveTo(x + s * 0.4, y + s * 0.2); p.lineTo(x + s * 0.7, y + s * 0.5); p.lineTo(x + s * 0.4, y + s * 0.8);
                g.draw(p);
            }
            case CHAT -> {
                double i = stroke / 2;
                Path2D p = new Path2D.Double();
                p.append(new RoundRectangle2D.Double(x + s * 0.08 + i, y + s * 0.14 + i, s * 0.84 - stroke, s * 0.58 - stroke, s * 0.3, s * 0.3), false);
                g.draw(p);
                Path2D tail = new Path2D.Double();
                tail.moveTo(x + s * 0.3, y + s * 0.71); tail.lineTo(x + s * 0.24, y + s * 0.9); tail.lineTo(x + s * 0.46, y + s * 0.71);
                g.draw(tail);
                g.draw(new Line2D.Double(x + s * 0.3, y + s * 0.36, x + s * 0.7, y + s * 0.36));
                g.draw(new Line2D.Double(x + s * 0.3, y + s * 0.52, x + s * 0.56, y + s * 0.52));
            }
            case SCROLL -> {
                double i = stroke / 2;
                g.draw(new RoundRectangle2D.Double(x + s * 0.24 + i, y + s * 0.06 + i, s * 0.52 - stroke, s * 0.88 - stroke, s * 0.5, s * 0.5));
                g.draw(new Line2D.Double(cx, y + s * 0.24, cx, y + s * 0.4));
                Path2D p = new Path2D.Double();
                p.moveTo(cx - s * 0.1, y + s * 0.62); p.lineTo(cx, y + s * 0.72); p.lineTo(cx + s * 0.1, y + s * 0.62);
                g.draw(p);
            }
            case CLOSE -> {
                double r = s * 0.3;
                g.draw(new Line2D.Double(cx - r, cy - r, cx + r, cy + r));
                g.draw(new Line2D.Double(cx + r, cy - r, cx - r, cy + r));
            }
            case MOVE -> {
                double r = s * 0.42, a = s * 0.13;
                g.draw(new Line2D.Double(cx - r, cy, cx + r, cy));
                g.draw(new Line2D.Double(cx, cy - r, cx, cy + r));
                for (int i = 0; i < 4; i++) {
                    double dx = i == 0 ? 1 : i == 1 ? -1 : 0, dy = i == 2 ? 1 : i == 3 ? -1 : 0;
                    double tx = cx + dx * r, ty = cy + dy * r;
                    Path2D p = new Path2D.Double();
                    p.moveTo(tx - dx * a + dy * a, ty - dy * a + dx * a);
                    p.lineTo(tx, ty);
                    p.lineTo(tx - dx * a - dy * a, ty - dy * a - dx * a);
                    g.draw(p);
                }
            }
        }
    }
    private static Shape star(double cx, double cy, double r) {
        Path2D p = new Path2D.Double();
        p.moveTo(cx, cy - r);
        p.quadTo(cx, cy, cx + r, cy);
        p.quadTo(cx, cy, cx, cy + r);
        p.quadTo(cx, cy, cx - r, cy);
        p.quadTo(cx, cy, cx, cy - r);
        p.closePath();
        return p;
    }

    private static Font font(String file, int fallbackStyle) {
        try (InputStream in = Art.class.getResourceAsStream("/fonts/" + file)) {
            if (in != null) return Font.createFont(Font.TRUETYPE_FONT, in);
        } catch (Exception ignored) { }
        return new Font(Font.SANS_SERIF, fallbackStyle, 12);
    }
    static Color rgb(int value) { return new Color(value); }
    static Color argb(int value) { return new Color(value, true); }
    static Color alpha(Color c, double a) {
        return new Color(c.getRed(), c.getGreen(), c.getBlue(), (int) Math.round(255 * Math.max(0, Math.min(1, a))));
    }
    static Color mix(Color a, Color b, double t) {
        t = Math.max(0, Math.min(1, t));
        return new Color((int) Math.round(a.getRed() + (b.getRed() - a.getRed()) * t),
                (int) Math.round(a.getGreen() + (b.getGreen() - a.getGreen()) * t),
                (int) Math.round(a.getBlue() + (b.getBlue() - a.getBlue()) * t),
                (int) Math.round(a.getAlpha() + (b.getAlpha() - a.getAlpha()) * t));
    }
}
