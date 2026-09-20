#include <stddef.h>
#include "mecha_defs.h"

//-------------------------------------------------------------------------------------------------
/*
 * Tracer and hull palette indices.
 *
 * The tracer entries are the ones ROLLER's own firework table already uses
 * (function.c), so they are known-bright in the stock palette. The hull
 * entries are tuned by eye against the same palette; every colour the mode
 * paints with is named here or in mecha_arena.c so a retune stays local.
 */
/*
 * Measured against the game's own PALETTE.PAL, not chosen to look right in
 * the mode's fallback table. Several of these were picked before the retail
 * palette was ever loaded and were badly wrong in it -- the "green" tracer
 * came out blue, "amber" magenta and "orange" pink -- which nothing noticed
 * while the mode only ever drew through its own colours.
 *
 * Every one is the top of one of the palette's pure-hue ramps, and no hue
 * here is one the arena itself is painted in: the terrain owns grey, green
 * and brown, so weapon fire owns everything else. The comment beside each
 * is what the retail palette actually holds there. [DEF-12]
 */
#define PAL_TRACER_ORANGE  171   /* 255,125,  0 */
#define PAL_TRACER_YELLOW  207   /* 255,255,  0 */
#define PAL_TRACER_MAGENTA 195   /* 255,  0,255 */
#define PAL_TRACER_ROSE    183   /* 255,  0,113 */
#define PAL_TRACER_CYAN    219   /*   0,255,255 */
#define PAL_TRACER_RED     231   /* 255,  0,  0 */
/*
 * White is a blade and nothing else. It is the one colour here a grey arena
 * can swallow -- against a pale block top it is the least visible thing the
 * roster paints with -- and a sword is swung at arm's length where that does
 * not matter. Nothing that has to be spotted crossing the arena is white.
 * [DEF-12]
 */
#define PAL_TRACER_WHITE   143   /* 255,255,255 */

#define PAL_HULL_STEEL    128
#define PAL_HULL_STEEL_T  136
#define PAL_HULL_IRON     125
#define PAL_HULL_IRON_T   18
#define PAL_HULL_PALE     137
#define PAL_HULL_PALE_T   141
#define PAL_HULL_DARK     119
#define PAL_HULL_DARK_T   127
#define PAL_JOINT         105

//-------------------------------------------------------------------------------------------------

/*
 * Paint schemes, named for the makes that built the race game's cars.
 *
 * The names are Whiplash's own, out of CompanyNames in carplans.c. The
 * colours are not: nothing in the retail data carries a flat colour per car
 * -- tCarDesign is geometry, and car_flat_remap is a mirror remap -- so
 * these are index pairs picked out of the same palette ramps the roster's
 * own machines use, which is what keeps a repainted machine looking like it
 * belongs in the same arena. Scheme zero is the machine's own. [DEF-06]
 */
static const tMechaScheme s_aSchemes[] = {
  { "WORKS",             0,                 0,                0 },
  { "AUTO ARIEL",        PAL_HULL_PALE,     PAL_TRACER_CYAN,  PAL_JOINT },
  { "DESILVA",           PAL_HULL_DARK,     PAL_TRACER_YELLOW, PAL_JOINT },
  { "PULSE ENGINEERING", PAL_HULL_STEEL,    PAL_TRACER_MAGENTA,PAL_JOINT },
  { "GLOBAL",            PAL_HULL_IRON,     PAL_TRACER_ROSE, PAL_JOINT },
  { "MILLION PLUS",      PAL_HULL_PALE_T,   PAL_TRACER_YELLOW,  PAL_JOINT },
  { "MISSION MOTORS",    PAL_HULL_DARK_T,   PAL_TRACER_RED,   PAL_JOINT },
  { "ZIZIN",             PAL_HULL_PALE,     PAL_HULL_DARK,    PAL_JOINT },
  { "REISE WAGON",       PAL_HULL_STEEL_T,  PAL_TRACER_WHITE, PAL_JOINT },
  { "DRH MOTORS",        PAL_HULL_IRON_T,   PAL_TRACER_ORANGE,PAL_JOINT },
  { "GODLEY PLC",        PAL_HULL_DARK,     PAL_TRACER_ROSE, PAL_JOINT },
  { "GSS AUTOS",         PAL_HULL_STEEL,    PAL_TRACER_YELLOW,  PAL_JOINT },
  { "CROSS CARS",        PAL_HULL_PALE_T,   PAL_TRACER_RED,   PAL_JOINT },
  { "VRANIC",            PAL_HULL_IRON,     PAL_TRACER_MAGENTA,PAL_JOINT },
  { "DEATH MOTORS",      PAL_HULL_DARK_T,   PAL_TRACER_YELLOW, PAL_JOINT },
};

#define MECHA_SCHEME_COUNT \
  ((int)(sizeof(s_aSchemes) / sizeof(s_aSchemes[0])))

int mecha_scheme_count(void)
{
  return MECHA_SCHEME_COUNT;
}

const tMechaScheme *mecha_scheme_get(int iScheme)
{
  if (iScheme <= 0 || iScheme >= MECHA_SCHEME_COUNT)
    return NULL;
  return &s_aSchemes[iScheme];
}

const char *mecha_scheme_name(int iScheme)
{
  if (iScheme < 0 || iScheme >= MECHA_SCHEME_COUNT)
    return s_aSchemes[0].szName;
  return s_aSchemes[iScheme].szName;
}

//-------------------------------------------------------------------------------------------------

static const tMechaMechDef s_aMechDefs[] = {

//-------------------------------------------------------------------------------------------------
{
  .szName = "Lancer G", .szClass = "LINE ASSAULT",
  .fGrip = MECHA_MPS(90.0f), .fDriveAccel = MECHA_MPS(70.0f),
  .fBrake = MECHA_MPS(60.0f),
  .fBuildShoulder = 1.00f, .fBuildTorso = 1.00f, .fBuildLimb = 1.00f,
  .fBuildHead = 1.00f, .fBuildGun = 1.00f,
  .fHeight = MECHA_M(14.0f), .fRadius = MECHA_M(3.2f), .fMass = 1.0f,
  .fArmour = 1000.0f,
  .fWalkSpeed = MECHA_MPS(20.0f), .fDashSpeed = MECHA_MPS(56.0f),
  .fAirSpeed = MECHA_MPS(34.0f), .fTurnRate = (float)MECHA_DEG(200),
  .fJumpVelocity = MECHA_MPS(27.0f),
  .iBoostMax = 1000, .iBoostDashDrain = 340, .iBoostJumpCost = 90,
  .iBoostJumpDrain = 260, .iBoostRegen = 150, .iBoostGuardRegen = 460,
  .iDashTicks = MECHA_SEC(0.75f), .iLandTicks = MECHA_SEC(0.28f),
  .abyPalette = { PAL_HULL_STEEL, PAL_HULL_STEEL_T, PAL_JOINT, PAL_TRACER_CYAN },
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "SCATTER", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(4),
        .fSpeed = MECHA_MPS(120.0f), .fDamage = 32.0f, .fRadius = MECHA_M(0.8f),
        .iLifeTicks = 90, .iAmmo = 6, .iReloadTicks = MECHA_SEC(2.0f),
        .iRecoveryTicks = 14, .fStagger = 8.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = -1.1f },
      [MECHA_STANCE_GUARD] = { .szName = "SCATTER LONG", .byKind = MECHA_PROJ_BULLET,
        .byCount = 5, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(2),
        .fSpeed = MECHA_MPS(155.0f), .fDamage = 30.0f, .fRadius = MECHA_M(0.8f),
        .iLifeTicks = 130, .iAmmo = 4, .iReloadTicks = MECHA_SEC(2.6f),
        .iRecoveryTicks = 26, .fStagger = 9.0f,
        .fMuzzleHeight = 0.44f, .fMuzzleSide = -1.1f },
      [MECHA_STANCE_DASH] = { .szName = "SCATTER RUN", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(7),
        .fSpeed = MECHA_MPS(110.0f), .fDamage = 26.0f, .fRadius = MECHA_M(0.8f),
        .iLifeTicks = 70, .iAmmo = 8, .iReloadTicks = MECHA_SEC(1.8f),
        .iRecoveryTicks = 8, .fStagger = 5.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = -1.1f },
      [MECHA_STANCE_JUMP] = { .szName = "SCATTER RAIN", .byKind = MECHA_PROJ_ARC,
        .byCount = 4, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(6),
        .fSpeed = MECHA_MPS(75.0f), .fDamage = 28.0f, .fRadius = MECHA_M(1.0f),
        .fArcGravity = MECHA_MPS(30.0f), .iLifeTicks = 150, .iAmmo = 5,
        .iReloadTicks = MECHA_SEC(2.2f), .iRecoveryTicks = 18, .fStagger = 7.0f,
        .fMuzzleHeight = 0.58f, .fMuzzleSide = -1.1f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "LANCE RIFLE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_CYAN,
        .fSpeed = MECHA_MPS(190.0f), .fDamage = 78.0f, .fRadius = MECHA_M(1.0f),
        .iLifeTicks = 150, .iAmmo = 4, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 22, .fStagger = 26.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "LANCE CHARGE", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_CYAN,
        .fSpeed = MECHA_MPS(420.0f), .fDamage = 148.0f, .fRadius = MECHA_M(1.6f),
        .iLifeTicks = 60, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.6f),
        .iRecoveryTicks = 46, .fStagger = 62.0f,
        .fMuzzleHeight = 0.52f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "LANCE SNAP", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(2),
        .fSpeed = MECHA_MPS(175.0f), .fDamage = 44.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 110, .iAmmo = 5, .iReloadTicks = MECHA_SEC(2.0f),
        .iRecoveryTicks = 12, .fStagger = 14.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "LANCE DIVE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(3),
        .fSpeed = MECHA_MPS(165.0f), .fDamage = 52.0f, .fRadius = MECHA_M(1.0f),
        .iLifeTicks = 120, .iAmmo = 3, .iReloadTicks = MECHA_SEC(2.8f),
        .iRecoveryTicks = 20, .fStagger = 20.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      [MECHA_STANCE_STAND] = { .szName = "ARC BOMB", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(95.0f), .fDamage = 96.0f, .fRadius = MECHA_M(1.4f),
        .fBlastRadius = MECHA_M(11.0f), .fArcGravity = MECHA_MPS(32.0f),
        .iLifeTicks = 200, .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 30, .fStagger = 46.0f,
        .fMuzzleHeight = 0.60f, .fMuzzleSide = 1.1f },
      [MECHA_STANCE_GUARD] = { .szName = "SEED MINE", .byKind = MECHA_PROJ_MINE,
        .byCount = 2, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(14),
        .fSpeed = MECHA_MPS(48.0f), .fDamage = 110.0f, .fRadius = MECHA_M(2.0f),
        .fBlastRadius = MECHA_M(13.0f), .fArcGravity = MECHA_MPS(34.0f),
        .iLifeTicks = MECHA_SEC(9.0f), .iAmmo = 2, .iReloadTicks = MECHA_SEC(4.0f),
        .iRecoveryTicks = 26, .fStagger = 54.0f,
        .fMuzzleHeight = 0.36f, .fMuzzleSide = 1.1f },
      [MECHA_STANCE_DASH] = { .szName = "RAM BLADE", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(58.0f), .fDamage = 132.0f, .fRadius = MECHA_M(5.0f),
        .iLifeTicks = 16, .iAmmo = 2, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 26, .fStagger = 74.0f,
        .fMuzzleHeight = 0.55f, .fMuzzleSide = 1.1f },
      [MECHA_STANCE_JUMP] = { .szName = "CLUSTER DROP", .byKind = MECHA_PROJ_ARC,
        .byCount = 3, .byPalette = PAL_TRACER_ORANGE, .iSpreadAngle = MECHA_DEG(9),
        .fSpeed = MECHA_MPS(58.0f), .fDamage = 70.0f, .fRadius = MECHA_M(1.4f),
        .fBlastRadius = MECHA_M(9.0f), .fArcGravity = MECHA_MPS(38.0f),
        .iLifeTicks = 180, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.4f),
        .iRecoveryTicks = 24, .fStagger = 40.0f,
        .fMuzzleHeight = 0.30f, .fMuzzleSide = 1.1f },
    },
  },
},

