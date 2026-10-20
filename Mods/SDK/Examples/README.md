# Example mods

Bundled examples for all six modding surfaces described in `../SDK.md`.

## Audio mods (DLL)

- **`example_gain_mod.cpp`**, minimal working mod (just halves the
  volume), no parameters. Verified building and running against the real
  SDK ABI during development (compiled with `g++ -shared -fPIC`, loaded
  with `dlopen`, confirmed it correctly halves sample values through the
  exact interleaved-buffer interface `ModHost` uses).
- **`example_param_mod.cpp`**, the adjustable-gain mod from SDK.md
  section 1.5, exporting the optional parameter-metadata functions so it
  gets a real slider in the FX Rack automatically (see SDK.md section 1.6).

Build either one:

```bash
# Linux
g++ -shared -fPIC -I.. -o example_gain_mod.so example_gain_mod.cpp

# macOS
clang++ -shared -fPIC -I.. -o example_gain_mod.dylib example_gain_mod.cpp

# Windows (MSVC, from a Developer Command Prompt)
cl /LD /I.. example_gain_mod.cpp
```

(swap in `example_param_mod.cpp` for the second one). Drop the resulting
`.so`/`.dylib`/`.dll` into `mods/dll` next to the app, then hit "Rescan
Mods Folder" on the Mods tab.

## Visualizer mod (DLL, separate ABI)

- **`example_plasma_viz.cpp`**, a signal-reactive scrolling colour field
  built against `VLabVisualizerAPI.h` (see SDK.md section 4). Build the
  same way as the audio mods above, but drop the result into
  `mods/visualizers` and pick it from the Visualizer tab's dropdown
  instead of the Mods tab.

## Theme mods (JSON, no code)

Eight examples in `themes/`: `amber_terminal.theme.json` and
`minimal_override.theme.json` (colour-only, full vs. partial override),
`sharp_and_flat.theme.json`, `neon_pill.theme.json`, `frosted_glass.theme.json`,
`crt_terminal.theme.json`, and `synthwave_gradient.theme.json` (all now
plain colour palettes, pair any of them with the matching UI Preset in
Settings -> General for the look their names describe, see SDK.md section
2.3), and `hover_and_cursor.theme.json` (hover/role colours plus a custom
cursor style). Drop any of them straight into `mods/themes`; see SDK.md
section 2 for the full format.

## App mods (DLL, third separate ABI)

Two examples in `appmods/`, against `VLabAppModAPI.h` (see SDK.md
section 5):

- **`example_qol_mod.cpp`**, the lightweight path: a button that jumps
  to the Board tab, a toggle that shows a message when flipped on, and an
  F9 hotkey that does the same. No custom drawing at all.
- **`example_canvas_mod.cpp`**, the "override the whole GUI" path: a
  raw-RGBA animated starfield with `takeOverMainView = true`, so it gets
  its own "Starfield" tab in vlab's top bar and, once selected, owns the
  entire main content area, pixels and input both, until the user
  clicks any other tab.

Build either one the same way as the audio mods above, but drop the
result into `mods/appmods` instead of `mods/dll`, then hit "Reload All
Mods" (Settings -> General, or the Mods tab), as of v1.7.1 that
correctly rescans App Mods too (previously this mod type only loaded at
startup; see SDK.md section 5.4).

## Translation mods (JSON, no code)

Eight examples in `languages/`: `spanish.lang.json`, `french.lang.json`,
`german.lang.json`, and `japanese.lang.json` (real languages), plus
`pirate.lang.json`, `yoda.lang.json`, `uwu.lang.json`, and
`shakespearean.lang.json` (not real languages, but vlab doesn't judge).
See SDK.md section 6 for the full format, it's the simplest mod type
there is: a name and a flat key/value list of button text. Drop any of
them straight into `mods/languages`, then pick the name from the
Language dropdown in Settings -> General.

## Song mod (JSON, no code)

- **`songs/example_arpeggio.json`**, a four-note major-arpeggio-plus-rest
  loop, in the format documented in SDK.md section 3. Drop it into
  `mods/songs` and it shows up in the Synth tab's song dropdown as
  "example_arpeggio".
