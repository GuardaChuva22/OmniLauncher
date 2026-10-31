# Omni in-game client

The launcher loads `omni-agent.jar` with `-javaagent`; no mod loader or mod installation
is needed. The map feed remains independent from the UI's error handling.

* **INS** opens/closes the Omni panel. **ESC** or the close button also closes it.
* The panel contains only the global background-blur switch. Click the row, or use
  **TAB** followed by **Space/Enter**, to toggle it.
* Blur applies behind screens (including inventory, pause, chat and menus); text,
  controls and the in-game HUD during normal play stay sharp. Disabling the switch
  also disables vanilla menu blur for this session, without changing `options.txt`.
* The switch is saved per instance in `config/omni-client.properties`.
* Ordinary menus are restored on closing the panel. Opening it from an inventory
  closes the container normally, so its stale server-side instance is never restored.

The initial API profiles are vanilla **1.21.1, 26.2 and 26.3** (and matching official-name
clients). Hooks and generated Screen bytecode were checked against those actual jars
with `-Xverify:all`; 26.3 also passed startup with the agent. Visual/gameplay acceptance
is performed by the user. Other versions/loaders are not yet validated. In particular,
1.20.1 lacks the native blur API and the UI disables itself instead of blocking startup.
The launcher's existing OmniLink eligibility/enablement rules still apply.

## Build

The normal launcher CMake build compiles and packages the agent beside the executable.
A JDK 17+ is required at build time; the game only needs its usual runtime.

The agent can also be built independently, without Qt or a C++ toolchain:

```powershell
cmake -S agent -B build-agent -G Ninja
cmake --build build-agent
```

ASM 9.9.1 is fetched at build time with a pinned SHA-256. Its packages are relocated to
`omni.agent.internal.asm` and embedded in the agent jar to avoid loader conflicts.
Its BSD license is included in `LICENSE-ASM.txt`. No Blur code or assets are bundled;
the implementation reuses Minecraft's own blur shader.

## Compatibility check

Compile `tests/UiSmoke.java` against the built agent, then run `omni.agent.UiSmoke`
with the agent, test classes, target client and its libraries in the classpath. Supply
the Mojang client mappings path for obfuscated vanilla versions. The check validates
the three hook targets and defines/verifies the generated native Screen, including
its mouse-event override, without starting Minecraft or OpenGL.

UI diagnostics are prefixed `[Omni UI]` in the game's output log.

## Mod-loader isolation

Minecraft code calls only `omni.bridge.Hooks`, a small bridge embedded as
`omni-bootstrap.jar` and registered with Java's bootstrap class loader before the
UI hooks are installed. The bridge delegates to the system-loaded implementation
through bound method handles. Fabric's Knot can therefore resolve the hooks without
exposing the implementation jar to the game or changing its classpath isolation.
UI bridge setup failures leave the game's classes untouched and the map feed active.

`tests/FabricBridgeSmoke.java` runs with the actual Fabric Loader 0.20.1 Knot loader
and an empty parent-classpath allowlist. It verifies that direct access to the agent
is rejected, that Knot sees the single bootstrap bridge, and that all ten callbacks
from a game-loader fixture reach their implementation. Compile it with Fabric Loader,
ASM and Mixin in the classpath, plus `omni-bootstrap.jar` for `BridgeGameProbe.java`.
Run with `-javaagent:omni-agent.jar=port=9,token=test` and pass the fixture classes
directory to `omni.agent.test.FabricBridgeSmoke`.
