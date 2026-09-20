#ifndef _ROLLER_MECHA_TYPES_H
#define _ROLLER_MECHA_TYPES_H
//-------------------------------------------------------------------------------------------------
/*
 * State for ROLLER's arena mode: a mecha duel on the track game's own
 * rasteriser, 14-bit heading circle and world scale.
 *
 * Nothing here, or in mecha_sim.c, mecha_ai.c, mecha_arena.c and
 * mecha_defs.c behind it, includes SDL or touches a ROLLER global: the
 * simulation is a pure function of its own state plus one input struct per
 * machine per tick, which is what lets it run headless under the tests. The
 * engine-facing half is mecha_render.c and mecha_mode.c.
 *
 * Roster, arenas and artwork are original to this project. What is borrowed
 * from the arcade lineage is the shape of the mechanics, not anyone's data.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_math.h"
//-------------------------------------------------------------------------------------------------

#define MECHA_TICK_HZ          60
#define MECHA_TICK_SECONDS     (1.0f / (float)MECHA_TICK_HZ)

#define MECHA_MAX_MECHS         16
#define MECHA_MAX_PROJECTILES 192
#define MECHA_MAX_EFFECTS      96
#define MECHA_MAX_OBSTACLES    48
/* Ways across an arena, and stations on one. Two is a causeway apiece; the
 * stations are the ones the map was measured at, with an end on each base.
 * [AI-13] */
#define MECHA_MAX_WAYS         2
#define MECHA_WAY_POINTS       24
#define MECHA_RAGDOLL_AXES     3
#define MECHA_RAGDOLL_BONES   27

/*
 * What is in an arena's sky. Clouds are the default and what every arena on
 * a planet wants; a starfield is the same generator asked for something
 * else. [MESH-49]
 */
typedef enum
{
  MECHA_SKY_CLOUDS    = 0,
  MECHA_SKY_STARFIELD = 1
} eMechaSkyKind;

/* Terrain: a grid of square cells, a height per corner and a surface word
 * per cell. Coarse on purpose. [ARENA-04] */
/*
 * The array size, not any arena's own division -- each carries its own count
 * in iTerrainCells, and a bigger arena needs more. A hundred and sixty puts
 * a cell under nine metres on the largest arena there is, which is what the
 * causeways on FACING WORLDS need: their edges are the shape of the stage,
 * and a cell there is the finest that shape can be. [ARENA-20]
 */
#define MECHA_TERRAIN_CELLS 160
#define MECHA_TERRAIN_NODES (MECHA_TERRAIN_CELLS + 1)
/* What an arena gets when it does not ask for anything else. */
#define MECHA_TERRAIN_CELLS_DEFAULT 12

/*
 * Surface bits, duplicated from the engine's own types.h because nothing in
 * the simulation may include it; mecha_render.c asserts they agree.
 * [TYPE-01]
 *
 * A pit is a surface and not a hole, exactly as in the race game. [SIM-14]
 */
#define MECHA_SURF_SKIP_RENDER  0x00020000u
#define MECHA_SURF_NON_MAGNETIC 0x00080000u
#define MECHA_SURF_PIT          0x02000000u
/* Tarmac rather than whatever the arena's ground normally is. Purely a
 * drawing instruction -- a street holds a wheel exactly as the grass
 * beside it does -- so it lives with the other surface bits rather than
 * needing a second grid. */
#define MECHA_SURF_ROAD         0x04000000u

/* Left trigger, both triggers, right trigger -- the three shots every mech
 * carries. */
#define MECHA_WEAPON_SLOTS      3
#define MECHA_SLOT_LEFT         0
#define MECHA_SLOT_CENTER       1
#define MECHA_SLOT_RIGHT        2

//-------------------------------------------------------------------------------------------------
/*
 * How the machine is moving when the trigger goes down. Every weapon is
 * defined per stance, which is why weapons are a 3 x 4 table rather than a
 * flat list of three.
 */
typedef enum
{
  MECHA_STANCE_STAND  = 0,
  MECHA_STANCE_GUARD = 1,
  MECHA_STANCE_DASH   = 2,
  MECHA_STANCE_JUMP   = 3,
  MECHA_STANCE_COUNT  = 4
} eMechaStance;

//-------------------------------------------------------------------------------------------------
/*
 * What the machine is built on, which is a question about drawing and
 * nothing else: how many legs to hang off it and what they do. The physics
 * asks bWheeled, which is older and answers a different question -- whether
 * the machine has one signed speed along its nose instead of a walk and a
 * strafe -- and the two are kept apart deliberately. A tracked machine
 * walks, turns and strafes like every other biped; it merely does not look
 * like one. [TYPE-06]
 */
typedef enum
{
  MECHA_CHASSIS_BIPED    = 0,
  /* The race game's own car plan, with a gun bolted to it. */
  MECHA_CHASSIS_CAR      = 1,
  /* A tank for legs: two track units and a hull slung between them. */
  MECHA_CHASSIS_TREAD    = 2,
  /* Six legs off a low body, arched above it. */
  MECHA_CHASSIS_ARACHNID = 3,
  MECHA_CHASSIS_COUNT    = 4
} eMechaChassis;

