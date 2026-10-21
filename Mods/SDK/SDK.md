# V LAB Lite Mod SDK

Six independent ways to extend the app without touching its source:
**DLL audio mods** (native audio processors), **theme mods** (JSON colour
palettes), **song mods** (JSON arpeggiator sequences), **visualizer mods**
(a second, separate native DLL type that draws instead of processing
audio), **App mods** (a third, separate native DLL type that adds
buttons, QOL toggles, hotkeys, and even fully custom-drawn panels straight
into vlab's own interface, up to and including taking over its entire main
view), and **translation mods** (JSON button-text overrides, the simplest
mod type of all). This doc covers all six in depth, plus the background
image/GIF feature they're often used alongside.

---

## 1. DLL mods

### 1.1 Where they run

`AudioEngine::process()` runs a fixed pipeline: noise gate -> soundboard/TTS
mix-in -> gain -> EQ -> compressor -> pitch -> distortion/bitcrush/tremolo
-> stutter/ring-mod -> chorus/phaser/flanger/auto-wah (budget-gated) ->
delay (budget-gated) -> loud-mic limiter -> **your mods run here** -> MIDI
mix -> experimental FX block -> soundboard send -> pan/HeadSpace -> reverb
-> master gain -> post-limiter -> clip. Mods see the fully-processed voice
signal, not the raw mic input, and whatever they output continues down the
rest of the chain (MIDI mix, HeadSpace, reverb, etc. all still apply after
your mod).

If you need to run before some other stage, that's not currently
configurable, mods are one fixed insertion point, not a per-stage rack
slot. That's a real limitation, not an oversight: a full modular routing
system is a much bigger feature than "load a DLL."

### 1.2 The ABI

Six `extern "C"` functions, declared in `Source/Mods/SDK/VLabModAPI.h`:

```c
void vlab_mod_get_info(VLabModInfo* outInfo);
bool vlab_mod_init(double sampleRate, int maxBlockSize);
void vlab_mod_process(float* interleaved, int numFrames, int numChannels);
void vlab_mod_set_param(int index, float value);
float vlab_mod_get_param(int index);
void vlab_mod_shutdown();
```

`get_info`, `init`, `process`, and `shutdown` are **required**, `ModHost`
refuses to load a library missing any of these four. `set_param`/
`get_param` are checked for existence but not required to do anything
useful; leave the bodies empty if your mod has no runtime-adjustable
parameters.

`VLabModInfo` is a plain struct with fixed-size `char` arrays for name/
author/version plus an `sdkVersion` int that must equal
`VLAB_MOD_SDK_VERSION` (currently `1`) or the mod is rejected at load time.
This is the whole compatibility story right now: version numbers must
match exactly. There's no forward/backward compatibility negotiation, if
the SDK version bumps in a future rebuild, mods built against v1 will need
rebuilding, not just re-linking.

### 1.3 Real-time constraints

`vlab_mod_process` is called from the **audio thread**. The usual real-time
rules apply and this host does not protect you from breaking them:

- No `malloc`/`new`/`free`/`delete` (including hidden ones, `std::vector`
  resizing, `std::string` construction beyond SSO, etc.)
- No locks, mutexes, or anything that can block
- No file I/O, no logging to disk, no blocking syscalls
- No exceptions crossing the `extern "C"` boundary (undefined behaviour)

Do all allocation in `vlab_mod_init`, size everything for the
`maxBlockSize` you're given, and keep `process` itself allocation-free.

### 1.4 Buffer format

`interleaved` is `numFrames * numChannels` floats, channel-interleaved
(`L0 R0 L1 R1 L2 R2 ...`). `numChannels` is always `2` in this host, mono
isn't a case you need to handle. Process in place; whatever's in the
buffer when `process` returns is what continues down the chain.

### 1.5 Full example: a parameterized gain mod

The bundled `examples/example_gain_mod.cpp` is deliberately minimal (fixed
0.5x gain, to keep the first read-through short). Here's a second pattern
showing `set_param`/`get_param` in actual use, a mod with a
host-controllable gain:

