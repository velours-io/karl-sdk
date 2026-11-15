# Writing a KARL plugin (for coding assistants)

KARL is a MIDI controller: a 1280x800 display under 24 knobs (6 columns x 4
rows, absolute 14-bit angle sensors, no detents) and 44 keys around it, on a
Raspberry Pi CM4/CM5 (aarch64, Debian 12). A plugin is a C++17 shared library
that KARL loads at startup and draws with OpenSceneGraph (OpenGL, GLSL 1.20).

## Read first
1. `include/karl/sdk.h`: the whole interface. Nothing else of KARL is
   reachable; there is no KARL library to link and no other header.
2. `examples/pocket-seq/`: a plugin on part of a page (4 knobs, 2 keys,
   MIDI out, clock sync, saved state). The closest to most plugins.
3. `examples/fleet-game/`: a full-page plugin with shaders, particles and an
   offscreen pass.

## Rules that matter
- Only POD, `const char *` and `osg::*` cross the boundary. Never pass STL
  types to or from KARL. Inside the plugin, use anything.
- Every `Host` function takes the `Host` pointer first:
  `host->setLabel(host, key, "PLAY")`.
- Key and knob indices come from `host->keyIndex(host, edge, slot)` and
  `host->encoderIndex(host, row, col)` (row 0 = top). Never hard-code them.
- Draw only under the `root` you are given, in its own coordinates
  (0..host->width, 0..host->height, y up). Knob centres:
  `host->encoderCenter`. Design nothing under a knob's 48 px radius: the knob
  covers it.
- Claim the knobs and keys you use (`claimEncoder`, `claimKey`); read them all
  from `Input` every frame. Knobs give absolute angles (0..16383, -1 = none
  yet): turn deltas into steps yourself, wrap at half a turn, accumulate so
  jitter cancels (see `Knob::steps` in pocket-seq). Clockwise lowers the angle.
- `frame` runs only while the page shows; `tick` always. Both on KARL's UI
  thread: never block, no threads touching osg.
- `dataDir()` for files; write to a temporary file and rename.
- Big keys (`claimKey(host, key, 2)`) start on even slots.
- Text: `host->makeText` for KARL's look, held in an `osg::ref_ptr`.
- Export `karl_plugin()` with `KARL_PLUGIN_EXPORT`, return a static
  `karl::sdk::Plugin` with `size = sizeof(...)` and `abi = KARL_SDK_ABI`.
  Zero-initialise tables you fill field by field.

## Build and check
`docker run --rm -v "$PWD":/src ghcr.io/velours-io/karl-sdk:abi1` in the
project folder builds `dist/<name>.so`. Compile errors show there. There is no
simulator: install it on a KARL from the KARL Configurator (README, Install)
and try it.
