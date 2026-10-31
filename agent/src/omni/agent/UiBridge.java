package omni.agent;

import java.io.InputStream;
import java.lang.instrument.Instrumentation;
import java.nio.file.*;
import java.util.jar.JarFile;

final class UiBridge {
    // Keep the JarFile alive while the VM can resolve bootstrap classes from it.
    private static JarFile bootstrap;
    private UiBridge() {}

    static boolean install(Instrumentation instrumentation) {
        try (InputStream source = Agent.class.getResourceAsStream("/omni-bootstrap.jar")) {
            if (source == null) throw new IllegalStateException("Missing embedded UI bridge");
            Path jar = Files.createTempFile("omni-ui-bootstrap-", ".jar");
            jar.toFile().deleteOnExit();
            Files.copy(source, jar, StandardCopyOption.REPLACE_EXISTING);
            bootstrap = new JarFile(jar.toFile());
            instrumentation.appendToBootstrapClassLoaderSearch(bootstrap);
            Class<?> bridge = Class.forName("omni.bridge.Hooks", true, null);
            bridge.getMethod("initialize", Class.class).invoke(null, OmniClient.class);
            return true;
        } catch (Throwable error) {
            System.err.println("[Omni UI] UI bridge unavailable: " + error);
            return false;
        }
    }
}