//-------------------------------------------------------------------------------------------------
{
  .szName = "SJ Mk.IV", .szClass = "SIEGE PLATFORM",
  /* Carries its mass: slow to gather speed, slower to shed it, and it
   * slides a long way out of anything taken quickly. */
  .fGrip = MECHA_MPS(34.0f), .fDriveAccel = MECHA_MPS(30.0f),
  .fBrake = MECHA_MPS(24.0f),
  /* The bruiser: everything wide, everything heavy, and a gun on each
   * arm you could not possibly run with. */
  .fBuildShoulder = 1.50f, .fBuildTorso = 1.30f, .fBuildLimb = 1.34f,
  .fBuildHead = 0.78f, .fBuildGun = 2.00f,
  .fHeight = MECHA_M(17.0f), .fRadius = MECHA_M(4.2f), .fMass = 1.7f,
  .fArmour = 1450.0f,
  .fWalkSpeed = MECHA_MPS(13.0f), .fDashSpeed = MECHA_MPS(43.0f),
  .fAirSpeed = MECHA_MPS(24.0f), .fTurnRate = (float)MECHA_DEG(140),
  .fJumpVelocity = MECHA_MPS(20.0f),
  .iBoostMax = 1150, .iBoostDashDrain = 400, .iBoostJumpCost = 140,
  .iBoostJumpDrain = 330, .iBoostRegen = 130, .iBoostGuardRegen = 420,
  .iDashTicks = MECHA_SEC(0.60f), .iLandTicks = MECHA_SEC(0.45f),
  .abyPalette = { PAL_HULL_IRON, PAL_HULL_IRON_T, PAL_JOINT, PAL_TRACER_ORANGE },
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "AUTOCANNON", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(2),
        .fSpeed = MECHA_MPS(140.0f), .fDamage = 30.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 110, .iAmmo = 10, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 9, .fStagger = 7.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = -1.2f },
      [MECHA_STANCE_GUARD] = { .szName = "SUPPRESS", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(3),
        .fSpeed = MECHA_MPS(160.0f), .fDamage = 34.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 150, .iAmmo = 12, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 10, .fStagger = 9.0f,
        .fMuzzleHeight = 0.46f, .fMuzzleSide = -1.2f },
      [MECHA_STANCE_DASH] = { .szName = "BURST", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(6),
        .fSpeed = MECHA_MPS(125.0f), .fDamage = 26.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 80, .iAmmo = 8, .iReloadTicks = MECHA_SEC(2.2f),
        .iRecoveryTicks = 8, .fStagger = 6.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = -1.2f },
      [MECHA_STANCE_JUMP] = { .szName = "FLAK", .byKind = MECHA_PROJ_BULLET,
        .byCount = 5, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(115.0f), .fDamage = 24.0f, .fRadius = MECHA_M(1.1f),
        .fBlastRadius = MECHA_M(5.0f), .iLifeTicks = 90, .iAmmo = 4,
        .iReloadTicks = MECHA_SEC(2.8f), .iRecoveryTicks = 16, .fStagger = 10.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = -1.2f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "SIEGE SHELL", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(150.0f), .fDamage = 118.0f, .fRadius = MECHA_M(1.5f),
        .fBlastRadius = MECHA_M(9.0f), .iLifeTicks = 160, .iAmmo = 3,
        .iReloadTicks = MECHA_SEC(3.2f), .iRecoveryTicks = 34, .fStagger = 52.0f,
        .fMuzzleHeight = 0.72f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "SIEGE LANCE", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(400.0f), .fDamage = 205.0f, .fRadius = MECHA_M(2.2f),
        .iLifeTicks = 70, .iAmmo = 1, .iReloadTicks = MECHA_SEC(5.0f),
        .iRecoveryTicks = 70, .fStagger = 96.0f,
        .fMuzzleHeight = 0.50f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "ROLL SHELL", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(140.0f), .fDamage = 66.0f, .fRadius = MECHA_M(1.3f),
        .fBlastRadius = MECHA_M(6.0f), .iLifeTicks = 110, .iAmmo = 3,
        .iReloadTicks = MECHA_SEC(2.6f), .iRecoveryTicks = 20, .fStagger = 28.0f,
        .fMuzzleHeight = 0.72f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "MORTAR", .byKind = MECHA_PROJ_ARC,
        .byCount = 2, .byPalette = PAL_TRACER_ORANGE, .iSpreadAngle = MECHA_DEG(7),
        .fSpeed = MECHA_MPS(70.0f), .fDamage = 92.0f, .fRadius = MECHA_M(1.6f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(30.0f),
        .iLifeTicks = 200, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.6f),
        .iRecoveryTicks = 28, .fStagger = 48.0f,
        .fMuzzleHeight = 0.60f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      [MECHA_STANCE_STAND] = { .szName = "POD SALVO", .byKind = MECHA_PROJ_HOMING,
        .byCount = 4, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(11),
        .fSpeed = MECHA_MPS(72.0f), .fDamage = 42.0f, .fRadius = MECHA_M(1.1f),
        .fBlastRadius = MECHA_M(6.0f), .iLifeTicks = 190, .iAmmo = 2,
        .iReloadTicks = MECHA_SEC(3.4f), .iRecoveryTicks = 24,
        .iHomingRate = MECHA_DEG(1.7f), .fStagger = 18.0f,
        .fMuzzleHeight = 0.80f, .fMuzzleSide = 1.2f },
      [MECHA_STANCE_GUARD] = { .szName = "POD STORM", .byKind = MECHA_PROJ_HOMING,
        .byCount = 8, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(8),
        .fSpeed = MECHA_MPS(64.0f), .fDamage = 38.0f, .fRadius = MECHA_M(1.1f),
        .fBlastRadius = MECHA_M(6.0f), .iLifeTicks = 230, .iAmmo = 1,
        .iReloadTicks = MECHA_SEC(5.2f), .iRecoveryTicks = 44,
        .iHomingRate = MECHA_DEG(2.1f), .fStagger = 16.0f,
        .fMuzzleHeight = 0.58f, .fMuzzleSide = 1.2f },
      [MECHA_STANCE_DASH] = { .szName = "SHOULDER RAM", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(46.0f), .fDamage = 158.0f, .fRadius = MECHA_M(6.0f),
        .iLifeTicks = 20, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 34, .fStagger = 88.0f,
        .fMuzzleHeight = 0.50f, .fMuzzleSide = 1.2f },
      [MECHA_STANCE_JUMP] = { .szName = "MINE FIELD", .byKind = MECHA_PROJ_MINE,
        .byCount = 4, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(20),
        .fSpeed = MECHA_MPS(40.0f), .fDamage = 88.0f, .fRadius = MECHA_M(2.0f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(34.0f),
        .iLifeTicks = MECHA_SEC(10.0f), .iAmmo = 1, .iReloadTicks = MECHA_SEC(5.0f),
        .iRecoveryTicks = 30, .fStagger = 50.0f,
        .fMuzzleHeight = 0.34f, .fMuzzleSide = 1.2f },
    },
  },
},