```c
#include "VLabModAPI.h"
#include <cstring>
#include <atomic>

static std::atomic<float> gGain { 1.0f }; // set_param/get_param read+write this;
                                            // atomic because the GUI thread that
                                            // calls set_param and the audio thread
                                            // that calls process() are different threads

extern "C" VLAB_MOD_EXPORT void vlab_mod_get_info(VLabModInfo* info) {
    std::strncpy(info->name, "Adjustable Gain", sizeof(info->name));
    std::strncpy(info->author, "Your Name", sizeof(info->author));
    std::strncpy(info->version, "1.0", sizeof(info->version));
    info->sdkVersion = VLAB_MOD_SDK_VERSION;
}

extern "C" VLAB_MOD_EXPORT bool vlab_mod_init(double, int) { return true; }

extern "C" VLAB_MOD_EXPORT void vlab_mod_process(float* interleaved, int numFrames, int numChannels) {
    const float g = gGain.load();
    for (int i = 0; i < numFrames * numChannels; ++i) interleaved[i] *= g;
}

// index 0 = gain, 0.0-2.0
extern "C" VLAB_MOD_EXPORT void vlab_mod_set_param(int index, float value) {
    if (index == 0) gGain.store(value);
}
extern "C" VLAB_MOD_EXPORT float vlab_mod_get_param(int index) {
    return index == 0 ? gGain.load() : 0.0f;
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_shutdown() {}
```

Note this app's current Mods tab doesn't have per-mod parameter sliders
yet (see the caveats list on the tab itself), `set_param`/`get_param`
exist in the ABI now so that UI can be added later without breaking
already-built mods, but nothing calls them today. A mod using them will
just run with whatever default its own code sets internally until that
UI exists.

### 1.6 Parameters get automatic FX Rack sliders

If a mod exports the two optional metadata functions
(`vlab_mod_get_param_count`, `vlab_mod_get_param_info` - see the header
comment for the exact signatures and a worked example), the host builds a
real section in the **FX Rack** tab for it automatically: "Mod: `<name>`"
with an Enabled toggle and one slider per parameter, labelled and ranged
exactly as you described them. Moving a slider calls straight through to
your `vlab_mod_set_param(index, value)`. This was verified for real, the
same way the rest of this SDK doc's claims were: built a param-metadata
mod, loaded it with a standalone `dlopen` test mimicking exactly what
`ModHost` does (init → read param count → read each param's info → seed
defaults via `set_param` → move a slider → confirm `process` reflects the
new value), confirmed correct output.

Mods that don't export these two functions still load and work exactly as
before - they just get an Enabled toggle on the Mods tab and no sliders in
the FX Rack. This is fully additive; nothing about existing mods breaks.

### 1.7 Building

```bash
# Linux
g++ -shared -fPIC -I path/to/SDK -o mymod.so mymod.cpp

# macOS
clang++ -shared -fPIC -I path/to/SDK -o mymod.dylib mymod.cpp

# Windows (MSVC Developer Command Prompt)
cl /LD /I path\to\SDK mymod.cpp
```

Drop the result in `mods/dll` next to the app, click "Rescan Mods Folder."

### 1.8 Trust model, read this part

A DLL mod is native code with the same process privileges as the app
itself: file access, network access, anything. This SDK's narrow six-
function surface means a *well-behaved* mod only touches audio, but
nothing here prevents a *malicious* one from doing whatever it wants. This
is the same trust model as VST/AU plugins, browser extensions, or any
other native plugin format, only load mods from sources you'd trust with
that level of access. This host does not sandbox, does not scan, and does
not attempt to restrict mod code in any way.

---

## 2. Theme mods

Simpler and much safer: pure data (JSON), no code execution. A theme file
is a flat set of hex colours, plus an optional cursor:

```json
{
  "name": "My Theme",
  "bg": "#0a0a0a",
  "panel": "#121212",
  "panelAlt": "#181818",
  "stroke": "#2a2a2a",
  "accent": "#2ee66b",
  "accentDim": "#1d8a45",
  "accentGlow": "#552ee66b",
  "text": "#e8f5ec",
  "textDim": "#8fa398",
  "danger": "#e6543f"
}
```

Colours accept `#RRGGBB` (treated as fully opaque) or `#AARRGGBB` if you
want a specific colour to be semi-transparent, `accentGlow` is the one
that's normally meant to be translucent. Any key you omit falls back to
the current value, so a one-line theme that only overrides `accent` is
completely valid (see `examples/themes/minimal_override.theme.json`).

