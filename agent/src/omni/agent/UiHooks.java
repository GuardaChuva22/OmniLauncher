package omni.agent;

import java.lang.instrument.*;
import java.security.ProtectionDomain;
import omni.agent.internal.asm.*;
import static omni.agent.internal.asm.Opcodes.*;

// A few narrowly targeted classes; no polling, native injection, or changes to game jars.
final class UiHooks implements ClassFileTransformer {
    static final String SCREEN = "net.minecraft.client.gui.screens.Screen";
    static final String KEYBOARD = "net.minecraft.client.KeyboardHandler";
    static final String OPTIONS = "net.minecraft.client.Options";
    static final String MOUSE = "net.minecraft.client.MouseHandler";
    static final String CAMERA = "net.minecraft.client.Camera";
    static final String RENDERER = "net.minecraft.client.renderer.GameRenderer";
    static final String HUD = "net.minecraft.client.gui.Hud";
    static final String GUI = "net.minecraft.client.gui.Gui";
    static final String CHAT = "net.minecraft.client.gui.components.ChatComponent";
    static final String LIST = "net.minecraft.client.gui.components.AbstractSelectionList";
    static final String SCROLL_AREA = "net.minecraft.client.gui.components.AbstractScrollArea";
    static final String CLIENT = "omni/bridge/Hooks";
    private final UiNames names;

    UiHooks(UiNames names) { this.names = names; }

