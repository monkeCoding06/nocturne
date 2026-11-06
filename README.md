# NOCTURNE

A modular software synthesizer that lives entirely in the terminal. Real-time DSP,
a real signal graph, a playable computer keyboard, and a TUI built with FTXUI.

> Sound is something you build, manipulate, destroy, and discover.

* **Language:** C++20, **build:** CMake, **TUI:** FTXUI, **audio:** miniaudio (WASAPI / ALSA / PulseAudio / PipeWire via miniaudio backends), **patches:** JSON (nlohmann/json).
* Dependencies are fetched automatically by CMake (`FetchContent`). No other libraries.

## Text preview

```text
 NOCTURNE   F1 RACK  F2 BAY  F3 PLAY  F4 PATCHES  F5 SETTINGS  F6 HELP     PATCH: LEAD
 ──────────────────────────────────────────────────────────────────────────────────
 ┌ OSC1 ───────────┐ ┌ FILTER1 ─────────┐ ┌ AMP1 ───────────┐
 │ WAVE   SAW      │ │ TYPE   LOWPASS   │ │ GAIN  █████░░   │
 │ CUTOFF ...      │ │ CUTOFF 2400 Hz   │ └─────────────────┘
 └─────────────────┘ └──────────────────┘
   CUTOFF      ◀──────●────────▶  2400 Hz
 ──────────────────────────────────────────────────────────────────────────────────
  L ▓▓▓▓▒░░ R ▓▓▓▓▒░░ -8.2 dB │ VOICES 2/8 │ CPU 3% │ OCT 4 │ TIMED 0.5s   AUDIO ON
```

Six views: **F1 Rack**, **F2 Patch bay**, **F3 Play** (piano + live oscilloscope),
**F4 Patches**, **F5 Settings**, **F6 Help**. Meters and scope read the real output.

## Architecture

```
src/dsp       oscillator (PolyBLEP/PolyBLAMP), ADSR, filter, LFO, delay/saturator/reverb
src/modules   module registry: specs (ports, params) + DSP wrappers
src/patch     patch model, graph validation, cycle check, JSON I/O, factory presets
src/audio     lock-free queue, voice allocator, compiled rack, Engine, miniaudio device
src/input     key -> note mapping (timed / latch / live key state); MIDI could call the Engine directly
src/app       Session: patch editing, config, patch files, engine sync
src/tui       FTXUI views
tests         DSP, patch and engine tests (no audio hardware needed)
```

The engine never touches the UI. The UI thread sends events through a lock-free
queue; the audio thread owns all DSP state. Patch edits that change the graph build a
new compiled rack on the UI thread, which the audio thread swaps in (with a short
duck/fade, state is carried over). Retired racks are freed on the UI thread. The
audio callback does no allocation, locking, disk or terminal I/O.

## Build

### Linux
```sh
sudo apt install build-essential cmake ninja-build libasound2-dev   # Debian/Ubuntu
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/nocturne
```
### Windows (MSYS2 MinGW-w64 or Visual Studio)
```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
.\build\nocturne.exe
```
With Visual Studio use `cmake -S . -B build` then `cmake --build build --config Release`.
Use Windows Terminal for best colors and Unicode. Options: `-DNOCTURNE_BUILD_TESTS=OFF`,
`-DNOCTURNE_BUILD_APP=OFF` (DSP core + tests only, no network fetch of FTXUI/miniaudio).

`nocturne --export-presets DIR` writes the built-in factory patches as JSON (byte-identical to `presets/`);
`nocturne --check-presets` verifies them and compares them with an on-disk `presets/` directory
(`$NOCTURNE_PRESET_DIR`, `<exe>/presets`, `<exe>/../share/nocturne/presets`).

**Factory presets are generated from `presets/*.json` at build time** (`cmake/EmbedPresets.cmake`, order in
`presets/index.txt`) and compiled into the executable, so they never depend on the working directory or an
install. Embedded data always wins; an on-disk copy that differs is reported in F4. User patches
(`<config>/patches`) are listed as USER and never replace a factory patch of the same name.

