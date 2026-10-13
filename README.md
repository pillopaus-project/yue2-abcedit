# yue2-abcedit

src code by Muse-spark-1.3 

Qt 6 desktop app that converts **MIDI** and **standard ABC** input into a
**bounded native ABC dialect**, with a live-validation editor pane and an
audio preview of the editor content.

Working output is defined narrowly: output that **passes the checker**,
or an explicit **refusal naming group, voice, bar, and reason**.
`src/core/AbcChecker` is the reference checker and the ground truth
for every rule. Never "fix" output by editing the checker.

## Scope

- **Import**: MIDI (`.mid`, incl. uppercase Windows names) and standard ABC
  through the conversion pipeline (mapper + quantize); Yue2 JSON (`.json`)
  loads its already-native `abc` field straight into the editor unchanged.
- **Mapper**: assign each source track/voice to `Vocal`, `Ins`, or `Ignore`.
  Drums are ignored by default but stay visible.
- **Quantize preview**: picks the coarsest grid that writes the material.
  Exact timing passes untouched; with the snap box ticked, off-grid entries
  move to their closest writable position (every move logged per bar,
  triplet feels flagged per bar). Unticked = exact only, off-grid bars refuse.
- **Validate**: the editor content against the native-dialect rules.
- **Export**: writes `.abc` (extension auto-completed, overwrite needs confirm),
  or — when a `.json` was opened — merges the editor ABC back into a copy of
  the source JSON (only `abc` replaced, every other field untouched;
  `.json` auto-completed, overwrite needs confirm).
- **Editor pane**: free editing with live checker feedback, font-size control,
  and a **Preview sound / Play in app** path rendering `/tmp/temp.wav`.


<img width="1122" height="810" alt="yue2-abcedit_1" src="https://github.com/user-attachments/assets/553fd162-fdb7-476f-a3e5-e29d915b48de" />

<img width="1122" height="810" alt="yue2-abcedit_2" src="https://github.com/user-attachments/assets/afacd0ff-0fac-4b9a-9718-fbcd0014860c" />

<img width="1396" height="810" alt="yue2-abcedit_3" src="https://github.com/user-attachments/assets/b8499808-8b57-463e-8461-587591428f7e" />



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
`preview_render`, `yue2json`. Launch the app with `./yue2-abcedit [file.abc]`.

## Usage help

1. **Import**: pick the file — MIDI (`.mid`/`.midi`, any case) vs standard
   ABC vs Yue2 JSON (`.json`) is detected from the extension, no selector.
   A wrong header resolution (ticks per quarter note) can be overridden on
   the spot. For `.json`, the `abc` string is decoded (JSON unescaping
   handled, `\r\n` normalized) and loaded into the editor byte-for-byte —
   no rebuild, so `%` comments, accidental spellings, and bar layout
   survive; only `abc` is guaranteed — every other JSON field is carried
   along untouched for the later merge-save. Mapper/quantize stay
   MIDI/ABC-only: after a JSON import, *Next* jumps straight to Validate.
2. **Mapper**: map each source to Vocal / Ins / Ignore (first two mapped by
   default, the rest ignored with a warning).
3. **Quantize preview**: run quantize, read the grid line and the per-bar log.
   Tick the snap box only if you accept closest-slot moves; triplet feels are
   straightened and flagged, never hidden.
4. **Validate**: the conversion sits in the editor — edit freely, the live
   line tells PASS/FAIL per edit. Validate shows the full text + verdict.
5. **Export**: choose a path (`.abc` is appended automatically), confirm any
   overwrite. When a `.json` was opened, *Export JSON* writes a new `.json`
   (suggested default: `<source>-edited.json`) containing the origin file's
   fields with only `abc` replaced by the editor text — which must pass the
   checker first. Only the `"abc"` value is replaced in place — key order,
   whitespace, and every other field stay byte-identical to the source file.

**Preview sound**: with a passing editor, *Preview sound* renders
`/tmp/temp.wav` (overwritten each time; melody + block chord voicings,
rests silent) and *▶ Play in app* plays it. Broken text disables both —
fix the live message first. Tempo follows the file's `Q:` line; edit it in
the pane to hear it faster or slower.

## Headless conversion (`yue2-abcconvert`)

Same pipeline without UI or audio:

```bash
./build/yue2-abcconvert [--snap-on] [--force] <input> <output.abc> <log.txt>
./build/yue2-abcconvert [--snap-on] [--force] <input.json> <output.json> <log.txt>
```

- Input type follows the extension (`.mid`/`.midi`, any case, `.json` for
  Yue2 JSON, else ABC).
- A `.json` input is already native: it is checker-gated and merged back
  unchanged (only `abc` replaced, other fields preserved) — or written as
  plain `.abc` text when the output ends in `.abc`. MIDI/ABC inputs convert
  as before.
- Default mapping (first two voices, rest ignored with a warning).
- `--snap-on` allows closest-slot timing snaps (logged per bar, triplet
  feels flagged); default is exact-only and off-grid bars refuse.
- Existing output/log files refuse unless `--force` is given.
- The log file records input, grid, snap counts, the full per-bar log,
  and the final checker verdict; exit codes are 0 converted,
  1 conversion refused, 2 usage/I-O error.