//-------------------------------------------------------------------------------------------------
{
  .szName = "Exos 2000", .szClass = "FAST INTERCEPT",
  /* Almost no weight to fight: changes direction nearly on the spot. */
  .fGrip = MECHA_MPS(190.0f), .fDriveAccel = MECHA_MPS(150.0f),
  .fBrake = MECHA_MPS(130.0f),
  /* All silhouette and no mass: narrow shoulders, thin legs, and a head
   * that actually clears them. */
  .fBuildShoulder = 0.78f, .fBuildTorso = 0.80f, .fBuildLimb = 0.72f,
  .fBuildHead = 1.20f, .fBuildGun = 0.66f,
  .fHeight = MECHA_M(12.5f), .fRadius = MECHA_M(2.6f), .fMass = 0.7f,
  .fArmour = 780.0f,
  .fWalkSpeed = MECHA_MPS(25.0f), .fDashSpeed = MECHA_MPS(72.0f),
  .fAirSpeed = MECHA_MPS(42.0f), .fTurnRate = (float)MECHA_DEG(260),
  .fJumpVelocity = MECHA_MPS(33.0f),
  .iBoostMax = 900, .iBoostDashDrain = 300, .iBoostJumpCost = 70,
  .iBoostJumpDrain = 210, .iBoostRegen = 175, .iBoostGuardRegen = 520,
  .iDashTicks = MECHA_SEC(0.90f), .iLandTicks = MECHA_SEC(0.20f),
  .abyPalette = { PAL_HULL_PALE, PAL_HULL_PALE_T, PAL_JOINT, PAL_TRACER_MAGENTA },
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "NEEDLE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(2),
        .fSpeed = MECHA_MPS(200.0f), .fDamage = 26.0f, .fRadius = MECHA_M(0.6f),
        .iLifeTicks = 110, .iAmmo = 10, .iReloadTicks = MECHA_SEC(1.9f),
        .iRecoveryTicks = 7, .fStagger = 5.0f,
        .fMuzzleHeight = 0.64f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "NEEDLE FOCUS", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_MAGENTA,
        .fSpeed = MECHA_MPS(380.0f), .fDamage = 96.0f, .fRadius = MECHA_M(1.0f),
        .iLifeTicks = 60, .iAmmo = 3, .iReloadTicks = MECHA_SEC(2.8f),
        .iRecoveryTicks = 30, .fStagger = 34.0f,
        .fMuzzleHeight = 0.46f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_DASH] = { .szName = "NEEDLE STRAFE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(4),
        .fSpeed = MECHA_MPS(185.0f), .fDamage = 22.0f, .fRadius = MECHA_M(0.6f),
        .iLifeTicks = 90, .iAmmo = 12, .iReloadTicks = MECHA_SEC(1.7f),
        .iRecoveryTicks = 6, .fStagger = 4.0f,
        .fMuzzleHeight = 0.64f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "NEEDLE RAIN", .byKind = MECHA_PROJ_ARC,
        .byCount = 6, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(95.0f), .fDamage = 24.0f, .fRadius = MECHA_M(0.7f),
        .fArcGravity = MECHA_MPS(26.0f), .iLifeTicks = 140, .iAmmo = 4,
        .iReloadTicks = MECHA_SEC(2.4f), .iRecoveryTicks = 14, .fStagger = 5.0f,
        .fMuzzleHeight = 0.60f, .fMuzzleSide = -1.0f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "ARC BEAM", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_CYAN,
        .fSpeed = MECHA_MPS(430.0f), .fDamage = 82.0f, .fRadius = MECHA_M(1.2f),
        .iLifeTicks = 55, .iAmmo = 4, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 20, .fStagger = 28.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "ARC PIERCE", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_MAGENTA,
        .fSpeed = MECHA_MPS(470.0f), .fDamage = 138.0f, .fRadius = MECHA_M(1.5f),
        .iLifeTicks = 75, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.4f),
        .iRecoveryTicks = 40, .fStagger = 56.0f,
        .fMuzzleHeight = 0.50f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "ARC SNAP", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_CYAN,
        .fSpeed = MECHA_MPS(430.0f), .fDamage = 54.0f, .fRadius = MECHA_M(1.0f),
        .iLifeTicks = 50, .iAmmo = 5, .iReloadTicks = MECHA_SEC(2.0f),
        .iRecoveryTicks = 10, .fStagger = 16.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "ARC SWEEP", .byKind = MECHA_PROJ_BEAM,
        .byCount = 3, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(400.0f), .fDamage = 48.0f, .fRadius = MECHA_M(1.0f),
        .iLifeTicks = 55, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 22, .fStagger = 18.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      [MECHA_STANCE_STAND] = { .szName = "TRACKER", .byKind = MECHA_PROJ_HOMING,
        .byCount = 2, .byPalette = PAL_TRACER_ROSE, .iSpreadAngle = MECHA_DEG(13),
        .fSpeed = MECHA_MPS(88.0f), .fDamage = 52.0f, .fRadius = MECHA_M(1.0f),
        .fBlastRadius = MECHA_M(5.0f), .iLifeTicks = 180, .iAmmo = 3,
        .iReloadTicks = MECHA_SEC(2.8f), .iRecoveryTicks = 18,
        .iHomingRate = MECHA_DEG(2.6f), .fStagger = 20.0f,
        .fMuzzleHeight = 0.78f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "TRACKER LOCK", .byKind = MECHA_PROJ_HOMING,
        .byCount = 4, .byPalette = PAL_TRACER_ROSE, .iSpreadAngle = MECHA_DEG(9),
        .fSpeed = MECHA_MPS(80.0f), .fDamage = 56.0f, .fRadius = MECHA_M(1.0f),
        .fBlastRadius = MECHA_M(6.0f), .iLifeTicks = 220, .iAmmo = 2,
        .iReloadTicks = MECHA_SEC(3.8f), .iRecoveryTicks = 30,
        .iHomingRate = MECHA_DEG(3.2f), .fStagger = 22.0f,
        .fMuzzleHeight = 0.56f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_DASH] = { .szName = "WING BLADE", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(70.0f), .fDamage = 104.0f, .fRadius = MECHA_M(4.4f),
        .iLifeTicks = 14, .iAmmo = 3, .iReloadTicks = MECHA_SEC(2.0f),
        .iRecoveryTicks = 20, .fStagger = 64.0f,
        .fMuzzleHeight = 0.55f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "TRACKER RAIN", .byKind = MECHA_PROJ_HOMING,
        .byCount = 5, .byPalette = PAL_TRACER_ROSE, .iSpreadAngle = MECHA_DEG(15),
        .fSpeed = MECHA_MPS(76.0f), .fDamage = 40.0f, .fRadius = MECHA_M(1.0f),
        .fBlastRadius = MECHA_M(5.0f), .iLifeTicks = 200, .iAmmo = 2,
        .iReloadTicks = MECHA_SEC(3.4f), .iRecoveryTicks = 22,
        .iHomingRate = MECHA_DEG(2.9f), .fStagger = 16.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 1.0f },
    },
  },
},

