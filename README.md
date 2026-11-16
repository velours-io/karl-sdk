# KARL plugin SDK

Write C++ / OpenGL / OpenSceneGraph code that KARL loads at startup: a plugin
can draw anything on the whole display or part of it, and own any key or knob.

The contract is one header, [`include/karl/sdk.h`](include/karl/sdk.h). Read it
first. `include/karl/traces.h` is the flex PCB's copper under the display, for
designing around it. Using an AI coding assistant? Point it at
[`AGENTS.md`](AGENTS.md).

The SDK is for KARL only: see [LICENSE](LICENSE). Plugins you write are yours.

## How it works

- A plugin is a shared library exporting `karl_plugin()`, which returns a
  `karl::sdk::Plugin`: a name, a version, and the control types it adds.
- KARL loads every plugin installed on it at startup. Install them from the
  KARL Configurator (below); KARL updates keep them.
- `controls.json` uses it like a built-in plugin:
  ```json
  "plugins": ["fleet-game"],
  "pages": [{ "id": "fleet", "type": "Page", "controls": [{ "type": "FleetGame" }] }]
  ```
- Each control of a plugin's type is a **surface**. KARL calls `create` with a
  `Host` table, an `osg::Group` root and the control's JSON, then `frame` with
  every key and knob each frame while the page shows, then `destroy`.
  `tick`, if set, runs every frame whatever page shows (a clock, a fade).
- Tempo and transport: `clock` gets MIDI clock (incoming, or KARL's own
  Clock control): Start, Stop and the 24 ppqn count. Live's transport comes
  as the `live:transport` event (playing, tempo, song position) through
  `subscribe`, and `live:transport:set` starts or stops Live; see
  `LIVE_TRANSPORT` in `sdk.h`. Needs the `ableton` plugin active.
- `dataDir()` is a folder of the plugin's own that survives restarts and
  updates.
- Where a control goes: `"link"` (its top left knob) and `"span"`
  (`[columns, rows]` of knobs) give the surface those knobs' squares; without
  them it has the whole display (`"x"`, `"y"`, `"width"`, `"height"` in pixels
  also work). The rest of the page works as usual. An RTT camera of your own
  under a partial surface inherits its scissor: set your own on it.
- Its keys: `"keys": {"play": ["BUTTON_BOTTOM", 0], ...}`, one per role, and
  `karl::sdk::configKey(host, "play")` gives the key's index. A big key
  (`claimKey(..., 2)`) reports either of its slots on that index.
- A manifest (below) lets the KARL Configurator place all of this for the
  user: they pick the control and click where it goes.
- Keys and knobs are free: `claimKey` / `claimEncoder` take any of them at any
  time, and KARL's own control there goes quiet until `releaseKey` /
  `releaseEncoder` or the end of the surface. `Input` always carries all of
  them.
- Text: `makeText` for KARL's look, or `fontPath` and plain `osgText`.

Only plain data, `const char *` and osg types cross the boundary. The tables
are append-only, so a plugin keeps working across KARL releases until
`KARL_SDK_ABI` changes (new compiler or OSG on the OS image). KARL logs and
skips a library built for another ABI.

## Build

Plugins are built in a Docker image that holds the compiler and libraries KARL
itself uses, so a plugin always matches the KARL it runs on. It works the same
on macOS, Windows and Linux.

```bash
cd examples/pocket-seq
docker run --rm -v "$PWD":/src ghcr.io/velours-io/karl-sdk:abi1
# -> dist/pocket-seq.so
```

`karl-build clean` as the command starts from scratch, and `bash` gives a
shell in the image. Your own project is a `CMakeLists.txt` that includes
`$KARL_SDK/cmake/KarlPlugin.cmake` and calls
`karl_plugin(<name> <sources...>)`; copy one of the examples to start.

The image tag is the ABI: `abi1` builds plugins for every KARL with
`KARL_SDK_ABI` 1.

## Try it in the simulator

The same image runs KARL's app on your computer, with the plugin loaded, in a
browser tab:

```bash
docker run --rm -it --name karl-sim -p 6080:6080 -v "$PWD":/src ghcr.io/velours-io/karl-sdk:abi1 karl-sim
# -> http://localhost:6080/vnc.html?autoconnect=1&resize=scale
```

- Click keys, scroll or drag over knobs (up turns clockwise), hold shift for
  ALT.
- The flex PCB's copper is drawn in red and the knobs as outlined black discs:
  what the panel hides or crosses on a real KARL (Designing for the panel).
- The page comes from `sim.json` in your folder (a `controls.json`), or is
  made from the plugin's manifest: a knob block at the bottom left, its keys
  under it.
- Run the build command again in another terminal and the simulator starts
  again with the new library.
- From a script, or a coding assistant, while it runs:
  ```bash
  docker exec karl-sim karl-ctl click BUTTON_BOTTOM 0   # press, release, alt-click
  docker exec karl-sim karl-ctl turn 2 0 5              # row, column, steps (+ clockwise)
  docker exec karl-sim karl-ctl shot shot.png           # into your folder
  docker exec karl-sim karl-ctl log                     # KARL's log, plugin messages included
  ```

It renders in software, so it is slower than KARL and has no knob feel, LED
colours, MIDI ports, Live or configurator. Check a plugin on a real KARL before
you share it.

## Install

In the KARL Configurator, with KARL connected over USB: Plugins, On this KARL,
Install plugin, pick the `.so`. KARL checks it (a KARL plugin, the right ABI)
before it goes in, then Restart KARL loads it. Add the plugin's name to
`plugins` in your project and its control types to a page, as above.