Name the file `*.theme.json` and drop it in `mods/themes`. It appears in
the Theme dropdown under Settings -> General alongside the built-in
palettes (Default Green, Ocean Blue, Amethyst, Crimson, Monochrome, Sunset
Orange, Cyberpunk Magenta, Forest, Gold, Teal, Matrix, Daylight, Bubblegum,
Domino's; check the dropdown for the exact current list). Selecting a
theme applies immediately and repaints every tab.

Two bundled colour-only examples: `examples/themes/amber_terminal.theme.json`
(full override) and `examples/themes/minimal_override.theme.json` (partial,
3 keys). See sections 2.1 and 2.2 below for hover/role colours and cursor
customization, and section 2.3 for layout/style, which moved to a separate,
non-moddable system called UI Presets.

One real bug this doc exists partly to warn you away from repeating:
`#RRGGBB` (6 hex digits, no alpha) fed straight into JUCE's colour parser
comes back **fully transparent**, not opaque, JUCE's hex parser just
left-shifts whatever digits it finds, so 6 digits land in the low bits and
alpha stays zero. The loader in `ThemeManager.h` normalizes this (pads a
bare 6-digit value with `ff` alpha automatically) so you don't need to
think about it when writing a theme file, but it's worth knowing if you
ever call `juce::Colour::fromString` directly elsewhere.

### 2.1 Hover & role colours

Ten more optional colour keys give a theme control over specific
interactive states and roles, instead of the app deriving them
automatically. Every one defaults to fully transparent, meaning "not
overridden by this theme", so omitting all ten (as every built-in theme
and the original examples do) looks identical to before:

```json
{
  "accent2": "#2ee66b",
  "textHover": "#7dffb0",
  "buttonHover": "#20301f",
  "sliderTrack": "#181818",
  "sliderThumb": "#2ee66b",
  "scrollbarThumb": "#1d8a45",
  "tooltipBg": "#181818",
  "tooltipText": "#e8f5ec",
  "selectionBg": "#552ee66b",
  "linkColour": "#2ee66b",
  "focusRing": "#2ee66b"
}
```

- **`accent2`**, a second accent colour. Left equal to `accent` (the
  default), nothing changes. Set it to something different and primary
  buttons, active toggles, and slider fills/thumbs switch from the normal
  single-colour-plus-shading look to a genuine two-tone gradient between
  `accent` and `accent2`, at the angle set by the current UI Preset (see
  2.3).
- **`textHover`**, the colour of button/menu text while the mouse is over
  it. Unset, hover doesn't change text colour at all (the pre-1.7.1
  behaviour).
- **`buttonHover`**, the fill colour of a secondary (non-primary,
  non-toggled, non-danger) button while hovered, in place of the app's
  own "brighten the panel colour a bit" default.
- **`sliderTrack`**, the background track colour behind a slider's fill.
- **`sliderThumb`**, the slider handle's base colour, independent of the
  track fill (only visible when `accent2` isn't also overriding it with a
  gradient).
- **`scrollbarThumb`**, JUCE's built-in scrollbar thumb colour.
- **`tooltipBg`** / **`tooltipText`**, background and text colour of
  hover tooltips app-wide.
- **`selectionBg`**, the highlight colour behind selected text in text
  editors.
- **`linkColour`**, colour of hyperlink-style text/buttons.
- **`focusRing`**, the outline colour drawn around a focused text field,
  in place of the default (`accent`).

### 2.2 Cursor

A theme can also swap the mouse cursor, app-wide, wherever a component
hasn't set its own cursor for a specific reason (a couple of spots, like
the crosshair in Modder View, always keep their own regardless):

```json
{
  "cursorStyle": "custom",
  "cursorImage": "my_cursor.png",
  "cursorHotspotX": 0.1,
  "cursorHotspotY": 0.05,
  "cursorSize": 32
}
```

- **`cursorStyle`**: `"default"` (the OS arrow, the default), `"hand"`,
  `"crosshair"`, `"ibeam"`, `"none"` (hides the cursor entirely), or
  `"custom"`.
- **`cursorImage`**, only used when `cursorStyle` is `"custom"`. A PNG
  path, relative to the theme's own file (or an absolute path). The Theme
  Creator (2.4) copies whatever image you pick into `mods/themes`
  alongside the saved `.theme.json`, so a theme built there is a portable,
  shareable folder rather than pointing at a path that only exists on
  your machine - do the same if you're hand-authoring a theme meant to be
  shared.
- **`cursorHotspotX`** / **`cursorHotspotY`** (0-1), where in the image
  the actual click-point is, as a fraction of its width/height. `0, 0` is
  the top-left corner (the default), `0.5, 0.5` is dead centre, which is
  usually what you want for a crosshair-style custom cursor.
- **`cursorSize`**, only used when `cursorStyle` is `"custom"`. The
  on-screen size in pixels (of the longest edge) a custom cursor image is
  scaled to, aspect ratio preserved, defaulting to 32. A source PNG at any
  resolution (tiny pixel art, a huge export) ends up the same predictable
  on-screen size instead of showing at its native resolution.

### 2.3 UI Presets: layout/style, separate from colour

Corner radius, glow, gradients, glassmorphism, scanlines and the rest of
what used to be "advanced" theme fields are **not** part of a theme mod
any more, and can't be loaded from `mods/themes`. They're now **UI
Presets**: a fixed, built-in list (Default, Blueprint, Holotech, Vapor
Deck, Obsidian, Arcade Cab, Paper Console) picked from its own dropdown in
Settings -> General, right next to Theme, independently of whichever
colour theme is active. Pick any theme with any preset, "CRT Terminal"
the theme and "Vapor Deck" the preset works exactly as well as pairing
each with its own namesake.

