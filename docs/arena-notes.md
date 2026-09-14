# Arena mode: working notes

Long-form reasoning behind the arena mode's code. Source comments stay short and
point here by code, e.g. `[AI-02]`.

Each note records what was measured and what it ruled out, so a later change
does not re-litigate a question that already has an answer. Where a note says
"measured", the numbers came from running the simulation, not from reasoning
about it.

Codes are grouped by file: `AI-` mecha_ai.c, `MESH-` mecha_mesh.c, `SIM-`
mecha_sim.c, `ARENA-` mecha_arena.c, `REND-` mecha_render.c, `DEF-`
mecha_defs.c, `MODE-` mecha_mode.c, `TEST-` the test sources.

______________________________________________________________________

## HANDOFF — where the arena stands, for whoever picks it up next

Written at the end of the session that cut 0.2.1. Everything below is state and
working rules, not reasoning; the reasoning is in the coded notes.

### What this is

An original Virtual-On-style arcade mecha arena fighter built inside ROLLER, the
C reimplementation of Whiplash / Fatal Racing (1995). It reuses ROLLER's
software rasteriser, its 14-bit angle math and its physics idioms, and it reads
the retail game's textures out of FATDATA at runtime. It is not a mode of the
race game: it is a second game sharing an engine, reached with `--arena` or from
the main menu.

### How to work on it

These are the standing instructions, in the user's own terms:

- **Never guess.** If a fact is needed and not to hand, stop and ask. Do not
  invent numbers, APIs or behaviour.
- **Do it the way ROLLER does it.** Study the engine's own code first and follow
  it. Where a better approach exists, or the engine cannot do what is being
  asked, say so and ask before departing from it -- do not quietly improvise.
- **Do not push work that is not right yet.** A half-finished change on the
  branch is worse than no change.
- **Comments stay short.** Source comments are concise, descriptive and for a
  human reader; anything longer belongs in this file behind an alphanumeric
  code, with the code cited in the comment.
- **Be token-efficient.** Measure rather than argue; ask rather than explore
  blindly.

### The retail data, which is not ours

The user supplies `PALETTE.PAL` and a `FATDATA.zip` for debugging. They are
retail game data: they live in the session scratchpad, outside the repository,
and are **never** committed. Only index numbers and this project's own fallback
colours go in the tree. A new session starts with an empty scratchpad -- ask the
user to re-supply them if a texture or palette question comes up, rather than
going looking.

### Repository, branch, release state

- Repo is `FatalDecomp/ROLLER-X`, moved there from `kitaaaaaa/ROLLER`. The old
  URL 301s, and in a session whose GitHub scope is the old owner the redirect is
  what makes pushes work: pointing `origin` at the new URL gets a 403 from the
  git proxy, which will not inject a credential for a repo outside the session's
  authorized set. `add_repo` refuses cross-owner adds, so a session that needs
  GitHub API or write access under the new org has to be **started** with
  `FatalDecomp/ROLLER-X` as its source.
- Default branch is `master`. Arena work has been on
  `claude/virtual-on-mecha-game-19to7d`, merged to master for each release.
- `build.zig.zon` says 0.2.1; `docs/release-notes/0.2.1.md` is written and the
  Release workflow reads it by version.
- **Cutting a release cannot be done from a Claude session**: dispatching the
  workflow returns 403 (not accessible by integration) and pushing a tag is 403
  too. The user dispatches Actions -> Release -> Run workflow (version, draft
  off) by hand. Both 0.2.0 and 0.2.1 went out that way.

### Verifying a change

Run all of these before committing anything:

```
gcc -std=c99 -O1 -I PROJECTS/ROLLER \
    PROJECTS/ROLLER/mecha_math.c PROJECTS/ROLLER/mecha_arena.c \
    PROJECTS/ROLLER/mecha_defs.c PROJECTS/ROLLER/mecha_sim.c \
    PROJECTS/ROLLER/mecha_ai.c PROJECTS/ROLLER/mecha_mesh.c \
    PROJECTS/ROLLER/carplans.c tests/mecha_sim_test.c -lm -o simtest && ./simtest
python3 -m unittest discover -s tests -p "test_*.py"
zig build test-mecha-render
zig build -Doptimize=ReleaseSafe
python3 tools/check_roller_core_manifest.py
mdformat --check docs/
```

At the time of writing that is 94 sim test groups and 315 python tests, all
passing.

### Looking at it with the artwork on

`zig build test-mecha-render` runs against no retail data, which is what CI has
and what a fresh checkout has, and every frame it dumps is the flat-fill
fallback. To see the picture a player with the game installed sees, run the
built test binary from a directory holding the data -- it opens its files by
relative name, so the working directory is the whole of it:

```
zig build test-mecha-render                     # builds and runs it flat
BIN=$(ls -t <cache>/o/*/mecha_render_headless_test | head -1)
mkdir -p /tmp/wl && cd /tmp/wl                  # lower-case symlinks to FATDATA
for f in /path/to/FATDATA/*; do ln -sf "$f" "$(basename "$f" | tr 'A-Z' 'a-z')"; done
"$BIN" /tmp/shots                               # same test, textured
```

The last line it prints says which of the two it just did. Both have to pass:
anything that only holds in one of them is a check that has never met the other
[TEST-18].

### Looking at Whiplash itself

The game renders headlessly through the same PNG writer, and takes either a
named menu scene or a replay. Frame numbering starts at 1 for a scene:

```
zig build
./zig-out/bin/roller --whiplash-root /path/to/FATDATA --no-crash-handler \
    --snapshot-scene menu-main --frames 1 --out /tmp/wl
./zig-out/bin/roller --whiplash-root /path/to/FATDATA --no-crash-handler \
    --snapshot INTRO2.GSS --frames 300,600 --out /tmp/wl
```

`INTRO1.GSS`..`INTRO7.GSS` are the attract-mode replays, which is the cheapest
way to get a picture of the race game actually driving. A run costs a couple of
minutes because the replay plays through to the frame asked for; ask for the
frames in one run rather than one run each, and keep the highest under about a
thousand. `zig` is not on a remote session's PATH and its package fetcher cannot
reach GitHub through the agent proxy; the way round both is in the toolchain
notes below.

Notes on the toolchain and the harness:

- **gcc is the test toolchain.** Zig is for the render test and the release
  build only; when Zig's math got in the way the user's instruction was to keep
  using gcc.
- A remote session may have no `zig` on PATH. It can be fetched into the
  scratchpad (the `ziglang` pip package carries the binary) and run with
  `--global-cache-dir`/`--cache-dir` pointing there.
- **Zig's package fetcher cannot get through the agent proxy.** Every
  `git+https` dependency fails with "unable to discover remote git server
  capabilities". Plain `git` works, so the way round is to clone each dependency
  at its pinned commit, delete its `.git`, and `zig fetch` the local directory:
  the hashes come out identical to `build.zig.zon`. Two further rounds of the
  same catch the transitive ones (castholm/SDL, freepats, libsdl-org/SDL_image,
  SDL_linux_deps). Once they are in the global cache the build is offline and
  fast.
- When grabbing render frames, copy the **newest** `mecha_render_headless_test`
  (`ls -t`, not a bare `find`) into a scratch directory and run it there with
  the dump directory as argv[1]. A stale binary has wasted hours.
- Piping a build into `head` can kill the compiler with SIGPIPE and leave the
  **old** binary in place, so a measurement then reports the previous behaviour.
  Do not pipe `gcc` into `head`.

### What is in the game at 0.2.1

Modes: duel, survival (sixteen machines, each its own team), team deathmatch
(eight a side [MODE-08]) and spectator. Seven arenas, the last being FACING
WORLDS [ARENA-16..19]. Sound runs through Whiplash's mixer \[SND-01..07\]: the
walk drives the engine loop [SND-07], a ground boost is what squeals [SND-06],
every weapon has a voice built out of a car noise at a new pitch [SND-04], and
two cockpit warnings answer a hit taken and a dry trigger [SND-05]. A destroyed
machine burns for seconds rather than popping once [SIM-27]. Weapon fire is
painted in six neon hues the arena is never painted in [DEF-12] and held to a
minimum apparent size [MESH-48]; a landed hit flashes HIT over the clock
[REND-14]. Computer pilots get round cover [AI-09], turn a dash mid-burst
[AI-10], refuse drops [AI-11], get round gaps [AI-12] and follow the arena's
published ways [AI-13].

Nine machines on four drawn chassis \[TYPE-06\]: five ordinary bipeds, the
wheeled ZIZIN (which gets its own pilot branch in `mecha_ai_think`), BASTION 88
on tracks [MESH-38] and Tarant VZ on six legs [MESH-39]. Two of the bipeds wear
their own trim package [TYPE-07] -- Lilia 07 the slender frame with a crossing
walk [MESH-33, MESH-40] and Corvid 3 a rack of drone pods [MESH-36]. Machines
are built at one of three detail tiers by range [MESH-32], and a machine that
wins a round holds a pose [MESH-41, MESH-42].

### Known and open, at 0.2.1

- **FACING WORLDS endgame.** Two survivors on opposite lanes settle into trading
  fire at about 200 m instead of crossing at the pinch: 9 of 12 clock-less
  fights finish inside ten minutes, the rest run out. It is a chase behaviour,
  not navigation. With the briefing's round clock on, rounds end on armour and
  it does not show.
- **Falls.** About 1.2 machines a sixteen-machine fight still go into the hole
  on FACING WORLDS, nearly all of them the wheeled ZIZIN.
- **The keeps are open-topped.** A box here is solid from the ground up, so a
  roof would be a lid with no way under it.
- **Wall tiles.** The keeps wear `MECHA_TILE_RUST`; BRICK is a floral tile and
  CONCRETE is a glazed facade, so neither reads as masonry.
- **Sound is unheard.** Levels, pitches and the pan convention are asserted from
  the mixer's own code [SND-02] and by tests, but nobody has listened to it. The
  sample choices in [SND-04] were made by measuring the `.RAW` files --
  duration, zero crossings, dominant frequency, tonality -- not by ear, and the
  two warning pitches in [SND-05] are a guess that listening has already
  corrected once -- the dry trigger came out sounding like a car horn -- and the
  boost squeal's rate has been moved down once for the same reason [SND-06].
  Expect more of that.
- **A spectator sees DEFEAT.** `mecha_phase_banner` asks whether the viewer is
  allied with the winner, and a free camera is on nobody's side. Pre-existing,
  never reported as a bug, listed here so it is not rediscovered as one.

______________________________________________________________________

## AI-01 — the dodge margin does not scale with skill

`MECHA_AI_DODGE_MARGIN` is the same at every skill level, deliberately.

Scaling it with skill was tried and made the ladder run backwards. A pilot that
dodges more also dashes more; a dash changes its stance and swings it off the
firing cone, and the boost it burns eventually locks out, at which point it
cannot dodge at all. Measured over five duels, the wide-margin pilot both dealt
less damage and absorbed more than the middle rung.

Reaction time is the honest lever. How near a miss has to be before it is worth
answering is not.

## AI-02 — what actually separates the skill levels

The pilot reads the same world struct the simulation ticks, so it cannot be made
worse by hiding information from it — only by putting human limits back in. The
profile numbers were measured, and what they say is not what the obvious design
predicts.

**`iAimError` is the lever that works.** Every weapon aims itself at whatever is
locked, so with no error term the pilot fires a perfect solution every time.
Over twelve duels the damage it lands falls off cleanly once the error clears
the target's own width: about 23400 at zero, 19100 at nine degrees, 15500 at
fourteen. Below roughly four degrees nothing happens at all — the shot radius
and the target radius swallow the error. Past about fourteen the curve flattens
again.

**`iReactionTicks` is not a strength lever**, however much it looks like one.
Sweeping it from zero to six tenths of a second moved the totals around inside
run-to-run variance and never in a consistent direction: a pilot that answers
every shot the instant it is fired also dashes constantly, and dashing swings it
off its own firing cone and drains the boost it needs to dodge with. It is kept
because it changes how the pilot *reads* — a rookie visibly flinches late — not
because it makes one harder to beat.

**`iTriggerOdds`** barely touches damage dealt, but hesitating measurably raises
damage absorbed, which is the half a losing player actually feels.

**`iTurnPercent`** arrived with the auto-turn being confined to knife range.
Before that a locked machine squared itself up for free at any distance, so how
well a pilot steered did not exist as a quality. Once pointing the machine
became the pilot's job, all three levels steered perfectly and the ladder
stopped meaning anything on the damage-taken half.

## AI-03 — look-ahead is braking distance, not a linear guess

`mecha_ai_stopping_look` originally used a stride plus a fixed fraction of
speed, and it undershot badly at the top end: a machine at seventy metres a
second checked forty-one metres ahead and needed eighty-six to stop, so by the
time an edge was inside its look-ahead it was already past saving.

Braking distance is `v² / 2a`, and the machine's own grip is that `a`, so the
number is available rather than guessable.

The larger of the two is taken. The linear guess is the better number for a
machine that stops hard — a walker's grip is three times a car's, so its braking
distance at walking pace is shorter than its own reaction time — and the braking
distance is better at the top end. Taking the larger means this can only ever
look further ahead than it used to.

The stride added on the front is reaction: a machine standing still still has to
not step off.

## AI-04 — spread weapons are scored by the share of cone that lands

Counting every pellet of a scattergun as a hit at any distance is what made the
gun car fire buckshot across the whole arena and never once reach for its rifle:
seven pellets of twenty-four scored as a hundred and sixty-eight whether the
target was fifteen metres away or a hundred and fifty.

A cone that wide only lands as a cone up close, so what is scored is the share
of it the target still covers.

## AI-05 — the footing rule runs last, and has three cases

An arena can have nothing underneath it. On a roof with a hole through the
middle, a pilot that only thought about the fight would walk into the pit.

There is no path-finding. There is only: do not step off, do not spend a burst
that ends off, and if you are already going off, cancel.

It runs after the dodging and after the lock has had its say about boosting
round to face someone, because any of those will happily spend a burst over the
edge.

A wall is not a hazard. Only an arena you can leave has an edge worth avoiding;
a pit is worth avoiding anywhere. Checking the boundary on a walled arena would
have the pilot backing away from walls it is entitled to fight against.

Three states need separate handling:

- **Standing.** Letting go of the stick is not stopping — a machine just out of
  a burst is still travelling, so what it is carrying gets checked whether it
  asked for it or not.
- **Airborne.** There is no stepping back and nothing to brake against. All it
  can do with remaining thrust is lean towards the middle of the arena.
- **Mid-burst.** The burst is committed, so wanting to stop is not enough. The
  way out is the player's way: push back against it and boost again.

Roughly five per cent of rooftop fights still end in a walk-off, in the gaps
between those three states. Bounded by test rather than fixed.

## AI-06 — a boost cancel needs a fresh press

A burst is started by the press and a cancel needs another one, so a pilot that
simply leans on the boost button cancels nothing and rides the burst it wanted
to throw away straight off the edge. The input has to be released and pressed
again.

## AI-07 — a pilot saving its footing does not shoot

Firing locks a machine out of acting for the recovery, and a machine that cannot
act cannot steer. A shot taken while sliding towards an edge spends the only
ticks it had to stop itself. This was the last way computer pilots were leaving
the roof.

Held fire, by contrast, is applied at the trigger rather than earlier, so the
pilot goes on closing, circling and dodging exactly as it would. A machine that
stopped fighting would not show anything about how the fighting looks.

______________________________________________________________________

## MESH-01 — translucent quads carry a shade level, not a colour

`POLYFLAT` hands `SURFACE_FLAG_TRANSPARENT` polygons to `shadow_poly`, which
indexes `shade_palette[256 * level]` to darken what is already there.
`shade_palette` is 4096 bytes, so the level must stay under 16 or the read runs
off the end. The engine's own callers use 2 and 3 — `func2.c`'s `blankwindow`
and `replay.c`'s car shadows — so the mode's match.

## MESH-02 — walls are panelled because POLYTEX fits one tile per polygon

The legacy texture path works its coordinates out inside `POLYTEX` from the tile
index and the projected polygon, and fits exactly one tile to whatever polygon
it is given. A wall built as a single quad wears one tile stretched two hundred
metres wide and twenty high — a smear, not a texture. Cut into panels the size
of the floor's own tiles, each panel gets a tile at the scale the ground is
using and the two agree.

## MESH-03 — outer ground is rings, not a grid with a hole

Ground past the boundary cannot be walked on and is drawn coarsely, since it is
only ever seen at a distance. What it buys is that the arena stops being an
island.

It is built as rings of the boundary's own shape rather than a grid with the
middle knocked out, and that is not tidiness. A grid coarse enough to be cheap
has tiles far wider than the boundary is straight, so every tile it drops for
overlapping the arena takes a wedge of ground with it and the horizon fills with
holes, while every tile it keeps lies coplanar over the arena's own floor. Rings
share the edge exactly, so there is neither.

## MESH-04 — the walk cycle is paced by distance, not time

`fStepPhase` counts distance — one cycle every stride — so a machine that stops
mid-stride stops mid-stride, and a heavy one that covers ground slowly takes
slow steps without anything having to say so.

The thigh swings as a sine of the phase; the knee bends through the forward half
of that swing and straightens for the half the foot is on the ground pushing
back, which is the difference between walking and a pair of planks pivoting at
the hip.

Angles are positive forward and the caller negates them, because a positive
pitch in the pose matrix swings a limb backwards.

## MESH-05 — a glide runs on its own clock

Every other cycle is paced by distance. A boost breaks that: at seventy metres a
second, one stroke every five metres is fourteen cycles a second, and legs
moving that fast are a grey blur. The glide is timed instead — one long push
every two thirds of a second — which is what makes it read as gliding rather
than sprinting.

Boosting on the ground is not running. The thrusters do the work, so the legs
hold the machine up and steer it, which is a skater's problem: both knees bent
throughout, weight low, one leg reaching out and back in a long push while the
other glides underneath. The pushing leg straightens as it goes out, which is
what lets it stay on the floor at full stretch.

## MESH-06 — the ankle drop is what keeps feet on the floor

The body is lowered by whatever the straighter leg has lost, so bending the
knees sinks the machine instead of leaving it hanging in the air.

The thigh's pose pitch is `-A` and the knee's is `+K`, so the shin's frame sits
at `K - A` off the vertical and the ankle drops by the cosine of that. Getting
this sum wrong is the difference between a machine that walks and one that
skates with its feet through the floor.

## MESH-07 — attitude is summed in one place

Whiplash keeps the pieces apart all the way to the render pose and sums them at
the end (`car.c`: yaw takes the shake; pitch and roll take the landing wobble,
the shake and the control offset). The same three lines are all that is needed
here, and keeping them together makes it possible to read what a body is doing
without chasing the terms round the simulation.

## MESH-08 — a car's knockdown is a roll, not a pitch

Something tall enough to have a face pitches forward onto it. A machine nine
metres long and two high has nowhere to pitch to, and a Zizin standing on its
nose reads as a glitch rather than a wreck. The knockdown for a wheeled machine
is a half roll onto its roof.

## MESH-09 — the Zizin plan's axes are of opposite handedness

The body is the race game's own Zizin, polygon for polygon: `xzizin_coords` and
`xzizin_pols` out of `carplans.c`, the same fifty quads the car is drawn with on
the track.

The plan is in the race game's axes — x along the car, y across it, z up — where
the arena's are x across, y up, z forward. The three swap and the lateral one is
negated. That negation is what keeps the car the right way round: the two frames
are of opposite handedness, so swapping axes alone builds the car's reflection,
with wheel arches, exhausts and both flanks of its livery on the wrong sides.

Reflecting it back reverses every winding the plan had, which is why its panels
face inwards here and why artwork on them needs different treatment from the
rest of the mode.

## MESH-10 — three kinds of texture word in the plan

The race game's own draw path walks all three:

- Most panels carry a texture word: `APPLY_TEXTURE` set, tile in the low byte.
- Eight — the wheels and the livery — carry `ANMS_LOOKUP`, where the low byte
  indexes the car's animation table and the real word is a frame out of it.
  Frame zero is the one at rest.
- The rest carry no texture flag and the low byte is a plain palette index,
  which is how the tyres come out black.

## MESH-11 — the fifty panels do not need a sorted list

The race game draws this body through a sorted polygon list of its own — the
`nNextPolIdx` links in the plan — and a painter's algorithm has no such list, so
two panels sharing a plane would flicker. They do not: the fifty are tested
against each other by the coplanar check, and the body is rigid, so passing at
one pose is passing at all of them.

## MESH-12 — panel orientation comes from the plan, not from us

Each polygon carries `SURFACE_FLAG_FLIP_HORIZ` and `SURFACE_FLAG_FLIP_VERT`
beside its tile index. That is how the body wears one tile across a pair of
mirrored panels — roof rails, rear roof edge, lower tail corners — and it is why
painting it panel for panel out of the same file still came out back to front:
those bits were being dropped and the orientation guessed at afterwards, one
panel at a time, from renders.

Read the flags off the surface the lookup settled on, not off the polygon. This
matters for the wheels: those four name an animation slot and carry no
orientation of their own, while the frames behind them do — the near-side pair
flipped, the off-side pair not. Taking the polygon's word for it left all four
wheels wearing the same face.

Dropping the corner reversal is the vertical mirror, so horizontal is that
reversal plus two quarter turns, and the two together are a half turn with no
reflection at all. `MECHA_QUAD_TEX_ROT90` and `MECHA_QUAD_TEX_ROT180` form a
two-bit turn count, so all eight arrangements are reachable.

