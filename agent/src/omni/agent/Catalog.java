package omni.agent;

import java.io.*;
import java.nio.file.*;
import java.util.*;

// Every feature of the client, its options and where they are saved. Adding a feature is
// adding a Module here; the window builds its cards and pages from this list. Preferences
// live per instance in config/omni-client.properties, written atomically.
final class Catalog {
    static final String VISUAL = "Visual", CAMERA = "Câmera", CHAT = "Chat", WIDGETS = "Widgets";

    // Client preferences outside any one feature.
    boolean blur = true;
    double blurStrength = 7;
    int panelKey;
    double windowX, windowY;

    final Widgets widgets = new Widgets();
    final List<Module> modules = new ArrayList<>();
    final List<Option> settings = new ArrayList<>();
    final Option editLayout;
    private final List<Option> saved = new ArrayList<>();

    Catalog(int insertKey, int zoomKey, Runnable editHud) {
        panelKey = insertKey;
        Zoom.key = zoomKey;
        add(new Module("blur", "Desfoque", "Fundo suave atrás de menus, inventários e do chat.", VISUAL, Art.Glyph.BLUR,
                Option.toggle("blur", "Desfoque das interfaces", "Desfoca o jogo atrás de qualquer tela.", () -> blur, v -> blur = v),
                List.of(Option.slider("blur.strength", "Intensidade", "Quanto o fundo é desfocado.", 1, 10, 1,
                        v -> Integer.toString((int) Math.round(v)), () -> blurStrength, v -> blurStrength = v))));
        add(new Module("scroll", "Rolagem suave", "Listas de servidores, mundos, opções e pacotes rolam suavemente.", VISUAL, Art.Glyph.SCROLL,
                Option.toggle("scroll", "Rolagem suave", "Anima a rolagem das listas do jogo.", () -> Motion.scroll, v -> Motion.scroll = v),
                List.of(Option.slider("scroll.duration", "Duração", "Quanto tempo a lista leva para chegar.", 100, 600, 20,
                        v -> Math.round(v) + " ms", () -> Motion.scrollMillis, v -> Motion.scrollMillis = v))));
        add(new Module("chat", "Chat animado", "Mensagens novas sobem deslizando em vez de aparecer de uma vez.", CHAT, Art.Glyph.CHAT,
                Option.toggle("chat.animation", "Chat animado", "Desliza o chat quando chega uma mensagem.", () -> Motion.chat, v -> Motion.chat = v),
                List.of(Option.slider("chat.duration", "Duração", "Quanto tempo a mensagem leva para subir.", 80, 400, 10,
                        v -> Math.round(v) + " ms", () -> Motion.chatMillis, v -> Motion.chatMillis = v))));
        add(new Module("zoom", "Zoom", "Segure uma tecla para aproximar a visão, como no OptiFine.", CAMERA, Art.Glyph.LENS,
                Option.toggle("zoom", "Zoom", "Aproxima enquanto a tecla está pressionada.", () -> Zoom.enabled, v -> {
                    Zoom.enabled = v;
                    if (!v) Zoom.disable();
                }),
                List.of(Option.key("zoomKey", "Tecla do zoom", "Clique para trocar. ESC cancela.", () -> Zoom.key, v -> Zoom.key = v),
                        Option.slider("zoom.level", "Aproximação inicial", "Quanto aproxima ao pressionar a tecla.", 2, 10, 0.5,
                                v -> (v == Math.rint(v) ? Integer.toString((int) v) : Double.toString(v)).replace('.', ',') + "×",
                                () -> Zoom.startLevel, v -> Zoom.startLevel = v),
                        Option.toggle("zoom.scroll", "Ajustar com a roda do mouse", "Role enquanto segura para aproximar mais ou menos.",
                                () -> Zoom.scrollAdjust, v -> Zoom.scrollAdjust = v),
                        Option.toggle("zoom.smooth", "Transição suave", "Anima a entrada e a saída do zoom.",
                                () -> Zoom.smooth, v -> Zoom.smooth = v),
                        Option.toggle("zoom.hideHud", "Esconder HUD e mão", "Tira a hotbar e a mão da frente durante o zoom.",
                                () -> Zoom.hideHud, v -> Zoom.hideHud = v),
                        Option.toggle("zoom.sensitivity", "Sensibilidade proporcional", "O mouse fica mais lento quanto maior o zoom.",
                                () -> Zoom.followSensitivity, v -> Zoom.followSensitivity = v))));
        for (Widgets.Widget w : widgets.all) {
            List<Option> options = new ArrayList<>(w.options());
            options.add(Option.slider("widget." + w.id + ".scale", "Tamanho", "Também dá para rolar o mouse sobre ele no editor.",
                    0.5, 2, 0.05, v -> Math.round(v * 100) + "%", () -> w.scale, v -> w.scale = v));
            add(new Module("widget." + w.id, w.name, w.detail, WIDGETS, w.glyph,
                    Option.toggle("widget." + w.id, w.name, w.detail, () -> w.enabled, v -> w.enabled = v), options));
        }
        editLayout = Option.button("Layout do HUD", "Arraste os widgets para onde quiser na tela.", "Editar layout", editHud);

        Option key = Option.key("panelKey", "Tecla do painel", "Abre e fecha este painel. ESC cancela.", () -> panelKey, v -> panelKey = v);
        settings.add(key);
        saved.add(key);
        settings.add(Option.button("Posição do painel", "Volta o painel para o centro da tela.", "Centralizar", () -> { windowX = 0; windowY = 0; }));
        settings.add(Option.button("Widgets", "Volta todos os widgets para o lugar e tamanho originais.", "Restaurar",
                () -> widgets.all.forEach(Widgets.Widget::reset)));
    }

