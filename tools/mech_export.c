/*
 * One machine out of the arena roster, as JSON: its skeleton, its polygons,
 * and for every polygon the bone that posed it.
 *
 * The mesh is procedural -- there is no model file anywhere in the tree, the
 * machine is assembled from boxes and frusta every frame -- so this is how a
 * modelling package gets hold of one. It leans entirely on the builder
 * already naming its joints [MESHH-05]: a quad that knows its bone is a quad
 * whose skin weight is known, and rigid weights are the right answer here
 * because the machines are plate armour, not skin.
 *
 * The machine is built standing, facing north, with the guns level and the
 * head straight -- see mech_bind_pose() for what that costs, because the
 * builder has no bind-pose mode and does not need one.
 *
 * Output goes to stdout. tools/export_mech_blend.py turns it into a .blend.
 *
 *   gcc -std=c99 -I PROJECTS/ROLLER PROJECTS/ROLLER/mecha_{math,arena,defs,
 *       sim,ai,mesh}.c PROJECTS/ROLLER/carplans.c tools/mech_export.c -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mecha_types.h"
#include "mecha_mesh.h"
#include "mecha_defs.h"
#include "mecha_sim.h"
#include "mecha_math.h"

static tMechaQuad s_aQuads[MECHA_QUAD_CAPACITY];
static tMechaWorld s_World;

/*
 * The project's own fallback colours, as 6-bit VGA. The retail palette is
 * not ours to ship and an exported model must not carry it, so what goes in
 * the file is the table mecha_render.c falls back to when there is no game
 * data -- which is the same table a player without the artwork sees, and
 * every index a machine paints itself with is in it.
 */
static const struct {
  uint8_t byIndex;
  uint8_t byR, byG, byB;
  uint8_t byGlow;      /* a light the machine carries, not paint it wears */
} s_aPalette[] = {
  { 105, 21, 21, 27, 0 },   /* joints      */
  { 115,  5,  5,  5, 0 },
  { 118, 11, 11, 11, 0 },
  { 119, 13, 13, 13, 0 },   /* dark hull   */
  { 120, 15, 15, 15, 0 },
  { 123, 21, 21, 21, 0 },
  { 124, 24, 24, 24, 0 },
  { 125, 26, 26, 26, 0 },   /* iron hull   */
  { 126, 28, 28, 28, 0 },
  { 127, 30, 30, 30, 0 },   /* dark trim   */
  { 128, 32, 32, 32, 0 },   /* steel hull  */
  { 129, 34, 34, 34, 0 },
  { 130, 36, 36, 36, 0 },
  { 136, 48, 48, 48, 0 },   /* steel trim  */
  { 137, 50, 50, 50, 0 },   /* pale hull   */
  { 141, 59, 59, 59, 0 },   /* pale trim   */
  { 163, 20, 10,  0, 0 },
  { 166, 37, 18,  0, 0 },
  /*
   * The pure-hue ramp the tracers and the glow trim come off, valued in
   * mecha_defs.c beside the indices themselves. These are lights the
   * machine carries rather than paint it wears, and are marked so: a
   * magenta vent rendered as flat magenta plastic is the one thing that
   * makes an exported mech look like a toy.
   */
  { 171, 63, 31,  0, 1 },   /* orange      */
  { 183, 63,  0, 28, 1 },   /* rose        */
  { 195, 63,  0, 63, 1 },   /* magenta     */
  { 207, 63, 63,  0, 1 },   /* yellow      */
  { 219,  0, 63, 63, 1 },   /* cyan        */
  { 231, 63,  0,  0, 1 },   /* red         */
};
#define PALETTE_COUNT ((int)(sizeof(s_aPalette) / sizeof(s_aPalette[0])))

static void palette_rgb(int iIndex, int *piR, int *piG, int *piB)
{
  int i;

  for (i = 0; i < PALETTE_COUNT; i++) {
    if (s_aPalette[i].byIndex == (uint8_t)iIndex) {
      /* 6-bit to 8-bit the way the engine does it: the top two bits
       * repeated, so 63 comes out 255 rather than 252. */
      *piR = (s_aPalette[i].byR << 2) | (s_aPalette[i].byR >> 4);
      *piG = (s_aPalette[i].byG << 2) | (s_aPalette[i].byG >> 4);
      *piB = (s_aPalette[i].byB << 2) | (s_aPalette[i].byB >> 4);
      return;
    }
  }
  *piR = *piG = *piB = 128;   /* an index the fallback does not name */
}

/*
 * Standing square, guns level, nothing in flight.
 *
 * The builder has no neutral mode: every machine is posed from simulated
 * state, which is the right design for a game and an awkward one for an
 * exporter. So the state is set rather than the builder changed. What is
 * left is the breath -- the stance sway that is the only movement in a
 * machine at ease -- and that is killed by picking the tick where it is at
 * zero, which is tick 0.
 */
static void mech_bind_pose(tMechaMech *pMech)
{
  pMech->byMove = MECHA_MOVE_STAND;
  pMech->fStepPhase = 0.0f;
  pMech->fCombat = 0.0f;       /* at ease, not squared up at a target */
  pMech->iTargetIdx = -1;
  pMech->fVelX = pMech->fVelY = pMech->fVelZ = 0.0f;
  pMech->fX = pMech->fY = pMech->fZ = 0.0f;
  pMech->iFacing = 0;
  pMech->iLegYaw = 0;
  pMech->iAimPitch = 0;
  pMech->iInvulnTicks = 0;
  pMech->iStateTicks = 0;
}

