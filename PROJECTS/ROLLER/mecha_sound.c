//-------------------------------------------------------------------------------------------------
/*
 * The arena's sound, played through Whiplash's mixer. See mecha_sound.h for
 * the shape of it and docs/arena-notes.md [SND-01] for why it reads the
 * world instead of being told about it.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_sound.h"

#include "mecha_defs.h"
#include "mecha_math.h"
#include "mecha_render.h"
#include "mecha_sim.h"

#include "sound.h"

#include <math.h>
#include <string.h>

//-------------------------------------------------------------------------------------------------

/* Which of Whiplash's samples the arena borrows, and what it uses them as. */
#define MECHA_SFX_ENGINE  SOUND_SAMPLE_ENGINE   /* the machine, idling to flat out */
#define MECHA_SFX_SKID    SOUND_SAMPLE_SKID1    /* thrusters scrubbing the floor */
#define MECHA_SFX_LAND    SOUND_SAMPLE_LANDSKID /* coming down off a jump */
#define MECHA_SFX_BLAST   SOUND_SAMPLE_EXPLO    /* a shell going off */
#define MECHA_SFX_WRECK   SOUND_SAMPLE_BIGCRASH /* a machine going up */
#define MECHA_SFX_HIT     SOUND_SAMPLE_FENDER   /* two machines meeting */
/*
 * The guns. Whiplash has no laser in it, so each of these is a car noise
 * put to a new use and moved off its recorded pitch: the gear change is a
 * breech, the water blip a bolt, the light landing a launch tube and the
 * menu click a mine going down. Pitch is what separates a light gun from a
 * heavy one, which is the whole reason the mixer grew a pitched one-shot.
 * [SND-04]
 */
#define MECHA_SFX_SLUG    SOUND_SAMPLE_GRSHIFT  /* solid rounds: a breech clack */
#define MECHA_SFX_BOLT    SOUND_SAMPLE_BLOP     /* energy: a blip                */
#define MECHA_SFX_LAUNCH  SOUND_SAMPLE_LIGHTLAN /* pods and lobbed charges       */
#define MECHA_SFX_LAY     SOUND_SAMPLE_BUTTON   /* a mine going down             */
/*
 * The two cockpit warnings. Being hit is a buzz taken below the rate it was
 * cut at; a trigger pulled on an empty gun is a dead mechanical clunk, not
 * a beep. The same buzz taken up instead was a car horn, which is what it
 * sounds like when a bright broadband sample is pitched up. [SND-05]
 */
#define MECHA_SFX_WARN    SOUND_SAMPLE_BRP
#define MECHA_SFX_DRY     SOUND_SAMPLE_BANK

/*
 * Inverse-square with a floor, exactly as enginesound() has it: the constant
 * is the distance squared at which a sound is half as loud, so a machine is
 * down to half volume 32 m away.
 */
#define MECHA_SND_ROLLOFF     65536000.0f
/* Below this the mixer is being asked for silence, so ask for nothing. */
#define MECHA_SND_FLOOR       256
#define MECHA_SND_FULL        0x7FFF
/* Whiplash's own pitch base for a looped engine, and the span an engine
 * covers between idle and flat out. */
#define MECHA_SND_PITCH_BASE  8192.0f
#define MECHA_SND_PITCH_SPAN  100000.0f
/* A walker's servos are a narrower band than an engine: it is a hum that
 * rises as it moves, not a rev range. */
#define MECHA_SND_SERVO_SPAN  34000.0f
/*
 * The scrub's rate at a standstill, what the quickest machine on the roster
 * adds to it, and the speed that counts as quickest. The old pair ran the
 * sample to 2.7x at an ordinary dash and 4.6x at the fastest, which is not
 * a tyre -- it is a whistle. They were set for a slide at walking pace and
 * never revisited when the squeal moved onto the boost. [SND-06]
 */