//-------------------------------------------------------------------------------------------------
{
  .szName = "Kira Type R", .szClass = "CLOSE QUARTERS",
  .fGrip = MECHA_MPS(120.0f), .fDriveAccel = MECHA_MPS(100.0f),
  .fBrake = MECHA_MPS(85.0f),
  .fBuildShoulder = 0.90f, .fBuildTorso = 0.94f, .fBuildLimb = 0.84f,
  .fBuildHead = 1.06f, .fBuildGun = 0.82f,
  .fHeight = MECHA_M(13.5f), .fRadius = MECHA_M(3.0f), .fMass = 0.9f,
  .fArmour = 900.0f,
  .fWalkSpeed = MECHA_MPS(23.0f), .fDashSpeed = MECHA_MPS(80.0f),
  .fAirSpeed = MECHA_MPS(37.0f), .fTurnRate = (float)MECHA_DEG(230),
  .fJumpVelocity = MECHA_MPS(29.0f),
  .iBoostMax = 1000, .iBoostDashDrain = 330, .iBoostJumpCost = 80,
  .iBoostJumpDrain = 240, .iBoostRegen = 160, .iBoostGuardRegen = 500,
  .iDashTicks = MECHA_SEC(0.80f), .iLandTicks = MECHA_SEC(0.24f),
  .abyPalette = { PAL_HULL_DARK, PAL_HULL_DARK_T, PAL_JOINT, PAL_TRACER_RED },
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "SIDEARM", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(3),
        .fSpeed = MECHA_MPS(150.0f), .fDamage = 22.0f, .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 80, .iAmmo = 9, .iReloadTicks = MECHA_SEC(1.8f),
        .iRecoveryTicks = 8, .fStagger = 5.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "SIDEARM AIMED", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(1),
        .fSpeed = MECHA_MPS(195.0f), .fDamage = 44.0f, .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 140, .iAmmo = 6, .iReloadTicks = MECHA_SEC(2.2f),
        .iRecoveryTicks = 16, .fStagger = 12.0f,
        .fMuzzleHeight = 0.44f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_DASH] = { .szName = "SIDEARM RUN", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(140.0f), .fDamage = 20.0f, .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 70, .iAmmo = 10, .iReloadTicks = MECHA_SEC(1.6f),
        .iRecoveryTicks = 6, .fStagger = 4.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "SIDEARM FAN", .byKind = MECHA_PROJ_BULLET,
        .byCount = 4, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(6),
        .fSpeed = MECHA_MPS(135.0f), .fDamage = 24.0f, .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 80, .iAmmo = 5, .iReloadTicks = MECHA_SEC(2.2f),
        .iRecoveryTicks = 12, .fStagger = 6.0f,
        .fMuzzleHeight = 0.60f, .fMuzzleSide = -1.0f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "SABRE SLASH", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(34.0f), .fDamage = 118.0f, .fRadius = MECHA_M(5.2f),
        .iLifeTicks = 14, .iAmmo = 3, .iReloadTicks = MECHA_SEC(1.8f),
        .iRecoveryTicks = 20, .fStagger = 58.0f,
        .fMuzzleHeight = 0.60f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "SABRE RISE", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(26.0f), .fDamage = 146.0f, .fRadius = MECHA_M(4.6f),
        .iLifeTicks = 18, .iAmmo = 2, .iReloadTicks = MECHA_SEC(2.6f),
        .iRecoveryTicks = 32, .fStagger = 104.0f,
        .fMuzzleHeight = 0.40f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "SABRE LUNGE", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(96.0f), .fDamage = 172.0f, .fRadius = MECHA_M(5.6f),
        .iLifeTicks = 26, .iAmmo = 2, .iReloadTicks = MECHA_SEC(2.6f),
        .iRecoveryTicks = 30, .fStagger = 96.0f,
        .fMuzzleHeight = 0.55f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "SABRE DIVE", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(74.0f), .fDamage = 154.0f, .fRadius = MECHA_M(5.0f),
        .iLifeTicks = 24, .iAmmo = 2, .iReloadTicks = MECHA_SEC(2.8f),
        .iRecoveryTicks = 34, .fStagger = 92.0f,
        .fMuzzleHeight = 0.45f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      [MECHA_STANCE_STAND] = { .szName = "SNARE", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_ROSE,
        .fSpeed = MECHA_MPS(90.0f), .fDamage = 62.0f, .fRadius = MECHA_M(1.6f),
        .fBlastRadius = MECHA_M(10.0f), .fArcGravity = MECHA_MPS(30.0f),
        .iLifeTicks = 170, .iAmmo = 3, .iReloadTicks = MECHA_SEC(2.6f),
        .iRecoveryTicks = 22, .fStagger = 70.0f,
        .fMuzzleHeight = 0.58f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "TRIP MINE", .byKind = MECHA_PROJ_MINE,
        .byCount = 3, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(18),
        .fSpeed = MECHA_MPS(44.0f), .fDamage = 74.0f, .fRadius = MECHA_M(1.8f),
        .fBlastRadius = MECHA_M(10.0f), .fArcGravity = MECHA_MPS(34.0f),
        .iLifeTicks = MECHA_SEC(8.0f), .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.6f),
        .iRecoveryTicks = 24, .fStagger = 44.0f,
        .fMuzzleHeight = 0.34f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_DASH] = { .szName = "CROSS SLASH", .byKind = MECHA_PROJ_MELEE,
        .byCount = 2, .byPalette = PAL_TRACER_WHITE, .iSpreadAngle = MECHA_DEG(16),
        .fSpeed = MECHA_MPS(88.0f), .fDamage = 92.0f, .fRadius = MECHA_M(4.6f),
        .iLifeTicks = 22, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 32, .fStagger = 62.0f,
        .fMuzzleHeight = 0.55f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "DROP CHARGE", .byKind = MECHA_PROJ_ARC,
        .byCount = 2, .byPalette = PAL_TRACER_ORANGE, .iSpreadAngle = MECHA_DEG(8),
        .fSpeed = MECHA_MPS(52.0f), .fDamage = 80.0f, .fRadius = MECHA_M(1.5f),
        .fBlastRadius = MECHA_M(11.0f), .fArcGravity = MECHA_MPS(40.0f),
        .iLifeTicks = 170, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.2f),
        .iRecoveryTicks = 26, .fStagger = 46.0f,
        .fMuzzleHeight = 0.30f, .fMuzzleSide = 1.0f },
    },
  },
},

