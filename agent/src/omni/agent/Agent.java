package omni.agent;

import java.lang.instrument.Instrumentation;

// The Omni agent: started by the launcher with
//   -javaagent:omni-agent.jar=port=<p>,token=<t>[,mappings=<file>]
// It never stops the game from opening: everything runs on one daemon thread and every
// failure ends up as a status line sent to the launcher.
public final class Agent {
    private Agent() {}

    public static void premain(String arguments, Instrumentation instrumentation) {
        try {
            int port = 0;
            String token = "";
            String mappings = "";
            String rest = arguments == null ? "" : arguments;
            // mappings= is last and is a path, which may hold commas: it takes the rest.
            final int at = rest.indexOf("mappings=");
            if (at >= 0) {
                mappings = rest.substring(at + "mappings=".length());
                rest = rest.substring(0, at);
            }
            for (String part : rest.split(",")) {
                final int equals = part.indexOf('=');
                if (equals < 0)
                    continue;
                final String key = part.substring(0, equals).trim();
                final String value = part.substring(equals + 1).trim();
                if (key.equals("port"))
                    port = Integer.parseInt(value);
                else if (key.equals("token"))
                    token = value;
            }
            if (port <= 0 || token.isEmpty())
                return;

            // Install before Minecraft loads its Screen/KeyboardHandler classes. The
            // optional UI has its own failure boundary and never stops the map feed.
            if (UiBridge.install(instrumentation))
                OmniClient.install(mappings, instrumentation);
            final Link link = new Link(port, token, mappings.isEmpty() ? "mojang" : "obfuscated");
            final Telemetry telemetry = new Telemetry(instrumentation, mappings, link);
            final Thread thread = new Thread(() -> {
                try {
                    telemetry.run();
                } catch (Throwable error) {
                    link.status("stopped: " + error);
                }
            }, "Omni agent");
            thread.setDaemon(true);
            thread.setPriority(Thread.MIN_PRIORITY);
            thread.start();
        } catch (Throwable ignored) {
            // The game opens as if the agent were not there.
        }
    }
}
