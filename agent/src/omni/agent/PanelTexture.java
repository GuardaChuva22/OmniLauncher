package omni.agent;

import java.lang.reflect.*;
import java.nio.IntBuffer;
import java.util.function.Supplier;

// One dynamic game texture holding an image painted by Art, drawn through the GUI with a
// transform. 26.x draws with a RenderPipeline and an ARGB tint; 1.21.1 draws with
// RenderSystem's shader color. Any missing piece throws, and the caller falls back.
final class PanelTexture {
    private static final String TEXTURES = "net.minecraft.client.renderer.texture.";
    private final boolean modern;
    private final Object minecraft, id, pipeline;
    private final Method manager, register, release, upload, pixels, blit, pose, push, pop, translate, scale, memInts;
    private final Method flush, enableBlend, blendFunc, disableBlend, shaderColor;
    private final Field pointer;
    private final Constructor<?> create;
    private Object texture;
    private int width, height;

    PanelTexture(UiNames n, OmniClient.Bindings b, String path) throws ReflectiveOperationException {
        modern = b.modern;
        String mc = OmniClient.Bindings.MC, gui = modern ? "net.minecraft.client.gui.GuiGraphicsExtractor" : "net.minecraft.client.gui.GuiGraphics";
        String identifier = modern ? "net.minecraft.resources.Identifier" : "net.minecraft.resources.ResourceLocation";
        String image = "com.mojang.blaze3d.platform.NativeImage";
        minecraft = b.minecraft;
        manager = n.method(mc, "getTextureManager");
        id = n.method(identifier, "fromNamespaceAndPath", "java.lang.String", "java.lang.String").invoke(null, "omni", path);
        register = n.method(TEXTURES + "TextureManager", "register", identifier, TEXTURES + "AbstractTexture");
        release = n.method(TEXTURES + "TextureManager", "release", identifier);
        upload = n.method(TEXTURES + "DynamicTexture", "upload");
        pixels = n.method(TEXTURES + "DynamicTexture", "getPixels");
        pointer = n.field(image, "pixels");
        Class<?> dynamic = n.type(TEXTURES + "DynamicTexture");
        create = modern ? dynamic.getConstructor(Supplier.class, int.class, int.class, boolean.class)
                : dynamic.getConstructor(int.class, int.class, boolean.class);
        memInts = Class.forName("org.lwjgl.system.MemoryUtil", true, dynamic.getClassLoader())
                .getMethod("memIntBuffer", long.class, int.class);
        pose = n.method(gui, "pose");
        Class<?> stack = pose.getReturnType();
        if (modern) {
            // RenderPipeline moved packages between 26.2 and 26.3: match blit by shape.
            Field textured = n.type("net.minecraft.client.renderer.RenderPipelines").getField("GUI_TEXTURED");
            pipeline = textured.get(null);
            Class<?> idType = n.type(identifier);
            Method found = null;
            for (Method m : n.type(gui).getMethods()) {
                Class<?>[] p = m.getParameterTypes();
                if (m.getName().equals("blit") && p.length == 11 && p[0] == textured.getType() && p[1] == idType
                        && p[4] == float.class && p[10] == int.class) found = m;
            }
            if (found == null) throw new NoSuchMethodException("blit(RenderPipeline, Identifier, ..., color)");
            blit = found;
            push = stack.getMethod("pushMatrix"); pop = stack.getMethod("popMatrix");
            translate = stack.getMethod("translate", float.class, float.class);
            scale = stack.getMethod("scale", float.class, float.class);
            flush = enableBlend = blendFunc = disableBlend = shaderColor = null;
        } else {
            pipeline = null;
            blit = n.method(gui, "blit", identifier, "int", "int", "float", "float", "int", "int", "int", "int");
            String poseStack = "com.mojang.blaze3d.vertex.PoseStack";
            push = n.method(poseStack, "pushPose"); pop = n.method(poseStack, "popPose");
            translate = n.method(poseStack, "translate", "float", "float", "float");
            scale = n.method(poseStack, "scale", "float", "float", "float");
            flush = n.method(gui, "flush");
            String rs = "com.mojang.blaze3d.systems.RenderSystem";
            enableBlend = n.method(rs, "enableBlend"); blendFunc = n.method(rs, "defaultBlendFunc");
            disableBlend = n.method(rs, "disableBlend");
            shaderColor = n.method(rs, "setShaderColor", "float", "float", "float", "float");
        }
    }

    int width() { return width; }
    int height() { return height; }

    // Copies an ARGB image into the texture, recreating it when the size changes.
    void upload(Art.Image image) throws ReflectiveOperationException {
        int[] argb = image.argb();
        int w = image.width(), h = image.height();
        if (texture == null || w != width || h != height) {
            Object textures = manager.invoke(minecraft);
            if (texture != null) release.invoke(textures, id);
            Supplier<String> label = () -> "omni_" + id;
            texture = modern ? create.newInstance(label, w, h, false) : create.newInstance(w, h, false);
            register.invoke(textures, id, texture);
            width = w; height = h;
        }
        long address = pointer.getLong(pixels.invoke(texture));
        IntBuffer target = (IntBuffer) memInts.invoke(null, address, w * h);
        int[] abgr = new int[argb.length];
        for (int i = 0; i < argb.length; i++) {
            int c = argb[i];
            abgr[i] = (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
        }
        target.put(abgr);
        upload.invoke(texture);
    }

    // Draws the texture with its top-left at (x, y) in GUI units; zoom maps texture pixels to GUI units.
    void draw(Object graphics, double x, double y, double zoom, double alpha) throws ReflectiveOperationException {
        if (texture == null) return;
        Object stack = pose.invoke(graphics);
        push.invoke(stack);
        try {
            if (modern) {
                translate.invoke(stack, (float) x, (float) y);
                scale.invoke(stack, (float) zoom, (float) zoom);
                int color = ((int) Math.round(Math.max(0, Math.min(1, alpha)) * 255) << 24) | 0xFFFFFF;
                blit.invoke(graphics, pipeline, id, 0, 0, 0f, 0f, width, height, width, height, color);
            } else {
                flush.invoke(graphics);
                translate.invoke(stack, (float) x, (float) y, 0f);
                scale.invoke(stack, (float) zoom, (float) zoom, 1f);
                enableBlend.invoke(null); blendFunc.invoke(null);
                shaderColor.invoke(null, 1f, 1f, 1f, (float) Math.max(0, Math.min(1, alpha)));
                try { blit.invoke(graphics, id, 0, 0, 0f, 0f, width, height, width, height); }
                finally { shaderColor.invoke(null, 1f, 1f, 1f, 1f); disableBlend.invoke(null); }
            }
        } finally { pop.invoke(stack); }
    }
}
