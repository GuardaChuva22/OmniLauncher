package omni.agent;

// Hold-to-zoom in the spirit of Zoomify: the FOV eases toward the zoom level, the
// mouse wheel changes the level while the key is held, and mouse sensitivity follows
// the zoom so aiming stays steady. Time-based, so it is the same at any frame rate and
// however many times the game asks for the FOV in one frame.
final class Zoom {
    static final double MIN_LEVEL = 1.5, MAX_LEVEL = 50, STEP = 1.25, SPEED = 11;
    static boolean enabled = true, smooth = true, scrollAdjust = true, hideHud = true, followSensitivity = true;
    static double startLevel = 4;
    static int key = -1;
    private static boolean held;
    private static double level = startLevel, current = 1;
    private static long last;

    private Zoom() {}

    // Returns true when the key event belongs to the zoom and must not reach the game.
    static boolean key(int code, int action, boolean playing) {
        if (!enabled || code != key) return false;
        if (action == 1) {
            if (!playing) return false;
            held = true;
            level = startLevel;
            return true;
        }
        if (action == 0) {
            boolean was = held;
            held = false;
            return was;
        }
        return held;
    }

    static boolean scroll(double amount, boolean playing) {
        if (!held || !playing || !scrollAdjust || amount == 0) return false;
        level = Math.max(MIN_LEVEL, Math.min(MAX_LEVEL, level * Math.pow(STEP, Math.signum(amount))));
        return true;
    }

    static void disable() { held = false; }

    static double fov(double fov) {
        update();
        return current > 1.0005 ? fov / current : fov;
    }

    // Hidden from the press until the zoom has nearly eased back out, as OptiFine and Zoomify do.
    static boolean hidesHud() { return enabled && hideHud && (held || current > 1.02); }

    static double sensitivity() { return followSensitivity && current > 1.0005 ? 1 / current : 1; }

    private static void update() {
        long now = System.nanoTime();
        double dt = last == 0 ? 0 : Math.min(0.25, (now - last) / 1e9);
        last = now;
        double target = held && enabled ? level : 1;
        if (!smooth) { current = target; return; }
        // Ease on a log scale so each step feels equally fast at 2x and at 30x.
        double from = Math.log(current), to = Math.log(target);
        double eased = to + (from - to) * Math.exp(-SPEED * dt);
        current = Math.abs(eased - to) < 1e-4 ? target : Math.exp(eased);
    }
}
