# Working rules for this repository

## Check first. Always. Then act.

**Before doing a thing, verify the assumption the thing rests on.** If you
cannot verify it, stop and ask. Do not guess, and do not proceed on "it's
probably".

This is the rule that matters most here, because every expensive mistake on this
project has been the same mistake: acting on an assumption that one command
would have checked. Re-deriving a decision we already made, or reworking
something that was already correct, wastes more than the check would have cost.

A decision already made is made. If the conversation, a note in
`docs/arena-notes.md`, or a comment in the source already settles something,
that is the answer — do not re-litigate it or quietly redo it a different way.

### The checks that would have caught real failures

Each of these is something that actually went wrong, not a hypothetical.

- **Read the tool before running it.** `tools/export_mech_blend.py` documents in
  its own docstring that a `.blend` is written in the format of whichever `bpy`
  is installed and cannot be opened by an older Blender. A 5.0 file was handed
  to a 4.3 Blender anyway. The scratchpad holds a pinned 4.3 environment for
  exactly this; the exporter now prints the format it wrote.
- **Grep an identifier before claiming it.** Working-note codes (`[SIM-30]`,
  `[MESH-62]`, `[TYPE-10]`) and constant names must be checked for collisions
  first: `grep -roh "SIM-[0-9]\+" --include=*.c --include=*.h --include=*.md .`
  Picking a taken code and then renumbering with `sed` clobbered two unrelated
  cross-references.
- **Never run a formatter in place across a file you only appended to.** Format
  a copy, diff it, and confirm the change is confined to your own lines.
  `mdformat docs/arena-notes.md` silently reflowed two untouched paragraphs.
- **Verify the claim about the toolchain.** "Local `mdformat` differs from CI's"
  was assumed for a whole session and was false — both are 1.0.0, pinned in
  `mise.toml`. CI runs
  `git ls-files -z '*.md' ':!:LICENSE.md' | xargs -0 mdformat --check`; run
  that, not a guess about it.
- **Measure by tag or by diff, never by eyeballing extremes.** A table of
  min/max heights "showed" a pose change had not reached four machines; a
  per-quad diff showed all 396 quads and 26 bones had moved. Rounding at the
  extremes is not evidence. Measuring by height band rather than by part or bone
  tag has bitten this project four separate times.
- **Check the tree after any `git stash` or `checkout` before trusting a test
  run.** Several "passing" runs were against a stale tree. `grep -c` for a
  marker you just added is enough.
- **Check scope before calling into a module.** `bpy` is imported inside
  `build_blend`, not at module level.

### Never committed

The retail data (`PALETTE.PAL`, `FATDATA.zip`) is not ours. It lives in the
session scratchpad, outside the repository, and never enters the tree. Only
index numbers and this project's own fallback colours go in source.

## Verifying a change

Run all of it before saying a change is done. Say plainly if a step failed or
was skipped.

```sh
S=<scratchpad>            # session scratchpad; holds zig and its caches

# Arena simulation, arena, roster and mesh tests (SDL-free, builds with gcc)
gcc -O1 -I PROJECTS/ROLLER \
  PROJECTS/ROLLER/mecha_math.c PROJECTS/ROLLER/mecha_arena.c \
  PROJECTS/ROLLER/mecha_defs.c PROJECTS/ROLLER/mecha_sim.c \
  PROJECTS/ROLLER/mecha_ai.c PROJECTS/ROLLER/mecha_mesh.c \
  PROJECTS/ROLLER/carplans.c tests/mecha_sim_test.c -o "$S/simtest" -lm
"$S/simtest"

python3 -m unittest discover -s tests
python3 tools/check_roller_core_manifest.py
git ls-files -z '*.md' ':!:LICENSE.md' | xargs -0 mdformat --check

# Zig needs its cache dirs passed explicitly -- the global package cache is
# empty in a fresh container, and without these it tries to refetch SDL and
# fails on the network.
"$S/zigpkg/ziglang/zig" build test-mecha-render \
  --global-cache-dir "$S/zig-global" --cache-dir "$S/zig-cache"
"$S/zigpkg/ziglang/zig" build -Doptimize=ReleaseSafe \
  --global-cache-dir "$S/zig-global" --cache-dir "$S/zig-cache"
```

A new test must be shown to **fail against the old behaviour**, not merely to
pass against the new. Run it both ways and say so.

Frame dumps come from `zig build test-mecha-render -Dmecha-frames=<dir>`.

## Conventions

- Short source comments citing a code (`[MESH-62]`); the reasoning goes in
  `docs/arena-notes.md` under that code. Append notes; do not rewrite history.
  When a change makes an existing note wrong, update that note.
- `docs/` wraps at 80 columns (`.mdformat.toml`).
- The game view basis is **left-handed**. Game `(x,y,z)` to Blender is
  `(-x,-z,y)`; back again is `(-bx, bz, -by)`, in metres times 250.
- Blender rig bones run **down** the limb, so a bone's own Y is along its
  length: X pitches, Y twists, Z cants.
- `mech_export` writes each bone's axes as **rows**. Transposing is harmless on
  a pure-translation frame and wrong on a rotated one.
- There is **no depth buffer** — a painter's sort. Coplanar overlapping quads
  cannot be ordered, which `coplanar_overlaps` guards.
- Angles are a 14-bit circle (`MECHA_ANGLE_FULL` 16384). `MECHA_DEG(x)` takes a
  float.
- `mecha_add_hull` and friends: `fHx0/fHz0` is the **−Y (bottom)** face,
  `fHx1/fHz1` the **+Y (top)**, and `fSkewX/fSkewZ` offsets the top only.
- Quad budget: capacity 12288, and the worst-scene test asserts a peak below the
  9216 mark. Near detail is paid for out of the far tier.