The reasoning: colour and "how rounded/glowy/glass/textured everything
is" are genuinely orthogonal choices, and coupling them into one theme
file meant every colour palette either had to duplicate a style, or pick
one arbitrarily. Splitting them means eleven colour themes and six style
presets combine into sixty-six looks instead of eleven.

Because they're not mod-loadable, UI Presets aren't documented here as a
JSON schema, see `UI/UiPresetManager.h` in source if you want to see or
add to the built-in list, that's a source change, not a mod.

### 2.4 Building themes without writing JSON by hand

The app also has a built-in **Theme Creator** tab: every colour (core and
the ten hover/role colours from 2.1) gets a swatch picker and a hex box
(type or paste a code directly), the cursor section from 2.2 gets a style
dropdown, an image picker, and hotspot sliders, everything previews live
across the whole app as you adjust it, and "Save Theme" writes the exact
same `*.theme.json` format documented above into `mods/themes` - so a
theme built entirely by hand in the UI is indistinguishable from one
written by hand as JSON, and either one is a normal theme mod afterward.

---

## 3. Song mods

Same spirit as theme mods, pure data (JSON), no code execution, and the
simplest of the four to write. A song is a fixed sequence of notes played
by the arpeggiator in the Synth tab.

### 3.1 File format

The whole file *is* one song (unlike theme mods there's no wrapper object
with extra top-level keys, just `loop` and `steps`):

```json
{
  "loop": true,
  "steps": [
    { "note": "C4", "dur": 0.25, "vel": 0.9, "rest": false },
    { "note": "E4", "dur": 0.25, "vel": 0.9, "rest": false },
    { "note": "G4", "dur": 0.25, "vel": 0.9, "rest": false },
    { "note": "C5", "dur": 0.25, "vel": 1.0, "rest": false },
    { "note": "C4", "dur": 0.125, "vel": 0.0, "rest": true }
  ]
}
```

Each step in `steps`:

- **`note`**, scientific pitch notation (`C4` = middle C). Sharps use
  `#` (`C#4`), flats use `b` (`Eb4`). Octave numbers follow the usual
  MIDI-ish convention where `C4` is middle C.
- **`dur`**, how long this step holds, in beats (not seconds, actual
  duration depends on the Synth tab's tempo). `0.25` is a sixteenth note
  at a 4/4-beats-per-quarter-note reading, `1.0` a quarter note, etc.
  Exact scaling is whatever `SongSequencer`/`RealtimeSynth` currently use
  for a beat, treat these as relative note-length ratios rather than an
  absolute time unit if you're porting a song from somewhere else.
- **`vel`**, velocity/loudness, `0.0`-`1.0`.
- **`rest`**, `true` means this step is silence for `dur` beats rather
  than a note; when `true`, `note` and `vel` are ignored (conventionally
  left at a placeholder like the example's last step) but must still be
  present since the parser doesn't treat any key as optional here.

**`loop`**, `true` repeats the whole `steps` sequence forever once
started (until Stop is pressed or another song is picked); `false` plays
through once and stops on the last step.

### 3.2 Where it lives

Name the file anything ending in `.json` and drop it in `mods/songs`. The
file name (minus `.json`) is what shows up as the song's display name in
the Synth tab's song dropdown, alongside the built-in songs (Major Triad
Up, Minor Triad Up, Octave Bounce, Pentatonic Up, Blues Scale, Major
Scale, Phrygian Mode, Chromatic Run) and anything saved from in-app song
recording to `arp_songs.json`. All three sources (built-ins, `arp_songs.json`,
`mods/songs/*.json`) are merged into one list at startup by `loadSongs()`
in `Synth/Song.h`, a mod song with the same name as a built-in or a
saved song overrides it, later sources winning in that same order.

A bundled example: `examples/songs/example_arpeggio.json`.

### 3.3 Trust model

Fully safe, same reasoning as theme mods, it's plain JSON, there's no
code execution path, a malformed file just fails to parse (and is
skipped) rather than doing anything harmful.

---

## 4. Visualizer mods

A second, separate native DLL mod type from the audio mods in section 1
above, different ABI, different folder, different SDK version counter,
but built the same way and sharing the same trust model (section 1.8
applies here too: native code, same process privileges as the app, only
load ones you trust).

Where audio mods process samples on the audio thread, a visualizer mod
draws directly into a pixel buffer on the UI thread, once per frame
(roughly 20-30Hz, not audio-rate), the whole ABI, a worked example, and
the exact real-time-ish constraints are documented in
`Source/Mods/SDK/VLabVisualizerAPI.h` (the five required exports are
`vlab_viz_get_info`, `vlab_viz_init`, `vlab_viz_render`, `vlab_viz_resize`,
`vlab_viz_shutdown`, plus one optional sixth, `vlab_viz_render_ex`). The
short version:

- `vlab_viz_render(left, right, numSamples, rgba, width, height)` gets the
  most recent block of audio samples the engine processed (both channels,
  raw floats) and an RGBA pixel buffer to fill, 4 bytes per pixel
  (R, G, B, A), top-left origin, `width * height * 4` bytes total. The
  buffer isn't cleared between calls, so fill every pixel you want drawn,
  alpha included. Export this one even if you also export render_ex
  below, it's the fallback for any vlab build that doesn't know about
  render_ex yet.
- `vlab_viz_render_ex(frame, rgba, width, height)`, OPTIONAL. If your DLL
  exports this, it's called instead of `vlab_viz_render` (same pixel-
  buffer contract). `frame` is a `VLabVizFrame*` bundling everything
  `vlab_viz_render` gets plus:
  - a precomputed frequency spectrum (`fftMagnitudes`, `numFftBins`
    linear-magnitude bins from the left channel), so a bar/spectrum-style
    visualizer doesn't need to run its own FFT,
  - `sampleRate`, for turning a bin index into Hz
    (`bin * sampleRate / (2 * numFftBins)`),
  - `deltaSeconds`, time since the previous render call, for animation
    speed that doesn't depend on the host's tick rate,
  - `accentR/accentG/accentB`, the app's current theme accent colour, so
    a mod can match whatever theme the user has picked instead of
    hardcoding its own palette.
  See the struct itself and the second worked example in
  `VLabVisualizerAPI.h` for the exact field list.