**F4 Patches:** the header and F4 show the loaded patch's name, FACTORY/USER, source (embedded id or file
path) and MODIFIED. `Tab` switches the right panel between preview, all load diagnostics (errors fatal, warnings
for clamped/ignored/dropped content) and the exact loaded JSON; `Ctrl+E` exports it to `<config>/exports/`.
A fatal load error never replaces the current patch.

## Playing

**Keyboard layout:** the piano keys sit at the same physical positions on every layout. Pick
QWERTY, QWERTZ or AZERTY in Settings (F5) → KEY LAYOUT; the Play view relabels itself and the
choice is saved. On AZERTY the accented digit-row sharps cannot be read, and `A` is a note, so
use **F9** to add a module in the Rack.

Terminals normally report key presses but **no releases**, so NOCTURNE offers three modes
(cycle with **Tab** in the Play view or in Settings):

* **TIMED**: each key sounds for NOTE LENGTH (default 0.5 s), then releases. Holding a key
  (auto-repeat) extends it.
* **LATCH**: press a key to start the note, press it again to release it.
* **LIVE**: real keyboard behaviour: a note sounds exactly while its key is physically held and
  releases (ADSR release) on key-up; chords, overlaps and rapid re-taps work; effect tails keep
  ringing. It reads the *physical* key state, not typed characters, so auto-repeat is ignored.
  Limitations: implemented on **Windows only** (`GetAsyncKeyState` by scan code, so it is
  layout-independent). It acts only while the terminal window is the foreground window (conhost
  and Windows Terminal are detected via the console window owner / ancestor processes; with several
  Windows Terminal tabs any tab counts as focused), ignores keys while Ctrl/Alt/Win are held,
  and Shift = accent. Over SSH/remote consoles and on Linux/macOS terminals there are no key-up
  events (kitty keyboard protocol is not implemented), so LIVE is shown as **UNAVAILABLE** and is
  skipped by Tab. It never falls back to a timer. LIVE is a keyboard mode only: the patch's
  mono/poly/legato setting still decides voice behaviour.

```text
  sharps:   2 3   5 6 7   9 0        (upper row, octave+1)
  notes:   Q W E R T Y U I O P
  sharps:    S D   G H J   L         (lower row)
  notes:    Z X C V B N M , . /
```
CAPS = accent (full velocity). `[` `]` octave, Up/Down (Play view) velocity,
**Space** panic, **Esc** release all.

## Controls