//-------------------------------------------------------------------------------------------------
/*
 * The trim package a biped wears over that skeleton, and how it carries
 * itself. The bones are the same in all three -- a profile changes what is
 * bolted to them and which gait table the legs read, not how many joints
 * there are. [TYPE-07]
 */
typedef enum
{
  MECHA_PROFILE_STANDARD = 0,
  /* Narrow waist, flared skirt, tapered limbs, trailing head crests, and a
   * walk that leads from the hips. */
  MECHA_PROFILE_SLENDER  = 1,
  /* A rack of pods across the back and no main gun in either hand: this one
   * fights by what it puts in the air. */
  MECHA_PROFILE_CARRIER  = 2,
  MECHA_PROFILE_COUNT    = 3
} eMechaProfile;

//-------------------------------------------------------------------------------------------------

/*
 * What the reticle is actually doing. iTargetIdx says who it is pointed at;
 * this says whether the mech is tracking them. Only MECHA_LOCK_HELD makes
 * the weapons lead their shots and the mech turn itself.
 */
typedef enum
{
  MECHA_LOCK_NONE     = 0,  /* broken: no auto-turn, no lead, no guidance */
  MECHA_LOCK_SLIPPING = 1,  /* outside the cone, inside the grace period */
  MECHA_LOCK_HELD     = 2
} eMechaLockState;

//-------------------------------------------------------------------------------------------------

typedef enum
{
  MECHA_MOVE_STAND    = 0,
  MECHA_MOVE_WALK     = 1,
  MECHA_MOVE_GUARD    = 2,
  MECHA_MOVE_DASH     = 3,
  MECHA_MOVE_JUMP     = 4,
  /* Guard pressed in the air: the arc is abandoned and the mech drops. It
   * still counts as airborne, and it still poses as a jump, but nothing
   * about it is under the player's control except that it ends sooner. */
  MECHA_MOVE_CANCEL   = 5,
  /* Touchdown recovery. Nothing can be cancelled out of it, which is what
   * makes a jump attack a commitment rather than a free reposition -- the
   * one exception being a cancelled landing, which is shortened and leaves
   * the turn rate off its leash so the mech can come down facing away. */
  MECHA_MOVE_LAND     = 6,
  MECHA_MOVE_STAGGER  = 7,
  MECHA_MOVE_DOWN     = 8,
  MECHA_MOVE_RISE     = 9,
  MECHA_MOVE_DESTROYED = 10
} eMechaMoveState;

//-------------------------------------------------------------------------------------------------

typedef enum
{
  MECHA_PROJ_BULLET = 0,  /* straight line, no drop */
  MECHA_PROJ_HOMING = 1,  /* steers towards the shooter's lock */
  MECHA_PROJ_BEAM   = 2,  /* fast, flat, pierces nothing but travels far */
  MECHA_PROJ_ARC    = 3,  /* lobbed, falls under its own gravity */
  MECHA_PROJ_MINE   = 4,  /* drops, arms, then detonates on proximity */
  MECHA_PROJ_MELEE  = 5,  /* short-lived hitbox carried in front of the mech */
  /* What a bomb leaves behind: a standing fire that hurts anything walking
   * into it and swallows shots crossing it. Never a target. */
  MECHA_PROJ_SHELL  = 6
} eMechaProjectileKind;

//-------------------------------------------------------------------------------------------------

/*
 * What a melee swing looks like. Drawing only, and deliberately not another
 * eMechaProjectileKind: a club and a blade are the same hitbox on the same
 * clock, and giving them separate kinds would fork the ten places that ask
 * whether a shot is melee -- the lunge, the guard's damage scaling, the AI's
 * reach, the sound -- to no purpose. Zero is the blade, which is what every
 * weapon written before this existed still gets. [TYPE-10]
 */
typedef enum
{
  MECHA_MELEE_BLADE = 0,  /* pointed, edged, with a crossguard */
  MECHA_MELEE_CLUB  = 1,  /* blunt beam, no guard, radial spikes */
  MECHA_MELEE_COUNT = 2
} eMechaMeleeShape;

//-------------------------------------------------------------------------------------------------

/*
 * How a swing is thrown, which is decided by where the machine's feet are
 * rather than by the weapon. A machine with the floor under it winds up over
 * its shoulder and cuts across; one in the air has nothing to brace against,
 * so it turns at the waist and puts the point out in front instead.
 * Drawing and timing only -- both do the same damage. [SIM-31]
 */
typedef enum
{
  MECHA_SWING_SLASH  = 0, /* raise, then cut across: the grounded swing */
  MECHA_SWING_THRUST = 1, /* coil at the waist, then stab: the aerial one */
  MECHA_SWING_COUNT  = 2
} eMechaSwingKind;

//-------------------------------------------------------------------------------------------------

