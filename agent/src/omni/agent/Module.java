package omni.agent;

import java.util.List;

// A feature as the window shows it: a card in its category with an on/off switch, and a
// page of options behind it. `toggle` may be null for features that are always on.
record Module(String id, String name, String detail, String category, Art.Glyph glyph, Option toggle, List<Option> options) {
    boolean on() { return toggle == null || toggle.on(); }
}
