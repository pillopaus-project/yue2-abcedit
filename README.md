# yue2-abcedit

Qt 6 desktop app that converts **MIDI** and **standard ABC** input into a
**bounded native ABC dialect**, with a live-validation editor pane and an
audio preview of the editor content.

Working output is defined narrowly: output that **passes the checker**,
or an explicit **refusal naming group, voice, bar, and reason**.
`example_code/abc_tools.py` is the reference checker and the ground truth
for every rule. Never "fix" output by editing the checker.

## Scope

- **Import**: MIDI (`.mid`, incl. uppercase Windows names) and standard ABC.
- **Mapper**: assign each source track/voice to `Vocal`, `Ins`, or `Ignore`.
  Drums are ignored by default but stay visible.
- **Quantize preview**: picks the coarsest grid that writes the material.
  Exact timing passes untouched; with the snap box ticked, off-grid entries
  move to their closest writable position (every move logged per bar,
  triplet feels flagged per bar). Unticked = exact only, off-grid bars refuse.
- **Validate**: the editor content against the native-dialect rules.
- **Export**: writes `.abc` (extension auto-completed, overwrite needs confirm).
- **Editor pane**: free editing with live checker feedback, font-size control,
  and a **Preview sound / Play in app** path rendering `/tmp/temp.wav`.

## Compiling

Requirements: CMake ≥ 3.21, C++17 compiler, Qt 6
(`qt6-base-dev` + `qt6-multimedia-dev` on Debian).

```bash
cmake -S . -B build
cmake --build build
```

Run from inside `build/` (test working directories assume the repo root):

```bash
ctest
```

Suites: `checker_parity`, `pipeline_mid2abc`, `editor_open_edit`,
`preview_render`. Launch the app with `./yue2-abcedit [file.abc]`.

## Usage help

1. **Import**: pick the file — MIDI (`.mid`/`.midi`, any case) vs standard
   ABC is detected from the extension, no selector. A wrong header
   resolution (ticks per quarter note) can be overridden on the spot.
2. **Mapper**: map each source to Vocal / Ins / Ignore (first two mapped by
   default, the rest ignored with a warning).
3. **Quantize preview**: run quantize, read the grid line and the per-bar log.
   Tick the snap box only if you accept closest-slot moves; triplet feels are
   straightened and flagged, never hidden.
4. **Validate**: the conversion sits in the editor — edit freely, the live
   line tells PASS/FAIL per edit. Validate shows the full text + verdict.
5. **Export**: choose a path (`.abc` is appended automatically), confirm any
   overwrite.

**Preview sound**: with a passing editor, *Preview sound* renders
`/tmp/temp.wav` (overwritten each time; melody + block chord voicings,
rests silent) and *▶ Play in app* plays it. Broken text disables both —
fix the live message first. Tempo follows the file's `Q:` line; edit it in
the pane to hear it faster or slower.

## Headless conversion (`yue2-abcconvert`)

Same pipeline without UI or audio:

```bash
./build/yue2-abcconvert [--snap-on] [--force] <input> <output.abc> <log.txt>
```

- Input type follows the extension (`.mid`/`.midi`, any case, else ABC).
- Default mapping (first two voices, rest ignored with a warning).
- `--snap-on` allows closest-slot timing snaps (logged per bar, triplet
  feels flagged); default is exact-only and off-grid bars refuse.
- Existing output/log files refuse unless `--force` is given.
- The log file records input, grid, snap counts, the full per-bar log,
  and the final checker verdict; exit codes are 0 converted,
  1 conversion refused, 2 usage/I-O error.