typedef enum
{
  MECHA_FX_MUZZLE    = 0,
  MECHA_FX_IMPACT    = 1,
  MECHA_FX_EXPLOSION = 2,
  MECHA_FX_DUST      = 3,
  MECHA_FX_THRUSTER  = 4,
  MECHA_FX_SPARK     = 5,
  /* A thrown, falling, cooling particle, the way the race game draws smoke
   * and flame: camera-facing squares with their own velocity. [MESH-29] */
  MECHA_FX_EMBER     = 6,
  /*
   * A puff off a damaged machine. Rises and spreads rather than falling
   * and cooling, which is the difference between something thrown off an
   * explosion and something pouring out of a hole.
   */
  MECHA_FX_SMOKE     = 7
} eMechaEffectKind;

//-------------------------------------------------------------------------------------------------

typedef enum
{
  MECHA_CONTROL_NONE  = 0,
  MECHA_CONTROL_HUMAN = 1,
  MECHA_CONTROL_AI    = 2
} eMechaController;

//-------------------------------------------------------------------------------------------------

/*
 * How good the computer pilot is. The levels are degrees of human limitation
 * put back in, not degrees of knowledge: the pilot reads the same world the
 * simulation ticks. ACE has none of them. [AI-02]
 */
typedef enum
{
  MECHA_AI_ROOKIE  = 0,
  MECHA_AI_VETERAN = 1,
  MECHA_AI_ACE     = 2,
  MECHA_AI_SKILL_COUNT
} eMechaAiSkill;

//-------------------------------------------------------------------------------------------------

typedef enum
{
  MECHA_PHASE_READY      = 0,  /* round announcement, controls locked */
  MECHA_PHASE_FIGHT      = 1,
  MECHA_PHASE_ROUND_OVER = 2,  /* someone is down, the camera lingers */
  MECHA_PHASE_MATCH_OVER = 3
} eMechaPhase;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  const char *szName;
  uint8_t byKind;           /* eMechaProjectileKind */
  uint8_t byCount;          /* shots per pull; > 1 is a spread */
  uint8_t byPalette;        /* tracer colour, palette index */
  int     iSpreadAngle;     /* angle units between adjacent spread shots */
  float   fSpeed;           /* world units per second */
  float   fDamage;
  float   fRadius;          /* projectile collision radius */
  float   fBlastRadius;     /* > 0 detonates and damages within this radius */
  float   fArcGravity;      /* world units per second squared, MECHA_PROJ_ARC */
  int     iLifeTicks;
  int     iAmmo;            /* shots before the slot has to reload */
  int     iReloadTicks;     /* empty to full */
  int     iRecoveryTicks;   /* the shooter cannot act for this long */
  int     iHomingRate;      /* angle units per tick; 0 leaves it flying straight */
  float   fStagger;         /* stagger inflicted; see MECHA_STAGGER_DOWN */
  float   fMuzzleHeight;    /* fraction of mech height the shot leaves from */
  float   fMuzzleSide;      /* lateral muzzle offset, fraction of mech radius */
  uint8_t byMelee;          /* eMechaMeleeShape; drawing only, MELEE kinds */
} tMechaWeaponDef;

//-------------------------------------------------------------------------------------------------
/*
 * A mech's fixed characteristics. The roster in mecha_defs.c is built from
 * these and never changes at runtime, so the whole table is const.
 */
typedef struct
{
  const char *szName;
  const char *szClass;

  /*
   * What the mesh builds and what it hangs on it, as eMechaChassis and
   * eMechaProfile. Drawing only: zero is a standard biped, which is what
   * every machine written before these existed still gets. [TYPE-06]
   */
  uint8_t byChassis;
  uint8_t byProfile;

  /*
   * Silhouette multipliers, so an archetype reads from across the arena
   * rather than living only in the stat block. Zero means one. [TYPE-02]
   */
  /*
   * How the machine carries its own weight: grip kills sideways velocity,
   * drive pushes towards the speed asked for, brake sheds it with nothing
   * asked. Absolute, in m/s^2, deliberately not multiples of walk speed.
   * [TYPE-03]
   */
  float fGrip;
  float fDriveAccel;
  float fBrake;

  float fBuildShoulder;
  float fBuildTorso;
  float fBuildLimb;
  float fBuildHead;
  float fBuildGun;
  /*
   * Where the hips sit, as a fraction of the machine's height, and how long
   * the arms are against the frame they hang off. Zero means the classic
   * figure -- hips at 0.47 and arms at full length -- which is what every
   * machine written before these existed still gets.
   *
   * Raising the hips lengthens the legs and shortens everything above them
   * together, because the upper body is then drawn at whatever scale still
   * puts the head where the machine's height says it goes. One number moves
   * both halves, which is the only way they stay a figure. [TYPE-08]
   */
  float fBuildHip;
  float fBuildArm;

  float fHeight;            /* world units, ground to head */
  float fRadius;            /* collision cylinder */
  float fMass;              /* scales knockback taken */

  float fArmour;            /* starting and maximum hit points */

  float fWalkSpeed;         /* world units per second */
  float fDashSpeed;
  float fAirSpeed;
  float fTurnRate;          /* angle units per second while free */
  float fJumpVelocity;      /* world units per second at takeoff */

  int   iBoostMax;
  int   iBoostDashDrain;    /* per second while dashing */
  int   iBoostJumpCost;     /* one-off, charged at takeoff */
  int   iBoostJumpDrain;    /* per second while thrusting upward */
  int   iBoostRegen;        /* per second standing or walking */
  int   iBoostGuardRegen;  /* per second guarding -- the fast refill */

  int   iDashTicks;         /* how long one dash burst lasts */
  int   iLandTicks;         /* touchdown recovery */

  /*
   * Wheels instead of legs: no strafe, no boost, no jump, one signed speed
   * along the nose. Everything else -- lock, slots, armour, stagger -- works
   * as it does for the rest of the roster. fSteerFloor is the race game's
   * own rule. [SIM-06]
   */
  bool  bWheeled;
  float fSteerFloor;
  /* What running into somebody costs them, per metre a second over the
   * speed it takes to be worth anything. A machine with no close-quarters
   * weapon still has to have an answer at close quarters. */
  float fRamDamage;
  float fRamSpeed;
  /* How hard firing shoves the machine backwards. A gun the size of the
   * car it is bolted to does not go off quietly. */
  float fRecoilPush;

  /* Palette indices the mesh builder paints with: body, trim, joints, glow. */
  uint8_t abyPalette[4];

  tMechaWeaponDef aWeapons[MECHA_WEAPON_SLOTS][MECHA_STANCE_COUNT];
} tMechaMechDef;

