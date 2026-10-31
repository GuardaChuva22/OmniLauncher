package omni.agent;

import java.lang.instrument.*;
import java.security.ProtectionDomain;
import omni.agent.internal.asm.*;
import static omni.agent.internal.asm.Opcodes.*;

// Three narrowly targeted classes; no polling, native injection, or changes to game jars.
final class UiHooks implements ClassFileTransformer {
    static final String SCREEN = "net.minecraft.client.gui.screens.Screen";
    static final String KEYBOARD = "net.minecraft.client.KeyboardHandler";
    static final String OPTIONS = "net.minecraft.client.Options";
    static final String CLIENT = "omni/bridge/Hooks";
    private final UiNames names;

    UiHooks(UiNames names) { this.names = names; }

    @Override public byte[] transform(ClassLoader loader, String className, Class<?> redefining,
                                      ProtectionDomain domain, byte[] bytes) {
        boolean screen = names.internal(SCREEN).equals(className);
        boolean keyboard = names.internal(KEYBOARD).equals(className);
        boolean options = names.internal(OPTIONS).equals(className);
        if (!screen && !keyboard && !options) return null;
        try {
            ClassReader reader = new ClassReader(bytes);
            // Existing frames remain valid: insertions are stack-neutral, with one SAME frame
            // for each new entry guard. Avoid resolving game classes during their definition.
            ClassWriter writer = new ClassWriter(reader, ClassWriter.COMPUTE_MAXS);
            reader.accept(new ClassVisitor(ASM9, writer) {
                @Override public MethodVisitor visitMethod(int access, String name, String desc, String sig, String[] ex) {
                    MethodVisitor base = super.visitMethod(access, name, desc, sig, ex);
                    String legacyGui = "net.minecraft.client.gui.GuiGraphics";
                    String modernGui = "net.minecraft.client.gui.GuiGraphicsExtractor";
                    boolean modernWrapper = screen && name.equals("extractRenderStateWithTooltipAndSubtitles")
                            && desc.equals("(L" + names.internal(modernGui) + ";IIF)V");
                    boolean oldWrapper = screen && name.equals(names.methodName(SCREEN, "renderWithTooltip", legacyGui, "int", "int", "float"))
                            && desc.equals("(L" + names.internal(legacyGui) + ";IIF)V");
                    boolean nativeBlur = screen && (name.equals("extractBlurredBackground") && desc.equals("(L" + names.internal(modernGui) + ";)V")
                            || name.equals(names.methodName(SCREEN, "renderBlurredBackground", "float")) && desc.equals("(F)V"));
                    boolean panorama = screen && name.equals(names.methodName(SCREEN, "renderPanorama", legacyGui, "float"))
                            && desc.equals("(L" + names.internal(legacyGui) + ";F)V");
                    boolean radius = options && name.equals(names.methodName(OPTIONS, "getMenuBackgroundBlurriness")) && desc.equals("()I");
                    boolean oldKey = keyboard && name.equals(names.methodName(KEYBOARD, "keyPress", "long", "int", "int", "int", "int")) && desc.equals("(JIIII)V");
                    boolean newKey = keyboard && name.equals("keyPress") && desc.equals("(JILnet/minecraft/client/input/KeyEvent;)V");
                    return new MethodVisitor(ASM9, base) {
                        @Override public void visitCode() {
                            super.visitCode();
                            if (modernWrapper || oldWrapper) {
                                visitVarInsn(ALOAD, 0); visitVarInsn(ALOAD, 1); visitVarInsn(FLOAD, 4);
                                visitInsn(modernWrapper ? ICONST_1 : ICONST_0);
                                visitMethodInsn(INVOKESTATIC, CLIENT, "beginScreen", "(Ljava/lang/Object;Ljava/lang/Object;FZ)V", false);
                            }
                            if (nativeBlur) {
                                visitMethodInsn(INVOKESTATIC, CLIENT, "active", "()Z", false);
                                skipWhenTrue();
                            }
                            if (oldKey || newKey) {
                                if (newKey) {
                                    visitVarInsn(ALOAD, 4);
                                    visitMethodInsn(INVOKEVIRTUAL, "net/minecraft/client/input/KeyEvent", "key", "()I", false);
                                    visitVarInsn(ILOAD, 3);
                                } else { visitVarInsn(ILOAD, 3); visitVarInsn(ILOAD, 5); }
                                visitMethodInsn(INVOKESTATIC, CLIENT, "key", "(II)Z", false);
                                skipWhenTrue();
                            }
                        }
                        private void skipWhenTrue() {
                            Label original = new Label();
                            visitJumpInsn(IFEQ, original); visitInsn(RETURN);
                            visitLabel(original); visitFrame(F_SAME, 0, null, 0, null);
                        }
                        @Override public void visitMethodInsn(int opcode, String owner, String method, String descriptor, boolean itf) {
                            super.visitMethodInsn(opcode, owner, method, descriptor, itf);
                            if (modernWrapper && method.equals("extractBackground") && descriptor.endsWith(";IIF)V")) {
                                visitVarInsn(ALOAD, 1);
                                visitMethodInsn(INVOKESTATIC, CLIENT, "modernBlur", "(Ljava/lang/Object;)V", false);
                            }
                        }
                        @Override public void visitInsn(int opcode) {
                            if (radius && opcode == IRETURN)
                                visitMethodInsn(INVOKESTATIC, CLIENT, "radius", "(I)I", false);
                            if (panorama && opcode == RETURN)
                                visitMethodInsn(INVOKESTATIC, CLIENT, "panoramaBlur", "()V", false);
                            super.visitInsn(opcode);
                        }
                    };
                }
            }, 0);
            return writer.toByteArray();
        } catch (Throwable error) {
            System.err.println("[Omni UI] Unsupported hook " + className + ": " + error);
            return null;
        }
    }
}
