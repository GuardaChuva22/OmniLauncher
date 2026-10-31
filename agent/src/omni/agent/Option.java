package omni.agent;

import java.util.function.*;

// One adjustable value: a switch, a key, a slider or a button. Options with an id are
// saved; the window draws and edits every kind the same way, so a new feature only
// declares its options.
final class Option {
    enum Kind { TOGGLE, KEY, SLIDER, BUTTON }
    final Kind kind;
    final String id, title, detail;
    private BooleanSupplier on;
    private Consumer<Boolean> setOn;
    private IntSupplier key;
    private IntConsumer setKey;
    private DoubleSupplier value;
    private DoubleConsumer setValue;
    private double min, max, step;
    private DoubleFunction<String> format;
    private Runnable action;
    String label;

    private Option(Kind kind, String id, String title, String detail) {
        this.kind = kind; this.id = id; this.title = title; this.detail = detail;
    }

    static Option toggle(String id, String title, String detail, BooleanSupplier on, Consumer<Boolean> set) {
        Option o = new Option(Kind.TOGGLE, id, title, detail);
        o.on = on; o.setOn = set;
        return o;
    }
    static Option key(String id, String title, String detail, IntSupplier key, IntConsumer set) {
        Option o = new Option(Kind.KEY, id, title, detail);
        o.key = key; o.setKey = set;
        return o;
    }
    static Option slider(String id, String title, String detail, double min, double max, double step,
                         DoubleFunction<String> format, DoubleSupplier value, DoubleConsumer set) {
        Option o = new Option(Kind.SLIDER, id, title, detail);
        o.min = min; o.max = max; o.step = step; o.format = format; o.value = value; o.setValue = set;
        return o;
    }
    static Option button(String title, String detail, String label, Runnable action) {
        Option o = new Option(Kind.BUTTON, null, title, detail);
        o.label = label; o.action = action;
        return o;
    }

    boolean on() { return on.getAsBoolean(); }
    void flip() { setOn.accept(!on.getAsBoolean()); }
    int key() { return key.getAsInt(); }
    void key(int code) { setKey.accept(code); }
    double value() { return value.getAsDouble(); }
    double fraction() { return (value() - min) / (max - min); }
    String formatted() { return format.apply(value()); }
    void fraction(double f) {
        double v = min + Math.max(0, Math.min(1, f)) * (max - min);
        v = Math.round((v - min) / step) * step + min;
        setValue.accept(Math.max(min, Math.min(max, v)));
    }
    void run() { action.run(); }

    // What the window compares to know whether a redraw is needed, and what is saved.
    String save() {
        return switch (kind) {
            case TOGGLE -> Boolean.toString(on());
            case KEY -> Integer.toString(key());
            case SLIDER -> Double.toString(value());
            case BUTTON -> "";
        };
    }
    void load(String text) {
        if (text == null) return;
        try {
            switch (kind) {
                case TOGGLE -> setOn.accept(Boolean.parseBoolean(text.trim()));
                case KEY -> setKey.accept(Integer.parseInt(text.trim()));
                case SLIDER -> fraction((Double.parseDouble(text.trim()) - min) / (max - min));
                case BUTTON -> { }
            }
        } catch (NumberFormatException ignored) { }
    }
}