static void emit_json_string(const char *sz)
{
  putchar('"');
  for (; *sz; sz++) {
    if (*sz == '"' || *sz == '\\')
      putchar('\\');
    putchar(*sz);
  }
  putchar('"');
}

int main(int argc, char **argv)
{
  const char *szWanted = argc > 1 ? argv[1] : "Hiiragi 14A";
  const tMechaMechDef *pDef = NULL;
  tMechaBoneFrame aBones[MECHA_BONE_COUNT];
  tMechaQuadList list;
  int iDef = -1;
  int iFound;
  int i;
  int v;
  int iFirst;
  int iUntagged = 0;

  for (iFound = 0; iFound < mecha_def_count(); iFound++) {
    if (strcmp(mecha_def_get(iFound)->szName, szWanted) == 0) {
      iDef = iFound;
      break;
    }
  }
  if (iDef < 0) {
    fprintf(stderr, "no machine named '%s'. The roster is:\n", szWanted);
    for (iFound = 0; iFound < mecha_def_count(); iFound++)
      fprintf(stderr, "  %s\n", mecha_def_get(iFound)->szName);
    return 1;
  }

  mecha_sim_init(&s_World, 0, 0x1E65u, 1);
  mecha_sim_add_mech(&s_World, iDef, MECHA_CONTROL_HUMAN, 0);
  mecha_sim_add_mech(&s_World, iDef, MECHA_CONTROL_HUMAN, 1);
  mecha_sim_begin_match(&s_World);
  s_World.iTick = 0;
  pDef = mecha_def_get((int)s_World.aMechs[0].byDefIdx);
  mech_bind_pose(&s_World.aMechs[0]);
  /* The other machine is only here because a match needs two; park it far
   * enough away that nothing on machine 0 is aiming at it. */
  s_World.aMechs[1].bActive = false;

  mecha_quads_reset(&list, s_aQuads, MECHA_QUAD_CAPACITY);
  mecha_mesh_mech_rigged(&list, &s_World, 0, MECHA_DETAIL_FULL, aBones);

  printf("{\n");
  printf("  \"name\": ");
  emit_json_string(pDef->szName);
  printf(",\n  \"class\": ");
  emit_json_string(pDef->szClass);
  printf(",\n  \"height\": %.4f,\n", pDef->fHeight);
  printf("  \"metre\": %.4f,\n", (double)MECHA_METRE);

  printf("  \"bones\": [\n");
  iFirst = 1;
  for (i = 1; i < MECHA_BONE_COUNT; i++) {
    if (!aBones[i].bPosed)
      continue;
    if (!iFirst)
      printf(",\n");
    iFirst = 0;
    printf("    { \"id\": %d, \"name\": ", i);
    emit_json_string(mecha_bone_name(i));
    printf(", \"parent\": %d", mecha_bone_parent(i));
    printf(", \"origin\": [%.5f, %.5f, %.5f]",
           aBones[i].afOrigin[0], aBones[i].afOrigin[1],
           aBones[i].afOrigin[2]);
    /* The bone's own axes, columns of the rotation. A limb runs down its
     * -Y, so the exporter has everything it needs to point a bone. */
    printf(", \"axes\": [[%.5f, %.5f, %.5f], [%.5f, %.5f, %.5f],"
           " [%.5f, %.5f, %.5f]]",
           aBones[i].afRot[0][0], aBones[i].afRot[1][0], aBones[i].afRot[2][0],
           aBones[i].afRot[0][1], aBones[i].afRot[1][1], aBones[i].afRot[2][1],
           aBones[i].afRot[0][2], aBones[i].afRot[1][2], aBones[i].afRot[2][2]);
    printf(" }");
  }
  printf("\n  ],\n");

  printf("  \"palette\": [\n");
  for (i = 0; i < PALETTE_COUNT; i++) {
    int iR;
    int iG;
    int iB;

    palette_rgb(s_aPalette[i].byIndex, &iR, &iG, &iB);
    printf("    { \"index\": %d, \"rgb\": [%d, %d, %d], \"glow\": %s }%s\n",
           s_aPalette[i].byIndex, iR, iG, iB,
           s_aPalette[i].byGlow ? "true" : "false",
           i + 1 < PALETTE_COUNT ? "," : "");
  }
  printf("  ],\n");

  printf("  \"quads\": [\n");
  for (i = 0; i < list.iCount; i++) {
    if (s_aQuads[i].byBone == MECHA_BONE_NONE)
      iUntagged++;
    printf("    { \"bone\": %d, \"part\": %d, \"palette\": %d, \"v\": [",
           s_aQuads[i].byBone, s_aQuads[i].byPart, s_aQuads[i].byPalette);
    for (v = 0; v < 4; v++) {
      printf("%s[%.5f, %.5f, %.5f]", v ? ", " : "",
             s_aQuads[i].afVert[v][0], s_aQuads[i].afVert[v][1],
             s_aQuads[i].afVert[v][2]);
    }
    printf("] }%s\n", i + 1 < list.iCount ? "," : "");
  }
  printf("  ]\n}\n");

  iFound = 0;
  for (i = 1; i < MECHA_BONE_COUNT; i++)
    iFound += aBones[i].bPosed ? 1 : 0;
  fprintf(stderr, "%s: %d quads, %d bones posed, %d untagged, %d dropped\n",
          pDef->szName, list.iCount, iFound, iUntagged, list.iDropped);
  return 0;
}