    private void add(Module module) {
        modules.add(module);
        if (module.toggle() != null) saved.add(module.toggle());
        for (Option o : module.options()) if (o.id != null) saved.add(o);
    }

    List<Module> category(String name) {
        if (name == null) return modules;
        return modules.stream().filter(m -> m.category().equals(name)).toList();
    }

    // Everything that may need a redraw or a save, in one comparable value.
    List<String> values() {
        List<String> out = new ArrayList<>();
        for (Option o : saved) out.add(o.save());
        return out;
    }

    void load(Path file) throws IOException {
        if (!Files.isRegularFile(file)) return;
        Properties values = new Properties();
        try (Reader reader = Files.newBufferedReader(file)) { values.load(reader); }
        for (Option o : saved) o.load(values.getProperty(o.id));
        for (Widgets.Widget w : widgets.all) {
            double[] at = pair(values.getProperty("widget." + w.id + ".at"));
            if (at != null) { w.x = clamp(at[0]); w.y = clamp(at[1]); }
        }
        double[] at = pair(values.getProperty("window.at"));
        if (at != null) { windowX = at[0]; windowY = at[1]; }
    }

    void save(Path file) throws IOException {
        StringBuilder out = new StringBuilder("# Omni in-game client\n");
        for (Option o : saved) out.append(o.id).append('=').append(o.save()).append('\n');
        for (Widgets.Widget w : widgets.all) out.append("widget.").append(w.id).append(".at=").append(w.x).append(',').append(w.y).append('\n');
        out.append("window.at=").append(windowX).append(',').append(windowY).append('\n');
        Files.createDirectories(file.getParent());
        Path temp = Files.createTempFile(file.getParent(), ".omni-client-", ".tmp");
        try {
            Files.writeString(temp, out);
            try { Files.move(temp, file, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE); }
            catch (AtomicMoveNotSupportedException e) { Files.move(temp, file, StandardCopyOption.REPLACE_EXISTING); }
        } finally { Files.deleteIfExists(temp); }
    }

    private static double[] pair(String text) {
        if (text == null) return null;
        String[] parts = text.split(",");
        if (parts.length != 2) return null;
        try { return new double[]{Double.parseDouble(parts[0].trim()), Double.parseDouble(parts[1].trim())}; }
        catch (NumberFormatException e) { return null; }
    }
    static double clamp(double v) { return Math.max(0, Math.min(1, v)); }
}
