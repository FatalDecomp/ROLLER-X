#include "mecha_mesh.h"

#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_sim.h"

#include "carplans.h"
#include "types.h"

#include <math.h>
#include <string.h>

/* Which retail texture banks are loaded, set once a frame by the render
 * layer. Both start false, so a checkout with no data draws flat. */
static bool s_bSprites = false;
static bool s_bCarSkin = false;

//-------------------------------------------------------------------------------------------------
/* Palette indices used only by the geometry; see mecha_arena.c for the rest. */
#define MECHA_PAL_TRACER_CORE 143
/* Only ever seen if a cloud somehow rasterises flat, which the mesh refuses
 * to let happen -- a pale index so a bug reads as a bug and not as a hole. */
#define MECHA_PAL_CLOUD 143
/* What a puff of dust falls back to if it is ever drawn untextured. */
#define MECHA_PAL_SMOKE 137
/* A billboarded tree is nothing but its sprite, so this is only ever seen
 * where the sprite banks are missing -- and there the trees are not drawn at
 * all. It is the canopy green the built trees use, for the one case where a
 * bank loads but a tile in it does not. */
#define MECHA_PAL_CANOPY 252

/* How square to the sky a panel must look to be painted as the car's top. */
#define MECHA_ZIZIN_ROOF_FACING 0.55f

/* The gun: how far it swings off the nose, how it is held, what firing does
 * to it. */
#define MECHA_GUN_YAW_LIMIT   MECHA_DEG(40)
/* Where the mount sits on the car -- out over the right wing, forward of
 * the screen -- and how long the gun is against the car's own length. */
#define MECHA_GUN_MOUNT_X     0.825f
#define MECHA_GUN_MOUNT_Y     0.66f
#define MECHA_GUN_MOUNT_Z     1.03f
#define MECHA_GUN_LENGTH      0.33f
/* How far across the bonnet it points on top of wherever it is aiming. */
#define MECHA_GUN_ACROSS      MECHA_DEG(9)
#define MECHA_GUN_GRIP_RAKE   MECHA_DEG(22)
#define MECHA_GUN_KICK_TICKS  16
#define MECHA_GUN_KICK_PITCH  MECHA_DEG(34)

/*
 * Translucent quads carry a shade level in the low byte, not a colour:
 * shadow_poly indexes shade_palette[256 * level], and that table is 4096
 * bytes, so the level must stay under 16. These match the engine's own
 * callers, blankwindow and replay.c's car shadows. [MESH-01]
 */
#define MECHA_SHADE_SHADOW 3
#define MECHA_SHADE_DUST   2

/* The arena floor is a checkerboard rather than one big quad: the software
 * rasteriser has no depth buffer and no texture here, so the tiling is what
 * gives the ground any sense of distance at all. */
/* Across the whole arena, so a 220 metre floor at sixteen was stretching one
 * 64-pixel texture over fourteen metres of ground. Thirty-two puts a tile at
 * roughly the size of a road panel, which is the scale the artwork was drawn
 * at. It costs a thousand quads on a four-thousand budget. */
#define MECHA_FLOOR_TILES 32
/* A bigger arena needs more of them or every tile comes out stretched, but
 * the ground is most of the quad budget, so there is a ceiling. An arena
 * that is mostly hole draws only the ribbon it has, so it can afford more of
 * them than its area suggests -- and an arena with an edge wants its tiles
 * the size of its terrain cells, because a tile is drawn or not drawn whole
 * and the cell is what decides which. [ARENA-20, ARENA-29] */
#define MECHA_FLOOR_TILES_MAX 160
/* How wide a panel of wall is. Cover and keeps are cut into panels because
 * POLYTEX fits exactly one tile to a quad; twenty metres is about the
 * closest anything is ever looked at from. [ARENA-29] */
#define MECHA_PANEL_SIZE MECHA_M(20.0f)
/* How many courses a spire is drawn in, and how much of its base is left
 * at the top of it rather than coming to an exact point. [MESH-50] */
#define MECHA_SPIRE_TIERS 6
#define MECHA_SPIRE_TIP   0.02f
/* How the ground outside the arena is drawn: rings of the boundary's own
 * shape, each one cut into this many quads a side. It is scenery. */
#define MECHA_OUTER_RINGS 4
#define MECHA_OUTER_SPANS 3

/* Number of ticks a knocked-down mech takes to actually hit the floor. */
#define MECHA_FALL_TICKS 8

//-------------------------------------------------------------------------------------------------

void mecha_quads_reset(tMechaQuadList *pList, tMechaQuad *paStorage,
                       int iCapacity)
{
  if (!pList)
    return;
  pList->paQuads = paStorage;
  pList->iCapacity = paStorage ? iCapacity : 0;
  pList->iCount = 0;
  pList->iDropped = 0;
  pList->byPart = MECHA_PART_NONE;
  pList->byBone = MECHA_BONE_NONE;
}

//-------------------------------------------------------------------------------------------------

void mecha_quads_part(tMechaQuadList *pList, uint8_t byPart)
{
  if (pList) {
    pList->byPart = byPart;
    /* A new section of the machine starts off nobody's bone: the primitives
     * that are posed set it, and the ones that are not stay honest. */
    pList->byBone = MECHA_BONE_NONE;
  }
}

//-------------------------------------------------------------------------------------------------

bool mecha_quads_add(tMechaQuadList *pList, const float afVert[4][3],
                     uint8_t byPalette, uint8_t byFlags)
{
  tMechaQuad *pQuad;
  float afEdge1[3];
  float afEdge2[3];
  float fLength;
  int i;

  if (!pList || !pList->paQuads)
    return false;
  if (pList->iCount >= pList->iCapacity) {
    pList->iDropped++;
    return false;
  }

  pQuad = &pList->paQuads[pList->iCount];
  memcpy(pQuad->afVert, afVert, sizeof(pQuad->afVert));
  pQuad->byPalette = byPalette;
  pQuad->byFlags = byFlags;
  pQuad->byTexBank = MECHA_TEX_NONE;
  pQuad->byTile = 0;
  pQuad->byPart = pList->byPart;
  pQuad->byBone = pList->byBone;

  for (i = 0; i < 3; i++) {
    afEdge1[i] = afVert[1][i] - afVert[0][i];
    afEdge2[i] = afVert[2][i] - afVert[0][i];
  }
  pQuad->afNormal[0] = afEdge1[1] * afEdge2[2] - afEdge1[2] * afEdge2[1];
  pQuad->afNormal[1] = afEdge1[2] * afEdge2[0] - afEdge1[0] * afEdge2[2];
  pQuad->afNormal[2] = afEdge1[0] * afEdge2[1] - afEdge1[1] * afEdge2[0];
  fLength = mecha_length3(pQuad->afNormal[0], pQuad->afNormal[1],
                          pQuad->afNormal[2]);
  if (fLength > 1e-6f) {
    for (i = 0; i < 3; i++)
      pQuad->afNormal[i] /= fLength;
  } else {
    /* Degenerate quad -- keep it drawable but never cullable. */
    pQuad->afNormal[0] = 0.0f;
    pQuad->afNormal[1] = 1.0f;
    pQuad->afNormal[2] = 0.0f;
    pQuad->byFlags |= MECHA_QUAD_TWO_SIDED;
  }

  pList->iCount++;
  return true;
}

//-------------------------------------------------------------------------------------------------
/* Local-space assembly */

typedef struct
{
  float afRot[3][3];   /* local axes expressed in world space, as columns */
  float afOrigin[3];
  float fVerticalScale;
  const tMechaMech *pMech;
  /*
   * Which joint of the skeleton this frame is, once something has named it.
   * A pose hung off a named one inherits the name, so the trim bolted to a
   * shin is on the shin's bone without anyone having to say so, and only
   * the joints themselves need naming. [MESHH-05]
   */
  uint8_t byBone;
} tMechaPose;

//-------------------------------------------------------------------------------------------------

static void mecha_matrix_multiply(float afOut[3][3], const float afA[3][3],
                                  const float afB[3][3])
{
  float afTemp[3][3];
  int iRow;
  int iCol;

  for (iRow = 0; iRow < 3; iRow++) {
    for (iCol = 0; iCol < 3; iCol++) {
      afTemp[iRow][iCol] = afA[iRow][0] * afB[0][iCol]
                         + afA[iRow][1] * afB[1][iCol]
                         + afA[iRow][2] * afB[2][iCol];
    }
  }
  memcpy(afOut, afTemp, sizeof(afTemp));
}

//-------------------------------------------------------------------------------------------------

/* Yaw about the vertical, then pitch forward, then roll sideways -- applied
 * in that order so a leaning mech that then falls over falls the way it was
 * facing rather than the way it was leaning. */
static void mecha_pose_build(tMechaPose *pPose, int iYaw, int iPitch,
                             int iRoll, float fX, float fY, float fZ,
                             float fVerticalScale)
{
  float fCosY = mecha_cos(iYaw);
  float fSinY = mecha_sin(iYaw);
  float fCosP = mecha_cos(iPitch);
  float fSinP = mecha_sin(iPitch);
  float fCosR = mecha_cos(iRoll);
  float fSinR = mecha_sin(iRoll);
  const float afYawM[3][3] = {
    {  fCosY, 0.0f, fSinY },
    {  0.0f,  1.0f, 0.0f  },
    { -fSinY, 0.0f, fCosY },
  };
  const float afPitchM[3][3] = {
    { 1.0f, 0.0f,   0.0f  },
    { 0.0f, fCosP, -fSinP },
    { 0.0f, fSinP,  fCosP },
  };
  const float afRollM[3][3] = {
    { fCosR, -fSinR, 0.0f },
    { fSinR,  fCosR, 0.0f },
    { 0.0f,   0.0f,  1.0f },
  };

  mecha_matrix_multiply(pPose->afRot, afYawM, afPitchM);
  mecha_matrix_multiply(pPose->afRot, pPose->afRot, afRollM);
  pPose->afOrigin[0] = fX;
  pPose->afOrigin[1] = fY;
  pPose->afOrigin[2] = fZ;
  pPose->fVerticalScale = fVerticalScale;
  pPose->pMech = NULL;
  pPose->byBone = MECHA_BONE_NONE;
}

//-------------------------------------------------------------------------------------------------

static void mecha_pose_apply(const tMechaPose *pPose, float fX, float fY,
                             float fZ, float afOut[3])
{
  float fScaledY = fY * pPose->fVerticalScale;

  afOut[0] = pPose->afRot[0][0] * fX + pPose->afRot[0][1] * fScaledY
           + pPose->afRot[0][2] * fZ + pPose->afOrigin[0];
  afOut[1] = pPose->afRot[1][0] * fX + pPose->afRot[1][1] * fScaledY
           + pPose->afRot[1][2] * fZ + pPose->afOrigin[1];
  afOut[2] = pPose->afRot[2][0] * fX + pPose->afRot[2][1] * fScaledY
           + pPose->afRot[2][2] * fZ + pPose->afOrigin[2];
}

//-------------------------------------------------------------------------------------------------

/*
 * A pose hung off another. The pivot is a point in the parent's space and
 * the rotation is the parent's plus a further turn, so a forearm swings
 * about an elbow swinging about a shoulder. Every limb is a chain of these;
 * only the root knows where it is in the world.
 */
static void mecha_pose_child(tMechaPose *pOut, const tMechaPose *pParent,
                             float fPivotX, float fPivotY, float fPivotZ,
                             int iYaw, int iPitch, int iRoll)
{
  tMechaPose local;
  float afOrigin[3];

  mecha_pose_apply(pParent, fPivotX, fPivotY, fPivotZ, afOrigin);
  mecha_pose_build(&local, iYaw, iPitch, iRoll, 0.0f, 0.0f, 0.0f, 1.0f);
  mecha_matrix_multiply(pOut->afRot, pParent->afRot, local.afRot);
  pOut->afOrigin[0] = afOrigin[0];
  pOut->afOrigin[1] = afOrigin[1];
  pOut->afOrigin[2] = afOrigin[2];
  pOut->fVerticalScale = pParent->fVerticalScale;
  pOut->pMech = pParent->pMech;
  pOut->byBone = pParent->byBone;
}

//-------------------------------------------------------------------------------------------------

/*
 * Name a frame as a joint of the skeleton, and write down where it ended up
 * if anyone is collecting. Called on the frames that are joints; everything
 * hung off one inherits it. [MESHH-05]
 */
static void mecha_pose_name(tMechaPose *pPose, int iBone,
                            tMechaBoneFrame *paBones)
{
  if (pPose->pMech && iBone > MECHA_BONE_ROOT
      && iBone < MECHA_BONE_COUNT) {
    float fWeight = 0.0f;
    int iAngle[MECHA_RAGDOLL_AXES];
    tMechaPose ragdoll;
    int iAxis;

    switch (pPose->pMech->byMove) {
    case MECHA_MOVE_DOWN:
      fWeight = mecha_clampf((float)pPose->pMech->iStateTicks
                             / (float)MECHA_FALL_TICKS, 0.0f, 1.0f);
      break;
    case MECHA_MOVE_RISE:
      fWeight = mecha_clampf((float)pPose->pMech->iStunTicks
                             / (float)MECHA_RISE_TICKS, 0.0f, 1.0f);
      break;
    case MECHA_MOVE_DESTROYED:
      fWeight = 1.0f;
      break;
    default:
      break;
    }
    if (fWeight > 0.0f) {
      for (iAxis = 0; iAxis < MECHA_RAGDOLL_AXES; iAxis++)
        iAngle[iAxis] = (int)((float)pPose->pMech
                              ->aiRagdollAngle[iBone][iAxis] * fWeight);
      mecha_pose_build(&ragdoll, iAngle[1], iAngle[0], iAngle[2],
                       0.0f, 0.0f, 0.0f, 1.0f);
      mecha_matrix_multiply(pPose->afRot, pPose->afRot, ragdoll.afRot);
    }
  }
  pPose->byBone = (uint8_t)iBone;
  if (!paBones || iBone <= MECHA_BONE_NONE || iBone >= MECHA_BONE_COUNT)
    return;
  memcpy(paBones[iBone].afRot, pPose->afRot, sizeof(paBones[iBone].afRot));
  memcpy(paBones[iBone].afOrigin, pPose->afOrigin,
         sizeof(paBones[iBone].afOrigin));
  paBones[iBone].bPosed = true;
}

//-------------------------------------------------------------------------------------------------

/*
 * The skeleton, written down once. Names are the ones a modelling package
 * expects -- the .L/.R suffix is what makes a rig mirrorable there -- and
 * the parent column is the chain the builder already walks. [MESHH-05]
 */
static const struct
{
  const char *szName;
  uint8_t     byParent;
} s_aBones[MECHA_BONE_COUNT] = {
  [MECHA_BONE_NONE]       = { "",            MECHA_BONE_NONE },
  [MECHA_BONE_ROOT]       = { "root",        MECHA_BONE_NONE },
  [MECHA_BONE_PELVIS]     = { "pelvis",      MECHA_BONE_ROOT },
  [MECHA_BONE_TORSO]      = { "torso",       MECHA_BONE_ROOT },
  [MECHA_BONE_HEAD]       = { "head",        MECHA_BONE_TORSO },
  [MECHA_BONE_SKIRT_FRONT_L] = { "skirt_front.L", MECHA_BONE_PELVIS },
  [MECHA_BONE_SKIRT_FRONT_R] = { "skirt_front.R", MECHA_BONE_PELVIS },
  [MECHA_BONE_SKIRT_SIDE_L]  = { "skirt_side.L",  MECHA_BONE_PELVIS },
  [MECHA_BONE_SKIRT_SIDE_R]  = { "skirt_side.R",  MECHA_BONE_PELVIS },
  [MECHA_BONE_SKIRT_REAR_L]  = { "skirt_rear.L",  MECHA_BONE_PELVIS },
  [MECHA_BONE_SKIRT_REAR_R]  = { "skirt_rear.R",  MECHA_BONE_PELVIS },
  [MECHA_BONE_SHOULDER_L] = { "shoulder.L",  MECHA_BONE_TORSO },
  [MECHA_BONE_SHOULDER_R] = { "shoulder.R",  MECHA_BONE_TORSO },
  [MECHA_BONE_UPPERARM_L] = { "upper_arm.L", MECHA_BONE_SHOULDER_L },
  [MECHA_BONE_UPPERARM_R] = { "upper_arm.R", MECHA_BONE_SHOULDER_R },
  [MECHA_BONE_FOREARM_L]  = { "forearm.L",   MECHA_BONE_UPPERARM_L },
  [MECHA_BONE_FOREARM_R]  = { "forearm.R",   MECHA_BONE_UPPERARM_R },
  [MECHA_BONE_HAND_L]     = { "hand.L",      MECHA_BONE_FOREARM_L },
  [MECHA_BONE_HAND_R]     = { "hand.R",      MECHA_BONE_FOREARM_R },
  [MECHA_BONE_HIP_L]      = { "hip.L",       MECHA_BONE_ROOT },
  [MECHA_BONE_HIP_R]      = { "hip.R",       MECHA_BONE_ROOT },
  [MECHA_BONE_THIGH_L]    = { "thigh.L",     MECHA_BONE_HIP_L },
  [MECHA_BONE_THIGH_R]    = { "thigh.R",     MECHA_BONE_HIP_R },
  [MECHA_BONE_SHIN_L]     = { "shin.L",      MECHA_BONE_THIGH_L },
  [MECHA_BONE_SHIN_R]     = { "shin.R",      MECHA_BONE_THIGH_R },
  [MECHA_BONE_FOOT_L]     = { "foot.L",      MECHA_BONE_SHIN_L },
  [MECHA_BONE_FOOT_R]     = { "foot.R",      MECHA_BONE_SHIN_R },
};

const char *mecha_bone_name(int iBone)
{
  if (iBone <= MECHA_BONE_NONE || iBone >= MECHA_BONE_COUNT)
    return "";
  return s_aBones[iBone].szName;
}

int mecha_bone_parent(int iBone)
{
  if (iBone <= MECHA_BONE_NONE || iBone >= MECHA_BONE_COUNT)
    return MECHA_BONE_NONE;
  return (int)s_aBones[iBone].byParent;
}

//-------------------------------------------------------------------------------------------------
/*
 * Corner numbering packs the three sign bits: bit 0 is +X, bit 1 is +Y,
 * bit 2 is +Z. The face table below is wound so that the normal
 * mecha_quads_add derives from the first three vertices points outward,
 * which is what makes back-face rejection work on a closed box.
 */
static const uint8_t s_aabyBoxFaces[6][4] = {
  { 1, 3, 7, 5 },   /* +X */
  { 0, 4, 6, 2 },   /* -X */
  { 2, 6, 7, 3 },   /* +Y */
  { 0, 1, 5, 4 },   /* -Y */
  { 4, 5, 7, 6 },   /* +Z */
  { 0, 2, 3, 1 },   /* -Z */
};

//-------------------------------------------------------------------------------------------------

