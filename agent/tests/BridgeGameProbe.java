package omni.fixture;

import omni.bridge.Hooks;

// Loaded by Knot, matching the caller side of the injected Minecraft bytecode.
public final class BridgeGameProbe {
    public static void run() {
        Hooks.beginScreen(null, null, 0.5f, true);
        Hooks.modernBlur(null);
        Hooks.panoramaBlur();
        if (!Hooks.active() || Hooks.radius(7) != 8 || !Hooks.key(260, 1))
            throw new AssertionError("Basic callbacks");
        Hooks.draw(null, null, 0, 0);
        if (!Hooks.panelKey(32) || !Hooks.panelClick(null, 1, 2, 0))
            throw new AssertionError("Panel callbacks");
        Hooks.close();
    }
}