#define MECHA_SND_SKID_REST   0.80f
#define MECHA_SND_SKID_RISE   0.70f
#define MECHA_SND_SKID_TOP    MECHA_MPS(92.0f)
/* Sound travels at 343 m/s here as anywhere else. */
#define MECHA_SND_MACH        MECHA_MPS(343.0f)
/*
 * The squeal belongs to the boost, not to sliding. It used to key off the
 * angle between where a machine pointed and where it was going, which is
 * how Whiplash spots a car sliding -- but a mecha strafes for a living, and
 * a machine walking sideways is not scrubbing anything. What does scrub is
 * a ground dash: thrusters lit with the feet still down, in any direction.
 * [SND-06]
 */
#define MECHA_SND_BOOST_LEVEL 0.85f
/* How hard a landing has to be to be worth a sample, and what counts as the
 * full-volume one. */
#define MECHA_SND_LAND_SOFT   MECHA_MPS(6.0f)
#define MECHA_SND_LAND_HARD   MECHA_MPS(34.0f)

/*
 * The walk. fStepPhase counts strides, and both a biped and the arachnid's
 * tripod put a foot down twice a stride, so one figure covers every machine
 * with legs. The servos wind up through the swing and drop as the foot goes
 * down; the depth of it follows how fast the machine is actually walking,
 * so a machine standing still hums flat. [SND-07]
 */
#define MECHA_SND_GAIT_STEPS  2.0f
#define MECHA_SND_GAIT_VOL    0.34f
#define MECHA_SND_GAIT_PITCH  0.16f

/* The rate a sample was recorded at, as the mixer counts pitch. */
#define MECHA_SND_NATIVE      0x10000
/*
 * A gun's voice follows its weight: the lightest round in the roster does
 * 26 damage and the heaviest 148, and the same sample played across this
 * span is the difference between a rifle and a siege gun. [SND-04]
 */
#define MECHA_SND_GUN_LIGHT   26.0f
#define MECHA_SND_GUN_HEAVY   150.0f
#define MECHA_SND_GUN_HIGH    1.30f
#define MECHA_SND_GUN_LOW     0.72f
#define MECHA_SND_GUN_LEVEL   0.72f
/* And the two warnings. [SND-05] */
#define MECHA_SND_WARN_HURT   0.80f
#define MECHA_SND_WARN_DRY    1.30f
#define MECHA_SND_WARN_LEVEL  0.85f
#define MECHA_SND_DRY_LEVEL   0.80f
/*
 * Two blasts inside a sixth of a second are not two blasts. pannedsample
 * keys its handle on the sample index alone, so the second stops the first
 * part way through and what comes out is a stutter rather than a bang. A
 * burning wreck throws one every few ticks [SIM-27], so the channel has to
 * be held open between them. [SND-08]
 */
#define MECHA_SND_BLAST_GAP   MECHA_SEC(0.17f)

//-------------------------------------------------------------------------------------------------

/* What the update compares against to spot the things that make a noise. */
static bool  s_abEffectWas[MECHA_MAX_EFFECTS];
static int   s_aiRamWas[MECHA_MAX_MECHS];
static bool  s_abAirborneWas[MECHA_MAX_MECHS];
static float s_afFallWas[MECHA_MAX_MECHS];
static bool  s_abEngineOn[MECHA_MAX_MECHS];
static bool  s_abSkidOn[MECHA_MAX_MECHS];
/* The event ticks as they stood last frame. -1 is the sim's own "never", so
 * these start there too and a thing that happens on tick zero is still new.
 * [TYPE-09] */
static int   s_aiFireWas[MECHA_MAX_MECHS];
static int   s_aiDryFireWas[MECHA_MAX_MECHS];
static int   s_aiHitTakenWas[MECHA_MAX_MECHS];
/* Which machines were already wrecked last frame, so a kill sounds once
 * rather than every frame it is dead for. [SND-08] */
static bool  s_abWreckedWas[MECHA_MAX_MECHS];
/* And when the blast sample last started, so a burning wreck is a roll of
 * explosions rather than one sample restarted forever. [SND-08] */
static int   s_iBlastTick;
static bool  s_bActive;

//-------------------------------------------------------------------------------------------------

/* Everything the listener needs to know about one noise in the arena. */
typedef struct
{
  int   iPan;               /* 0 hard left, 0x8000 centre, 0xFFFF hard right */
  float fAttenuation;       /* 1 at the listener, falling off with distance */
  float fDoppler;           /* pitch multiplier from closing speed */
} tMechaSoundPlace;