//-------------------------------------------------------------------------------------------------
{
  /*
   * The odd one out, and deliberately so.
   *
   * It is a race car with a handgun on it: no legs, no arms, no boost and
   * no jump. What it has instead is speed it does not have to spend
   * anything on and a body small enough to be hard to hit, and the price is
   * that it can only point where it is driving. There is no auto-turn on
   * this machine at any range, so the only way it holds a lock is to drive
   * at somebody and keep them in the middle of the screen -- which is also
   * the only way it lines up its one gun.
   *
   * That gun is the whole armament. All three triggers are the same weapon,
   * because there is only one of it: one shot in the chamber, two and a
   * half seconds to put another one in, and enough behind it that landing
   * one matters. Firing shoves the car. And with no melee row at all, its
   * answer at close quarters is to drive into you, which is the other thing
   * a car is for.
   */
  .szName = "ZIZIN KLR 330", .szClass = "GUN CAR",
  /* bWheeled is the physics -- one signed speed along the nose -- and the
   * chassis is what gets drawn. The roster test holds the two together so
   * a wheeled machine cannot quietly come out as a biped. [TYPE-06] */
  .bWheeled = true, .byChassis = MECHA_CHASSIS_CAR,
  /*
   * Traction. Eleven left the car crabbing sixty-five degrees off its own
   * nose through any corner it tried to take under power -- permanently
   * sliding rather than driving, and four seconds to gather up a slide.
   * Thirty puts the nose roughly where the car is going, clears the same
   * slide in a third of the time and still spins right round on the brakes
   * when it is asked to.
   */
  .fGrip = MECHA_MPS(30.0f), .fDriveAccel = MECHA_MPS(34.0f),
  .fBrake = MECHA_MPS(72.0f),
  .fSteerFloor = MECHA_MPS(4.0f),
  .fRamDamage = 3.4f, .fRamSpeed = MECHA_MPS(28.0f),
  .fRecoilPush = MECHA_MPS(13.0f),
  .fBuildShoulder = 1.00f, .fBuildTorso = 1.00f, .fBuildLimb = 1.00f,
  .fBuildHead = 1.00f, .fBuildGun = 1.00f,
  /* A sixth of a machine's height, and about as wide as it is tall, which
   * is what a car is. */
  .fHeight = MECHA_M(2.4f), .fRadius = MECHA_M(2.0f), .fMass = 0.55f,
  .fArmour = 720.0f,
  .fWalkSpeed = MECHA_MPS(66.0f), .fDashSpeed = MECHA_MPS(66.0f),
  /*
   * Flat out it barely turns and just off a standstill it spins on the
   * spot: 57 degrees a second times the steering bonus, which is seven at
   * rest. That is Whiplash's own curve -- 72 units of lock a tick at 36 Hz
   * with the bonus on top -- and it is why a car has to be slowed into a
   * corner rather than steered round one.
   */
  .fAirSpeed = MECHA_MPS(66.0f), .fTurnRate = (float)MECHA_DEG(57),
  .fJumpVelocity = 0.0f,
  .iBoostMax = 1000, .iBoostDashDrain = 0, .iBoostJumpCost = 0,
  .iBoostJumpDrain = 0, .iBoostRegen = 1000, .iBoostGuardRegen = 1000,
  .iDashTicks = MECHA_SEC(1.0f), .iLandTicks = MECHA_SEC(0.10f),
  .abyPalette = { PAL_HULL_PALE, PAL_HULL_DARK, PAL_JOINT, PAL_TRACER_YELLOW },
  /*
   * One gun, three loads, nine rounds between them.
   *
   * The Zizin carries a single oversized handgun and the three triggers
   * are three things to put through it, not three weapons: a magazine of
   * nine that every trigger draws from, and one long reload when it runs
   * dry. So the choice is never which gun to use, it is what to spend the
   * next round on -- and spending it badly costs the same as spending it
   * well.
   *
   * Each slot is the same in every stance, because a car has no stances:
   * it is always simply driving.
   */
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      /* Buckshot. Seven pellets across five degrees and gone in half a
       * second, so it is devastating at ramming distance and litter at
       * any other -- which suits a machine whose other close-quarters
       * answer is to drive into you. */
      [MECHA_STANCE_STAND] = { .szName = "KLR BUCKSHOT", .byKind = MECHA_PROJ_BULLET,
        .byCount = 7, .byPalette = PAL_TRACER_YELLOW,
        .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(210.0f), .fDamage = 24.0f,
        .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 34, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 20, .fStagger = 11.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_GUARD] = { .szName = "KLR BUCKSHOT", .byKind = MECHA_PROJ_BULLET,
        .byCount = 7, .byPalette = PAL_TRACER_YELLOW,
        .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(210.0f), .fDamage = 24.0f,
        .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 34, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 20, .fStagger = 11.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_DASH] = { .szName = "KLR BUCKSHOT", .byKind = MECHA_PROJ_BULLET,
        .byCount = 7, .byPalette = PAL_TRACER_YELLOW,
        .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(210.0f), .fDamage = 24.0f,
        .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 34, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 20, .fStagger = 11.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_JUMP] = { .szName = "KLR BUCKSHOT", .byKind = MECHA_PROJ_BULLET,
        .byCount = 7, .byPalette = PAL_TRACER_YELLOW,
        .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(210.0f), .fDamage = 24.0f,
        .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 34, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 20, .fStagger = 11.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
    },
    [MECHA_SLOT_CENTER] = {
      /* The lance. One round, no spread, five hundred metres a second
       * and a long look down the barrel before the car can do anything
       * else. This is the shot the whole machine is built around: keep
       * somebody centred in the reticle at range and take their head
       * off. */
      [MECHA_STANCE_STAND] = { .szName = "KLR LANCE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_YELLOW,
        .fSpeed = MECHA_MPS(520.0f), .fDamage = 112.0f,
        .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 150, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 34, .fStagger = 34.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_GUARD] = { .szName = "KLR LANCE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_YELLOW,
        .fSpeed = MECHA_MPS(520.0f), .fDamage = 112.0f,
        .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 150, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 34, .fStagger = 34.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_DASH] = { .szName = "KLR LANCE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_YELLOW,
        .fSpeed = MECHA_MPS(520.0f), .fDamage = 112.0f,
        .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 150, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 34, .fStagger = 34.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_JUMP] = { .szName = "KLR LANCE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_YELLOW,
        .fSpeed = MECHA_MPS(520.0f), .fDamage = 112.0f,
        .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 150, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 34, .fStagger = 34.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
    },
    [MECHA_SLOT_RIGHT] = {
      /* And a shell lobbed over whatever is in the way. It does not care
       * whether it hits -- twelve metres of blast finds people behind
       * cover -- and the speed and the gravity are raised together so it
       * arrives in two thirds the time while keeping the arc it had and
       * the reach to match the lock. [DEF-07] */
      [MECHA_STANCE_STAND] = { .szName = "KLR MORTAR", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(140.0f), .fDamage = 74.0f,
        .fRadius = MECHA_M(1.3f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(75.0f),
        .iLifeTicks = 200, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 42, .fStagger = 40.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_GUARD] = { .szName = "KLR MORTAR", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(140.0f), .fDamage = 74.0f,
        .fRadius = MECHA_M(1.3f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(75.0f),
        .iLifeTicks = 200, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 42, .fStagger = 40.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_DASH] = { .szName = "KLR MORTAR", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(140.0f), .fDamage = 74.0f,
        .fRadius = MECHA_M(1.3f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(75.0f),
        .iLifeTicks = 200, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 42, .fStagger = 40.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_JUMP] = { .szName = "KLR MORTAR", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(140.0f), .fDamage = 74.0f,
        .fRadius = MECHA_M(1.3f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(75.0f),
        .iLifeTicks = 200, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 42, .fStagger = 40.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
    },
  }
},

//-------------------------------------------------------------------------------------------------
/*
 * BASTION 88. The gun that brought its own emplacement: a tracked carriage
 * under a turret of a torso, with a battery on each shoulder. It walks,
 * turns and strafes exactly as the bipeds do -- what is different is that
 * it does all of it slowly, cannot get out of the way, and does not have to.
 * The hop is deliberately a hop: a machine that could not leave the ground
 * at all would be locked out of the jump weapons and the jump cancel, which
 * is a quarter of the mode. [DEF-08]
 */
{
  .szName = "BASTION 88", .szClass = "TRACKED GUN",
  .byChassis = MECHA_CHASSIS_TREAD,
  .fGrip = MECHA_MPS(54.0f), .fDriveAccel = MECHA_MPS(40.0f),
  .fBrake = MECHA_MPS(46.0f),
  .fBuildShoulder = 1.30f, .fBuildTorso = 1.22f, .fBuildLimb = 1.10f,
  .fBuildHead = 0.86f, .fBuildGun = 1.30f,
  .fHeight = MECHA_M(15.5f), .fRadius = MECHA_M(4.3f), .fMass = 1.8f,
  .fArmour = 2000.0f,
  .fWalkSpeed = MECHA_MPS(15.0f), .fDashSpeed = MECHA_MPS(44.0f),
  .fAirSpeed = MECHA_MPS(18.0f), .fTurnRate = (float)MECHA_DEG(105),
  .fJumpVelocity = MECHA_MPS(17.0f),
  .iBoostMax = 1000, .iBoostDashDrain = 400, .iBoostJumpCost = 240,
  .iBoostJumpDrain = 420, .iBoostRegen = 150, .iBoostGuardRegen = 430,
  .iDashTicks = MECHA_SEC(0.70f), .iLandTicks = MECHA_SEC(0.44f),
  .abyPalette = { PAL_HULL_IRON, PAL_HULL_IRON_T, PAL_JOINT, PAL_TRACER_ORANGE },
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "TWIN AUTOCANNON", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_ORANGE, .iSpreadAngle = MECHA_DEG(2),
        .fSpeed = MECHA_MPS(165.0f), .fDamage = 30.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 110, .iAmmo = 14, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 7, .fStagger = 7.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "BRACED AUTOCANNON", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_ORANGE, .iSpreadAngle = MECHA_DEG(1),
        .fSpeed = MECHA_MPS(215.0f), .fDamage = 44.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 160, .iAmmo = 10, .iReloadTicks = MECHA_SEC(2.8f),
        .iRecoveryTicks = 12, .fStagger = 13.0f,
        .fMuzzleHeight = 0.58f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_DASH] = { .szName = "ROLLING FIRE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_ORANGE, .iSpreadAngle = MECHA_DEG(6),
        .fSpeed = MECHA_MPS(150.0f), .fDamage = 24.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 95, .iAmmo = 12, .iReloadTicks = MECHA_SEC(2.6f),
        .iRecoveryTicks = 8, .fStagger = 6.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "AIRBURST", .byKind = MECHA_PROJ_BULLET,
        .byCount = 5, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(7),
        .fSpeed = MECHA_MPS(135.0f), .fDamage = 26.0f, .fRadius = MECHA_M(1.0f),
        .iLifeTicks = 85, .iAmmo = 6, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 15, .fStagger = 9.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = -1.0f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "SIEGE MORTAR", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_RED,
        .fSpeed = MECHA_MPS(110.0f), .fDamage = 168.0f, .fRadius = MECHA_M(1.8f),
        .fBlastRadius = MECHA_M(15.0f), .fArcGravity = MECHA_MPS(52.0f),
        .iLifeTicks = 220, .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.4f),
        .iRecoveryTicks = 46, .fStagger = 92.0f,
        .fMuzzleHeight = 0.72f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "ANCHORED SHOT", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(460.0f), .fDamage = 210.0f, .fRadius = MECHA_M(1.7f),
        .iLifeTicks = 110, .iAmmo = 2, .iReloadTicks = MECHA_SEC(4.4f),
        .iRecoveryTicks = 62, .fStagger = 76.0f,
        .fMuzzleHeight = 0.64f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "SNAP SHELL", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_RED,
        .fSpeed = MECHA_MPS(200.0f), .fDamage = 96.0f, .fRadius = MECHA_M(1.3f),
        .fBlastRadius = MECHA_M(8.0f),
        .iLifeTicks = 130, .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 30, .fStagger = 48.0f,
        .fMuzzleHeight = 0.68f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "PLUNGING FIRE", .byKind = MECHA_PROJ_ARC,
        .byCount = 3, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(9),
        .fSpeed = MECHA_MPS(80.0f), .fDamage = 90.0f, .fRadius = MECHA_M(1.5f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(66.0f),
        .iLifeTicks = 200, .iAmmo = 2, .iReloadTicks = MECHA_SEC(4.0f),
        .iRecoveryTicks = 40, .fStagger = 60.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      [MECHA_STANCE_STAND] = { .szName = "ROCKET POD", .byKind = MECHA_PROJ_HOMING,
        .byCount = 4, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(11),
        .fSpeed = MECHA_MPS(105.0f), .fDamage = 40.0f, .fRadius = MECHA_M(1.1f),
        .fBlastRadius = MECHA_M(7.0f),
        .iLifeTicks = 190, .iAmmo = 4, .iReloadTicks = MECHA_SEC(3.2f),
        .iRecoveryTicks = 26, .iHomingRate = MECHA_DEG(100), .fStagger = 22.0f,
        .fMuzzleHeight = 0.74f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "FULL SALVO", .byKind = MECHA_PROJ_HOMING,
        .byCount = 8, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(14),
        .fSpeed = MECHA_MPS(95.0f), .fDamage = 34.0f, .fRadius = MECHA_M(1.1f),
        .fBlastRadius = MECHA_M(6.0f),
        .iLifeTicks = 210, .iAmmo = 2, .iReloadTicks = MECHA_SEC(5.0f),
        .iRecoveryTicks = 54, .iHomingRate = MECHA_DEG(115), .fStagger = 18.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_DASH] = { .szName = "SIDE BATTERY", .byKind = MECHA_PROJ_HOMING,
        .byCount = 3, .byPalette = PAL_TRACER_YELLOW, .iSpreadAngle = MECHA_DEG(16),
        .fSpeed = MECHA_MPS(115.0f), .fDamage = 32.0f, .fRadius = MECHA_M(1.0f),
        .fBlastRadius = MECHA_M(6.0f),
        .iLifeTicks = 160, .iAmmo = 4, .iReloadTicks = MECHA_SEC(3.4f),
        .iRecoveryTicks = 20, .iHomingRate = MECHA_DEG(90), .fStagger = 16.0f,
        .fMuzzleHeight = 0.74f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "DROP RACK", .byKind = MECHA_PROJ_MINE,
        .byCount = 4, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(20),
        .fSpeed = MECHA_MPS(40.0f), .fDamage = 86.0f, .fRadius = MECHA_M(1.8f),
        .fBlastRadius = MECHA_M(11.0f), .fArcGravity = MECHA_MPS(44.0f),
        .iLifeTicks = MECHA_SEC(9.0f), .iAmmo = 2, .iReloadTicks = MECHA_SEC(4.6f),
        .iRecoveryTicks = 28, .fStagger = 40.0f,
        .fMuzzleHeight = 0.50f, .fMuzzleSide = 1.0f },
    },
  }
},

//-------------------------------------------------------------------------------------------------
/*
 * Tarant VZ. Six legs off a low hull, and the trade is written into the
 * gauge rather than into the mesh: it holds a boost for barely a third of a
 * second and pays through the nose for it, so it crosses ground by walking
 * fast on six legs instead of by throwing itself about on thrusters. It is
 * the hardest machine in the roster to knock over and the easiest to walk
 * away from. [DEF-09]
 */
{
  .szName = "Tarant VZ", .szClass = "SIX-LEG SKIRMISH",
  .byChassis = MECHA_CHASSIS_ARACHNID,
  .fGrip = MECHA_MPS(150.0f), .fDriveAccel = MECHA_MPS(120.0f),
  .fBrake = MECHA_MPS(140.0f),
  .fBuildShoulder = 0.82f, .fBuildTorso = 1.00f, .fBuildLimb = 0.90f,
  .fBuildHead = 0.94f, .fBuildGun = 0.92f,
  .fHeight = MECHA_M(11.5f), .fRadius = MECHA_M(4.0f), .fMass = 1.25f,
  .fArmour = 1120.0f,
  .fWalkSpeed = MECHA_MPS(30.0f), .fDashSpeed = MECHA_MPS(58.0f),
  .fAirSpeed = MECHA_MPS(26.0f), .fTurnRate = (float)MECHA_DEG(260),
  .fJumpVelocity = MECHA_MPS(21.0f),
  /* Barely a third of a second of burst, and a drain that empties the gauge
   * in three of them: this machine does not get to dash its way out. */
  .iBoostMax = 1000, .iBoostDashDrain = 620, .iBoostJumpCost = 150,
  .iBoostJumpDrain = 300, .iBoostRegen = 210, .iBoostGuardRegen = 560,
  .iDashTicks = MECHA_SEC(0.34f), .iLandTicks = MECHA_SEC(0.18f),
  .abyPalette = { PAL_HULL_DARK, PAL_HULL_DARK_T, PAL_JOINT, PAL_TRACER_ROSE },
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "SPINE REPEATER", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_ROSE, .iSpreadAngle = MECHA_DEG(4),
        .fSpeed = MECHA_MPS(160.0f), .fDamage = 24.0f, .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 95, .iAmmo = 10, .iReloadTicks = MECHA_SEC(2.0f),
        .iRecoveryTicks = 7, .fStagger = 5.0f,
        .fMuzzleHeight = 0.72f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "SPINE AIMED", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_ROSE, .iSpreadAngle = MECHA_DEG(1),
        .fSpeed = MECHA_MPS(210.0f), .fDamage = 46.0f, .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 150, .iAmmo = 6, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 15, .fStagger = 12.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_DASH] = { .szName = "SCUTTLE BURST", .byKind = MECHA_PROJ_BULLET,
        .byCount = 4, .byPalette = PAL_TRACER_ROSE, .iSpreadAngle = MECHA_DEG(8),
        .fSpeed = MECHA_MPS(145.0f), .fDamage = 20.0f, .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 80, .iAmmo = 8, .iReloadTicks = MECHA_SEC(2.2f),
        .iRecoveryTicks = 8, .fStagger = 4.0f,
        .fMuzzleHeight = 0.72f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "SPINE RAIN", .byKind = MECHA_PROJ_ARC,
        .byCount = 5, .byPalette = PAL_TRACER_ROSE, .iSpreadAngle = MECHA_DEG(7),
        .fSpeed = MECHA_MPS(88.0f), .fDamage = 28.0f, .fRadius = MECHA_M(0.9f),
        .fArcGravity = MECHA_MPS(48.0f),
        .iLifeTicks = 170, .iAmmo = 4, .iReloadTicks = MECHA_SEC(2.8f),
        .iRecoveryTicks = 14, .fStagger = 8.0f,
        .fMuzzleHeight = 0.76f, .fMuzzleSide = -1.0f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "FANG", .byKind = MECHA_PROJ_MELEE,
        .byCount = 2, .byPalette = PAL_TRACER_WHITE, .iSpreadAngle = MECHA_DEG(12),
        .fSpeed = MECHA_MPS(40.0f), .fDamage = 96.0f, .fRadius = MECHA_M(4.4f),
        .iLifeTicks = 14, .iAmmo = 3, .iReloadTicks = MECHA_SEC(2.0f),
        .iRecoveryTicks = 20, .fStagger = 62.0f,
        .fMuzzleHeight = 0.56f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "CROUCH FANG", .byKind = MECHA_PROJ_MELEE,
        .byCount = 2, .byPalette = PAL_TRACER_WHITE, .iSpreadAngle = MECHA_DEG(9),
        .fSpeed = MECHA_MPS(30.0f), .fDamage = 130.0f, .fRadius = MECHA_M(3.9f),
        .iLifeTicks = 18, .iAmmo = 2, .iReloadTicks = MECHA_SEC(2.8f),
        .iRecoveryTicks = 30, .fStagger = 96.0f,
        .fMuzzleHeight = 0.40f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "POUNCE", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(90.0f), .fDamage = 156.0f, .fRadius = MECHA_M(5.4f),
        .iLifeTicks = 24, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 30, .fStagger = 100.0f,
        .fMuzzleHeight = 0.52f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "DROP FANG", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(70.0f), .fDamage = 140.0f, .fRadius = MECHA_M(4.8f),
        .iLifeTicks = 22, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.2f),
        .iRecoveryTicks = 32, .fStagger = 88.0f,
        .fMuzzleHeight = 0.44f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      [MECHA_STANCE_STAND] = { .szName = "SNARE LINE", .byKind = MECHA_PROJ_MINE,
        .byCount = 2, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(22),
        .fSpeed = MECHA_MPS(50.0f), .fDamage = 70.0f, .fRadius = MECHA_M(1.9f),
        .fBlastRadius = MECHA_M(10.0f), .fArcGravity = MECHA_MPS(36.0f),
        .iLifeTicks = MECHA_SEC(10.0f), .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.4f),
        .iRecoveryTicks = 22, .fStagger = 44.0f,
        .fMuzzleHeight = 0.44f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "WEB", .byKind = MECHA_PROJ_MINE,
        .byCount = 5, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(30),
        .fSpeed = MECHA_MPS(42.0f), .fDamage = 56.0f, .fRadius = MECHA_M(2.0f),
        .fBlastRadius = MECHA_M(9.0f), .fArcGravity = MECHA_MPS(32.0f),
        .iLifeTicks = MECHA_SEC(12.0f), .iAmmo = 2, .iReloadTicks = MECHA_SEC(4.4f),
        .iRecoveryTicks = 30, .fStagger = 36.0f,
        .fMuzzleHeight = 0.34f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_DASH] = { .szName = "TRAIL MINE", .byKind = MECHA_PROJ_MINE,
        .byCount = 2, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(10),
        .fSpeed = MECHA_MPS(30.0f), .fDamage = 62.0f, .fRadius = MECHA_M(1.8f),
        .fBlastRadius = MECHA_M(9.0f), .fArcGravity = MECHA_MPS(40.0f),
        .iLifeTicks = MECHA_SEC(8.0f), .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.2f),
        .iRecoveryTicks = 16, .fStagger = 34.0f,
        .fMuzzleHeight = 0.44f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "CEILING SPIKE", .byKind = MECHA_PROJ_ARC,
        .byCount = 3, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(8),
        .fSpeed = MECHA_MPS(66.0f), .fDamage = 76.0f, .fRadius = MECHA_M(1.4f),
        .fBlastRadius = MECHA_M(10.0f), .fArcGravity = MECHA_MPS(56.0f),
        .iLifeTicks = 180, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.6f),
        .iRecoveryTicks = 26, .fStagger = 50.0f,
        .fMuzzleHeight = 0.50f, .fMuzzleSide = 1.0f },
    },
  }
},