- `vlab_viz_resize(width, height)` fires whenever the display area
  changes size; update any internally-sized buffers there.
- Same allocation/blocking cautions as audio mods (section 1.3) apply,
  just with a looser deadline since this isn't the audio thread.
- Version checking is lenient on purpose: a host accepts any
  `sdkVersion >= 1`, so a mod built for an older SDK still loads on a
  newer vlab (it just won't have render_ex or whatever else came after
  it), and a mod built for a newer SDK than a given host knows about
  still gets a chance rather than being flatly rejected.

Build the same way as an audio mod (section 1.7), drop the result into
`mods/visualizers`, then pick it from the Visualizer tab's "Visualizer"
dropdown alongside the built-in visualizers (waveform, spectrum,
goniometer, virtual head). A bundled example exercising the whole ABI,
a signal-reactive scrolling colour field, is
`examples/example_plasma_viz.cpp`.

---

## 5. App mods

A third, separate native DLL mod type from both the audio mods (section 1)
and the visualizer mods (section 4), different ABI again, different
folder, different SDK version counter, same trust model (native code,
same process, same privileges as vlab, only load ones you trust).

The key difference from the other two: an App Mod doesn't hook into any of
vlab's own audio or rendering pipelines. It hooks into vlab's own
**interface**. Instead of opening a separate window and running an
independent program alongside vlab (which is what the GUI mod type this
section used to describe did), an App Mod adds real, embedded pieces to
the app you're already looking at:

- a clickable **button** on the mod's own card in the Mods tab;
- a QOL **toggle** switch, same place;
- a global **hotkey**;
- a fully custom-drawn **canvas**, a raw pixel buffer the mod repaints
  every frame, either sitting inline as a widget on its card, or, if the
  mod asks for it, taking over **vlab's entire main content area** as its
  own tab. That's the "override the whole GUI" option: for as long as that
  tab is selected, every pixel and every mouse/keyboard event in the main
  view belongs to the mod, draw literally anything, with any technique
  you want, since from vlab's side it's just bytes. vlab keeps its own top
  bar and sidebar, so the user can always click back to a normal tab.

### 5.1 The contract

Full details, including exact function pointer types, are in
`Source/Mods/SDK/VLabAppModAPI.h`. The required exports:

- **`vlab_appmod_get_info(VLabAppModInfo* outInfo)`**, fill in a name/
  author/version/description/SDK-version struct. Called once, right after
  the library loads.
- **`vlab_appmod_init(const VLabAppHostAPI* host)`**, called once, after
  get_info. `host` stays valid for the rest of the mod's life, store the
  pointer. This is also the **only** point at which `registerButton`,
  `registerToggle`, `registerHotkey`, and `registerCanvas` (section 5.2)
  work, that's when vlab builds the mod's card. Return `false` to abort
  loading.