/*
 * Where a point in the arena sits relative to the listener. The pan and the
 * doppler are worked out the way enginesounds() works them out for a car:
 * pan off the angle between the camera's right and forward axes, doppler off
 * the closing speed along the line between the two. [SND-02]
 */
static void mecha_sound_place(const tMechaCamera *pCamera, float fX, float fY,
                              float fZ, float fVelX, float fVelY, float fVelZ,
                              tMechaSoundPlace *pOut)
{
  float afRight[3];
  float afUp[3];
  float afForward[3];
  float fDx;
  float fDy;
  float fDz;
  float fDistSq;
  float fDist;
  float fSide;
  float fAhead;
  float fClosing;
  double dPan;

  pOut->iPan = 0x8000;
  pOut->fAttenuation = 0.0f;
  pOut->fDoppler = 1.0f;
  if (!pCamera)
    return;

  mecha_camera_basis(pCamera, afRight, afUp, afForward);
  fDx = fX - pCamera->fX;
  fDy = fY - pCamera->fY;
  fDz = fZ - pCamera->fZ;
  fDistSq = fDx * fDx + fDy * fDy + fDz * fDz;
  fDist = sqrtf(fDistSq);
  pOut->fAttenuation = MECHA_SND_ROLLOFF / (fDistSq + MECHA_SND_ROLLOFF);

  fSide = fDx * afRight[0] + fDy * afRight[1] + fDz * afRight[2];
  fAhead = fDx * afForward[0] + fDy * afForward[1] + fDz * afForward[2];
  /*
   * Straight ahead is centre, straight out to one side is hard over, and
   * behind folds back onto the near side -- which is all two speakers can
   * say. Zero is hard left and 0xFFFF hard right: DIGISetPanLocation turns
   * this into iPan / 0x8000 - 1 and hands it to the mixer as -1 left to +1
   * right, which is what decides the sign here. [SND-02]
   */
  dPan = (1.0 + (double)mecha_sin(mecha_atan2_angle(fSide, fAhead)))
         * 32768.0;
  if (dPan < 0.0)
    dPan = 0.0;
  pOut->iPan = dPan >= 65535.0 ? 0xFFFF : (int)dPan;

  /* Closing on the listener raises the pitch, opening away drops it. */
  if (fDist > 1.0f) {
    fClosing = (fVelX * fDx + fVelY * fDy + fVelZ * fDz) / fDist;
    if (fClosing > MECHA_SND_MACH * 0.5f)
      fClosing = MECHA_SND_MACH * 0.5f;
    if (fClosing < -MECHA_SND_MACH * 0.5f)
      fClosing = -MECHA_SND_MACH * 0.5f;
    pOut->fDoppler = MECHA_SND_MACH / (MECHA_SND_MACH - fClosing);
  }
}

//-------------------------------------------------------------------------------------------------

/* A volume that has been through the distance and the player's own setting,
 * or zero when it is not worth the mixer's time. */
static int mecha_sound_volume(float fLevel, const tMechaSoundPlace *pPlace,
                              int iSetting)
{
  float fVolume;

  if (fLevel <= 0.0f)
    return 0;
  if (fLevel > 1.0f)
    fLevel = 1.0f;
  fVolume = fLevel * (float)MECHA_SND_FULL * pPlace->fAttenuation
            * ((float)iSetting / 127.0f);
  if (fVolume < (float)MECHA_SND_FLOOR)
    return 0;
  return fVolume > (float)MECHA_SND_FULL ? MECHA_SND_FULL : (int)fVolume;
}

//-------------------------------------------------------------------------------------------------

/* A one-shot somewhere in the arena. */
static void mecha_sound_shot(int iSample, float fLevel,
                             const tMechaSoundPlace *pPlace)
{
  int iVolume = mecha_sound_volume(fLevel, pPlace, SFXVolume);

  if (iVolume > 0)
    pannedsample(iSample, iVolume, pPlace->iPan);
}

//-------------------------------------------------------------------------------------------------