## MESH-13 — the gun car's gun

A handgun about as long as the car, attached to nothing: it floats off the front
right wheel, held over on its side so the slide is horizontal and the shot goes
out across the bonnet rather than over the roof. That roll is what puts the grip
out to the left instead of underneath. No arms, no turret, just an absurd pistol
keeping station beside a race car.

Firing throws it up and back. The recovery counts down from the shot, so the
kick is hardest on the tick it goes off and has run out by the time the next
round is chambered. The same kick shoves the car in the simulation, so what is
drawn is what happened.

## MESH-14 — rolling over lifts the body back onto the floor

The pose turns about the car's own floor, so half a roll puts the whole body
below it: a point at height `h` lands at `h cos t`, and at 180 degrees the roof
is a full height underground. Raising the origin by however far the lowest
corner has gone under keeps the car resting on the floor the whole way over — a
car rolling, rather than a car sinking into the tarmac.

## MESH-15 — the lean is negated

Positive roll lifts the machine's right side and so leans the machine left.
Established by building a mesh at a known roll and measuring which flank came
out lower, after reasoning about it got the sign wrong twice.

Without the negation the machine leant away from its direction of travel. A
machine boosting to its right leans right, as anything on wheels or blades does.

## MESH-16 — the planting solve opens the hips, not the knees

Since MESH-45 this runs second: the knees equalise the two legs' reach first,
and what the hips are left to answer for is whatever remains.

Both feet down: the floor is as far as the shorter leg can reach once its own
hip roll is counted, and the other leg makes up the difference by rolling
further out.

That is how the pose works rather than a fudge. A skater at full stretch has its
pushing leg out to the side precisely because it is straight, and a machine
standing with its feet apart has its hips open for the same reason. Take the
difference out of the knees instead and the stance has no width.

## MESH-17 — hip roll is its own frame, above the swing

Rolled first and swung afterwards, the whole leg tips outwards as one and its
foot lands exactly `cos(roll)` of the way down, which is what lets the planting
solve pick a roll and be right.

Roll the thigh itself instead and the swing happens in the unrolled plane, the
two rotations no longer commute, and the feet miss the floor.

## MESH-18 — the knee stands proud of the limb

It is how the reference art draws a knee, and it is the only thing keeping its
faces out of their planes: a joint the same width as the limb it sits on has
coplanar sides with it the moment the joint angle passes through straight, and
there is no depth buffer to sort that out.

## MESH-19 — the ankle cancels the hip roll

The foot stays flat to the floor whatever the leg above it is doing, both ways.
The three pitches up the chain cancel to nothing by construction, so what is
left of the hip above the ankle is the roll alone, and giving the ankle the same
roll back undoes it exactly. Without it a splayed leg lands on the outer edge of
its foot and drives the inner corner through the floor.

## MESH-20 — an idle machine lets its arms down

A machine with nothing locked and nothing in flight lets the whole chain unfold:
the shoulder stops tracking, the elbow gives up its right angle, and the guns
end up pointed at the floor. It is the only way to tell at a glance which of two
machines across the arena is about to shoot, and it costs nothing to read.

## MESH-21 — mirroring a sprite means reversing its corners

`POLYTEX` takes its texture coordinates from the projected corners, so a quad
always carries the whole tile however wide it is drawn. The way to mirror a
sprite is therefore to reverse the order its corners arrive in, which is what
`MECHA_QUAD_TEX_FLIP` switches. Two half-billboards side by side, one flipped,
are one sprite and its own reflection meeting down the middle.

## MESH-22 — the blade is geometry, not a billboard

It used to be an ordinary billboard: a bright square facing the camera, which
read as a shield held up rather than anything being swung. A close-quarters
weapon wants a shape with direction in it.

Built as two planes through the same axis, one flat and one upright, so it never
turns edge-on and vanishes — there is no camera in the geometry at all, which is
the point. Each plane is a tapering body and a point, and a short crossguard at
the hilt stops the whole thing reading as a spike.

## MESH-23 — the depth key, and the two cases the middle gets wrong

There is no depth buffer, so a quad is drawn either before another or after it,
whole. For most geometry the middle of the quad is the honest answer to which.
Two cases it is not:

- **A shadow lying on the floor.** Its middle can easily be further off than the
  middle of a floor tile it covers, and the tile is then painted over it,
  cutting the shadow along a tile edge that moves with the camera. Sorting the
  decal by its nearest corner and the ground by its farthest fixes it both ways
  round.
- **Broad horizontal surfaces**, the same argument from the other side: a floor
  tile stretching away under a machine standing on it has to be drawn first, and
  its far corner is what says so.

That second rule applies only to quads carrying `MECHA_QUAD_GROUND`, which the
arena sets and machines never do. Keyed on the normal alone it also caught a
car's own floor and roof — both horizontal, both wider than the threshold — so
the two halves of the body sorted against each other by whichever was broader.
Upright that happens to look right; upside down the two swap and the error
shows.

## MESH-24 — self-lit geometry is pulled forward in the sort

A blast centred on a machine intersects it, and per-quad sorting then lets some
panels paint over the fireball and not others — a fireball with a hole in it
that swims about as the camera moves.

Pulling the key forward by the sprite's own half-width, which is the radius of
the volume it stands for, sorts it as though it stood clear in front: everything
inside that volume is outranked, everything outside is not, so a shoulder well
clear of the fireball still occludes it. The narrower of the two edges is
measured, because a long thin tracer has no business claiming to be half its
length nearer than it is.

## MESH-25 — the sky dome radius is chosen against the arena

The floor is a couple of hundred metres across, so a dome at six hundred swung
by nearly twenty degrees as a player crossed it — the sky sliding rather than
the machine walking. At fourteen hundred it is a few degrees.

Puff size scales with radius, so pushing it out costs nothing but parallax, and
it is still four orders of magnitude short of straining a float. The retail dome
sits ten million units out, which suits a track renderer written around it and
does not suit this one.

## MESH-26 — the forest grows outwards off the boundary

The trees a machine can hide behind are boxes built from the same panels as the
cover. These are the other hundred and fifty: the game's own tree sprites,
upright and camera-facing, with nothing behind them. They do not collide, do not
block a shot and are not on the ground mesh. They are there so an arena with an
invisible boundary reads as a clearing in a wood rather than a field that stops.

Placement is a hash of the tree's index and the match seed, so nothing is stored
between frames and the same match always grows the same forest.

The draw is squared rather than uniform. A square scatter puts as many trees
five hundred metres away as fifty, which is a thin haze on the horizon and
nothing at the edge — and the edge is the point, since that is where the
invisible wall is. Squaring crowds them against the boundary and thins them
behind.

## MESH-27 — a tracer's streak keeps the weapon's colour

That colour is how a player tells whose fire is crossing the arena. A textured
quad draws the frame's colours and nothing else, so a plasma-skinned streak
would make every machine's beams the same blue. The head is small enough to read
as the glow at the front of the bolt rather than the bolt.

## MESH-28 — a blast opens fast and collapses

There is no alpha in an indexed frame buffer, so size is the only thing carrying
the shape of the blast. The earlier curve grew all the way to full scale at the
end of its life, so a blast covered the most screen on the last frame before
vanishing — which reads as the arena being blanked and restored rather than
something exploding. Peaking a third of the way in and shrinking from there
reads as a burst.

## MESH-29 — debris cools as it falls

The ramp runs from the pale gold at the top of the sky gradient back down
through orange into the deep reds at its zenith. The shared indices are not a
coincidence worth fighting: they are the one contiguous warm ramp the palette
has, they read as heat in either palette, and a particle walking them downwards
is a particle going out.

## MESH-30 — the jetpack flame is two mirrored halves

The fire tiles are drawn leaning one way, so a single one reads as a flame blown
sideways — wrong for something pointing straight down out of a jetpack. Two
halves meeting down the middle, the right-hand copy mirrored, cost one extra
quad and no overlap, so nothing is drawn twice into the same pixels and the
painter's order has nothing to decide.

______________________________________________________________________

## REND-01 — the mode carries its own font

ROLLER's HUD font lives in the retail sprite blocks, which the rest of this mode
deliberately does without: mechs, arena and effects are all generated rather
than loaded. A HUD that needed game data would be the one asset dependency in an
otherwise self-contained mode, so the mode carries a five-by-seven face as a
fallback. Each glyph is seven rows of five bits, most significant bit leftmost.

`minitext.bm` is the small face the race HUD prints speed and gear with and is
what the mode uses when it is present; `font6.bm` is the larger sprite face the
game announces things in, and is what the title and round banners want. The two
are not interchangeable.

Two things about the retail path matter. Glyphs are indexed through
`ascii_conv3` (or `font6_ascii` for the large face), where 255 means "no glyph"
and costs a flat four pixels of advance. And `prt_letter` scales through the
`scr_size` global rather than an argument, pre-multiplying the coordinates it is
handed — so drawing at `iScale` means setting the global and passing coordinates
that have *not* been scaled.

## REND-02 — the camera does not dodge occlusion

It used to: a segment trace to whatever it was looking at, and up to six
three-metre steps upward until the line came clear. That was always eager — it
swung the whole arena for one pillar — and it got much worse once the ground
itself began blocking that trace, because then every hill the player drove
behind heaved the camera into the air.

Virtual-On does not move the camera for this at all. It leaves the camera where
it belongs and turns whatever is in the way transparent, which keeps the frame
still and tells the player exactly what is happening. That wants a renderer that
can blend, so it is not written yet. Until it is, nothing happens, which is
better than the wrong thing happening quickly.

The floor clamp is not this and stays: keeping the camera out of the ground is
not occlusion avoidance.

## REND-03 — the chase rig scales with the machine

The chase is written around a machine fourteen metres tall, which is most of the
roster. The car is a sixth of that and would be a speck under a camera hung
fourteen metres up, so the rig scales.

Not all the way down: a car doing seventy metres a second needs to see further
ahead than two metres of camera height gives it, and the floor clamp is what
stops the view ending up in the bodywork.

## REND-04 — the camera follows the player's heading, not the enemy's bearing

It used to swing onto the bearing to the enemy at every range, so the view
turned when the enemy moved rather than when the player did — and with the
machine no longer squaring itself up outside knife range, the camera pointed
somewhere the machine was not.

## REND-05 — near-plane clipping keeps the polygon a quad

`game_render_quad_world` accepts quads only, so a vertex behind the near plane
is pulled forward along an edge that crosses it rather than the polygon being
split. This is what stops a floor tile the camera is standing on from smearing
across the screen when the rasteriser clamps its z.

## REND-06 — banks are resolved before anything is built

The mech mesh is the first thing to ask which banks are loaded, so answering
with last frame's result left the car in flat paint for its first frame.

The effect bank loads itself, because the first shot fired names it and the draw
path loads whatever a quad names. The car's skin has no such trigger — the mesh
will not name a bank it has been told is missing, and the bank stays missing
because nothing named it — so it is asked for explicitly, and only when there is
something in the fight to wear it.

## REND-07 — how the banks load, and what the loaders get wrong

Every bank is loaded by a routine the game already has; nothing here parses a
`.DRH`. What this owns is the part those routines are careless about.

Each calls `ErrorBoxExit` when its file is missing — taking the process down
rather than returning a failure — so each is probed first, and a bank that is
not there simply never becomes available. This matters most for
`LoadGenericCarTextures`: on a checkout with no retail data it would kill the
process instead of falling back, and falling back is the whole point. Every
effect still carries a palette index, so a mode with no bank draws what it drew
before.

Each uploads through `g_pGameRenderer`, the global the race sets up, so a mode
drawing on its own renderer gets the decompress and the sort but no upload. The
pixels are left in a global either way, so they are handed to the renderer that
is actually drawing.

The engine's own numbering is not exposed past that table: the track bank is
bank 0 while its tile count lives at `num_textures[19]`, and that is not a quirk
worth spreading through the mesh.

## REND-08 — the low byte means different things on different paths

`POLYFLAT` takes its colour from the low byte of the surface flags and routes
anything marked transparent through `shadow_poly`. On that path the low byte is
a shade *level*, not a colour, and `shade_palette` holds only 16 blocks — so the
value is masked. A bad colour is a visible bug; a bad read is not.

On the textured path the low byte is a *tile index*, which is the easiest thing
on it to get wrong: a colour left in those bits names a tile the bank does not
have, the renderer rejects it, and the quad quietly comes out flat.

`PARTIAL_TRANS` is what makes a frame a sprite rather than a black square: on
that path index 0 is skipped instead of written, and every effect frame is drawn
on index 0, with between a third and nine tenths of each tile background.

## REND-09 — POLYTEX derives its own coordinates, so corner order is the API

The legacy path works its texture coordinates out inside `POLYTEX` from the tile
index and the projected polygon; the track renderer passes zeroes on every
vertex and always has.

So the order the four corners arrive in decides how the tile lies on them, and
the arena winds its quads the other way round the face from the track. Nothing
else in the mode noticed: this renderer rejects back faces off the stored normal
rather than the projected winding, so a quad wound backwards still culls, sorts
and fills correctly, and every texture it had worn — grass, tarmac, concrete, a
plasma bolt — was near enough symmetrical to look right mirrored. Put lettering
on one and it reads backwards.

Geometry the mode builds itself is therefore handed over reversed. Geometry out
of the game's own files is not: it arrived already reflected by the frame change
that got it here. See [MESH-12] for the flags that decide the rest.

## REND-10 — the briefing's line budget

The game's smaller video mode gives this a 320x200 buffer, and at 200 pixels
there is room for exactly twenty-five lines. Anything that does not fit is lost
off the bottom, and the bottom is where the exit row lives.

Besides the rows themselves: two lines for the double-height title, one for the
result and a blank after it, a blank either side of the rows, the footer, and
one more as the margin the footer's glyphs need.

The controls used to be printed here, ten lines of them, which is most of why
the rows had nowhere to grow. They are on a page of their own now — still one
keypress away, no longer in the way.

## REND-11 — the sky is DrawHorizon, and the clouds are off

`DrawHorizon` paints the sky, the same routine the race uses: two flat fills
split by a line through the projection, blue above and a haze colour below. The
arena had a nine-band sunset gradient before this, which looked well enough on a
still frame but was the mode inventing a sky the engine already had, and it
could never carry clouds.

What `DrawHorizon` reads, it reads from globals. Most are already written by the
time this runs — `game_render_set_camera` and `set_projection` push `viewx`, the
vk basis, `xbase`, `ybase`, `scr_size` and `VIEWDIST` through for exactly this
kind of legacy path — so what is left is elevation, tilt and colour.

The clouds are disabled. That dome is real geometry: forty quads placed ten
million units out, in the track code's coordinate system where the up axis is Z
rather than Y, submitted through the renderer's cloud subdivision path. Handing
that path an arena camera makes it subdivide quads that size until the frame
stops arriving — a run that takes a fifth of a second takes minutes. Getting
them in wants the dome rebuilt against the arena's own scale and axes, rather
than the basis swapped underneath it.

## REND-12 — why these palette indices

The names live next to the code that uses them; the palette table is where those
indices get colours for the case where no palette has been loaded.

The indices themselves were chosen by matching the intended colours against the
retail palette, so a player with the game data sees roughly the same picture
from their own `PALETTE.PAL` rather than whatever sits at an arbitrary index.
That palette is mostly a grey ramp between 115 and 143 with saturated primaries
higher up, which is why the arena reads as grey structure with coloured tracers.
Some indices are deliberately shared — a tracer and a HUD accent — because the
mode paints into a palette it does not own all of.

The sky bands climb steadily in brightness whether resolved through the retail
palette or the fallback: 221-230 is a dark-to-bright red ramp, 167-171 orange,
204-207 the top of a yellow. **231 is deliberately unused.** It is the obvious
brightest red to finish on, and it is also the low-armour warning: a sky
matching the colour of "you are about to die" hides it.

## REND-13 — the recoloured effect banks

The game's plasma frames are blue and there is only one set, so every machine's
fire came out the same colour. In a crossfire you could not tell whose shot was
whose, which is the one thing a shot has to say.

A tint is built by walking each frame's palette indices onto the nearest colour
the palette has in the wanted hue at the same brightness, and uploading the
result as a bank of its own — so the recolour is real pixels rather than a
shading trick the rasteriser does not have. Index 0 stays index 0: that is the
transparent key and everything about these frames depends on it.

Slots 20 and up are used. The engine's texture-count table only ever speaks for
0, 17, 18 and 19, so the rest are free for the arena to take, and the count is
set alongside the upload.

______________________________________________________________________

## SIM-01 — an empty gauge takes the thrust, not the legs

The gauge gates four things. Only three of them are thrust:

| action               | needs gauge |
| -------------------- | ----------- |
| jump takeoff         | no          |
| hover (holding jump) | yes         |
| dash, air dash       | yes         |
| jump cancel          | no          |

Leaving the ground is the legs' work, so an empty machine still jumps and still
cancels out of the jump. What it loses is the thrust to hang in the air with and
to dash with.

The takeoff used to be gated on the gauge, which meant an empty machine could
not get airborne at all — and so could not jump-cancel either, since the cancel
needs a jump to cancel. The lockout cooldown on spending the last of the gauge
is deliberate and stays; what it should cost is hover and dash, not the ability
to leave the ground.

The cost is charged only when there is gauge to charge. Otherwise a locked
machine mashing jump would spend the recovery it needs in order to unlock, and
never climb back out.

______________________________________________________________________

## ARENA-01 — palette indices are tuned, not derived

ROLLER's flat polygons take a palette index in the low byte of the surface
flags, and that palette comes from the game's own data. Every arena and mech
colour goes through a name defined in `mecha_arena.c` or `mecha_defs.c`, so
retuning against a different palette is a one-file edit. See [REND-12] for how
the indices were chosen.

The meadow takes its greens from 244-255, a black-to-green ramp in the game's
palette, and its browns from 48-63, rather than borrowing tracer colours; stone
stays one of the greys. A field checkered green against grey read as a chess
board, which is the one thing a meadow must not look like.

## ARENA-02 — the projectile trace stride

The stride decides whether a bullet can step over a hillside between two
samples. A metre against hills forty metres wide leaves no gap to step through,
and the fastest shot in the game covers seven metres in a tick, so a segment is
eight samples at worst. The step cap exists only so an absurdly long query
cannot become an unbounded loop.

## ARENA-03 — the ground begins slightly below where it is drawn

The gun car's weapon floats six metres off its right flank, so parked across the
steepest hillside in Coldwater Meadow its muzzle dips about three centimetres
into the slope. Sweeping every machine over every square metre of that arena at
sixteen facings found that in 24 of 2.28 million samples — rare, and unplayable
where it happens, because a shot that begins underground detonates at the
muzzle.

So the ground is treated as beginning a little below where it is drawn. Twenty
centimetres is six times the worst graze measured and small enough to be
invisible: a shot stopping into a slope stops a fifth of a metre late along the
normal, on hills that stand twenty-six metres.

## ARENA-04 — hills are raised by hand, and are meant to be faceted

Pick a middle, a reach and a height, and every grid corner inside comes up by a
cosine of its distance. Angular, because the corners are all the ground has: a
hill built this way is a dozen facets, which is what it should look like.

## ARENA-05 — a ramp is a truncated cone, and every part of that is load-bearing

- **Straight sides** are one constant grade, which is what a ramp is. A smooth
  shoulder launches nothing, because by the time the machine is fast the slope
  has flattened out under it.
- **A flat top** is somewhere to land and fight, and it stops a walker hopping
  the apex — a cone that comes to a point drops out from under anything crossing
  it, boost or no boost.
- **The edge between them** is the lip the launch comes off.

## ARENA-06 — retail tiles do not replace the palette entries

Track-bank tiles are used whenever the retail data is installed. The palette
entries stay as the fallback, so an arena still comes up on a bare checkout and
draws the same checkerboard, only flat.

Each arena takes a different surface so they do not read as one place with the
furniture moved: a yard in tarmac, a field in grass, a plate floor in worn
metal.

## ARENA-07 — the meadow, and why it has no visible walls

Eight sides, hills you can be thrown off, nothing built on it. The ground is not
magnetic, which is the point: boost up one of these and you leave it at the top.

The boundary is still there and still stops a machine, but what is drawn past it
is more forest — ground running out to twice the arena again with trees on it —
so the edge of the fight is a place the fight stops rather than a place the
world does. A wall in a meadow is a fence around a field.

## ARENA-08 — MERIDIAN CROSSING puts a city in the middle of that

The biggest ground the mode has: an octagon four hundred and twenty metres to a
side face, near enough twice the meadow. Almost all of it is meadow — the same
rolling, non-magnetic grass with hills, rocks and a wood, and forest drawn past
the boundary.

What is different is the middle. Three blocks by three of tall building sit at
the centre and nowhere else, so the city is a place you go into rather than a
place the arena is. The streets do not stop at the last building: they run
straight out to the boundary both ways, which is what stops the city reading as
nine boxes dropped on a field.

## ARENA-09 — the tower roof

No walls at all — walk off it and you are falling — with a raised hexagonal
tabletop in the middle and a block in each corner to fight around. The tabletop
is sloped rather than sheer, so it is high ground you take rather than a wall
you go round.

The edge runs a long way down. It is the top of a tower, and a tower that stops
six metres below its own roof is a table.

## ARENA-10 — terrain is a height per grid corner; the tabletop is not

The cell a point falls in is found by index and the height inside it
interpolated between four corners, which is what makes a slope a slope rather
than a staircase. Everything else about the ground — the pit, whether a machine
sticks to it — lives in the cell's surface word, exactly as a track chunk's
does.

