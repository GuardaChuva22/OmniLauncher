package omni.agent;

import java.lang.reflect.*;
import java.util.Map;
import java.util.WeakHashMap;

// Two small motions for the game's own UI, resolved apart from the client so a missing
// name only turns these off:
// * Chat animation: when a message arrives at the bottom of the chat, the chat starts one
//   line lower and slides up into place.
// * Smooth scrolling: a wheel step on a list (servers, worlds, options, packs, controls)
//   moves a target; each frame the list eases toward it. Scrollbar drags and the game's
//   own scroll changes still apply at once.
final class Motion {
    static boolean chat = true, scroll = true;
    static double chatMillis = 200, scrollMillis = 240;

    private final boolean modern;
    private final Method pose, push, translate, pop, lineHeight, chatScale, getScroll, setScroll;
    private final Field chatScrollbar;
    private final Class<?> list;
    private long messageAt;
    private boolean pushed;

    private static final class State { double target, shown, lastSet; long last; }
    private final Map<Object, State> lists = new WeakHashMap<>();
    private Object scrolling;

    Motion(UiNames n, boolean modern) throws ReflectiveOperationException {
        this.modern = modern;
        String gui = modern ? "net.minecraft.client.gui.GuiGraphicsExtractor" : "net.minecraft.client.gui.GuiGraphics";
        pose = n.method(gui, "pose");
        Class<?> stack = pose.getReturnType();
        if (modern) {
            push = stack.getMethod("pushMatrix"); pop = stack.getMethod("popMatrix");
            translate = stack.getMethod("translate", float.class, float.class);
        } else {
            String poseStack = "com.mojang.blaze3d.vertex.PoseStack";
            push = n.method(poseStack, "pushPose"); pop = n.method(poseStack, "popPose");
            translate = n.method(poseStack, "translate", "float", "float", "float");
        }
        lineHeight = n.method(UiHooks.CHAT, "getLineHeight");
        chatScale = n.method(UiHooks.CHAT, "getScale");
        chatScrollbar = n.field(UiHooks.CHAT, "chatScrollbarPos");
        list = n.type(UiHooks.LIST);
        if (modern) {
            getScroll = n.method(UiHooks.SCROLL_AREA, "scrollAmount");
            setScroll = n.method(UiHooks.SCROLL_AREA, "setScrollAmount", "double");
        } else {
            getScroll = n.method(UiHooks.LIST, "getScrollAmount");
            setScroll = n.method(UiHooks.LIST, "setScrollAmount", "double");
        }
    }

    // ---- Chat ----

    void messageAdded(Object chatComponent) throws ReflectiveOperationException {
        // Only when the chat shows its newest lines; scrolled back, the view does not move.
        if (chat && chatScrollbar.getInt(chatComponent) == 0) messageAt = System.nanoTime();
    }

    void chatBegin(Object chatComponent, Object graphics) throws ReflectiveOperationException {
        pushed = false;
        if (!chat || messageAt == 0) return;
        double t = (System.nanoTime() - messageAt) / 1e6 / chatMillis;
        if (t >= 1) { messageAt = 0; return; }
        double eased = 1 - Math.pow(1 - t, 3);
        double offset = (int) lineHeight.invoke(chatComponent) * (double) chatScale.invoke(chatComponent) * (1 - eased);
        Object stack = pose.invoke(graphics);
        push.invoke(stack);
        pushed = true;
        if (modern) translate.invoke(stack, 0f, (float) offset);
        else translate.invoke(stack, 0f, (float) offset, 0f);
    }

    void chatEnd(Object graphics) throws ReflectiveOperationException {
        if (!pushed) return;
        pushed = false;
        pop.invoke(pose.invoke(graphics));
    }

    // ---- Lists ----

    // Around the game's wheel handler: it scrolls from the target, then the shown position
    // is put back and eased toward the new target frame by frame.
    void scrollBefore(Object area) throws ReflectiveOperationException {
        scrolling = null;
        if (!scroll || !list.isInstance(area)) return;
        State s = lists.computeIfAbsent(area, k -> new State());
        double now = get(area);
        if (s.last == 0 || Math.abs(now - s.lastSet) > 0.5) s.target = now;
        s.shown = now;
        setScroll.invoke(area, s.target);
        scrolling = area;
    }

    void scrollAfter(Object area) throws ReflectiveOperationException {
        if (scrolling != area) return;
        scrolling = null;
        State s = lists.get(area);
        s.target = get(area);
        setScroll.invoke(area, s.shown);
        s.lastSet = get(area);
        if (s.last == 0) s.last = System.nanoTime();
    }

    void frame(Object area) throws ReflectiveOperationException {
        State s = lists.get(area);
        if (s == null || s.last == 0) return;
        long now = System.nanoTime();
        double dt = Math.min(0.1, (now - s.last) / 1e9);
        s.last = now;
        double at = get(area);
        // Moved by something else (scrollbar drag, keyboard, a resize): follow it.
        if (Math.abs(at - s.lastSet) > 0.5) { s.target = at; s.lastSet = at; return; }
        if (at == s.target) return;
        double next = !scroll ? s.target : s.target + (at - s.target) * Math.exp(-dt * 4.6 / (scrollMillis / 1000));
        if (Math.abs(next - s.target) < 0.3) next = s.target;
        setScroll.invoke(area, next);
        s.lastSet = get(area);
        // The game clamps; a target past the end would never be reached.
        if (next == s.target || s.lastSet != next) s.target = s.lastSet;
    }

    private double get(Object area) throws ReflectiveOperationException { return (double) getScroll.invoke(area); }
}