void mecha_sound_enter(void)
{
  static const int aiSamples[] = {
    MECHA_SFX_ENGINE, MECHA_SFX_SKID, MECHA_SFX_LAND,
    MECHA_SFX_BLAST, MECHA_SFX_WRECK, MECHA_SFX_HIT,
    MECHA_SFX_SLUG, MECHA_SFX_BOLT, MECHA_SFX_LAUNCH,
    MECHA_SFX_LAY, MECHA_SFX_WARN, MECHA_SFX_DRY,
  };
  size_t i;

  memset(s_abEffectWas, 0, sizeof(s_abEffectWas));
  memset(s_aiRamWas, 0, sizeof(s_aiRamWas));
  memset(s_abAirborneWas, 0, sizeof(s_abAirborneWas));
  memset(s_afFallWas, 0, sizeof(s_afFallWas));
  memset(s_abEngineOn, 0, sizeof(s_abEngineOn));
  memset(s_abSkidOn, 0, sizeof(s_abSkidOn));
  /* Not memset: -1 is the sim's "it has not happened", and zero is a real
   * tick a match can fire on. [TYPE-09] */
  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    s_aiFireWas[i] = -1;
    s_aiDryFireWas[i] = -1;
    s_aiHitTakenWas[i] = -1;
  }
  s_iBlastTick = -MECHA_SND_BLAST_GAP;
  memset(s_abWreckedWas, 0, sizeof(s_abWreckedWas));
  s_bActive = true;

  /* The race loads the whole set when it starts; the arena wants twelve of
   * them, and a sample already in memory is not loaded twice. */
  for (i = 0; i < sizeof(aiSamples) / sizeof(aiSamples[0]); i++)
    if (!SamplePtr[aiSamples[i]])
      loadasample(aiSamples[i]);
  initsounds();
}

//-------------------------------------------------------------------------------------------------

void mecha_sound_exit(void)
{
  int i;

  if (!s_bActive)
    return;
  /* Volume zero is how loopsample is told to stop. */
  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    loopsample(i, MECHA_SFX_ENGINE, 0, 0, 0x8000);
    loopsample(i, MECHA_SFX_SKID, 0, 0, 0x8000);
    s_abEngineOn[i] = false;
    s_abSkidOn[i] = false;
  }
  stopmusic();
  s_bActive = false;
}

//-------------------------------------------------------------------------------------------------

void mecha_sound_briefing(void)
{
  if (s_bActive)
    startmusic(optionssong);
}

//-------------------------------------------------------------------------------------------------

void mecha_sound_match(void)
{
  if (s_bActive)
    stopmusic();
}

//-------------------------------------------------------------------------------------------------

/* Whether this chassis walks. Wheels and tracks roll: their noise follows
 * road speed and nothing else, which is what it already did. [SND-07] */
static bool mecha_sound_has_legs(const tMechaMechDef *pDef)
{
  return pDef->byChassis == MECHA_CHASSIS_BIPED
         || pDef->byChassis == MECHA_CHASSIS_ARACHNID;
}

/*
 * One machine's engine and thrusters. The pitch is Whiplash's: a base the
 * sample was recorded at, plus a span the machine's own speed rides up,
 * times the doppler shift. A walker gets a narrower span, because servos hum
 * where an engine revs, and its whole voice then rises and falls with the
 * gait. [SND-03, SND-07]
 */