The tabletop is answered rather than baked. Hills go into the grid because they
are meant to be lumpy. A tabletop is a made thing with six straight edges, and
rounding those to the nearest grid corner would lose the only thing that says
somebody built it. The ground mesh picks the computed version up for free,
because it samples the same query at every corner it draws.

## ARENA-11 — a platform is a floor from above and nothing from below

Off the edge there is no floor at any height, and underneath it there is none
either. Without that second half, a machine that has fallen past the edge and
drifted back beneath the roof pops up through it — the same mistake as walking
into the side of a box and being teleported onto its roof.

## ARENA-12 — the ground query is what makes terrain stop a bullet

The floor used to be the `y = 0` plane, which was true of the first three arenas
and nothing since. A shot crossing Coldwater Meadow passed clean through every
hill it met, and one fired across Tower Seven went through the tabletop, because
neither is at zero.

The terrain query is what the ground mesh is built from, so asking it here is
what makes the shape you can see the shape that stops a bullet.

## ARENA-13 — grip, in the race game's own fourteen grades

Whiplash keeps a table of surfaces (`loadtrak.c`, `tSurface surface[14]`) and
stores an index into it per track chunk, separately for the centre lane and each
shoulder. What the physics reads off it is `iGripModifier`, running 100, 95, 90,
85, 80, 75, 70, 65, 60, 55, 50, 40, 30, 20 — then adds the engine's own grip
bonus and divides by how wrecked the car is:

```
(modifier + engine.fGripBonus) / (2.5 - health * 1.5)
```

clamped to `fMaxGripLimit`. Only the first term is a property of the ground, so
only that is here: the engine bonus is the machine's own grip figure in the
roster, and damage is accounted for elsewhere.

Written as a fraction of the best surface, so grade zero is 1.0 and costs
nothing. Every track the race game ships is laid at the maximum bar one bonus
track, which is why an arena that says nothing gets the best of it.

## SIM-02 — damage particles come off the machine's own generator

The race game's `dospray()` runs every frame over every car and throws a
particle when a die roll beats the car's health factor — the worse the car, the
more often it lands, so a machine does not switch from clean to smoking, it gets
gradually dirtier. Whiplash has one damage tier; this has two, so a machine that
is merely hurt smokes and one nearly gone burns as well.

Every draw comes off the machine's own RNG, not the world's. Written the other
way first, this took three extra numbers a tick out of the shared stream and
moved a rooftop fight off a cliff. The particles were fine; the fight was simply
no longer the same fight.

The same precedent applies to anything else cosmetic that needs randomness.

## SIM-03 — guard stops melee only

Guard is a posture for answering something that has closed the distance, not a
shield. Standing in it against gunfire has to lose, or the fast boost refill it
already grants would make it the only thing anyone ever does.

The helper returns 1.0 for every case that is not a guarded melee hit, so
callers can multiply unconditionally.

## SIM-04 — a downed machine takes one blow, but a volley is one blow

Three ways to be off the table: destroyed, invulnerable through a rise, or lying
on the floor. The last is the point — a knockdown should be a reprieve, not an
invitation to empty a magazine into something that cannot move.

The reprieve starts on the tick *after* the knockdown, not on the hit, so a
single volley resolves in full. Buckshot is seven projectiles and one trigger
pull; if the first pellet to arrive closed the door on the other six, a shotgun
would do a seventh of its damage exactly when it was working.

## SIM-05 — the lock is live only inside a cone

`mecha_update_target` picks who; this decides whether the lock is live. It holds
while the target sits inside a generous cone of the machine's own heading and
drops once it has been outside for the grace period, at which point the
auto-turn stops following and every weapon fires straight down the barrel.
Boosting or jumping snaps it back from any angle, which is what makes those
worth gauge for reasons other than distance.

It reads `byMove` as movement left it last tick. One tick of lag on a dash that
lasts dozens does not matter, and running before movement is what lets the
facing update act on a fresh lock.

## SIM-06 — the car's steering is Whiplash's, both halves of it

Whiplash works the lock out as `input * (1 + (top - speed) / k)` and then throws
it away entirely below the car's own steering speed limit (`control.c`). Both
halves are here: the lock is widest just off a standstill and narrows as speed
comes up, and a car that is not moving cannot be pointed at all. That second
rule is why the gun car has to keep moving to point at anybody, which is the
whole of how it fights.

It reads the stick as much as the turn axis, because a car has no strafe for the
stick to mean anything else by.

The exact form in `control.c` is `input * (1 + (360 - speed) / 60)`. 360 is that
game's reference speed, so the divisor is a sixth of it and the bonus runs from
seven times the input at a standstill to nothing flat out. Written against the
machine's own top speed that is a gain of six on the slack, which is the same
curve.

**There is no ceiling on any of it.** The yaw is simply accumulated: nothing in
the race game limits how far a car may come round, which is why one can be spun
through a whole circle on the stick in a drift. Grip decides whether the car
goes where its nose has gone, and that is a separate number.

**Reverse flips the steering, and a drift must not count as reverse.** Whiplash
decides this on `fFinalSpeed`, the car's signed speed along its nose, and on a
track that is the only speed it has — position is advanced straight along the
heading, so a Whiplash car cannot travel at an angle to where it points. This
one carries a real velocity vector, and in a drift that vector swings more than
a quarter turn off the nose. A test on the dot product then decided the car was
reversing and flipped the steering, which stopped the slide dead. That was the
"rotation limit": not a clamp anywhere, but the stick fighting the spin halfway
through it.

Reverse is a third of forward top speed and a drift is fast, so the car's own
reverse speed separates the two cleanly.

That alone is not enough, though: it flips on any backwards drift, which is most
of a handbrake turn, so the wheels swap hands halfway through and the car fights
itself. The throttle alone is no better — braking and reversing are the same
lever, and a brake at speed is not reverse. The condition is all three: the
lever down, the car going backwards, and slow enough to be reverse rather than a
slide.

## SIM-07 — hitting a wall

The push the arena applied to get the machine back out is the surface normal,
which is all a bounce needs. At walking pace the machine leans on the wall and
the speed into it is dropped — pressing into a corner should not build up a
shove that fires you out of it later. Carry a boost into the same wall and it
comes off, the way the race game's cars do, and the burst is over.

## SIM-08 — a committed dash can still be steered

Two ways in. Boost again while pushing back against the direction you left on
and the dash restarts the other way — the cancel, and the reason a committed
dash is not a trap. Or let the stick go and tap a new direction: the burst turns
without a second press, which is the crossing step, and the release is the whole
cost of it.

## SIM-09 — drive towards a velocity, split into along and across

The velocity a machine already has is split into the part pointing where it is
being asked to go and the part across that. The first is pushed towards the
speed asked for at the machine's drive rate; the second is bled off at its grip.

That single split is what makes a heavy machine slide out of a direction change
and a light one snap round: the sideways component is the skid, and grip is how
fast it stops being one.

The direction must be unit length, or zero to mean "nothing asked for", in which
case everything is treated as sideways and simply brakes.

## SIM-10 — the drawn attitude is never read back

Nothing in the attitude block is read by movement, collision or the firing
solution. That is deliberate and it is what makes it affordable: a machine can
be squatting, ringing and rattling at once because none of the three has to
agree with the others about anything.

**The input tilt goes opposite ways in the two games**, which is the only
interesting thing about it. A car leans out of the corner because that is what
weight transfer does to a body on springs; a robot leans into it because a
machine that has started moving before it has moved feels quicker to the hands.
Neither is more than a couple of degrees.

**The ground contour is wheels-only.** A car sitting perfectly flat while it
drives up a hill gives away that the hill is a height field rather than a
surface, so the machine asks what the ground does across its own footprint —
fore against aft for the climb, left against right for the traverse. A walking
machine has feet and a gait to put them down with, and tilting the whole of it
would fight both. It is also terrain-only: standing on the roof of a box, the
height field underneath describes ground the machine is nowhere near, and
following it would lean the car over on a flat roof.

## SIM-11 — the ground query asks from the higher of two positions

A platform answers a height query only to something near enough above it; below
the lip it is a wall, not a floor, which is what stops a machine underneath a
roof popping up onto it.

A jump cancel falls at a hundred and twenty metres a second — two metres a tick
against a lip of one and a half — so a cancel from high over Tower Seven stepped
straight past the roof in one tick, was told there was no floor, and fell to its
death through solid ground. Measured: -449 m and dead before, resting on the
tabletop at 9 m after.

Taking the higher of where the feet were and where they have got to means the
query sees the surface the machine was standing over when the tick began.

## SIM-12 — downhill slopes are rolled down, not fallen down

The contact rules only see a machine that has sunk to or below the ground. Going
downhill it never does: the ground drops away faster than one tick of gravity
follows, so the machine is left hanging a fraction of a metre up, falls, lands,
and is hanging again — an invisible staircase.

A machine that was in contact when the tick began and is over ground that has
merely sloped away is put back on it, then falls through the ordinary contact
rules like anything else.

Three conditions stop this gluing a machine to the world: it must have been in
contact already, so nothing in flight is caught; it must not be climbing, so a
launch off a crest is never undone; and the ground must have sloped rather than
ended — past one in one it is a cliff, not a hill, and driving off it should
fly.

## SIM-13 — leaving the ground off a ramp, the way the race game does it

A car in Whiplash is held to the road by the surface being magnetic. Where it is
not, the game compares where the car's own momentum would put it against the
height of the ground under it, and if the ground has dropped away the car is in
the air.

The same rule from the other end: on a surface that does not hold you, the rate
the ground rose under you this tick is a real upward velocity, and when the
slope runs out you keep it. So a machine that walks up a hill is glued to it —
the climb is slow and the threshold sees to that — and one that boosts up the
same hill leaves at the top.

## SIM-14 — two ways to be gone that are not damage

A pit in the race game is a surface like any other: it answers a height query,
it is simply flagged as a pit and not drawn, so a machine standing over one has
fallen *in* rather than fallen through. And below the kill plane there is
nothing at all, which is what becomes of anything that walks off an open arena.

## SIM-15 — one gun, three triggers, one magazine

A machine carrying a single weapon still has all three slots, so it plays and
reads like everything else on the roster. But they are three loads for the same
gun, not three guns, and a magazine that could be stretched by rolling across
the other two triggers would not be a magazine. Every round spent is spent out
of all of them, and they run dry and reload together.

Which makes the choice a real one: nine rounds, each either buckshot, a lance or
a shell, and nothing about picking the third stops the first two costing exactly
as much.

## SIM-16 — shots can shoot each other down

Two shots that meet are worth what they do: within a sixth of each other they
trade, both gone, and outside that the heavier one carries on unchanged. It is
what makes a siege shell worth the wind-up and a spread worth firing at one, and
it is why a wall of fire is a wall rather than a suggestion.

Anything carrying a blast goes off where it was stopped rather than blinking
out, so shooting a bomb down is a decision about *where* it explodes rather than
whether it does.

## SIM-17 — the gun car can run somebody over

It is the only thing that machine has at close quarters — it carries no melee
row at all — so it has to hurt. Charged on the speed the two are closing at
rather than on its own speed: driving alongside somebody is not a ram, and a
head-on is worse than catching them up. Both machines can be doing it at once,
and neither can do it to a friend.

______________________________________________________________________

## DEF-01 — the lock is breakable, and getting it back is harder than keeping it

Weapons aim themselves at whatever is locked, so a lock that can never be lost
means the fight is decided entirely by the feet. Holding it is a skill instead:
it survives while the target is inside a generous cone of the machine's own
heading, and once it has been outside for the grace period it drops — the
auto-turn stops following, shots fire straight down the barrel with no lead, and
missiles launch unguided.

On its own it returns only when the target is well inside the much narrower
reacquire cone. The quick way back is to boost or jump, either of which snaps it
on from any angle — which is what makes those two worth gauge beyond the
distance they cover.

## DEF-02 — guard cuts damage by 85% and stagger by half

It was a crouch, and it still refills the gauge fastest and selects its own row
of weapons. What it adds is a hard answer to being closed on. Melee only, for
the reason in [SIM-03].

Stagger is cut by half rather than by the same 85%, so a guarded blade still
rocks the machine it lands on. Reading the swing should win the exchange
outright; it should not make the swing feel like nothing happened, and leaving
some stagger on is what keeps a blade rush worth committing to even against
someone who saw it coming.

## DEF-03 — the jump cancel is one move in two halves

A jump snaps the lock on, which makes going up the reliable way to find an
opponent who has got behind you. Guard in the air then drops the machine
straight down instead of riding the arc out, and the landing leaves the turn
rate off its leash for a moment — long enough to come down facing the other way.
Up to find them, down to face them.

A cancelled jump does not fall, it is dropped. Forty-six metres a second was a
brisk fall; at a hundred and twenty the machine is simply on the ground, which
is what makes the cancel a way out of an arc rather than a slightly faster way
of finishing it.

See [SIM-01] for what an empty gauge does and does not take away.

## DEF-04 — ground tiles were chosen by measuring the banks

`track1.drh` holds 246 tiles and most are track furniture — kerbs, arrows, lane
markings, a sponsor emblem — none of which survives being tiled across a floor.
What a ground surface needs is uniformity, so the candidates were ranked by the
standard deviation of their luminance and the flattest taken: 54 and 55 are the
same grey a shade apart, 205 and 210 clean grass, 12 and 13 concrete and rust.

A pair has to be neighbours in appearance as well, because the two alternate
across the floor the way the two palette entries do. Pairing plain tarmac with a
lane-marked tile turned the arena into a chessboard instead of a surface.

## DEF-05 — the auto-turn is confined to knife range

It used to run at every range, which quietly took the steering away: there was
no distance at which a player chose where the machine was facing. Confining it
keeps the one place it earns its keep — a melee exchange is too fast to aim by
hand — and gives the rest of the fight back.

The lock is unaffected either way; it still decides whether the weapons lead,
and holding it at range now means actually keeping the enemy in front of you.

A move that re-centres holds the machine on its lock for long enough to complete
the turn and let a shot go, and no longer: the auto-turn coming back on
permanently would undo the point of confining it.

______________________________________________________________________

## TYPE-01 — surface bits are the engine's own values, duplicated

`SURFACE_FLAG_PIT`, `SURFACE_FLAG_SKIP_RENDER` and `SURFACE_FLAG_NON_MAGNETIC`
out of `types.h`, which `mecha_types.h` cannot include because nothing in the
simulation may reach into the engine. `mecha_render.c` includes both and asserts
at compile time that they agree.

## TYPE-02 — silhouette multipliers exist so archetypes read across the arena

Everything the mesh builds is scaled off `fHeight` and `fRadius`, which made
every machine the same shape at a different size — the archetypes existed only
in the stat block. These let a siege platform read as one: heavy shoulders,
thick limbs, an oversized gun in each hand, a head sunk into the chest, against
an interceptor that is all narrow torso and thin legs. Zero means one, so a
machine that never sets them still builds.

## TYPE-03 — the three mass figures are absolute, not multiples of walk speed

The race game's cars do not set their velocity, they drive it: a grip figure
limits how fast sideways motion is corrected, whatever is left decays on its
own, and steering authority falls off as speed rises. The same three ideas are
what make a machine here feel like it has mass rather than a cursor.

`fGrip` kills sideways velocity per second — high is crisp, low slides wide.
`fDriveAccel` is how hard it pushes towards the speed asked for. `fBrake` is how
fast it sheds speed with nothing asked of it.

All three are in metres per second squared and deliberately **not** multiples of
the machine's own walk speed. Scaling them that way normalises out the very
thing they exist to express: every machine then takes the same time to gather
itself, so the interceptor — being simply faster — slides the furthest, and the
siege platform comes out the nimbler of the two.

## TYPE-04 — what the attitude fields are, and where each comes from Whiplash

All of it is cosmetic, all in the shared 14-bit circle, none read back. Whiplash
keeps these in independent pieces and sums them at the last moment in `car.c`;
the same split is kept because they genuinely do not interact.

- **Input tilt.** Whiplash moves it *against* the steering
  (`iRollDynamicOffset`, wound at `iRollResponseRate`, clamped at
  `iMaxRollOffset`) so a car leans out of a corner. Virtual-On's robots lean
  *into* the input. Two degrees either way: you would not name it if you saw it,
  and you would notice if it went.
- **Air pitch.** The nose follows the velocity vector with no ground under it —
  Whiplash derives airborne `nPitch` from `atan2` of vertical against horizontal
  speed, so a car launched off a crest points where it is going.
- **Ground contour.** Pitch and roll of the slope actually stood on, sampled
  across the footprint and eased rather than snapped. See [SIM-10].
- **Landing wobble.** Two amplitudes decaying while a phase runs, giving a
  damped cosine about both axes. Whiplash seeds them from the attitude held at
  the moment of contact, which is why a flat landing barely registers and one
  off a hillside rings.
- **Body shake.** White noise on all three axes, resampled every tick, scaled by
  how hard the machine is working — in the race game, road speed times damage
  divided by the engine's `iStabilityFactor`.

## TYPE-05 — the leg facing is the one piece of animation state the sim owns

The torso holds the aim while the legs follow the line of travel, so a machine
strafing across your guns is walking sideways rather than sliding with its
shoulders square. The sim owns it because it is smoothed over time and the mesh
is built fresh every frame.

`fFightMix` is similar in spirit: how much of a fight the machine thinks it is
in, held while it has a lock or is shooting. Nothing reads it back, so a machine
animating out of a fighting stance has never stopped fighting.

## MESHH-01 — the effect bank's frames, and what doubles as what

Frames 8..12 are the sky's cloud puffs — `horizon.c` picks one of those five for
every quad of its dome, and so does this mode. They double as the glow on a
plasma bolt, because the bank has no bolt art of its own: at bolt size a soft
blue puff reads as plasma. 0 and 21..23 are smoke, 1..3 the start lights, 4..7
flame, 13..20 the blast.

## MESHH-02 — the quad buffer is caller-owned and fixed

Nothing in the mode allocates, so a frame that would overflow stops adding
geometry rather than growing or crashing. `iDropped` records how much was lost
so a debug overlay can say so.

The mesh layer never sees a texture either: it names a bank and a tile and the
renderer resolves them, which is what keeps the file free of the engine.
Anything unresolvable falls back to `byPalette`, so the same mesh works with or
without the retail data.

______________________________________________________________________

## MODE-01 — the palette dance on entry and exit

Everything the arena draws is generated, but the frame is still an indexed
buffer presented through `pal_addr`, and `pal_addr` is only filled in by the
states that load the retail data. Coming straight in on `--arena` skips all of
those, so without the mode installing its own the geometry rasterises correctly
and then presents as a black screen.

The game's own palette is loaded first when it is installed. The fallback table
defines about thirty indices and fills the rest with one neutral grey, which is
fine for geometry the mode colours itself and wrong for anything out of the
retail banks — those tiles and frames are drawn in the retail palette's indices,
so resolving them through the fallback turns a tarmac surface into noise.

**Two ownership traps, both of which crashed the process.**