    @Override public byte[] transform(ClassLoader loader, String className, Class<?> redefining,
                                      ProtectionDomain domain, byte[] bytes) {
        boolean screen = names.internal(SCREEN).equals(className);
        boolean keyboard = names.internal(KEYBOARD).equals(className);
        boolean options = names.internal(OPTIONS).equals(className);
        boolean mouse = names.internal(MOUSE).equals(className);
        boolean camera = names.internal(CAMERA).equals(className);
        boolean renderer = names.internal(RENDERER).equals(className);
        boolean hud = names.internal(HUD).equals(className);
        boolean gui = names.internal(GUI).equals(className);
        boolean chat = names.internal(CHAT).equals(className);
        boolean list = names.internal(LIST).equals(className);
        boolean area = names.internal(SCROLL_AREA).equals(className);
        if (!screen && !keyboard && !options && !mouse && !camera && !renderer && !hud && !gui && !chat && !list && !area) return null;
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
                    // Zoom: 26.x computes the FOV once per frame in Camera; 1.21.1 in GameRenderer.
                    boolean modernFov = camera && name.equals(names.methodName(CAMERA, "calculateFov", "float")) && desc.equals("(F)F");
                    boolean legacyFov = renderer && name.equals(names.methodName(RENDERER, "getFov", CAMERA, "float", "boolean"))
                            && desc.equals("(L" + names.internal(CAMERA) + ";FZ)D");
                    boolean turn = mouse && name.equals(names.methodName(MOUSE, "turnPlayer", "double")) && desc.equals("(D)V");
                    boolean scroll = mouse && name.equals(names.methodName(MOUSE, "onScroll", "long", "double", "double")) && desc.equals("(JDD)V");
                    // Zoom hides the HUD and hand through F1's own flag, read where they are drawn:
                    // 26.x keeps it in Hud (the hand follows the state Hud extracts); 1.21.1 in
                    // Options.hideGui, read by Gui's layers and by renderItemInHand. F1 is untouched.
                    boolean hudFlag = hud && name.equals("extractRenderState");
                    // Widgets draw after the game's HUD: Hud on 26.x, Gui.render on 1.21.1.
                    boolean hudEnd = hudFlag || gui && name.equals(names.methodName(GUI, "render", legacyGui, "net.minecraft.client.DeltaTracker"))
                            && desc.equals("(L" + names.internal(legacyGui) + ";L" + names.internal("net.minecraft.client.DeltaTracker") + ";)V");
                    boolean guiFlag = gui || renderer && name.equals(names.methodName(RENDERER, "renderItemInHand", CAMERA, "float", "org.joml.Matrix4f"))
                            && desc.equals("(L" + names.internal(CAMERA) + ";FLorg/joml/Matrix4f;)V");
                    // Chat animation: the chat's draw, wrapped in a translation, and new messages.
                    String font = "net.minecraft.client.gui.Font";
                    boolean chatDraw = chat && (name.equals("extractRenderState") && desc.startsWith("(L" + names.internal(modernGui) + ";L" + names.internal(font) + ";IIIL")
                            || name.equals(names.methodName(CHAT, "render", legacyGui, "int", "int", "int", "boolean"))
                            && desc.equals("(L" + names.internal(legacyGui) + ";IIIZ)V"));
                    boolean chatAdded = chat && (name.equals("addMessageToDisplayQueue") && desc.equals("(Lnet/minecraft/client/multiplayer/chat/GuiMessage;)V")
                            || name.equals(names.methodName(CHAT, "addMessageToDisplayQueue", "net.minecraft.client.GuiMessage"))
                            && desc.equals("(L" + names.internal("net.minecraft.client.GuiMessage") + ";)V"));
                    // Smooth scrolling: the wheel handler (AbstractScrollArea on 26.x, the list on 1.21.1)
                    // and each frame of a list.
                    boolean wheel = (area || list) && name.equals(names.methodName(area ? SCROLL_AREA : LIST, "mouseScrolled", "double", "double", "double", "double"))
                            && desc.equals("(DDDD)Z");
                    boolean listFrame = list && (name.equals("extractWidgetRenderState") && desc.equals("(L" + names.internal(modernGui) + ";IIF)V")
                            || name.equals(names.methodName(LIST, "renderWidget", legacyGui, "int", "int", "float"))
                            && desc.equals("(L" + names.internal(legacyGui) + ";IIF)V"));
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
                            if (turn) {
                                // Scale this frame's mouse movement before the game turns the player.
                                for (String delta : new String[]{"accumulatedDX", "accumulatedDY"}) {
                                    String field = names.fieldName(MOUSE, delta), owner = names.internal(MOUSE);
                                    visitVarInsn(ALOAD, 0); visitInsn(DUP);
                                    visitFieldInsn(GETFIELD, owner, field, "D");
                                    visitMethodInsn(INVOKESTATIC, CLIENT, "zoomSensitivity", "()D", false);
                                    visitInsn(DMUL);
                                    visitFieldInsn(PUTFIELD, owner, field, "D");
                                }
                            }
                            if (legacyFov) {
                                // The flag's slot is reused before the return: read it on entry.
                                visitVarInsn(ILOAD, 3);
                                visitMethodInsn(INVOKESTATIC, CLIENT, "zoomWorld", "(Z)V", false);
                            }
                            if (chatDraw) {
                                visitVarInsn(ALOAD, 0); visitVarInsn(ALOAD, 1);
                                visitMethodInsn(INVOKESTATIC, CLIENT, "chatBegin", "(Ljava/lang/Object;Ljava/lang/Object;)V", false);
                            }
                            if (chatAdded || wheel || listFrame) {
                                visitVarInsn(ALOAD, 0);
                                visitMethodInsn(INVOKESTATIC, CLIENT, chatAdded ? "chatAdded" : wheel ? "scrollBefore" : "scrollFrame",
                                        "(Ljava/lang/Object;)V", false);
                            }
                            if (scroll) {
                                visitVarInsn(DLOAD, 5);
                                visitMethodInsn(INVOKESTATIC, CLIENT, "zoomScroll", "(D)Z", false);
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
                        @Override public void visitFieldInsn(int opcode, String owner, String field, String descriptor) {
                            super.visitFieldInsn(opcode, owner, field, descriptor);
                            boolean flag = opcode == GETFIELD && descriptor.equals("Z") && (
                                    hudFlag && owner.equals(names.internal(HUD)) && field.equals("isHidden")
                                    || guiFlag && owner.equals(names.internal(OPTIONS)) && field.equals(names.fieldName(OPTIONS, "hideGui")));
                            if (flag) visitMethodInsn(INVOKESTATIC, CLIENT, "zoomHides", "(Z)Z", false);
                        }
                        @Override public void visitInsn(int opcode) {
                            if (radius && opcode == IRETURN)
                                visitMethodInsn(INVOKESTATIC, CLIENT, "radius", "(I)I", false);
                            if (modernFov && opcode == FRETURN)
                                visitMethodInsn(INVOKESTATIC, CLIENT, "zoomFov", "(F)F", false);
                            if (legacyFov && opcode == DRETURN)
                                visitMethodInsn(INVOKESTATIC, CLIENT, "zoomFovLegacy", "(D)D", false);
                            if (chatDraw && opcode == RETURN) {
                                visitVarInsn(ALOAD, 1);
                                visitMethodInsn(INVOKESTATIC, CLIENT, "chatEnd", "(Ljava/lang/Object;)V", false);
                            }
                            if (wheel && opcode == IRETURN) {
                                visitVarInsn(ALOAD, 0);
                                visitMethodInsn(INVOKESTATIC, CLIENT, "scrollAfter", "(Ljava/lang/Object;)V", false);
                            }
                            if (hudEnd && opcode == RETURN) {
                                visitVarInsn(ALOAD, 1);
                                visitMethodInsn(INVOKESTATIC, CLIENT, "hud", "(Ljava/lang/Object;)V", false);
                            }
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