static void mecha_sound_machine(const tMechaWorld *pWorld, int iMechIdx,
                                const tMechaCamera *pCamera)
{
  const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef;
  tMechaSoundPlace place;
  float fSpeed;
  float fTop;
  float fRatio;
  float fLevel;
  float fSpan;
  float fGait;
  int iVolume;
  int iPitch;

  if (!pMech->bActive || !mecha_mech_alive(pMech)) {
    if (s_abEngineOn[iMechIdx]) {
      loopsample(iMechIdx, MECHA_SFX_ENGINE, 0, 0, 0x8000);
      s_abEngineOn[iMechIdx] = false;
    }
    if (s_abSkidOn[iMechIdx]) {
      loopsample(iMechIdx, MECHA_SFX_SKID, 0, 0, 0x8000);
      s_abSkidOn[iMechIdx] = false;
    }
    return;
  }

  pDef = mecha_def_get((int)pMech->byDefIdx);
  mecha_sound_place(pCamera, pMech->fX, mecha_mech_centre_height(pWorld, iMechIdx),
                    pMech->fZ, pMech->fVelX, pMech->fVelY, pMech->fVelZ, &place);

  fSpeed = mecha_length2(pMech->fVelX, pMech->fVelZ);
  fTop = pDef->fWalkSpeed > 1.0f ? pDef->fWalkSpeed : 1.0f;
  fRatio = fSpeed / fTop;
  if (fRatio > 1.6f)
    fRatio = 1.6f;

  /* Idling is still a noise, and a machine on its thrusters is the loudest
   * thing short of one coming apart. */
  fLevel = 0.32f + fRatio * 0.48f;
  if (pMech->byMove == MECHA_MOVE_DASH || pMech->byMove == MECHA_MOVE_JUMP)
    fLevel += 0.20f;
  fSpan = pDef->bWheeled ? MECHA_SND_PITCH_SPAN : MECHA_SND_SERVO_SPAN;
  fGait = 1.0f;

  /*
   * A machine with legs is not a machine with a throttle: what the ear
   * follows is the stride, so the loop swings either side of where the road
   * speed alone would put it, once per footfall. On the ground only -- feet
   * in the air are not walking, and a machine gliding on its thrusters
   * should not sound like one striding. [SND-07]
   */
  if (mecha_sound_has_legs(pDef) && !mecha_mech_is_airborne(pWorld, iMechIdx)
      && pMech->byMove != MECHA_MOVE_DASH) {
    float fCycle = pMech->fStepPhase * MECHA_SND_GAIT_STEPS;
    float fDepth = fRatio > 1.0f ? 1.0f : fRatio;
    float fSwing;

    fCycle -= floorf(fCycle);
    fSwing = mecha_sin((int)(fCycle * (float)MECHA_ANGLE_FULL)) * fDepth;
    fLevel *= 1.0f + MECHA_SND_GAIT_VOL * fSwing;
    fGait = 1.0f + MECHA_SND_GAIT_PITCH * fSwing;
  }

  iVolume = mecha_sound_volume(fLevel, &place, EngineVolume);
  iPitch = (int)((MECHA_SND_PITCH_BASE + fRatio * fSpan) * fGait
                 * place.fDoppler);
  loopsample(iMechIdx, MECHA_SFX_ENGINE, iVolume, iPitch, place.iPan);
  s_abEngineOn[iMechIdx] = iVolume > 0;

  /*
   * The scrub. A ground dash is thrusters lit with the machine still on the
   * floor, whichever way it is pointed and whichever way it is going; that
   * is what tears at the ground, and it is the one thing in the arena that
   * should squeal. [SND-06]
   */
  fLevel = 0.0f;
  if (pMech->byMove == MECHA_MOVE_DASH
      && !mecha_mech_is_airborne(pWorld, iMechIdx)) {
    fLevel = MECHA_SND_BOOST_LEVEL * (fRatio > 1.0f ? 1.0f : fRatio);
  }
  iVolume = mecha_sound_volume(fLevel, &place, SFXVolume);
  {
    float fScrub = fSpeed / MECHA_SND_SKID_TOP;

    if (fScrub > 1.0f)
      fScrub = 1.0f;
    iPitch = (int)((float)MECHA_SND_NATIVE
                   * (MECHA_SND_SKID_REST + MECHA_SND_SKID_RISE * fScrub)
                   * place.fDoppler);
  }
  loopsample(iMechIdx, MECHA_SFX_SKID, iVolume, iPitch, place.iPan);
  s_abSkidOn[iMechIdx] = iVolume > 0;
}

//-------------------------------------------------------------------------------------------------

/* A one-shot off its recorded rate. Same placement as any other, but the
 * mixer is asked for a playback ratio too. [SND-04] */
static void mecha_sound_pitched(int iSample, float fLevel, float fRate,
                                const tMechaSoundPlace *pPlace)
{
  int iVolume = mecha_sound_volume(fLevel, pPlace, SFXVolume);

  if (iVolume > 0)
    pitchedsample(iSample, iVolume,
                  (int)((float)MECHA_SND_NATIVE * fRate * pPlace->fDoppler),
                  pPlace->iPan);
}