`setpal` owns `pal_addr`: it frees whatever was there, loads the file, and
points `pal_addr` and `pal_selector` at its own buffer. The mode used to repoint
`pal_addr` at the static `palette[]` array afterwards, on the strength of a note
in the GPU renderer saying `setpal` leaves it alone — true of the original, not
of this one. The cost was not a wrong colour: the loaded buffer leaked, and the
next `setpal` anybody called (the main menu's, on the way out) took the static
array's address to `free()` and aborted. That was the crash on "exit to
whiplash".

Presentation reads `pal_addr`, so the mode's own table has to go there, and that
table is static. The selector is how the engine says whose memory this is:
`setpal` frees `pal_addr` only when the selector is non-negative, so marking it
-1 while the arena's table is installed makes the static safe to leave there.
Both go back on the way out.

## MODE-02 — the renderer is created if absent and never torn down

`g_pGameRenderer` is created by `play_game_init()`, which only runs once a race
starts. Coming in on `--arena` leaves it NULL, and `game_render_get_mode()`
dereferences it without a guard, so the mode stands one up itself the way
`play_game_init` does.

It is not torn down on the way out. Doing so nulled `g_pGameRenderer`, which is
the renderer the menus and the race then reach for, so leaving the arena crashed
the moment anything else tried to draw. It is the same renderer `play_game_init`
would have built; handing it on is the point of having built it.

## MODE-03 — the fixed tick, and the catch-up cap

The simulation runs at a fixed 60 Hz whatever the display does, so a match plays
identically anywhere and stays reproducible from its seed. A frame that took too
long catches up over a few ticks and no further: without the cap, one long stall
— a window drag, a breakpoint — is paid back as a burst of simulation the player
cannot react to.

______________________________________________________________________

## TEST-01 — what the headless render test is for

`mecha_sim_test.c` covers the simulation, which needs nothing but libc. This
covers the other half: a real `GameRenderer` in software mode with no GPU device
and no window, rendering arena frames into an indexed buffer, and asserting that
geometry, effects and HUD all reach pixels. That is the part no unit test and no
compile check can speak for.

Given an output directory it writes the frames as indexed PNGs so the layout can
be looked at rather than only asserted about. They are dumped through the
palette the frame was actually drawn with, when there is one: dumping through
the mode's fallback regardless is what made these previews lie, since retail
tiles and effect frames are drawn in the retail palette's indices and showed as
noise for surfaces that were fine on screen.

## TEST-02 — assertions that must be one-directional

Two places where the obvious two-way assertion is wrong:

- **Palette coverage** is checked forwards, from the constants, not backwards
  from the frame. `shadow_poly` emits indices out of the shade table that the
  mode never chose, so "everything on screen is one of ours" is false and
  asserting it only produces failures.
- **The HUD's colours** are only the mode's while the mode is choosing all of
  them. The retail font brings its own indices, so with it loaded that assertion
  says nothing — and would amount to asserting the font failed to load.

## TEST-03 — the blast is measured differently on each path

Drawn from the game's own texture bank, the blast paints none of the flat path's
palette index, so a count of that index is legitimately zero and the size bound
belongs to the other path. Without the bank — a checkout with no retail data,
which is how CI runs — the flat particles are what is on screen and their size
is what is worth pinning.

What proves the bank frames reached the screen is the opposite test: the bank's
tiles are drawn in retail palette indices, mostly ones this mode never paints
with, so pixels the mode's own palette does not define can only have come from a
sprite. It is also why the dumped PNGs look empty on that path — written through
the fallback palette, those indices resolve to neutral fill.

The flat path's colour is not required to vanish either: the machine's visor is
painted in it, so counting that index was only ever an upper bound on blast
size.

The bound itself: the explosion is an opaque billboard whose scale is a
half-extent, so an over-large figure paints a slab across the middle of the
screen on the frame the player most needs to read. Measured against a recorded
match, the original covered 17% of the play area at its widest.

## TEST-04 — the numbered panel render

The plan's fifty polygons are the first fifty quads the mesh puts out, one from
each, so a quad's place in the list is the polygon's number.

Only panels facing the camera are numbered, or the far side of the car writes
over the near side. Which those are is read off the sign of the projected screen
area: all fifty share the plan's winding, so those turned towards the camera
come out one sign and those turned away the other. That needs no view on how the
normals ended up pointing in this frame.

Nearest panel wins the space. Without that the roof and the tail, whose middles
project into the same corner of the screen as the windscreen, write their
numbers over the panels being asked about.

Shots are named for what the camera looks at, which is the far side of the car
from where it stands: the nose points +Z, so the camera out at +Z sees the
front. Flanks are named for the axis they face — which is the driver's right is
not something the geometry says.

## TEST-05 — the briefing has to fit in 320x200

The footer is the last thing drawn, so anything running off the bottom took it
first, and the exit row sits just above it. Twenty-five lines of this font is
the entire buffer. See [REND-10].

## TEST-06 — what the AI duel probes can and cannot assert

**Lock-held fraction is bounded loosely, on purpose.** Both halves have to be
true: the pilot must lose a lock sometimes, or the mechanic does not exist in
its hands and it is quietly privileged over the player; and it must hold one for
most of a fight, or it has no idea how to fight and the skill levels measure
noise. Everything that fights at range holds a lock better than nine tenths of a
fight. Kira sits near two thirds and belongs there — the close quarters machine,
spending the fight at the distance where anything moving sideways leaves the
cone. Asserting the rangefighters' figure would assert that every machine fights
the same way.

**The stand-in player has to steer.** It did not used to: a locked machine
squared itself up at any range for free. With the auto-turn confined to knife
range, a scripted opponent that never touches the stick spins away from the
fight, and the probe then measures how often the computer pilot wandered into
the fixed cone of someone who cannot turn — which ranks a decisive pilot as the
one that takes the most fire.

**Damage absorbed is deliberately not asserted on.** It reads as a skill measure
and is not one: what a pilot takes depends on how long it leaves its target
alive, so a better pilot ending rounds faster cuts its exposure and a worse one
wandering out of the fight cuts its exposure too — the two ends meet in the
middle. Measured across twelve duels the three come out within a few per cent in
no reliable order. An assertion that passes by one per cent is a future failure.
Still printed, because it is worth seeing.

**Recent-shove memory, not instantaneous stagger.** Asking whether stagger was
above zero on the exact tick a machine crossed the line is a different question:
stagger bleeds off at fifty-five a second, so a machine hit hard at the far end
of a slide arrives with none left and books itself down as having strolled. Two
of six seeds did exactly that, each after being shot the whole way across the
roof.

**The rooftop bound is a rate, not zero.** See [AI-05] for the three states in
which the pilot has no steering left to decline anything with.

## TEST-07 — measure along the motion, not along a world axis

A machine faces whatever it has locked, so "forward" is wherever the fight put
it. Measuring against +Z reported zero for all three machines and looked for a
moment like the physics had stopped working.

The two mass levers are measured separately. Grip decides how much of the old
direction survives being asked for a new one, so it is measured by turning
*across* the motion — never by reversing along it, where the sideways component
is zero and grip is never consulted. Drive acceleration decides how long obeying
takes, and reversing is what measures that.

Orderings rather than figures, since the walk speeds these play out at move
whenever the roster is tuned.

## TEST-08 — the inward-facing car body is the check on the axis negation

The race game's frame is right-handed and this one is not, so swapping the three
axes without negating one builds the car's mirror image: same silhouette, wheel
arches and exhausts and both flanks of the livery on the wrong sides. Negating
the lateral axis puts it right, and a reflection reverses a winding — so a
correctly reflected body is one whose panels all face inwards. Drop the negation
and all fifty turn round, which is what this catches, because nothing about the
car's outline would. See [MESH-09].

Silhouette is asserted as an aspect ratio rather than an absolute size, because
size alone is not silhouette: a machine that is merely bigger still reads as the
same machine. See [TYPE-02].

## SIM-18 — a car rolls off a cambered launch, and may land on its roof

Whiplash's own mechanic, in three parts (`control.c`):

- **At launch** (6473): `iRollMomentum += chunk.iRoll * fFinalSpeed / 720`. 360
  is that game's reference speed, so at full speed the momentum is half the
  camber per tick at 36 Hz. The same rotation per second at 60 Hz is three
  tenths of the camber against the machine's own top speed, which is
  `MECHA_CAMBER_SPIN_GAIN`.
- **In the air** (2501): `nRoll += iRollMomentum` every tick.
- **At landing** (3216): roll inside `4096..12288` — a quarter turn either side
  of level — is an ordinary touchdown and the roll is zeroed. Anything else sets
  `iStunned = -1`, zeroes the steering and parks the car at `0x2000`. Here that
  is a knockdown.

The arena has no track chunks, so the camber is the ground-contour roll already
sampled under the wheels. The spin is kept up to date while the wheels are down
rather than computed at the moment of launch, so what carries into the air is
the figure from the surface actually left.

The landing is judged on the tick the wheels touch, not off `byMove`: the
wheeled path sets the car back to `MECHA_MOVE_STAND` before the shared landing
code runs, so there is no JUMP state left to key on by then.

A car that lands upside down keeps what it arrived with and slides on its roof.
Zeroing the velocity made it stop dead, which reads as hitting a wall rather
than going over.

## AI-08 — the pilot presses forward and takes the high ground

Three changes, all because the pilots read as hiding:

- **The station-keeping band is narrow and sits inside the preferred range**,
  and its neutral case walks forward rather than holding position. A pilot
  parked at exactly the range its weapon likes never arrives, and never makes
  the other one move.
- **The guard-and-refill clause needs the gauge to be nearly spent**, not merely
  low. At a third of a gauge it fired constantly, which is what put these pilots
  behind a box for most of a fight.
- **Height is taken on purpose rather than at random.** A box or a building
  answers the ground query at its roof, so something to stand on is ground ahead
  that is well above the ground here and within jumping reach. Looking along the
  line to the target means the thing it climbs is the thing between them, which
  is the one worth being on top of.

The lock-held assertion in the tests moved with this: a pilot that presses
forward keeps the enemy in front of it, so an archetype that never breaks lock
is doing its job. Breaking lock is now asserted across the roster rather than
per machine. [TEST-06]

## SIM-19 — a car in the air bounces off what it hits

Whiplash reflects the approach speed on contact, charges damage for it at
`0.005` per unit, and negates `iRollMomentum` so the spin turns the other way
(`control.c`, the airborne wall cases). A car that clips something mid-flight
arrives somewhere else spinning the other way rather than stopping dead against
it.

On the ground the older rule still holds: walking pace leans on the wall, a
boost comes off it. [SIM-07]

## SIM-20 — a spread is a packed cone, not a fan

Shots laid out along one axis miss above and below whatever they are pointed at,
and cover ground either side that nothing is standing on. Pellets go on a
sunflower spiral instead — a golden angle apart, at a radius growing as the
square root of the index — which fills the circle evenly and puts the first one
straight down the middle.

The half-angle is unchanged; only the arrangement inside it is.

## SIM-21 — the body turns to aim, the travel stays the player's

The stick is body-relative. Firing off a boost or out of the air swings the
machine onto its lock, so a machine crossing in front of its enemy and pulling a
trigger had "left" quietly become a different direction in the world — and since
the airborne and walking velocities are driven straight from the stick, the
whole burst came round with the shoulders.

The stick is now read against `iStickYaw`, the heading the player last chose. It
tracks the facing normally and is held while a recentre is swinging the body.
The body turns to aim; the travel is the player's.

Two things came out of this that were wrong before:

- **The recentre was only counted down when the machine had a lock.** A jump
  sets it regardless, so a machine with nothing locked carried it for the rest
  of the round — which, among other things, kept the air dash at a quarter of
  its speed and let it sink five metres over a burst. It now runs down
  unconditionally.
- **A melee swing still drags the machine into it**, deliberately: the lunge
  *is* the attack, and it sets the dash direction from the swing. That is the
  one case where firing moves the travel.

## REND-14 — the close camera pivots on the machine, not on its feet

At knife range the camera swings round to look along the lock. Orbiting the
machine's origin puts the pivot at its feet, so the body is carried across the
frame and tipped as the camera comes round. Pivoting on the middle of the
machine keeps it where it is on screen and turns it in place.

The usual camera lift is scaled back once the pivot has moved up, or the view
ends up looking down on the fight from the height of two offsets stacked.

## DEF-06 — paint schemes, named for Whiplash's makes

The names are the race game's own, out of `CompanyNames` in `carplans.c`. The
colours are not: nothing in the retail data carries a flat colour per car —
`tCarDesign` is geometry, and `car_flat_remap` is a mirror remap for the
advanced car set. So the schemes are index pairs picked out of the same palette
ramps the roster's own machines already use, which keeps a repainted machine
looking like it belongs in the same arena.

A scheme replaces body, trim and joint. It never replaces the glow index: that
colour is how a player reads whose fire is crossing the arena, and a repaint
that changed it would undo [REND-13].

Scheme zero is the machine's own paint, and `mecha_scheme_get` returns NULL for
it so the mesh falls through to the definition.

## MODE-04 — survival fills the arena

A duel is two machines; survival adds one of everything the roster has, over and
over, until the world is full. Measured on MERIDIAN CROSSING, sixteen machines
cost about 140 ms of simulation for a minute of fighting — a fifth of a per cent
of realtime — and fight down to one or two survivors inside ninety seconds
rather than stalemating.

What it does cost is geometry. Sixteen machines peak near 4,900 quads against
the old 4,096 cap, which silently dropped nineteen thousand quads over a minute
— a machine that stops being drawn because the buffer filled is a machine the
player cannot see coming. The cap is 8,192 now, and the test asserts nothing is
dropped rather than just that it runs.

Everyone is given their own team. The simulation only ever asks whether two
machines share a team, so distinct teams is the whole of a free-for-all.

## MODE-05 — spectator flies ROLLER's own free camera

With no machine of the player's own there is nothing to chase, so the arena
borrows the track's noclip camera: the same mouse look, the same WASD, the same
speed multipliers, driven through `noclip_camera_update`.

It keeps its state in the track frame, where **Z is up**, and the arena is Y-up
— so the two are mapped rather than shared. Position `(x, y, z)` there is
`(x, z, y)` here. For the heading: the arena's forward is
`(sinYaw·cosPitch, sinPitch, cosYaw·cosPitch)` and noclip's, mapped into the
arena's axes, is `(cosYaw·cosPitch, sinPitch, sinYaw·cosPitch)` — so a quarter
turn separates the two yaws and the pitches are the same.

The arena draws through its own renderer and never reads the view globals
`noclip_camera_apply` publishes, which is why `view.c` grew a place/get pair
rather than the mode reading `worldx`.

The camera grabs the mouse while it runs, so leaving a match releases it and
resets `g_bNoclip`, or the briefing has no pointer.

## MODE-06 — the arena has to claim TAB back off the engine

`roller.c`'s event loop binds TAB to the renderer toggle and SHIFT+TAB to split
screen, both of which call `game_render_set_mode` and save the input config. The
arena uses TAB for the target cycle, and `mecha_mode_enter` forces
`GAME_RENDER_SOFTWARE` because every arena frame is rasterised through
`screen_pointer`, `winx/winy/winw/winh` and `scrbuf` — the globals
`mecha_render_frame` writes before it draws. Pressing TAB in a match therefore
swapped the renderer out from under the mode mid-frame and took the game down
with it.

Both handlers now skip while `eFrontendCurrentState` is `eFRONTEND_STATE_ARENA`,
which is the shape roller.c already uses for state-specific behaviour (the
background FPS cap tests `eFRONTEND_STATE_PAUSE_OVERLAY` the same way). The
event still reaches `InputHandleEvent` first, so `mecha_key(WHIP_SCANCODE_TAB)`
sees it.

Why it looked like a survival-only bug: with two machines in the arena the
target cycle is a no-op, so a duel gives nobody any reason to press TAB.
Survival is simply the first mode where the key gets used.

## MODE-07 — a spectator still needs frames drawn

`mecha_mode_draw` returned early when there was no player mech, so a spectated
match presented nothing at all: no `game_render_begin_frame`, no `end_frame`,
and the last briefing frame left on screen. The simulation was running the whole
time, which is what made it read as a lock-up rather than a blank screen.

`mecha_render_frame` already takes a negative view mech: the scene ignores it
outright and `mecha_render_hud` returns before touching `aMechs`, so the world
draws and the HUD simply is not there. Nothing else was needed.

The other half of the same bug was the seat itself. Spectating used to leave the
player's slot empty, so a spectated duel had one machine in it — and
`mecha_check_round_end` only ends a round when more than one team was present,
so that match could never finish. The seat is now filled with the player's
chosen machine under a computer pilot, which is also what makes the COLOURS row
still mean something from the free camera.

## SIM-22 — computer pilots paint themselves off the match seed

Sixteen machines in one paint is unreadable, so every machine that is not under
a player's hands takes a scheme of its own in `mecha_sim_add_mech`.

The draw uses a `tMechaRng` seeded on `uiSeed ^ (slot + 1) * 0x9E3779B9` rather
than `pWorld->rng`. The world's stream decides how the fight goes, and a match
has to replay exactly from its seed: taking paint draws out of it would mean
adding a machine — or changing how many colours exist — silently changed the
fight. A private RNG keyed on the same seed gives colours that are stable for a
given match and independent of everything else.

Scheme 0 is the machine's own palette (`mecha_scheme_get(0)` is NULL), so it is
one of the outcomes rather than a special case. The briefing's own choice still
wins for the player's seat: the mode writes `byScheme` after adding.

## SIM-23 — both triggers together, within a window

Left and right together is the centre weapon. On a pad those are analogue
triggers and they never break their thresholds on the same tick, so the
same-tick test made the centre weapon effectively unreachable.

An outer press now waits `MECHA_FIRE_PAIR_TICKS` (4, so 67 ms) for its partner;
if the partner arrives the centre weapon fires and both outer presses are
dropped, and if it does not the press fires as the outer weapon it was.
`byPairMask` is 1 for left and 2 for right, and 3 is the pair.

Three things the shape of it is protecting against:

- **The window starts once and is never extended.** Pumping one trigger faster
  than the window is long would otherwise hold its own shot forever, since each
  press would restart the wait.
- **It only runs when the centre weapon could actually fire.** No ammo, mid
  reload, still recovering, or no centre weapon at all, and the outer press goes
  off immediately — the 67 ms is only spent when it could buy something. If the
  centre goes away mid-wait, whatever is held back fires at once.
- **Only for a machine under a player's hands.** The computer fires one slot per
  tick and picks each deliberately; pairing its shots turned two aimed outer
  shots into a centre shot it never asked for, which measurably moved the skill
  ladder.

## DEF-07 — the KLR mortar, faster but still a mortar

The shell was 92 m/s under 32 m/s² of its own gravity. `mecha_arc_pitch` solves
the launch angle for whatever speed the weapon has, so raising speed alone keeps
it on target — but it also flattens the arc, and the arc is the entire point of
a weapon that does not need line of sight.

Raising both together keeps the shape and buys the speed: 140 m/s under 75 m/s².
At 60 m the shell now flies 0.43 s instead of 0.66 s and still apexes at the
same 1.7 m, and the maximum range works out at 261 m, which is the lock range
(260 m) it has to cover.

The sim test that read `lance > shell * 4` was asserting a ratio rather than the
intent; it now asserts the shell stays the slowest of the car's three weapons,
which is the thing that must never stop being true.

## SND-01 — the arena's sound reads the world, it is not told about it

`mecha_sim.c` has no idea sound exists, and that is deliberate: the sim is the
SDL-free half and `tests/mecha_sim_test.c` links it on its own. So
`mecha_sound.c` works out what to play by looking at the world each frame rather
than by the sim calling it.

What it watches:

- **Engine and skid** are loops, retuned every frame. There is nothing to
  detect: a machine that is active has an engine.
- **Blasts** are effect slots going from empty to full. `bActive` shadowed per
  slot catches every explosion born since the last frame, however many ticks the
  frame ran — which matters, because the mode runs catch-up ticks and an event
  flag set during one of them would otherwise be missed.
- **Collisions** are `iRamCooldown` going up, which is the sim saying a machine
  just ran into something.
- **Landings** are the airborne flag falling. The fall speed is read a frame
  early: by the time the wheels are down it has already been spent.

The alternative was an event mask on the mech that the sim sets and the sound
layer clears. It would be exact, but it puts a field that only exists for sound
into the structure the simulation is built on, and it has to survive being
consumed by nobody when sound is off. Shadow state in the sound layer costs one
array and nothing anywhere else.

Nothing here needs a sound card: `loopsample`, `pannedsample` and `loadasample`
all check `soundon` and `SamplePtr[]`, so with no device and no FATDATA every
call is a no-op.

## SND-02 — pan is the mixer's convention, not a guess

`DIGISetPanLocation` computes `iPan / 0x8000 - 1` and hands that to the mixer,
where `digi_pan` is documented as -1.0 full left to +1.0 full right. So 0 is
hard left, 0x8000 is centre, 0xFFFF is hard right.

That sign is worth the trouble of checking: getting it backwards puts every
machine on the wrong side of the player, and it sounds plausible either way.
Whiplash's own expression is `(1 - sin(getangle(...))) * 32768`, but
`getangle(x, y)` is `atan2(y, x)` — the second argument drives the sine — while
`mecha_atan2_angle(x, z)` is `atan2(x, z)`, where the first does. The two
conventions cancel the minus sign, so the arena's version is
`(1 + sin(...)) * 32768` and a machine off the camera's right pans right.
Measured, not reasoned: a probe walked a source around the camera and read the
numbers back.

Distance attenuation is Whiplash's constant unchanged,
`65536000 / (d^2 + 65536000)`. In arena units that is half volume at 32 m, which
suits an arena about as well as it suited a track.

## SND-03 — an engine is a pitched loop, and that is what makes a servo too

Whiplash does not synthesise an engine. `enginesound()` plays one looped sample
and rewrites its pitch and volume every frame:

- pitch is `fRPMRatio * 100000 + 8192`, plus a wheelspin term, plus
  `tsin[iEngineVibrateOffset] * (1 - health) * 10000` — which is why a damaged
  car sounds rough, the vibration is a pitch wobble
- volume is `258 * EngineVolume` scaled by engine state, then by distance
- the whole thing is multiplied by a doppler factor of
  `(listener + c) / (c - source)`

The arena uses the same machinery: `MECHA_SND_PITCH_BASE` is Whiplash's 8192,
and the span a machine rides up is its own speed over its walk speed. A walker
gets a narrower span (`MECHA_SND_SERVO_SPAN`) than a car, which is the whole
difference between a servo humming and an engine revving — the same sample, a
different slice of pitch.

That is the lever for weapon and servo sounds later: one sample, and the pitch
says what it is. Nothing here needs a new asset.

Skid follows the same idea. Whiplash decides a car is sliding by comparing the
steered yaw against the one the car ended up with; the arena compares the
machine's facing against the direction it is actually travelling, and folds the
backwards half of the circle away so that reversing is not sliding.

## AI-09 — going round what is in the way

Neither pilot could see an obstacle. The footing checks knew about pits and
about the edge of an open arena, and nothing else, so a car drove into the side
of a building and parked there for the rest of the round, and a walker stood
against it pressing forward. Measured on MERIDIAN CROSSING's middle block, with
the enemy directly behind it: the car travelled 0 m sideways in fourteen
seconds, the walker 24 m and never round.

The fix is a fan. `mecha_ai_detour` traces along the direction the pilot wanted,
and if that is blocked it tries bearings either side -- 17 degrees apart, up to
seven of them, nearest side first -- and takes the first that is clear. The car
steers for that bearing instead of for the enemy; the walker puts it in as its
stick and spends gauge on it. Both come round the same block in under seven
seconds now.

Two things that had to be right:

- **A wall is not a step.** `mecha_arena_ground_height` hides any box whose roof
  is more than `MECHA_ARENA_STEP_UP` above the feet, which is exactly the tall
  ones, so a hundred-metre building reads through it as flat ground. A pilot
  asking the ground query whether it could climb the thing in front of it was
  told yes about a tower block. Two traces at two heights -- the body, and a
  jump higher -- is what actually separates a crate from a building.
- **The detour is decided before the guard.** The pilot used to sit down and
  refill when it had no line and no gauge, which on the far side of a building
  is most of the time. Finding a way round is now a reason not to.

## AI-10 — the pilot takes the crossing step

Watari-dashing is already in the simulation: let the stick go mid-burst and put
it down somewhere else, and the burst starts again the new way rather than
limping out the old one [SIM-08]. The pilot never used it, because it held one
direction for the whole burst.

It now checks, last of all, whether what it wants is far enough off what it
launched with to be worth turning -- and if it is, it lets the stick go for one
tick, because that is what the machine is waiting to see, then puts it down the
new way. Last, because the detour, the evasion and the lock may all still change
its mind about where it is going.

What this buys is the thing it is for: leaving cover on one heading and arriving
on another. The detour above takes the pilot round the corner of a building; the
crossing step is what lets it cut back at the enemy without stopping first.

## AI-11 — a drop is a drop however the arena makes one

`mecha_ai_footing_clear` tested two things: a pit flag, and whether the point is
inside an open arena's boundary. Ground that simply falls away is neither -- the
sides of a causeway are inside the boundary and carry no flag -- so the pilots
walked off them. It also explains the rooftop walk-off in the city arena: the
roof of a building is inside the boundary too.

It now also refuses ground more than `MECHA_AI_FOOTING_DROP` (25 m) below where
the machine is standing. Hills and kerbs are well inside that; a causeway edge,
a roof and a hole are not. On the causeway map it cut the machines lost over the
side in the first minute of a sixteen-way from ten to four.

## ARENA-14 — where machines start when the ground is not a square

The spawn ring is a circle of `fHalfExtent * 0.62`, which is right for every
arena whose floor fills its own boundary. Two lanes with a hole down the middle
is not one of those: half the ring is over the hole.

`bySpawnShape` picks the rule. `MECHA_SPAWN_LANES` spreads the slots along the
long axis and alternates which lane each one is on, so a duel opens at opposite
ends on opposite sides and a sixteen-way fills both lanes rather than the drop
between them. The facing is worked out from where the machine actually ended up
rather than from the ring angle, which is only the same thing on a circle.

The spawn line also has to clear the buildings: at 120 m it put machines inside
the keep's flank wall, which the mesh test caught as quads facing into a box.
104 m stands them on the approach instead.

## ARENA-15 — a hole you fall into, not a hole that deletes you

Whiplash makes a hole in a track by flagging the surface, and the arena
inherited it: `MECHA_SURF_PIT` kills a machine the moment its feet are on a pit
cell. That reads as being deleted. It is also why a hole made that way felt
wrong -- there is no fall, no tumble, and no moment of knowing it has happened.

The causeway map makes holes out of ground instead. `mecha_arena_void` drops
every node far below the arena, and the ground it actually has is painted back
over that a piece at a time with `mecha_arena_lane` and `mecha_arena_pad`.
Anything not painted is a hole, and a machine that goes into one falls --
measured at 35 ticks of falling before the kill plane takes it, against zero for
a pit.

The nodes either side of an edge are one cell apart, so with 40 cells over 400 m
the drop is 260 m over 10 m of ground: a cliff, not a slope.

## ARENA-16 — FACING WORLDS

After the Unreal Tournament map, built from a model of it the player supplied.
Two keeps at the ends of two causeways, with a hole between the causeways on
each side of the middle.

- Seven hundred metres of arena, the keeps 504 m apart, at 6 cm to the unit.
- The lanes are about 30 m wide and they bow: out to 47 m off the axis a quarter
  of the way along, back in to 26 m at the middle, where they all but touch.
  That pinch is the one crossing between them.
- The bases sit at the low ends and the lanes climb 32 m to the middle, so
  leaving a base is uphill and falling back to it is downhill.
- A keep on each base, 90 m square, walls 34 m high. Four walls with a pier in
  the middle of the one facing the causeway, so there are two doorways and each
  opens onto a lane. A single gate on the centreline opened onto the hole
  between them and half the field walked into it inside ten seconds. Open to the
  sky, because a box here is solid from the ground up and a roof would be a lid
  with nothing able to get under it.
- Nothing out on the run. The original has no cover there either -- what it has
  is a crest you cannot see over -- and a block standing near a lane's edge puts
  a wall of terrain quads inside its own footprint, which is a mesh with no
  right answer.

Machines start inside the keeps, which is how the original starts a match.

## ARENA-17 — reading the model, and what it says

The model is Z-up, and the first reading took Y for up. Everything followed from
that: the causeway came out as one bowed strip rather than two, the climb came
out as a W, and the plan was measured across the map's height instead of its
width.

What settles it is the area of the flat faces. Sorting every triangle by which
axis its normal points along and totalling the area, +Z has the most and -Z the
least -- floors facing up, underside cut away. Y-up would have put nearly equal
area on +Y and -Y, which is what walls do, not floors.

Read the right way up, the arena is built from a station every 15 m along the
run. Each station carries where the centre of each lane sits, how wide it is
there, and how high, straight off the model; `mecha_arena_lane` interpolates all
three between stations, so a run of them follows the curve instead of stepping
along it. The table is in `mecha_arena.c` beside the arena it builds.

Two things the measurements do not give and the arena has to:

- **The crossing.** Where the lanes pinch, the model leaves six metres between
  them, which is narrower than the machines that have to use it. It is widened
  to something two of them can pass on: the crossing is the only way from one
  lane to the other, and one nobody can take is a hole with extra steps.
- **The last stretch.** The measurements stop 24 m short of the bases, because
  that is where the lanes merge into them and the sampling can no longer tell
  one from the other. The arena runs the end station out to the base itself.

## ARENA-18 — a box knows what it is standing on

`mecha_mesh_arena` has always drawn a box up from the terrain under its centre.
`mecha_arena_ground_height` read the same `fHeight` as an absolute Y. On level
ground those agree, which is why it never mattered; on a slope the drawn box and
the solid box are in different places.

It showed up as soon as cover stood on a climbing causeway: passing the
collision an absolute height made the mesh draw the box at the terrain height
twice over.

`tMechaObstacle` now carries `fBaseY`, filled in at the end of
`mecha_arena_init` where the ground is finished, and the mesh, the ground query,
the cylinder push and the segment trace all read it. Box heights are plain
heights above their own ground again, everywhere.

## SIM-24 — a spawn stands on the ground, not under it

`mecha_reset_round` asked `mecha_arena_ground_height` for the spawn height with
the feet at zero. On an open arena that is the query for "am I under this
platform", and any ground above zero answers void -- so on the first arena with
raised ground, every machine was placed at -4000 m and fell out of the world
before the round started. It asks `mecha_arena_terrain_height` now, which is the
ground itself with nothing standing on it.

## SIM-25 — a round goes to a side, not to a machine

`mecha_end_round` credited `iRoundsWon` to the one machine it named the winner.
With one machine a side that is the same thing; with eight it is not, because
the machine left standing at the end of one round is rarely the one left
standing at the end of the next, so a match of sixteen could run for ever
without anyone reaching the rounds it takes to win.

The round is credited to every active machine on the winner's team instead, and
`mecha_mech_allied` is the one place that answers "are these two on the same
side" for everything outside the simulation — the banner and the mode's result
line both ask it rather than comparing indices. Out-of-range indices are on
nobody's side, which is what makes it safe to ask about a spectator's `-1`.

## MODE-08 — team deathmatch is a duel with sixteen machines in it

Survival's sixteen machines with survival's sixteen teams collapsed to two: the
MECH row flies one side, the OPPONENT row the other, eight each.

The seats alternate sides, and that is not cosmetic. `mecha_reset_round` hands
out spawn points in seat order, and both arena spawn shapes split a duel by that
order — `MECHA_SPAWN_BASES` puts even slots in one keep and odd slots in the
other [ARENA-14], the ring puts consecutive slots opposite each other. Filling
one side's seats first would therefore start half of each team inside the
enemy's base.

Paint is a side's, not a machine's, so `mecha_mode_paint_teams` overwrites what
`mecha_sim_add_mech` drew per machine \[SIM-22\]: the player's side wears the
scheme the briefing chose and the other side takes one draw of its own, stepped
on by one if it lands on the player's, because two sides in one colour is the
one thing this mode cannot have. The draw runs on a `tMechaRng` of its own
seeded off the match seed — the world's stream decides how the fight goes, and
paint must not move it.

Measured with computer pilots on both sides and no round clock: MERIDIAN
CROSSING wipes a side out in about 45 s, the small arenas in 20-25 s. FACING
WORLDS does not finish at all — the last few machines sit in their own halves
trading long-range shots, which a free-for-all on that map does too, so it is
the map's size rather than the mode. With the round clock on (the briefing's
default) it is decided on armour like any other round.