//-------------------------------------------------------------------------------------------------
/*
 * Hiiragi 14A. The slender frame: narrow through the waist, wide at the skirt,
 * and the lightest armour on the roster carried by the longest dash. It
 * leads from the hips and puts its feet down on its own centreline, and the
 * whole machine rocks back when it fires, because it is small and its guns
 * are not. [DEF-10]
 */
{
  .szName = "Hiiragi 14A", .szClass = "STREET BOSS",
  .byProfile = MECHA_PROFILE_SLENDER,
  .fGrip = MECHA_MPS(165.0f), .fDriveAccel = MECHA_MPS(135.0f),
  .fBrake = MECHA_MPS(120.0f),
  /*
   * Narrow across the shoulders and wide at the hip, which is the contrast
   * the frame is built on and the one thing that tells it apart from the
   * interceptor at range -- the two were written near enough as twins, and
   * the roster's own silhouette check caught it. [DEF-10]
   *
   * The shoulder number used to be the largest on the frame, on the theory
   * that binders standing clear of a slight torso were the contrast. They
   * were not: they were the widest thing on the machine, wider than its
   * hips by two thirds, and a figure whose broadest point is its shoulders
   * reads as a T however narrow the waist under it. Measured off the
   * builder's own part tags rather than eyeballed, the hips were 0.52 of
   * the shoulders where the male frames sit at 0.64. Now 1.00: the widest
   * thing on this machine is its hips, which is true of nothing else on
   * the roster. [DEF-13]
   */
  /*
   * Read off Fei-Yen. What makes that figure is not one measurement, it is
   * a set of them agreeing: a head under an eighth of the height, shoulders
   * narrower than the hips, a short body over legs that are most of the
   * machine, and limbs whose cross-section is small against their length.
   * Miss any one and the rest stop reading -- a slim limb under a wide
   * shoulder is a thin arm on a big mech, not a slender frame. [DEF-14]
   */
  .fBuildShoulder = 0.43f, .fBuildTorso = 0.57f, .fBuildLimb = 0.68f,
  .fBuildHead = 0.70f, .fBuildGun = 0.74f,
  /* Hips higher still and the arms lengthened rather than shortened: the
   * body shrinks by exactly what the legs gain, so the head stays put and
   * the figure grows a waist instead of growing taller. [DEF-10] */
  .fBuildHip = 0.62f, .fBuildArm = 0.94f,
  /*
   * And the smallest thing on two legs in the game. A hip flare is three
   * pixels at the range machines are told apart at, so the difference
   * between this frame and the interceptor had to be one that survives being
   * thirty pixels tall: it is a head shorter. [DEF-10]
   */
  .fHeight = MECHA_M(11.4f), .fRadius = MECHA_M(2.9f), .fMass = 0.72f,
  .fArmour = 760.0f,
  .fWalkSpeed = MECHA_MPS(27.0f), .fDashSpeed = MECHA_MPS(92.0f),
  .fAirSpeed = MECHA_MPS(44.0f), .fTurnRate = (float)MECHA_DEG(280),
  .fJumpVelocity = MECHA_MPS(33.0f),
  .iBoostMax = 1000, .iBoostDashDrain = 290, .iBoostJumpCost = 70,
  .iBoostJumpDrain = 210, .iBoostRegen = 175, .iBoostGuardRegen = 520,
  .iDashTicks = MECHA_SEC(0.95f), .iLandTicks = MECHA_SEC(0.20f),
  .abyPalette = { PAL_HULL_PALE, PAL_HULL_PALE_T, PAL_JOINT, PAL_TRACER_MAGENTA },
  /*
   * The frame was named for a delinquent, not a dancer, and the old names
   * were still the ballet: PIROUETTE, CURTSEY, GRAND JETE. One family per
   * slot, picked to fit what the slot actually does -- thrown blades on the
   * scatter, a bike chain on the melee, and the steel yo-yo on the homing
   * rack, which is the one weapon that tracks and comes back. Its trick
   * names fall on the stances for free: a sleeper hangs, a walk runs out
   * flat, a loop goes overhead, and the jump rack is the one that arcs.
   *
   * This also settles a collision. The right rack used to be NEEDLE, which
   * the interceptor already owns four of; two machines sharing a weapon
   * family is the same failure as two sharing a silhouette. [DEF-15]
   */
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "RAZOR", .byKind = MECHA_PROJ_BULLET,
        .byCount = 4, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(3),
        .fSpeed = MECHA_MPS(175.0f), .fDamage = 19.0f, .fRadius = MECHA_M(0.6f),
        .iLifeTicks = 100, .iAmmo = 12, .iReloadTicks = MECHA_SEC(1.7f),
        .iRecoveryTicks = 6, .fStagger = 4.0f,
        .fMuzzleHeight = 0.64f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "RAZOR OPEN", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_MAGENTA,
        .fSpeed = MECHA_MPS(430.0f), .fDamage = 112.0f, .fRadius = MECHA_M(1.2f),
        .iLifeTicks = 120, .iAmmo = 4, .iReloadTicks = MECHA_SEC(2.6f),
        .iRecoveryTicks = 26, .fStagger = 30.0f,
        .fMuzzleHeight = 0.50f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_DASH] = { .szName = "RAZOR SPRAY", .byKind = MECHA_PROJ_BULLET,
        .byCount = 5, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(7),
        .fSpeed = MECHA_MPS(160.0f), .fDamage = 16.0f, .fRadius = MECHA_M(0.6f),
        .iLifeTicks = 85, .iAmmo = 10, .iReloadTicks = MECHA_SEC(1.9f),
        .iRecoveryTicks = 6, .fStagger = 3.0f,
        .fMuzzleHeight = 0.64f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "RAZOR RAIN", .byKind = MECHA_PROJ_ARC,
        .byCount = 6, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(6),
        .fSpeed = MECHA_MPS(92.0f), .fDamage = 22.0f, .fRadius = MECHA_M(0.8f),
        .fArcGravity = MECHA_MPS(42.0f),
        .iLifeTicks = 160, .iAmmo = 5, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 12, .fStagger = 6.0f,
        .fMuzzleHeight = 0.68f, .fMuzzleSide = -1.0f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "CHAIN WHIP", .byKind = MECHA_PROJ_MELEE,
        .byCount = 3, .byPalette = PAL_TRACER_WHITE, .iSpreadAngle = MECHA_DEG(40),
        .fSpeed = MECHA_MPS(46.0f), .fDamage = 74.0f, .fRadius = MECHA_M(4.6f),
        .iLifeTicks = 16, .iAmmo = 3, .iReloadTicks = MECHA_SEC(2.2f),
        .iRecoveryTicks = 22, .fStagger = 54.0f,
        .fMuzzleHeight = 0.58f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "CHAIN WRAP", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(34.0f), .fDamage = 142.0f, .fRadius = MECHA_M(4.2f),
        .iLifeTicks = 20, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 32, .fStagger = 92.0f,
        .fMuzzleHeight = 0.38f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "CHAIN RUSH", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(104.0f), .fDamage = 164.0f, .fRadius = MECHA_M(5.6f),
        .iLifeTicks = 26, .iAmmo = 2, .iReloadTicks = MECHA_SEC(2.8f),
        .iRecoveryTicks = 26, .fStagger = 88.0f,
        .fMuzzleHeight = 0.56f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "CHAIN DROP", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(82.0f), .fDamage = 150.0f, .fRadius = MECHA_M(5.0f),
        .iLifeTicks = 24, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 30, .fStagger = 84.0f,
        .fMuzzleHeight = 0.46f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      [MECHA_STANCE_STAND] = { .szName = "YO-YO", .byKind = MECHA_PROJ_HOMING,
        .byCount = 3, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(13),
        .fSpeed = MECHA_MPS(135.0f), .fDamage = 34.0f, .fRadius = MECHA_M(0.8f),
        .iLifeTicks = 175, .iAmmo = 5, .iReloadTicks = MECHA_SEC(2.6f),
        .iRecoveryTicks = 18, .iHomingRate = MECHA_DEG(150), .fStagger = 14.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "YO-YO SLEEPER", .byKind = MECHA_PROJ_HOMING,
        .byCount = 6, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(18),
        .fSpeed = MECHA_MPS(120.0f), .fDamage = 30.0f, .fRadius = MECHA_M(0.8f),
        .iLifeTicks = 200, .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.6f),
        .iRecoveryTicks = 30, .iHomingRate = MECHA_DEG(175), .fStagger = 12.0f,
        .fMuzzleHeight = 0.60f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_DASH] = { .szName = "YO-YO WALK", .byKind = MECHA_PROJ_HOMING,
        .byCount = 2, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(9),
        .fSpeed = MECHA_MPS(150.0f), .fDamage = 32.0f, .fRadius = MECHA_M(0.8f),
        .iLifeTicks = 150, .iAmmo = 6, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 12, .iHomingRate = MECHA_DEG(130), .fStagger = 11.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "YO-YO LOOP", .byKind = MECHA_PROJ_HOMING,
        .byCount = 8, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(24),
        .fSpeed = MECHA_MPS(115.0f), .fDamage = 26.0f, .fRadius = MECHA_M(0.8f),
        .iLifeTicks = 210, .iAmmo = 2, .iReloadTicks = MECHA_SEC(4.2f),
        .iRecoveryTicks = 34, .iHomingRate = MECHA_DEG(160), .fStagger = 10.0f,
        .fMuzzleHeight = 0.74f, .fMuzzleSide = 1.0f },
    },
  }
},