- **`vlab_appmod_tick()`**, called roughly once per frame (~20Hz) on
  vlab's own UI thread. Most mods that are just a button/toggle/hotkey
  don't need this (an empty function is fine), it's for anything that
  wants to poll something periodically without its own thread.
- **`vlab_appmod_on_action(int id, bool toggleState)`**, fires when a
  registered button is clicked, a registered toggle is flipped
  (`toggleState` = the new state), or a registered hotkey is pressed.
  `id` matches whatever was passed to the matching `register*` call.
- **`vlab_appmod_shutdown()`**, called once, when vlab quits (or the mod
  is unloaded). Don't call back into `VLabAppHostAPI` from inside this
  function.

If the mod calls `registerCanvas`, it also needs to export
`vlab_appmod_canvas_paint`, `vlab_appmod_canvas_resized`,
`vlab_appmod_canvas_mouse`, and `vlab_appmod_canvas_key`, see the header
for their exact shapes; a mod with no canvas can skip all four.

### 5.2 What `VLabAppHostAPI` gives you

- `getAppVersion` / `getActiveTabName`, read-only info.
- `checkForUpdates()`, re-runs vlab's own `version.json` update check on
  demand (same one that runs at startup, see the top-level README for
  how that works).
- `switchTab(tabName)`, jump vlab to a tab. Tab names are the plain enum
  names: `"Board"`, `"FxRack"`, `"Settings"`, `"Visualizer"`, `"Mods"`,
  `"Synth"`, `"ThemeCreator"`, `"Tts"`.
- `showMessage(text)`, puts up a brief notice from vlab itself.
- `quitApp()`.
- `runCommand(name, argJson, outResult, outResultLen)`, the escape
  hatch. Current command ids:
  - `"get_now_playing"` -> JSON `{ "name", "elapsedSeconds", "totalSeconds", "loop" }`,
    or `{}` if nothing's playing on the soundboard.
  - `"list_boards"` -> JSON array of board names.
  - `"switch_board"` with `argJson` = a board name (plain string, not
    JSON-wrapped) -> switches to it, returns `"true"`/`"false"`.
  - `"list_soundboard_slots"` -> JSON array of `{ "index", "name",
    "hasHotkey" }` for every filled slot on the *current* board. Empty
    slots are skipped, `index` is what `"play_soundboard_slot"` expects.
  - `"play_soundboard_slot"` with `argJson` = a slot index (plain
    number, not JSON-wrapped) -> plays it using the user's current
    Board-tab playback defaults (volume/speed/loop/reverse), returns
    `"true"`/`"false"`.
  - `"list_glitch_effects"` -> JSON array of effect name strings (see
    below). Doesn't include Ring Mod, which has no on/off flag of its
    own, just a mix amount.
  - `"get_glitch_effect_enabled"` with `argJson` = an effect name (plain
    string) -> `"true"`/`"false"`.
  - `"set_glitch_effect_enabled"` with `argJson` = JSON `{ "name",
    "enabled" }` -> `"true"`/`"false"` (false if the name wasn't
    recognized).
  - `"set_glitch_effect_mix"` with `argJson` = JSON `{ "name", "mix" }`
    (`mix` 0-1) -> `"true"`/`"false"`.
    Effect names for the three commands above: `"stutter"`,
    `"escalating_repeat"`, `"accelerating_repeat"`, `"decelerate_repeat"`,
    `"pitch_down_repeat"`, `"scatter"`, `"reverse_bursts"`,
    `"glitch_gate"`, `"buffer_freeze"`, `"vinyl_stop"`, `"broken_delay"`,
    `"pitch_jumps"`, `"backmask"`, `"micro_freeze"`, `"buffer_shuffle"`.
  - `"get_monitor_volume"` -> a number as a string (0-1.5, 1.0 = unity).
  - `"set_monitor_volume"` with `argJson` = a number (plain, not
    JSON-wrapped) -> `"true"`, clamped to 0-1.5.
  - `"hold_synth_tone"` with `argJson` = JSON `{ "freq", "on" }` -> holds
    (or releases) a sustained tone on the Synth tab's patch, the same
    thing the "Hold Tone" toggle there does, `freq` in Hz (20-5000),
    ignored when `on` is false. Returns `"true"`.
  - `"reset_synth_patch"` -> puts every Synth tab param back to its
    default and stops whatever's currently playing (arp/song/held
    tone). Returns `"true"`.

  New commands get added here over time without breaking the ABI for App
  Mods already built against an older vlab, an unrecognized command name
  just returns `false` (outResult untouched).