## AI-12 — three things that stopped a pilot leaving its own half

Measured on FACING WORLDS with computer pilots on both sides and no round clock:
sixteen machines, ten minutes, nobody past their own base. Three separate
faults, none of them the one it looked like.

**A burst that cannot be taken is not a reason to walk home.** The footing rules
checked the whole length of a boost burst along the wanted heading and, when it
failed, cancelled the boost *and reversed the stick*. With an enemy a
burst-and-a-bit away across a hole that fails every tick, so the pilot walked
backwards out of every approach it started. The burst check now only refuses the
burst; backing off is what the walking check is for.

**A gap narrower than a stride read as solid ground.** `mecha_ai_footing_clear`
sampled one point at the far end of the look-ahead. The far lip of a hole is
ground, so the hole was invisible. It samples the whole way now, a step of
`MECHA_AI_FOOTING_STEP` at a time, and `mecha_ai_footing_run` returns how far it
got — which is what the fan below needs anyway.

**Standing still, a machine has no heading.** The carry check looks along the
machine's own velocity. At a standstill that is a couple of centimetres a second
of noise pointing anywhere, so a pilot loitering near an edge threw itself into
an escape sixty times a second and never went anywhere. `MECHA_AI_CARRY_MIN` is
the floor below which there is nothing being carried.

With those three fixed a pilot gets round a gap rather than backing away from
one: the fan in `mecha_ai_skirt` turns off the wanted heading a step at a time
and takes the bearing that gets *furthest* before the ground runs out — not the
first that is clear, because every bearing over a hole is clear somewhere past
the far lip. The choice is held for `MECHA_AI_SKIRT_HOLD` ticks, the way the
strafe direction is held: re-picking on a causeway barely wider than the
look-ahead gives a different answer every tick, and a machine that changes its
mind sixty times a second walks on the spot.

That was worth going from nought fights in five finishing to three, and it is as
far as looking at the ground in front of you can get: the rest is AI-13.

## AI-13 — the way spine, which is Whiplash's racing line

The race game's computer drivers do not look at the road in front of them. Each
track chunk carries four AI lines (`localdata[].fAILine1..4`), the driver holds
an index into them (`iAICurrentLine`, swapped by `changeline` when `linevalid`
says the one it is on has run out), and `findnearcarsforce` walks the chunks
ahead by a strategy distance scaled by speed, interpolates the line offset
between two chunks, and hands back a world-space point. The whole of the
steering is then `atan2` to that point, clamped by the engine's steering
sensitivity.

The arena has no track, so the arena publishes the line: `tMechaWay` is a chain
of stations, each a centre and how far either side of it there is still ground,
and an arena that needs one fills it in as it builds itself. FACING WORLDS
publishes two, one per causeway, straight out of the same measured table the
lanes are painted from [ARENA-17], with an end on each base so a machine in a
keep is led out of a doorway rather than at the hole. Every other arena
publishes none and nothing about them changes.

`mecha_arena_way_aim` is `findnearcarsforce`: the way the machine is nearest,
the station it is nearest on it, the station to head for, then walk the chain by
the look-ahead and interpolate where that lands. Four lines across the way,
picked per machine at spawn as Whiplash picks its driver's, so eight a side do
not file down the middle of one. Two details the race game does not need:

- **The link.** Where the enemy is on the *other* way, the goal is not the
  station beside them -- that is straight across the hole -- but the station
  where the two ways come closest, which on this map is the crossing at the
  pinch. Each way stores that index per other way, worked out once when the
  arena finishes building.
- **The soft edge.** The ground is a grid of cells with heights interpolated
  between them, so the outermost cell of any edge is a ramp into the void rather
  than floor. The lines are laid out on the station's width less one cell, or a
  machine on the outside line is already sliding.

A pilot follows a way only while the straight line to the enemy has nothing to
walk on, and only as far as its own feet can see: the way is the arena's own
ground, but getting onto one from wherever the machine is standing is not, and a
walker that trusted the line from the far side of the hole walked into it. What
cannot be walked goes to the fan in AI-12, which now has the way to aim off
instead of the enemy.

The car gets it too, and needed it most -- it cannot step sideways off a
causeway it has driven onto the edge of. Its one extra rule is the moment of
committing: nose already on the way, way clear ahead, wheels still carrying the
turn that put it there. A driver that lifted through that moment never joined a
lane at all and sat at the mouth of one for the whole round.

Measured over twelve sixteen-machine fights, no clock, ten-minute cap:

|                               | before  | with the spine |
| ----------------------------- | ------- | -------------- |
| fights decided                | 0 of 12 | 9 of 12        |
| both sides out of their bases | never   | every fight    |
| closest the two sides come    | 400 m+  | 175 m          |
| machines lost to the hole     | —       | 1.2 a fight    |

## ARENA-19 — the causeways are built wider than they were measured

CTF-Face is walked by a man. This is driven by something eight metres across, on
ground whose cells are eight and three quarter metres and whose outermost cell
at any edge is a ramp rather than floor. At the measured width that leaves about
a machine and a half of usable lane -- a tightrope, not a causeway to fight
along, and it showed: pilots spent more of a fight falling off the map than
fighting on it.

The station table keeps the measurements. The builder scales their half-widths
by `fWide` (1.6) and nothing else: the bow, the pinch, the climb and the hole
between the lanes are all the map's own. Falls on FACING WORLDS went from 3.4
machines a fight to 1.0.

______________________________________________________________________

## TYPE-06 — the chassis is a drawing question, and bWheeled is not

`bWheeled` predates every other body in the mode and answers a physics question:
whether the machine has one signed speed along its nose instead of a walk and a
strafe. It is read in eleven places in `mecha_sim.c` alone.

`byChassis` answers a different one -- what the mesh builds -- and the two are
kept apart on purpose. A tracked machine walks, turns and strafes exactly as a
biped does; it merely does not look like one, so it takes the biped's physics
and its own geometry. Folding the two into one field would have meant either
giving the tank a car's steering or giving the car a biped's, and neither is
what anyone meant.

They do have to agree in the one place they overlap, and the roster test says
so: `bWheeled == (byChassis == MECHA_CHASSIS_CAR)`. That assertion exists
because the first thing that happened after the dispatch moved off `bWheeled`
was the Zizin coming out as a biped -- silently, with legs, still driving like a
car.

## TYPE-07 — a profile is trim, not a skeleton

`byProfile` picks what gets hung on the biped's bones and which gait table the
legs read. It is deliberately not a chassis: all three profiles have the same
joints in the same places, so anything written for one arm or one knee works for
all of them.

The split earns itself on the slender frame, which needed a narrower waist, a
wider skirt, longer head crests, a different walk and a body that rocks when it
fires -- none of which is a new joint.

## MESH-31 — a frustum costs what a box costs

Every shape in the mode was an axis-aligned box, and the reference art this
roster is drawn from is mostly not boxes: it is tapered thighs, sloped chest
plates, shoulder binders cut back at the outer face, and blades.

A box with a separate top size and a top offset covers all of it. Each side
stays planar, because both of its horizontal edges keep their axis, so it is
still six quads and it still culls off a stored normal. There is no cheaper way
to get a taper and no reason to want one.

One thing it will not do is come to a point. A top extent of zero collapses two
corners of the end face into one, `mecha_quads_add` finds a degenerate normal,
and the quad falls back to being two-sided and uncullable. Tips keep a little
width -- which is also what the reference art draws, a fin having a tip rather
than a mathematical point.

## MESH-32 — detail tiers, and why the outline never goes

Measured on FACING WORLDS with sixteen machines and the camera behind one of
them, over ninety seconds: the scene peaked at 6706 quads before this pass and
8946 with every machine built in full. The buffer was 8192, and a frame that
overflows does not crash -- it stops adding geometry, quietly, wherever it
happened to get to.

Three tiers, by range to the eye. What falls away is trim that is already
sub-pixel at the range it falls away at: vents, muzzle rings, heel blocks, calf
verniers, the rear skirt plate. What never falls away is the outline --
shoulders, skirt sides, head crest, gun -- because that is the whole of what
tells one machine from another across an arena, and a machine that changed shape
as you walked towards it would be worse than one with no detail at all.

With tiers the same scene peaks at 8116. That is what made the detail
affordable; MESHH-04 is what made 8116 safe.

The thresholds -- 55 m and 130 m -- were picked by rendering the roster in a
line and finding the range at which each tier stops being visible, not by
reasoning about pixel sizes.

## MESH-33 — a slender frame is an opposition, not a scale factor

Drawing the whole machine at three quarters produces a smaller machine, which is
not the read. What produces the read is two things going opposite ways: the
limbs narrow towards the joints while the skirt flares wider than any other
frame's. Thin legs under a wide flare.

Three numbers, applied to different pieces on purpose. `fTaper` at 0.74 goes on
the thighs, shins, upper arms, forearms and the waist. `fFlare` at 1.45 goes
only on how far the side skirt plates reach past that waist. `fChest` at 0.82
goes on the chest, the plate over it, the collar, and the pack behind it.

The chest needs a number of its own rather than the limbs': taking 0.74 to the
torso as well puts the chest narrower than the waist under it, which is not a
slighter machine, it is an upside-down one. The flare was briefly at 1.9,
reaching for an outline that would not read as the interceptor's at range --
which is a job the machine's height does instead [DEF-10], and which made the
hips one slab from one side to the other as soon as the plates were properly
joined to the waist.

## MESH-34 — the waist is three shapes, not a column

A machine with a waist has a chest wider than it, a skirt wider than it, and the
waist between them. Before this the torso was one box from hips to shoulders and
the machine had no waist at all.

The four skirt plates are what make the hips read, and the two at the sides do
nearly all of it -- they are what says one machine is broad in the hip and
another is not -- so those two survive to the far tier and the front and rear
plates do not.

The front pair hinge on the leg they hang in front of, at just over half that
leg's swing. Fixed, they had the thigh pass straight through them at the top of
every stride; at the full swing they stop reading as armour and start reading as
a second thigh.

**A plate is hung off the waist, not placed beside it.** Its inner edge starts
at the waist block's own bottom edge, and `fFlare` grows how far past that it
reaches -- only that. Scaling the plate's centre by the flare instead, which is
how this was first written, moves the inner edge out along with the rest of it:
at a flare of one nobody notices, and at 1.9 the machine has its hips hanging in
the air a metre and a half either side of it. The top of the plate is skewed
back in so that edge lands on the waist too, which is what stops the join
opening up again above.

**The shoulder binder had the same bug, fixed the same way.** It was placed at a
multiple of the machine's radius scaled by the build, so a wide binder walked
its own inner face away from the chest it is bolted to; at the build the slender
frame briefly carried, the armour hung half a metre clear of the shoulder. It is
seated against where the chest actually ends now, overlapping rather than
meeting it, because two faces in one plane have nothing to sort them with
[MESH-18]. The arm hangs from the same place.

That is twice this mistake has been made in one part of the mesh, and its shape
is always the same: a number meaning "how big" used for "how far out".

## MESH-35 — the crest is the cheapest identity there is

Two blades off the brow, and at the far tier they are most of what is left of
the head. They are built at every tier for that reason: a head at a dozen pixels
tall is a smudge, and a smudge with two spikes on it is a machine you recognise.

Swept back, a crest reaches about as far again as the head is deep. It was at
two and a half times that, which from the side is not a crest, it is a pair of
banners the machine is towing.

The slender frame runs them backwards and longer instead of up and out, which
reads differently at any range and costs the same six quads apiece.

## MESH-36 — a carrier reads from behind

Eight pods in two raked columns above the pack, and no gun in either hand -- a
manipulator instead, because a hand cannon says the machine fights by shooting
and this one does not.

The rack is drawn whether or not anything has been launched. Emptying it as the
round went on was tried and abandoned: the machine looked like a different
machine by the end of a round, and the outline is the one thing that has to hold
still.

## MESH-37 — three chassis, one upper body

A tracked machine is a torso that happens to have no legs under it, and an
arachnid is the same torso on a hull. Pulling the waist, chest, arms and head
out of the biped builder is what stopped the second and third of those being
copies -- and a copy is where the next change only lands in two of the three.

`tMechaBuild` is the bundle that makes it bearable: passing a machine, its
colours, its build multipliers and its tier as a dozen separate floats is a
signature nobody would keep in step.

## MESH-38 — what makes a tracked machine read as tracked

Not the tracks. A track drawn as one long box slides across the ground with
nothing turning on it, and the eye reads that as a building on castors.

What sells it is the road wheels, turning on the same distance counter the walk
cycle is paced by, so a tracked machine and a walking one agree about how fast
the world is going past. Four of them a side at full detail, two at mid, none at
far -- by which range there is nothing to see turning anyway.

The rest is the shape a tank has and a mech does not: an approach angle at the
front of each unit, a fender over the top, a hull between them and a ring the
torso turns on.