//-------------------------------------------------------------------------------------------------
/*
 * One tick of intent for one mech. The human's pad and the AI both produce
 * this and nothing else, so an AI mech and a player mech are literally the
 * same code path downstream.
 */
typedef struct
{
  int  iMoveX;        /* -100..100, strafe: positive is the mech's right */
  int  iMoveZ;        /* -100..100, positive is forward */
  int  iTurn;         /* -100..100, explicit turn on top of the lock */
  bool bDash;
  bool bJump;
  bool bGuard;
  bool bFireLeft;
  bool bFireCenter;
  bool bFireRight;
  bool bCycleTarget;
} tMechaInput;

//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/*
 * The drawn attitude of one machine, in the pieces the race game keeps it in.
 */
typedef struct
{
  /* The tilt that answers the stick: a car leans out of the corner, a robot
   * into it. [TYPE-04] */
  int   iRollSteer;
  /*
   * Squat and dive. Whiplash's iPitchDynamicOffset: the nose comes up on
   * the throttle at iPitchAccelRate, goes down on the brakes at
   * iPitchDecayRate, and unwinds to level at the recovery rates.
   */
  int   iPitchDrive;
  /* Airborne, the nose follows the velocity vector, as Whiplash's nPitch
   * does. [TYPE-04] */
  int   iAirPitch;
  /* Pitch and roll of the slope actually stood on, sampled across the
   * footprint and eased rather than snapped. [SIM-10] */
  int   iContourPitch;
  int   iContourRoll;

  /*
   * A car launched off a cambered surface rolls in the air, and lands on
   * its roof if it has gone far enough over. iRollSpin is the rate the
   * camber under the wheels would impart at the current speed; iAirRoll is
   * what has accumulated since the wheels left the ground. [SIM-18]
   */
  int   iRollSpin;
  int   iAirRoll;
  bool  bWasAirborne;   /* to catch the tick the wheels touch down */
  /* What is left of the last landing: a damped cosine about both axes,
   * seeded from the attitude held at contact. [TYPE-04] */
  float fWobblePitchAmp;
  float fWobbleRollAmp;
  int   iWobblePhase;
  int   iPitchWobble;
  int   iRollWobble;
  /* The body shake: white noise on all three axes, scaled by how hard the
   * machine is working. [TYPE-04] */
  int   iPitchShake;
  int   iRollShake;
  int   iYawShake;
  /*
   * What a legged machine shakes from, since it has no road speed to
   * shake from. Set by taking a hit and bled off, so the shudder belongs
   * to the blow rather than to the walking.
   */
  float fHitShake;
  /*
   * The shake has its own noise so that nothing cosmetic ever reaches into
   * the draw sequence the fight is decided from. A machine rattling on
   * screen must not be able to move an AI pilot's aim by a hair.
   */
  tMechaRng shake;
} tMechaAttitude;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  bool    bActive;
  uint8_t byDefIdx;
  uint8_t byController;     /* eMechaController */
  uint8_t byTeam;

  /* Feet position. fY is height above the arena floor, so a grounded mech
   * sits at exactly zero and airborne tests are just fY > 0. */
  float fX, fY, fZ;
  float fVelX, fVelY, fVelZ;

  int   iFacing;            /* 14-bit heading, 0 along +Z */
  int   iAimPitch;          /* 14-bit, clamped to +/- MECHA_AIM_PITCH_LIMIT */

  uint8_t byMove;           /* eMechaMoveState */
  int   iStateTicks;        /* ticks spent in byMove */

  /* Boost is stored pre-multiplied by MECHA_BOOST_SCALE, which is exactly
   * the tick rate. That makes every per-second drain and regen figure in the
   * mech definitions land as an exact per-tick integer delta, so the gauge
   * stays deterministic without carrying a fractional remainder around.
   * mecha_mech_boost_fraction() is what the HUD should read. */
  int   iBoost;
  /* Set when the gauge bottoms out. Dash and jump stay refused until the
   * gauge climbs back past MECHA_BOOST_UNLOCK, so running the tank dry
   * costs a real window rather than one frame. */
  bool  bBoostLocked;

  /* A dash keeps the heading it launched with even while the mech turns to
   * track its target, which is what makes a circle-strafing dash read the
   * way it should. */
  float fDashDirX, fDashDirZ;

  float fArmour;
  float fStagger;           /* bleeds off; crossing MECHA_STAGGER_DOWN floors the mech */
  int   iStunTicks;
  int   iInvulnTicks;

  int   aiAmmo[MECHA_WEAPON_SLOTS];
  int   aiReload[MECHA_WEAPON_SLOTS];
  int   iRecovery;          /* ticks of firing recovery left */
  /* Stops a car resting against somebody billing them every tick. */
  int   iRamCooldown;
  int   iLastFiredSlot;     /* -1 when nothing has been fired yet */
  int   iLastFiredStance;

  /* A melee swing drags the mech along with it -- the lunge is the attack.
   * Set when a MECHA_PROJ_MELEE weapon fires, and it overrides ordinary
   * movement until it runs out. */
  int   iLungeTicks;
  float fLungeSpeed;

  /*
   * The swing, as a clock the mesh animates against. iSwingTicks runs from
   * iSwingTotal down to zero; the first iSwingWindup of those ticks are the
   * wind-up, with the blade not out yet, and the rest are the strike. So
   * the elapsed count, iSwingTotal - iSwingTicks, is what says which phase
   * the machine is in. The total is carried because an animation wants a
   * fraction and the weapons differ in how long they take. [SIM-31]
   */
  int     iSwingTicks;
  int     iSwingTotal;
  int     iSwingWindup;
  uint8_t bySwingKind;      /* eMechaSwingKind */

  /* Edge detection. Every trigger in the mode fires on the press rather than
   * on the hold, and the AI produces the same held-button struct a pad does,
   * so the previous frame's state has to live with the mech. */
  bool  abFireHeld[MECHA_WEAPON_SLOTS];
  /* An outer trigger waiting to see whether the other one is coming: the
   * two together are the centre weapon. byPairMask is 1 for left, 2 for
   * right. [SIM-23] */
  int   iPairTicks;
  uint8_t byPairMask;
  bool  bJumpHeld;
  bool  bDashHeld;
  bool  bGuardHeld;
  bool  bCycleHeld;

  int   iTargetIdx;         /* who the reticle is on; -1 for nobody */
  uint8_t byLock;           /* eMechaLockState: whether it is tracking them */
  int   iLockSlipTicks;     /* ticks the target has been outside the cone */

  /* Set by a jump cancel's landing. While it runs, the manual turn is
   * uncapped and works even though the landing itself locks out control --
   * that window is the entire reason to cancel. */
  int   iFreeTurnTicks;

  /* While this runs the machine squares itself up on its lock whatever the
   * range. Set by the moves that are supposed to put the enemy back in
   * front of you -- a jump cancel, and firing while boosting or airborne. */
  /*
   * The heading the stick is read against, which is the machine's own
   * except while a move is swinging the body onto its lock. [SIM-21]
   */
  /* Index into the paint schemes; zero is the machine's own. [DEF-06] */
  uint8_t byScheme;
  int   iStickYaw;
  int   iRecentreTicks;

  /* Angular error added to the firing solution, in the shared 14-bit
   * circle. Weapons aim themselves at whatever is locked, so this is the
   * only thing separating a pilot who can shoot from one who cannot; the
   * computer pilot rolls it per shot and the player leaves it at zero. */
  int   iAimError;

  /* The way round a gap the computer pilot has settled on, as a heading in
   * the shared circle, and how long it holds it for. Without the hold it
   * re-picks every tick and walks on the spot; the player never sets
   * either. [AI-12] */
  int   iSkirtYaw;
  int   iSkirtTicks;

  /* Which of the four lines across a way this pilot walks, the way a
   * Whiplash driver picks one of its four AI lines: sixteen machines down
   * one line is a queue rather than a fight. [AI-13] */
  uint8_t byAiLine;

  /* The tick the machine went down on: the grace is the tick rather than
   * the hit, so a whole volley still counts. [SIM-04] */
  int   iDownTick;

  /* How much of the wreck's burn is left. Set when the machine is destroyed
   * and counted down; a wreck throws fire for as long as it runs. It is its
   * own field rather than a reading off iStateTicks because a destroyed
   * machine is no longer moved and nothing else advances a clock for it.
   * [SIM-27] */
  int   iBurnTicks;

  /*
   * When each of the things worth hearing or announcing last happened, as
   * ticks. The simulation writes them and never reads them back; sound and
   * the HUD are the only things that care. Ticks rather than flags because
   * a frame can run several ticks and a flag set inside one would be gone
   * before anything looked, and ticks rather than counters because what the
   * HUD wants is "how long ago", which a counter cannot answer. -1 is never,
   * and the match's own tick starts at zero. [TYPE-09]
   */
  int   iFireTick;      /* a shot left a barrel */
  int   iDryFireTick;   /* a trigger was pulled on an empty or reloading slot */
  int   iHitTakenTick;  /* something landed on this machine */
  int   iHitDealtTick;  /* this machine landed something on somebody else */

  /* Noise for the damage particles. Private, because nothing cosmetic may
   * reach into the sequence a fight is decided from. [SIM-02] */
  tMechaRng spray;

  int   iRoundsWon;
  float fDamageDealt;

  /*
   * How the body sits, as against where the machine is: independent pieces
   * summed at the last moment, the way car.c composes a render pose. All
   * cosmetic, all in the 14-bit circle, never read back. [TYPE-04]
   */
  tMechaAttitude attitude;

  /* Rendering-only smoothing; the simulation never reads these back. */
  float fLeanRoll;
  float fStepPhase;
  /* How much of a fight the machine thinks it is in. Everything above the
   * hips reads it; the simulation never does. [TYPE-05] */
  float fCombat;
  /* Where the feet point, which is not where the machine points. The one
   * piece of animation state the sim owns. [TYPE-05] */
  int   iLegYaw;
  bool  bLegsBackward;      /* stepping backwards: the cycle runs in reverse */
  /* What is left of a boost after the burst: the speed carries and the
   * steering is feeble. [SIM-08] */
  int   iCoastTicks;
  /* Where the ground was under it last tick. The difference is how fast the
   * ground is rising, which on a surface that does not hold a machine down
   * is what throws it off the top of a slope. */
  float fGroundY;
  /* Whether the stick has been let go since this dash began: the release is
   * what makes the crossing step a choice. [SIM-08] */
  bool  bDashStickFree;

  /* Fixed-point ragdoll pose state.  Angles and angular velocities are in
   * the same 14-bit units as the rest of the procedural rig, so the pose is
   * replayable without a render-time random source. */
  int16_t aiRagdollAngle[MECHA_RAGDOLL_BONES][MECHA_RAGDOLL_AXES];
  int32_t aiRagdollVelocity[MECHA_RAGDOLL_BONES][MECHA_RAGDOLL_AXES];
  int     iRagdollImpactYaw;
  int     iRagdollImpactStrength;
} tMechaMech;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  bool    bActive;
  uint8_t byKind;           /* eMechaProjectileKind */
  uint8_t byOwner;
  uint8_t byPalette;
  uint8_t byMelee;          /* eMechaMeleeShape; drawing only */

  float fX, fY, fZ;
  float fPrevX, fPrevY, fPrevZ;   /* start of this tick's segment */
  float fVelX, fVelY, fVelZ;

  float fRadius;
  float fDamage;
  float fBlastRadius;
  float fStagger;
  float fArcGravity;

  int   iLife;
  int   iAge;               /* ticks since launch, for reaction timing */
  int   iHomingRate;
  int   iTarget;            /* -1 for unguided */
  int   iArmTicks;          /* mines ignore everything until this reaches zero */
  /*
   * What a mine that has settled gets back when it goes hunting. A mine with
   * no gravity in it never lands, so it never stops on its own; it bleeds its
   * throw off over the arming time instead and then, if it was built to hunt,
   * moves off at this. [SIM-26]
   */
  float fHuntSpeed;
  /*
   * One bit per mech, for a shell: who has already been burned by it. The
   * blast that spawns it hits everyone standing inside at the time, so those
   * are marked at birth and the shell only catches whoever walks in after.
   */
  uint8_t byHitMask;
} tMechaProjectile;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  bool    bActive;
  uint8_t byKind;           /* eMechaEffectKind */
  uint8_t byPalette;
  float   fX, fY, fZ;
  float   fVelX, fVelY, fVelZ;
  float   fScale;
  int     iAge;
  int     iLife;
} tMechaEffect;