- `registerButton(id, label, tooltip)` / `registerToggle(id, label,
  tooltip, initialState)` / `registerHotkey(id, keyDescription, label)`,
  init-only, see 5.1. `setToggleState(id, state)` works any time (init,
  tick, or on_action) and updates a toggle's displayed state from inside
  the mod itself.
- `registerCanvas(id, title, preferredW, preferredH, takeOverMainView)`,
  also init-only. `takeOverMainView = false` embeds the canvas as a widget
  on the mod's own card, sized `preferredW x preferredH`; `true` instead
  adds a whole new top-bar tab (labelled `title`) that, when selected,
  hides vlab's entire normal UI and shows nothing but this canvas, sized
  to whatever the main content area currently is. Only one canvas per mod
  may set this `true`.
- `registerSlider(id, label, tooltip, minValue, maxValue, stepSize,
  initialValue)`, init-only, same rules as the three above. Adds a
  numeric slider to the mod's card alongside its buttons/toggles.
  `stepSize` of `0` means continuous (the host picks a sane fine-grained
  step internally). Dragging it fires `vlab_appmod_on_slider(id, value)`
 , export that function if you call this. Capped at
  `VLAB_APPMOD_MAX_SLIDERS` (16) registrations per mod.
  `setSliderValue(id, value)` works any time afterward and updates a
  registered slider's displayed value from inside the mod, the same way
  `setToggleState` does for toggles, a no-op if `id` wasn't registered
  as a slider.
- `getModDataString(key, outBuf, outBufLen)` / `setModDataString(key,
  value)`, a small persistent key/value string store, one per mod
  (keyed by the mod's own filename, so two mods can't collide), saved to
  disk automatically by the host. `getModDataString` returns `false`
  (buffer left untouched) if `key` was never set; values round-trip as
  opaque UTF-8, so store your own JSON as the value if you need more than
  one field per key. This means a mod that just wants to remember, say, a
  last-used setting between launches doesn't need to write its own config
  file or parser.
- `isAdvancedMode()`, `true` if the user has Advanced Mode switched on
  in Settings. Useful for a mod that wants to hide riskier or power-user
  actions (anything destructive, or anything only relevant alongside
  vlab's own advanced-only features like the Visualizer/Theme Creator
  tabs) from casual users, simply by not registering them at all in
  `vlab_appmod_init()` when this comes back `false`. Since registration
  only happens once, at init, a mod that wants this to react live if the
  user flips Advanced Mode mid-session needs to poll it in `tick()` and
  adjust its own behaviour there (e.g. gate what a button's `on_action`
  actually does), rather than expecting new buttons to appear, the
  registration APIs themselves stay init-only.
- `reloadAllMods()`, re-runs vlab's own "Reload All Mods" pipeline on
  demand: the same one bound to the Reload button in Settings/Mods.
  Reloads audio mods, visualizer mods, theme mods, language mods,
  **and every App Mod, including this one**. Expect `vlab_appmod_shutdown()` to be called
  on you shortly after this returns, followed by a fresh
  `vlab_appmod_get_info`/`init` if your DLL is still on disk at that
  point. As of v1.7.1 the DLL-loading part of this runs on a background
  thread (paced with a cooldown between files, so it doesn't spike CPU
  scanning a big mods folder all at once) and this call itself returns
  essentially immediately, don't assume the reload has actually
  finished by the time `reloadAllMods()` returns to you.

### 5.3 Threading and real-time rules

Everything in this SDK, `vlab_appmod_init`, `vlab_appmod_tick`,
`vlab_appmod_on_action`, `vlab_appmod_on_slider`,
`vlab_appmod_canvas_paint/resized/mouse/key`, and every call *into*
`VLabAppHostAPI` from any of them, runs on vlab's own UI (message)
thread. That has two practical consequences:

- **You're not competing with vlab's audio thread**, unlike an audio mod
  (section 1.3), there's no hard real-time deadline here in the audio
  sense, a slow `tick()` won't cause a click/pop in playback.
- **You are still blocking the whole UI** if you're slow. `tick()` runs
  ~20Hz; `canvas_paint()` runs ~20Hz while a canvas is visible. Anything
  that takes more than a few milliseconds in either, a blocking file
  read, a network call, heavy computation, will make vlab itself feel
  laggy or unresponsive for as long as it takes, exactly the same
  "loading screen freeze" class of bug this changelog's v1.7.1 fixes were
  about, just triggered by a mod instead of vlab's own mod-scanning code.
  If you need to do something slow, do it on your own background thread
  inside the mod and just poll the result from `tick()`.

### 5.4 Where it lives

Build as a shared library (`.dll`/`.dylib`/`.so`) exporting the functions
above, drop it in `mods/appmods`, restart vlab, or click "Reload All
Mods" (Settings -> General, or the Mods tab) instead of restarting; as of
v1.7.1 that button (and the `reloadAllMods()` host-API call from 5.2) both
correctly rescan App Mods along with everything else, previously the
host-API call was documented but not actually wired up, so calling it did
nothing.