The barrels were written at twice their length and from the side read as a pair
of planks the machine was carrying. They are shorter than the machine is wide
now.

## MESH-39 — an arachnid's knees are above its body

That is the whole of what makes six legs read as an arachnid rather than as a
table. The body rides at half the machine's height and the femur goes up and out
from there, so the knee stands above the hull and the tibia comes back down past
it to the floor.

Which means the tibia is the long bone -- eight tenths of the machine's height
against the femur's three -- and its angle cannot be picked. It is solved:
`asin((knee height - foot lift) / tibia)`. Picked instead, at the numbers that
looked about right, the feet finished three metres above the ground and the
machine floated.

The fold is against the femur's turn, not with it. Both the same sign and the
leg comes back up over the body instead of reaching the floor.

## MESH-40 — the crossing walk, and what it costs above the waist

Every machine walked the same way, and how far apart the feet are across the
line of travel is what separates one walk from another far more than how far
they travel along it.

The slender frame puts its feet down near the centreline: the standing hip rolls
in through the planted half of the cycle, so the body passes over the foot
rather than beside it.

What that costs above the waist is MESH-46, and the answer written here first --
sliding the whole torso from side to side and rolling it with the hips -- was
the wrong one.

Firing rocks the same waist back. Every other frame kicks only the arm that
fired; this one adds the body, which is what makes a small machine firing a
large weapon read as a small machine firing a large weapon.

## MESH-41 — the arms had one aim between them

Every arm in the mode read the same yaw and the same elevation, because both
were pointing at the same target. Correct while the machine is fighting, and the
reason "one arm up and one on the hip" was not a shape this rig could make at
all -- the only thing either arm owned by itself was its recoil kick.

`tMechaArmPose` is per-arm, and its angles are chosen to be read rather than to
match the chain: `iUpper` is where the upper arm points, nought hanging straight
down, a quarter turn negative level and forward, half a turn negative straight
up.

Making it blendable meant collapsing the two shoulder pitches the aim was split
across into one. That is the same rotation -- a yaw and then two pitches about
one axis compose as the yaw and their sum -- and it is what makes an aimed arm
and a posed arm two values of one number instead of two different chains. The
gun heights the tests measure came out unchanged to two decimal places, which is
how that was checked rather than argued.

## MESH-42 — a machine that has won stands like it

A round used to end with the winner standing exactly as it stands at any other
moment, which is the one moment in a match where a machine has nothing else to
be doing.

One pose per profile: a salute for the standard frame, an arm up beside the head
and a hand on the hip for the slender one, both arms low and open for the
carrier, whose point is the rack on its back. It is held only by a machine that
is alive, on the winning side, and on the ground.

Under the arms is a stance of its own -- one foot forward and turned across the
other, the weight settled back. The cross is a yaw at the hip and not a roll,
and that is not a detail: a planting gait has its hip rolls solved for so that
both feet finish on the floor [MESH-16], and anything written into the roll is
overwritten by that solve. A yaw swings the leg across without changing how far
down the foot reaches, so the solve never notices it.

Eased in over 0.45 s off the phase clock rather than snapped, and off the phase
rather than off a tick, so a machine that wins on the last shot of a round is
not thrown into a victory stance by the same frame that killed the loser.

## MESHH-03 — a quad says which part it is

The tests used to name a box by counting how many the builder had emitted before
it: `MESH_BOX_GUN_LEFT` was 16, and the legs were the first `2 * 4 * 6` quads.
That was true until the builder emitted a different number, and then it did not
fail -- it quietly measured a different box and went on passing.

`byPart` costs one byte on a 68-byte struct and is set once per section rather
than per call, through the list rather than through every helper. Nothing in the
renderer reads it. What reads it is anything that wants to find a part without
knowing the order they are built in: the gait comparison, the gun height, and
the block-facing check, which was catching a machine's own legs whenever the
machine was standing inside a keep.

Every top-level builder sets it on entry, including the ones that set it to
`MECHA_PART_NONE`. Leaving it to whatever the last builder happened to set makes
it depend on call order, which is exactly the fragility it exists to remove.

## MESHH-04 — the buffer is sized to the worst scene, not to a round number

FACING WORLDS is 4346 quads of arena before a single machine stands in it, which
is over half of what 8192 was. Sixteen machines on it peaked at 8116 with the
detail tiers doing their work -- ninety-nine per cent, which is not a margin.
One more effect and the frame starts dropping geometry.

12288 leaves it at sixty-six per cent. It costs 272 KB of BSS once, for the
whole mode, and the test asserts the peak stays under three quarters so that the
next thing anyone adds to a machine has somewhere to come from.

## DEF-08 — BASTION 88, and why it can still jump

A tracked machine that could not leave the ground at all would be locked out of
the jump weapons and the jump cancel, which is a quarter of the mode. So it
hops: seventeen metres a second against the interceptor's thirty-three, at three
times the gauge cost, which buys the stance weapons and buys nothing else.

## DEF-09 — Tarant VZ pays for six legs in the gauge

The trade is not in the mesh. It holds a burst for a third of a second and
drains at 620 a second against the roster's usual 300, so three dashes empty it:
this machine crosses ground by walking fast on six legs, not by throwing itself
about on thrusters.

## DEF-10 — Lilia 07 is the lightest thing on the roster

760 armour against the tracked gun's 2000, carried by the longest dash and the
highest turn rate. It is the frame the slender profile was written for and the
one that most needs the profile to be legible, because at that armour a player
has to recognise it before it reaches them.

## DEF-11 — the carrier's mines do not fall

`fArcGravity` of zero, which the flight code already handles: it only pulls a
shot down when that figure is above zero, and `mecha_arc_pitch` returns level
for it rather than dividing by it. So a mine laid with no gravity hangs exactly
where it was put -- a field at chest height instead of one on the floor.

The roster test used to forbid it, requiring gravity on both arcs and mines.
That was written before a floating mine was a thing anyone wanted. An arc still
has to have something to fall under, because an arc with no gravity is a flat
shot wearing the wrong kind; a mine does not.

## TEST-09 — a silhouette measured against the arena behind it

Counting pixels that are not the sky counts the arena. The machine is measured
by rendering the frame twice, once with it and once with it switched off, and
taking what differs -- which is the machine and nothing else, whatever it is
standing in front of.

Three numbers come out: how wide the outline is, how tall, and how much of its
own box it fills. Two machines matching on all three within a tenth is the
failure this catches, which is a new machine shipping as an old one with the
numbers changed. It is a weak test of a strong claim -- two different shapes can
cover the same ink -- and it is the strongest claim a number can make about a
silhouette.

The machines are stood in the middle of the arena with the cover taken away.
Left where their pilots walked them, half the roster was measured from behind a
block.

## TEST-10 — the frames that exist to be looked at

A walk is the one thing about a machine no still frame can show, so the render
test steps the cycle round in eight and dumps each one, then pulls a trigger and
dumps the recoil settling, then wins a round and dumps the pose easing in.

None of it is asserted. The numbers that pin the rig live in the sim tests;
these are for a human to look at, which is the only way anyone has ever found
out whether a walk reads as a walk.

## TYPE-08 — the hips move both halves of the figure

`fBuildHip` says where the hips sit as a fraction of the machine's height, and
raising it does two things at once because it has to. The legs run from the hip
to the floor, so they get longer; every offset above the waist was written
against the hips being at `MECHA_HIP_CLASSIC`, so the upper body is redrawn at
`(1 - hip) / (1 - 0.47)` and gets shorter by exactly as much.

Left as one number the machine simply grows taller than its own height, which
then breaks the camera framing, the muzzle heights and the silhouette check
together. Moving both halves off one figure is what keeps `fHeight` meaning what
it says.

Every machine that does not declare one comes out bit-identical: at the classic
hip the upper scale is exactly one, which is how the change was checked -- the
gun heights the arm tests measure did not move.

`fBuildArm` is separate and only shortens the bones, because an arm's length and
a torso's are not the same question: the slender frame wanted short arms on a
short body, and the standard frame wants neither.

## MESH-43 — the upper body answers the legs

Every machine carried its torso and its arms as though it were standing still
while its legs moved underneath it. That does not read as stiffness so much as
it reads as a doll being slid along the floor, and it was true of the whole
roster rather than of any one frame.

Three things, on every chassis that has a stride:

- **The arms swing against the legs.** The left arm goes forward as the left leg
  goes back, which is the half-turn offset being on the side rather than on the
  phase. The elbow follows at about half, because an arm swinging from a locked
  shoulder is a pendulum and not an arm.
- **The shoulders twist against the hips**, eight degrees of it.
- **The body rocks fore and aft**, three degrees, at twice the stride rate --
  once per footfall rather than once per cycle.

A machine holding a lock keeps 30% of all of it. Not none: a machine with
somebody to shoot at still walks, it just does not swing its arms like one out
for a stroll. Dropping it to nothing meant the swing appeared and vanished as
the lock came and went, which is worse than either.

The stride reaches the arms through `tMechaBuild`, set by whichever chassis
builder knows which gait the legs are running. A tracked machine never sets it
and never swings: there is no stride to answer.

## SIM-26 — a floating mine has to be stopped, and can be sent

A mine only ever stopped by landing. That was fine while every mine fell, and
wrong the moment one was given no gravity to fall under: it flew straight at its
throwing speed until its life ran out. The notes for the carrier said its mines
hung where they were put. They did not -- they were very slow bullets.

A mine with no gravity now bleeds its throw off while it arms, at 0.86 a tick,
which takes thirty-odd metres a second to near enough nothing over the arming
time and leaves the charge hanging where the throw carried it.

Then, if it has a homing rate, it goes looking. The speed has to be handed back
before the steering runs, because `mecha_home_projectile` turns the direction of
a velocity and a stationary mine has no direction to turn; from a dead stop it
is pointed at whoever it was laid against and the steering takes it from there.
A third of the throwing speed, because a mine that chases at bullet speed is a
missile and this machine already has missiles.

The test watches rather than times. How long a mine takes to arm is the
simulation's own business, so what is asserted is the shape: the throw is spent
to under a fifth of what it was at some point, the mine never touches the floor,
and afterwards it is closing on somebody.

## MESH-44 — a neck

The head sat straight on the collar, which is what makes a machine read as
hunched however well the rest of it is proportioned. There is a short one
between them now.

It is drawn on the torso rather than on the head, because a neck does not turn
with what it carries -- and this is the one place that distinction shows, the
head being the only part of a machine that turns on its own [MESH-20].

## MODE-09 — a screen that is drawn and never presented

Selecting VIEW CONTROLS on the briefing softlocked the game, and none of it was
in the controls page. Its drawing was correct, and the headless render test
already checked its pixels -- by calling `mecha_render_controls` itself, which
is precisely the call that was fine.

What the page did not do was open a renderer frame. `game_render_end_frame` is
what puts the buffer on the screen; the branch drew into `scrbuf` and returned
without it. So the page was rendered into memory and never shown: the briefing
stayed up, the mode went on running behind it with its input working, and from
the player's side the button did nothing, forever.

The fix is one begin/end pair around the whole of `mecha_mode_draw` rather than
one per branch. Wrapping each branch would have fixed this instance and left the
next screen free to make the same mistake; wrapping the function means a branch
cannot be the one that forgets.

The test is a source test, in the same vein as the palette one \[the arena exit
scene\]: it reads the function, and asserts one frame opened, one closed,
nothing drawn outside them, no return between them to strand one, and a branch
for every screen the mode declares. It was checked by putting the bug back, and
it names the offending call when it fires.

The general lesson is the one worth keeping: a test that calls a drawing
function directly proves the drawing, and says nothing at all about whether
anybody asked for it to be shown.

## MESH-45 — two legs standing on one floor reach it with their knees

A stance puts one thigh forward and one back and gives both the same knee. Those
two legs do not reach the same distance: `cos(lead - knee)` and
`cos(-lead - knee)` are not the same number, and at a seventeen degree lead with
a twenty-five degree knee they differ by a fifth of a shin.

The planting solve took that difference out in the hips [MESH-16], which is the
right answer to the question a hip roll is for and the wrong one here. It
splayed the longer leg right out and left the other standing straight -- not a
stance, a machine with one leg kicked sideways. It scales with leg length, so
the frame with the longest legs wore it worst, and on the shortest it was
invisible.

The knees go first now: the leg that reaches further is bent until it reaches
the same as the other, and only what is left over goes to the hips. Both legs
then keep the splay the gait asked for, which is what makes a stance symmetric.

Worth recording what that did to a test. "Wider with someone to fight" had been
passing partly on the strength of the bug -- one leg kicked out is a wide stance
by any measure of width -- so once the legs were even, the fighting splay had to
go from eleven degrees to eighteen to be honestly wider than standing. The
assertion was right all along and the pose had been cheating it.

## MESH-46 — hips roll, shoulders turn, and nothing slides

The first version of the walk's upper body slid the whole torso from side to
side and rolled it with the hips. Everything above the waist moved as one piece,
and what that reads as is not a machine walking, it is a machine wobbling.

What moves is the pelvis. It tilts, dropping on the side whose leg is swinging
through, and turns a little with that leg. The shoulders do neither: they stay
level, and they turn the other way. That opposition between hips and shoulders
is the walk. The lateral slide was never part of it and is gone.

Two frames off one point carry it: a pelvis that the skirt hangs on and that
tilts, and a torso that everything above the waist hangs on and that does not.
The legs stay on the root frame, untouched, so none of this can disturb the
planting solve.

The tilt has a ceiling, and the reason is the skirt. The plates overlap the
waist by a fixed amount [MESH-34] while the legs hang off the frame above them,
so tilting the pelvis tilts the armour away from the legs it is sitting over. A
few degrees stays inside that overlap; the nine the victory pose was first given
opened a gap you could see daylight through, which is what "her leg has come
away from her hip" turned out to be.

______________________________________________________________________

## TYPE-09 — what the sim leaves behind for the things that have no voice

Sound and the HUD both need to know that something *just happened*: a shot went
off, a trigger came up dry, a hit landed. The simulation cannot tell them —
nothing behind `mecha_sim.h` knows either exists, and that is worth keeping.

So the sim leaves a record and they read it. Four fields on the mech, each the
tick a thing last happened on: `iFireTick`, `iDryFireTick`, `iHitTakenTick`,
`iHitDealtTick`.

Ticks, not flags, because a frame can run up to five ticks [MODE-03] and a flag
raised inside one would be lowered before anything looked. Ticks, not counters,
because the HUD's question is "how long ago", which a counter cannot answer —
the HIT flash [REND-14] is drawn straight off `iHitDealtTick` and needs no state
of its own, so a paused frame drawn twice draws the same thing twice.

`-1` is never. Zero is a real tick a match can fire on, so the comparison state
in the sound layer starts at `-1` as well rather than being `memset` flat; that
is the one thing a reader has to get right.

Two distinctions turned out to matter:

- **Recovery is not empty.** A trigger pulled while the last shot is still
  recovering has rounds behind it and will fire in a moment. Counting that as a
  dry trigger cries wolf on every burst, so `iDryFireTick` only moves when the
  slot is actually out or reloading. The test disables that condition to prove
  it.
- **Your own blast is not a hit.** `mecha_sim_damage` is the single chokepoint
  for every point of damage in the game, including a machine standing in its own
  explosion, so the attacker's `iHitDealtTick` only moves when the attacker is
  not the victim. Otherwise the HUD congratulates you for blowing yourself up.

______________________________________________________________________

## SND-04 — a gun bank built out of car noises

Whiplash has no laser in it. It has engines, tyres, gear changes, fenders, menu
clicks and a commentator. Every weapon in the arena is one of those put to
another use:

| kind          | sample     | why                               |
| ------------- | ---------- | --------------------------------- |
| bullet        | `GRSHIFT`  | a gear change is a breech clack   |
| beam          | `BLOP`     | short, bright, tonal              |
| arc, homing   | `LIGHTLAN` | a low thump is a launch tube      |
| mine          | `BUTTON`   | a click as it goes down           |
| melee         | `SKID1`    | short and metallic, taken well up |
| standing fire | `EXPLO`    | it is an explosion                |

What separates one gun from another is **pitch**, not sample. A weapon's
`fDamage` rides between 26 and 150 across the roster, and the same clack played
from 1.30x down to 0.72x across that span is the difference between a rifle and
a siege gun. That is the whole reason `pitchedsample()` exists.

The samples were picked by measuring rather than by name: duration, zero
crossing rate, dominant frequency and tonality over the whole `.RAW` set. It is
what ruled out the obvious-sounding candidates — `REJECT1`, `REJECT2`, `BLOCK`
and `TDAMAGE` are all commentator speech, not effects, and would have put a man
shouting over every shot.

One caveat found in the mixer rather than by ear: `pannedsample` keys its handle
table on the sample index alone, so two machines firing the same weapon on the
same frame means the second cuts the first off. That is Whiplash's own behaviour
and the race lives with it; in a crossfire it reads as one shot rather than two.

______________________________________________________________________

## SND-05 — one warning sample, two pitches

Virtual-On warns you that you are being hit and warns you, differently, that the
gun you just pulled is empty.

The first attempt was one sample at two rates — `BRP` at 0.80x and 1.45x — which
answered the brief's "a slightly different warning sound" literally and sounded,
at the high end, exactly like a car horn. That is what pitching up a bright
broadband sample does: `BRP` already peaks between 2.6 and 4.7 kHz, so 1.45x
puts it at 3.8 to 6.8 kHz and there is nowhere else for it to read as.

So the two became a buzz and a clunk rather than one buzz twice: `BRP` at 0.80x,
and `BANK` — the barrier impact, a low decaying thud — at 1.30x. Being a
different *kind* of sound rather than a different pitch is what makes them
impossible to confuse in the middle of a fight.

Which event gets which then changed again on listening: the clunk is taking a
hit and the buzz is the dry trigger, where it started the other way round. That
is two reversals on two sounds, so the mapping is now one pair of defines
(`MECHA_SND_HURT_SFX` / `MECHA_SND_DRY_SFX`) rather than something spelt out at
the call, and the samples are named for what they sound like rather than for
what they mean.

**Rate travels with the sample, not with the event.** Each rate was tuned
against the sample it sits on, and the buzz taken up instead of down is the car
horn again — so a swap moves the pair. Level stays with the event: being shot
matters more than a trigger that did nothing.

They are **not** placed and **not** attenuated. Everything else in the arena is
mixed from where the camera stands [SND-02], but a cockpit warning is not in the
arena — it is the machine talking to the pilot, so it plays centre and at full
level, and only for `iViewMech`. A spectator passes `-1` and gets neither.

______________________________________________________________________

## SND-06 — the squeal belongs to the boost

The skid loop used to key off the angle between where a machine pointed and
where it was travelling, which is precisely how `enginesounds()` spots a car
sliding in the race game. Borrowing that was the mistake: a mecha strafes for a
living. Walking sideways is not a slide, and the machine squealed its way around
every circle-strafe.

What actually scrubs the floor is a ground dash — thrusters lit with the feet
still down, in any direction. That is now the only thing that squeals, its level
riding the machine's speed against its own walk speed. Airborne dashes are
silent on this channel, because nothing is being scrubbed.

**The pitch had to come down with it.** The old pair of constants — a base of
1.00x and a rise of one octave per 25.6 m/s — were set for a slide at walking
pace and were never revisited when the trigger moved. Dash speeds on the roster
run from 43 to 92 m/s, which put the sample at 2.7x for an ordinary dash and
4.6x for the quickest: not a tyre, a whistle. It is a floor of 0.80x now with
0.70x added across the full range, so the slowest dash sits at 1.13x and the
fastest at 1.50x. Those two numbers are the knob if it is still wrong.

______________________________________________________________________

## SND-07 — the engine loop follows the gait

A machine with legs is not a machine with a throttle. Driving its loop off road
speed alone gave a flat hum that rose and fell with a joystick, and what the ear
expects from something walking is the stride.

`fStepPhase` counts strides rather than time [MESH-13], so the sound reads it
directly: the loop's level and pitch swing either side of where speed alone
would put them, once per footfall. Two footfalls per stride covers every machine
with legs — a biped's two feet and the arachnid's tripod [MESH-39] both put
something down twice a stride, so one figure does for both.

The depth of the swing follows how fast the machine is actually walking, so a
machine standing still hums flat rather than pulsing on the spot. It is off
while airborne and off during a dash: feet in the air are not walking, and a
machine gliding on its thrusters should not sound like one striding.

Wheels and tracks are untouched. `MECHA_CHASSIS_CAR` and `MECHA_CHASSIS_TREAD`
roll, their noise follows road speed, and it already did.

______________________________________________________________________

## DEF-12 — weapon fire owns every hue the arena does not

Two complaints, one cause: green shots vanished against grass and violet ones
read as dark.

The arena paints in grey (floors, walls, blocks), green (the meadow's grass and
canopy) and brown (bark). The tracer set had a green in it — index 255, the top
of the same ramp the grass sits two steps down. A green bolt over a green field
is invisible, and no amount of "make it brighter" fixes it, because the
brightest thing on that ramp is what it already was.

So the rule is now hue, not brightness: **weapon fire owns every hue the terrain
does not.** Six of them, each the top of one of the retail palette's pure ramps:

| name    | index | retail RGB  |
| ------- | ----- | ----------- |
| orange  | 171   | 255, 125, 0 |
| yellow  | 207   | 255, 255, 0 |
| rose    | 183   | 255, 0, 113 |
| magenta | 195   | 255, 0, 255 |
| cyan    | 219   | 0, 255, 255 |
| red     | 231   | 255, 0, 0   |