static void mecha_add_box(tMechaQuadList *pList, const tMechaPose *pPose,
                          float fCx, float fCy, float fCz,
                          float fHx, float fHy, float fHz,
                          uint8_t byPalette, uint8_t byTopPalette,
                          uint8_t byFlags)
{
  float afCorner[8][3];
  int iCorner;
  int iFace;

  /* Everything built in a frame belongs to that frame's bone.
   * [MESHH-05] */
  pList->byBone = pPose->byBone;

  for (iCorner = 0; iCorner < 8; iCorner++) {
    mecha_pose_apply(pPose,
                     fCx + ((iCorner & 1) ? fHx : -fHx),
                     fCy + ((iCorner & 2) ? fHy : -fHy),
                     fCz + ((iCorner & 4) ? fHz : -fHz),
                     afCorner[iCorner]);
  }

  for (iFace = 0; iFace < 6; iFace++) {
    float afVert[4][3];
    int i;

    for (i = 0; i < 4; i++)
      memcpy(afVert[i], afCorner[s_aabyBoxFaces[iFace][i]], sizeof(afVert[i]));
    /* Face 2 is the +Y cap; picking it out is how a hull panel and the plate
     * on top of it get different colours from one call. */
    mecha_quads_add(pList, afVert,
                    iFace == 2 ? byTopPalette : byPalette, byFlags);
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * A box whose top face is a different size from its bottom, may be offset
 * from directly above it, and may be raked -- tipped about its own aft
 * edge so the forward end sits lower. Every side stays planar because both
 * of its horizontal edges keep their axis, so this costs exactly what a box
 * costs and buys the tapers, wedges and sloped plates the boxes could not
 * make. [MESH-31]
 *
 * The rake is the one degree of freedom a tilted sub-pose cannot stand in
 * for. A sub-pose turns the whole solid, floor face and all, which is fine
 * for a limb and wrong for a plate: rake a foot by rotating it and the sole
 * leaves the ground. Raking here shears the top face alone, measured as the
 * drop per unit of +Z across it and hinged on the aft edge, so a rake only
 * ever cuts material away from a shape that already fits -- the sole, the
 * silhouette's high point and every other face stay exactly where they
 * were. [MESH-55]
 *
 * The nose is the one parameter that costs something. Give the two ends of
 * a face different widths and the side quads stop being planar -- on the
 * shapes here by a couple of centimetres on an eleven-metre machine, which
 * is under a pixel at any range the mesh is drawn at, and is the same warp
 * the hand-sculpted reference carries. Everything else here keeps every
 * face flat. [MESH-55]
 */
static void mecha_add_hull(tMechaQuadList *pList, const tMechaPose *pPose,
                           float fCx, float fCy, float fCz,
                           float fHx0, float fHz0,
                           float fHx1, float fHz1,
                           float fHy, float fSkewX, float fSkewZ,
                           float fRakeZ, float fNoseX0, float fNoseX1,
                           uint8_t byPalette, uint8_t byTopPalette,
                           uint8_t byFlags)
{
  float afCorner[8][3];
  int iCorner;
  int iFace;

  /* Everything built in a frame belongs to that frame's bone.
   * [MESHH-05] */
  pList->byBone = pPose->byBone;

  for (iCorner = 0; iCorner < 8; iCorner++) {
    bool bTop = (iCorner & 2) != 0;
    float fHx = bTop ? fHx1 : fHx0;
    float fHz = bTop ? fHz1 : fHz0;
    float fOffX = bTop ? fSkewX : 0.0f;
    float fOffZ = bTop ? fSkewZ : 0.0f;
    /* Hinged on the aft edge: the -Z corners of the top face do not move,
     * the +Z corners drop by the rake over the face's whole depth. */
    float fRake = (bTop && (iCorner & 4)) ? fRakeZ * 2.0f * fHz1 : 0.0f;

    /* The nose narrows the forward edge of each face on its own, which is
     * what makes a chin a chin and a prow a prow. */
    if (iCorner & 4)
      fHx *= bTop ? fNoseX1 : fNoseX0;

    mecha_pose_apply(pPose,
                     fCx + fOffX + ((iCorner & 1) ? fHx : -fHx),
                     fCy + (bTop ? fHy : -fHy) - fRake,
                     fCz + fOffZ + ((iCorner & 4) ? fHz : -fHz),
                     afCorner[iCorner]);
  }

  for (iFace = 0; iFace < 6; iFace++) {
    float afVert[4][3];
    int i;

    for (i = 0; i < 4; i++)
      memcpy(afVert[i], afCorner[s_aabyBoxFaces[iFace][i]], sizeof(afVert[i]));
    mecha_quads_add(pList, afVert,
                    iFace == 2 ? byTopPalette : byPalette, byFlags);
  }
}

/* The same solid with its forward edges full width. */
static void mecha_add_raked(tMechaQuadList *pList, const tMechaPose *pPose,
                            float fCx, float fCy, float fCz,
                            float fHx0, float fHz0,
                            float fHx1, float fHz1,
                            float fHy, float fSkewX, float fSkewZ,
                            float fRakeZ,
                            uint8_t byPalette, uint8_t byTopPalette,
                            uint8_t byFlags)
{
  mecha_add_hull(pList, pPose, fCx, fCy, fCz, fHx0, fHz0, fHx1, fHz1,
                 fHy, fSkewX, fSkewZ, fRakeZ, 1.0f, 1.0f,
                 byPalette, byTopPalette, byFlags);
}

/* The same solid with a flat top, which is what almost every call wants. */
static void mecha_add_frustum(tMechaQuadList *pList, const tMechaPose *pPose,
                              float fCx, float fCy, float fCz,
                              float fHx0, float fHz0,
                              float fHx1, float fHz1,
                              float fHy, float fSkewX, float fSkewZ,
                              uint8_t byPalette, uint8_t byTopPalette,
                              uint8_t byFlags)
{
  mecha_add_raked(pList, pPose, fCx, fCy, fCz, fHx0, fHz0, fHx1, fHz1,
                  fHy, fSkewX, fSkewZ, 0.0f, byPalette, byTopPalette,
                  byFlags);
}

//-------------------------------------------------------------------------------------------------

/*
 * Is every corner of this tile ground a machine could stand on?
 *
 * A stage that floats is a ribbon of ground inside a square that is mostly
 * void, and the void has a height like anything else -- so drawn honestly
 * it is a second deck, the size of the whole arena, a few hundred metres
 * under the first. Drawing only the tiles that touch the deck fixed that,
 * and left the other half of the same problem: a tile with one corner on
 * the deck and three over the hole was still drawn, and drawn flat, because
 * the corners were clamped up to the cut. Sixteen per cent of the ground
 * you could see was ground you fell through, in places by five hundred
 * metres.
 *
 * So: all four corners, and all four have to be above the cut. What that
 * leaves drawn is what the collision calls solid, and the edge of the one
 * is the edge of the other. [ARENA-29]
 */
static bool mecha_ground_is_solid(const tMechaArena *pArena,
                                  float fX0, float fZ0, float fX1, float fZ1)
{
  float fCut;

  if (pArena->fDeckDrop <= 0.0f)
    return true;

  fCut = pArena->fDeckY - pArena->fDeckDrop;
  return mecha_arena_terrain_height(pArena, fX0, fZ0) > fCut
         && mecha_arena_terrain_height(pArena, fX0, fZ1) > fCut
         && mecha_arena_terrain_height(pArena, fX1, fZ1) > fCut
         && mecha_arena_terrain_height(pArena, fX1, fZ0) > fCut;
}

//-------------------------------------------------------------------------------------------------

/*
 * One tile of ground, each corner at the height the terrain gives it. The
 * four corners need not be coplanar, so a slope reads as facets rather than
 * a smooth surface, which is the look rather than a compromise.
 */
/*
 * The most tiles across a merged patch of ground may be, and the record of
 * which tiles a patch has already covered.
 *
 * Two, and not more, for two reasons that both come off the same fact: a
 * merged patch is one quad, and one quad wears exactly one tile of artwork.
 * Merge eight and the rock is stretched seventy metres and the checker
 * under it -- which is what tells a player the scale of the ground they are
 * crossing -- is gone with it. At two the tile lands at seventeen metres,
 * which is finer than the twenty-two this floor was drawn at before it went
 * to one tile per cell, and the checker is still a checker. [ARENA-30]
 */
#define MECHA_GROUND_MERGE_MAX 2
static uint8_t s_abyGroundDone[MECHA_FLOOR_TILES_MAX][MECHA_FLOOR_TILES_MAX];

/*
 * How many tiles square a patch of ground starting here can be drawn as one
 * quad: as many as are level with the first, solid, and with solid ground
 * on the far side of them, up to the limit.
 *
 * Level is the whole of it. A quad has four corners and nothing in between,
 * so merging two tiles that are not at the same height throws away the step
 * between them -- which on a causeway that climbs is the causeway. Ground
 * that is all one height loses nothing at all, and on this stage that is
 * both crags, which is most of it.
 *
 * Solid on the far side too, so a merged patch never touches an edge: the
 * rim is drawn off a patch's own boundary, and a patch that stopped short
 * of the drop would hang its rim out over the middle of the ground.
 * [ARENA-30]
 */
static int mecha_ground_merge(const tMechaArena *pArena, float fX0,
                              float fZ0, float fTile, int iRoom)
{
  float fFirst = mecha_arena_terrain_height(pArena, fX0, fZ0);
  int iBest = 1;
  int iSpan;

  /*
   * Only a stage with an edge. Merging is what pays for a floor drawn at
   * one tile per terrain cell, and a floor is drawn that finely only where
   * the drawn edge has to be the solid edge [ARENA-29]. Everywhere else the
   * tiles are already as coarse as they should be and merging would only
   * cost the checker.
   */
  if (pArena->fDeckDrop <= 0.0f)
    return 1;
  if (iRoom > MECHA_GROUND_MERGE_MAX)
    iRoom = MECHA_GROUND_MERGE_MAX;

  for (iSpan = 2; iSpan <= iRoom; iSpan++) {
    float fEdge = fTile * (float)iSpan;
    bool bFlat = true;
    int i;

    /* Every node along the two new edges, and the corner they meet at. */
    for (i = 0; i <= iSpan && bFlat; i++) {
      float fAt = fTile * (float)i;

      bFlat = fabsf(mecha_arena_terrain_height(pArena, fX0 + fAt,
                                               fZ0 + fEdge) - fFirst) < 1.0f
              && fabsf(mecha_arena_terrain_height(pArena, fX0 + fEdge,
                                                  fZ0 + fAt) - fFirst) < 1.0f;
    }
    if (!bFlat)
      break;
    /* And the ground beyond the patch on both of those sides, so the patch
     * cannot be the thing that meets the drop. */
    if (!mecha_ground_is_solid(pArena, fX0, fZ0 + fEdge, fX0 + fEdge,
                               fZ0 + fEdge + fTile)
        || !mecha_ground_is_solid(pArena, fX0 + fEdge, fZ0,
                                  fX0 + fEdge + fTile, fZ0 + fEdge))
      break;
    iBest = iSpan;
  }
  return iBest;
}

//-------------------------------------------------------------------------------------------------

static void mecha_add_ground_quad(tMechaQuadList *pList,
                                  const tMechaArena *pArena,
                                  float fX0, float fZ0, float fX1, float fZ1,
                                  uint8_t byPalette)
{
  float afVert[4][3];

  afVert[0][0] = fX0; afVert[0][2] = fZ0;
  afVert[1][0] = fX0; afVert[1][2] = fZ1;
  afVert[2][0] = fX1; afVert[2][2] = fZ1;
  afVert[3][0] = fX1; afVert[3][2] = fZ0;
  afVert[0][1] = mecha_arena_terrain_height(pArena, fX0, fZ0);
  afVert[1][1] = mecha_arena_terrain_height(pArena, fX0, fZ1);
  afVert[2][1] = mecha_arena_terrain_height(pArena, fX1, fZ1);
  afVert[3][1] = mecha_arena_terrain_height(pArena, fX1, fZ0);

  /*
   * No clamp. This used to pull a straddling tile's outer corners up to the
   * cut, which is what gave the stage a thickness and also what painted
   * solid-looking deck over the hole; the rim is drawn as its own geometry
   * now and only solid tiles get here at all. [ARENA-29]
   */
  mecha_quads_add(pList, afVert, byPalette,
                  MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GROUND);
}

//-------------------------------------------------------------------------------------------------

/* A vertical panel running from (fX0,fZ0) to (fX1,fZ1). The normal comes out
 * a quarter turn anticlockwise from that direction seen from above, so the
 * caller picks which way the panel faces by choosing which end to start at. */
static void mecha_add_panel(tMechaQuadList *pList,
                            float fX0, float fZ0, float fX1, float fZ1,
                            float fY0, float fY1,
                            uint8_t byPalette, uint8_t byFlags)
{
  float afVert[4][3];

  afVert[0][0] = fX0; afVert[0][1] = fY0; afVert[0][2] = fZ0;
  afVert[1][0] = fX1; afVert[1][1] = fY0; afVert[1][2] = fZ1;
  afVert[2][0] = fX1; afVert[2][1] = fY1; afVert[2][2] = fZ1;
  afVert[3][0] = fX0; afVert[3][1] = fY1; afVert[3][2] = fZ0;
  mecha_quads_add(pList, afVert, byPalette, byFlags);
}

//-------------------------------------------------------------------------------------------------

/* A horizontal quad, normal up. */
static void mecha_add_floor_quad(tMechaQuadList *pList,
                                 float fX0, float fZ0, float fX1, float fZ1,
                                 float fY, uint8_t byPalette, uint8_t byFlags)
{
  float afVert[4][3];

  afVert[0][0] = fX0; afVert[0][1] = fY; afVert[0][2] = fZ0;
  afVert[1][0] = fX0; afVert[1][1] = fY; afVert[1][2] = fZ1;
  afVert[2][0] = fX1; afVert[2][1] = fY; afVert[2][2] = fZ1;
  afVert[3][0] = fX1; afVert[3][1] = fY; afVert[3][2] = fZ0;
  mecha_quads_add(pList, afVert, byPalette, byFlags);
}

//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------

/* Defined with the rest of the quad helpers below, needed by the arena
 * builder above them. */
static void mecha_tag_box(tMechaQuadList *pList, int iFirst, int iBank,
                          int iSide, int iTop);
static void mecha_tag_texture(tMechaQuadList *pList, int iBank, int iTile);
static uint32_t mecha_cloud_hash(uint32_t uiValue);

//-------------------------------------------------------------------------------------------------

/*
 * Which bank a box's tiles are numbered in. Cover started out as buildings
 * and every arena but one still wants the building bank; zero is what an
 * arena that never set it leaves behind, and the building bank is what that
 * has always meant. [ARENA-28]
 */
static int mecha_box_bank(const tMechaObstacle *pBox)
{
  return pBox->byBank != 0 ? (int)pBox->byBank : MECHA_TEX_STRUCT;
}

//-------------------------------------------------------------------------------------------------

/*
 * Breaks up a wall built out of one tile. A keep is a hundred metres of
 * panelling a side; laid in a single tile it reads as wallpaper, and the
 * thing that makes a real building out of it is the panel here and there
 * that is a door, a window, or a piece of machinery bolted on.
 *
 * The side faces only -- a roof is not where the detail goes -- and hashed
 * off the box and the panel, so it is the same wall every frame and every
 * match rather than something that crawls. A pick that lands on the tile
 * the wall is already made of is left alone, which is what keeps the body
 * of the wall the body of the wall. [ARENA-28]
 */
static void mecha_scatter_detail(tMechaQuadList *pList, int iFirst,
                                 const tMechaObstacle *pBox, uint32_t uiSalt)
{
  int i;

  if (pBox->byDetailCount == 0)
    return;
  for (i = iFirst; i < pList->iCount; i++) {
    tMechaQuad *pQuad = &pList->paQuads[i];
    uint32_t uiHash;

    if (pQuad->afNormal[1] > 0.5f || pQuad->afNormal[1] < -0.5f)
      continue;
    uiHash = mecha_cloud_hash(uiSalt * 2654435761u + (uint32_t)i * 40503u);
    if ((uiHash & 3u) != 0u)
      continue;                       /* about one panel in four */
    pQuad->byTile = (uint8_t)(pBox->byDetailFirst
                              + (uiHash >> 8) % pBox->byDetailCount);
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * The same thing seen from underneath: a floor quad wound the other way, so
 * its normal points down and it is drawn for a camera below it rather than
 * one above. Tiled like the top face, because a ceiling this wide stretched
 * one tile across the whole of it. [ARENA-23]
 */
static void mecha_add_ceiling(tMechaQuadList *pList, float fX, float fZ,
                              float fHalfX, float fHalfZ, float fY,
                              float fTile, uint8_t byPalette, int iBank,
                              uint8_t byTile)
{
  float fLowX = fX - fHalfX;
  float fLowZ = fZ - fHalfZ;
  int iCols = (int)(2.0f * fHalfX / fTile + 0.5f);
  int iRows = (int)(2.0f * fHalfZ / fTile + 0.5f);
  int iCol;
  int iRow;

  if (iCols < 1)
    iCols = 1;
  if (iRows < 1)
    iRows = 1;
  for (iRow = 0; iRow < iRows; iRow++) {
    for (iCol = 0; iCol < iCols; iCol++) {
      float fX0 = fLowX + 2.0f * fHalfX * (float)iCol / (float)iCols;
      float fX1 = fLowX + 2.0f * fHalfX * (float)(iCol + 1) / (float)iCols;
      float fZ0 = fLowZ + 2.0f * fHalfZ * (float)iRow / (float)iRows;
      float fZ1 = fLowZ + 2.0f * fHalfZ * (float)(iRow + 1) / (float)iRows;
      float afVert[4][3];

      afVert[0][0] = fX0; afVert[0][1] = fY; afVert[0][2] = fZ0;
      afVert[1][0] = fX1; afVert[1][1] = fY; afVert[1][2] = fZ0;
      afVert[2][0] = fX1; afVert[2][1] = fY; afVert[2][2] = fZ1;
      afVert[3][0] = fX0; afVert[3][1] = fY; afVert[3][2] = fZ1;
      mecha_quads_add(pList, afVert, byPalette, 0);
      mecha_tag_texture(pList, iBank, byTile);
    }
  }
}


/*
 * A wall, in panels rather than one slab: POLYTEX fits exactly one tile to
 * whatever polygon it is given, so a single-quad wall wears one tile
 * stretched two hundred metres wide. [MESH-02]
 *
 * Panels are emitted from the start end, so the winding, and with it which
 * way the wall faces, stays the caller's to pick.
 */
static void mecha_add_wall(tMechaQuadList *pList,
                           float fX0, float fZ0, float fX1, float fZ1,
                           float fLowY, float fHighY, float fTile,
                           int iBank, uint8_t byPalette, uint8_t byTile)
{
  float fHeight = fHighY - fLowY;
  float fRunX = fX1 - fX0;
  float fRunZ = fZ1 - fZ0;
  float fLength = mecha_length2(fRunX, fRunZ);
  int iAcross;
  int iUp;
  int iCol;
  int iRow;

  if (fTile < 1.0f || fLength < 1.0f || fHeight < 1.0f)
    return;
  iAcross = (int)(fLength / fTile + 0.5f);
  iUp = (int)(fHeight / fTile + 0.5f);
  if (iAcross < 1)
    iAcross = 1;
  if (iUp < 1)
    iUp = 1;

  for (iRow = 0; iRow < iUp; iRow++) {
    float fBandLow = fLowY + fHeight * (float)iRow / (float)iUp;
    float fBandHigh = fLowY + fHeight * (float)(iRow + 1) / (float)iUp;

    for (iCol = 0; iCol < iAcross; iCol++) {
      float fNear = (float)iCol / (float)iAcross;
      float fFar = (float)(iCol + 1) / (float)iAcross;

      mecha_add_panel(pList, fX0 + fRunX * fNear, fZ0 + fRunZ * fNear,
                      fX0 + fRunX * fFar, fZ0 + fRunZ * fFar,
                      fBandLow, fBandHigh, byPalette, 0);
      mecha_tag_texture(pList, iBank, byTile);
    }
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * A block of cover, panelled for the same reason walls are. Its sides face
 * outwards, the opposite winding to an arena wall, so each side is walked
 * from the end that puts it on the outside. No underside: it sits on the
 * floor.
 */
static void mecha_add_tiled_box(tMechaQuadList *pList, float fX, float fZ,
                                float fHalfX, float fHalfZ, float fBaseY,
                                float fTopY, float fTile, int iBank,
                                uint8_t byPalette, uint8_t byTopPalette,
                                uint8_t byTile, uint8_t byTopTile)
{
  float fLowX = fX - fHalfX;
  float fHighX = fX + fHalfX;
  float fLowZ = fZ - fHalfZ;
  float fHighZ = fZ + fHalfZ;
  int iCols;
  int iRows;
  int iCol;
  int iRow;

  mecha_add_wall(pList, fHighX, fHighZ, fHighX, fLowZ, fBaseY, fTopY, fTile,
                 iBank, byPalette, byTile);
  mecha_add_wall(pList, fLowX, fLowZ, fLowX, fHighZ, fBaseY, fTopY, fTile,
                 iBank, byPalette, byTile);
  mecha_add_wall(pList, fLowX, fHighZ, fHighX, fHighZ, fBaseY, fTopY, fTile,
                 iBank, byPalette, byTile);
  mecha_add_wall(pList, fHighX, fLowZ, fLowX, fLowZ, fBaseY, fTopY, fTile,
                 iBank, byPalette, byTile);

  /* And the roof, tiled the same way, because a wide one stretched its
   * tile exactly as the walls did. */
  iCols = (int)(2.0f * fHalfX / fTile + 0.5f);
  iRows = (int)(2.0f * fHalfZ / fTile + 0.5f);
  if (iCols < 1)
    iCols = 1;
  if (iRows < 1)
    iRows = 1;
  for (iRow = 0; iRow < iRows; iRow++) {
    for (iCol = 0; iCol < iCols; iCol++) {
      mecha_add_floor_quad(pList,
                           fLowX + 2.0f * fHalfX * (float)iCol
                                   / (float)iCols,
                           fLowZ + 2.0f * fHalfZ * (float)iRow
                                   / (float)iRows,
                           fLowX + 2.0f * fHalfX * (float)(iCol + 1)
                                   / (float)iCols,
                           fLowZ + 2.0f * fHalfZ * (float)(iRow + 1)
                                   / (float)iRows,
                           fTopY, byTopPalette, MECHA_QUAD_GROUND);
      mecha_tag_texture(pList, iBank, byTopTile);
    }
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * A corner of the arena's own boundary, wrapping. Eight of them for an
 * octagon and four otherwise -- the same points the walls are built on and
 * the same ones the boundary test uses, which is what lets the ground
 * outside start exactly where the ground inside stops.
 */
static void mecha_boundary_corner(const tMechaArena *pArena, int iCorner,
                                  float *pafOut)
{
  float fExtent = pArena->fHalfExtent;

  if (pArena->byShape == MECHA_ARENA_OCTAGON) {
    float fCut = 2.0f * fExtent / (2.0f + MECHA_OCTAGON_ROOT2);
    float fIn = fExtent - fCut;
    const float aafCorner[8][2] = {
      {  fExtent, -fIn      }, {  fExtent,  fIn      },
      {  fIn,      fExtent  }, { -fIn,      fExtent  },
      { -fExtent,  fIn      }, { -fExtent, -fIn      },
      { -fIn,     -fExtent  }, {  fIn,     -fExtent  },
    };

    pafOut[0] = aafCorner[iCorner & 7][0];
    pafOut[1] = aafCorner[iCorner & 7][1];
    return;
  }
  {
    const float aafCorner[4][2] = {
      {  fExtent, -fExtent }, {  fExtent,  fExtent },
      { -fExtent,  fExtent }, { -fExtent, -fExtent },
    };

    pafOut[0] = aafCorner[iCorner & 3][0];
    pafOut[1] = aafCorner[iCorner & 3][1];
  }
}

//-------------------------------------------------------------------------------------------------

void mecha_mesh_arena(tMechaQuadList *pList, const tMechaArena *pArena)
{
  mecha_quads_part(pList, MECHA_PART_NONE);
  float fExtent;
  float fTile;
  int iTiles;
  int iRow;
  int i;

  if (!pList || !pArena)
    return;

  fExtent = pArena->fHalfExtent;
  iTiles = pArena->iFloorTiles > 0 ? pArena->iFloorTiles : MECHA_FLOOR_TILES;
  if (iTiles > MECHA_FLOOR_TILES_MAX)
    iTiles = MECHA_FLOOR_TILES_MAX;
  fTile = fExtent * 2.0f / (float)iTiles;

  /*
   * Which tiles a patch drawn on an earlier row has already covered. A
   * merged patch is square, so it eats rows as well as columns, and without
   * this the rows under it draw the same ground again -- quads in the same
   * plane, in the same place, which is the one thing this renderer cannot
   * sort. [ARENA-30]
   */
  memset(s_abyGroundDone, 0, sizeof(s_abyGroundDone));

  for (iRow = 0; iRow < iTiles; iRow++) {
    int iCol;

    for (iCol = 0; iCol < iTiles; iCol++) {
      float fX0 = -fExtent + fTile * (float)iCol;
      float fZ0 = -fExtent + fTile * (float)iRow;
      /*
       * A patch of flat ground with flat ground all round it is drawn as
       * one quad however many tiles across it is. The tiles have to be the
       * size of a terrain cell for the stage's edge to be honest
       * [ARENA-29], and at that size the two crags alone are three and a
       * half thousand quads of ground that is all exactly the same height.
       * Merged they are two hundred, and the causeways -- which climb, so
       * no two of their cells are level -- keep every cell they had.
       * [ARENA-30]
       */
      int iMerge;
      float fSpan;

      if (s_abyGroundDone[iRow][iCol])
        continue;
      iMerge = mecha_ground_merge(pArena, fX0, fZ0, fTile,
                                  iTiles - iCol < iTiles - iRow
                                    ? iTiles - iCol : iTiles - iRow);
      fSpan = fTile * (float)iMerge;
      {
        int iMarkRow;
        int iMarkCol;

        for (iMarkRow = iRow; iMarkRow < iRow + iMerge; iMarkRow++)
          for (iMarkCol = iCol; iMarkCol < iCol + iMerge; iMarkCol++)
            s_abyGroundDone[iMarkRow][iMarkCol] = 1;
      }

      bool bAlternate = ((iRow + iCol) & 1) != 0;

      float fMidX = fX0 + fSpan * 0.5f;
      float fMidZ = fZ0 + fSpan * 0.5f;
      uint32_t uiSurface = mecha_arena_surface(pArena, fMidX, fMidZ);

      /*
       * A pit is a surface that is not drawn -- the flag pair the race game
       * uses for exactly this -- and an arena that is not a square has
       * ground outside itself that nobody should see either.
       */
      if ((uiSurface & MECHA_SURF_SKIP_RENDER) != 0)
        continue;
      if (!mecha_arena_contains(pArena, fMidX, fMidZ))
        continue;
      if (!mecha_ground_is_solid(pArena, fX0, fZ0, fX0 + fSpan, fZ0 + fSpan))
        continue;

      /*
       * One tile, chosen and then drawn. The road case used to draw and
       * then `continue`, which made every line below it -- the whole of
       * the texture choice, including the longer cycle an arena can ask
       * for [ARENA-28] -- code that never ran. [ARENA-29]
       */
      {
        bool bRoad = (uiSurface & MECHA_SURF_ROAD) != 0;
        uint8_t byPalette;
        uint8_t byTile;

        if (bRoad) {
          /* A street is drawn in tarmac wherever the arena has marked one,
           * and keeps the checker so it still reads as ground to move
           * over. */
          byPalette = bAlternate ? MECHA_SHADE_ROAD_A : MECHA_SHADE_ROAD_B;
          byTile = bAlternate ? MECHA_TILE_TARMAC_A : MECHA_TILE_TARMAC_B;
        } else if (pArena->byGroundTileCount > 0) {
          /*
           * A rotation longer than two. The checkerboard is what a floor
           * wants -- a grid to move about on rather than one flat expanse
           * -- and the opposite of what ground meant to read as rock
           * wants, which is a cycle long enough that the eye does not find
           * the repeat. The palette pair still alternates under it, so a
           * checkout with no artwork keeps the checker. [ARENA-28]
           */
          int iCycle = pArena->byGroundTileCount;

          if (iCycle > (int)(sizeof(pArena->abyGroundTile)
                             / sizeof(pArena->abyGroundTile[0])))
            iCycle = (int)(sizeof(pArena->abyGroundTile)
                           / sizeof(pArena->abyGroundTile[0]));
          byPalette = bAlternate ? pArena->byFloorPalette
                                 : pArena->byGridPalette;
          byTile = pArena->abyGroundTile[(iRow + iCol) % iCycle];
        } else {
          byPalette = bAlternate ? pArena->byFloorPalette
                                 : pArena->byGridPalette;
          byTile = bAlternate ? pArena->byFloorTile : pArena->byGridTile;
        }
        mecha_add_ground_quad(pList, pArena, fX0, fZ0, fX0 + fSpan,
                              fZ0 + fSpan, byPalette);
        mecha_tag_texture(pList, MECHA_TEX_WORLD, byTile);
      }

      /*
       * And the rim, wherever the ground stops. A tile with nothing solid
       * beside it is an edge, and an edge with no thickness is a sheet of
       * paper seen from the side -- which is what a stage floating in a
       * void looks like without this. The top of it follows the ground's
       * own height, so the rim under a causeway that climbs climbs with
       * it. [ARENA-29]
       */
      if (pArena->fDeckDrop > 0.0f) {
        static const int aiStep[4][2] = { { 1, 0 }, { -1, 0 },
                                          { 0, 1 }, { 0, -1 } };
        int iSide;

        for (iSide = 0; iSide < 4; iSide++) {
          float fAtX = fX0 + fSpan * (float)aiStep[iSide][0];
          float fAtZ = fZ0 + fSpan * (float)aiStep[iSide][1];
          float afEdge[2][2];
          float afVert[4][3];
          int iCorner;

          if (mecha_ground_is_solid(pArena, fAtX, fAtZ, fAtX + fSpan,
                                    fAtZ + fSpan))
            continue;

          /*
           * The two corners of this tile that face the missing one, in the
           * order that leaves the rim looking outwards.
           */
          if (aiStep[iSide][0] > 0) {
            afEdge[0][0] = fX0 + fSpan; afEdge[0][1] = fZ0;
            afEdge[1][0] = fX0 + fSpan; afEdge[1][1] = fZ0 + fSpan;
          } else if (aiStep[iSide][0] < 0) {
            afEdge[0][0] = fX0; afEdge[0][1] = fZ0 + fSpan;
            afEdge[1][0] = fX0; afEdge[1][1] = fZ0;
          } else if (aiStep[iSide][1] > 0) {
            afEdge[0][0] = fX0 + fSpan; afEdge[0][1] = fZ0 + fSpan;
            afEdge[1][0] = fX0;         afEdge[1][1] = fZ0 + fSpan;
          } else {
            afEdge[0][0] = fX0;         afEdge[0][1] = fZ0;
            afEdge[1][0] = fX0 + fSpan; afEdge[1][1] = fZ0;
          }

          for (iCorner = 0; iCorner < 2; iCorner++) {
            float fTop = mecha_arena_terrain_height(pArena,
                                                    afEdge[iCorner][0],
                                                    afEdge[iCorner][1]);

            afVert[iCorner][0] = afEdge[iCorner][0];
            afVert[iCorner][1] = fTop;
            afVert[iCorner][2] = afEdge[iCorner][1];
            afVert[3 - iCorner][0] = afEdge[iCorner][0];
            afVert[3 - iCorner][1] = fTop - pArena->fDeckDrop;
            afVert[3 - iCorner][2] = afEdge[iCorner][1];
          }
          mecha_quads_add(pList, afVert, pArena->byGridPalette,
                          MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GROUND);
          mecha_tag_texture(pList, MECHA_TEX_WORLD, pArena->byGridTile);
        }
      }
    }
  }

  /* The four walls, each facing inward. A camera shoved outside the arena
   * sees straight through them rather than at a wall of solid colour,
   * because they are one-sided and get culled from behind. */
  if (pArena->byShape == MECHA_ARENA_OCTAGON) {
    /*
     * Eight sides, walked anticlockwise seen from above so every panel's
     * normal comes out facing the middle. The corner points are the square
     * with its corners cut, which is what the boundary test is too.
     */
    int iSide;

    for (iSide = 0; iSide < 8; iSide++) {
      float afFrom[2];
      float afTo[2];

      mecha_boundary_corner(pArena, iSide, afFrom);
      mecha_boundary_corner(pArena, iSide + 1, afTo);
      mecha_add_wall(pList, afFrom[0], afFrom[1], afTo[0], afTo[1],
                     0.0f, pArena->fWallHeight, fTile, MECHA_TEX_WORLD,
                     pArena->byWallPalette, pArena->byWallTile);
    }
  } else if (pArena->byShape != MECHA_ARENA_OPEN) {
    mecha_add_wall(pList,  fExtent, -fExtent,  fExtent,  fExtent,
                   0.0f, pArena->fWallHeight, fTile, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList, -fExtent,  fExtent, -fExtent, -fExtent,
                   0.0f, pArena->fWallHeight, fTile, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList,  fExtent,  fExtent, -fExtent,  fExtent,
                   0.0f, pArena->fWallHeight, fTile, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList, -fExtent, -fExtent,  fExtent, -fExtent,
                   0.0f, pArena->fWallHeight, fTile, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
  }
  /*
   * An open arena has a lip instead: the platform's own edge, seen from
   * outside as you fall past it. Without it the roof is a paper cutout.
   *
   * Only where the ground reaches the boundary, though. The lip is drawn on
   * the boundary square, so on a stage whose ground is a ribbon inside that
   * square it is the edge of nothing: a rectangle of wall hanging in space
   * hundreds of metres from the nearest thing anyone can stand on, which
   * from outside reads as a line ruled under the stage. Such a stage says
   * so by asking for no skirt, and cuts its own edge with fDeckDrop
   * instead. [ARENA-27]
   */
  if (pArena->byShape == MECHA_ARENA_OPEN && pArena->fSkirt > 0.0f) {
    float fLip = pArena->fSkirt;
    /*
     * Its own panel size, and a coarse one. The skirt is the side of a
     * tower rather than a wall of the arena: it runs far enough down that
     * panelling it at the floor's scale would cost more quads than
     * everything standing on the roof put together, and nothing that far
     * below the player is being looked at closely.
     */
    float fPanel = MECHA_M(20.0f);

    mecha_add_wall(pList,  fExtent,  fExtent,  fExtent, -fExtent,
                   -fLip, 0.0f, fPanel, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList, -fExtent, -fExtent, -fExtent,  fExtent,
                   -fLip, 0.0f, fPanel, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList, -fExtent,  fExtent,  fExtent,  fExtent,
                   -fLip, 0.0f, fPanel, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList,  fExtent, -fExtent, -fExtent, -fExtent,
                   -fLip, 0.0f, fPanel, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
  }

  /*
   * Ground past the boundary: unwalkable, drawn coarsely, and there so the
   * arena stops being an island. Built as rings of the boundary's own shape
   * rather than a grid with the middle knocked out. [MESH-03]
   */
  if (pArena->fOuterReach > fExtent) {
    float fGrow = pArena->fOuterReach / fExtent;
    int iRing;

    for (iRing = 0; iRing < MECHA_OUTER_RINGS; iRing++) {
      /* Each ring a fixed multiple of the last, so they get wider as they
       * get further away and the near ones stay the size of the arena. */
      float fInner = powf(fGrow, (float)iRing / (float)MECHA_OUTER_RINGS);
      float fOuter = powf(fGrow, (float)(iRing + 1)
                                 / (float)MECHA_OUTER_RINGS);
      int iEdge;
      int iEdges = pArena->byShape == MECHA_ARENA_OCTAGON ? 8 : 4;

      for (iEdge = 0; iEdge < iEdges; iEdge++) {
        float afFrom[2];
        float afTo[2];
        int iSpan;

        mecha_boundary_corner(pArena, iEdge, afFrom);
        mecha_boundary_corner(pArena, iEdge + 1, afTo);
        for (iSpan = 0; iSpan < MECHA_OUTER_SPANS; iSpan++) {
          float fA = (float)iSpan / (float)MECHA_OUTER_SPANS;
          float fB = (float)(iSpan + 1) / (float)MECHA_OUTER_SPANS;
          float fAx = afFrom[0] + (afTo[0] - afFrom[0]) * fA;
          float fAz = afFrom[1] + (afTo[1] - afFrom[1]) * fA;
          float fBx = afFrom[0] + (afTo[0] - afFrom[0]) * fB;
          float fBz = afFrom[1] + (afTo[1] - afFrom[1]) * fB;
          float afVert[4][3];
          int iCorner;

          afVert[0][0] = fAx * fInner; afVert[0][2] = fAz * fInner;
          afVert[1][0] = fBx * fInner; afVert[1][2] = fBz * fInner;
          afVert[2][0] = fBx * fOuter; afVert[2][2] = fBz * fOuter;
          afVert[3][0] = fAx * fOuter; afVert[3][2] = fAz * fOuter;
          for (iCorner = 0; iCorner < 4; iCorner++) {
            afVert[iCorner][1] =
                mecha_arena_terrain_height(pArena, afVert[iCorner][0],
                                           afVert[iCorner][2]);
          }
          mecha_quads_add(pList, afVert,
                          ((iRing + iSpan + iEdge) & 1)
                            ? pArena->byFloorPalette : pArena->byGridPalette,
                          MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GROUND);
          mecha_tag_texture(pList, MECHA_TEX_WORLD,
                            ((iRing + iSpan + iEdge) & 1)
                              ? pArena->byFloorTile : pArena->byGridTile);
        }
      }
    }
  }

  /*
   * What the things standing on the ground are panelled at, which is not
   * what the ground is panelled at. They used to share a number, and the
   * day the floor went to one tile per terrain cell every wall in the
   * arena went with it -- a keep is a hundred and thirty metres tall and
   * that turned four panels into thirty-six of them. The floor's tile size
   * answers to the terrain; a wall's answers to how far away it is looked
   * at from. [ARENA-29]
   */
  fTile = MECHA_PANEL_SIZE;

  for (i = 0; i < pArena->iObstacleCount; i++) {
    const tMechaObstacle *pBox = &pArena->aObstacles[i];
    /* Where the underside of it is, which is what the collision stands it
     * on too -- the ground for everything that sits on the floor, and a
     * storey up for a deck. [ARENA-23] */
    float fBase = pBox->fBaseY + pBox->fRise;

    switch (pBox->byKind) {
    case MECHA_PROP_TREE: {
      /*
       * A trunk with a canopy over it. The canopy overhangs what a machine
       * can walk into, which is right: you can stand under a tree, and the
       * thing you cannot walk through is the trunk.
       */
      float fTrunk = pBox->fHalfX * 0.42f;
      float fCanopy = pBox->fHeight * 0.44f;

      mecha_add_tiled_box(pList, pBox->fX, pBox->fZ, fTrunk, fTrunk, fBase,
                          fBase + pBox->fHeight - fCanopy, fTile,
                          MECHA_TEX_WORLD, pBox->byPalette, pBox->byPalette,
                          MECHA_TILE_RUST, MECHA_TILE_RUST);
      mecha_add_tiled_box(pList, pBox->fX, pBox->fZ, pBox->fHalfX,
                          pBox->fHalfZ, fBase + pBox->fHeight - fCanopy,
                          fBase + pBox->fHeight, fTile, MECHA_TEX_WORLD,
                          pBox->byTrimPalette, pBox->byTrimPalette,
                          pBox->byTile, pBox->byTopTile);
      /* A second, smaller crown, so a tree is not a lollipop. */
      mecha_add_tiled_box(pList, pBox->fX, pBox->fZ, pBox->fHalfX * 0.62f,
                          pBox->fHalfZ * 0.62f, fBase + pBox->fHeight,
                          fBase + pBox->fHeight + fCanopy * 0.55f, fTile,
                          MECHA_TEX_WORLD, pBox->byTrimPalette,
                          pBox->byTrimPalette, pBox->byTile,
                          pBox->byTopTile);
      break;
    }

    case MECHA_PROP_ROCK:
      /* Squat, and stepped, so it reads as stone rather than as a crate. */
      mecha_add_tiled_box(pList, pBox->fX, pBox->fZ, pBox->fHalfX,
                          pBox->fHalfZ, fBase - MECHA_M(1.0f),
                          fBase + pBox->fHeight * 0.62f, fTile,
                          MECHA_TEX_WORLD, pBox->byPalette,
                          pBox->byTrimPalette, pBox->byTile,
                          pBox->byTopTile);
      mecha_add_tiled_box(pList, pBox->fX + pBox->fHalfX * 0.18f,
                          pBox->fZ - pBox->fHalfZ * 0.14f,
                          pBox->fHalfX * 0.66f, pBox->fHalfZ * 0.7f,
                          fBase + pBox->fHeight * 0.62f,
                          fBase + pBox->fHeight, fTile, MECHA_TEX_WORLD,
                          pBox->byPalette, pBox->byTrimPalette,
                          pBox->byTile, pBox->byTopTile);
      break;

    case MECHA_PROP_SPIRE: {
      /*
       * A roof that comes to a point. Built as a stack of frusta rather
       * than one, so the sides are broken into pieces a painter's sort can
       * order against the walls under them -- one quad running the whole
       * height of a spire this tall is a single depth for a surface that
       * spans two hundred metres of it. The silhouette is still a straight
       * taper: every tier picks up where the last left off. [MESH-50]
       */
      tMechaPose pose;
      int iTier;

      mecha_pose_build(&pose, 0, 0, 0, 0.0f, 0.0f, 0.0f, 1.0f);
      for (iTier = 0; iTier < MECHA_SPIRE_TIERS; iTier++) {
        float fLow = (float)iTier / (float)MECHA_SPIRE_TIERS;
        float fHigh = (float)(iTier + 1) / (float)MECHA_SPIRE_TIERS;
        float fY0 = fBase + pBox->fHeight * fLow;
        float fY1 = fBase + pBox->fHeight * fHigh;
        /* Stopping a whisker short of a point. A face whose two top
         * corners are the same point is a triangle, and which corners a
         * box's faces are built from decides whether the cross product
         * that gives it its normal comes out at all -- two of the four
         * came out as nothing and took the degenerate normal, which is
         * straight up, which is a pair of quads in one plane facing the
         * same way. The finial is two metres across on a roof a hundred
         * and eighty wide. [MESH-50] */
        float fNear = 1.0f - fLow * (1.0f - MECHA_SPIRE_TIP);
        float fFar = 1.0f - fHigh * (1.0f - MECHA_SPIRE_TIP);
        int iFirst = pList->iCount;

        mecha_add_frustum(pList, &pose, pBox->fX, (fY0 + fY1) * 0.5f,
                          pBox->fZ,
                          pBox->fHalfX * fNear,
                          pBox->fHalfZ * fNear,
                          pBox->fHalfX * fFar,
                          pBox->fHalfZ * fFar,
                          (fY1 - fY0) * 0.5f, 0.0f, 0.0f,
                          /* Courses of tile, alternating, so a roof this
                           * big is not one flat face of colour. */
                          (iTier & 1) ? pBox->byTrimPalette
                                      : pBox->byPalette,
                          pBox->byTrimPalette, 0);
        /* And the artwork over the lot of it where there is any: a spire
         * is a roof, so every face of it takes the roof tile rather than
         * the side-and-top pair a box wants. [ARENA-28] */
        mecha_tag_box(pList, iFirst, mecha_box_bank(pBox), pBox->byTile,
                      pBox->byTile);
      }
      break;
    }

    default: {
      int iFirst = pList->iCount;

      mecha_add_tiled_box(pList, pBox->fX, pBox->fZ, pBox->fHalfX,
                          pBox->fHalfZ, fBase, fBase + pBox->fHeight, fTile,
                          mecha_box_bank(pBox), pBox->byPalette,
                          pBox->byTrimPalette, pBox->byTile,
                          pBox->byTopTile);
      mecha_scatter_detail(pList, iFirst, pBox, (uint32_t)i);
      /*
       * A box that stands on the floor needs no underside and is not given
       * one. A deck does: it is the ceiling of the room below, and without
       * it that room is open to whatever is over the deck. [ARENA-23]
       */
      if (pBox->fRise > 0.0f) {
        mecha_add_ceiling(pList, pBox->fX, pBox->fZ, pBox->fHalfX,
                          pBox->fHalfZ, fBase, fTile, pBox->byPalette,
                          mecha_box_bank(pBox), pBox->byTile);
      }
      break;
    }
    }
  }
}

//-------------------------------------------------------------------------------------------------
/* Mechs */

/* How far the body is tipped over. Falling and getting up are both driven
 * off the simulation's own timers, so the animation can never disagree with
 * when the mech is actually helpless. */
/* One angle towards another, for poses that are held rather than cycled. */
static int mecha_blend_angle(int iFrom, int iTo, float fAmount)
{
  return iFrom + (int)((float)(iTo - iFrom) * mecha_clampf(fAmount, 0.0f,
                                                           1.0f));
}

//-------------------------------------------------------------------------------------------------

/*
 * The walk cycle. fStepPhase counts distance, not time, so pace follows
 * ground covered. Angles are positive forward and the caller negates them:
 * a positive pose pitch swings a limb backwards. [MESH-04]
 */
#define MECHA_LEG_SWING   MECHA_DEG(41)
#define MECHA_LEG_KNEE    MECHA_DEG(56)
/* How far the toe drops below flat once the leg has the foot off the
 * ground, and how much clearance under the sole counts as fully off it, as
 * a fraction of the leg. A stride lifts a foot about a fifth of its own
 * leg. [MESH-53] */
#define MECHA_LEG_ANKLE   MECHA_DEG(19)
#define MECHA_ANKLE_CLEAR 0.14f
/*
 * Standing, and standing with someone to fight. A machine at ease has its
 * feet apart and knees off the lock; given a target it settles lower, wider,
 * one foot forward. The lead angle breathes, which is the only movement in
 * either pose.
 */
#define MECHA_STAND_LEAD   MECHA_DEG(5)
#define MECHA_STAND_KNEE   MECHA_DEG(9)
#define MECHA_STAND_SPLAY  MECHA_DEG(5)
#define MECHA_FIGHT_LEAD   MECHA_DEG(17)
#define MECHA_FIGHT_KNEE   MECHA_DEG(25)
#define MECHA_FIGHT_SPLAY  MECHA_DEG(18)
#define MECHA_STANCE_BREATH MECHA_DEG(3)
/* Boosting on the ground is a skater's problem, not a runner's: knees bent
 * throughout, weight low, one leg pushing while the other glides. [MESH-05] */
#define MECHA_SKATE_KNEE    MECHA_DEG(34)
#define MECHA_SKATE_PUSH    MECHA_DEG(20)
#define MECHA_SKATE_GATHER  MECHA_DEG(11)
#define MECHA_SKATE_EDGE    MECHA_DEG(9)
#define MECHA_SKATE_TICKS   40
/*
 * Hanging. Not a tuck -- a tuck is what you do to clear something -- but a
 * machine with its weight off its feet: one leg reaching a little, the
 * other trailing, both knees soft, and a slow sway so it is not a statue.
 */
#define MECHA_LEG_HANG      MECHA_DEG(14)
#define MECHA_LEG_HANGKNEE  MECHA_DEG(30)
#define MECHA_LEG_TRAIL     MECHA_DEG(22)
#define MECHA_LEG_TRAILKNEE MECHA_DEG(46)
/* A guard is a squat, and a human squat has to be deep to lower anything:
 * the knee travels forward as far as the hip drops, so the two cosines all
 * but cancel until the angles get large. Bird-legged, half of this was
 * enough; on a knee that bends the right way it is not -- and it went
 * deeper again once standing stopped being a machine on locked knees, since
 * a crouch is only a crouch relative to whatever the machine does the rest
 * of the time. */
#define MECHA_LEG_SQUAT   MECHA_DEG(54)
#define MECHA_LEG_SQKNEE  MECHA_DEG(108)

/*
 * The slender frame walks differently, and the difference is not that it
 * takes smaller steps. What separates one walk from another is the track --
 * how far apart the feet are across the line of travel -- and the knee. This
 * one puts its feet down near enough on the centreline, rolling the standing
 * hip inwards so the body passes over the planted foot rather than beside
 * it, and picks its knees up further on the way through. The rest of it is
 * the sway that falls out of doing those two things, which is drawn on the
 * torso rather than here. [MESH-40]
 */
/* How far the fist breaks from the forearm. Small -- a wrist, not an elbow.
 * Its whole job is that the arm does not read as one straight piece from
 * shoulder to muzzle. [MESH-54] */
#define MECHA_WRIST_BREAK  MECHA_DEG(9)
/*
 * Two pieces the reference mesh has tilted, which a frustum cannot be: its
 * top and bottom faces are flat in its own frame by construction, so a
 * sloped toe or a raked fin is not a number you can pick, it is a frame you
 * have to give it. Both are sub-poses of the part they sit on, which also
 * means they inherit its bone and cost no extra skinning. [MESH-55]
 */
#define MECHA_CREST_SWEEP MECHA_DEG(29.85f)
#define MECHA_CHEST_PITCH MECHA_DEG(8.99f)
#define MECHA_SKULL_NOD   MECHA_DEG(5.716f)
#define MECHA_SLIM_SWING   MECHA_DEG(45)
#define MECHA_SLIM_KNEE    MECHA_DEG(74)
/* A deeper knee picks the foot up higher, so the toe drops further.
 * [MESH-53] */
#define MECHA_SLIM_ANKLE   MECHA_DEG(24)
/* How far the standing hip rolls in under the body. */
#define MECHA_SLIM_CROSS   MECHA_DEG(14)
/*
 * Feet together at ease and apart in a fight, like any other frame -- just
 * less so. Held at the narrow figure throughout, this one stood in a fight
 * with its ankles touching, which is a pose for a photograph and not for
 * being shot at.
 */
#define MECHA_SLIM_SPLAY   MECHA_DEG(2)
#define MECHA_SLIM_FIGHT_SPLAY MECHA_DEG(13)
/*
 * And what the hips do about it. The first version of this slid the whole
 * upper body from side to side and rolled it with them, which is not a walk
 * -- it is a machine wobbling. What actually moves is the pelvis: it tilts,
 * dropping on the side whose leg is swinging through, and it turns a little
 * with that leg. The shoulders do neither. They stay level and turn the
 * other way, and that opposition is what reads as walking. [MESH-46]
 */
#define MECHA_SLIM_HIP_ROLL 1.9f

/*
 * What the upper body does about the walk, on every machine that has legs.
 * Before this the torso and the arms were carried as though the machine were
 * standing still while its legs moved underneath it, which does not read as
 * stiff so much as it reads as a doll being slid along the floor. [MESH-43]
 *
 * Three things, and the arms are the one that matters: an arm swings against
 * the leg on its own side, which is what walking is. The shoulders twisting
 * against the hips and the small rock fore and aft are what stop the swing
 * looking like two arms bolted to a post.
 */
#define MECHA_WALK_ARM_SWING   MECHA_DEG(20)
#define MECHA_WALK_ARM_ELBOW   MECHA_DEG(11)
#define MECHA_WALK_TWIST       MECHA_DEG(9)
#define MECHA_WALK_ROCK        MECHA_DEG(3)
/* How far the pelvis tilts, and how much of the shoulders' turn it takes
 * back the other way. */
#define MECHA_WALK_HIP_ROLL    MECHA_DEG(4)
#define MECHA_WALK_HIP_TURN    0.45f
/*
 * How much of all that survives having something to point the guns at. Not
 * none: a machine holding a lock still walks, it just does not swing its arms
 * like one out for a stroll.
 */
#define MECHA_WALK_AIM_KEEP    0.30f

/*
 * What the legs are doing, which is not the same question as what the
 * machine is doing. Walking is a cycle; a stance, a glide, the two
 * airborne shapes and the squat are poses with a little movement in them.
 */
#define MECHA_GAIT_WALK    0
#define MECHA_GAIT_SKATE   1
#define MECHA_GAIT_AIR     2
#define MECHA_GAIT_AIRDASH 3
#define MECHA_GAIT_GUARD   4
#define MECHA_GAIT_STANCE  5
/* Won the round, and standing like it. [MESH-42] */
#define MECHA_GAIT_VICTORY 6

/*
 * The stance a machine holds when it has won. One foot forward and turned
 * across the other, the weight settled back, the knees soft. The cross is a
 * yaw at the hip rather than a roll, and that is not a detail: a planting
 * gait has its hip rolls solved for, so that both feet finish on the floor
 * [MESH-16], and anything written into the roll is overwritten. A yaw
 * swings the leg across without changing how far down the foot reaches, so
 * the solve never notices it.
 */
#define MECHA_WIN_LEAD     MECHA_DEG(15)
#define MECHA_WIN_KNEE     MECHA_DEG(20)
#define MECHA_WIN_CROSS    MECHA_DEG(13)
#define MECHA_WIN_SLIM_LEAD  MECHA_DEG(24)
#define MECHA_WIN_SLIM_KNEE  MECHA_DEG(30)
#define MECHA_WIN_SLIM_CROSS MECHA_DEG(14)

/*
 * A glide runs on its own clock rather than on ground covered: at boost
 * speed, a distance-paced cycle is fourteen strokes a second and reads as a
 * grey blur. [MESH-05]
 */

/* Whether both feet are meant to be on the floor throughout. A stance and a
 * glide are poses the machine holds; a walk is a cycle it steps through, and
 * a leg that never leaves the floor is not a leg that is walking. */
static bool mecha_gait_plants(int iGait)
{
  return iGait == MECHA_GAIT_STANCE || iGait == MECHA_GAIT_SKATE
         || iGait == MECHA_GAIT_VICTORY;
}

/*
 * iSide 0 is the left leg. piRoll comes back positive for a hip rolled
 * outwards, which the caller signs for the side it is building.
 */
/*
 * How far the toe drops below flat: how high the leg has picked the foot
 * up, and nothing else.
 *
 * A foot that stays level while the leg swings under it is a boot on the
 * end of a stick, and it is what every machine on the roster was doing --
 * the ankle was built to cancel the thigh and the knee exactly, so the sole
 * stayed parallel to the floor whatever the leg did. That is right while
 * the foot is on the ground and wrong while it is not.
 *
 * It goes one way. The ankle hangs as the leg lifts it and comes back to
 * flat as the leg puts it down, which is the foot following the leg. A
 * version that swung both ways instead -- down off the push, through flat,
 * up onto the landing -- read as the ankle rolling about underneath the
 * machine rather than as a foot being carried.
 *
 * The lift is what gates it, and it has to be the real one. Two goes at
 * deriving it from the gait's own trig both put a planted toe half a metre
 * through the floor, because the half of the cycle the knee bends on is not
 * the half the foot is up: the knee folds to take the weight as the body
 * passes over the planted leg, and the lift is a much narrower window than
 * either half. Measured rather than reasoned about, and then taken from the
 * one number that already knows -- how far this leg reaches compared to the
 * leg standing on the floor. Zero clearance is a planted foot and it stays
 * flat, exactly. [MESH-53]
 */
int mecha_leg_ankle(float fClear, float fSpan, int iRange)
{
  float fLift;

  if (fSpan < 1e-4f)
    return 0;
  fLift = fClear / fSpan;
  if (fLift <= 0.0f)
    return 0;
  if (fLift > 1.0f)
    fLift = 1.0f;
  return (int)((float)iRange * fLift);
}

//-------------------------------------------------------------------------------------------------

static void mecha_leg_angles(int iGait, float fPhase, int iSide, int iTick,
                             float fCombat, int iProfile, int *piThigh,
                             int *piKnee, int *piRoll)
{
  bool bSlim = iProfile == MECHA_PROFILE_SLENDER;
  int iAngle = (int)(fPhase * (float)MECHA_ANGLE_FULL) & (MECHA_ANGLE_FULL - 1);
  float fCos = mecha_cos(iAngle);
  /* A slow breath for the poses that are not cycles, the two legs half a
   * turn apart so they do not move as one. */
  int iSway = (int)((float)MECHA_DEG(5)
                    * mecha_sin(mecha_angle_wrap(iTick * 90
                                                 + iSide * MECHA_ANGLE_HALF)));

  *piRoll = 0;
  switch (iGait) {
  case MECHA_GAIT_STANCE: {
    /*
     * The lead angle breathes rather than the knees, so the machine rocks
     * its weight between its feet instead of bobbing on the spot, and the
     * left foot is the one that leads.
     */
    float fLead = iSide == 0 ? 1.0f : -1.0f;
    int iBreath = (int)((float)MECHA_STANCE_BREATH
                        * mecha_sin(mecha_angle_wrap(iTick * 70)));

    *piThigh = (int)(fLead * (float)(mecha_blend_angle(MECHA_STAND_LEAD,
                                                       MECHA_FIGHT_LEAD,
                                                       fCombat)
                                     + iBreath));
    *piKnee = mecha_blend_angle(MECHA_STAND_KNEE, MECHA_FIGHT_KNEE, fCombat);
    *piRoll = bSlim
                ? mecha_blend_angle(MECHA_SLIM_SPLAY,
                                    MECHA_SLIM_FIGHT_SPLAY, fCombat)
                : mecha_blend_angle(MECHA_STAND_SPLAY, MECHA_FIGHT_SPLAY,
                                    fCombat);
    return;
  }

  case MECHA_GAIT_SKATE: {
    /*
     * One stroke a cycle, the two legs opposite each other: the pushing leg
     * swings back and straightens while the gliding leg gathers underneath
     * and folds. The push also rolls the hip out; how far it ends up rolled
     * is not decided here, because the foot has to finish on the floor.
     */
    float fPush = mecha_sin(iAngle);
    float fOut = fPush > 0.0f ? fPush : 0.0f;
    float fIn = fPush < 0.0f ? -fPush : 0.0f;

    *piThigh = (int)((float)MECHA_SKATE_GATHER * fIn
                     - (float)MECHA_SKATE_PUSH * fOut);
    *piKnee = (int)((float)MECHA_SKATE_KNEE * (1.0f - 0.6f * fOut)
                    + (float)MECHA_SKATE_KNEE * 0.45f * fIn);
    *piRoll = MECHA_SKATE_EDGE;
    return;
  }

  case MECHA_GAIT_VICTORY: {
    /* The left foot leads, the right takes the weight. The roll is left
     * alone for the planting solve to fill in. */
    int iLead = bSlim ? MECHA_WIN_SLIM_LEAD : MECHA_WIN_LEAD;

    *piThigh = iSide == 0 ? iLead : -iLead / 2;
    *piKnee = bSlim ? MECHA_WIN_SLIM_KNEE : MECHA_WIN_KNEE;
    return;
  }

  case MECHA_GAIT_GUARD:
    *piThigh = MECHA_LEG_SQUAT;
    *piKnee = MECHA_LEG_SQKNEE;
    return;

  case MECHA_GAIT_AIR:
    if (iSide == 0) {
      *piThigh = MECHA_LEG_HANG + iSway;
      *piKnee = MECHA_LEG_HANGKNEE;
    } else {
      *piThigh = -MECHA_LEG_TRAIL + iSway;
      *piKnee = MECHA_LEG_TRAILKNEE;
    }
    return;

  case MECHA_GAIT_AIRDASH:
    /*
     * Half of each. The lead leg holds a glide's edge, because the machine
     * is being driven somewhere and is braced against it; the other hangs,
     * because there is nothing under either of them to push against.
     */
    if (iSide == 0) {
      *piThigh = -MECHA_SKATE_PUSH + iSway;
      *piKnee = MECHA_SKATE_KNEE / 2;
      *piRoll = MECHA_SKATE_EDGE * 2;
    } else {
      *piThigh = MECHA_LEG_TRAIL + MECHA_DEG(6) + iSway;
      *piKnee = MECHA_LEG_TRAILKNEE + MECHA_DEG(10);
      *piRoll = MECHA_SKATE_EDGE;
    }
    return;

  default:
    if (bSlim) {
      /*
       * The planted half is the half the leg is swinging back through,
       * which is where the hip rolls in: the foot lands under the middle
       * of the machine instead of under its own hip, and the body has to
       * come across to follow it. [MESH-40]
       */
      *piThigh = (int)((float)MECHA_SLIM_SWING * mecha_sin(iAngle));
      *piKnee = fCos > 0.0f ? (int)((float)MECHA_SLIM_KNEE * fCos) : 0;
      *piRoll = -(int)((float)MECHA_SLIM_CROSS
                       * (fCos < 0.0f ? -fCos : 0.0f));
      return;
    }
    *piThigh = (int)((float)MECHA_LEG_SWING * mecha_sin(iAngle));
    *piKnee = fCos > 0.0f ? (int)((float)MECHA_LEG_KNEE * fCos) : 0;
    return;
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * How far below the hip the ankle ends up. This is what keeps the feet on
 * the floor: the body sinks by whatever the straighter leg has lost. The
 * shin's frame sits at K - A off the vertical. [MESH-06]
 */
static float mecha_leg_reach(int iThigh, int iKnee, float fThighLen,
                             float fShinLen)
{
  return fThighLen * mecha_cos(iThigh)
       + fShinLen * mecha_cos(mecha_angle_wrap(iThigh - iKnee));
}

//-------------------------------------------------------------------------------------------------

/*
 * The knee that puts this leg's ankle exactly fReach below the hip, which is
 * the inverse of the function above. Used to make two legs standing on the
 * same floor reach it the same way. [MESH-45]
 */
static int mecha_leg_knee_for_reach(int iThigh, float fReach,
                                    float fThighLen, float fShinLen)
{
  float fRest;

  if (fShinLen < 1e-4f)
    return 0;
  fRest = (fReach - fThighLen * mecha_cos(iThigh)) / fShinLen;
  return iThigh + (int)(acosf(mecha_clampf(fRest, -1.0f, 1.0f))
                        * (float)MECHA_ANGLE_FULL / 6.28318531f);
}

//-------------------------------------------------------------------------------------------------

/*
 * Where this machine is pointing its weapons, as a heading and an elevation.
 * Under a held lock that is the line to the target, which is what makes the
 * arms track an enemy that is circling you; otherwise it is the machine's
 * own heading and the elevation the sim is carrying, so the arms still point
 * wherever the shot is going to go.
 */
static void mecha_mech_aim(const tMechaWorld *pWorld, int iMechIdx,
                           int *piYaw, int *piPitch)
{
  const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMech *pTarget;
  const tMechaMechDef *pTargetDef;
  float fDx;
  float fDy;
  float fDz;
  float fFlat;

  *piYaw = pMech->iFacing;
  *piPitch = pMech->iAimPitch >= MECHA_ANGLE_HALF
             ? pMech->iAimPitch - MECHA_ANGLE_FULL : pMech->iAimPitch;

  if (pMech->byLock != MECHA_LOCK_HELD
      || pMech->iTargetIdx < 0 || pMech->iTargetIdx >= MECHA_MAX_MECHS)
    return;
  pTarget = &pWorld->aMechs[pMech->iTargetIdx];
  if (!pTarget->bActive)
    return;
  pTargetDef = mecha_def_get((int)pTarget->byDefIdx);

  fDx = pTarget->fX - pMech->fX;
  fDz = pTarget->fZ - pMech->fZ;
  fDy = (pTarget->fY + 0.62f * pTargetDef->fHeight)
        - (pMech->fY + 0.70f * mecha_def_get((int)pMech->byDefIdx)->fHeight);
  fFlat = mecha_length2(fDx, fDz);
  if (fFlat < 1e-3f)
    return;
  *piYaw = mecha_atan2_angle(fDx, fDz);
  *piPitch = mecha_atan2_angle(fDy, fFlat);
  if (*piPitch >= MECHA_ANGLE_HALF)
    *piPitch -= MECHA_ANGLE_FULL;
}

//-------------------------------------------------------------------------------------------------

/*
 * The drawn attitude, added up in one place as Whiplash does it in car.c.
 * [MESH-07]
 */
static void mecha_mesh_attitude(const tMechaMech *pMech, int *piYaw,
                                int *piPitch, int *piRoll)
{
  const tMechaAttitude *pAtt = &pMech->attitude;

  *piYaw += pAtt->iYawShake;
  *piPitch += pAtt->iContourPitch + pAtt->iAirPitch + pAtt->iPitchDrive
              + pAtt->iPitchWobble + pAtt->iPitchShake;
  *piRoll += pAtt->iContourRoll + pAtt->iRollSteer + pAtt->iRollWobble
             + pAtt->iRollShake + pAtt->iAirRoll;
}

//-------------------------------------------------------------------------------------------------

/*
 * How far through going down a machine is: 0 standing, 1 flat out. Going
 * down and getting up run the same curve off different clocks, and both
 * kinds of machine measure themselves against it -- they just fall in
 * different directions.
 */
static float mecha_mesh_fall_progress(const tMechaMech *pMech)
{
  switch (pMech->byMove) {
  case MECHA_MOVE_DOWN:
    return mecha_clampf((float)pMech->iStateTicks / (float)MECHA_FALL_TICKS,
                        0.0f, 1.0f);
  case MECHA_MOVE_RISE:
    /* iStunTicks runs down through the rise, so it doubles as the
     * animation's own clock. */
    return mecha_clampf((float)pMech->iStunTicks / (float)MECHA_RISE_TICKS,
                        0.0f, 1.0f);
  case MECHA_MOVE_DESTROYED: return 1.0f;
  default:                   return 0.0f;
  }
}

//-------------------------------------------------------------------------------------------------

static int mecha_mesh_fall_pitch(const tMechaMech *pMech)
{
  return (int)((float)MECHA_DEG(78) * mecha_mesh_fall_progress(pMech));
}

//-------------------------------------------------------------------------------------------------

/* A car does not fall on its face, it goes over: a wheeled machine's
 * knockdown is a half roll onto its roof. [MESH-08] */
static int mecha_mesh_fall_roll(const tMechaMech *pMech)
{
  return (int)((float)MECHA_ANGLE_HALF * mecha_mesh_fall_progress(pMech));
}

//-------------------------------------------------------------------------------------------------

/* The lean, which belongs to the torso alone: a machine leaning into a dash
 * leans over its own legs, and a fall takes the legs with it. */
static int mecha_mesh_lean_pitch(const tMechaMech *pMech)
{
  switch (pMech->byMove) {
  case MECHA_MOVE_DASH:      return MECHA_DEG(12);
  case MECHA_MOVE_JUMP:      return -MECHA_DEG(6);
  /* Nose down through the drop, which is what tells the other player the
   * arc has been thrown away rather than merely peaked. */
  case MECHA_MOVE_CANCEL:    return MECHA_DEG(16);
  case MECHA_MOVE_LAND:      return MECHA_DEG(9);
  case MECHA_MOVE_STAGGER:   return -MECHA_DEG(10);
  default:                   return 0;
  }
}

//-------------------------------------------------------------------------------------------------
/* The one machine on wheels */

/*
 * The ZIZIN KLR 330's body is the race game's own Zizin, polygon for
 * polygon, out of carplans.c. The plan's axes are of opposite handedness to
 * the arena's, so they swap and the lateral one is negated -- which reverses
 * every winding and is why these panels face inwards. [MESH-09]
 */
#define MECHA_ZIZIN_VERTS 86
/* The size of xzizin_anms in carplans.c, which the header only declares. */
#define MECHA_ZIZIN_ANMS  8

static void mecha_zizin_extent(float *pfLength, float *pfHeight)
{
  float fMinX = xzizin_coords[0].fX;
  float fMaxX = fMinX;
  float fMinZ = xzizin_coords[0].fZ;
  float fMaxZ = fMinZ;
  int i;

  for (i = 1; i < MECHA_ZIZIN_VERTS; i++) {
    if (xzizin_coords[i].fX < fMinX) fMinX = xzizin_coords[i].fX;
    if (xzizin_coords[i].fX > fMaxX) fMaxX = xzizin_coords[i].fX;
    if (xzizin_coords[i].fZ < fMinZ) fMinZ = xzizin_coords[i].fZ;
    if (xzizin_coords[i].fZ > fMaxZ) fMaxZ = xzizin_coords[i].fZ;
  }
  *pfLength = fMaxX - fMinX;
  *pfHeight = fMaxZ - fMinZ;
}

//-------------------------------------------------------------------------------------------------

/*
 * What the plan says a panel is painted with: a texture word, an animation
 * slot to look the real word up in, or a plain palette index. [MESH-10]
 */
static uint32_t mecha_zizin_surface(int iPoly)
{
  uint32_t uiTex = xzizin_pols[iPoly].uiTex;

  if ((uiTex & CAR_FLAG_ANMS_LOOKUP) != 0) {
    uint32_t uiSlot = uiTex & 0xFFu;

    if (uiSlot >= MECHA_ZIZIN_ANMS)
      return 0u;
    uiTex = xzizin_anms[uiSlot].framesAy[0];
  }
  return uiTex;
}

//-------------------------------------------------------------------------------------------------

static void mecha_add_zizin_body(tMechaQuadList *pList,
                                 const tMechaPose *pPose, float fScale,
                                 float fSink, uint8_t byBody, uint8_t byTop)
{
  int iFirst = pList->iCount;
  int iPoly;
  int i;

  for (iPoly = 0; iPoly < MECHA_ZIZIN_BODY_QUADS; iPoly++) {
    float afVert[4][3];
    int iCorner;

    /* Straight off the plan, with nothing nudged apart: no two of the fifty
     * share a plane, so the painter's algorithm needs no help. [MESH-11] */
    for (iCorner = 0; iCorner < 4; iCorner++) {
      const tVec3 *pPlan = &xzizin_coords[xzizin_pols[iPoly].verts[iCorner]];

      mecha_pose_apply(pPose, -pPlan->fY * fScale,
                       pPlan->fZ * fScale - fSink, pPlan->fX * fScale,
                       afVert[iCorner]);
    }
    if (s_bCarSkin) {
      /*
       * Painted the way the race game paints it: the same file, the same
       * tiles, panel for panel. Nothing is chosen here at all.
       */
      uint32_t uiTex = mecha_zizin_surface(iPoly);

      /*
       * How the artwork sits is the plan's to say. Read the flip flags off
       * the surface the lookup settled on, not off the polygon -- the wheels
       * carry no orientation of their own and the frames behind them do.
       * [MESH-12]
       */
      bool bFlipH = (uiTex & SURFACE_FLAG_FLIP_HORIZ) != 0;
      bool bFlipV = (uiTex & SURFACE_FLAG_FLIP_VERT) != 0;
      uint8_t byFlags = MECHA_QUAD_TWO_SIDED | MECHA_QUAD_TEX_FLIP;

      if (bFlipH != bFlipV)
        byFlags &= (uint8_t)~MECHA_QUAD_TEX_FLIP;
      if (bFlipH)
        byFlags |= MECHA_QUAD_TEX_ROT180;

      mecha_quads_add(pList, afVert, (uint8_t)(uiTex & 0xFFu), byFlags);
      if ((uiTex & SURFACE_FLAG_APPLY_TEXTURE) != 0)
        mecha_tag_texture(pList, MECHA_TEX_CAR, (int)(uiTex & 0xFFu));
    } else {
      mecha_quads_add(pList, afVert, byBody, MECHA_QUAD_TWO_SIDED);
    }
  }

  /*
   * With no skin, fall back to the machine's two colours, split by each
   * panel's own normal rather than by where it sits -- the sills are as high
   * as some of the bonnet. Downwards, because the reflection reversed every
   * normal. [MESH-09]
   */
  if (!s_bCarSkin) {
    for (i = iFirst; i < pList->iCount; i++) {
      if (pList->paQuads[i].afNormal[1] < -MECHA_ZIZIN_ROOF_FACING)
        pList->paQuads[i].byPalette = byTop;
    }
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * And the gun: a handgun as long as the car, attached to nothing, floating
 * off the front right wheel and held on its side. [MESH-13]
 */
static void mecha_add_zizin_gun(tMechaQuadList *pList,
                                const tMechaPose *pCar, const tMechaMech *pMech,
                                const tMechaMechDef *pDef, int iAimYaw,
                                int iAimPitch, float fLength, uint8_t byBody,
                                uint8_t byTrim, uint8_t byGlow)
{
  float fKick = pMech->iRecovery > 0
                  ? (float)mecha_clampi(pMech->iRecovery, 0, MECHA_GUN_KICK_TICKS)
                    / (float)MECHA_GUN_KICK_TICKS
                  : 0.0f;
  int iYaw = mecha_clampi(mecha_angle_delta(pMech->iFacing, iAimYaw),
                          -MECHA_GUN_YAW_LIMIT, MECHA_GUN_YAW_LIMIT);
  tMechaPose mount;
  tMechaPose gun;
  /* The whole weapon, nose to backplate, and the calibre everything else on
   * it is measured in. A third of the car's length and thick with it, so the
   * gun still reads as a gun from the far side of an arena. */
  float fGun = fLength * MECHA_GUN_LENGTH;
  float fBore = fGun * 0.068f;
  float fMount = pDef->fHeight * MECHA_GUN_MOUNT_Y;

  /*
   * A heavy machine gun on a pylon over the front right wing, not a pistol
   * held out beside the car. The pylon runs down into the bodywork on
   * purpose -- it is a mount, and a mount that stops short of what it is
   * bolted to reads as a prop floating beside the car. Firing shoves the
   * whole thing back and tips the muzzle up as the recovery runs down.
   * [MESH-13]
   */
  /*
   * The mount is bolted to the car and the gun turns on it, so they are two
   * poses rather than one: the pylon is built from the mount and does not
   * move when the gun tracks, which is what a pintle looks like. The recoil
   * shove belongs to the gun for the same reason.
   */
  mecha_pose_child(&mount, pCar,
                   pDef->fRadius * MECHA_GUN_MOUNT_X,
                   fMount,
                   pDef->fRadius * MECHA_GUN_MOUNT_Z,
                   0, 0, 0);
  mecha_pose_child(&gun, &mount, 0.0f, 0.0f, -fGun * 0.18f * fKick,
                   iYaw - MECHA_GUN_ACROSS,
                   -iAimPitch + (int)((float)MECHA_GUN_KICK_PITCH * fKick),
                   0);

  /*
   * No two of these boxes share a face plane, and none comes within a
   * centimetre of doing so. Two coplanar quads that overlap have no answer
   * to which is in front and there is no depth buffer to settle it, so the
   * extents below are deliberately odd rather than tidy. [MESH-11]
   */

  /* The pylon, down into the wing. It stops inside the bodywork rather
   * than under it: a mount that reaches past the floor hangs below the car
   * and catches the ground. */
  {
    float fDrop = fMount - MECHA_M(0.55f);

    if (fDrop < fBore)
      fDrop = fBore;
    mecha_add_box(pList, &mount, 0.0f, -fDrop * 0.5f, -fGun * 0.04f,
                  fBore * 0.90f, fDrop * 0.5f, fBore * 1.30f,
                  byTrim, byTrim, 0);
  }

  /* Receiver: the square body of it, which is most of what reads at
   * distance. */
  mecha_add_box(pList, &gun, 0.0f, 0.0f, -fGun * 0.16f,
                fBore * 1.50f, fBore * 1.70f, fGun * 0.26f,
                byBody, byTrim, 0);

  /* Backplate and spade grips. */
  mecha_add_box(pList, &gun, 0.0f, -fBore * 0.17f, -fGun * 0.445f,
                fBore * 1.62f, fBore * 1.28f, fBore * 0.66f,
                byTrim, byTrim, 0);

  /* Jacket over the rear of the barrel, then the barrel running out past
   * it. */
  mecha_add_box(pList, &gun, 0.0f, 0.0f, fGun * 0.225f,
                fBore * 1.02f, fBore * 1.02f, fGun * 0.175f,
                byTrim, byBody, 0);
  mecha_add_box(pList, &gun, 0.0f, 0.0f, fGun * 0.475f,
                fBore * 0.56f, fBore * 0.56f, fGun * 0.165f,
                byBody, byTrim, 0);

  /* The muzzle, which is the only part of it anybody looks at. */
  mecha_add_box(pList, &gun, 0.0f, 0.0f, fGun * 0.625f,
                fBore * 0.78f, fBore * 0.78f, fBore * 0.52f,
                byGlow, byGlow, MECHA_QUAD_GLOW);

  /* Ammunition box on the feed side, which is what says heavy. */
  mecha_add_box(pList, &gun, -fBore * 2.24f, -fBore * 0.60f, -fGun * 0.30f,
                fBore * 0.98f, fBore * 1.00f, fBore * 1.36f,
                byTrim, byBody, 0);
}

//-------------------------------------------------------------------------------------------------

/* The machine's own three colours, or the paint scheme it was given. The
 * glow is never repainted: it is what says whose fire is whose. [DEF-06] */
static void mecha_mech_colours(const tMechaMech *pMech,
                               const tMechaMechDef *pDef,
                               uint8_t *pbyBody, uint8_t *pbyTrim,
                               uint8_t *pbyJoint)
{
  const tMechaScheme *pScheme = mecha_scheme_get((int)pMech->byScheme);

  *pbyBody = pScheme ? pScheme->byBody : pDef->abyPalette[0];
  *pbyTrim = pScheme ? pScheme->byTrim : pDef->abyPalette[1];
  *pbyJoint = pScheme ? pScheme->byJoint : pDef->abyPalette[2];
}

//-------------------------------------------------------------------------------------------------

static void mecha_mesh_car(tMechaQuadList *pList, const tMechaWorld *pWorld,
                           int iMechIdx)
{
  const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_def_get((int)pMech->byDefIdx);
  tMechaPose pose;
  float fPlanLength;
  float fPlanHeight;
  float fScale;
  int iAimYaw;
  int iAimPitch;
  uint8_t byBody;
  uint8_t byTrim;
  uint8_t byJoint;

  mecha_zizin_extent(&fPlanLength, &fPlanHeight);
  if (fPlanHeight <= 0.0f)
    return;
  /* Scaled by height, because height is what the roster is measured in and
   * what says this thing is a sixth of a machine. */
  fScale = pDef->fHeight / fPlanHeight;

  {
    int iYaw = pMech->iFacing;
    int iPitch = 0;
    /* Negated for the same reason as the walkers': positive roll leans
     * left, and a car sliding right should lean right. */
    int iRoll = -(int)(pMech->fLeanRoll
                       * mecha_clampf((pMech->fVelX
                                         * mecha_cos(pMech->iFacing)
                                       - pMech->fVelZ
                                         * mecha_sin(pMech->iFacing))
                                      / (pDef->fWalkSpeed > 1.0f
                                           ? pDef->fWalkSpeed : 1.0f),
                                      -1.0f, 1.0f));
    int iFlip = mecha_mesh_fall_roll(pMech);
    float fLift = 0.0f;

    iRoll += iFlip;
    /* Going over lifts it back onto the ground: the pose turns about the
     * car's floor, so half a roll would bury it. [MESH-14] */
    if (iFlip != 0) {
      float fDrop = -mecha_cos(iFlip);

      if (fDrop > 0.0f)
        fLift = pDef->fHeight * fDrop;
    }

    mecha_mesh_attitude(pMech, &iYaw, &iPitch, &iRoll);
    mecha_pose_build(&pose, iYaw, iPitch, iRoll,
                     pMech->fX, pMech->fY + fLift, pMech->fZ, 1.0f);
  }

  mecha_mech_colours(pMech, pDef, &byBody, &byTrim, &byJoint);
  mecha_add_zizin_body(pList, &pose, fScale, 0.0f, byBody, byTrim);

  mecha_mech_aim(pWorld, iMechIdx, &iAimYaw, &iAimPitch);
  iAimPitch = mecha_clampi(iAimPitch, -MECHA_ARM_PITCH_LIMIT,
                           MECHA_ARM_PITCH_LIMIT);
  mecha_add_zizin_gun(pList, &pose, pMech, pDef, iAimYaw, iAimPitch,
                      fPlanLength * fScale, byBody, byTrim,
                      pDef->abyPalette[3]);
}


//-------------------------------------------------------------------------------------------------
/*
 * Everything the part builders need that does not change between them: the
 * machine, its colours, its build multipliers and the tier being drawn.
 * Three chassis share an upper body, and passing this rather than a dozen
 * floats is what makes sharing it bearable. [MESH-37]
 */
typedef struct
{
  const tMechaMech    *pMech;
  const tMechaMechDef *pDef;
  uint8_t byBody;
  uint8_t byTrim;
  uint8_t byJoint;
  uint8_t byGlow;
  float   fHeight;
  float   fRadius;
  float   fShoulder;
  float   fTorso;
  float   fLimb;
  float   fHead;
  float   fGun;
  /* What the profile does to the frame: limbs in, skirt out. [MESH-33] */
  float   fTaper;
  float   fFlare;
  /*
   * And what it does to the chest, which is not the same number. Taking the
   * limbs' taper to the torso as well puts the chest narrower than the waist
   * under it, which is not a slighter machine, it is an upside-down one.
   * [MESH-33]
   */
  float   fChest;
  /* Hiiragi's reference mesh has a lowered, tucked toe and a taller crest.
   * Keep these in the build description so animation poses remain unchanged. */
  float   fFootToeDrop;
  float   fCrestLift;
  float   fCrestHeight;
  /*
   * Where the hips are, and what that does to everything above them. A
   * machine whose hips ride high has long legs and a short body, and the
   * second half of that is not optional -- left alone, raising the hips
   * simply makes the machine taller than its own height. [TYPE-08]
   */
  float   fHipY;
  float   fUpperY;   /* fHeight, scaled for the shortened upper body */
  float   fArmLen;   /* fHeight, scaled for the arm length */
  /*
   * Where in the stride the machine is, or -1 when there is no stride to be
   * in. Everything above the waist reads this: arms that swing against the
   * legs, shoulders that twist against the hips, a torso that rocks with
   * the step. Set by whichever chassis builder knows, because only it knows
   * which gait the legs are running. [MESH-43]
   */
  float   fWalkPhase;
  int     iProfile;
  int     iDetail;
  /* Where to write the joints down, or NULL for the usual case of nobody
   * asking. [MESHH-05] */
  tMechaBoneFrame *paBones;
} tMechaBuild;

static void mecha_build_setup(tMechaBuild *pB, const tMechaMech *pMech,
                              const tMechaMechDef *pDef, int iDetail)
{
  memset(pB, 0, sizeof(*pB));
  pB->pMech = pMech;
  pB->pDef = pDef;
  mecha_mech_colours(pMech, pDef, &pB->byBody, &pB->byTrim, &pB->byJoint);
  pB->byGlow = pDef->abyPalette[3];
  pB->fHeight = pDef->fHeight;
  pB->fRadius = pDef->fRadius;
  /* Zero means one, so a machine that never declares a build still gets the
   * proportions the mesh was originally written around. */
  pB->fShoulder = pDef->fBuildShoulder > 0.0f ? pDef->fBuildShoulder : 1.0f;
  pB->fTorso    = pDef->fBuildTorso    > 0.0f ? pDef->fBuildTorso    : 1.0f;
  pB->fLimb     = pDef->fBuildLimb     > 0.0f ? pDef->fBuildLimb     : 1.0f;
  pB->fHead     = pDef->fBuildHead     > 0.0f ? pDef->fBuildHead     : 1.0f;
  pB->fGun      = pDef->fBuildGun      > 0.0f ? pDef->fBuildGun      : 1.0f;
  pB->iProfile  = (int)pDef->byProfile;
  /*
   * A slender machine is not simply a smaller one: its limbs narrow towards
   * the joints while its skirt goes the other way, and it is that
   * opposition -- thin legs under a wide flare -- that reads as the build
   * rather than as a mech drawn at three quarters. [MESH-33]
   */
  pB->fTaper = pB->iProfile == MECHA_PROFILE_SLENDER ? 0.74f : 1.0f;
  /*
   * Back off from the 1.9 this was briefly at. That was reaching for a
   * silhouette that would not read as the interceptor's at range, which is
   * a job the machine's height does instead [DEF-10]; a flare that wide,
   * once the plates were actually joined to the waist, made the hips one
   * slab from one side to the other rather than armour hung on a frame.
   */
  /*
   * Raised with the narrowed waist [DEF-13]. The skirt is drawn off
   * the torso width, so bringing the torso in takes the hips with it and
   * the figure merely gets smaller rather than getting a shape; 1.25 only
   * held the skirt where it was. This puts it past where it started, which
   * is what makes the hips rather than the shoulders the widest thing on
   * the machine.
   *
   * There is headroom for it now that there was not at 1.9. What broke then
   * was the plates reaching a width they could not be joined to the waist
   * at, and width here is torso times flare: 1.9 on the old 0.74 torso is
   * 1.41, where 2.15 on the 0.60 one is 1.29. The number went up by more
   * than a tenth and the skirt still came out narrower than the one that
   * failed, which is the only reason a figure this high is safe.
   */
  pB->fFlare = pB->iProfile == MECHA_PROFILE_SLENDER ? 2.0f : 1.0f;
  pB->fChest = pB->iProfile == MECHA_PROFILE_SLENDER ? 0.82f : 1.0f;
  pB->fFootToeDrop = pB->iProfile == MECHA_PROFILE_SLENDER ? 0.0f : 0.0f;
  /* Both read only by the slender crest, which is swept back in a frame of
   * its own [MESH-55]: the lift is off the brow line and the height is the
   * blade's thickness, not its length. */
  pB->fCrestLift = 0.0026f;
  pB->fCrestHeight = 0.0161f;
  {
    /*
     * The figure. MECHA_HIP_CLASSIC is where the hips sat when there was
     * only one build, and every offset above the waist was written against
     * it -- so the upper body is drawn at whatever scale puts the head back
     * where it was, and raising the hips shortens the body by exactly as
     * much as it lengthens the legs. [TYPE-08]
     */
    float fHip = pDef->fBuildHip > 0.0f ? pDef->fBuildHip
                                        : MECHA_HIP_CLASSIC;
    float fArm = pDef->fBuildArm > 0.0f ? pDef->fBuildArm : 1.0f;

    pB->fHipY = fHip * pB->fHeight;
    pB->fUpperY = pB->fHeight * (1.0f - fHip) / (1.0f - MECHA_HIP_CLASSIC);
    pB->fArmLen = pB->fHeight * fArm;
  }
  pB->fWalkPhase = -1.0f;
  pB->iDetail = iDetail;
}

//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/*
 * The waist, the chest and the pack on the back of it. Every chassis with a
 * torso at all shares these, which is the point of pulling them out: a
 * machine on tracks is a torso that happens to have no legs under it.
 * [MESH-37]
 */
static void mecha_build_torso(tMechaQuadList *pList, const tMechaBuild *pB,
                              const tMechaPose *pTorso)
{
  int iSide;

  mecha_quads_part(pList, MECHA_PART_TORSO);


  /*
   * The waist block, and it is narrower than both the chest above it and
   * the skirt below: a machine with a waist has three shapes stacked up
   * where a machine without one has a column. [MESH-34]
   */
  /* The waist, which keeps its widths and gains a twist: the belly flares
   * forward of the hip and the small of the back stays broad, while above
   * it that reverses and the front draws in under the chest. [MESH-55] */
  mecha_add_hull(pList, pTorso, 0.0f, 0.0f, 0.0f,
                 0.52f * pB->fRadius * pB->fTorso * pB->fTaper,
                 0.38f * pB->fRadius * pB->fTorso * pB->fTaper,
                 0.58f * pB->fRadius * pB->fTorso * pB->fTaper,
                 0.40f * pB->fRadius * pB->fTorso * pB->fTaper,
                 0.07f * pB->fUpperY, 0.0f, 0.0f, 0.0f,
                 1.1436f, 0.7487f, pB->byJoint, pB->byJoint, 0);

  /*
   * The chest, widening from the waist to the shoulders. Everything about
   * a mecha's build is in that one taper, which is why it is a frustum and
   * the thing it replaced was a box.
   *
   * Pitched forward in a frame of its own and raked on top of that, so it
   * leans into the machine's own front rather than standing square on the
   * waist: the breastbone runs out and down and the deck behind it falls
   * away to the spine. It is narrower than it was, and deeper, which is
   * the trade that stops a slender frame reading as a crate. The binders
   * and the arms keep hanging off fChestW and do not move with it, which
   * is why that width is worked out separately. [MESH-55]
   */
  {
    tMechaPose chest;

    mecha_pose_child(&chest, pTorso, 0.0f, 0.0f, 0.0f, 0,
                     MECHA_CHEST_PITCH, 0);
    mecha_add_hull(pList, &chest, 0.0f, 0.160f * pB->fUpperY,
                   -0.013f * pB->fRadius,
                   0.618f * pB->fRadius * pB->fTorso * pB->fTaper,
                   0.380f * pB->fRadius * pB->fTorso * pB->fChest,
                   0.6616f * pB->fRadius * pB->fTorso * pB->fChest,
                   0.583f * pB->fRadius * pB->fTorso * pB->fChest,
                   0.1306f * pB->fUpperY, 0.0f,
                   -0.0626f * pB->fRadius, 0.0774f, 0.865f, 0.768f,
                   pB->byBody, pB->byTrim, 0);
  }

  /*
   * The glacis. This was a plate laid on the front of the chest with four
   * hundredths of a radius of rake on it, which is to say it was the front
   * of the chest: a flat box with a seam drawn on it. What it is now is the
   * front end of a tank -- the bottom edge proud of the waist, the whole
   * plate laid back from there to the collar, so the chest is a wedge seen
   * from the side and the machine has something that would turn a round
   * rather than catch it square.
   *
   * The rake is the whole of it and it wants to be large: at 0.04 the plate
   * reads as panel line, and it is only past about 0.14 that the eye stops
   * seeing a box. Further than 0.24 and the plate's top rear corner starts
   * cutting back through the chest it is supposed to be lying on. [MESH-54]
   */
  mecha_add_frustum(pList, pTorso, 0.0f, 0.230f * pB->fUpperY,
                    0.573f * pB->fRadius * pB->fTorso * pB->fChest,
                    0.52f * pB->fRadius * pB->fTorso * pB->fChest,
                    0.08f * pB->fRadius,
                    0.44f * pB->fRadius * pB->fTorso * pB->fChest,
                    0.05f * pB->fRadius,
                    0.09f * pB->fUpperY, 0.0f, -0.18f * pB->fRadius,
                    pB->byTrim, pB->byTrim, 0);

  /* Intakes either side of it, the one piece of chest trim that reads at
   * any distance because it breaks the plate's outline rather than sitting
   * inside it. */
  if (pB->iDetail >= MECHA_DETAIL_FULL) {
    for (iSide = 0; iSide < 2; iSide++) {
      float fSide = iSide == 0 ? -1.0f : 1.0f;

      /* Narrower, deeper at the root and swept outboard and aft as they
       * rise, which is the reference's shape: a pair of gills raked back
       * along the chest rather than two lamps stuck on the front of it.
       * [MESH-55] */
      mecha_add_frustum(pList, pTorso,
                        fSide * 0.422f * pB->fRadius * pB->fTorso,
                        0.198f * pB->fUpperY,
                        0.4392f * pB->fRadius * pB->fTorso,
                        0.1229f * pB->fRadius * pB->fTorso,
                        0.1155f * pB->fRadius,
                        0.120f * pB->fRadius * pB->fTorso,
                        0.0592f * pB->fRadius,
                        0.0619f * pB->fUpperY,
                        fSide * 0.1079f * pB->fRadius * pB->fTorso,
                        -0.0531f * pB->fRadius,
                        pB->byGlow, pB->byGlow, MECHA_QUAD_GLOW);
    }
  }

  /* A collar between the shoulders, so the head has something to sit in. */
  if (pB->iDetail >= MECHA_DETAIL_MID) {
    mecha_add_frustum(pList, pTorso, 0.0f, 0.30f * pB->fUpperY, 0.0f,
                      0.42f * pB->fRadius * pB->fTorso * pB->fChest,
                      0.34f * pB->fRadius * pB->fTorso * pB->fChest,
                      0.30f * pB->fRadius * pB->fTorso * pB->fChest,
                      0.26f * pB->fRadius * pB->fTorso * pB->fChest,
                      0.03f * pB->fUpperY, 0.0f, 0.0f, pB->byJoint, pB->byJoint, 0);
  }

  /*
   * The thruster pack. A carrier wears a rack of pods here instead, which
   * is the whole of how that machine reads from behind. [MESH-36]
   */
  mecha_add_frustum(pList, pTorso, 0.0f, 0.19f * pB->fUpperY,
                    -0.56f * pB->fRadius * pB->fTorso * pB->fChest,
                    0.50f * pB->fRadius * pB->fTorso * pB->fChest,
                    0.16f * pB->fRadius,
                    0.44f * pB->fRadius * pB->fTorso * pB->fChest,
                    0.0806f * pB->fRadius,
                    0.11f * pB->fUpperY, 0.0f, 0.0194f * pB->fRadius,
                    pB->byJoint, pB->byJoint, 0);

  /*
   * A carrier wears a rack of pods over the pack, and that rack is the
   * whole of how this machine reads: two columns of four standing above
   * the shoulders, so from any angle there is a block of hardware up there
   * that no other frame in the roster has. They are drawn whether or not
   * anything has been launched -- a rack that emptied as the round went on
   * would leave the machine looking like a different machine by the end of
   * it, and the silhouette has to be the one thing that holds still.
   * [MESH-36]
   */
  if (pB->iProfile == MECHA_PROFILE_CARRIER) {
    int iPod;

    mecha_quads_part(pList, MECHA_PART_DRONE);
    for (iPod = 0; iPod < 8; iPod++) {
      float fSide = (iPod & 1) ? 1.0f : -1.0f;
      int iRow = iPod / 2;
      float fY = 0.20f * pB->fUpperY + (float)iRow * 0.085f * pB->fUpperY;
      /* The columns rake backwards going up, so the rack leans off the
       * pack rather than standing on it like a wardrobe. */
      float fZ = -0.66f * pB->fRadius * pB->fTorso
                 - (float)iRow * 0.045f * pB->fRadius;

      mecha_add_frustum(pList, pTorso,
                        fSide * 0.30f * pB->fRadius * pB->fTorso, fY, fZ,
                        0.19f * pB->fRadius, 0.16f * pB->fRadius,
                        0.15f * pB->fRadius, 0.13f * pB->fRadius,
                        0.035f * pB->fUpperY, 0.0f, 0.0f,
                        pB->byTrim, pB->byGlow, 0);
    }
    mecha_quads_part(pList, MECHA_PART_TORSO);
  }

  /* Two nozzles under it, angled out. One slab of a backpack has no scale
   * to it; a pair of bells says how big the machine is. */
  if (pB->iDetail >= MECHA_DETAIL_FULL) {
    for (iSide = 0; iSide < 2; iSide++) {
      float fSide = iSide == 0 ? -1.0f : 1.0f;

      mecha_add_frustum(pList, pTorso,
                        fSide * 0.30f * pB->fRadius * pB->fTorso,
                        0.05f * pB->fUpperY, -0.60f * pB->fRadius * pB->fTorso,
                        0.15f * pB->fRadius, 0.13f * pB->fRadius,
                        0.10f * pB->fRadius, 0.09f * pB->fRadius,
                        0.05f * pB->fUpperY,
                        fSide * 0.02f * pB->fRadius, 0.03f * pB->fRadius,
                        pB->byTrim, pB->byTrim, 0);
    }
  }
}

//-------------------------------------------------------------------------------------------------
/*
 * Skirt armour, which belongs to a machine with legs to hang it over.
 * paiThigh is the pair of thigh swings the front plates hinge on.
 */
static void mecha_build_skirt(tMechaQuadList *pList, const tMechaBuild *pB,
                              const tMechaPose *pPelvis, const int *paiThigh)
{
  int iSide;

  mecha_quads_part(pList, MECHA_PART_SKIRT);

  /*
   * Skirt armour: six plates hanging off the waist, flaring as they fall.
   * The two at the sides are the whole of what makes one machine's hips
   * read wide and another's narrow, so they are the pair that survives to
   * the far tier while the front and rear plates do not. [MESH-34]
   *
   * Every one of them is hinged on the leg behind it, the way the plates on
   * a model kit are. The front pair always were -- a skirt that stayed put
   * has the thigh pass straight through it at the top of every stride --
   * but the sides and the rear were bolted to the pelvis, which on a walking
   * machine reads as a solid bell hung under the waist rather than armour.
   * The rear was one plate spanning both legs, which is the giveaway: a
   * plate that both legs share cannot move with either.
   *
   * They swing by different amounts because they are in different places.
   * The front and rear plates are directly in a thigh's way and take just
   * over half its swing; the sides are beside the leg rather than in front
   * of it, and only need enough to sway with it. All three use the same
   * sign, which is what makes each plate get out of the way of its own leg:
   * a thigh forward carries the front plate forward, and a thigh back
   * carries the rear one back. [MESH-54]
   */
  {
    /*
     * The plate is hung off the waist rather than placed beside it. The
     * waist's own bottom edge is where it starts, and the flare grows how
     * far past that it reaches -- which is the whole fix for a wide skirt
     * that used to float: scaling the plate's centre by the flare moved the
     * inner edge out too, and left a machine with its hips hanging in the
     * air either side of it. [MESH-34]
     */
    float fWaist = 0.52f * pB->fRadius * pB->fTorso * pB->fTaper;
    float fReach = 0.30f * pB->fRadius * pB->fTorso * pB->fFlare;
    float fHalf = 0.5f * fReach;

    /* Narrower where it meets the waist than where it hangs, so the plate
     * has a taper of its own and reads as armour rather than as a slab. */
    float fHalfTop = 0.45f * fHalf;

    for (iSide = 0; iSide < 2; iSide++) {
      float fSide = iSide == 0 ? -1.0f : 1.0f;
      int   iSwing = paiThigh[iSide];
      tMechaPose plate;

      /*
       * The side plate, hinged where its top inner corner meets the waist
       * so it swings from the joint rather than about its own middle. The
       * geometry is unchanged: at rest the pivot puts it exactly where it
       * sat when it was bolted on.
       */
      mecha_pose_child(&plate, pPelvis, fSide * fWaist, 0.0f,
                       -0.01f * pB->fRadius, 0,
                       (int)(-0.30f * (float)iSwing), 0);
      mecha_pose_name(&plate, MECHA_BONE_SKIRT_SIDE_L + iSide, pB->paBones);
      mecha_add_frustum(pList, &plate,
                        fSide * fHalf,
                        -0.07f * pB->fUpperY, 0.0f,
                        fHalf, 0.34f * pB->fRadius * pB->fTorso,
                        fHalfTop, 0.30f * pB->fRadius * pB->fTorso,
                        0.07f * pB->fUpperY,
                        /* The top's inner edge lands on the waist too, which
                         * is what the skew is carrying. */
                        fSide * (fHalfTop - fHalf), 0.0f,
                        pB->byBody, pB->byTrim, 0);

      if (pB->iDetail >= MECHA_DETAIL_MID) {
        /* Just over half the thigh's swing: enough that the plate is never
         * inside the leg, little enough that it still reads as armour rather
         * than as a second thigh. */
        mecha_pose_child(&plate, pPelvis,
                         fSide * 0.24f * pB->fRadius * pB->fTorso,
                         -0.02f * pB->fUpperY, 0.0f, 0,
                         (int)(-0.55f * (float)iSwing), 0);
        mecha_pose_name(&plate, MECHA_BONE_SKIRT_FRONT_L + iSide,
                        pB->paBones);
        mecha_add_frustum(pList, &plate, 0.0f, -0.05f * pB->fUpperY,
                          0.30f * pB->fRadius * pB->fTorso,
                          0.21f * pB->fRadius * pB->fTorso,
                          0.10f * pB->fRadius * pB->fTorso,
                          0.18f * pB->fRadius * pB->fTorso,
                          0.07f * pB->fRadius * pB->fTorso,
                          0.06f * pB->fUpperY, 0.0f,
                          -0.05f * pB->fRadius * pB->fTorso,
                          pB->byBody, pB->byTrim, 0);
      }

      /* And half a plate across the back of the waist, per leg. One plate
       * spanning both is a plate neither leg can move. */
      if (pB->iDetail >= MECHA_DETAIL_FULL) {
        mecha_pose_child(&plate, pPelvis,
                         fSide * 0.02f * pB->fRadius * pB->fTorso,
                         -0.02f * pB->fUpperY,
                         -0.10f * pB->fRadius * pB->fTorso, 0,
                         (int)(-0.55f * (float)iSwing), 0);
        mecha_pose_name(&plate, MECHA_BONE_SKIRT_REAR_L + iSide,
                        pB->paBones);
        mecha_add_frustum(pList, &plate,
                          fSide * 0.21f * pB->fRadius * pB->fTorso,
                          -0.03f * pB->fUpperY,
                          -0.22f * pB->fRadius * pB->fTorso,
                          0.21f * pB->fRadius * pB->fTorso,
                          0.11f * pB->fRadius * pB->fTorso,
                          0.19f * pB->fRadius * pB->fTorso,
                          0.08f * pB->fRadius * pB->fTorso,
                          0.05f * pB->fUpperY, 0.0f,
                          0.04f * pB->fRadius * pB->fTorso,
                          pB->byJoint, pB->byJoint, 0);
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
/*
 * Pauldrons, arms, the guns on the end of them and the head above them.
 * Shared for the same reason the torso is.
 */
/*
 * An arm held somewhere the aim did not put it. [MESH-41]
 *
 * Every arm in the mode until now read one aim between the two of them: the
 * shoulder turned and elevated onto the line the weapons pointed down, and
 * the only thing either arm owned by itself was the recoil kick. That is
 * right while the machine is fighting and wrong the moment it stops -- one
 * arm up and one at the hip is not a thing that rig could say at all.
 *
 * The angles are chosen to be readable rather than to match the chain:
 * iUpper is where the upper arm points, measuring nought as hanging
 * straight down, a quarter turn negative as level and forward, and half a
 * turn negative as straight up. iElbow folds the forearm on top of that.
 */
typedef struct
{
  int iYaw;      /* shoulder, off the torso; positive is across the body */
  /*
   * The whole arm out to the side, and the sign is not mirrored between the
   * two arms because it cannot be: the roll happens before the elevation, so
   * which way a given sign throws the hand depends on whether the arm is
   * hanging or raised. A hanging arm rolled positive goes to the machine's
   * right; a raised one rolled positive goes to its left. Each entry below
   * is therefore written for the arm it is on, and checked by looking.
   */
  int iRoll;
  int iUpper;    /* 0 hangs down, -quarter is forward, -half is up */
  int iElbow;    /* the forearm, on top of the upper arm */
} tMechaArmPose;

/*
 * The win poses, one per profile, and they are the reason this exists. A
 * round used to end with the machine that had just won it standing exactly
 * as it stands at any other moment. [MESH-42]
 *
 * Indexed [profile][side], side 0 being the machine's left.
 */
static const tMechaArmPose s_aaWinPose[MECHA_PROFILE_COUNT][2] = {
  /* STANDARD: the gun arm comes up across the chest, the other stays down
   * and out. A soldier's salute rather than a flourish. */
  {
    { MECHA_DEG(-8),  MECHA_DEG(-14), MECHA_DEG(-24),  MECHA_DEG(-52) },
    { MECHA_DEG(26),  MECHA_DEG(-10), MECHA_DEG(-118), MECHA_DEG(-46) },
  },
  /* SLENDER: one arm straight up beside the head, the other folded onto the
   * hip. This is the pose the frame was built for. */
  {
    { MECHA_DEG(-6),  MECHA_DEG(-30), MECHA_DEG(-6),   MECHA_DEG(-56) },
    { MECHA_DEG(14),  MECHA_DEG(-20), MECHA_DEG(-138), MECHA_DEG(-30) },
  },
  /* CARRIER: both arms low and open, because what this machine is showing
   * off is the rack on its back rather than anything in its hands. */
  {
    { MECHA_DEG(-22), MECHA_DEG(-30), MECHA_DEG(-28),  MECHA_DEG(-34) },
    { MECHA_DEG(22),  MECHA_DEG(30),  MECHA_DEG(-28),  MECHA_DEG(-34) },
  },
};

/*
 * Whether this machine is holding a pose, and how far into it. Zero is
 * fighting; one is fully posed. A round ends on a phase change rather than
 * on a tick, so the ease comes off how long the phase has been running --
 * which also means a machine that wins on the last shot of a round is not
 * snapped into a victory stance by the same frame that killed the loser.
 */
static float mecha_mech_pose_amount(const tMechaWorld *pWorld, int iMechIdx)
{
  const tMechaMech *pMech;

  if (pWorld->match.byPhase != MECHA_PHASE_ROUND_OVER
      && pWorld->match.byPhase != MECHA_PHASE_MATCH_OVER)
    return 0.0f;
  if (iMechIdx < 0 || iMechIdx >= MECHA_MAX_MECHS)
    return 0.0f;
  pMech = &pWorld->aMechs[iMechIdx];
  /* Only a machine still standing, and only one on the winning side. The
   * loser has its own thing to be doing. */
  if (!mecha_mech_alive(pMech) || pMech->byMove == MECHA_MOVE_DOWN
      || pMech->byMove == MECHA_MOVE_DESTROYED)
    return 0.0f;
  if (!mecha_mech_allied(pWorld, iMechIdx, pWorld->match.iWinnerIdx))
    return 0.0f;
  /* Airborne, it has no business striking a pose. */
  if (pMech->fY > mecha_arena_ground_height(&pWorld->arena, pMech->fX,
                                            pMech->fZ, pMech->fY)
                  + 0.05f * MECHA_METRE)
    return 0.0f;
  return mecha_clampf((float)pWorld->match.iPhaseTicks
                      / (float)MECHA_POSE_EASE_TICKS, 0.0f, 1.0f);
}

//-------------------------------------------------------------------------------------------------

static void mecha_build_arms_head(tMechaQuadList *pList,
                                  const tMechaWorld *pWorld, int iMechIdx,
                                  const tMechaBuild *pB,
                                  const tMechaPose *pTorso)
{
  int iSide;

  mecha_quads_part(pList, MECHA_PART_ARM);

  /* --- arms -------------------------------------------------------------
   *
   * Each arm is a shoulder, an elbow and the gun the forearm carries, and
   * the whole chain is aimed: the shoulder turns and elevates onto the line
   * the weapons are pointing down, which under a held lock is the line to
   * the target. Firing kicks the arm that fired.
   */
  {
    int iAimYaw;
    int iAimPitch;
    int iArmYaw;
    float fReady = mecha_clampf(pB->pMech->fCombat, 0.0f, 1.0f);
    float fUpper = 0.20f * pB->fArmLen;
    float fFore = 0.17f * pB->fArmLen;
    /* How far this machine has moved off its aim and into a held pose. */
    float fPosed = mecha_mech_pose_amount(pWorld, iMechIdx);
    /*
     * Where the chest actually ends, and therefore where the shoulder is.
     * The binder is seated against this rather than placed at a multiple of
     * the machine's radius: scaling its offset by the build, which is how
     * this was first written, walks its inner face away from the shoulder
     * it is bolted to and leaves it hanging in the air beside it -- the same
     * mistake the skirt plates had. [MESH-34]
     */
    float fChestW = 0.76f * pB->fRadius * pB->fTorso * pB->fChest;
    /* Flared at the root off the reference mesh: the binder's underside
     * reaches further outboard than its top, so the outer face slopes in
     * as it rises instead of standing straight up. [MESH-55] */
    float fBinderHx0 = 0.421f * pB->fRadius * pB->fShoulder;
    float fBinderHx1 = 0.24f * pB->fRadius * pB->fShoulder;
    /* Seated so its inner face overlaps the shoulder rather than meeting it
     * exactly, because two faces in one plane have nothing to sort them
     * with. [MESH-18] */
    float fBinderX = fChestW + fBinderHx0 - 0.12f * pB->fRadius;
    float fArmX = fChestW + 0.14f * pB->fRadius;
    int iProfile = pB->iProfile < MECHA_PROFILE_COUNT ? pB->iProfile
                                                      : MECHA_PROFILE_STANDARD;

    mecha_mech_aim(pWorld, iMechIdx, &iAimYaw, &iAimPitch);
    iArmYaw = mecha_clampi(mecha_angle_delta(pB->pMech->iFacing, iAimYaw),
                           -MECHA_ARM_YAW_LIMIT, MECHA_ARM_YAW_LIMIT);
    iAimPitch = mecha_clampi(iAimPitch, -MECHA_ARM_PITCH_LIMIT,
                             MECHA_ARM_PITCH_LIMIT);

    for (iSide = 0; iSide < 2; iSide++) {
      float fSide = iSide == 0 ? -1.0f : 1.0f;
      int iSlot = iSide == 0 ? MECHA_SLOT_LEFT : MECHA_SLOT_RIGHT;
      int iKick = 0;
      tMechaPose shoulder;
      tMechaPose upper;
      tMechaPose fore;
      tMechaPose hand;
      int iShoulderYaw;
      int iShoulderRoll;
      int iUpperPitch;
      int iElbowPitch;

      if (pB->pMech->iRecovery > 0 && pB->pMech->iLastFiredSlot == iSlot)
        iKick = MECHA_ARM_RECOIL
                * mecha_clampi(pB->pMech->iRecovery, 0, 6) / 6;

      /*
       * Shoulder binder, on the torso rather than on the arm: it is armour
       * bolted to the machine, not something the elbow swings. Its outer
       * face is cut back top and bottom, which is the shape that makes a
       * shoulder read as armour rather than as a crate -- and the shoulders
       * are the first thing anyone reads a machine by, so this is the one
       * frustum that has to be right at every tier.
       */
      mecha_add_frustum(pList, pTorso, fSide * fBinderX,
                        0.29f * pB->fUpperY, 0.0f,
                        fBinderHx0,
                        0.40f * pB->fRadius * pB->fShoulder,
                        fBinderHx1,
                        0.30f * pB->fRadius * pB->fShoulder,
                        0.09f * pB->fUpperY * pB->fShoulder,
                        fSide * 0.0229f * pB->fRadius * pB->fShoulder, 0.0f,
                        pB->byTrim, pB->byTrim, 0);
      /* A lip along the top of it, in the joint colour, so the binder has
       * an edge instead of fading into the shoulder. */
      if (pB->iDetail >= MECHA_DETAIL_FULL) {
        /* Carried out to the binder's outer edge and raked the other way,
         * off the reference mesh: it reads as a cap on the end of the
         * binder rather than a band across the middle of it. Same size,
         * moved and re-skewed. [MESH-55] */
        mecha_add_frustum(pList, pTorso,
                          fSide * (fBinderX + 0.0882f * pB->fRadius),
                          0.38f * pB->fUpperY * pB->fShoulder, 0.0f,
                          fBinderHx1,
                          0.30f * pB->fRadius * pB->fShoulder,
                          0.80f * fBinderHx1,
                          0.24f * pB->fRadius * pB->fShoulder,
                          0.018f * pB->fUpperY,
                          fSide * -0.02f * pB->fRadius, 0.0f,
                          pB->byJoint, pB->byJoint, 0);
      }

      /*
       * A machine with nothing locked lets the whole chain unfold and
       * points its guns at the floor [MESH-20]; one holding a pose is taken
       * off the aim entirely and put where the pose says [MESH-41].
       *
       * The two shoulder pitches the aim used to be split across are
       * collapsed into one here, which is the same rotation -- a yaw and
       * then two pitches about the same axis compose as a yaw and their
       * sum -- and is what makes the aimed arm and the posed arm two values
       * of one number rather than two different chains.
       */
      {
        const tMechaArmPose *pPose = &s_aaWinPose[iProfile][iSide];
        int iAimUpper = (int)((float)(-iAimPitch + iKick) * fReady)
                        - (int)((float)MECHA_ARM_DROOP * fReady);
        int iAimElbow = (int)(-(float)(MECHA_ANGLE_QUARTER - MECHA_ARM_DROOP)
                                  * fReady
                              - (float)MECHA_ARM_REST_ELBOW * (1.0f - fReady));

        /*
         * An arm swings against the leg on its own side, which is why the
         * half-turn offset is on iSide and not on the phase: the left arm
         * goes forward as the left leg goes back. The elbow follows at
         * about half, because an arm swinging from a locked shoulder is a
         * pendulum and not an arm. [MESH-43]
         */
        if (pB->fWalkPhase >= 0.0f) {
          int iSwingAngle = (int)((pB->fWalkPhase
                                   + (iSide == 0 ? 0.5f : 0.0f))
                                  * (float)MECHA_ANGLE_FULL)
                            & (MECHA_ANGLE_FULL - 1);
          float fSwing = mecha_sin(iSwingAngle);
          float fHow = MECHA_WALK_AIM_KEEP
                       + (1.0f - MECHA_WALK_AIM_KEEP) * (1.0f - fReady);

          iAimUpper -= (int)((float)MECHA_WALK_ARM_SWING * fSwing * fHow);
          iAimElbow -= (int)((float)MECHA_WALK_ARM_ELBOW
                             * (fSwing > 0.0f ? fSwing : 0.0f) * fHow);
        }

        iShoulderYaw = mecha_blend_angle((int)((float)iArmYaw * fReady),
                                         pPose->iYaw, fPosed);
        iShoulderRoll = mecha_blend_angle(0, pPose->iRoll, fPosed);
        iUpperPitch = mecha_blend_angle(iAimUpper, pPose->iUpper, fPosed);
        iElbowPitch = mecha_blend_angle(iAimElbow, pPose->iElbow, fPosed);
      }

      mecha_pose_child(&shoulder, pTorso, fSide * fArmX,
                       0.27f * pB->fUpperY,
                       0.0f, iShoulderYaw, 0, iShoulderRoll);
      mecha_pose_name(&shoulder, MECHA_BONE_SHOULDER_L + iSide, pB->paBones);
      mecha_pose_child(&upper, &shoulder, 0.0f, 0.0f, 0.0f, 0,
                       iUpperPitch, 0);
      mecha_pose_name(&upper, MECHA_BONE_UPPERARM_L + iSide, pB->paBones);
      mecha_add_frustum(pList, &upper, 0.0f, -0.5f * fUpper, 0.0f,
                        0.13f * pB->fRadius * pB->fLimb * pB->fTaper,
                        0.13f * pB->fRadius * pB->fLimb * pB->fTaper,
                        0.17f * pB->fRadius * pB->fLimb, 0.17f * pB->fRadius * pB->fLimb,
                        0.5f * fUpper, 0.0f, 0.0f, pB->byBody, pB->byBody, 0);
      /* Proud of both the upper arm and the forearm, for the reason the
       * knee is. */
      mecha_add_box(pList, &upper, 0.0f, -fUpper, 0.0f,
                    0.19f * pB->fRadius * pB->fLimb, 0.04f * pB->fUpperY,
                    0.20f * pB->fRadius * pB->fLimb, pB->byJoint, pB->byJoint, 0);

      /* The elbow makes up the rest of the right angle, so the forearm and
       * the gun on the end of it come out level along the line of aim --
       * and gives all but a bend of it back when the arm comes down. */
      mecha_pose_child(&fore, &upper, 0.0f, -fUpper, 0.0f, 0,
                       iElbowPitch, 0);
      mecha_pose_name(&fore, MECHA_BONE_FOREARM_L + iSide, pB->paBones);
      /* The forearm flares towards the wrist -- it is the piece carrying
       * the gun, and a limb that narrows all the way down has nothing to
       * carry one with. */
      mecha_add_frustum(pList, &fore, 0.0f, -0.5f * fFore, 0.0f,
                        0.17f * pB->fRadius * pB->fLimb, 0.17f * pB->fRadius * pB->fLimb,
                        0.12f * pB->fRadius * pB->fLimb * pB->fTaper,
                        0.12f * pB->fRadius * pB->fLimb * pB->fTaper,
                        0.5f * fFore, 0.0f, 0.0f, pB->byTrim, pB->byTrim, 0);

      /*
       * The wrist, and the difference between carrying a weapon and having
       * one bolted on.
       *
       * The gun used to be a frustum hung off the end of the forearm, dead
       * coaxial with it, with nothing between the two. That reads as a limb
       * that ends in a gun -- which is a fine thing for a mech to be, and
       * not what this roster is. What makes it read as held is three
       * things, none of them the gun itself: a fist at the wrist, the gun
       * sitting forward of that fist rather than through it, and a grip
       * bridging the two. The break at the wrist is the fourth, and it is
       * what stops the whole arm reading as one straight piece. [MESH-54]
       */
      mecha_pose_child(&hand, &fore, 0.0f, -fFore, 0.0f, 0,
                       MECHA_WRIST_BREAK, 0);
      mecha_pose_name(&hand, MECHA_BONE_HAND_L + iSide, pB->paBones);
      mecha_add_box(pList, &hand, 0.0f, -0.035f * pB->fUpperY, 0.0f,
                    0.13f * pB->fRadius * pB->fLimb, 0.035f * pB->fUpperY,
                    0.12f * pB->fRadius * pB->fLimb,
                    pB->byJoint, pB->byJoint, 0);

      mecha_quads_part(pList, MECHA_PART_GUN);
      /*
       * A carrier carries no main gun: what it puts in the air is its
       * weapon, and a hand cannon on top of that would say the wrong thing
       * about how it fights. It gets a manipulator instead. [MESH-36]
       */
      if (pB->iProfile == MECHA_PROFILE_CARRIER) {
        mecha_add_frustum(pList, &hand, 0.0f, -0.08f * pB->fUpperY, 0.0f,
                          0.11f * pB->fRadius * pB->fGun, 0.12f * pB->fRadius * pB->fGun,
                          0.08f * pB->fRadius * pB->fGun, 0.09f * pB->fRadius * pB->fGun,
                          0.04f * pB->fUpperY, 0.0f, 0.0f, pB->byJoint, pB->byJoint, 0);
      } else {
        /*
         * How far in front of the fist the gun sits. This wants to be small.
         * At a tenth of a radius the weapon reads as floating beside the
         * machine with nothing holding it -- the eye needs the fist and the
         * gun to overlap, and the grip to be the thing between them, not a
         * gap.
         */
        float fHold = 0.06f * pB->fRadius * pB->fGun;

        /* The grip, out of the fist and into the gun. It has to be wide
         * enough to see, because it is the whole argument that the gun is
         * held: a grip you cannot make out is a gun that is still bolted
         * on, just further forward. */
        if (pB->iDetail >= MECHA_DETAIL_MID) {
          /* Its top sits inside the fist rather than flush with the top
           * of it. Flush put the two faces in one plane with nothing to
           * sort them by, which is the one thing the painter's order
           * cannot do [MESH-18] -- and it did it on every machine on the
           * roster at once, two pairs each. */
          mecha_add_frustum(pList, &hand, 0.0f, -0.065f * pB->fUpperY,
                            0.5f * fHold,
                            0.10f * pB->fRadius * pB->fGun,
                            0.11f * pB->fRadius * pB->fGun,
                            0.09f * pB->fRadius * pB->fGun,
                            0.10f * pB->fRadius * pB->fGun,
                            0.05f * pB->fUpperY, 0.0f,
                            0.5f * fHold, pB->byJoint, pB->byJoint, 0);
        }
        mecha_add_frustum(pList, &hand, 0.0f,
                          -0.14f * pB->fUpperY * pB->fGun, fHold,
                          0.20f * pB->fRadius * pB->fGun, 0.30f * pB->fRadius * pB->fGun,
                          0.22f * pB->fRadius * pB->fGun, 0.24f * pB->fRadius * pB->fGun,
                          0.13f * pB->fUpperY * pB->fGun, 0.0f,
                          -0.03f * pB->fRadius * pB->fGun, pB->byBody, pB->byBody, 0);
        /* A muzzle off the end of it, so a gun has a direction. */
        if (pB->iDetail >= MECHA_DETAIL_MID) {
          mecha_add_frustum(pList, &hand, 0.0f,
                            -0.27f * pB->fUpperY * pB->fGun,
                            fHold + 0.16f * pB->fRadius * pB->fGun,
                            0.09f * pB->fRadius * pB->fGun, 0.14f * pB->fRadius * pB->fGun,
                            0.07f * pB->fRadius * pB->fGun, 0.12f * pB->fRadius * pB->fGun,
                            0.05f * pB->fUpperY * pB->fGun, 0.0f, 0.0f,
                            pB->byJoint, pB->byJoint, 0);
        }
      }
      mecha_quads_part(pList, MECHA_PART_ARM);
    }

    /* --- head ---------------------------------------------------------
     *
     * Looks at whoever is being tracked, within the limits of a neck, and
     * independently of both the legs it stands on and the shoulders it sits
     * between: the machine watches you even while it walks somewhere else.
     */
    {
      tMechaPose head;
      int iHeadYaw = mecha_clampi(iArmYaw, -MECHA_HEAD_YAW_LIMIT,
                                  MECHA_HEAD_YAW_LIMIT);
      /* The head goes on watching after the guns have come down -- it is the
       * arms that say whether the machine means it, not the eyes. */
      int iHeadPitch = mecha_clampi(iAimPitch, -MECHA_HEAD_PITCH_LIMIT,
                                    MECHA_HEAD_PITCH_LIMIT);

      mecha_quads_part(pList, MECHA_PART_HEAD);
      /*
       * A neck, so the head sits on the shoulders rather than in them. It
       * is drawn on the torso and not on the head, because a neck does not
       * turn with what it carries. [MESH-44]
       */
      if (pB->iDetail >= MECHA_DETAIL_MID) {
        mecha_add_hull(pList, pTorso, 0.0f, 0.33f * pB->fUpperY, 0.0f,
                       0.132f * pB->fRadius * pB->fHead,
                       0.15f * pB->fRadius * pB->fHead,
                       0.1144f * pB->fRadius * pB->fHead,
                       0.13f * pB->fRadius * pB->fHead,
                       0.03f * pB->fUpperY, 0.0f, 0.0f, 0.0f,
                       0.779f, 0.779f, pB->byJoint, pB->byJoint, 0);
      }
      mecha_pose_child(&head, pTorso, 0.0f, 0.38f * pB->fUpperY, 0.0f,
                       iHeadYaw, -iHeadPitch, 0);
      mecha_pose_name(&head, MECHA_BONE_HEAD, pB->paBones);
      /*
       * The skull. It narrows towards the crown and juts at the jaw, and
       * the jaw now draws to a chin: the underside is cut away to under
       * half its width by the time it reaches the front, while the crown
       * above it widens the other way. Two tapers pulling opposite ways
       * along the same axis is the whole difference between a face and a
       * cardboard box, and the head is nodded forward a few degrees so it
       * is looking at you rather than past you. [MESH-55]
       */
      {
        tMechaPose skull;

        mecha_pose_child(&skull, &head, 0.0f, 0.0f, 0.0f, 0,
                         MECHA_SKULL_NOD, 0);
        mecha_add_hull(pList, &skull, 0.0f, 0.0202f * pB->fUpperY,
                       0.0268f * pB->fRadius,
                       0.260f * pB->fRadius * pB->fHead,
                       0.2866f * pB->fRadius * pB->fHead,
                       0.159f * pB->fRadius * pB->fHead,
                       0.2219f * pB->fRadius * pB->fHead,
                       0.0515f * pB->fUpperY * pB->fHead, 0.0f,
                       -0.0244f * pB->fRadius * pB->fHead,
                       0.0359f, 0.451f, 1.226f,
                       pB->byTrim, pB->byTrim, 0);
      }
      /*
       * The visor. Drawn in a frame stood on its nose, so the frustum's own
       * bottom-to-top taper runs fore and aft instead of up and down and
       * the glass narrows to a beak the way a helmet does. A box could not:
       * its taper only ever runs up. [MESH-55]
       */
      {
        tMechaPose visor;

        mecha_pose_child(&visor, &head, 0.0f, 0.0f, 0.0f, 0,
                         MECHA_DEG(90), 0);
        mecha_add_raked(pList, &visor, 0.0f,
                        0.290f * pB->fRadius * pB->fHead,
                        -0.0160f * pB->fUpperY,
                        0.289f * pB->fRadius * pB->fHead,
                        0.0188f * pB->fUpperY,
                        0.200f * pB->fRadius * pB->fHead,
                        0.0100f * pB->fUpperY,
                        0.0893f * pB->fRadius * pB->fHead,
                        0.0f, -0.00826f * pB->fUpperY, 0.0253f,
                        pB->byGlow, pB->byGlow, MECHA_QUAD_GLOW);
      }

      /*
       * The crest. Two blades off the brow, swept up and out -- and on a
       * slender frame they run backwards and much longer instead, which is
       * the one silhouette cue that survives being a dozen pixels tall.
       * It is the reason the head is worth drawing at range at all, so it
       * is built at every tier. The tips keep a little width: a blade taken
       * to a true point collapses two corners of its own end face and the
       * quad stops being cullable. [MESH-35]
       */
      for (iSide = 0; iSide < 2; iSide++) {
        float fSide = iSide == 0 ? -1.0f : 1.0f;

        if (pB->iProfile == MECHA_PROFILE_SLENDER) {
          /*
           * Swept back about as far again as the head is deep. It was at
           * two and a half times that, which from the side is not a crest,
           * it is a pair of banners the machine is towing. [MESH-35]
           */
          tMechaPose crest;

          /* Swept back in a frame of its own, so the blade lies along the
           * skull instead of standing off it, and raked on top of that so
           * its two long edges are not parallel -- the fin is deepest at
           * the brow and tapers out to the tip. One rotation cannot do
           * that; the sweep turns the whole blade and the rake shears the
           * outer edge alone. [MESH-55] */
          mecha_pose_child(&crest, &head, 0.0f, 0.0f, 0.0f, 0,
                           MECHA_CREST_SWEEP, 0);
          mecha_add_raked(pList, &crest,
                          fSide * 0.152f * pB->fRadius * pB->fHead,
                          pB->fCrestLift * pB->fUpperY,
                          -0.206f * pB->fRadius * pB->fHead,
                          0.0624f * pB->fRadius * pB->fHead,
                          0.487f * pB->fRadius * pB->fHead,
                          0.0268f * pB->fRadius * pB->fHead,
                          0.440f * pB->fRadius * pB->fHead,
                          pB->fCrestHeight * pB->fUpperY * pB->fHead,
                          fSide * 0.0981f * pB->fRadius * pB->fHead,
                          -0.286f * pB->fRadius * pB->fHead, -0.182f,
                          pB->byGlow, pB->byGlow, 0);
        } else {
          mecha_add_frustum(pList, &head,
                            fSide * 0.14f * pB->fRadius * pB->fHead,
                            0.07f * pB->fUpperY, 0.10f * pB->fRadius * pB->fHead,
                            0.09f * pB->fRadius * pB->fHead,
                            0.13f * pB->fRadius * pB->fHead,
                            0.04f * pB->fRadius * pB->fHead,
                            0.05f * pB->fRadius * pB->fHead,
                            0.05f * pB->fUpperY * pB->fHead,
                            fSide * 0.16f * pB->fRadius * pB->fHead,
                            0.06f * pB->fRadius * pB->fHead, pB->byGlow, pB->byGlow, 0);
        }
      }

      /* Vents down the cheeks. Trim, and it goes first. */
      if (pB->iDetail >= MECHA_DETAIL_FULL) {
        for (iSide = 0; iSide < 2; iSide++) {
          float fSide = iSide == 0 ? -1.0f : 1.0f;

          mecha_add_box(pList, &head, fSide * 0.25f * pB->fRadius * pB->fHead,
                        0.01f * pB->fUpperY, 0.10f * pB->fRadius * pB->fHead,
                        0.04f * pB->fRadius * pB->fHead, 0.025f * pB->fUpperY,
                        0.10f * pB->fRadius * pB->fHead, pB->byJoint, pB->byJoint, 0);
        }
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
/* The plume, whenever the machine is actually spending gauge. */
static void mecha_build_plume(tMechaQuadList *pList,
                              const tMechaWorld *pWorld,
                              const tMechaBuild *pB,
                              const tMechaPose *pTorso)
{
  mecha_quads_part(pList, MECHA_PART_THRUST);

  /* Thruster plume, whenever the mech is actually spending gauge. */
  if (pB->pMech->byMove == MECHA_MOVE_DASH
      || (pB->pMech->byMove == MECHA_MOVE_JUMP && pB->pMech->fVelY > 0.0f)) {
    float fPlume = (0.35f + 0.12f * mecha_sin(pWorld->iTick * 2100))
                   * pB->fRadius;

    mecha_add_box(pList, pTorso, 0.0f, 0.15f * pB->fUpperY,
                  -0.74f * pB->fRadius - fPlume,
                  0.30f * pB->fRadius, 0.09f * pB->fUpperY, fPlume,
                  pB->byGlow, pB->byGlow, MECHA_QUAD_GLOW);
  }
}

//-------------------------------------------------------------------------------------------------
/*
 * The root frame every chassis stands in: the machine's position, turned to
 * whichever way its running gear is pointed, carrying the fall and the lean
 * the stick answers with. Pulled out because all three chassis want the same
 * thing and only the biped used to. [MESH-37]
 */
static void mecha_build_root(tMechaPose *pPose, const tMechaMech *pMech,
                             const tMechaMechDef *pDef, float fLift,
                             int iLegYaw)
{
  int iPoseYaw = iLegYaw;
  int iPosePitch = pDef->byChassis == MECHA_CHASSIS_CAR
                     ? mecha_mesh_fall_pitch(pMech) : 0;
  int iPoseRoll = mecha_mesh_fall_roll(pMech);
  float fLateral;

  fLateral = (pMech->fVelX * mecha_cos(pMech->iFacing)
              - pMech->fVelZ * mecha_sin(pMech->iFacing));
  if (pDef->fDashSpeed > 1.0f)
    fLateral /= pDef->fDashSpeed;
  /* Negated, because positive roll lifts the right side. [MESH-15] */
  iPoseRoll += -(int)(pMech->fLeanRoll * mecha_clampf(fLateral, -1.0f, 1.0f));

  mecha_mesh_attitude(pMech, &iPoseYaw, &iPosePitch, &iPoseRoll);
  mecha_pose_build(pPose, iPoseYaw, iPosePitch, iPoseRoll,
                   pMech->fX, pMech->fY + fLift, pMech->fZ, 1.0f);
}

//-------------------------------------------------------------------------------------------------
/*
 * A tank for legs. Two track units with a hull slung between them, and the
 * shared torso sitting on that hull exactly where a biped's sits on its
 * hips -- so a machine built this way reads as the same roster's work from
 * the waist up and as something else entirely from the waist down, which is
 * the whole of what it is for. [MESH-38]
 *
 * What sells it moving is the road wheels. A track drawn as one long box
 * slides across the ground with nothing turning, and the eye reads that as
 * a building on castors; wheels that turn at the speed the ground is going
 * past cost six quads each and fix it.
 */
static void mecha_mesh_tread(tMechaQuadList *pList, const tMechaWorld *pWorld,
                             int iMechIdx, int iDetail)
{
  const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_def_get((int)pMech->byDefIdx);
  tMechaBuild build;
  tMechaPose pose;
  tMechaPose torso;
  float fHeight = pDef->fHeight;
  float fRadius = pDef->fRadius;
  float fTrackLen;
  float fTrackTop;
  int iSpin;
  int iSide;

  mecha_build_setup(&build, pMech, pDef, iDetail);
  mecha_build_root(&pose, pMech, pDef, 0.0f, pMech->iLegYaw);

  fTrackLen = 0.52f * fHeight;
  fTrackTop = 0.34f * fHeight;
  /* The wheels turn on ground covered, the same counter the walk cycle is
   * paced by, so a tracked machine and a walking one agree about how fast
   * the world is going past. [MESH-04] */
  iSpin = (int)(pMech->fStepPhase * (float)MECHA_ANGLE_FULL);

  mecha_quads_part(pList, MECHA_PART_LEG);
  for (iSide = 0; iSide < 2; iSide++) {
    float fSide = iSide == 0 ? -1.0f : 1.0f;
    float fTrackX = fSide * 0.74f * fRadius;
    int iWheel;
    int iWheels;

    /*
     * The track unit: deeper at the back than the front, so it has an
     * approach angle rather than being a brick. Its top face is the run the
     * return rollers would carry.
     */
    mecha_add_frustum(pList, &pose, fTrackX, 0.5f * fTrackTop, 0.0f,
                      0.30f * fRadius, fTrackLen,
                      0.26f * fRadius, 0.82f * fTrackLen,
                      0.5f * fTrackTop, 0.0f, 0.04f * fTrackLen,
                      build.byBody, build.byTrim, 0);

    /* Drive sprocket aft and idler forward, both standing proud of the
     * track so the unit has ends rather than corners. */
    {
      int iEnd;

      for (iEnd = 0; iEnd < 2; iEnd++) {
        float fZ = iEnd == 0 ? -0.86f * fTrackLen : 0.86f * fTrackLen;
        tMechaPose hub;

        mecha_pose_child(&hub, &pose, fTrackX, 0.40f * fTrackTop, fZ,
                         0, iEnd == 0 ? iSpin : -iSpin, 0);
        mecha_add_box(pList, &hub, 0.0f, 0.0f, 0.0f,
                      0.33f * fRadius, 0.30f * fTrackTop,
                      0.30f * fTrackTop, build.byJoint, build.byJoint, 0);
      }
    }

    /* Road wheels along the bottom run. */
    iWheels = iDetail >= MECHA_DETAIL_FULL ? 4
            : iDetail >= MECHA_DETAIL_MID  ? 2 : 0;
    for (iWheel = 0; iWheel < iWheels; iWheel++) {
      float fT = ((float)iWheel + 0.5f) / (float)iWheels;
      float fZ = (fT * 2.0f - 1.0f) * 0.62f * fTrackLen;
      tMechaPose hub;

      mecha_pose_child(&hub, &pose, fTrackX, 0.26f * fTrackTop, fZ,
                       0, iSpin, 0);
      mecha_add_box(pList, &hub, 0.0f, 0.0f, 0.0f,
                    0.34f * fRadius, 0.22f * fTrackTop, 0.22f * fTrackTop,
                    build.byTrim, build.byTrim, 0);
    }

    /* A fender over the top of the unit, which is what stops the track
     * reading as a bare wheel set. */
    if (iDetail >= MECHA_DETAIL_MID) {
      mecha_add_frustum(pList, &pose, fTrackX, fTrackTop + 0.02f * fHeight,
                        0.0f, 0.36f * fRadius, 0.92f * fTrackLen,
                        0.34f * fRadius, 0.88f * fTrackLen,
                        0.02f * fHeight, 0.0f, 0.0f,
                        build.byJoint, build.byJoint, 0);
    }
  }

  /*
   * The hull between the tracks, with a glacis sloping up to the ring the
   * torso turns on.
   */
  mecha_quads_part(pList, MECHA_PART_TORSO);
  mecha_add_frustum(pList, &pose, 0.0f, 0.38f * fHeight, -0.04f * fRadius,
                    0.62f * fRadius, 0.92f * fTrackLen,
                    0.56f * fRadius, 0.76f * fTrackLen,
                    0.09f * fHeight, 0.0f, 0.0f,
                    build.byBody, build.byTrim, 0);
  if (iDetail >= MECHA_DETAIL_MID) {
    mecha_add_frustum(pList, &pose, 0.0f, 0.45f * fHeight, 0.0f,
                      0.42f * fRadius, 0.42f * fRadius,
                      0.38f * fRadius, 0.38f * fRadius,
                      0.02f * fHeight, 0.0f, 0.0f,
                      build.byJoint, build.byJoint, 0);
  }

  /*
   * And the machine from the waist up, turned on the ring rather than at a
   * waist. It carries no skirt: there are no legs for a skirt to be over.
   */
  mecha_pose_child(&torso, &pose, 0.0f, build.fHipY, 0.0f,
                   mecha_angle_delta(pMech->iLegYaw, pMech->iFacing),
                   mecha_mesh_lean_pitch(pMech), 0);
  mecha_build_torso(pList, &build, &torso);
  mecha_build_arms_head(pList, pWorld, iMechIdx, &build, &torso);

  /*
   * Shoulder artillery. This is a gunner, and what says so from across an
   * arena is a pair of barrels standing above the shoulders rather than
   * whatever is in its hands.
   */
  mecha_quads_part(pList, MECHA_PART_GUN);
  for (iSide = 0; iSide < 2; iSide++) {
    float fSide = iSide == 0 ? -1.0f : 1.0f;
    int iAimYaw;
    int iAimPitch;
    tMechaPose mount;

    mecha_mech_aim(pWorld, iMechIdx, &iAimYaw, &iAimPitch);
    iAimPitch = mecha_clampi(iAimPitch, -MECHA_ARM_PITCH_LIMIT,
                             MECHA_ARM_PITCH_LIMIT);
    /*
     * Sat on top of the shoulder and no longer than the machine is wide.
     * Written first at twice this, which from the side read as a pair of
     * planks the machine was carrying rather than as guns it was aiming.
     */
    mecha_pose_child(&mount, &torso, fSide * 0.98f * fRadius * build.fShoulder,
                     0.36f * fHeight, 0.0f, 0, -iAimPitch, 0);
    mecha_add_frustum(pList, &mount, 0.0f, 0.0f, 0.13f * fHeight,
                      0.20f * fRadius * build.fGun,
                      0.17f * fHeight * build.fGun,
                      0.16f * fRadius * build.fGun,
                      0.17f * fHeight * build.fGun,
                      0.13f * fRadius * build.fGun, 0.0f, 0.0f,
                      build.byBody, build.byTrim, 0);
    /* The barrel proper, thinner than the breech it comes out of. */
    mecha_add_frustum(pList, &mount, 0.0f, 0.0f, 0.40f * fHeight,
                      0.10f * fRadius * build.fGun,
                      0.10f * fHeight * build.fGun,
                      0.08f * fRadius * build.fGun,
                      0.10f * fHeight * build.fGun,
                      0.08f * fRadius * build.fGun, 0.0f, 0.0f,
                      build.byJoint, build.byJoint, 0);
    if (iDetail >= MECHA_DETAIL_MID) {
      mecha_add_frustum(pList, &mount, 0.0f, 0.0f, 0.52f * fHeight,
                        0.08f * fRadius * build.fGun, 0.03f * fHeight,
                        0.12f * fRadius * build.fGun, 0.025f * fHeight,
                        0.11f * fRadius * build.fGun, 0.0f, 0.0f,
                        build.byTrim, build.byTrim, 0);
    }
  }
}

//-------------------------------------------------------------------------------------------------
/*
 * Six legs off a low body. Each is a coxa out to the side, a femur up and
 * out to a knee well above the body, and a tibia back down to the ground --
 * which is the arrangement that makes an arachnid read as one: the joints
 * are higher than the thing they carry. A machine whose knees are below its
 * belly is a table. [MESH-39]
 *
 * The gait is the alternating tripod every six-legged thing walks: legs 0,
 * 2 and 4 swing while 1, 3 and 5 are planted, then the other way about.
 */
#define MECHA_ARACHNID_LEGS 6

static void mecha_mesh_arachnid(tMechaQuadList *pList,
                                const tMechaWorld *pWorld,
                                int iMechIdx, int iDetail)
{
  const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_def_get((int)pMech->byDefIdx);
  tMechaBuild build;
  tMechaPose pose;
  tMechaPose torso;
  float fHeight = pDef->fHeight;
  float fRadius = pDef->fRadius;
  float fBodyY;
  float fCoxa;
  float fFemur;
  float fTibia;
  float fPhase;
  int iLeg;

  mecha_build_setup(&build, pMech, pDef, iDetail);
  mecha_build_root(&pose, pMech, pDef, 0.0f, pMech->iLegYaw);

  /*
   * The body rides low and the legs arch over it. The tibia is the long
   * bone here -- it has to reach the floor from a knee standing above the
   * hull, which is further than anything on a biped reaches.
   */
  fBodyY = 0.42f * fHeight;
  fCoxa  = 0.30f * fRadius;
  fFemur = 0.30f * fHeight;
  fTibia = 0.80f * fHeight;
  fPhase = pMech->fStepPhase - (float)(int)pMech->fStepPhase;

  mecha_quads_part(pList, MECHA_PART_LEG);
  for (iLeg = 0; iLeg < MECHA_ARACHNID_LEGS; iLeg++) {
    int iSide = (iLeg & 1) ? 1 : 0;
    float fSide = iSide == 0 ? -1.0f : 1.0f;
    int iRank = iLeg / 2;                  /* fore, middle, aft */
    /* Splayed fore and aft so the six do not stand in one line, and the
     * middle pair reach furthest out. */
    static const int aiFan[3] = { MECHA_DEG(52), MECHA_DEG(0),
                                  -MECHA_DEG(52) };
    static const float afOut[3] = { 0.78f, 1.05f, 0.78f };
    /* How far up and down the body each rank sits. Written at 0.55 of the
     * radius, where the three pairs stood close enough together that from
     * the front the machine had four legs and a smudge. */
    static const float afAlong[3] = { 0.95f, 0.0f, -0.95f };
    /* The alternating tripod: one triple is down while the other swings. */
    float fLegPhase = fPhase + (((iLeg & 1) == (iRank & 1)) ? 0.0f : 0.5f);
    int iAngle = (int)(fLegPhase * (float)MECHA_ANGLE_FULL)
                 & (MECHA_ANGLE_FULL - 1);
    float fSwing = mecha_sin(iAngle);
    float fPlant = mecha_cos(iAngle);
    tMechaPose coxa;
    tMechaPose femur;
    tMechaPose tibia;
    float fLift;
    float fKneeY;
    float fDrop;
    /*
     * Out further than up. At sixty degrees the six legs stood under the
     * machine like table legs and the thing read as a walker with too many
     * of them; at forty-two they go out to the sides and the knees still
     * clear the body, which is what an arachnid is. [MESH-39]
     */
    int iFemurUp = MECHA_DEG(42);
    int iTibiaDown;

    /* A planted leg does not move; a swinging one picks its foot up. */
    fLift = fPlant > 0.0f ? fPlant * 0.16f * fHeight : 0.0f;

    /*
     * The knee angle is chosen and the ankle angle is worked out, rather
     * than both being picked and the foot landing wherever that leaves it.
     * A leg is only a leg if it reaches the floor, and reaching the floor
     * from a knee that stands above the body is most of a tibia's length --
     * so the one number that cannot be guessed is the one solved for here.
     * [MESH-39]
     */
    fKneeY = fBodyY + fFemur * mecha_sin(iFemurUp);
    fDrop = (fKneeY - fLift) / fTibia;
    iTibiaDown = (int)(asinf(mecha_clampf(fDrop, -1.0f, 1.0f))
                       * (float)MECHA_ANGLE_FULL / 6.28318531f);

    mecha_pose_child(&coxa, &pose,
                     fSide * afOut[iRank] * 0.45f * fRadius,
                     fBodyY,
                     afAlong[iRank] * fRadius,
                     (int)(fSide * (float)aiFan[iRank])
                       + (int)((float)MECHA_DEG(13) * fSwing),
                     0, 0);
    mecha_add_frustum(pList, &coxa, fSide * 0.5f * fCoxa, 0.0f, 0.0f,
                      0.5f * fCoxa, 0.16f * fRadius,
                      0.42f * fCoxa, 0.13f * fRadius,
                      0.14f * fRadius, 0.0f, 0.0f,
                      build.byJoint, build.byJoint, 0);

    /* Up and out to the knee, which stands well above the body. */
    mecha_pose_child(&femur, &coxa, fSide * fCoxa, 0.0f, 0.0f, 0,
                     0, (int)(fSide * (float)iFemurUp));
    mecha_add_frustum(pList, &femur, fSide * 0.5f * fFemur, 0.0f, 0.0f,
                      0.5f * fFemur, 0.19f * fRadius,
                      0.5f * fFemur, 0.15f * fRadius,
                      0.18f * fRadius, 0.0f, 0.0f,
                      build.byBody, build.byTrim, 0);
    if (iDetail >= MECHA_DETAIL_MID) {
      mecha_add_box(pList, &femur, fSide * fFemur, 0.0f, 0.0f,
                    0.14f * fRadius, 0.15f * fRadius, 0.15f * fRadius,
                    build.byJoint, build.byJoint, 0);
    }

    /*
     * And back down to the floor. The fold is against the femur's own
     * turn, not with it: both the same sign and the leg comes back up over
     * the body instead of reaching the ground.
     */
    mecha_pose_child(&tibia, &femur, fSide * fFemur, 0.0f, 0.0f, 0, 0,
                     (int)(-fSide * (float)(iFemurUp + iTibiaDown)));
    mecha_add_frustum(pList, &tibia, fSide * 0.5f * fTibia, 0.0f, 0.0f,
                      0.5f * fTibia, 0.15f * fRadius,
                      0.5f * fTibia, 0.07f * fRadius,
                      0.14f * fRadius, 0.0f, 0.0f,
                      build.byTrim, build.byTrim, 0);
    /* A claw on the end, so the leg finishes on something rather than
     * tapering away into the floor. */
    if (iDetail >= MECHA_DETAIL_MID) {
      mecha_add_frustum(pList, &tibia, fSide * fTibia, 0.0f, 0.0f,
                        0.09f * fRadius, 0.09f * fRadius,
                        0.04f * fRadius, 0.04f * fRadius,
                        0.07f * fRadius, 0.0f, 0.0f,
                        build.byJoint, build.byJoint, 0);
    }
  }

  /*
   * The hub the legs hang off, and it is small on purpose: this machine is
   * its legs, and a hull big enough to look like a body turns six limbs into
   * a fringe round a box. What sits above it is the roster's own torso --
   * an ordinary robot from the waist up, walking on something that is not
   * legs. [MESH-39]
   */
  mecha_quads_part(pList, MECHA_PART_TORSO);
  mecha_add_frustum(pList, &pose, 0.0f, fBodyY, 0.0f,
                    0.54f * fRadius, 0.66f * fRadius,
                    0.46f * fRadius, 0.54f * fRadius,
                    0.05f * fHeight, 0.0f, 0.0f,
                    build.byJoint, build.byJoint, 0);
  /* Underside plate, so the hub is not an open shell from below -- which is
   * a view this machine actually gives, being the one that climbs. */
  mecha_add_frustum(pList, &pose, 0.0f, fBodyY - 0.05f * fHeight, 0.0f,
                    0.38f * fRadius, 0.44f * fRadius,
                    0.52f * fRadius, 0.62f * fRadius,
                    0.015f * fHeight, 0.0f, 0.0f,
                    build.byJoint, build.byJoint, 0);

  /*
   * And the machine from the waist up, turned on the hub. The shared builder
   * measures from its own origin, so placing that origin is the whole of how
   * a different body wears the same shoulders.
   */
  build.fWalkPhase = fPhase;
  mecha_pose_child(&torso, &pose, 0.0f, fBodyY + 0.03f * fHeight, 0.0f,
                   mecha_angle_delta(pMech->iLegYaw, pMech->iFacing), 0, 0);
  mecha_build_torso(pList, &build, &torso);
  mecha_build_arms_head(pList, pWorld, iMechIdx, &build, &torso);
  mecha_build_plume(pList, pWorld, &build, &torso);
}

//-------------------------------------------------------------------------------------------------

/*
 * Detail falls away with range, and the outline never does: a machine is
 * told apart across an arena by its shoulders, its skirt and its head, not
 * by the vents on its chest. The two thresholds were picked by rendering the
 * roster in a line and finding the range at which each tier stops being
 * visible. [MESH-32]
 */
int mecha_mesh_detail_for_range(float fRange)
{
  if (fRange < MECHA_M(55.0f))
    return MECHA_DETAIL_FULL;
  if (fRange < MECHA_M(130.0f))
    return MECHA_DETAIL_MID;
  return MECHA_DETAIL_FAR;
}

//-------------------------------------------------------------------------------------------------

void mecha_mesh_mech(tMechaQuadList *pList, const tMechaWorld *pWorld,
                     int iMechIdx, int iDetail)
{
  mecha_mesh_mech_rigged(pList, pWorld, iMechIdx, iDetail, NULL);
}

//-------------------------------------------------------------------------------------------------

void mecha_mesh_mech_rigged(tMechaQuadList *pList, const tMechaWorld *pWorld,
                            int iMechIdx, int iDetail,
                            tMechaBoneFrame *paBones)
{
  const tMechaMech *pMech;
  const tMechaMechDef *pDef;
  tMechaPose pose;
  uint8_t byBody;
  uint8_t byTrim;
  uint8_t byJoint;
  tMechaBuild build;
  float fHeight;
  float fRadius;
  float fLimb;
  float fTaper;
  float fWalkPhase = -1.0f;
  float fVertical;
  float fLateral;
  float fAnkle;
  float fLegSpan;
  float fThighLen;
  float fShinLen;
  float fLift = 0.0f;
  int aiThigh[2];
  int aiKnee[2];
  int aiRoll[2];
  int aiAnkle[2];
  int aiHipYaw[2] = { 0, 0 };
  float fPosed;
  int iSide;
  int iRoll;
  bool bAirborne;
  tMechaPose torso;
  tMechaPose pelvis;

  if (paBones)
    memset(paBones, 0, MECHA_BONE_COUNT * sizeof(*paBones));
  if (!pList || !pWorld || iMechIdx < 0 || iMechIdx >= MECHA_MAX_MECHS)
    return;
  pMech = &pWorld->aMechs[iMechIdx];
  if (!pMech->bActive)
    return;

  /* Invulnerability after a knockdown flickers the mech, the same way the
   * gauge tells you about boost: state the player has to read is state the
   * player can see. */
  if (pMech->iInvulnTicks > 0 && ((pWorld->iTick / 3) & 1))
    return;

  pDef = mecha_def_get((int)pMech->byDefIdx);
  switch (pDef->byChassis) {
  case MECHA_CHASSIS_CAR:
    /* No legs to walk, no arms to aim: everything below this line is a
     * skeleton the one machine on wheels does not have. */
    mecha_mesh_car(pList, pWorld, iMechIdx);
    return;
  case MECHA_CHASSIS_TREAD:
    mecha_mesh_tread(pList, pWorld, iMechIdx, iDetail);
    return;
  case MECHA_CHASSIS_ARACHNID:
    mecha_mesh_arachnid(pList, pWorld, iMechIdx, iDetail);
    return;
  default:
    break;
  }
  mecha_build_setup(&build, pMech, pDef, iDetail);
  build.paBones = paBones;
  fPosed = mecha_mech_pose_amount(pWorld, iMechIdx);
  byBody = build.byBody;
  byTrim = build.byTrim;
  byJoint = build.byJoint;
  fHeight = build.fHeight;
  fRadius = build.fRadius;
  fLimb = build.fLimb;
  fTaper = build.fTaper;
  bAirborne = pMech->fY > mecha_arena_ground_height(&pWorld->arena, pMech->fX,
                                                    pMech->fZ, pMech->fY)
                          + 0.05f * MECHA_METRE;

  /*
   * Lean into the direction of travel, scaled by how much of it is sideways;
   * fLeanRoll is smoothed by the simulation so this never snaps. Negated,
   * because positive roll lifts the right side. [MESH-15]
   */
  fLateral = (pMech->fVelX * mecha_cos(pMech->iFacing)
              - pMech->fVelZ * mecha_sin(pMech->iFacing));
  if (pDef->fDashSpeed > 1.0f)
    fLateral /= pDef->fDashSpeed;
  iRoll = -(int)(pMech->fLeanRoll * mecha_clampf(fLateral, -1.0f, 1.0f));

  /* Guard used to squash the whole machine down to two thirds. It bends its
   * knees now instead, which is the same read and an honest one. */
  fVertical = 1.0f;
  fAnkle = 0.07f * fHeight;

  /* --- the walk ---------------------------------------------------------
   *
   * Both legs run the same cycle half a stride apart, backwards when the
   * machine is backing up. The angles come first, then the body is dropped
   * onto whichever foot reaches lowest, so a crouch sinks and a stride bobs
   * without either being animated as such.
   */
  {
    float fPhase = pMech->fStepPhase - (float)(int)pMech->fStepPhase;
    bool bDash = pMech->byMove == MECHA_MOVE_DASH;
    int iGait;

    if (fPosed > 0.5f)
      iGait = MECHA_GAIT_VICTORY;
    else if (pMech->byMove == MECHA_MOVE_GUARD)
      iGait = MECHA_GAIT_GUARD;
    else if (bDash && bAirborne)
      iGait = MECHA_GAIT_AIRDASH;
    else if (bDash)
      iGait = MECHA_GAIT_SKATE;
    else if (bAirborne)
      iGait = MECHA_GAIT_AIR;
    else if (pMech->byMove == MECHA_MOVE_WALK)
      iGait = MECHA_GAIT_WALK;
    else
      iGait = MECHA_GAIT_STANCE;

    if (pMech->bLegsBackward)
      fPhase = 1.0f - fPhase;
    /* The glide is timed rather than paced by the ground it covers; see
     * MECHA_SKATE_TICKS. */
    if (iGait == MECHA_GAIT_SKATE)
      fPhase = (float)(pWorld->iTick % MECHA_SKATE_TICKS)
               / (float)MECHA_SKATE_TICKS;
    fLegSpan = build.fHipY - fAnkle;
    fThighLen = 0.52f * fLegSpan;
    fShinLen = fLegSpan - fThighLen;

    /* The cross, eased in with the rest of the pose. Blended here rather
     * than in the gait table because the gait table has no idea how far
     * into the pose the machine is. */
    {
      int iCross = build.iProfile == MECHA_PROFILE_SLENDER
                     ? MECHA_WIN_SLIM_CROSS : MECHA_WIN_CROSS;

      aiHipYaw[0] = mecha_blend_angle(0, -iCross, fPosed);
      aiHipYaw[1] = mecha_blend_angle(0, -iCross / 3, fPosed);
    }
    mecha_leg_angles(iGait, fPhase, 0, pWorld->iTick, pMech->fCombat,
                     build.iProfile, &aiThigh[0], &aiKnee[0], &aiRoll[0]);
    mecha_leg_angles(iGait, fPhase + 0.5f, 1, pWorld->iTick, pMech->fCombat,
                     build.iProfile, &aiThigh[1], &aiKnee[1], &aiRoll[1]);
    /* The walk phase the torso sway reads, kept where the gait decided it
     * so the two cannot drift apart. */
    fWalkPhase = (iGait == MECHA_GAIT_WALK) ? fPhase : -1.0f;
    /* Flat unless a stride says otherwise: a machine on both feet, or in
     * the air with nothing to push off, is not picking a foot up. */
    aiAnkle[0] = 0;
    aiAnkle[1] = 0;
    if (!bAirborne) {
      float afReach[2];

      for (iSide = 0; iSide < 2; iSide++)
        afReach[iSide] = mecha_leg_reach(aiThigh[iSide], aiKnee[iSide],
                                         fThighLen, fShinLen);

      if (mecha_gait_plants(iGait)) {
        float fFloor;
        float fOther;

        /*
         * Both feet down, and the difference between the two legs is taken
         * out in the knees before anything else. A stance puts one thigh
         * forward and one back with the same knee on both, which does not
         * reach the same distance -- and leaving that for the hips to
         * absorb splays the longer leg right out while the other stands
         * straight, which is not a stance, it is a machine with one leg
         * kicked sideways. It scales with leg length, so the frame with the
         * longest legs wore it worst. [MESH-45]
         */
        for (iSide = 0; iSide < 2; iSide++) {
          float fWantReach = afReach[0] < afReach[1] ? afReach[0]
                                                     : afReach[1];

          if (afReach[iSide] > fWantReach + 1e-3f) {
            aiKnee[iSide] = mecha_leg_knee_for_reach(aiThigh[iSide],
                                                     fWantReach, fThighLen,
                                                     fShinLen);
            afReach[iSide] = mecha_leg_reach(aiThigh[iSide], aiKnee[iSide],
                                             fThighLen, fShinLen);
          }
        }

        /* Whatever is left over the hips still answer for, which is what
         * keeps a machine standing on uneven angles upright. [MESH-16] */
        fFloor = afReach[0] * mecha_cos(aiRoll[0]);
        fOther = afReach[1] * mecha_cos(aiRoll[1]);

        if (fOther < fFloor)
          fFloor = fOther;
        for (iSide = 0; iSide < 2; iSide++) {
          float fWant = afReach[iSide] > 0.0f ? fFloor / afReach[iSide]
                                              : 1.0f;

          aiRoll[iSide] = (int)(acosf(mecha_clampf(fWant, -1.0f, 1.0f))
                                * (float)MECHA_ANGLE_FULL / 6.28318531f);
        }
        fLift = fFloor - fLegSpan;
      } else {
        /*
         * One foot down, which is a stride: the body sits so the leg that
         * reaches furthest is the one on the floor, and how far short the
         * other leg falls is exactly how high its foot is. That is the
         * number the ankle hangs on. [MESH-53]
         */
        float fFloorReach = afReach[0] > afReach[1] ? afReach[0]
                                                    : afReach[1];
        int iRange = build.iProfile == MECHA_PROFILE_SLENDER
                       ? MECHA_SLIM_ANKLE : MECHA_LEG_ANKLE;

        fLift = fFloorReach - fLegSpan;
        for (iSide = 0; iSide < 2; iSide++)
          aiAnkle[iSide] = mecha_leg_ankle(fFloorReach - afReach[iSide],
                                           MECHA_ANKLE_CLEAR * fLegSpan,
                                           iRange);
      }
    }
  }

  /*
   * The root pose is the legs, and the legs are not the machine: they point
   * along the line of travel while the shoulders hold the aim. Only a fall
   * pitches the whole thing over -- a dash lean belongs to the torso, which
   * is why the pitch is split in two.
   */
  {
    /* The whole machine, body and legs together: the tilt that answers the
     * stick belongs to the machine, not to its torso, which is the
     * difference between leaning and merely turning at the waist. */
    int iPoseYaw = pMech->iLegYaw;
    int iPosePitch = mecha_mesh_fall_pitch(pMech);
    int iPoseRoll = iRoll;

    mecha_mesh_attitude(pMech, &iPoseYaw, &iPosePitch, &iPoseRoll);
    mecha_pose_build(&pose, iPoseYaw, iPosePitch, iPoseRoll,
                     pMech->fX, pMech->fY + fLift, pMech->fZ, fVertical);
    pose.pMech = pMech;
    mecha_pose_name(&pose, MECHA_BONE_ROOT, build.paBones);
  }

  /* --- legs -------------------------------------------------------------- */
  mecha_quads_part(pList, MECHA_PART_LEG);
  for (iSide = 0; iSide < 2; iSide++) {
    float fSide = iSide == 0 ? -1.0f : 1.0f;
    tMechaPose hip;
    tMechaPose thigh;
    tMechaPose shin;
    tMechaPose foot;

    /*
     * Rolled out at the hip, so a stance has width and a glide has an edge
     * to push off. A frame of its own rather than a roll on the thigh: the
     * two rotations do not commute. [MESH-17]
     */
    mecha_pose_child(&hip, &pose, fSide * 0.42f * fRadius * fLimb,
                     build.fHipY, 0.0f, aiHipYaw[iSide], 0,
                     (int)(fSide * (float)aiRoll[iSide]));
    mecha_pose_name(&hip, MECHA_BONE_HIP_L + iSide, build.paBones);
    mecha_pose_child(&thigh, &hip, 0.0f, 0.0f, 0.0f, 0, -aiThigh[iSide], 0);
    mecha_pose_name(&thigh, MECHA_BONE_THIGH_L + iSide, build.paBones);
    /* Wide at the hip and narrowing to the knee, which is the line a
     * thigh has in the reference art and which a box cannot hold. */
    mecha_add_frustum(pList, &thigh, 0.0f, -0.5f * fThighLen, 0.0f,
                      0.20f * fRadius * fLimb * fTaper,
                      0.21f * fRadius * fLimb * fTaper,
                      0.187f * fRadius * fLimb, 0.226f * fRadius * fLimb,
                      0.5f * fThighLen,
                      /* The top of the thigh is narrower than it was and
                       * carried inboard rather than centred on the hip, so
                       * the gap between the legs opens at the top and the
                       * outer line stays where it was. Read off the
                       * reference mesh. [MESH-55] */
                      -fSide * 0.106f * fRadius * fLimb, 0.0f,
                      byBody, byBody, 0);
    /* The knee, standing proud of both thigh and shin so it reads as a
     * joint and its faces stay out of their planes. [MESH-18] */
    mecha_add_box(pList, &thigh, 0.0f, -fThighLen, 0.0f,
                  0.26f * fRadius * fLimb, 0.05f * fHeight,
                  0.27f * fRadius * fLimb, byJoint, byJoint, 0);
    /* A guard over the front of it, sloped back to the shin. This is the
     * one piece of leg trim that survives to MID: a kneecap catches the
     * light against the joint behind it and reads a long way out. */
    if (iDetail >= MECHA_DETAIL_MID) {
      mecha_add_frustum(pList, &thigh, 0.0f, -fThighLen - 0.01f * fHeight,
                        0.17f * fRadius * fLimb,
                        0.22f * fRadius * fLimb, 0.11f * fRadius * fLimb,
                        0.14f * fRadius * fLimb, 0.05f * fRadius * fLimb,
                        0.07f * fHeight, 0.0f, -0.05f * fRadius * fLimb,
                        byTrim, byTrim, 0);
    }

    /*
     * The knee bends backwards, the way a person's does: a positive pose
     * pitch swings a limb aft, so the shin takes the knee angle unnegated
     * while the thigh takes its swing negated. Both the same sign and the
     * machine becomes a bird, which is a fine thing for a mech to be but not
     * what this roster is.
     */
    mecha_pose_child(&shin, &thigh, 0.0f, -fThighLen, 0.0f, 0,
                     aiKnee[iSide], 0);
    mecha_pose_name(&shin, MECHA_BONE_SHIN_L + iSide, build.paBones);
    /*
     * The calf goes the other way from the thigh: in at the knee, out
     * again at the ankle. That flare is what a leg stands on, and a shin
     * that merely tapers to a point looks like it would fall over.
     */
    mecha_add_frustum(pList, &shin, 0.0f, -0.5f * fShinLen, 0.0f,
                      0.23f * fRadius * fLimb, 0.26f * fRadius * fLimb,
                      0.16f * fRadius * fLimb * fTaper,
                      0.17f * fRadius * fLimb * fTaper,
                      0.5f * fShinLen, 0.0f, 0.0f, byBody, byBody, 0);
    /* Armour round the ankle, which is where the leg meets the foot and
     * the one place a walking machine shows its weight. */
    if (iDetail >= MECHA_DETAIL_MID) {
      mecha_add_frustum(pList, &shin, 0.0f, -fShinLen + 0.03f * fHeight,
                        -0.04f * fRadius * fLimb,
                        0.27f * fRadius * fLimb, 0.24f * fRadius * fLimb,
                        0.22f * fRadius * fLimb, 0.20f * fRadius * fLimb,
                        0.05f * fHeight, 0.0f, 0.0f, byJoint, byJoint, 0);
    }
    /* A vernier down the outside of the calf. Pure trim, and the first
     * thing to go. */
    if (iDetail >= MECHA_DETAIL_FULL) {
      mecha_add_frustum(pList, &shin,
                        fSide * 0.20f * fRadius * fLimb,
                        -0.52f * fShinLen, -0.10f * fRadius * fLimb,
                        0.05f * fRadius * fLimb, 0.13f * fRadius * fLimb,
                        0.04f * fRadius * fLimb, 0.09f * fRadius * fLimb,
                        0.18f * fShinLen, 0.0f, 0.0f, byTrim, byTrim, 0);
    }

    /*
     * The foot is flat to the floor both ways while it is on it: the
     * pitches cancel by construction and the ankle gives the hip roll back
     * [MESH-19]. Off it, the toe hangs below that -- zero on the tick the
     * foot leaves the ground and zero again on the tick it lands, so the
     * two rules never argue.
     *
     * Added, so the ankle's lift stacks on what the knee already did.
     * MESH-53 argued the other way from how a forward-pointing limb ought
     * to swing and was wrong: a positive pose pitch carries +Z downwards,
     * so this is what drops the toe. Fitting the toe to the reference mesh
     * wanted the same sign, independently. [MESH-55]
     */
    mecha_pose_child(&foot, &shin, 0.0f, -fShinLen, 0.0f, 0,
                     aiThigh[iSide] - aiKnee[iSide] + aiAnkle[iSide],
                     -(int)(fSide * (float)aiRoll[iSide]));
    mecha_pose_name(&foot, MECHA_BONE_FOOT_L + iSide, build.paBones);
    mecha_add_raked(pList, &foot, 0.0f, -0.5f * fAnkle, 0.06f * fRadius,
                    0.27f * fRadius * fLimb, 0.38f * fRadius * fLimb,
                    0.24f * fRadius * fLimb, 0.34f * fRadius * fLimb,
                    0.5f * fAnkle, 0.0f, 0.0f, 0.19f,
                    byTrim, byTrim, 0);
    /* A toe wedge off the front of it. The foot was one slab, which from
     * the front is a brick the machine is standing on.
     *
     * Raked hard and reaching past the slab, so the machine has a chisel
     * toe rather than a kerb: the sole stays flat on the floor and the
     * armour above it falls away towards the tip. Rotating the toe instead
     * would put the tip through the ground, which the walk cycle checks
     * for. [MESH-55] */
    if (iDetail >= MECHA_DETAIL_MID) {
      mecha_add_raked(pList, &foot,
                      0.0f, -(0.723f + build.fFootToeDrop) * fAnkle,
                      (0.30f + 0.14f) * fRadius * fLimb,
                      0.24f * fRadius * fLimb, 0.10f * fRadius * fLimb,
                      0.19f * fRadius * fLimb, 0.0925f * fRadius * fLimb,
                      0.277f * fAnkle,
                      0.0f, 0.0732f * fRadius * fLimb, 0.55f,
                      byTrim, byTrim, 0);
    }
    /* And a heel behind it, so the foot has a front and a back. */
    if (iDetail >= MECHA_DETAIL_FULL) {
      mecha_add_frustum(pList, &foot, 0.0f, -0.5f * fAnkle,
                        -0.32f * fRadius * fLimb,
                        0.20f * fRadius * fLimb, 0.09f * fRadius * fLimb,
                        0.16f * fRadius * fLimb, 0.07f * fRadius * fLimb,
                        0.42f * fAnkle, 0.0f, -0.03f * fRadius * fLimb,
                        byJoint, byJoint, 0);
    }
  }

  /* --- torso ------------------------------------------------------------
   *
   * Turned off the legs by whatever the heading differs from the stance,
   * and carrying the lean, so a machine strafing across your guns is walking
   * sideways with its shoulders still square to you.
   */
  {
    int iHipRoll = 0;
    int iHipYaw = 0;
    int iTwist = 0;
    int iRock = 0;
    int iRecoil = 0;

    /*
     * What the upper body does about the walk, and the first version of
     * this had it wrong in a way worth recording: the whole torso slid
     * from side to side and rolled with the hips, which is not a walk, it
     * is a machine wobbling.
     *
     * What actually moves is the pelvis. It tilts, dropping on the side
     * whose leg is swinging through, and it turns a little with that leg.
     * The shoulders do neither -- they stay level and turn the other way,
     * and it is that opposition between hips and shoulders that reads as
     * walking rather than as being carried along. [MESH-46]
     */
    if (fWalkPhase >= 0.0f) {
      int iWalkAngle = (int)(fWalkPhase * (float)MECHA_ANGLE_FULL)
                       & (MECHA_ANGLE_FULL - 1);
      float fSwing = mecha_sin(iWalkAngle);
      float fHips = build.iProfile == MECHA_PROFILE_SLENDER
                      ? MECHA_SLIM_HIP_ROLL : 1.0f;

      iHipRoll = (int)((float)MECHA_WALK_HIP_ROLL * fSwing * fHips);
      iHipYaw = (int)((float)MECHA_WALK_TWIST * MECHA_WALK_HIP_TURN * fSwing);
      iTwist = -(int)((float)MECHA_WALK_TWIST * fSwing);
      /* Once a footfall rather than once a cycle, which is why it is read
       * off twice the angle. */
      iRock = (int)((float)MECHA_WALK_ROCK
                    * mecha_cos(mecha_angle_wrap(iWalkAngle * 2)));
    }

    /*
     * A held pose settles the weight onto one hip, which is the same tilt
     * the walk uses held still instead of cycling. Without it the machine
     * stands square with its arm in the air, which reads as a signal rather
     * than as a pose. [MESH-42]
     */
    if (fPosed > 0.0f) {
      /*
       * Small, and it has to be. The pelvis carries the skirt and the legs
       * hang off the frame above it, so tilting the pelvis tilts the armour
       * away from the legs it is meant to be sitting over -- a few degrees
       * is inside the overlap the plates already have [MESH-34], and the
       * nine this was first given opened a gap you could see daylight
       * through. The weight shift is in the stance's own lead and cross;
       * this is only what the hips add to it. [MESH-42]
       */
      iHipRoll += (int)(fPosed * (float)MECHA_WALK_HIP_ROLL * 0.6f);
    }

    /*
     * And the whole machine answers its own guns. Every other frame kicks
     * only the arm that fired [MESH-20]; this one rocks back from the waist
     * as well, which is what makes a small machine firing a large weapon
     * read as a small machine firing a large weapon.
     */
    if (build.iProfile == MECHA_PROFILE_SLENDER && pMech->iRecovery > 0)
      iRecoil = MECHA_ARM_RECOIL * mecha_clampi(pMech->iRecovery, 0, 8) / 8;

    /*
     * Two frames off the same point: the pelvis, which the skirt hangs on
     * and which tilts, and the torso, which everything above the waist
     * hangs on and which does not.
     */
    mecha_pose_child(&pelvis, &pose, 0.0f, build.fHipY, 0.0f,
                     mecha_angle_delta(pMech->iLegYaw, pMech->iFacing)
                       + iHipYaw, 0, iHipRoll);
    mecha_pose_name(&pelvis, MECHA_BONE_PELVIS, build.paBones);
    mecha_pose_child(&torso, &pose, 0.0f, build.fHipY, 0.0f,
                     mecha_angle_delta(pMech->iLegYaw, pMech->iFacing)
                       + iTwist,
                     mecha_mesh_lean_pitch(pMech) - iRecoil + iRock, 0);
    mecha_pose_name(&torso, MECHA_BONE_TORSO, build.paBones);
  }

  build.fWalkPhase = fWalkPhase;
  mecha_build_torso(pList, &build, &torso);
  mecha_build_skirt(pList, &build, &pelvis, aiThigh);
  mecha_build_arms_head(pList, pWorld, iMechIdx, &build, &torso);
  mecha_build_plume(pList, pWorld, &build, &torso);
}

//-------------------------------------------------------------------------------------------------

void mecha_mesh_shadows(tMechaQuadList *pList, const tMechaWorld *pWorld)
{
  static tMechaQuad aSource[MECHA_QUAD_CAPACITY];
  mecha_quads_part(pList, MECHA_PART_NONE);
  int i;

  if (!pList || !pWorld)
    return;

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    const tMechaMech *pMech = &pWorld->aMechs[i];
    tMechaQuadList source;
    float fGround;
    int iQuad;
    int iShadowCount = 0;

    if (!mecha_mech_alive(pMech))
      continue;
    mecha_quads_reset(&source, aSource, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&source, pWorld, i, MECHA_DETAIL_FAR);
    if (source.iCount <= 0)
      continue;

    /* The shadow sits on whatever the mech is standing over, so a mech on
     * top of a box casts onto the box rather than onto the floor below it. */
    fGround = mecha_arena_ground_height(&pWorld->arena, pMech->fX, pMech->fZ,
                                        pMech->fY);
    /*
     * Model 2 used a flattened copy of the machine silhouette projected
     * onto the ground. Apply a shallow directional projection and put each
     * resulting vertex on the local terrain height.
     */
    for (iQuad = 0; iQuad < source.iCount; iQuad++) {
      float afVert[4][3];
      int iVert;

      /* Weapons, exhaust and drones are visual effects rather than the
       * machine's readable ground silhouette. */
      if (source.paQuads[iQuad].byPart == MECHA_PART_GUN
          || source.paQuads[iQuad].byPart == MECHA_PART_THRUST
          || source.paQuads[iQuad].byPart == MECHA_PART_DRONE)
        continue;
      /*
       * Keep the complete early body panels rather than striding through the
       * source mesh. Striding made the shadow visibly hollow because a leg
       * or torso face could be skipped while its neighbours survived.
       */
      if (iShadowCount >= 32)
        break;
      if (pList->iCount >= MECHA_QUAD_CAPACITY)
        break;

      for (iVert = 0; iVert < 4; iVert++) {
        float fHeight = source.paQuads[iQuad].afVert[iVert][1] - fGround;
        float fX;
        float fZ;

        fHeight = mecha_clampf(fHeight, 0.0f, 18.0f * MECHA_METRE);
        fX = source.paQuads[iQuad].afVert[iVert][0] + 0.18f * fHeight;
        fZ = source.paQuads[iQuad].afVert[iVert][2] + 0.12f * fHeight;
        afVert[iVert][0] = fX;
        afVert[iVert][2] = fZ;
        afVert[iVert][1] = mecha_arena_ground_height(&pWorld->arena,
                                                     fX, fZ, fGround)
                           + (0.04f + 0.004f * (float)i
                              + 0.001f * (float)iShadowCount) * MECHA_METRE;
      }
      mecha_quads_add(pList, afVert, MECHA_SHADE_SHADOW,
                      MECHA_QUAD_TWO_SIDED | MECHA_QUAD_SHADOW);
      iShadowCount++;
    }
  }
}

//-------------------------------------------------------------------------------------------------
/* Camera-facing geometry */

/* Tags the quad most recently added to the list. Every add appends exactly
 * one, so this is simply "the billboard I just made". */
/*
 * Tags every quad added since iFirst. A box is six faces, and the one
 * looking at the sky wants a roof rather than a wall -- picked off the
 * normal rather than off the order the faces happen to be built in, so it
 * stays right if that order ever changes.
 */
static void mecha_tag_box(tMechaQuadList *pList, int iFirst, int iBank,
                          int iSide, int iTop)
{
  int i;

  if (!pList || iBank == MECHA_TEX_NONE)
    return;
  for (i = iFirst; i < pList->iCount; i++) {
    pList->paQuads[i].byTexBank = (uint8_t)iBank;
    pList->paQuads[i].byTile = pList->paQuads[i].afNormal[1] > 0.5f
                                 ? (uint8_t)iTop : (uint8_t)iSide;
  }
}

static void mecha_tag_texture(tMechaQuadList *pList, int iBank, int iTile)
{
  if (pList && pList->iCount > 0) {
    pList->paQuads[pList->iCount - 1].byTexBank = (uint8_t)iBank;
    pList->paQuads[pList->iCount - 1].byTile = (uint8_t)iTile;
  }
}

/* Walks a frame range by an effect's age, clamped at both ends. */
static int mecha_sprite_frame(int iFirst, int iLast, float fAge)
{
  int iCount = iLast - iFirst + 1;
  int iStep = (int)(fAge * (float)iCount);

  if (iStep < 0)
    iStep = 0;
  if (iStep >= iCount)
    iStep = iCount - 1;
  return iFirst + iStep;
}

/*
 * The plasma frames boil on a fixed cadence rather than over a fraction of
 * a life. A shot in flight has no age that means anything to look at -- a
 * beam that lives a third of a second and a lobbed charge that arcs for two
 * should shimmer at the same rate -- so this walks the sequence by ticks and
 * wraps, where the effect sprites walk theirs once and stop.
 */
#define MECHA_PLASMA_TICKS_PER_FRAME 2

void mecha_mesh_set_sprites(bool bAvailable)
{
  s_bSprites = bAvailable;
}

void mecha_mesh_set_car_skin(bool bAvailable)
{
  s_bCarSkin = bAvailable;
}

static int mecha_plasma_frame(int iAge)
{
  int iCount = MECHA_SPRITE_PLASMA_LAST - MECHA_SPRITE_PLASMA_FIRST + 1;
  int iStep;

  if (iAge < 0)
    iAge = 0;
  iStep = (iAge / MECHA_PLASMA_TICKS_PER_FRAME) % iCount;
  return MECHA_SPRITE_PLASMA_FIRST + iStep;
}

static void mecha_add_billboard(tMechaQuadList *pList, int iCameraYaw,
                                float fX, float fY, float fZ, float fSize,
                                uint8_t byPalette)
{
  float fRightX = mecha_cos(iCameraYaw);
  float fRightZ = -mecha_sin(iCameraYaw);
  float afVert[4][3];

  afVert[0][0] = fX - fRightX * fSize;
  afVert[0][1] = fY - fSize;
  afVert[0][2] = fZ - fRightZ * fSize;
  afVert[1][0] = fX + fRightX * fSize;
  afVert[1][1] = fY - fSize;
  afVert[1][2] = fZ + fRightZ * fSize;
  afVert[2][0] = fX + fRightX * fSize;
  afVert[2][1] = fY + fSize;
  afVert[2][2] = fZ + fRightZ * fSize;
  afVert[3][0] = fX - fRightX * fSize;
  afVert[3][1] = fY + fSize;
  afVert[3][2] = fZ - fRightZ * fSize;
  mecha_quads_add(pList, afVert, byPalette,
                  MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);
}

/*
 * Half a billboard, offset sideways, optionally mirrored. Two side by side,
 * one flipped, make a sprite and its reflection. [MESH-21]
 */
static void mecha_add_billboard_half(tMechaQuadList *pList, int iCameraYaw,
                                     float fX, float fY, float fZ,
                                     float fHalfW, float fHalfH,
                                     float fShift, bool bMirror,
                                     uint8_t byPalette)
{
  float fRightX = mecha_cos(iCameraYaw);
  float fRightZ = -mecha_sin(iCameraYaw);
  float fCx = fX + fRightX * fShift;
  float fCz = fZ + fRightZ * fShift;
  float afVert[4][3];
  uint8_t byFlags = MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW;

  afVert[0][0] = fCx - fRightX * fHalfW;
  afVert[0][1] = fY - fHalfH;
  afVert[0][2] = fCz - fRightZ * fHalfW;
  afVert[1][0] = fCx + fRightX * fHalfW;
  afVert[1][1] = fY - fHalfH;
  afVert[1][2] = fCz + fRightZ * fHalfW;
  afVert[2][0] = fCx + fRightX * fHalfW;
  afVert[2][1] = fY + fHalfH;
  afVert[2][2] = fCz + fRightZ * fHalfW;
  afVert[3][0] = fCx - fRightX * fHalfW;
  afVert[3][1] = fY + fHalfH;
  afVert[3][2] = fCz - fRightZ * fHalfW;
  if (bMirror)
    byFlags |= MECHA_QUAD_TEX_FLIP;
  mecha_quads_add(pList, afVert, byPalette, byFlags);
}

//-------------------------------------------------------------------------------------------------

/*
 * A billboard standing on the ground rather than centred on a point: its
 * bottom edge is at fBase and it is as tall as it is wide, which is what a
 * tree is. No glow -- a self-lit tree is a lamp -- so it sorts on its own
 * middle like any other quad, which for something standing upright on the
 * floor is where it actually is.
 */
static void mecha_add_upright_billboard(tMechaQuadList *pList, int iCameraYaw,
                                        float fX, float fBase, float fZ,
                                        float fHeight, uint8_t byPalette)
{
  float fRightX = mecha_cos(iCameraYaw);
  float fRightZ = -mecha_sin(iCameraYaw);
  float fHalf = fHeight * 0.5f;
  float afVert[4][3];

  afVert[0][0] = fX - fRightX * fHalf;
  afVert[0][1] = fBase;
  afVert[0][2] = fZ - fRightZ * fHalf;
  afVert[1][0] = fX + fRightX * fHalf;
  afVert[1][1] = fBase;
  afVert[1][2] = fZ + fRightZ * fHalf;
  afVert[2][0] = fX + fRightX * fHalf;
  afVert[2][1] = fBase + fHeight;
  afVert[2][2] = fZ + fRightZ * fHalf;
  afVert[3][0] = fX - fRightX * fHalf;
  afVert[3][1] = fBase + fHeight;
  afVert[3][2] = fZ - fRightZ * fHalf;
  mecha_quads_add(pList, afVert, byPalette, MECHA_QUAD_TWO_SIDED);
}

//-------------------------------------------------------------------------------------------------

/*
 * The close-quarters blade: pointed, level, running out along the line of
 * the swing. Two planes through the same axis, so it never turns edge-on --
 * there is no camera in the geometry at all. [MESH-22]
 */
static void mecha_add_blade(tMechaQuadList *pList, float fX, float fY,
                            float fZ, float fDirX, float fDirZ,
                            float fReach, uint8_t byPalette)
{
  /* Proportions, against the reach: how far back the hilt sits, how wide
   * the blade is, where it starts tapering, and the crossguard. */
  const float fHilt = 0.34f;
  const float fWide = 0.055f;
  const float fShoulder = 0.66f;
  const float fGuard = 0.22f;
  float fLen = mecha_length2(fDirX, fDirZ);
  float fAxisX;
  float fAxisZ;
  float fSideX;
  float fSideZ;
  float fBackX;
  float fBackZ;
  float fBackY = fY;
  float afVert[4][3];
  int iPlane;

  if (fLen < 1e-4f)
    return;
  fAxisX = fDirX / fLen;
  fAxisZ = fDirZ / fLen;
  fSideX = fAxisZ;
  fSideZ = -fAxisX;
  fBackX = fX - fAxisX * fReach * fHilt;
  fBackZ = fZ - fAxisZ * fReach * fHilt;

  for (iPlane = 0; iPlane < 2; iPlane++) {
    /* The flat of the blade, then the same blade stood on edge. */
    float fOutX = iPlane == 0 ? fSideX * fReach * fWide : 0.0f;
    float fOutZ = iPlane == 0 ? fSideZ * fReach * fWide : 0.0f;
    float fOutY = iPlane == 0 ? 0.0f : fReach * fWide;
    float fShoulderX = fBackX + fAxisX * fReach * fShoulder;
    float fShoulderZ = fBackZ + fAxisZ * fReach * fShoulder;
    float fTipX = fBackX + fAxisX * fReach;
    float fTipZ = fBackZ + fAxisZ * fReach;

    /* Body: the hilt end, squared off, out to the shoulder. */
    afVert[0][0] = fBackX - fOutX;
    afVert[0][1] = fBackY - fOutY;
    afVert[0][2] = fBackZ - fOutZ;
    afVert[1][0] = fBackX + fOutX;
    afVert[1][1] = fBackY + fOutY;
    afVert[1][2] = fBackZ + fOutZ;
    afVert[2][0] = fShoulderX + fOutX;
    afVert[2][1] = fBackY + fOutY;
    afVert[2][2] = fShoulderZ + fOutZ;
    afVert[3][0] = fShoulderX - fOutX;
    afVert[3][1] = fBackY - fOutY;
    afVert[3][2] = fShoulderZ - fOutZ;
    mecha_quads_add(pList, afVert, byPalette,
                    MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);

    /* Point: the same width collapsing onto the tip. Two of the corners
     * land on the same place, which is how a quad list draws a triangle. */
    afVert[0][0] = fShoulderX - fOutX;
    afVert[0][1] = fBackY - fOutY;
    afVert[0][2] = fShoulderZ - fOutZ;
    afVert[1][0] = fShoulderX + fOutX;
    afVert[1][1] = fBackY + fOutY;
    afVert[1][2] = fShoulderZ + fOutZ;
    afVert[2][0] = fTipX;
    afVert[2][1] = fBackY;
    afVert[2][2] = fTipZ;
    afVert[3][0] = fTipX;
    afVert[3][1] = fBackY;
    afVert[3][2] = fTipZ;
    mecha_quads_add(pList, afVert, byPalette,
                    MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);
  }

  /* The crossguard, across the hilt and lying flat. */
  {
    float fGuardX = fSideX * fReach * fGuard * 0.5f;
    float fGuardZ = fSideZ * fReach * fGuard * 0.5f;
    float fThickX = fAxisX * fReach * fWide * 0.7f;
    float fThickZ = fAxisZ * fReach * fWide * 0.7f;

    afVert[0][0] = fBackX - fGuardX - fThickX;
    afVert[0][1] = fBackY;
    afVert[0][2] = fBackZ - fGuardZ - fThickZ;
    afVert[1][0] = fBackX + fGuardX - fThickX;
    afVert[1][1] = fBackY;
    afVert[1][2] = fBackZ + fGuardZ - fThickZ;
    afVert[2][0] = fBackX + fGuardX + fThickX;
    afVert[2][1] = fBackY;
    afVert[2][2] = fBackZ + fGuardZ + fThickZ;
    afVert[3][0] = fBackX - fGuardX + fThickX;
    afVert[3][1] = fBackY;
    afVert[3][2] = fBackZ - fGuardZ + fThickZ;
    mecha_quads_add(pList, afVert, byPalette,
                    MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);
  }
}

//-------------------------------------------------------------------------------------------------

/* A streak along the segment the shot covered this tick, widened towards the
 * camera. This is what makes a fast round readable at 60 Hz instead of a dot
 * that teleports across the arena. */
static void mecha_add_tracer(tMechaQuadList *pList, int iCameraYaw,
                             const float afEye[3],
                             float fX0, float fY0, float fZ0,
                             float fX1, float fY1, float fZ1,
                             float fWidth, uint8_t byPalette)
{
  /*
   * The line from the eye to the streak, not the direction the camera
   * happens to be pointing. Those agree only at the centre of the screen;
   * off to one side the old vector widened the quad along a direction that
   * was not square to the eye and the streak foreshortened away to a
   * hairline exactly where it was hardest to see. [MESH-48]
   */
  float fForwardX = (fX0 + fX1) * 0.5f - afEye[0];
  float fForwardY = (fY0 + fY1) * 0.5f - afEye[1];
  float fForwardZ = (fZ0 + fZ1) * 0.5f - afEye[2];
  float fForwardLen = mecha_length3(fForwardX, fForwardY, fForwardZ);
  float fAxisX = fX1 - fX0;
  float fAxisY = fY1 - fY0;
  float fAxisZ = fZ1 - fZ0;
  float fLength = mecha_length3(fAxisX, fAxisY, fAxisZ);
  float fSideX;
  float fSideY;
  float fSideZ;
  float fSideLength;
  float afVert[4][3];

  if (fLength < fWidth) {
    mecha_add_billboard(pList, iCameraYaw, fX1, fY1, fZ1, fWidth, byPalette);
    return;
  }

  fAxisX /= fLength;
  fAxisY /= fLength;
  fAxisZ /= fLength;
  if (fForwardLen > 1e-4f) {
    fForwardX /= fForwardLen;
    fForwardY /= fForwardLen;
    fForwardZ /= fForwardLen;
  } else {
    fForwardX = mecha_sin(iCameraYaw);
    fForwardY = 0.0f;
    fForwardZ = mecha_cos(iCameraYaw);
  }

  /* Perpendicular to both the flight path and the view direction, so the
   * streak keeps its width whatever angle it is seen from. */
  fSideX = fAxisY * fForwardZ - fAxisZ * fForwardY;
  fSideY = fAxisZ * fForwardX - fAxisX * fForwardZ;
  fSideZ = fAxisX * fForwardY - fAxisY * fForwardX;
  fSideLength = mecha_length3(fSideX, fSideY, fSideZ);
  if (fSideLength < 1e-4f) {
    /* Flying straight at or away from the camera. */
    fSideX = mecha_cos(iCameraYaw);
    fSideY = 0.0f;
    fSideZ = -mecha_sin(iCameraYaw);
    fSideLength = 1.0f;
  }
  fSideX = fSideX / fSideLength * fWidth;
  fSideY = fSideY / fSideLength * fWidth;
  fSideZ = fSideZ / fSideLength * fWidth;

  afVert[0][0] = fX0 - fSideX; afVert[0][1] = fY0 - fSideY; afVert[0][2] = fZ0 - fSideZ;
  afVert[1][0] = fX1 - fSideX; afVert[1][1] = fY1 - fSideY; afVert[1][2] = fZ1 - fSideZ;
  afVert[2][0] = fX1 + fSideX; afVert[2][1] = fY1 + fSideY; afVert[2][2] = fZ1 + fSideZ;
  afVert[3][0] = fX0 + fSideX; afVert[3][1] = fY0 + fSideY; afVert[3][2] = fZ0 + fSideZ;
  mecha_quads_add(pList, afVert, byPalette,
                  MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);
}

//-------------------------------------------------------------------------------------------------

/*
 * The roster paints its tracers in eight colours (mecha_defs.c names them
 * PAL_TRACER_*), and a bolt is drawn from whichever recoloured copy of the
 * plasma frames sits nearest to that. Three recoloured banks cover the warm
 * half of the set; anything blue, cyan or white keeps the blue the frames
 * were drawn in. [MESH-47]
 */
int mecha_bolt_bank(uint8_t byPalette)
{
  switch (byPalette) {
  case 171:                       /* orange  */
  case 207:                       /* yellow  */
  case 231:                       /* red     */
    return MECHA_TEX_EFFECT_WARM;
  case 195:                       /* magenta */
    return MECHA_TEX_EFFECT_MAGENTA;
  case 183:                       /* rose    */
    return MECHA_TEX_EFFECT_ROSE;
  default:
    return MECHA_TEX_EFFECT;      /* blue, cyan and white are near enough */
  }
}

//-------------------------------------------------------------------------------------------------
/* Draw order */

/*
 * A quad is "broad" when it is big enough for the far end of it to be a long
 * way further off than the middle. Two and a half metres is the line: floor
 * tiles and the tops of cover are broad, the panels a mech is built from are
 * not, and it matters which side of it a quad falls on -- see below.
 */
#define MECHA_BROAD_QUAD MECHA_M(2.5f)

float mecha_quad_depth_key(const tMechaQuad *pQuad, const float afEye[3],
                           const float afForward[3])
{
  float afDepth[4];
  float fCentre = 0.0f;
  float fMin;
  float fMax;
  float fSpanX = 0.0f;
  float fSpanZ = 0.0f;
  int v;

  for (v = 0; v < 4; v++) {
    afDepth[v] = (pQuad->afVert[v][0] - afEye[0]) * afForward[0]
               + (pQuad->afVert[v][1] - afEye[1]) * afForward[1]
               + (pQuad->afVert[v][2] - afEye[2]) * afForward[2];
    fCentre += afDepth[v] * 0.25f;
  }
  fMin = afDepth[0];
  fMax = afDepth[0];
  for (v = 1; v < 4; v++) {
    if (afDepth[v] < fMin)
      fMin = afDepth[v];
    if (afDepth[v] > fMax)
      fMax = afDepth[v];
  }

  /*
   * No depth buffer, so a quad is drawn before another or after it, whole.
   * The middle of the quad is the honest key for most geometry; decals take
   * their nearest corner and broad floors their farthest. [MESH-23]
   */
  if (pQuad->byFlags & (MECHA_QUAD_SHADOW | MECHA_QUAD_DECAL))
    /*
     * Projected silhouettes can span several source panels. Pull the
     * nearest corner back by a small footprint allowance so a broad floor
     * quad never sorts in front of the shadow it receives.
     */
    return fMin - MECHA_M(0.5f);

  /*
   * Self-lit geometry is drawn on top of whatever it is going off inside:
   * the key is pulled forward by the sprite's own size, capped. [MESH-24]
   */
  if (pQuad->byFlags & MECHA_QUAD_GLOW) {
    /* The half-width is the radius of the volume the sprite stands for.
     * The narrower edge is measured, for the sake of tracers. [MESH-24] */
    float fEdgeA = mecha_length3(pQuad->afVert[1][0] - pQuad->afVert[0][0],
                                 pQuad->afVert[1][1] - pQuad->afVert[0][1],
                                 pQuad->afVert[1][2] - pQuad->afVert[0][2]);
    float fEdgeB = mecha_length3(pQuad->afVert[2][0] - pQuad->afVert[1][0],
                                 pQuad->afVert[2][1] - pQuad->afVert[1][1],
                                 pQuad->afVert[2][2] - pQuad->afVert[1][2]);
    float fReach = 0.5f * (fEdgeA < fEdgeB ? fEdgeA : fEdgeB)
                 + MECHA_M(0.4f);

    if (fReach > MECHA_M(6.0f))
      fReach = MECHA_M(6.0f);
    return fMin - fReach;
  }

  if ((pQuad->byFlags & MECHA_QUAD_GROUND) != 0
      && (pQuad->afNormal[1] > 0.9f || pQuad->afNormal[1] < -0.9f)) {
    float fLowX = pQuad->afVert[0][0];
    float fHighX = fLowX;
    float fLowZ = pQuad->afVert[0][2];
    float fHighZ = fLowZ;

    for (v = 1; v < 4; v++) {
      if (pQuad->afVert[v][0] < fLowX)
        fLowX = pQuad->afVert[v][0];
      if (pQuad->afVert[v][0] > fHighX)
        fHighX = pQuad->afVert[v][0];
      if (pQuad->afVert[v][2] < fLowZ)
        fLowZ = pQuad->afVert[v][2];
      if (pQuad->afVert[v][2] > fHighZ)
        fHighZ = pQuad->afVert[v][2];
    }
    fSpanX = fHighX - fLowX;
    fSpanZ = fHighZ - fLowZ;
    if (fSpanX > MECHA_BROAD_QUAD || fSpanZ > MECHA_BROAD_QUAD)
      return fMax;
  }
  return fCentre;
}

//-------------------------------------------------------------------------------------------------
/* Clouds */

/*
 * How many, how far out, and how big. The radius is chosen against the arena
 * rather than the sky, so crossing the floor does not swing it. [MESH-25]
 */
#define MECHA_CLOUD_COUNT   30
/*
 * A starfield is the same dome asked for something else: many more of them,
 * far smaller, spread over the whole sphere rather than banked towards the
 * horizon, and flat rather than textured -- a star is a point of light and
 * needs no artwork, which is also what lets one appear on a checkout with no
 * retail data. And one thing that is not a star. [MESH-49]
 */
#define MECHA_STAR_COUNT    420
#define MECHA_STAR_SIZE     0.0022f
#define MECHA_STAR_VARY     0.0026f
#define MECHA_PAL_STAR      143
/*
 * The planet: where it sits on the dome, and how much of it it takes up.
 * Read these on the tipped dome, not the upright one -- elevation there is
 * how near the world axis the planet orbits (70 degrees is a circle twenty
 * wide around it, so the planet wheels without ever leaving the sky) and
 * azimuth is where on that circle it starts. [MESH-49]
 */
#define MECHA_PLANET_AZIMUTH MECHA_DEG(36)
#define MECHA_PLANET_HEIGHT  MECHA_DEG(70)
#define MECHA_PLANET_SIZE    0.16f
#define MECHA_PLANET_WEDGES  20
#define MECHA_PAL_PLANET     148   /* the face of it */
#define MECHA_PAL_PLANET_RIM 145   /* and its limb, a shade deeper */
#define MECHA_CLOUD_RADIUS  MECHA_M(1400.0f)
#define MECHA_CLOUD_FLOOR   MECHA_DEG(7)    /* nothing below this elevation */
#define MECHA_CLOUD_CEILING MECHA_DEG(52)
/* Ticks per angle unit. A shade under one circuit an hour: a sky that is
 * visibly moving is a sky the player is looking at instead of the fight. */
#define MECHA_CLOUD_DRIFT   12
/*
 * Except over a stage that is not on a planet, where the sky turning is the
 * point rather than a distraction: ten times the rate, which is a circuit
 * every five and a half minutes, or a bit over a quarter turn in a ninety
 * second round -- motion a player can see happening rather than motion they
 * can only tell has happened. [MESH-52]
 */
#define MECHA_STAR_SPIN     10

/* A cheap integer hash, so the sky is a pure function of the cloud's index
 * and the arena it hangs over. Nothing is stored between frames and nothing
 * is drawn from the world's random stream, which would put the look of the
 * sky at the mercy of how many shots had been fired under it. */
static uint32_t mecha_cloud_hash(uint32_t uiValue)
{
  uiValue *= 2654435761u;
  uiValue ^= uiValue >> 15;
  uiValue *= 2246822519u;
  uiValue ^= uiValue >> 13;
  return uiValue;
}

/*
 * The forest: camera-facing tree sprites past the boundary that neither
 * collide nor block a shot, so the arena reads as a clearing in a wood.
 * Placed by a hash of index and match seed, so the same match grows the
 * same forest. [MESH-26]
 */
#define MECHA_TREE_TILE_FIRST 27
#define MECHA_TREE_TILE_COUNT 3

void mecha_mesh_scenery(tMechaQuadList *pList, const tMechaArena *pArena,
                        uint32_t uiSeed, int iCameraYaw)
{
  mecha_quads_part(pList, MECHA_PART_NONE);
  float fGrow;
  int iEdges;
  int i;

  if (!pList || !pArena || !s_bSprites || pArena->iBillboards <= 0
      || pArena->fOuterReach <= pArena->fHalfExtent)
    return;

  fGrow = pArena->fOuterReach / pArena->fHalfExtent;
  iEdges = pArena->byShape == MECHA_ARENA_OCTAGON ? 8 : 4;

  for (i = 0; i < pArena->iBillboards; i++) {
    uint32_t uiHash = mecha_cloud_hash((uint32_t)i * 2654435761u + uiSeed);
    uint32_t uiJitter = mecha_cloud_hash(uiHash ^ 0x9E3779B9u);
    float afFrom[2];
    float afTo[2];
    float fAlong = (float)(uiHash & 1023u) / 1024.0f;
    float fOut = (float)((uiHash >> 10) & 1023u) / 1024.0f;
    float fScale;
    float fSpread;
    float fX;
    float fZ;
    float fHigh;

    /* Squared, so the trees crowd against the boundary rather than
     * scattering evenly into a haze. [MESH-26] */
    mecha_boundary_corner(pArena, (int)((uiHash >> 20) % (uint32_t)iEdges),
                          afFrom);
    mecha_boundary_corner(pArena,
                          (int)((uiHash >> 20) % (uint32_t)iEdges) + 1, afTo);
    fScale = 1.0f + (fGrow - 1.0f) * fOut * fOut;
    fX = (afFrom[0] + (afTo[0] - afFrom[0]) * fAlong) * fScale;
    fZ = (afFrom[1] + (afTo[1] - afFrom[1]) * fAlong) * fScale;

    /* Pushed about by roughly the spacing they would otherwise sit at, so
     * they are a wood and not a set of concentric fences. */
    fSpread = pArena->fHalfExtent * 0.22f * fScale;
    fX += ((float)(uiJitter & 2047u) / 1024.0f - 1.0f) * fSpread;
    fZ += ((float)((uiJitter >> 11) & 2047u) / 1024.0f - 1.0f) * fSpread;

    /* Not where the fight is. A sprite with no collision standing in the
     * arena is a tree you walk through, which is worse than no tree. */
    if (mecha_arena_contains(pArena, fX, fZ))
      continue;

    fHigh = MECHA_M(22.0f)
            + MECHA_M(18.0f) * (float)((uiJitter >> 24) & 255u) / 255.0f;
    mecha_add_upright_billboard(pList, iCameraYaw, fX,
                                mecha_arena_terrain_height(pArena, fX, fZ),
                                fZ, fHigh, MECHA_PAL_CANOPY);
    mecha_tag_texture(pList, MECHA_TEX_STRUCT,
                      MECHA_TREE_TILE_FIRST
                        + (int)((uiHash >> 28) % MECHA_TREE_TILE_COUNT));
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * The dome's frame at one point on it: which way that point lies, and the
 * two axes tangent to the dome there. Tipped means the whole frame is put
 * on its side -- see mecha_sky_quad. [MESH-49]
 */
static bool mecha_sky_frame(int iElevation, int iAzimuth, bool bTipped,
                            float afDir[3], float afRight[3], float afUp[3])
{
  float fLength;

  afDir[0] = mecha_cos(iElevation) * mecha_sin(iAzimuth);
  afDir[1] = mecha_sin(iElevation);
  afDir[2] = mecha_cos(iElevation) * mecha_cos(iAzimuth);

  afRight[0] = afDir[2];
  afRight[1] = 0.0f;
  afRight[2] = -afDir[0];
  fLength = mecha_length3(afRight[0], afRight[1], afRight[2]);
  if (fLength < 1e-4f)
    return false;
  afRight[0] /= fLength;
  afRight[2] /= fLength;
  afUp[0] = afDir[1] * afRight[2] - afDir[2] * afRight[1];
  afUp[1] = afDir[2] * afRight[0] - afDir[0] * afRight[2];
  afUp[2] = afDir[0] * afRight[1] - afDir[1] * afRight[0];

  if (bTipped) {
    float fSwap;

    /* On its side: vertical and forward change places, which stands the
     * dome on its edge with the spin axis pointing north. */
    fSwap = afDir[1];   afDir[1] = afDir[2];     afDir[2] = fSwap;
    fSwap = afRight[1]; afRight[1] = afRight[2]; afRight[2] = fSwap;
    fSwap = afUp[1];    afUp[1] = afUp[2];       afUp[2] = fSwap;

    /*
     * And then turned flat, a quarter anticlockwise, which carries that
     * axis round from north-south to west-east: (x, z) -> (-z, x) takes
     * north to west. The dome is edge-on to a player facing north now, so
     * the sky climbs past them rather than wheeling in front of them, and
     * face-on from the ends of the stage. [MESH-51]
     */
    fSwap = afDir[0];   afDir[0] = -afDir[2];     afDir[2] = fSwap;
    fSwap = afRight[0]; afRight[0] = -afRight[2]; afRight[2] = fSwap;
    fSwap = afUp[0];    afUp[0] = -afUp[2];       afUp[2] = fSwap;
  }
  return true;
}

//-------------------------------------------------------------------------------------------------

/*
 * One quad on the dome, facing its middle. The direction is built in the
 * canonical frame -- elevation off the horizon, azimuth around it -- and
 * then turned into whatever frame the sky is using, so the tangent basis
 * comes out consistent with it however the dome is standing. [MESH-49]
 */
static void mecha_sky_quad(tMechaQuadList *pList, int iElevation,
                           int iAzimuth, float fSize, bool bTipped,
                           uint8_t byPalette)
{
  float afDir[3];
  float afRight[3];
  float afUp[3];
  float afVert[4][3];
  int iCorner;

  /*
   * Tangent to the dome, so every puff faces its middle -- which is where
   * the camera is, near enough, and is why these are not camera-facing
   * billboards: a billboard high overhead turns edge-on to a camera
   * underneath it and the sky develops holes.
   *
   * And the dome on its side, when asked. Swapping the vertical and forward
   * axes moves the spin axis from straight up to straight out, so the sky
   * turns like a wheel standing in front of the player rather than like a
   * ceiling fan above them -- and turns clockwise, because a point climbing
   * in azimuth goes up and then right on a screen looking down +Z. Applied
   * to the whole frame, not just the direction, or the quads end up facing
   * somewhere the dome is not. [MESH-49]
   */
  if (!mecha_sky_frame(iElevation, iAzimuth, bTipped, afDir, afRight, afUp))
    return;

  for (iCorner = 0; iCorner < 4; iCorner++) {
    float fU = (iCorner == 0 || iCorner == 3) ? -fSize : fSize;
    float fV = iCorner < 2 ? -fSize : fSize;
    int iAxis;

    for (iAxis = 0; iAxis < 3; iAxis++) {
      afVert[iCorner][iAxis] = afDir[iAxis] * MECHA_CLOUD_RADIUS
                             + afRight[iAxis] * fU + afUp[iAxis] * fV;
    }
  }
  mecha_quads_add(pList, afVert, byPalette,
                  MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);
}


//-------------------------------------------------------------------------------------------------

/*
 * A circle on the dome, built rather than drawn: a ring of quads round a
 * centre, each one a wedge of the disc.
 *
 * Geometry rather than a generated sprite. A sprite would have wanted a
 * texture bank of its own, and the engine's legacy texture path is nineteen
 * banks of pointers indexed without a bounds check [REND-15] -- a tenth
 * bank there is a memory bug waiting rather than a circle. Built from quads
 * it costs a dozen of them, needs no artwork, and is there on a checkout
 * with no retail data at all. [MESH-49]
 */
static void mecha_sky_disc(tMechaQuadList *pList, int iElevation,
                           int iAzimuth, float fSize, uint8_t byPalette,
                           uint8_t byRimPalette)
{
  float afDir[3];
  float afRight[3];
  float afUp[3];
  float afVert[4][3];
  int iWedge;

  if (!mecha_sky_frame(iElevation, iAzimuth, true, afDir, afRight, afUp))
    return;

  for (iWedge = 0; iWedge < MECHA_PLANET_WEDGES; iWedge++) {
    int iA = MECHA_ANGLE_FULL * iWedge / MECHA_PLANET_WEDGES;
    int iB = MECHA_ANGLE_FULL * (iWedge + 1) / MECHA_PLANET_WEDGES;
    float fAx = mecha_sin(iA) * fSize;
    float fAy = mecha_cos(iA) * fSize;
    float fBx = mecha_sin(iB) * fSize;
    float fBy = mecha_cos(iB) * fSize;
    /* The limb a shade off the body, so the edge reads as a curve rather
     * than as the end of a flat colour. */
    float fIn = 0.88f;
    int iAxis;

    /* The middle, out to the rim: two corners on the rim and two on the
     * inner ring, which is a quad the whole way round. */
    for (iAxis = 0; iAxis < 3; iAxis++) {
      afVert[0][iAxis] = afDir[iAxis] * MECHA_CLOUD_RADIUS;
      afVert[1][iAxis] = afDir[iAxis] * MECHA_CLOUD_RADIUS
                       + afRight[iAxis] * fAx * fIn + afUp[iAxis] * fAy * fIn;
      afVert[2][iAxis] = afDir[iAxis] * MECHA_CLOUD_RADIUS
                       + afRight[iAxis] * fBx * fIn + afUp[iAxis] * fBy * fIn;
      afVert[3][iAxis] = afVert[0][iAxis];
    }
    mecha_quads_add(pList, afVert, byPalette,
                    MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);

    for (iAxis = 0; iAxis < 3; iAxis++) {
      afVert[0][iAxis] = afDir[iAxis] * MECHA_CLOUD_RADIUS
                       + afRight[iAxis] * fAx * fIn + afUp[iAxis] * fAy * fIn;
      afVert[1][iAxis] = afDir[iAxis] * MECHA_CLOUD_RADIUS
                       + afRight[iAxis] * fAx + afUp[iAxis] * fAy;
      afVert[2][iAxis] = afDir[iAxis] * MECHA_CLOUD_RADIUS
                       + afRight[iAxis] * fBx + afUp[iAxis] * fBy;
      afVert[3][iAxis] = afDir[iAxis] * MECHA_CLOUD_RADIUS
                       + afRight[iAxis] * fBx * fIn + afUp[iAxis] * fBy * fIn;
    }
    mecha_quads_add(pList, afVert, byRimPalette,
                    MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);
  }
}

//-------------------------------------------------------------------------------------------------

/* A sky with no weather in it: stars over the whole sphere and one world
 * hanging in it, on a dome standing on its side. [MESH-49] */
static void mecha_mesh_starfield(tMechaQuadList *pList,
                                 const tMechaWorld *pWorld)
{
  int iTurn = pWorld->iTick * MECHA_STAR_SPIN / MECHA_CLOUD_DRIFT;
  int i;

  for (i = 0; i < MECHA_STAR_COUNT; i++) {
    uint32_t uiHash = mecha_cloud_hash((uint32_t)i * 7u + pWorld->uiSeed);
    /* Over the whole sphere, not banked towards one edge of it: a starfield
     * has no horizon to crowd against. */
    int iElevation = (int)((uiHash >> 12) & (uint32_t)(MECHA_ANGLE_FULL - 1));
    int iAzimuth = mecha_angle_wrap(
        (int)(uiHash & (uint32_t)(MECHA_ANGLE_FULL - 1)) + iTurn);
    float fSize = MECHA_CLOUD_RADIUS
                  * (MECHA_STAR_SIZE
                     + MECHA_STAR_VARY * (float)((uiHash >> 24) & 255u)
                       / 255.0f);

    mecha_sky_quad(pList, iElevation, iAzimuth, fSize, true, MECHA_PAL_STAR);
  }

  /* And the world this one is facing. It wheels with the rest of the sky,
   * which is what says the stage is the thing turning. */
  mecha_sky_disc(pList, MECHA_PLANET_HEIGHT,
                 mecha_angle_wrap(MECHA_PLANET_AZIMUTH + iTurn),
                 MECHA_CLOUD_RADIUS * MECHA_PLANET_SIZE,
                 MECHA_PAL_PLANET, MECHA_PAL_PLANET_RIM);
}

//-------------------------------------------------------------------------------------------------

void mecha_mesh_clouds(tMechaQuadList *pList, const tMechaWorld *pWorld)
{
  mecha_quads_part(pList, MECHA_PART_NONE);
  int i;

  if (!pList || !pWorld)
    return;

  if (pWorld->arena.bySkyKind == MECHA_SKY_STARFIELD) {
    mecha_mesh_starfield(pList, pWorld);
    return;
  }

  /* Weather needs the artwork; a checkout with no retail data simply has a
   * clear sky. */
  if (!s_bSprites)
    return;

  for (i = 0; i < MECHA_CLOUD_COUNT; i++) {
    /* Seeded off the match, so every arena hangs under its own sky and the
     * same match always gets the same one. */
    uint32_t uiHash = mecha_cloud_hash((uint32_t)i * 3u + pWorld->uiSeed);
    int iAzimuth;
    int iElevation;
    float fLow;
    float fSize;
    float afDir[3];
    float afRight[3];
    float afUp[3];
    float afVert[4][3];
    float fLength;
    int iCorner;

    /* Squaring a uniform draw crowds the dome down towards the horizon,
     * which is where clouds are in any sky worth looking at and also where
     * they do the most work: a band of them along the skyline is what gives
     * a flat fill a distance. */
    fLow = (float)((uiHash >> 14) & 1023u) / 1024.0f;
    iElevation = MECHA_CLOUD_FLOOR
               + (int)((float)(MECHA_CLOUD_CEILING - MECHA_CLOUD_FLOOR)
                       * fLow * fLow);
    iAzimuth = mecha_angle_wrap((int)(uiHash & (uint32_t)(MECHA_ANGLE_FULL - 1))
                                + pWorld->iTick / MECHA_CLOUD_DRIFT);

    afDir[0] = mecha_cos(iElevation) * mecha_sin(iAzimuth);
    afDir[1] = mecha_sin(iElevation);
    afDir[2] = mecha_cos(iElevation) * mecha_cos(iAzimuth);

    /* Tangent to the dome, so every puff faces its middle -- which is where
     * the camera is, near enough, and is why these are not camera-facing
     * billboards: a billboard high overhead turns edge-on to a camera
     * underneath it and the sky develops holes. */
    afRight[0] = afDir[2];
    afRight[1] = 0.0f;
    afRight[2] = -afDir[0];
    fLength = mecha_length3(afRight[0], afRight[1], afRight[2]);
    if (fLength < 1e-4f)
      continue;
    afRight[0] /= fLength;
    afRight[2] /= fLength;
    afUp[0] = afDir[1] * afRight[2] - afDir[2] * afRight[1];
    afUp[1] = afDir[2] * afRight[0] - afDir[0] * afRight[2];
    afUp[2] = afDir[0] * afRight[1] - afDir[1] * afRight[0];

    fSize = MECHA_CLOUD_RADIUS
            * (0.055f + 0.045f * (float)((uiHash >> 24) & 255u) / 255.0f);

    for (iCorner = 0; iCorner < 4; iCorner++) {
      float fU = (iCorner == 0 || iCorner == 3) ? -fSize : fSize;
      float fV = iCorner < 2 ? -fSize : fSize;
      int iAxis;

      for (iAxis = 0; iAxis < 3; iAxis++) {
        afVert[iCorner][iAxis] = afDir[iAxis] * MECHA_CLOUD_RADIUS
                               + afRight[iAxis] * fU + afUp[iAxis] * fV;
      }
    }
    mecha_quads_add(pList, afVert, MECHA_PAL_CLOUD,
                    MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);
    mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                      MECHA_SPRITE_CLOUD_FIRST
                      + (int)((uiHash >> 8) % (uint32_t)(
                          MECHA_SPRITE_CLOUD_LAST
                          - MECHA_SPRITE_CLOUD_FIRST + 1)));
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * The smallest angle a shot is allowed to subtend, and the most it may be
 * inflated to get there. The projection puts a world half-extent of h at a
 * distance d on screen at h * 200 / d pixels in the 320-wide reference
 * frame, so this floor is a hair over a pixel of half-width -- call it two
 * and a half pixels across, which is the least a moving dot can be and
 * still be followed. A 0.8 m round is under that from about 50 m out, which
 * is inside the range these fights are actually held at. [MESH-48]
 */
#define MECHA_SHOT_MIN_ANGLE 0.0065f
#define MECHA_SHOT_MAX_GROW  3.2f

/* A shot's drawn half-extent: its own, or the floor above, whichever is
 * larger, and never more than a few times its own so a distant round grows
 * into a dot rather than a balloon. */
static float mecha_shot_size(float fSize, float fDist)
{
  float fFloor = fDist * MECHA_SHOT_MIN_ANGLE;
  float fCeil = fSize * MECHA_SHOT_MAX_GROW;

  if (fFloor <= fSize)
    return fSize;
  return fFloor > fCeil ? fCeil : fFloor;
}

/* The heading from the eye to a point, which is what a sprite standing at
 * that point has to be turned to. Using the camera's own heading instead
 * leaves everything away from the middle of the screen turned slightly off
 * the viewer. [MESH-48] */
static int mecha_shot_yaw(const float afEye[3], float fX, float fZ)
{
  return mecha_atan2_angle(fX - afEye[0], fZ - afEye[2]);
}

static float mecha_shot_dist(const float afEye[3], float fX, float fY,
                             float fZ)
{
  return mecha_length3(fX - afEye[0], fY - afEye[1], fZ - afEye[2]);
}

void mecha_mesh_projectiles(tMechaQuadList *pList, const tMechaWorld *pWorld,
                            int iCameraYaw, const float afEye[3])
{
  mecha_quads_part(pList, MECHA_PART_NONE);
  int i;

  if (!pList || !pWorld || !afEye)
    return;

  for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
    const tMechaProjectile *pShot = &pWorld->aProjectiles[i];
    float fDist;
    int iFaceYaw;

    if (!pShot->bActive)
      continue;

    fDist = mecha_shot_dist(afEye, pShot->fX, pShot->fY, pShot->fZ);
    iFaceYaw = mecha_shot_yaw(afEye, pShot->fX, pShot->fZ);

    switch (pShot->byKind) {
    case MECHA_PROJ_MINE: {
      /* A laid mine is a real object on the floor, not a sprite: you have to
       * be able to spot one and go round it. */
      tMechaPose pose;

      mecha_pose_build(&pose, 0, 0, 0, pShot->fX, pShot->fY, pShot->fZ, 1.0f);
      mecha_add_box(pList, &pose, 0.0f, pShot->fRadius * 0.4f, 0.0f,
                    pShot->fRadius, pShot->fRadius * 0.4f, pShot->fRadius,
                    pShot->byPalette,
                    pShot->iArmTicks > 0 ? pShot->byPalette
                                         : MECHA_PAL_TRACER_CORE,
                    pShot->iArmTicks > 0 ? 0 : MECHA_QUAD_GLOW);
      break;
    }

    case MECHA_PROJ_SHELL: {
      /*
       * The standing fireball. Drawn as a shell of puffs on its surface
       * rather than one billboard, because what has to read is where the
       * edge of it is: everything inside is being burned and everything
       * shot into it is being eaten, and a flat disc says nothing about
       * which side of that line a machine is on.
       */
      int iPuff;

      if (!s_bSprites) {
        mecha_add_billboard(pList, iFaceYaw, pShot->fX, pShot->fY,
                            pShot->fZ, pShot->fRadius, pShot->byPalette);
        break;
      }
      for (iPuff = 0; iPuff < MECHA_SHELL_PUFFS; iPuff++) {
        uint32_t uiHash = mecha_cloud_hash((uint32_t)(i * 131 + iPuff));
        int iAzimuth = (int)(uiHash & (uint32_t)(MECHA_ANGLE_FULL - 1));
        /* Spread over the sphere rather than round its waist, so it is a
         * ball from any angle. */
        int iElevation = (int)((uiHash >> 14) % (uint32_t)MECHA_ANGLE_HALF)
                         - MECHA_ANGLE_QUARTER;
        float afDir[3];
        float fSize = pShot->fRadius * 0.42f;

        afDir[0] = mecha_cos(iElevation) * mecha_sin(iAzimuth);
        afDir[1] = mecha_sin(iElevation);
        afDir[2] = mecha_cos(iElevation) * mecha_cos(iAzimuth);
        mecha_add_billboard(pList, iFaceYaw,
                            pShot->fX + afDir[0] * pShot->fRadius,
                            pShot->fY + afDir[1] * pShot->fRadius,
                            pShot->fZ + afDir[2] * pShot->fRadius,
                            fSize, pShot->byPalette);
        mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                          MECHA_SPRITE_BLAST_FIRST
                          + (int)((uiHash >> 24)
                                  % (uint32_t)(MECHA_SPRITE_BLAST_LAST
                                               - MECHA_SPRITE_BLAST_FIRST
                                               + 1)));
      }
      break;
    }

    case MECHA_PROJ_MELEE:
      /*
       * The swing itself: a blade run out along the line of the lunge from
       * the weapon that threw it, not a projectile and not a billboard.
       * Long against the hitbox it draws, because a sword that is as wide
       * as its reach is a shield.
       */
      mecha_add_blade(pList, pShot->fX, pShot->fY, pShot->fZ,
                      pShot->fVelX, pShot->fVelZ,
                      pShot->fRadius * MECHA_BLADE_REACH, pShot->byPalette);
      break;

    case MECHA_PROJ_BEAM:
      /* Streak plus head. The streak stays flat and keeps the weapon's own
       * colour, which is how a player reads whose fire it is. [MESH-27] */
      mecha_add_tracer(pList, iCameraYaw, afEye, pShot->fPrevX, pShot->fPrevY,
                       pShot->fPrevZ, pShot->fX, pShot->fY, pShot->fZ,
                       mecha_shot_size(pShot->fRadius, fDist),
                       pShot->byPalette);
      if (s_bSprites) {
        mecha_add_billboard(pList, iFaceYaw, pShot->fX, pShot->fY, pShot->fZ,
                            mecha_shot_size(pShot->fRadius * 1.8f, fDist),
                            pShot->byPalette);
        mecha_tag_texture(pList, mecha_bolt_bank(pShot->byPalette),
                          mecha_plasma_frame(pShot->iAge));
      }
      break;

    case MECHA_PROJ_BULLET:
      /*
       * Solid rounds stay solid: a slug is not made of light, so it gets no
       * plasma frame. It does get a head, which it did not before -- a bare
       * streak is one tick of travel long and vanishes the moment the round
       * is far enough away for that to be short, which is why these were
       * the shots that could not be seen coming. [MESH-48]
       */
      mecha_add_tracer(pList, iCameraYaw, afEye, pShot->fPrevX, pShot->fPrevY,
                       pShot->fPrevZ, pShot->fX, pShot->fY, pShot->fZ,
                       mecha_shot_size(pShot->fRadius, fDist),
                       pShot->byPalette);
      mecha_add_billboard(pList, iFaceYaw, pShot->fX, pShot->fY, pShot->fZ,
                          mecha_shot_size(pShot->fRadius, fDist),
                          pShot->byPalette);
      break;

    default:
      /*
       * Homing pods and lobbed charges travel slowly enough to be looked
       * at, so they are the sprite rather than carrying one. Wider than the
       * flat square they replace: most of a keyed frame is background, so
       * the same quad reads smaller once it is textured.
       */
      mecha_add_billboard(pList, iFaceYaw, pShot->fX, pShot->fY, pShot->fZ,
                          mecha_shot_size(pShot->fRadius
                                            * (s_bSprites ? 2.0f : 1.4f),
                                          fDist),
                          pShot->byPalette);
      mecha_tag_texture(pList, mecha_bolt_bank(pShot->byPalette),
                        mecha_plasma_frame(pShot->iAge));
      break;
    }
  }
}

//-------------------------------------------------------------------------------------------------

/* Where in an explosion's life it is at its widest, as a fraction. */
#define MECHA_FX_BURST_PEAK 0.33f

void mecha_mesh_effects(tMechaQuadList *pList, const tMechaWorld *pWorld,
                        int iCameraYaw)
{
  mecha_quads_part(pList, MECHA_PART_NONE);
  int i;

  if (!pList || !pWorld)
    return;

  for (i = 0; i < MECHA_MAX_EFFECTS; i++) {
    const tMechaEffect *pFx = &pWorld->aEffects[i];
    float fAge;
    float fSize;

    if (!pFx->bActive || pFx->iLife <= 0)
      continue;
    fAge = (float)pFx->iAge / (float)pFx->iLife;

    switch (pFx->byKind) {
    case MECHA_FX_EXPLOSION:
      /* Opens fast, then collapses: with no alpha, size is all that carries
       * the shape of a blast. [MESH-28] */
      fSize = fAge < MECHA_FX_BURST_PEAK
              ? pFx->fScale * (0.35f + 0.65f * (fAge / MECHA_FX_BURST_PEAK))
              : pFx->fScale * (1.0f - 0.7f * ((fAge - MECHA_FX_BURST_PEAK)
                                              / (1.0f - MECHA_FX_BURST_PEAK)));
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, pFx->byPalette);
      mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                        mecha_sprite_frame(MECHA_SPRITE_BLAST_FIRST,
                                           MECHA_SPRITE_BLAST_LAST, fAge));
      break;

    case MECHA_FX_EMBER: {
      /* Debris cools as it falls, down the sky gradient's own warm ramp --
       * the only contiguous one the palette has. [MESH-29] */
      static const uint8_t abyCool[] = {
        207, 204, 171, 170, 167, 230, 227, 224, 221
      };
      const int iSteps = (int)(sizeof(abyCool) / sizeof(abyCool[0]));
      int iStep = (int)(fAge * (float)iSteps);

      if (iStep < 0)
        iStep = 0;
      if (iStep >= iSteps)
        iStep = iSteps - 1;
      /* Shrinking as well as cooling, so the last frames are embers rather
       * than full-size squares blinking out. */
      fSize = pFx->fScale * (1.0f - 0.55f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, abyCool[iStep]);
      /* Alight for the first half of its life, smoke for the rest. */
      mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                        fAge < 0.5f
                          ? mecha_sprite_frame(MECHA_SPRITE_FIRE_FIRST,
                                               MECHA_SPRITE_FIRE_LAST,
                                               fAge * 2.0f)
                          : mecha_sprite_frame(MECHA_SPRITE_SMOKE_FIRST,
                                               MECHA_SPRITE_SMOKE_LAST,
                                               (fAge - 0.5f) * 2.0f));
      break;
    }

    case MECHA_FX_DUST:
      /*
       * A landing throws dust outwards, not upwards: a ring of flat puffs
       * sliding away from the feet along the ground, each one a frame of
       * the smoke sequence. One expanding square was the old version of
       * this, and it read as a stain spreading rather than as anything
       * being kicked up.
       */
      if (s_bSprites) {
        int iPuff;
        /* Thins out towards the end rather than vanishing at full size. */
        float fFade = fAge < 0.6f ? 1.0f : 1.0f - (fAge - 0.6f) / 0.4f;
        float fRing = pFx->fScale * (0.20f + 1.30f * fAge);
        float fPuff = pFx->fScale * (0.34f + 0.20f * fAge) * fFade;

        for (iPuff = 0; iPuff < MECHA_DUST_PUFFS; iPuff++) {
          /* Spaced evenly and then jittered off the spokes, so a landing
           * does not read as a cog. The jitter is a hash of the effect
           * slot, so it holds still for the life of the puff. */
          uint32_t uiHash = mecha_cloud_hash((uint32_t)(i * 31 + iPuff));
          int iStep = MECHA_ANGLE_FULL / MECHA_DUST_PUFFS;
          int iAngle = mecha_angle_wrap(iPuff * iStep
                                        + (int)(uiHash % (uint32_t)iStep));
          float fPx = pFx->fX + mecha_sin(iAngle) * fRing;
          float fPz = pFx->fZ + mecha_cos(iAngle) * fRing;
          /* A millimetre apiece, so overlapping puffs are never in exactly
           * the same plane fighting over which is on top. */
          float fY = pFx->fY + (0.06f + 0.004f * (float)iPuff)
                               * MECHA_METRE;

          if (fPuff <= 0.0f)
            break;
          mecha_add_floor_quad(pList, fPx - fPuff, fPz - fPuff,
                               fPx + fPuff, fPz + fPuff, fY, MECHA_PAL_SMOKE,
                               MECHA_QUAD_TWO_SIDED | MECHA_QUAD_DECAL);
          mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                            mecha_sprite_frame(MECHA_SPRITE_SMOKE_FIRST,
                                               MECHA_SPRITE_SMOKE_LAST,
                                               fAge));
        }
        break;
      }
      /* No bank: the old stain, which at least says something happened.
       * Darkens the ground rather than painting on it, so the shade level
       * goes in the low byte -- the effect's own colour would be read as a
       * level and index far past the end of shade_palette. */
      fSize = pFx->fScale * (0.5f + fAge);
      mecha_add_floor_quad(pList, pFx->fX - fSize, pFx->fZ - fSize,
                           pFx->fX + fSize, pFx->fZ + fSize,
                           pFx->fY + 0.08f * MECHA_METRE, MECHA_SHADE_DUST,
                           MECHA_QUAD_TWO_SIDED | MECHA_QUAD_SHADOW);
      break;

    case MECHA_FX_MUZZLE:
      /* The flash at the barrel is the front of the shot, so it is drawn
       * from the same sequence the shot is -- otherwise a bolt leaves a
       * flat square behind it every time one is fired. */
      fSize = pFx->fScale * (1.0f - 0.6f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, pFx->byPalette);
      mecha_tag_texture(pList, mecha_bolt_bank(pFx->byPalette),
                        mecha_plasma_frame(pFx->iAge));
      break;

    case MECHA_FX_IMPACT:
      /* A hit is a small explosion and walks the blast frames like one, just
       * over a much shorter life. */
      fSize = pFx->fScale * (1.0f - 0.6f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, pFx->byPalette);
      mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                        mecha_sprite_frame(MECHA_SPRITE_BLAST_FIRST,
                                           MECHA_SPRITE_BLAST_LAST, fAge));
      break;

    case MECHA_FX_SMOKE: {
      /*
       * Damage smoke: it grows and thins rather than shrinking, because
       * something pouring out of a machine spreads as it leaves. Palette
       * walks a grey ramp so a puff goes from dirty to faint instead of
       * blinking out at full strength.
       */
      static const uint8_t abyFade[] = { 121, 123, 125, 127, 129 };
      const int iSteps = (int)(sizeof(abyFade) / sizeof(abyFade[0]));
      int iStep = mecha_clampi((int)(fAge * (float)iSteps), 0, iSteps - 1);

      fSize = pFx->fScale * (0.7f + 1.1f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, abyFade[iStep]);
      mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                        mecha_sprite_frame(MECHA_SPRITE_SMOKE_FIRST,
                                           MECHA_SPRITE_SMOKE_LAST, fAge));
      break;
    }

    case MECHA_FX_THRUSTER: {
      /*
       * Burning fuel, not a bolt: the fire frames cycle, because a boost
       * outlasts one pass. Drawn as two mirrored halves so the flame is
       * symmetrical about its thruster. [MESH-30]
       */
      int iFrame = mecha_sprite_frame(MECHA_SPRITE_FIRE_FIRST,
                                      MECHA_SPRITE_FIRE_LAST, fAge);
      int iHalf;

      fSize = pFx->fScale * (1.0f - 0.6f * fAge);
      for (iHalf = 0; iHalf < 2; iHalf++) {
        mecha_add_billboard_half(pList, iCameraYaw, pFx->fX, pFx->fY,
                                 pFx->fZ, fSize * 0.5f, fSize,
                                 (iHalf == 0 ? -0.5f : 0.5f) * fSize,
                                 iHalf != 0, pFx->byPalette);
        mecha_tag_texture(pList, MECHA_TEX_EFFECT, iFrame);
      }
      break;
    }

    default:
      fSize = pFx->fScale * (1.0f - 0.6f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, pFx->byPalette);
      break;
    }
  }
}