/* A cockpit warning: the player's own machine talking to the player, so it
 * is neither placed nor attenuated -- it is not in the arena. [SND-05] */
static void mecha_sound_warn(int iSample, float fLevel, float fRate)
{
  float fVolume = fLevel * (float)MECHA_SND_FULL
                  * ((float)SFXVolume / 127.0f);

  if (fVolume < (float)MECHA_SND_FLOOR)
    return;
  if (fVolume > (float)MECHA_SND_FULL)
    fVolume = (float)MECHA_SND_FULL;
  pitchedsample(iSample, (int)fVolume,
                (int)((float)MECHA_SND_NATIVE * fRate), 0x8000);
}

/* Which noise a weapon makes, and how far off its recorded rate. A heavier
 * round speaks lower. [SND-04] */
static int mecha_sound_gun(const tMechaWeaponDef *pWeapon, float *pfRate)
{
  float fSpan = MECHA_SND_GUN_HEAVY - MECHA_SND_GUN_LIGHT;
  float fWeight = (pWeapon->fDamage - MECHA_SND_GUN_LIGHT) / fSpan;

  fWeight = mecha_clampf(fWeight, 0.0f, 1.0f);
  *pfRate = MECHA_SND_GUN_HIGH
            + (MECHA_SND_GUN_LOW - MECHA_SND_GUN_HIGH) * fWeight;

  switch (pWeapon->byKind) {
  case MECHA_PROJ_BEAM:
    return MECHA_SFX_BOLT;
  case MECHA_PROJ_ARC:
    return MECHA_SFX_LAUNCH;
  case MECHA_PROJ_HOMING:
    /* Pods leave their tubes brighter than a lobbed charge does, whatever
     * they weigh. */
    *pfRate *= 1.35f;
    return MECHA_SFX_LAUNCH;
  case MECHA_PROJ_MINE:
    *pfRate = 1.0f;
    return MECHA_SFX_LAY;
  case MECHA_PROJ_MELEE:
    /* The skid sample, short and metallic, taken well up: a blade coming
     * round rather than a tyre. */
    *pfRate = 1.7f;
    return MECHA_SFX_SKID;
  case MECHA_PROJ_SHELL:
    *pfRate = 0.85f;
    return MECHA_SFX_BLAST;
  default:
    return MECHA_SFX_SLUG;
  }
}

/* The weapon a machine last actually fired, or NULL if it has not fired. */
static const tMechaWeaponDef *mecha_sound_last_weapon(const tMechaMech *pMech)
{
  const tMechaMechDef *pDef = mecha_def_get((int)pMech->byDefIdx);

  if (!pDef || pMech->iLastFiredSlot < 0
      || pMech->iLastFiredSlot >= MECHA_WEAPON_SLOTS
      || pMech->iLastFiredStance < 0
      || pMech->iLastFiredStance >= MECHA_STANCE_COUNT)
    return NULL;
  return &pDef->aWeapons[pMech->iLastFiredSlot][pMech->iLastFiredStance];
}