Green is gone from the roster entirely. Blue was tried and dropped: 148 is the
only usable blue in the palette and it scores worse against grey than any of the
six above.

Two knock-on changes were needed to make the rule true rather than nearly true:

- **The hazard stripe moved off the magenta ramp.** `MECHA_PAL_HAZARD` was 193,
  two steps under the magenta a weapon now fires. A painted stripe on a concrete
  block has no business competing with a bolt, so it went to 166, a dark amber —
  which is what hazard paint looks like anyway.
- **White is a blade and nothing else.** 143 is the one colour in the set a grey
  arena can swallow: against the pale top of a block it scored 27 on the measure
  below, worse than the green that started this. Every white *projectile* took
  its machine's own accent colour instead — which is better design as well as
  more visible, since a unit's beam now reads as that unit's. The blades kept
  it; a sword is swung at arm's length, where being the colour of steel is
  right.

### Measuring it

The test walks every flying weapon on the roster against every arena's floor,
grid and wall, and insists on a gap of 50. The measure is **not** a
channel-by-channel distance: that calls a saturated blue and a mid grey close,
because every channel is near the middle, which is exactly the mistake that
would let a shot go invisible. It is an opponent-colour distance — brightness,
red against green, blue against the other two.

On that measure the set as painted bottoms out at 56 (orange over the floor
grid). The green that started this scores 36 over the meadow, and 19 over the
canopy. Re-pointing one tracer back at green is what was used to prove the test
catches it.

### The fallback table was lying

Half of this was invisible because `s_aArenaPalette` — the colours the mode uses
when no retail palette is loaded — did not match the retail palette at the
indices that mattered. Index 148 was written as green and is blue; 183 was
"orange" and is hot pink; 193 and 194 were "hazard" and "amber" and are both
magenta. Every one of those is now taken straight from `PALETTE.PAL`, so the
no-data view and the retail view are the same picture, and the names beside them
say what is actually there.

______________________________________________________________________

## MESH-48 — a shot has to be worth a pixel

Small rounds were hard to see, and there were two reasons rather than one.

**A bare streak.** A solid round was drawn as its tracer and nothing else — one
tick of travel, widened towards the camera. That is fine at 20 m and gone by
100: the streak is as long as the distance the shot covered since the last tick,
which is short whenever the shot is far enough away for the foreshortening to
bite. Energy bolts had a head and solid rounds did not, and the ones that could
not be seen coming were the solid ones. They have a head now, in the shot's own
colour rather than a white core [DEF-12].

**A floor on apparent size.** The projection puts a world half-extent `h` at
distance `d` on screen at `h * 200 / d` pixels in the 320-wide frame the
rasteriser works in. The smallest round on the roster is 0.8 m, which is 1.6 px
across at 200 m — under the two pixels a moving dot needs to be followed by eye.
So a shot is never drawn subtending less than 0.0065 radians, capped at 3.2x its
own size so a distant round grows into a dot rather than a balloon. Measured:
16.0 px at 20 m, 3.6 at 90 m (the floor is not yet in play), 2.6 at 200 m
against 1.6 before.

**And they face the eye, not the view plane.** Both the billboards and the
tracer streaks took the camera's own heading, which is right only at the centre
of the screen; off to one side a streak was widened along a direction that was
not square to the eye and foreshortened to a hairline exactly where it was
hardest to see. Each shot now takes the heading from the eye to itself, and the
streak is widened perpendicular to both its flight path and the line to the eye.

Left alone deliberately: scenery, clouds and explosions still take the camera
yaw. They are large or distant enough that the difference is not visible, and
the silhouette tests measure them.

______________________________________________________________________

## REND-14 — HIT, over the clock

When the player lands a hit the word HIT flashes in red above the round clock,
which is the arcade convention and the one piece of feedback the arena had no
way of giving: at range, with the opponent's armour bar a bar rather than a
number, a shot that connects and a shot that misses look the same.

It is drawn straight off `iHitDealtTick` [TYPE-09] rather than off any state of
the HUD's own — the HUD is handed a world and draws it, and adding a countdown
to it would be the one thing in the renderer that had to be ticked. About half a
second, blinking at roughly 11 Hz, so a steady stream of hits reads as a stream
rather than as one continuous word.

Above the clock rather than over it: the clock is in the bottom right [REND-09]
and is the one number that still has to be readable while HIT is up.

______________________________________________________________________

## SIM-27 — a wreck burns, it does not pop

A kill was one flash and fourteen pieces of debris, over inside a second and a
half. A Whiplash car does something quite different: `dospray()`'s type-2
particles respawn for **as long as the car is dead**, most of them small and
every eighth much larger, with `sfxpend(SOUND_SAMPLE_EXPLO)` every four to seven
respawns. It is a continuous fire, not a bang.

The arena does the same thing off a clock, because nothing here ever repairs a
machine and a wreck would otherwise burn until the round ended. Four seconds, an
eruption every four ticks, every eighth of them big and out of the middle of the
hull rather than off its shell — measured at fifteen blasts a second, constant
across the whole burn and nothing before or after it.

The clock is its own field. `iStateTicks` looks like the natural place to read
it from, but a destroyed machine is no longer moved and nothing advances a clock
for it, so `iBurnTicks` counts itself down in the emitter.

**Fire is not livery.** The blast took `abyPalette[3]`, the machine's own
accent, which made a dead Exos 2000 a magenta bonfire. A weapon's blast keeps
the colour of whoever fired it — that is how a player reads whose it was — but a
machine coming apart is fire, and it now cycles three steps off the hot end of
the ramp cooling debris walks down [MESH-29].

______________________________________________________________________

## SIM-28 — a full effect table used to move the fight

Adding the wreck burn changed the outcome of a sixteen-way brawl, which for a
cosmetic effect should have been impossible. It took a bisect to believe.

`mecha_spawn_burst` draws three values off the **world** RNG per particle and
then asks for a slot. When the table was full it `return`ed — abandoning the
rest of the loop, and with it the draws those particles would have taken. So the
number of values pulled out of the sequence the fight is decided from depended
on how many cosmetic effects happened to be alive at that moment. A busy screen
dealt different cards.

It is a `continue` now: the particle is dropped, the draws still happen, and the
count depends only on the caller's `iCount`.

This is the same class of mistake the private per-machine `spray` RNG exists to
prevent [SIM-02], arriving by a different door — not a cosmetic thing *reading*
the shared sequence, but a cosmetic thing changing how much of it gets consumed.
Worth remembering that allocation failure is a side channel.

The regression test fills all ninety-six effect slots with something that
outlives the measurement, runs twenty-five seconds of a sixteen-way fight beside
an untouched copy, and requires every machine's position, facing, armour and
state to match exactly. It needed a crowd rather than a duel: bursts are thrown
by kills and blast-radius weapons, and a quiet duel never calls the function at
all — the first version of the test passed with the bug reintroduced, which is
the only reason it was caught.

______________________________________________________________________

## TEST-11 — one seed is a coin flip

The causeway test asserted that more than half of sixteen machines survive the
first ten seconds, measured at one seed. Changing the RNG sequence for an
unrelated reason [SIM-28] dropped that seed from ten survivors to eight and
failed the build.

Eight is not a regression. Measured across twelve seeds the same scenario runs
from eight survivors to fifteen, mean about eleven: the map has a hole in it and
sixteen machines fighting near an edge is a chaotic system. One seed against a
threshold near the middle of that spread is a coin flip that fails the day
anything shifts the sequence.

It runs four seeds now. Each has to clear a third — no single opening may be a
rout — and the total has to clear half, which is the property actually being
asserted: a fight on this map is decided by shooting rather than by everybody
walking off.

______________________________________________________________________

## SND-08 — one bang at a time, and who the big one belongs to

Two things came out of making a wreck burn [SIM-27].

**The blast channel needed a gap.** `pannedsample` keys its handle table on the
sample index alone, so starting `EXPLO` while `EXPLO` is already playing stops
the first one part way through. A burning wreck throws an eruption every four
ticks; without a gap the channel is not a roll of explosions, it is one sample
being restarted fifteen times a second, which is a stutter. A sixth of a second
between starts is enough for each to be a bang. Whiplash does the same thing
with `nExplosionSoundTimer`, four to seven respawns apart.

**The wreck sample was asking the wrong question.** Which explosion was a
machine coming apart used to be decided by size: `fScale >= 4 m`. A machine's
death blast is a quarter of its height, which across the whole roster is 2.85 m
to 4.25 m — so only the tallest machine ever tripped it, and the one thing that
reliably did was a large missile. `BIGCRASH` was effectively playing for
missiles and not for kills.

The machines are asked instead. A machine entering `MECHA_MOVE_DESTROYED` gets
the big crash once, on the frame it happens; every explosion effect is a blast.

## ARENA-20 — FACING WORLDS at twice the size

The stage was 350 m to a side and read as a corridor: two pads, two causeways,
and no distance between them worth crossing. Doubled, it is 700 m, and the duel
now starts 1015 m apart instead of 507 m.

Doubling a stage is not one number. Four had to move together:

- `fHalfExtent`, which is the stage.
- `iTerrainCells` 80 → 160, so a cell stays the same size on the ground. Leaving
  it at 80 would have doubled the cell and halved the resolution of every slope
  on the map, which is the one thing the causeway climb cannot afford.
- `iFloorTiles` 32 → 64, for the same reason on the checkerboard.
- Every hard-coded metre in the layout table, which is why the table is written
  against a local `const float m = MECHA_METRE * 2.0f;` rather than
  `MECHA_METRE`. One scale in one place: the layout below it reads in the same
  numbers it always did.

`MECHA_TERRAIN_CELLS` is the ceiling on the first of those, and it was 128. It
is now 160.

## ARENA-21 — cover, and the drop that makes a stage float

**Cover.** A causeway with nothing on it is a shooting gallery: whoever fires
first across 400 m of flat deck wins, and the walk across is a formality. The
blocks are on the lane centrelines in pairs, offset so neither side of a
causeway is a clear line, and low enough to duck behind but not to stop a jump.

**The drop.** `fDeckDrop` is how far below the top of the stage geometry stops
being drawn — three cells' worth here. Under it the stage simply ends, and what
is behind it is sky. That is the whole of the floating look: no skirt, no
underside, no shadow. `mecha_add_ground_quad` clamps each corner to
`fTop - fDeckDrop`, so a quad that would have run down the outside of the stage
is flattened into the cut instead of disappearing — the edge stays solid when
you stand on it and look down.

It is worth being explicit that this is a rendering cut and not a change to the
stage: the terrain under it is unchanged, the pits are where they were, and a
machine that walks off the edge falls exactly as far as it did before.

## MESH-49 — a sky on its side

Three things, all in the same dome.

**Black.** `bySkyFill` is the index the sky is cleared to when the arena is not
using clouds. The first version treated a non-zero fill as "this arena sets a
sky", which works for every colour except the one this stage wanted: black is
palette index 0. The kind is what is asked now (`bySkyKind != MECHA_SKY_CLOUDS`)
and the fill is just a colour.

**Stars.** The same dome the clouds hang on, with the cloud sprite swapped for a
flat white quad and the elevation band removed — a starfield has no horizon to
crowd against, so the elevation is drawn from the whole circle rather than from
`MECHA_CLOUD_FLOOR`..`MECHA_CLOUD_CEILING`. 420 of them, which is about forty in
view at any moment.

**On its side.** The dome is built in a canonical frame — elevation off the
horizon, azimuth around it — and then the frame's vertical and forward axes are
swapped. That moves the spin axis from straight up to straight out, so the sky
turns like a wheel standing in front of the player rather than like a ceiling
fan above them, and it turns clockwise, because a point climbing in azimuth goes
up and then right on a screen looking down +Z.

The swap has to be applied to the whole tangent frame, not just to the
direction. Tipping the direction alone leaves every quad facing where the
upright dome was, which is a sky of quads seen edge-on.

Two consequences worth writing down, because they are not obvious from the code:

- On the tipped dome, elevation no longer means height. It means how close to
  the world's +Z axis a thing orbits, and azimuth means where on that orbit it
  starts. The planet sits at 70 degrees not because it is high but because that
  is a circle twenty degrees wide around the axis, which keeps it in the sky as
  the sky turns.
- The stars are placed in the tipped frame too, so they wheel with it. They are
  the motion; the planet is the landmark that says which way the wheel is going.

**The planet is geometry.** It was going to be a generated sprite in a texture
bank of its own — a circle drawn into a tile, the way the cloud sprite works.
That cost two crashes and is not worth it: see REND-15. It is a fan of quads
instead, twenty wedges around a centre with the outer ring a shade deeper than
the face, which reads as a limb rather than as a flat coin. A dozen quads, no
artwork, and it is there on a checkout with no retail data at all.

## REND-15 — the tint banks were writing off the end of mapsel

Found while trying to give the planet a texture bank, and worth more than the
planet was.

The engine's legacy texture path is `uint8 *mapsel[4884]`, filled by
`setmapsel()` as 19 banks of 257 pointers. `setmapsel()` does no bounds check.
The arena mode's three tint banks were at engine banks 20, 21 and 22 — which is
`mapsel[5140..5911]`, a kilobyte past the end of the array, straight into
whatever the linker put next.

It never crashed on a checkout with no retail data, because with no data loaded
nothing else writes there. It crashes the moment a player has `FATDATA` and the
banks either side are real. That is a memory bug that only fires for the people
who own the game.

The banks are at 2, 3 and 4 now. The slot counts stay where they were — the
count is a separate array and was never the problem.

The second crash, at bank 5, was the same path from the other end: `polyt`
panics on a bank whose `slot->pixels` is null, which a generated bank is until
something fills it. Between the two, a generated texture bank is a poor way to
draw a circle, and the circle is built from quads instead [MESH-49].

## AI-14 — every climb read as a hole

On the doubled stage the pilots stopped 390 m apart and would not close. The
same AI closed to 2 m on the same stage at half the size.

It was not the way-spine, and it was not the range logic — two fixes aimed at
those changed nothing and were reverted. It was edge avoidance: disabling the
footing probe entirely closed the duel to 2 m, which said the probe was
rejecting ground that was there.

`mecha_ai_footing_run` walks a line ahead of the pilot and asks
`mecha_arena_ground_height` about each step. That function returns
`MECHA_ARENA_VOID` for terrain more than one step-up above the *feet* — which is
the right answer for "can I stand here from where I am", and the wrong question
to ask about a point 40 m away. On a causeway that climbs 64 m from base to
crest, every probe past the first few metres was above the feet, so every step
of the climb read as a hole and the pilot stood at the bottom of the ramp
refusing to walk up it.

The probe asks the terrain directly now, and compares each step against the
previous step rather than against the feet: a step is clear if it is not a pit,
is within `MECHA_AI_FOOTING_DROP` below and `MECHA_AI_FOOTING_CLIMB` above the
step before it, and is inside the stage. A ramp is a sequence of small rises,
which is exactly what that accepts, and a cliff edge is still a drop.

The duel closes to 22 m on the doubled stage.

This cost something, and the cost is recorded honestly in TEST-15: pilots now
stroll onto a roof about 9% of the time rather than 3%, because a climb they can
take is a climb they will take. A roof is walkable ground; it is a worse
position, not a broken one.

## TEST-12 — a look at every stage from outside it

`arena_survey%d.png` is one frame per arena from far enough out to see the whole
stage. It is not an assertion — it is the frame a person looks at when a stage
changes, and half the things that went wrong this phase (the sky that was not
black, the deck with no cut in it, the planet that was not there) were visible
in it before any test caught them.

## TEST-13 — a stage test that survives the stage being resized

The causeway tests probed hard-coded metre positions, so doubling the stage
failed nine of them at once — not because anything was wrong but because 200 m
along a 350 m causeway is somewhere else on a 700 m one.

They read a scale out of the arena now:
`fS = arena.fHalfExtent / MECHA_M(350.0f)`, and every probe is
`MECHA_M(x) * fS`. Resize the stage again and the tests follow it.

One thing to watch in a rewrite like this: a regex over `MECHA_M(...)` misses
every call with parentheses inside it (`MECHA_M((float)i * 4.0f)`). Those were
found by eye afterwards, which is not a method.

## TEST-15 — thirty fights is not a measurement

The roof-stroll rate — how often a pilot ends up on top of a block instead of on
the deck — was guarded by a 30-fight test with a 1-in-10 bound. After AI-14 the
true rate went from about 3% to about 9%, and a 30-fight sample cannot tell 9%
from 10%: the test passed and failed on the seed.

It runs 150 fights and allows 1 in 6. That is loose enough not to fail on noise
and tight enough to catch the AI walking onto roofs as a habit. The number it is
actually guarding is written in the test, along with what it was before AI-14,
because a bound with no history in it is a number nobody can ever move.

## TEST-16 — the first sky test was counting the HUD

The starfield check counted white pixels, and the arena's HUD font is the same
white. It reported eight thousand stars in a frame with no sky in it at all —
the camera was inside a block, and every one of those pixels was `READY`,
`ROUND 1` and the weapon list.

It counts a band of rows the HUD never writes to, and ticks past the round
banner before it looks. The numbers dropped from ~8200 to ~400, which is what
forty stars and a planet actually cost.

The lesson is the cheap one: a test that counts a colour is only as good as the
list of things that use that colour. The frame was dumped to a PNG all along —
one look at it would have said there was no sky in the shot.

## ARENA-22 — the hole a floating stage stands in was being drawn

The cut in ARENA-21 gave the stage an edge, and everything outside that edge
carried on being drawn: a second deck, the size of the whole arena, six hundred
metres under the first. It is easy to see why. The void is terrain like any
other — `mecha_arena_void()` writes a height into every node and the floor loop
draws a tile wherever the boundary contains one. Nothing in that loop knew the
difference between the stage and the bottom of the hole it is standing in.

The cut is one plane now, and it is measured between two numbers the arena works
out for itself:

- `fVoidY` is what `mecha_arena_void()` was last asked for. The bottom.
- `fDeckY` is the lowest **node** that is not the void. The stage's own lowest
  ground, found once when the arena is built.

A ground tile whose highest corner is below `fDeckY - fDeckDrop` has no part of
the stage in it and is not drawn. A tile that reaches the deck still is, and is
still cut to a thickness by the clamp, which is what keeps the edge solid.

**Nodes, not samples, and corners, not the middle.** Both of those were paid for
once each. The first version tested against `fVoidY` — "is every corner down at
the bottom?" — which removed the slab and left three dozen shards hanging in
space under the stage. Those are floor tiles lying across the drop: a tile is
twenty-two metres and a terrain cell is under nine, so a tile that straddles the
edge samples the cliff face and comes back at some height between deck and void.
Their corners were hundreds of metres above the void floor, so the void test
kept them, and hundreds below the deck, so they belonged to nothing. Measuring
from the deck instead catches them. Nodes matter for the same reason from the
other side: a node is either the stage or the void, where a sample halfway
between the two is neither, so `fDeckY` is found over the node array rather than
by asking for heights.

The guard is `a floating stage has nothing under it`: build the stage's mesh,
count the ground quads whose highest corner is below the cut, and require none —
with a floor under the ground-quad count too, so it cannot pass by the stage
failing to be drawn at all. With the skip removed it reports 3117 of 4168 ground
quads below the cut, the lowest at −600 m.

## ARENA-23 — a box with air under it

Every piece of cover the mode had was solid from the ground up, which is what a
box is and why the keeps were open to the sky: a roof would have been a lid with
no way under it. `fRise` is how far above its own base a box's underside sits.
Zero is everything that stands on the floor; anything else is a deck, solid
between `fBaseY + fRise` and `fBaseY + fRise + fHeight`, with a room underneath.

It touches four places, and two of them were already slightly wrong:

- **The ground query** already read `fBaseY`; it now reads the rise too.
- **The cylinder push-out** compared the feet against `fHeight` alone, as though
  every box stood at zero. On flat ground those agree; on a slope the collision
  was a step away from where the box is drawn. It reads both faces off `fBaseY`
  now, which fixes that and gives walking under a deck for free: a machine whose
  head is below the underside is not touching it.
- **The shot ray** sliced the box from 0 to `fHeight` for the same reason, and
  now slices between the two faces.
- **The mesh** draws from the underside, and draws the underside — a box on the
  floor needs none and is not given one, but a deck is the ceiling of the room
  below and without it that room is open to whatever is above the deck. That was
  visible immediately: standing under the new floor you could see straight
  through it to the roof.

`mecha_arena_ceiling_height` is the query that goes with it: the lowest deck
underside over a point that is above the feet. Only boxes with a rise answer,
which keeps it strictly additive — cover standing on the ground is a wall, and
walking into a wall is the cylinder's business, not the ceiling's. On a slope a
rock's base can be above a machine's feet, and calling that a ceiling would put
a lid over anyone standing beside it.

## SIM-29 — a ceiling, so a hole in the floor means something

A machine under a deck used to rise straight through it: the ground query hands
back a box top as floor once the feet are within a step of it, so a jump made
under the solid part of a floor ended with the machine standing on that floor.
The hole cut in it was then a decoration.

One clamp in the vertical step, after the position moves and only while it is
rising: if the head would pass the lowest deck overhead, the feet stop at that
deck less the machine's height and any upward speed is dropped. Asked from where
the feet were at the start of the tick, so a machine already standing on the
deck is not under it.

The measurement that decides the fort's dimensions is on the other side of this:
every machine's head now stops at exactly 19 m under the floor above, and the
same jump from the plinth under the opening arrives on the deck.

## ARENA-24 — two storeys in a fort, and a roof over them

