#ifndef _ROLLER_MECHA_MESH_H
#define _ROLLER_MECHA_MESH_H
//-------------------------------------------------------------------------------------------------
/*
 * Geometry for the arena mode, built procedurally every frame. The machines
 * are assembled from boxes and posed from the simulation's own state, so the
 * walk cycle and the knockdown are driven by the numbers the physics uses
 * rather than by canned animation, and the mode plays with nothing but the
 * base palette.
 *
 * The output is a flat list of world-space quads. Nothing here knows about
 * SDL or the renderer: the caller sorts back to front and submits.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_types.h"
//-------------------------------------------------------------------------------------------------

/* Drawn from both sides -- the renderer must not cull it. Billboards and the
 * arena floor use this; solid hull panels do not. */
#define MECHA_QUAD_TWO_SIDED 0x01
/* Self-lit: tracers, thruster plumes, explosions. A renderer that shades by
 * face angle should leave these alone. */
#define MECHA_QUAD_GLOW      0x02
/* Draw through the translucent shadow path rather than as solid fill. A
 * quad carrying this must put a shade level in byPalette, not a colour.
 * [MESH-01] */
#define MECHA_QUAD_SHADOW    0x04
/* Lies flat on a surface and belongs on top of it. Purely a sorting
 * instruction, which is the difference between it and SHADOW. [MESH-23] */
#define MECHA_QUAD_DECAL     0x08

/*
 * This quad wears artwork out of the game's own data files, so it keeps the
 * corner order it arrived in -- the frame change that got it here already
 * reflected it once. [REND-09]
 */
#define MECHA_QUAD_TEX_FLIP  0x10

/*
 * Turn this quad's artwork through half a turn as well. Reversing the corner
 * order alone is the vertical mirror, so the horizontal one is that reversal
 * plus this. [MESH-12]
 */
#define MECHA_QUAD_TEX_ROT180 0x20
/* And a quarter turn. With ROT180 a two-bit turn count, so all eight
 * arrangements are reachable. [MESH-12] */
#define MECHA_QUAD_TEX_ROT90  0x40

/*
 * Arena ground: a floor tile, the outer ring, the top of a block. Broad
 * horizontal geometry a machine stands on, which has to be drawn before the
 * machine standing on it -- so it is keyed by its far corner rather than its
 * middle. Only the arena sets it. Machine panels are horizontal too, and a
 * car's floor and roof taking the ground rule is what put the two halves of
 * an upside-down car in the wrong order. [MESH-23]
 */
#define MECHA_QUAD_GROUND     0x80

//-------------------------------------------------------------------------------------------------

/*
 * Which part of a machine a quad belongs to. Nothing in the renderer reads
 * this: it is here so that a caller wanting "the legs" or "the gun" can say
 * so. The tests used to name a box by counting how many the builder had
 * emitted before it, which was true right up until the builder emitted a
 * different number, and then quietly measured the wrong box rather than
 * failing. [MESHH-03]
 */
#define MECHA_PART_NONE    0
#define MECHA_PART_LEG     1   /* legs, tracks, spider limbs: the running gear */
#define MECHA_PART_TORSO   2
#define MECHA_PART_SKIRT   3
#define MECHA_PART_ARM     4
#define MECHA_PART_GUN     5
#define MECHA_PART_HEAD    6
#define MECHA_PART_THRUST  7   /* plumes and the pack they come out of */
#define MECHA_PART_DRONE   8   /* a carrier's pods, on the rack or in the air */

typedef struct
{
  float   afVert[4][3];   /* world space, wound counter-clockwise seen from the front */
  float   afNormal[3];    /* unit outward normal, for back-face rejection */
  uint8_t byPalette;
  uint8_t byFlags;
  /* Which tile of which bank, or MECHA_TEX_NONE. Named rather than
   * resolved, and falls back to byPalette. [MESHH-02] */
  uint8_t byTexBank;
  uint8_t byTile;
  uint8_t byPart;         /* MECHA_PART_*, for callers that want to find one */
} tMechaQuad;

/* The banks the mode draws from. The renderer maps these onto the engine's
 * own numbering, which is not the same. [REND-07] */