//-------------------------------------------------------------------------------------------------
/*
 * A box standing on the arena floor. Mechs slide along its sides, shots stop
 * against it, and the AI uses it for cover, so one shape covers every
 * obstacle the arena needs.
 */
/* What a piece of cover is made of. All three collide as the same box; the
 * difference is what gets drawn around it. */
typedef enum
{
  MECHA_PROP_BLOCK = 0,
  MECHA_PROP_TREE  = 1,
  MECHA_PROP_ROCK  = 2,
  MECHA_PROP_SPIRE = 3    /* comes to a point: a roof, not a block */
} eMechaPropKind;

typedef struct
{
  float fX, fZ;             /* centre on the ground plane */
  float fHalfX, fHalfZ;
  /* How tall it stands above the ground it is on, and where that ground is.
   * The mesh always drew a box from the terrain under it while the collision
   * read the height as absolute; on level ground those agree and on a slope
   * they do not. fBaseY is filled in once the arena's ground is finished, and
   * both sides use it. [ARENA-18] */
  float fHeight;
  float fBaseY;
  /*
   * How far above that ground the underside of it is. Zero for anything
   * standing on the floor, which is everything the arenas had until there
   * was a building with two storeys in it: a deck with a rise is solid
   * between fBaseY + fRise and fBaseY + fRise + fHeight, and the space
   * underneath it is a room. [ARENA-23]
   */
  float fRise;
  uint8_t byKind;           /* eMechaPropKind */
  uint8_t byPalette;
  uint8_t byTrimPalette;
  /* Tiles in one of the game's texture banks, used when the retail data is
   * there. The palette entries above stay the fallback and the shading. */
  uint8_t byTile;
  uint8_t byTopTile;
  /*
   * Which bank those tiles are in. Cover started out as buildings and the
   * building bank was the only one it ever wanted; a stage whose structures
   * are concrete and rock takes them from the track's own bank instead, and
   * the two banks number their tiles independently. [ARENA-28]
   */
  uint8_t byBank;
  /*
   * A run of tiles to scatter over the sides of it, so a wall a hundred
   * panels wide is not a hundred copies of one panel. Zero count is a box
   * that is all one tile, which is every piece of cover in the roster's
   * other arenas. [ARENA-28]
   */
  uint8_t byDetailFirst;
  uint8_t byDetailCount;
} tMechaObstacle;

