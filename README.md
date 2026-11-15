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
- `"x"`, `"y"`, `"width"`, `"height"` on the control give the surface part of
  the display; by default it has all of it. The rest of the page works as
  usual. An RTT camera of your own under a partial surface inherits its
  scissor: set your own on it.
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

## Rules

- Everything runs on KARL's UI thread. Keep `frame` short.
- A crash in a plugin takes KARL down with it. Libraries are never unloaded.
- Plugins run with the app's rights. Install only what you trust.
- `tick`, `clock` and the Live transport events need KARL 0.1.78 or later
  (`KARL_SDK_VERSION` 1.1). An older KARL loads the plugin and never calls them.

## Examples

[`examples/pocket-seq/`](examples/pocket-seq/): a 16-step sequencer on part
of a page, the rest of the page staying normal KARL controls. Give it a 2x2
block of knobs with `x`, `y`, `width`, `height` (bottom left:
`75.25, 22, 376.5, 378`); it finds the four knobs under it. Top left picks the
step, top right its pitch (past B goes to the next octave), bottom left its
velocity, bottom right its octave; turning one of the last three turns the
step on and, while stopped, plays it. The steps are a 4x4 grid in the gap
between the knobs, coloured by pitch and lit by velocity, the playhead
flashing as it passes. Two big keys (`"keys": {"edge", "play", "step"}`):
PLAY / STOP, flashing on the beat, and the selected step on / off (ALT: a
random pentatonic pattern, hold: clear). `"midi": {"port", "channel"}`.

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