#define MECHA_TEX_NONE    0
#define MECHA_TEX_EFFECT  1   /* generic/car bank: explosions, fire, smoke */
#define MECHA_TEX_WORLD   2   /* track bank: ground, grass, walls */
#define MECHA_TEX_STRUCT  3   /* building bank: block faces */
/* The effect bank again, recoloured so a crossfire is readable. Falls back
 * to the blue one when there is no data to recolour. [REND-13] */
#define MECHA_TEX_EFFECT_WARM    4
#define MECHA_TEX_EFFECT_MAGENTA 5
#define MECHA_TEX_EFFECT_ROSE    6
/* The gun car's own skin: xzizin.bm, the same file the race game paints the
 * Zizin with, loaded into a car texture slot of its own. */
#define MECHA_TEX_CAR           7
#define MECHA_TEX_BANK_COUNT    8

/* One quad per polygon of the race game's Zizin plan, and they come first in
 * the machine's mesh: everything after them is the gun. */
#define MECHA_ZIZIN_BODY_QUADS 50

/*
 * Frames in the game's generic texture bank. Sequences run start..end
 * inclusive and are walked by an effect's age.
 */
#define MECHA_SPRITE_FIRE_FIRST   4
#define MECHA_SPRITE_FIRE_LAST    7
/* 8..12 are the sky's cloud puffs, and double as the glow on a plasma bolt.
 * 0 and 21..23 smoke, 1..3 start lights, 4..7 flame, 13..20 blast.
 * [MESHH-01] */
#define MECHA_SPRITE_CLOUD_FIRST 8
#define MECHA_SPRITE_CLOUD_LAST 12
#define MECHA_SPRITE_PLASMA_FIRST MECHA_SPRITE_CLOUD_FIRST
#define MECHA_SPRITE_PLASMA_LAST MECHA_SPRITE_CLOUD_LAST
#define MECHA_SPRITE_BLAST_FIRST 13
#define MECHA_SPRITE_BLAST_LAST  20
#define MECHA_SPRITE_SMOKE_FIRST 21
#define MECHA_SPRITE_SMOKE_LAST  23

/* Puffs in a landing's ring of dust. Enough to read as a circle from above
 * and as a spray from the side; many more and a landing is a smoke screen.
 * Out here because the tests count them. */
#define MECHA_DUST_PUFFS 7

/* Puffs on the surface of a bomb's standing fireball. Enough that the edge
 * of it reads as an edge, which is the only thing about it a player has to
 * judge. */
#define MECHA_SHELL_PUFFS 16

//-------------------------------------------------------------------------------------------------
/* A caller-owned, fixed-capacity quad buffer: nothing in the mode allocates.
 * iDropped records what an overflowing frame lost. [MESHH-02] */
typedef struct
{
  tMechaQuad *paQuads;
  int         iCount;
  int         iCapacity;
  int         iDropped;
  /* Stamped onto every quad added from here on, so a builder says which
   * part it is on once rather than on every call. [MESHH-03] */
  uint8_t     byPart;
} tMechaQuadList;

void mecha_quads_reset(tMechaQuadList *pList, tMechaQuad *paStorage,
                       int iCapacity);

/* What the quads added after this one belong to. Reset clears it. */
void mecha_quads_part(tMechaQuadList *pList, uint8_t byPart);

/* Appends one quad, deriving its normal from the first three vertices.
 * Returns false when the buffer is full. */
bool mecha_quads_add(tMechaQuadList *pList,
                     const float afVert[4][3],
                     uint8_t byPalette, uint8_t byFlags);

//-------------------------------------------------------------------------------------------------
/* Builders. Each appends to pList and leaves what is already there alone. */

void mecha_mesh_arena(tMechaQuadList *pList, const tMechaArena *pArena);

/*
 * How much of a machine to build. A player reads a machine by its outline,
 * so the outline survives every tier and what falls away is the trim that is
 * already sub-pixel by the time it does. Sixteen machines at full detail on
 * the largest arena do not fit in one quad buffer; tiering them is what makes
 * the detail affordable at all. [MESH-32]
 */
#define MECHA_DETAIL_FAR  0
#define MECHA_DETAIL_MID  1
#define MECHA_DETAIL_FULL 2

/* Which tier a machine that far from the eye is worth, in world units. The
 * thresholds live here with the geometry they drop. */
