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
        Hooks.hud(null);
        Hooks.chatBegin(null, null); Hooks.chatEnd(null); Hooks.chatAdded(null);
        Hooks.scrollBefore(null); Hooks.scrollAfter(null); Hooks.scrollFrame(null);
        if (!Hooks.panelRelease(null, 1, 2, 0) || !Hooks.panelDrag(null, 1, 2, 0) || !Hooks.panelScroll(null, 1, 2, 1))
            throw new AssertionError("Window callbacks");
        Hooks.zoomWorld(true);
        if (Hooks.zoomFov(80f) != 20f || Hooks.zoomFovLegacy(80) != 20 || Hooks.zoomSensitivity() != 0.25
                || !Hooks.zoomScroll(1) || !Hooks.zoomHides(false))
            throw new AssertionError("Zoom callbacks");
    }
}