//-------------------------------------------------------------------------------------------------
/*
 * Corvid 3. It fights by what it leaves in the air. Its left slot puts out
 * drones -- slow, long-lived homing units that chase rather than arrive --
 * and its right lays mines that hang where they are put instead of falling,
 * which is a mine field at head height rather than under the feet. It
 * carries no gun in either hand, and the rack across its back is what says
 * so from any angle. [DEF-13]
 */
{
  .szName = "Corvid 3", .szClass = "DRONE CONTROL",
  .byProfile = MECHA_PROFILE_CARRIER,
  .fGrip = MECHA_MPS(110.0f), .fDriveAccel = MECHA_MPS(92.0f),
  .fBrake = MECHA_MPS(90.0f),
  .fBuildShoulder = 1.06f, .fBuildTorso = 0.96f, .fBuildLimb = 0.82f,
  .fBuildHead = 1.14f, .fBuildGun = 0.70f,
  .fHeight = MECHA_M(13.8f), .fRadius = MECHA_M(3.1f), .fMass = 0.95f,
  .fArmour = 900.0f,
  .fWalkSpeed = MECHA_MPS(22.0f), .fDashSpeed = MECHA_MPS(70.0f),
  .fAirSpeed = MECHA_MPS(34.0f), .fTurnRate = (float)MECHA_DEG(210),
  .fJumpVelocity = MECHA_MPS(28.0f),
  .iBoostMax = 1000, .iBoostDashDrain = 330, .iBoostJumpCost = 90,
  .iBoostJumpDrain = 250, .iBoostRegen = 165, .iBoostGuardRegen = 540,
  .iDashTicks = MECHA_SEC(0.72f), .iLandTicks = MECHA_SEC(0.26f),
  .abyPalette = { PAL_HULL_STEEL, PAL_HULL_STEEL_T, PAL_JOINT, PAL_TRACER_CYAN },
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "DRONE PAIR", .byKind = MECHA_PROJ_HOMING,
        .byCount = 2, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(20),
        .fSpeed = MECHA_MPS(72.0f), .fDamage = 44.0f, .fRadius = MECHA_M(1.2f),
        .fBlastRadius = MECHA_M(6.0f),
        .iLifeTicks = MECHA_SEC(7.0f), .iAmmo = 4, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 20, .iHomingRate = MECHA_DEG(85), .fStagger = 20.0f,
        .fMuzzleHeight = 0.80f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "DRONE FLIGHT", .byKind = MECHA_PROJ_HOMING,
        .byCount = 5, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(34),
        .fSpeed = MECHA_MPS(62.0f), .fDamage = 40.0f, .fRadius = MECHA_M(1.2f),
        .fBlastRadius = MECHA_M(6.0f),
        .iLifeTicks = MECHA_SEC(9.0f), .iAmmo = 2, .iReloadTicks = MECHA_SEC(4.8f),
        .iRecoveryTicks = 40, .iHomingRate = MECHA_DEG(95), .fStagger = 18.0f,
        .fMuzzleHeight = 0.80f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_DASH] = { .szName = "DRONE RELEASE", .byKind = MECHA_PROJ_HOMING,
        .byCount = 2, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(26),
        .fSpeed = MECHA_MPS(80.0f), .fDamage = 38.0f, .fRadius = MECHA_M(1.2f),
        .fBlastRadius = MECHA_M(5.0f),
        .iLifeTicks = MECHA_SEC(6.0f), .iAmmo = 4, .iReloadTicks = MECHA_SEC(3.2f),
        .iRecoveryTicks = 16, .iHomingRate = MECHA_DEG(80), .fStagger = 16.0f,
        .fMuzzleHeight = 0.80f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "DRONE SCATTER", .byKind = MECHA_PROJ_HOMING,
        .byCount = 4, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(40),
        .fSpeed = MECHA_MPS(68.0f), .fDamage = 36.0f, .fRadius = MECHA_M(1.2f),
        .fBlastRadius = MECHA_M(5.0f),
        .iLifeTicks = MECHA_SEC(8.0f), .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.8f),
        .iRecoveryTicks = 24, .iHomingRate = MECHA_DEG(90), .fStagger = 15.0f,
        .fMuzzleHeight = 0.84f, .fMuzzleSide = -1.0f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "MARKER", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_CYAN,
        .fSpeed = MECHA_MPS(230.0f), .fDamage = 52.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 150, .iAmmo = 6, .iReloadTicks = MECHA_SEC(2.0f),
        .iRecoveryTicks = 14, .fStagger = 16.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "MARKER LOCKED", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_CYAN,
        .fSpeed = MECHA_MPS(450.0f), .fDamage = 128.0f, .fRadius = MECHA_M(1.3f),
        .iLifeTicks = 130, .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.4f),
        .iRecoveryTicks = 34, .fStagger = 34.0f,
        .fMuzzleHeight = 0.58f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "MARKER SNAP", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_CYAN,
        .fSpeed = MECHA_MPS(215.0f), .fDamage = 40.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 130, .iAmmo = 6, .iReloadTicks = MECHA_SEC(2.2f),
        .iRecoveryTicks = 12, .fStagger = 12.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "SPOTTER", .byKind = MECHA_PROJ_HOMING,
        .byCount = 1, .byPalette = PAL_TRACER_CYAN,
        .fSpeed = MECHA_MPS(140.0f), .fDamage = 84.0f, .fRadius = MECHA_M(1.1f),
        .fBlastRadius = MECHA_M(8.0f),
        .iLifeTicks = 180, .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.2f),
        .iRecoveryTicks = 24, .iHomingRate = MECHA_DEG(120), .fStagger = 40.0f,
        .fMuzzleHeight = 0.72f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      /*
       * Mines with no gravity in them, and every one of them a hunter. The
       * flight code only pulls a shot down when its arc gravity is above
       * zero, so these never reach the floor: they bleed the throw off while
       * they arm, hang where they were put, and then go after whoever they
       * were laid against at a third of the speed they were thrown.
       * [SIM-26]
       */
      [MECHA_STANCE_STAND] = { .szName = "HANGING MINE", .byKind = MECHA_PROJ_MINE,
        .byCount = 2, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(16),
        .fSpeed = MECHA_MPS(46.0f), .fDamage = 78.0f, .fRadius = MECHA_M(1.9f),
        .fBlastRadius = MECHA_M(11.0f),
        .iLifeTicks = MECHA_SEC(11.0f), .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.4f),
        .iHomingRate = MECHA_DEG(95), .iRecoveryTicks = 24, .fStagger = 46.0f,
        .fMuzzleHeight = 0.64f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "CURTAIN", .byKind = MECHA_PROJ_MINE,
        .byCount = 6, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(28),
        .fSpeed = MECHA_MPS(38.0f), .fDamage = 60.0f, .fRadius = MECHA_M(2.0f),
        .fBlastRadius = MECHA_M(9.0f),
        .iLifeTicks = MECHA_SEC(14.0f), .iAmmo = 2, .iReloadTicks = MECHA_SEC(5.0f),
        .iHomingRate = MECHA_DEG(70), .iRecoveryTicks = 38, .fStagger = 34.0f,
        .fMuzzleHeight = 0.56f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_DASH] = { .szName = "TRAIL CHARGE", .byKind = MECHA_PROJ_MINE,
        .byCount = 3, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(12),
        .fSpeed = MECHA_MPS(34.0f), .fDamage = 58.0f, .fRadius = MECHA_M(1.8f),
        .fBlastRadius = MECHA_M(9.0f),
        .iLifeTicks = MECHA_SEC(9.0f), .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.6f),
        .iHomingRate = MECHA_DEG(85), .iRecoveryTicks = 18, .fStagger = 32.0f,
        .fMuzzleHeight = 0.64f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "CEILING FIELD", .byKind = MECHA_PROJ_MINE,
        .byCount = 5, .byPalette = PAL_TRACER_MAGENTA, .iSpreadAngle = MECHA_DEG(34),
        .fSpeed = MECHA_MPS(40.0f), .fDamage = 66.0f, .fRadius = MECHA_M(1.9f),
        .fBlastRadius = MECHA_M(10.0f),
        .iLifeTicks = MECHA_SEC(12.0f), .iAmmo = 2, .iReloadTicks = MECHA_SEC(4.4f),
        .iHomingRate = MECHA_DEG(75), .iRecoveryTicks = 30, .fStagger = 38.0f,
        .fMuzzleHeight = 0.80f, .fMuzzleSide = 1.0f },
    },
  }
},

};

//-------------------------------------------------------------------------------------------------

#define MECHA_DEF_COUNT ((int)(sizeof(s_aMechDefs) / sizeof(s_aMechDefs[0])))

//-------------------------------------------------------------------------------------------------

int mecha_def_count(void)
{
  return MECHA_DEF_COUNT;
}

//-------------------------------------------------------------------------------------------------

const tMechaMechDef *mecha_def_get(int iDefIdx)
{
  if (iDefIdx < 0)
    iDefIdx = 0;
  return &s_aMechDefs[iDefIdx % MECHA_DEF_COUNT];
}