//-------------------------------------------------------------------------------------------------

/* One station on a way across the arena: where the middle of it is, and how
 * far either side of that there is still ground. [AI-13] */
typedef struct
{
  float fX;
  float fZ;
  float fHalf;
} tMechaWayPoint;

typedef struct
{
  int             iCount;
  tMechaWayPoint  aPoints[MECHA_WAY_POINTS];
  /* Where this way comes closest to each of the others: the station to walk
   * to when the enemy is on one of them. Two lanes either side of a hole
   * are joined by exactly one crossing, and this is how a pilot finds it.
   * [AI-13] */
  int             aiLink[MECHA_MAX_WAYS];
} tMechaWay;

//-------------------------------------------------------------------------------------------------

/*
 * What the boundary is. A square arena is walled on four sides, an octagon
 * on eight, and an open one is not walled at all -- its floor simply stops,
 * and so does anything that walks off it.
 */
/* How an arena lays out its starting positions. [ARENA-14] */
typedef enum
{
  MECHA_SPAWN_RING  = 0,    /* a circle inside the boundary */
  MECHA_SPAWN_BASES = 1     /* in the two strongholds, alternating ends */
} eMechaSpawnShape;

typedef enum
{
  MECHA_ARENA_SQUARE  = 0,
  MECHA_ARENA_OCTAGON = 1,
  MECHA_ARENA_OPEN    = 2
} eMechaArenaShape;