int mecha_mesh_detail_for_range(float fRange);

/* One posed mech. The walk cycle, the dash lean, the guard crouch and the
 * knockdown all come out of the mech's own simulated state. iDetail is one
 * of the tiers above; the tests build at MECHA_DETAIL_FULL. */
void mecha_mesh_mech(tMechaQuadList *pList, const tMechaWorld *pWorld,
                     int iMechIdx, int iDetail);

/* Projectiles and effects are camera-facing, so they need the view heading
 * the renderer is about to draw with. */
/*
 * Told by the render layer whether the sprite bank came up; the mesh is libc
 * only and cannot ask. A keyed frame and the flat square it falls back to
 * want different sizes. Defaults to false.
 */
void mecha_mesh_set_sprites(bool bAvailable);

/* And whether the gun car's own skin is. Without it the body falls back to
 * the machine's two palette entries, which is a car and not the car. */
void mecha_mesh_set_car_skin(bool bAvailable);

/*
 * The sky's cloud dome, at a radius that dwarfs the arena. Drawn only with
 * the sprite bank present: a cloud that falls back to a flat square is a
 * grey slab hanging in the air. [MESH-25]
 */
void mecha_mesh_clouds(tMechaQuadList *pList, const tMechaWorld *pWorld);

/* Billboarded trees standing outside the boundary, so an arena with nothing
 * to see at its edge still has somewhere to be the edge of. Non-collidable
 * and drawn only when the game's own sprite banks are installed. */
void mecha_mesh_scenery(tMechaQuadList *pList, const tMechaArena *pArena,
                        uint32_t uiSeed, int iCameraYaw);

/* Which recoloured copy of the effect bank a shot of this colour comes
 * from. The tracer index is all the geometry has. [REND-13] */
int mecha_bolt_bank(uint8_t byPalette);

/* Where a quad belongs in the painter's order, bigger first. The renderer
 * sorts on this and nothing else. [MESH-23] */
float mecha_quad_depth_key(const tMechaQuad *pQuad, const float afEye[3],
                           const float afForward[3]);

/* Shots take the eye position as well as the camera's heading: each one is
 * turned to face the eye rather than the view plane, and each is held to a
 * minimum apparent size so a small round stays visible across the arena.
 * [MESH-48] */
void mecha_mesh_projectiles(tMechaQuadList *pList, const tMechaWorld *pWorld,
                            int iCameraYaw, const float afEye[3]);
void mecha_mesh_effects(tMechaQuadList *pList, const tMechaWorld *pWorld,
                        int iCameraYaw);

/* The flat shadow every mech drops on whatever it is standing over. Cheap,
 * and the only ground contact cue the mode has. */
void mecha_mesh_shadows(tMechaQuadList *pList, const tMechaWorld *pWorld);

/*
 * How far a walking machine's ankle is off flat: how high the foot is off
 * the floor (fClear, over the fSpan that counts as fully up) times where
 * the leg is in its stride (fSwing, positive as it trails), scaled to
 * iRange. Zero clearance is a planted foot and comes back exactly flat.
 *
 * Public only so it can be tested. Telling the two feet apart in a finished
 * mesh is harder than the thing being tested -- they cross in a stride and
 * they are level through the double support -- so the rule is checked here
 * and the mesh is checked for the one thing that is unambiguous, which is
 * that no part of a leg goes through the floor. [MESH-53]
 */
int mecha_leg_ankle(float fClear, float fSpan, float fSwing, int iRange);

//-------------------------------------------------------------------------------------------------
/*
 * Enough for the arena, sixteen machines and a full projectile table, with
 * room over. The worst case the mode can reach is a team deathmatch on
 * FACING WORLDS, whose arena alone is 4346 quads -- over half of what 8192
 * was -- and measured over ninety seconds of that fight the scene peaked at
 * 6706 quads before the machines carried any detail and 8116 after, which
 * is ninety-nine per cent of the old buffer and not a margin. The detail
 * tiers are what keep it to 8116 rather than 8946; this is what keeps 8116
 * clear of the ceiling. It costs 272 KB of BSS, once, for the whole mode.
 * [MESHH-04]
 */
#define MECHA_QUAD_CAPACITY 12288
//-------------------------------------------------------------------------------------------------
#endif
