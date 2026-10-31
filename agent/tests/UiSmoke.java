package omni.agent;

import java.io.InputStream;
import omni.agent.internal.asm.*;

// Run against actual vanilla jars/libraries without initializing Minecraft or OpenGL.
public final class UiSmoke extends ClassLoader {
    private UiSmoke(ClassLoader parent) { super(parent); }
    public static void main(String[] args) throws Exception {
        UiNames names = new UiNames(args.length == 0 ? "" : args[0]);
        names.loader(UiSmoke.class.getClassLoader());
        boolean modern;
        try { names.type("net.minecraft.client.gui.GuiGraphicsExtractor"); modern = true; }
        catch (ClassNotFoundException e) { modern = false; }
        UiHooks hooks = new UiHooks(names);
        for (String target : new String[]{UiHooks.SCREEN, UiHooks.KEYBOARD, UiHooks.OPTIONS}) {
            try (InputStream in = UiSmoke.class.getClassLoader().getResourceAsStream(names.internal(target) + ".class")) {
                if (in == null) throw new AssertionError("Missing " + target);
                byte[] original = in.readAllBytes();
                byte[] transformed = hooks.transform(UiSmoke.class.getClassLoader(), names.internal(target), null, null, original);
                if (transformed == null) throw new AssertionError("No hooks: " + target);
                final int[] calls = {0};
                new ClassReader(transformed).accept(new ClassVisitor(Opcodes.ASM9) {
                    @Override public MethodVisitor visitMethod(int a, String n, String d, String s, String[] ex) {
                        return new MethodVisitor(Opcodes.ASM9) {
                            @Override public void visitMethodInsn(int opcode, String owner, String name, String desc, boolean itf) {
                                if (owner.equals(UiHooks.CLIENT)) calls[0]++;
                            }
                        };
                    }
                }, 0);
                if (calls[0] == 0) throw new AssertionError("No entry points in " + target);
            }
        }
        byte[] screen = PanelClass.bytes(names, modern);
        Class<?> generated = new UiSmoke(UiSmoke.class.getClassLoader()).defineClass(null, screen, 0, screen.length);
        generated.getDeclaredMethods();
        generated.getDeclaredConstructors();
        String event = "net.minecraft.client.gui.components.events.GuiEventListener";
        String[] params = modern ? new String[]{"net.minecraft.client.input.MouseButtonEvent", "boolean"}
                : new String[]{"double", "double", "int"};
        var mouse = names.method(event, "mouseClicked", params);
        if (generated.getMethod(mouse.getName(), mouse.getParameterTypes()).getDeclaringClass() != generated)
            throw new AssertionError("Mouse override does not match the game");
        System.out.println("OK: render, keyboard, blur hooks and verified native panel (" + (modern ? "extraction" : "legacy") + ")");
    }
}