typedef struct
{
  const char *szName;
  uint8_t byShape;          /* eMechaArenaShape */
  float fHalfExtent;        /* wall to wall is twice this, or floor to floor */
  float fWallHeight;
  uint8_t byFloorPalette;
  uint8_t byGridPalette;
  uint8_t byWallPalette;
  /* Tiles in the game's track bank, which is where its ground, grass and
   * wall artwork lives. Zero means this surface stays flat-shaded. */
  uint8_t byFloorTile;
  uint8_t byGridTile;
  uint8_t byWallTile;
  /*
   * Or a longer cycle than two. A floor is a checkerboard of two tiles
   * because a floor wants to read as a grid to move over; ground that is
   * meant to read as rock wants a rotation with no pattern in it short
   * enough to see. Count zero keeps the pair above. [ARENA-28]
   */
  uint8_t abyGroundTile[4];
  uint8_t byGroundTileCount;
  int   iObstacleCount;
  tMechaObstacle aObstacles[MECHA_MAX_OBSTACLES];

  /*
   * The ways across, for arenas whose ground does not join up. Whiplash
   * gives its computer drivers four AI lines a chunk and has them aim at a
   * point interpolated along the one they are on; a causeway is the same
   * thing with the track taken away, so an arena that has one publishes it
   * as a chain of stations and the pilots read it the same way. An arena
   * whose floor is one piece publishes none, and nothing changes for it.
   * [AI-13]
   */
  int       iWayCount;
  tMechaWay aWays[MECHA_MAX_WAYS];

  /*
   * The ground itself. afNode holds a height per grid corner and auiSurface
   * a surface word per cell; a level arena leaves both at zero and behaves
   * exactly as it did before either existed.
   */
  float    afNode[MECHA_TERRAIN_NODES][MECHA_TERRAIN_NODES];
  uint32_t auiSurface[MECHA_TERRAIN_CELLS][MECHA_TERRAIN_CELLS];
  /* Below this a machine is gone, however it got there. */
  float    fKillY;

  /*
   * A raised hexagonal mesa, answered by the height query rather than
   * written into the grid so its edges stay hexagonal. Zero height is none.
   * Both radii are apothems. [ARENA-10]
   */
  float    fMesaTop;
  float    fMesaBase;
  float    fMesaHeight;

  /*
   * How far below itself an open arena's edge is drawn. Six metres reads as
   * a platform; two hundred reads as the top of a tower. Zero draws none of
   * it, which is right for an arena whose ground does not reach its own
   * boundary.
   */
  float    fSkirt;

  /*
   * How far a drawn ground quad may fall below its own highest corner before
   * the rest of it is simply not there. An arena built as an island in a
   * void has cells straddling the edge whose outer corners sit at the bottom
   * of it, and drawn honestly those are a curtain running hundreds of metres
   * down -- the stage reads as the summit of a mountain rather than as
   * something floating. Cutting them off a few cells under the deck leaves
   * an edge with a thickness and nothing below it. Zero is no limit, and
   * every arena whose ground is one piece wants zero. [ARENA-20]
   *
   * Drawing only: the ground a machine stands on is unchanged.
   */
  float    fDeckDrop;

  /*
   * The bottom of the hole an island stage is standing in: what
   * mecha_arena_void() was last asked for, as a height. Ground at it is not
   * part of the stage and is not drawn at all when fDeckDrop is set, which
   * is what stops the floor of the void reading as a second deck below the
   * first. Meaningless, and ignored, on an arena whose ground is one piece.
   * [ARENA-22]
   */
  float    fVoidY;

  /*
   * And the lowest ground the stage itself has, worked out once when the
   * arena is built. The two together are what the cut is measured between:
   * ground more than fDeckDrop below fDeckY is the hole, not the stage.
   * Only meaningful where fDeckDrop is set. [ARENA-22]
   */
  float    fDeckY;

  /*
   * The sky. bySkyKind picks what hangs in it (eMechaSkyKind), and anything
   * that is not MECHA_SKY_CLOUDS replaces the horizon entirely with the flat
   * colour in bySkyFill -- a stage that is not on a planet has no horizon to
   * draw. The kind decides that, not the colour: black is palette index 0,
   * so a fill of zero cannot also mean "no fill". [MESH-49]
   */
  uint8_t  bySkyKind;
  uint8_t  bySkyFill;

  /*
   * Where machines start. A ring inside the boundary is right for every
   * arena whose floor fills its own square; an arena that is two lanes with
   * a hole down the middle has to place them along the lanes instead, or
   * half of them start over the hole. [ARENA-14]
   */
  uint8_t  bySpawnShape;    /* eMechaSpawnShape */
  float    fSpawnHalfX;
  float    fSpawnHalfZ;

  /* Ground drawn past the boundary, and scenery for it. Not walkable; zero
   * reach draws none of it. [ARENA-07] */
  float    fOuterReach;
  int      iBillboards;

  /* How finely the ground is drawn, which is not how finely it is shaped:
   * zero takes the default. A bigger arena wants more of them or its tiles
   * come out stretched. */
  int      iFloorTiles;
  /* And how finely it is shaped, which is the grid above. Zero takes
   * MECHA_TERRAIN_CELLS_DEFAULT; nothing may exceed MECHA_TERRAIN_CELLS. */
  int      iTerrainCells;

  /*
   * How well the ground holds a wheel, as one of the race game's fourteen
   * grades. Zero is the best, so an arena that says nothing gets the best of
   * it and only a slippery one has to say so. [ARENA-13]
   */
  uint8_t  byGripLevel;
} tMechaArena;

