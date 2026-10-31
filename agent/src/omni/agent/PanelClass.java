package omni.agent;

import java.lang.reflect.Method;
import omni.agent.internal.asm.*;
import static omni.agent.internal.asm.Opcodes.*;

// Generate a tiny real Screen subclass against the running game's API. No Java compiler
// is needed in the game, and the same agent handles official names and Mojang mappings.
final class PanelClass extends ClassLoader {
    private static final String NAME = "omni.agent.generated.OmniScreen";
    private PanelClass(ClassLoader parent) { super(parent); }

    static Object create(UiNames n, OmniClient.Bindings b) throws ReflectiveOperationException {
        byte[] bytes = bytes(n, b.modern);
        Class<?> type = new PanelClass(b.screenType.getClassLoader()).defineClass(NAME, bytes, 0, bytes.length);
        return type.getConstructor().newInstance();
    }

    static byte[] bytes(UiNames n, boolean modern) throws ReflectiveOperationException {
        String self = NAME.replace('.', '/'), parent = n.internal(UiHooks.SCREEN);
        String component = "net.minecraft.network.chat.Component";
        String gui = modern ? "net.minecraft.client.gui.GuiGraphicsExtractor" : "net.minecraft.client.gui.GuiGraphics";
        ClassWriter w = new ClassWriter(ClassWriter.COMPUTE_MAXS);
        w.visit(V17, ACC_PUBLIC | ACC_FINAL, self, null, parent, null);
        MethodVisitor m = w.visitMethod(ACC_PUBLIC, "<init>", "()V", null, null);
        m.visitCode(); m.visitVarInsn(ALOAD, 0); m.visitLdcInsn("Omni");
        Method literal = n.method(component, "literal", "java.lang.String");
        m.visitMethodInsn(INVOKESTATIC, n.internal(component), literal.getName(), Type.getMethodDescriptor(literal), true);
        m.visitMethodInsn(INVOKESPECIAL, parent, "<init>", "(L" + n.internal(component) + ";)V", false);
        m.visitInsn(RETURN); finish(m);

        m = w.visitMethod(ACC_PUBLIC, n.methodName(UiHooks.SCREEN, modern ? "extractRenderState" : "render", gui, "int", "int", "float"),
                "(L" + n.internal(gui) + ";IIF)V", null, null);
        m.visitCode();
        if (!modern) {
            m.visitVarInsn(ALOAD, 0); m.visitVarInsn(ALOAD, 1); m.visitVarInsn(ILOAD, 2); m.visitVarInsn(ILOAD, 3); m.visitVarInsn(FLOAD, 4);
            m.visitMethodInsn(INVOKESPECIAL, parent, n.methodName(UiHooks.SCREEN, "renderBackground", gui, "int", "int", "float"),
                    "(L" + n.internal(gui) + ";IIF)V", false);
        }
        m.visitVarInsn(ALOAD, 0); m.visitVarInsn(ALOAD, 1); m.visitVarInsn(ILOAD, 2); m.visitVarInsn(ILOAD, 3);
        m.visitMethodInsn(INVOKESTATIC, UiHooks.CLIENT, "draw", "(Ljava/lang/Object;Ljava/lang/Object;II)V", false);
        m.visitInsn(RETURN); finish(m);

        // No dark menu texture: the panel's own tint over the blurred world is the backdrop.
        m = w.visitMethod(ACC_PUBLIC, n.methodName(UiHooks.SCREEN, modern ? "extractMenuBackground" : "renderMenuBackground", gui),
                "(L" + n.internal(gui) + ";)V", null, null);
        m.visitCode(); m.visitInsn(RETURN); finish(m);

        String eventOwner = "net.minecraft.client.gui.components.events.GuiEventListener";
        String keyEvent = "net.minecraft.client.input.KeyEvent";
        String mouseEvent = "net.minecraft.client.input.MouseButtonEvent";
        m = w.visitMethod(ACC_PUBLIC, n.methodName(UiHooks.SCREEN, "keyPressed", modern ? new String[]{keyEvent} : new String[]{"int", "int", "int"}),
                modern ? "(L" + n.internal(keyEvent) + ";)Z" : "(III)Z", null, null);
        m.visitCode();
        if (modern) {
            m.visitVarInsn(ALOAD, 1);
            m.visitMethodInsn(INVOKEVIRTUAL, n.internal(keyEvent), "key", "()I", false);
        } else m.visitVarInsn(ILOAD, 1);
        m.visitMethodInsn(INVOKESTATIC, UiHooks.CLIENT, "panelKey", "(I)Z", false); m.visitInsn(IRETURN); finish(m);

        String[] mouseParams = modern ? new String[]{mouseEvent, "boolean"} : new String[]{"double", "double", "int"};
        m = w.visitMethod(ACC_PUBLIC, n.methodName(eventOwner, "mouseClicked", mouseParams),
                modern ? "(L" + n.internal(mouseEvent) + ";Z)Z" : "(DDI)Z", null, null);
        m.visitCode(); m.visitVarInsn(ALOAD, 0);
        if (modern) {
            for (String accessor : new String[]{"x", "y", "button"}) {
                m.visitVarInsn(ALOAD, 1);
                m.visitMethodInsn(INVOKEVIRTUAL, n.internal(mouseEvent), accessor, accessor.equals("button") ? "()I" : "()D", false);
            }
        } else { m.visitVarInsn(DLOAD, 1); m.visitVarInsn(DLOAD, 3); m.visitVarInsn(ILOAD, 5); }
        m.visitMethodInsn(INVOKESTATIC, UiHooks.CLIENT, "panelClick", "(Ljava/lang/Object;DDI)Z", false);
        m.visitInsn(IRETURN); finish(m);

        // Release and drag carry the same position and button; drag's deltas are not needed.
        String[] releaseParams = modern ? new String[]{mouseEvent} : new String[]{"double", "double", "int"};
        String[] dragParams = modern ? new String[]{mouseEvent, "double", "double"} : new String[]{"double", "double", "int", "double", "double"};
        mouse(n, w, modern, n.methodName(eventOwner, "mouseReleased", releaseParams),
                modern ? "(L" + n.internal(mouseEvent) + ";)Z" : "(DDI)Z", "panelRelease");
        mouse(n, w, modern, n.methodName(eventOwner, "mouseDragged", dragParams),
                modern ? "(L" + n.internal(mouseEvent) + ";DD)Z" : "(DDIDD)Z", "panelDrag");

        m = w.visitMethod(ACC_PUBLIC, n.methodName(eventOwner, "mouseScrolled", "double", "double", "double", "double"), "(DDDD)Z", null, null);
        m.visitCode(); m.visitVarInsn(ALOAD, 0); m.visitVarInsn(DLOAD, 1); m.visitVarInsn(DLOAD, 3); m.visitVarInsn(DLOAD, 7);
        m.visitMethodInsn(INVOKESTATIC, UiHooks.CLIENT, "panelScroll", "(Ljava/lang/Object;DDD)Z", false);
        m.visitInsn(IRETURN); finish(m);

        m = w.visitMethod(ACC_PUBLIC, n.methodName(UiHooks.SCREEN, "onClose"), "()V", null, null);
        m.visitCode(); m.visitMethodInsn(INVOKESTATIC, UiHooks.CLIENT, "close", "()V", false); m.visitInsn(RETURN); finish(m);
        w.visitEnd(); return w.toByteArray();
    }
    // A mouse override passing (screen, x, y, button) to the client, from a MouseButtonEvent
    // on 26.x or from the leading (double, double, int) on 1.21.1.
    private static void mouse(UiNames n, ClassWriter w, boolean modern, String name, String desc, String callback) {
        MethodVisitor m = w.visitMethod(ACC_PUBLIC, name, desc, null, null);
        m.visitCode(); m.visitVarInsn(ALOAD, 0);
        if (modern) {
            String event = n.internal("net.minecraft.client.input.MouseButtonEvent");
            for (String accessor : new String[]{"x", "y", "button"}) {
                m.visitVarInsn(ALOAD, 1);
                m.visitMethodInsn(INVOKEVIRTUAL, event, accessor, accessor.equals("button") ? "()I" : "()D", false);
            }
        } else { m.visitVarInsn(DLOAD, 1); m.visitVarInsn(DLOAD, 3); m.visitVarInsn(ILOAD, 5); }
        m.visitMethodInsn(INVOKESTATIC, UiHooks.CLIENT, callback, "(Ljava/lang/Object;DDI)Z", false);
        m.visitInsn(IRETURN); finish(m);
    }
    private static void finish(MethodVisitor method) { method.visitMaxs(0, 0); method.visitEnd(); }
}