| Key | Action |
|---|---|
| F1–F6, `` ` `` | switch views |
| F7 / Ctrl+S | save patch · F8 / Ctrl+N new patch · F10 / Ctrl+Q quit |
| Rack: Tab, ↑↓ | select module / parameter |
| ←→ | adjust (coarse); Ctrl+←→ or `-` `=` fine |
| Enter | type a value (`2.4k`, `440`, `saw`) · Backspace resets to default |
| A / Delete | add / remove module |
| Bay: Tab, Enter | switch list; arm source, then Enter on destination |
| Bay: ←→, Delete | connection amount, disconnect |

Frequency-like parameters use logarithmic travel. Min/max are shown when entering a value.

## Tutorial: a sound from an empty patch

1. **F8** new patch (only MASTER exists).
2. **F1**, press **A**, add OSCILLATOR, ENVELOPE (ADSR), AMPLIFIER.
3. **F2**: connect `OSC1.OUT → AMP1.IN`, `AMP1.OUT → MASTER.IN`, `ADSR1.OUT → AMP1.GAIN`.
4. Play a key (Z, X, C…). Pick WAVE in the Rack; add a FILTER and re-route `OSC1.OUT → FILTER1.IN → AMP1.IN`.
5. Sweep CUTOFF while playing. Edit A/D/S/R on the ADSR.
6. Add an LFO, connect `LFO1.OUT → FILTER1.CUTOFF`, adjust its amount in the Connections list.
7. Add another OSCILLATOR, detune it slightly, connect to the same filter.
8. Add DELAY / REVERB before MASTER and raise their mix.
9. **F7** to save; **F4** to reload it later (user patches live in the config directory).

## Example signal paths

```text
OSC1.OUT ──▶ FILTER1.IN ──▶ AMP1.IN ──▶ MASTER.IN
ADSR1.OUT ─────────────────▶ AMP1.GAIN
LFO1.OUT ──────────────────▶ FILTER1.CUTOFF
OSC2.OUT ──────────────────▶ OSC1.FM          (FM)
```

Cyan ports are audio, amber ports are control. Audio can only feed audio inputs and
FM-capable control inputs. Connections that would create a cycle, duplicate or cross
voice/global scope incorrectly are refused with a message. Each connection has an
amount (−1…1; negative inverts).

## Factory patches

INIT (sine), SUB (bass), GLASS (FM bell + delay/reverb), DRONE (evolving, LFO-modulated),
NOISE (band-passed noise), LEAD (mono, glide), HOOVER (rave stack with pitch swoop and PWM),
EURO HOOVER (`presets/euro_hoover.json`: two PWM-saw oscillators + PWM pulse + sine sub, dedicated ADSR3 pitch dive of
12 st, 6 Hz LFO PWM sweep, delayed ±24 ct vibrato, open low-pass, light drive, stereo CHORUS; best at octaves 1–3;
the first-generation version is kept as EURO HOOVER V1, `presets/euro_hoover_v1.json`).
Each is a real graph, not a renamed copy. Open it in F4 (Patches) and play in F3.

### Oscillator notes
* Waves: SINE, TRI, SAW, SQUARE, PULSE, NOISE, **SAWPWM** (band-limited ramp followed by a flat segment of width `pw`,
  an Alpha Juno-style PWM sawtooth approximation; `pw` and the PWM input move the shape, DC is removed).
* Modulation units: `PITCH`/`FM` are exponential (1.0 = 1 octave when OSC `RANGE` = 12 st). OSC **RANGE** (1–48 st, default 12)
  sets how many semitones a PITCH amount of 1.0 spans, e.g. RANGE 24 and amount 0.5 = exactly 12 st; the default keeps old
  patches unchanged. `PWM` = ±0.45 pulse width per unit, `FILTER.CUTOFF` = 5 octaves per unit.
* ADSR decay/release times are the time to fall to 0.1 % (time constant ≈ time / 6.9).

### Deep bass and LFO
* Keyboard octave runs **−1..8** (`[` `]` or ←/→ in Play; shown as `OCT n` in the status bar). Octave −1 puts the
  lower row on MIDI notes 0–11 (C = 8.18 Hz). Oscillator phase is double precision, so very low pitches stay exact.
* LFO: SINE/TRI/SQUARE/SAW/RSAW/S&H, 0.01–100 Hz, bipolar/unipolar, PHASE offset, DELAY and FADE-IN (restarted per note),
  FREE/KEY reset, RATE CV input. Depth/polarity of the target comes from the connection amount. No tempo sync (no tempo system).
* CHORUS module: three Hermite-interpolated modulated taps per side, RATE/DEPTH/MIX/WIDTH, stereo outputs L and R.
* `cmake --build build && build/nocturne_tests` also renders reference WAVs into `wav/` (EURO HOOVER C2/C3/riff, sub bass, pitch sweep).

## Files

Config and patches: `%APPDATA%\nocturne\` (Windows) or `~/.config/nocturne/` (Linux):
`config.json` and `patches/*.json` (human readable).

## Troubleshooting

* **AUDIO OFF**: open F5, check the error text, choose another device or buffer, press Enter to retry. NOCTURNE keeps running without audio.
* **Crackles**: raise BUFFER in Settings; watch CPU in the status bar.
* **Garbled boxes / colors**: use a UTF-8, truecolor terminal (Windows Terminal, kitty, iTerm2…). Minimum size 78×22.
* **Notes won't stop**: Space (panic). In LATCH mode press the key again.
* **Linux without sound server**: install ALSA dev packages; miniaudio picks PulseAudio/ALSA/JACK automatically.

## Testing

`ctest --test-dir build` runs 47 tests: oscillator pitch and anti-aliasing, ADSR
transitions, filter stability, parameter limits, graph validation and cycle detection,
JSON round trips, voice allocation/stealing, and offline rendering of every factory preset
(measured for non-silence and clipping). Audio hardware output was not auditioned by ear in
development; correctness is verified through generated sample buffers.