//-------------------------------------------------------------------------------------------------

/*
 * A paint scheme: a body colour, a trim colour and a joint colour, replacing
 * the three the machine's own definition carries. [DEF-06]
 */
typedef struct
{
  const char *szName;
  uint8_t     byBody;
  uint8_t     byTrim;
  uint8_t     byJoint;
} tMechaScheme;

typedef struct
{
  uint8_t byPhase;          /* eMechaPhase */
  int  iPhaseTicks;
  int  iRound;              /* 1-based */
  int  iRoundsToWin;
  int  iRoundTicks;         /* counts down; zero is time up */
  int  iRoundTimeLimit;
  int  iWinnerIdx;          /* -1 for a draw or an unfinished round */
} tMechaMatch;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  tMechaArena       arena;
  tMechaMatch       match;
  tMechaMech        aMechs[MECHA_MAX_MECHS];
  tMechaProjectile  aProjectiles[MECHA_MAX_PROJECTILES];
  tMechaEffect      aEffects[MECHA_MAX_EFFECTS];
  tMechaRng         rng;
  uint32_t          uiSeed;
  int               iTick;      /* ticks since the match started */
  int               iMechCount;
  uint8_t           byAiSkill;  /* eMechaAiSkill, applies to every AI mech */
  /*
   * A debug switch, not a difficulty: the computer pilots go on fighting for
   * position exactly as they would, they simply never pull a trigger. It is
   * there so the movement can be looked at without being shot while looking.
   */
  bool              bAiHoldFire;
} tMechaWorld;

//-------------------------------------------------------------------------------------------------
#endif