void mecha_sound_update(const tMechaWorld *pWorld, const tMechaCamera *pCamera,
                        int iViewMech)
{
  int i;

  if (!s_bActive || !pWorld || !pCamera || !soundon)
    return;

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    const tMechaMech *pMech = &pWorld->aMechs[i];
    tMechaSoundPlace place;
    bool bAirborne;

    mecha_sound_machine(pWorld, i, pCamera);
    if (!pMech->bActive)
      continue;

    mecha_sound_place(pCamera, pMech->fX,
                      mecha_mech_centre_height(pWorld, i), pMech->fZ,
                      pMech->fVelX, pMech->fVelY, pMech->fVelZ, &place);

    /* Two machines meeting. The ram cooldown going up is the sim saying one
     * of them just ran into something. */
    if (pMech->iRamCooldown > s_aiRamWas[i])
      mecha_sound_shot(MECHA_SFX_HIT, 0.9f, &place);
    s_aiRamWas[i] = pMech->iRamCooldown;

    /*
     * A shot leaving a barrel. The tick it happened on is what the sim
     * leaves behind, so a change in it is a new shot however many ticks
     * this frame ran -- and two slots fired on the same tick are one
     * noise, which is what a salvo sounds like anyway. [SND-04]
     */
    if (pMech->iFireTick != s_aiFireWas[i]) {
      const tMechaWeaponDef *pWeapon = mecha_sound_last_weapon(pMech);

      if (pWeapon && pMech->iFireTick >= 0) {
        float fRate;
        int iSample = mecha_sound_gun(pWeapon, &fRate);

        mecha_sound_pitched(iSample, MECHA_SND_GUN_LEVEL, fRate, &place);
      }
    }
    s_aiFireWas[i] = pMech->iFireTick;

    /* And the two warnings, which only the machine being flown gets: they
     * are its own cockpit, not something the arena can hear. [SND-05] */
    if (i == iViewMech) {
      if (pMech->iHitTakenTick != s_aiHitTakenWas[i]
          && pMech->iHitTakenTick >= 0)
        mecha_sound_warn(MECHA_SFX_WARN, MECHA_SND_WARN_LEVEL,
                         MECHA_SND_WARN_HURT);
      if (pMech->iDryFireTick != s_aiDryFireWas[i]
          && pMech->iDryFireTick >= 0)
        mecha_sound_warn(MECHA_SFX_DRY, MECHA_SND_DRY_LEVEL,
                         MECHA_SND_WARN_DRY);
    }
    s_aiHitTakenWas[i] = pMech->iHitTakenTick;
    s_aiDryFireWas[i] = pMech->iDryFireTick;

    /*
     * A machine coming apart. Once, on the frame it happens, and loudly:
     * this is the only thing in the arena that gets the big crash sample.
     * [SND-08]
     */
    {
      bool bWrecked = pMech->byMove == MECHA_MOVE_DESTROYED;

      if (bWrecked && !s_abWreckedWas[i])
        mecha_sound_shot(MECHA_SFX_WRECK, 1.0f, &place);
      s_abWreckedWas[i] = bWrecked;
    }

    /* Coming down. The fall speed is read a frame early because by the time
     * the wheels are on the ground it has already been spent. */
    bAirborne = mecha_mech_is_airborne(pWorld, i);
    if (s_abAirborneWas[i] && !bAirborne) {
      float fFall = -s_afFallWas[i];

      if (fFall > MECHA_SND_LAND_SOFT)
        mecha_sound_shot(MECHA_SFX_LAND,
                         (fFall - MECHA_SND_LAND_SOFT)
                           / (MECHA_SND_LAND_HARD - MECHA_SND_LAND_SOFT),
                         &place);
    }
    s_abAirborneWas[i] = bAirborne;
    s_afFallWas[i] = pMech->fVelY;
  }

  /*
   * Blasts. An effect slot going from empty to full is one that was born
   * this frame, which catches them however many ticks the frame ran.
   */
  for (i = 0; i < MECHA_MAX_EFFECTS; i++) {
    const tMechaEffect *pFx = &pWorld->aEffects[i];
    bool bWas = s_abEffectWas[i];

    s_abEffectWas[i] = pFx->bActive;
    if (!pFx->bActive || bWas || pFx->byKind != MECHA_FX_EXPLOSION)
      continue;

    {
      tMechaSoundPlace place;

      /*
       * Every one of these is a blast. Which one is a machine coming apart
       * is not a question the effect table can answer -- it was asked by
       * size, and no machine's death blast was ever big enough to count,
       * so the wreck sample only ever played for a large missile. The
       * machines themselves are asked instead, above. [SND-08]
       */
      if (pWorld->iTick - s_iBlastTick < MECHA_SND_BLAST_GAP)
        continue;
      mecha_sound_place(pCamera, pFx->fX, pFx->fY, pFx->fZ,
                        pFx->fVelX, pFx->fVelY, pFx->fVelZ, &place);
      mecha_sound_shot(MECHA_SFX_BLAST, 0.8f, &place);
      s_iBlastTick = pWorld->iTick;
    }
  }
}
