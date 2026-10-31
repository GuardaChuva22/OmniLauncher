# Omni in-game client

The launcher loads `omni-agent.jar` with `-javaagent`; no mod loader or mod installation
is needed. The map feed remains independent from the UI's error handling.

* **INS** (rebindable) opens/closes the Omni window. **ESC** leaves a feature's page,
  then closes; the close button closes too. Drag the window by its sidebar or header;
  its position is remembered.
* The window is drawn with Java2D at the window's real pixel density (Manrope, bundled
  under the OFL as `fonts/` in the jar) as two game textures: the frame (sidebar and
  header) and the page, which scrolls smoothly under a scissor without repainting. It
  fades and grows in like the launcher's dialogs; pages fade and slide when they change.
  If the texture path is unavailable, the same layout is drawn with the game's font.
* Features are `Module`s in `Catalog`: a card in a sidebar category with a switch, and a
  page of `Option`s (switch, key, slider, button) behind it. New features declare
  their options there; the window, hit testing and saving follow from that list.
* Chat animation: a message arriving at the bottom of the chat slides the chat up one
  line into place. Smooth scrolling: the game's lists (servers, worlds, options, packs,
  controls) ease toward each wheel step; scrollbar drags still move them at once. Both
  resolve their names apart from the rest, so a missing name turns off only them.
* Widgets (coordinates, FPS, clock) are glass pills drawn after the game's HUD (Hud on
  26.x, Gui.render on 1.21.1), hidden with F1 and while zoomed. **Editar HUD** opens
  the layout editor over the game: drag to move with snapping to edges and centre,
  scroll over a widget to resize, ESC or **Concluir** returns to the window.
* Blur applies behind screens (including inventory, pause, chat and menus); text,
  controls and the in-game HUD during normal play stay sharp. Disabling the switch
  also disables vanilla menu blur for this session, without changing `options.txt`.
  The panel always blurs what is behind it.
* Zoom (in the spirit of Zoomify): hold the key (default **C**) to zoom 4x; the mouse
  wheel changes the level while held (1.5x to 50x); mouse sensitivity follows the zoom.
  The transition is time-based. The held zoom key and wheel never reach the game.
  While zoomed the hotbar, HUD and hand are hidden as with F1 (OptiFine and Zoomify do
  the same); names, outlines and F1 itself are untouched.
* Preferences are saved per instance in `config/omni-client.properties`: every option
  by its id (`blur`, `zoom`, `zoomKey` and so on; keys are the game's own key codes),
  widget positions as fractions of the free screen space, and the window offset.
* Ordinary menus are restored on closing the panel. Opening it from an inventory
  closes the container normally, so its stale server-side instance is never restored.
* Key codes and the left mouse button come from the game's `InputConstants`: 26.3 reads
  input through SDL (left button 1, scancodes) while 26.2 and older use GLFW (left button 0).

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
every hook target (Screen, KeyboardHandler, Options, MouseHandler, Camera, GameRenderer,
ChatComponent, AbstractSelectionList, and Hud and AbstractScrollArea on 26.x or Gui on 1.21.1),
loads each patched class in an isolated loader so the JVM verifies the injected bytecode,
defines the generated Screen with its mouse-event overrides, resolves every reflective name
the client and its textures use, and renders every page and widget once, all without starting
Minecraft or OpenGL.

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
is rejected, that Knot sees the single bootstrap bridge, and that all twenty-six callbacks
from a game-loader fixture reach their implementation. Compile it with Fabric Loader,
ASM and Mixin in the classpath, plus `omni-bootstrap.jar` for `BridgeGameProbe.java`.
Run with `-javaagent:omni-agent.jar=port=9,token=test` and pass the fixture classes
directory to `omni.agent.test.FabricBridgeSmoke`.