The same screen lists what is installed, why a plugin was refused, and removes
plugins. A plugin keeps its `dataDir()` when it is removed.

If KARL stops within 10 seconds of starting with plugins on, the next start
skips them all and the configurator says so: remove the plugin at fault, then
restart. Its controls are left out of the pages until then; the rest loads.

## Manifest

`Plugin::manifest`, optional, is a JSON string that describes each control
type for the configurator. With one, a control is added from Add control and
placed on the grid, and its options get a form; without one, it is written by
hand in the JSON tab.

```json
{
  "label": "Pocket Seq",
  "description": "A 16 step sequencer on four knobs and two keys.",
  "category": "midi",
  "controls": [{
    "type": "PocketSeq",
    "name": "Pocket Seq",
    "description": "Step, pitch, velocity and octave on four knobs.",
    "footprint": {"knobs": [2, 2]},
    "keys": [{"role": "play", "label": "PLAY / STOP", "size": 2}],
    "reserved": [],
    "fields": [
      {"name": "bpm", "label": "Tempo", "type": "number", "default": 120, "min": 20, "max": 300, "group": "Clock"}
    ]
  }]
}
```

- `category`: `live`, `midi`, `mixer`, `show` or `tools`.
- `footprint`: `{"knobs": [columns, rows]}` for a block the user places, or
  `{"page": true}` for the whole page.
- `keys`: the roles the configurator assigns, near the block, as `"keys"` in
  the control. `size` 2 is a big key.
- `reserved`: slots the control always takes itself, as `[edge, slot]`, for a
  whole-page control that lays out keys of its own (see fleet-game).
- `fields`: options, `number`, `select` (with `options`), `toggle` or `text`,
  written into the control under their `name` with their `default`.

## Designing for the panel

- Knobs stand on the glass: a disc of 48 px radius around each knob centre
  (`Host::encoderCenter`) is hidden. Put nothing there that has to be read.
- The flex PCB's copper crosses the display as thin dark lines. Every segment
  is in `include/karl/traces.h`, and the simulator draws them in red. Going
  under them is fine when it is a choice: keep small text and fine detail off
  them, or use them, as fleet-game does for its sonar grid.

## Rules

- Everything runs on KARL's UI thread. Keep `frame` short.
- A crash in a plugin takes KARL down with it. Libraries are never unloaded.
- Plugins run with the app's rights. Install only what you trust.
- `tick`, `clock` and the Live transport events need KARL 0.1.78 or later
  (`KARL_SDK_VERSION` 1.1); `configKey`, the manifest and placing by knob need
  0.1.79 (1.2). An older KARL loads the plugin and ignores what it lacks.

## Examples

[`examples/pocket-seq/`](examples/pocket-seq/): a 16-step sequencer on part
of a page, the rest of the page staying normal KARL controls. Give it a 2x2
block of knobs (`"link"` and `"span": [2, 2]`, which the configurator writes
when you place it); it finds the four knobs under it. Top left picks the
step, top right its pitch (past B goes to the next octave), bottom left its
velocity, bottom right its octave; turning one of the last three turns the
step on and, while stopped, plays it. The steps are a 4x4 grid in the gap
between the knobs, coloured by pitch and lit by velocity, the playhead
flashing as it passes. Two big keys (`"keys": {"play": [...], "step": [...]}`):
PLAY / STOP, flashing on the beat, and the selected step on / off (ALT: a
random pentatonic pattern, hold: clear). `"midiPort"`, `"midiChannel"`.

`"sync"` picks what drives it, and ALT + PLAY cycles through them, shown
along the top:
- `internal` (default): its own tempo, `"bpm"`. Runs in `Surface::tick`, so
  it keeps playing on other pages.
- `midi`: MIDI clock, incoming or from a KARL `Clock` control. PLAY arms it
  (amber WAITING until a clock runs); Start plays from the top, Stop or half
  a second without ticks stops it. The tempo shown is measured off the ticks.
- `live`: Live's transport and tempo. PLAY starts and stops Live; a start
  lands in phase with Live's song position. For tight sync, send Live's MIDI
  clock and use `midi`.

The pattern, the selected step and the sync are saved to
`plugins/pocket-seq/<id>.json` a second after each change (`"id"`, default
`pocket-seq`), and win over the config at the next start.

[`examples/fleet-game/`](examples/fleet-game/): a naval tower defence over
the whole panel. Each encoder is a lighthouse on an island under the knob; its
absolute angle aims the beam, it fires on ships inside it, and ALT + turn aims
a shot by hand. `"mode": "sea"` (default, fleets from every side) or
`"canal"` (one canal across the middle). The right column is DAY / NIGHT,
FOG / CLEAR, SPEED, LIGHTS and auto fire; the bottom row is SEND WAVE plus the
wave, ships, lives and score.

Files: `Plugin.cpp` (the entry point), `FleetGame` (the surface: keys, knobs,
input), `Water` (GLSL 1.20 sea, with beams and fog drawn at 1/16 resolution
into a texture that lights the whole scene), `Battle` (ships, shots, waves,
banners), `Models` (procedural low-poly ships), `Particles` (wakes, smoke,
fire, splashes, health bars), `Tactical` (sonar plot whose sectors follow the
copper lines in `karl/traces.h`), `Label` (text on `Host::makeText`), `View.h`
(projection, lighting, render bins).