Two bundled examples exercise the whole ABI, in
`examples/appmods/`:

- **`example_qol_mod.cpp`**, the lightweight path: one button that jumps
  to the Board tab, one toggle that shows a message when flipped on, and
  an F9 hotkey that does the same thing. No canvas at all.
- **`example_canvas_mod.cpp`**, the "override the whole GUI" path: an
  animated starfield drawn from raw RGBA bytes with no toolkit involved,
  registered with `takeOverMainView = true`, so it gets its own
  "Starfield" tab in vlab's top bar. Click to add stars, Space to reset;
  every other vlab tab keeps working normally the moment you click away.

Because an App Mod is ordinary native code like the other two mod types,
nothing stops it from doing more than the APIs above, e.g. opening its
own OS window directly, the way the old GUI mod type (SLASHGUI and others)
used to work as its whole reason for existing. The button/toggle/hotkey/
canvas/slider APIs are the supported, documented way to integrate cleanly
into vlab's own interface instead of running alongside it.

---

## 6. Translation Mods

The simplest mod type in vlab: pure JSON, no code, no DLL, nothing to
compile or trust any more than you'd trust any other data file. A
translation mod (or "language mod") renames vlab's buttons, that's the
entire feature.

```json
{
  "name": "Pirate",
  "strings": {
    "boardTab": "Th' Board",
    "settingsTab": "Cap'n's Orders",
    "importButton": "Plunder"
  }
}
```

- `name` is what shows up in the Language dropdown (Settings -> General,
  right under the Theme dropdown).
- `strings` is a flat object, `key: "translated text"`. Every key is
  optional, a mod that only wants to rename three buttons just lists
  those three, everything else quietly falls back to the English default
  for that key. There's no penalty for translating a handful of buttons
  instead of all of them, that's the expected, normal way to write one
  of these.
- The full list of recognised keys is `Source/UI/TranslationManager.h`'s
  `defaults` map, one entry per translatable button, covering tab
  navigation and the most-used action buttons across the Board, FX Rack,
  Settings, Synth, and TTS tabs. Any key vlab doesn't recognise is simply
  ignored, so it's safe to leave stale keys in a mod after an update.

### 6.0 Translating ANY text (v1.9.6)

Besides the fixed keys above, a key can now be the **literal English text**
vlab shows. `"Master Out Gain (dB)": "Volume maestro (dB)"` renames that FX
Rack slider; the same works for section titles (`"Glitch FX"`), slider and
toggle captions, buttons, labels, and drop-down items (`"Reverse Bounce"`).
For sliders use the caption without the `: value` suffix. Anything you don't
list stays English. To get every string at once press **Settings -> General ->
Export Language Template**, which writes `mods/languages/_english_template.json`;
copy it to `yourlang.lang.json`, edit the right-hand side, Reload All Mods.

### 6.1 Where it lives

Drop a file named `*.lang.json` in `mods/languages` (created
automatically) and it shows up in the Language dropdown immediately
after "Reload All Mods" or a restart. Unlike theme mods there's no
Theme-Creator-style in-app editor, a translation mod is just a text file
you write by hand.

Eight bundled examples in `examples/languages/`: `spanish.lang.json`,
`french.lang.json`, `german.lang.json`, and `japanese.lang.json` for
people who want the app in another real language, plus
`pirate.lang.json`, `yoda.lang.json`, `uwu.lang.json`, and
`shakespearean.lang.json` for people who don't. All eight are fair game
as a starting template, copy one into `mods/languages`, rename it, and
start editing values.

## 7. Background Images/GIFs

Not technically a "mod" (no separate file format, no folder scan) but
lives in the same Settings area and is worth documenting here since people
building themes often want a matching background. Settings -> Background
-> "Load Background Image/GIF" accepts PNG/JPG/BMP (static) or GIF
(animated, decoded frame-by-frame with correct per-frame timing). Opacity
is adjustable 0-1 and applies as a simple alpha blend behind the UI panels:
the image shows through gaps between cards and in the sidebar, not
through the (opaque) panels themselves.

Implementation note for anyone curious: JUCE's own GIF reader only
decodes the first frame, so animated GIF support here is provided by a
vendored copy of `stb_image.h` (public domain,
`Source/ThirdParty/stb_image.h`), specifically its
`stbi_load_gif_from_memory` function, which was verified against a real
multi-frame test GIF during development (encode 3 solid-colour frames,
decode, confirm colours and per-frame delay come back correctly) before
being wired into the app.