**The floor above** is four slabs round a square opening. Nothing here can have
a hole in it, so the hole is what is left between the boxes. The second pair
stops a joint short of the first for the reason the walls do: two faces in one
place have nothing to decide which is in front [MESH-11].

The heights are not taste, they are the roster:

- The tallest machine stands 17 m, so the room under the deck is 19 m. A room
  shorter than the machine in it shoves that machine back out through the door —
  the deck's own collision does it.
- The floor is 3 m, so its top is at 22 m.
- The shortest jump on the roster climbs 21 m with the thrusters lit, which does
  not reach 22 from the floor. So the way up is a plinth: 9 m, under the
  opening, leaving a 13 m climb that every machine that jumps at all can make.
  The gun car cannot, and the gun car cannot climb anything.
- It costs boost. Unassisted jumps are 4 m to 16 m across the roster and the
  climb is 13, so getting upstairs is the same bargain as every other piece of
  height on this stage.

The plinth stands 16 m back from the middle of the keep rather than on it.
Machines spawn on the keep's centreline spread across its width, and with
sixteen on the field two of them per keep land within ten metres of the middle —
which is to say, inside a plinth sitting there.

**The roof** is a spire standing on the walls: 220 m on a fort 180 m across, a
joint clear of the wall tops because its underside covers every one of them and
coincident faces are the one thing this geometry may not have. It is a real lid
— the ceiling query finds it, so a machine that jumps off the second floor stops
under the roof instead of leaving over the wall. The way out of a fort is the
doorways, which is what a fort is.

Bronze rather than grey, off the low half of the orange ramp: the stage is grey
deck under a black sky and the roofs are the one thing on it with a colour,
while staying well below the bright end of the ramp that weapon fire owns
[DEF-12].

## MESH-50 — drawing a spire

A stack of six frusta rather than one. One frustum would do the same silhouette
in a quarter of the quads, but a side running the whole two hundred metres is a
single depth for a surface that spans the fort and everything near it, and this
renderer sorts by depth per quad with nothing to break the tie. Six courses sort
against the walls under them, and alternating the palette gives the roof banding
that reads as courses of tile rather than one face of colour.

It stops a whisker short of a point. A face whose two top corners are the same
point is a triangle, and whether the cross product that gives it its normal
comes out at all depends on which corners that face is built from: two of the
four came out as nothing and took the degenerate normal, which is straight up,
which is a pair of quads in one plane facing the same way — caught by the
coplanar test, which is what that test is for. The finial is two metres across
on a roof a hundred and eighty wide.

## ARENA-25 — a platform that has been broken rather than drawn

The two bases were rectangles. They are cut by eight planes now, each pushed in
by an amount of its own between 0.74 and 1.0 of the platform's half-width and
turned up to seven degrees off the even spacing, and the ground is kept only
where it is inside all of them.

Planes rather than a radius that varies with direction: a varying radius gives a
blob, and eight straight cuts at odd angles and odd depths give a slab that has
been broken. The terrain grid does the rest — an eight-metre cell steps a
diagonal edge, and a stepped edge reads as stone.

Each end is cut by its own seed, so they are two rocks rather than one rock and
its mirror. Neither cut can reach a keep: a keep reaches 0.47 of the platform's
half-width and the deepest cut stops at 0.74.

The one thing a cut could take that matters is the corner where the causeway
arrives, and it did — the last station of each way ended up over the void, which
the way test caught at once. The lane is painted sixty metres onto the base
rather than up to its edge: what that leaves is a tongue of causeway running
onto the rock, and ground under the way's last station whatever the cut does to
the edge beside it.

## TEST-17 — nested boxes broke the outward-faces test

`nothing is built coplanar` also checks that every side a block is built from
faces away from that block's middle, by taking the quads whose centroid lands in
a block's footprint. That works while no box stands over another. A fort's roof
covers the whole fort, so every wall inside it was suddenly a side of the roof —
and the walls face inwards, because that is the inside of the fort.

Three changes, each of which makes the test sharper rather than looser:

- **Height counts.** A quad belongs to a box only if its centroid is inside the
  box's vertical span as well as its footprint.
- **Ground is not a side.** Quads flagged as ground are skipped. A block stands
  on terrain, and the facets of that terrain answer to the slope, not to the
  block.
- **A cap is a cap.** Taking the ground out means the "roof, not a side" filter
  can be what it says — nearly horizontal — instead of a guess at how steep a
  side might be. It was 0.5, which excluded a spire's sides (33 degrees off
  vertical) from being checked at all.

## ARENA-26 — the keeps as towers

Walls twice the measured height (68 m to 136 m) and spires half again on top of
that (220 m to 330 m), so a keep stands 467 m from the rock to the finial. At
the old proportions the roof was most of the building; now the building is a
tower with a roof on it, and it is what the stage reads as from either end.

One thing had to be uncoupled to do it. The cover out on the causeways was sized
as a fraction of the wall height, which is a reasonable thing to measure against
right up until the wall doubles: cover that grew with the keeps would be a
sixty-metre wall down the middle of a lane. It has its own constant now, set to
the wall's old height, so the blocks are exactly what they were.

The second floor and the room under it are unchanged -- both are absolute
heights the roster decides [ARENA-24], not fractions of the wall. What the
taller wall buys is headroom above the second floor: 114 m of it rather than 46.

The stage's own survey frame had to pull back (1.9x the half-extent to 2.4x, and
the pitch off) to fit the spires in. Full load is 7098 quads of 12288.

## ARENA-27 — the line ruled under the stage

An open arena draws a lip round its boundary square: the platform's own edge,
seen from outside as you fall past it, and without it the roof arena is a paper
cutout. `fSkirt` is how deep it runs, and zero meant "use six metres" rather
than "none".

FACING WORLDS asks for no skirt, because its ground is a ribbon inside a square
that is mostly hole [ARENA-20] -- so the six-metre fallback drew a rectangle of
wall on the boundary, seven hundred metres out in empty space and the edge of
nothing at all. From any wide view it read as a thin horizontal line ruled under
the stage.

Zero means none now. The only other open arena sets its own skirt, so nothing
else changes, and a stage whose ground does not reach its own boundary cuts its
edge with `fDeckDrop` instead [ARENA-21].

## MESH-51 — the sky's axis, turned flat

The dome was tipped on its side, which put its spin axis along north-south:
standing at one end of the stage looking up it, the sky wheeled in front of you
like a wheel facing you. It is turned a further quarter anticlockwise in the
flat now, `(x, z) -> (-z, x)`, which takes that axis round to west-east.

The sky is edge-on to a player facing up the stage, so it climbs past them
rather than spinning in front of them, and face-on from either end. The rotation
itself is unchanged -- only which way the axle points.

Guarded by `the sky turns about a west-east axis`, which builds the sky twice a
quarter of an hour of ticks apart and asks which coordinate stayed put: a point
turning about the X axis keeps its X. Nothing moves more than a metre east;
everything moves two kilometres up and north.

## SND-09 — the one machine that is actually a car

The squeal moved off the angle between a machine's nose and its travel when it
became a boost sound [SND-06], and that was right for everything that walks: a
mecha strafes for a living, and walking sideways is not a slide.

The gun car is not one of those. It has no thrusters to scrub the floor with, so
a boost squeal on it is a noise coming from nothing, and the thing that does
make a tyre squeal -- the tyre going one way while the car points another -- is
exactly the rule that was taken away. Whiplash decides the same way, comparing
the steered yaw against the one the car ended up with.

So the skid loop branches on `bWheeled`, which is true of one machine on the
roster. The car gets the slip rule back, nine degrees off its nose before it
counts and thirty-eight for a full slide, with reversing excluded because a car
travelling a half-turn off its nose is going backwards rather than sliding.
Everything else keeps the ground dash. The pitch curve is the one the boost
squeal uses, which is where it was left after being taken down an octave.

## MESH-52 — the starfield turns ten times faster

The dome's rate was chosen for clouds, where a sky that is visibly moving is a
sky the player is watching instead of the fight: a shade under one circuit an
hour, which is a tenth of a degree a second. Over a stage with no weather and no
horizon the sky turning is the point rather than a distraction, and at that rate
a whole round goes by with the starfield apparently nailed in place.

The starfield gets its own multiplier on the same clock, ten. That is 1.1
degrees a second, a circuit every five and a half minutes, and a bit over a
quarter turn in a ninety second round -- motion a player can see happening
rather than motion they can only tell has happened. The cloud path is untouched,
so every other arena's sky drifts exactly as it did.

Measured rather than asserted: a star's angle about the axle advances 1.1
degrees a second, and its distance along the axle does not change at all, which
is the same thing `the sky turns about a west-east axis` guards [MESH-51].

## ARENA-28 — FACING WORLDS gets the retail artwork

Picked by eye off a numbered contact sheet of the decoded banks rather than by
measuring the banks for uniformity as everything before this was [DEF-04]. The
stage is concrete and rock, and both live in the track bank.

- **The keeps** are `track1` 240-242, grey concrete with conduit runs on it.
- **The ground** is 165-167, three courses of rust-brown strata.
- **The cover** is 168-173, a different pale stone per block.
- **The roofs** are `building` 23 on the west keep and 17 on the east, so the
  two ends of the stage are told apart at a distance by the one part of them
  that has only sky behind it.

Four pieces of machinery came out of it.

**A box can say which bank it is in.** Cover started life as buildings and the
building bank was the only one it ever wanted, so the mesh had the bank
hard-coded. `byBank` is per-obstacle, zero meaning the building bank, which is
what every arena that never set it has always meant. Worth noting what this also
fixed: `mecha_arena_face_stone` had been setting tile numbers out of the *track*
bank while the mesh drew them from the *building* bank, so the keeps were
wearing whatever two facades happened to sit at those indices.

**A wall can have more than one tile on it.** A keep is a hundred metres of
panelling a side and one tile across all of it is wallpaper. The detail run is a
range of tiles the mesh scatters over the side faces -- 234-245 here, the doors,
windows and machinery -- about one panel in four, hashed off the box and the
panel so it is the same wall every frame rather than something that crawls. A
pick that lands on the tile the wall is already made of is left alone.

**The ground can cycle more than two tiles.** A floor is a checkerboard because
a floor wants to read as a grid to move over; rock wants the opposite, a
rotation long enough that the eye does not find the repeat.

**The cover came down.** Fifteen to twenty-two metres rather than thirty to
forty-two, and a smaller footprint. The machines are eleven to seventeen metres:
cover twice the height of the machine behind it is a wall, and a wall is not
something you fight from. At this height a pilot standing behind one cannot see
over it and a pilot jumping can.

### The fallback colours, and the one place the measurement loses

Each palette entry beside a tile is the mean colour of the tile itself, walked
onto the nearest index the palette has -- the same method as REND-12, computed
rather than guessed. Keeps 123/125, cover 22/25, roofs 128 and 23.

The ground is the exception. Its tiles average a mid rust-brown, and the nearest
index to that (85) sits 46 from the orange tracer on the opponent-colour metric,
where the contrast guard wants 50 [DEF-12]. A brown ground and an orange tracer
are the same conflict the green-shot-over-grass was, and the guard caught it the
first time it was built. So the crag's fallback walks down the same ramp until
it clears: 29 and 47, darker and warmer than the tiles they stand in for. The
guard now reports 56 at its worst pair.

Only the three arena surface palettes are under that guard -- floor, grid and
wall. Cover, keeps and roofs are obstacle palettes and were free to take their
measured match.

### Two failures this turned up, neither of them fixed here

Running the headless render test with the retail data present -- which it has
plainly never had, since CI has none -- fails twice:

- `1 of 3 bolt tints built (textured)` at the tint check. Only one recoloured
  bank is ever asked for, so the other two are never built and the check that
  all three are up fails.
- The red-shot count check further down.

Both are the test's expectations meeting real artwork for the first time. They
are worth a pass of their own.

## TEST-18 — the render test had never seen the artwork

CI has no retail data and neither does a fresh checkout, so every frame the
headless render test had ever dumped was the flat-fill fallback. Run against a
FATDATA the first time, it failed twice, and both failures were the test's own.

**The tint check was measuring nothing.** It draws four bolts, one per
recoloured copy of the plasma frames, and then insists all three tints are up.
Its four colours were `{218, 171, 192, 255}` -- the tracer set from before
DEF-12 repainted them. Three of the four now fall through `mecha_bolt_bank` to
the same default, so only one tint was ever asked for and the other two were
never built. The check passed anyway on a data-less run, because with no artwork
none of them are built and the "or none of them" arm carries it. The colours are
taken off the bank function now: warm, magenta, rose, and one it leaves blue.

**The HIT check was counting the arena.** It compared how many red pixels the
whole frame had before and after putting a hit on the record. With the artwork
loaded the arena has reds of its own and the camera eases between one frame and
the next, so the count moved by more than a word of text is worth. It counts
inside the band the word is drawn in now -- which the placement check below it
was already doing.

That second one was hiding something real: see REND-16.

## REND-16 — HIT was not red for anyone who owns the game

The retail font's glyphs carry their own palette and cannot be tinted; the text
routine says so and drops the caller's colour on the floor when that font is
loaded [REND-01]. Everything else on this HUD is fine with that, because its
colour coding lives in the bars. HIT is not: a word whose whole job is to be red
came out in the face's own colour, and only for players with the game data
installed. On a checkout without it, the mode's own glyphs drew it red and it
looked exactly as intended.

`mecha_render_text_own` draws in the mode's five-by-seven glyphs whether the
retail face is loaded or not, and HIT uses it. It is the one string on the HUD
that does.

## ARENA-29 — sixteen per cent of the ground was not there

Measured before anything was changed: sample the stage on a two-metre grid, ask
each point whether a ground quad covers it and whether the collision calls it
solid. 94,163 points covered, 78,982 solid, **15,181 covered but not solid** --
a fringe all round the stage you could see and fall through, in places five
hundred and seventy metres above nothing at all. Nothing was solid but not
drawn.

Two causes, both of them the same mistake from opposite ends.

**The floor tiles were bigger than the terrain cells.** 64 tiles across 1400 m
is 22 m; the terrain is 160 cells, which is 8.75 m. A tile is drawn or not drawn
whole, and the cell is what decides which, so a tile that is wider than a cell
can only be one of the two things it is covering. One tile per cell now, and the
drawn edge is the solid edge.

**And a straddling tile's corners were clamped up.** ARENA-21 pulled the outer
corners of an edge tile up to the cut, which is what gave the stage its
thickness -- and what painted flat, solid-looking deck over the hole. There is
no clamp now: a tile is drawn only if all four of its corners are ground a
machine could stand on, and the thickness is drawn as its own geometry.

**The rim.** Wherever a drawn tile has no solid tile beside it, a quad drops
from that edge to `fDeckDrop` below it, following the ground's own height -- so
the rim under a causeway climbs with the causeway. Eight cells deep rather than
three, which is the "thicker" half of the request: at three it was a tabletop.

After: 77,672 covered, 79,839 solid, **nothing covered but not solid**. What is
left is 2,167 points that are solid but not drawn -- about a metre of slope
inside each edge cell, where the interpolation is still above the cut but the
cell is not drawn whole. That is the safe direction and it is narrower than a
machine's own radius.

Two things fell out of the same pass:

- **The wall panels were tied to the floor tiles.** `mecha_mesh_terrain` used
  one `fTile` for both, so the day the floor went to one tile per cell every
  wall in every arena went with it -- a keep is 136 m tall, and that turned four
  panels into thirty-six. Walls have their own panel size now, twenty metres,
  which is about the closest anything is looked at from.
- **The road branch ended in an unconditional `continue`.** Everything below it
  in the tile loop was code that never ran, which included the whole of the
  texture choice -- so the three-tile ground rotation added in ARENA-28 had
  never once been used. Rewritten as choose-then-draw.

Guarded by an addition to `a floating stage has nothing under it`: every flat
ground quad must have all four corners above the cut. Reverting the test to "any
corner" fails it.

## ARENA-30 — paying for a floor drawn at one tile per cell

One tile per terrain cell quadrupled the floor: 4,267 quads where there had been
about a thousand, and the sixteen-machine worst case went to 85% of the buffer
against a bound of 75%. The bound is there because the next thing anyone adds to
a machine comes out of it, so the floor had to give the space back.

Adjacent tiles that are **level** merge into one quad. Level is the whole of it:
a quad has four corners and nothing in between, so merging two tiles at
different heights throws away the step between them, which on a causeway that
climbs is the causeway. Both crags are flat, and they are most of the ground.

Two limits, each of which cost a pass to learn:

- **Two tiles, not eight.** A merged patch is one quad and one quad wears
  exactly one tile of artwork. At eight the rock was stretched seventy metres
  and the checker under it was gone -- and the checker is what tells a player
  the scale of the ground they are crossing. The first version of this failed
  the render test's "both checkerboard tones reach pixels" check on a flat
  square arena, which is that fact arriving as an assertion. At two the artwork
  lands at 17 m, finer than the 22 m this floor had before any of this.
- **Only a stage with an edge.** Merging pays for cell-sized tiles, and tiles
  are that fine only where the drawn edge has to be the solid edge. Everywhere
  else it would buy nothing and cost the checker.

A merged patch also has to have solid ground on the far side of it, so it can
never be the thing that meets the drop: the rim comes off a patch's own
boundary, and a patch that stopped short of the edge would hang its rim out over
the middle of the floor.

Floor 4,267 → 2,690, worst case 68% of the buffer.

## MESH-53 — the ankle

Every machine on the roster walked with its feet rigidly level. That is not an
oversight so much as an old decision working too well: the foot's pitch is built
to cancel the thigh's and the knee's exactly, so the sole stays parallel to the
floor whatever the leg does [MESH-19]. Right while the foot is on the ground,
wrong while it is not -- a boot on the end of a stick.

The flex is a toe drop weighted by how far the foot is off the floor, and it
goes one way: the ankle hangs as the leg lifts the foot and comes back to flat
as the leg puts it down. The first version swung it both ways -- toe down off
the push, through flat, toe up onto the landing -- and that reads as the ankle
rolling about underneath the machine rather than as a foot being carried. What
the foot does is follow the leg.

**Getting the lift right took three goes, and the first two are the note.** The
obvious weighting is the knee: the gait bends it on the half of the cycle where
`cos` is positive, so surely that is the half the foot is up. It is not. The
knee folds to take the weight as the body passes over the *planted* leg, and the
airborne half is the straight-legged one. Weighted that way the flex rolled
planted feet and put a toe half a metre through the floor. Weighted by `-cos`
instead it did the same thing on the other side, because the lift is not a half
of the cycle at all -- measured, a foot is meaningfully up for about a third of
one, in a window centred nowhere near either trig extreme.

So it is taken from the number that already knows. The builder sits the body so
the leg that reaches furthest is the one on the floor; how far short the other
leg falls **is** how high its foot is. Zero clearance is a planted foot and
comes back exactly flat, by construction rather than by tuning.

### Which way is down

Reasoning about the sign got it backwards, twice over, and the correction is
worth the space. A positive pose pitch swings a limb aft [MESH-04], and the foot
is the only limb on the machine that points **forwards** rather than down: swing
it aft and the toe comes **up**. Derived from the rotation instead, the flex was
added where it should have been subtracted, and every machine on the roster
walked pointing its toes at the sky.

The reason it survived a full pass of checking is that it is genuinely hard to
see in a quad list, and the obvious measurement lies. Take the foot as
everything within a fixed height of the sole, then read its front and back
vertices: a foot pitched far enough is taller than a flat one, the window clips
the raised heel, the back vertex lands on something else, and the tilt comes
back small or reversed. The sample that settled it was the one phase where the
pitch was shallow enough for the whole foot to fit the window -- and then the
answer was unambiguous and matched the angle to within a degree.

Two things fix this for next time. Print the angle from inside the builder
rather than inferring it from the mesh, and draw **one leg**: patching the leg
loop to a single side removes every ambiguity about which foot is which, and a
side-on plot of one leg over a cycle shows the whole thing at a glance.

### Testing it, and what would not work

Three attempts at measuring this from the finished mesh failed, and the reason
is worth keeping: **telling the two feet apart in a quad list is harder than the
thing being tested.** Sorting them by which side of the machine they are on
breaks because the legs cross in a stride -- exactly at the interesting moment.
Sorting them by height breaks through the double support, where both soles are
on the floor and any front-to-back measurement spans both feet at once. The
second version of the test passed with the flex compiled out entirely, which is
the clearest possible statement that it was measuring nothing.

What is tested instead: the rule directly, as a function -- flat at zero
clearance, growing one way with clearance, clamped past the span -- and of the
finished machine, the one thing that is unambiguous, which is that no part of a
leg goes through the floor. That is the assertion both bad versions failed.

### What the flex broke on the way past

A pitched foot also broke the bird-leg check in `legs walk on jointed knees`,
which had been reading the ankle as the centroid of everything within 0.09 of
the machine's height of the floor. That is a fair stand-in for the ankle only
while feet are rigidly level: cut a pitched foot at a fixed height and you catch
more of one end of it than the other, and the ankle appears to move. It read 167
units behind the knee with level feet, 66 with the flex in its wrong-signed form
-- under the bound of 80, which is how the failure surfaced -- and 535 once the
sign was right. That last number passes, and it is still a bad measurement: it
is three times the level reading, so what it mostly reports is how hard the toe
is pointing.

Widened to everything below the knee -- the shin's bottom and the whole foot, in
the window however the foot is turned -- it reads 304 level and 287 pitched. The
claim the test makes did not change; the thing it measures did.
