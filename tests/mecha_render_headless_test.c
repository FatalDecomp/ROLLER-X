/*
 * Headless proof that the arena mode rasterises: a real GameRenderer in
 * software mode with no device and no window, asserting that geometry,
 * effects and HUD reach pixels. Given an output directory it writes the
 * frames as indexed PNGs to be looked at. [TEST-01]
 */
#include "3d.h"
#include "game_render.h"
#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_mesh.h"
#include "mecha_render.h"
#include "mecha_sim.h"
#include "func2.h"
#include "png_writer.h"
#include "sound.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Not a colour this mode ever paints with; used to prove a full redraw. */
#define MECHA_TEST_SENTINEL 254

#define FRAME_W 640
#define FRAME_H 400
/* The rows of the frame the HUD never writes to. [TEST-16] */
#define SKY_BAND_TOP    72
#define SKY_BAND_BOTTOM 300

/* palette[] is the array setpal actually writes; pal_addr is not reliably
 * updated by it, which the GPU renderer documents in its own source. */
static bool mecha_test_palette_loaded(void)
{
    int i;

    for (i = 0; i < 256; i++) {
        if (palette[i].byR || palette[i].byG || palette[i].byB)
            return true;
    }
    return false;
}

static bool mecha_test_file_present(const char *szFile)
{
    FILE *pFile = fopen(szFile, "rb");

    if (!pFile)
        return false;
    fclose(pFile);
    return true;
}

static int check(int bCondition, int iLine)
{
    if (!bCondition)
        fprintf(stderr, "mecha render check failed at line %d\n", iLine);
    return bCondition ? 0 : iLine;
}

#define CHECK(condition) \
    do { \
        int iResult = check((condition), __LINE__); \
        if (iResult != 0) \
            return iResult; \
    } while (0)

static uint8 s_aFrame[FRAME_W * FRAME_H];
static tMechaQuad s_aQuads[MECHA_QUAD_CAPACITY];
/* Built again purely to be measured: the numbered overlay needs the body's
 * own quads, and the list the frame was drawn from has been sorted and has
 * everything else in the arena in it too. */
static tMechaQuad s_aBodyQuads[MECHA_QUAD_CAPACITY];

/* One number waiting to be painted onto a panel, and the box a painted one
 * has already taken. */
typedef struct { int iPoly; int iX; int iY; float fDist; } tPolyLabel;
typedef struct { int iX; int iY; int iW; } tPolyBox;
static tPolyLabel aLabels[MECHA_ZIZIN_BODY_QUADS];
static tPolyBox   aDrawn[MECHA_ZIZIN_BODY_QUADS];
static tMechaWorld s_World;
static tMechaCamera s_Camera;

/* How many pixels carry each palette index. */
static void histogram_of(const uint8 *pFrame, size_t uCount,
                         int aiCounts[256])
{
    memset(aiCounts, 0, sizeof(int) * 256);
    for (size_t i = 0; i < uCount; i++)
        aiCounts[pFrame[i]]++;
}

static void histogram(const uint8 *pFrame, int aiCounts[256])
{
    histogram_of(pFrame, (size_t)FRAME_W * FRAME_H, aiCounts);
}

/* True when any pixel on this row is something other than the background
 * the briefing fills with. */
static bool row_has_ink(const uint8 *pFrame, int iWidth, int iY)
{
    uint8 byGround = pFrame[0];
    int x;

    for (x = 0; x < iWidth; x++) {
        if (pFrame[iY * iWidth + x] != byGround)
            return true;
    }
    return false;
}

//-------------------------------------------------------------------------------------------------

/* The last row carrying anything, or -1 for an empty frame. */
static int last_ink_row(const uint8 *pFrame, int iWidth, int iHeight)
{
    int iLast = -1;
    int y;

    for (y = 0; y < iHeight; y++) {
        if (row_has_ink(pFrame, iWidth, y))
            iLast = y;
    }
    return iLast;
}

//-------------------------------------------------------------------------------------------------

/* True when one index covers the whole frame -- nothing rasterised. */
static int single_colour(const int aiCounts[256])
{
    int i;

    for (i = 0; i < 256; i++) {
        if (aiCounts[i] == FRAME_W * FRAME_H)
            return 1;
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

/* The index in the middle of a row, away from anything drawn at the edges. */
static uint8 row_colour(const uint8 *pFrame, int iY)
{
    return pFrame[iY * FRAME_W + FRAME_W / 2];
}

//-------------------------------------------------------------------------------------------------

static int distinct_colours(const int aiCounts[256])
{
    int iCount = 0;

    for (int i = 0; i < 256; i++) {
        if (aiCounts[i] > 0)
            iCount++;
    }
    return iCount;
}

/*
 * The palette the mode actually installs at runtime. Using the shipping
 * table rather than a stand-in is the point: an earlier version of this test
 * invented its own colours, which meant it happily passed while the real
 * mode presented a black screen because nothing had filled pal_addr.
 */
static void build_preview_palette(tColor *paPalette)
{
    /* The palette the frame was actually drawn through, when there is one,
     * or the preview lies about retail tiles. [TEST-01] */
    if (mecha_test_palette_loaded()) {
        memcpy(paPalette, palette, sizeof(tColor) * 256);
        return;
    }
    mecha_render_build_palette(paPalette);
}

static void dump_frame(const char *szOutDir, const char *szName)
{
    static tColor aPalette[256];
    char szPath[512];

    if (!szOutDir)
        return;
    build_preview_palette(aPalette);
    snprintf(szPath, sizeof(szPath), "%s/%s", szOutDir, szName);
    if (RollerWriteIndexedPng(szPath, s_aFrame, aPalette, FRAME_W, FRAME_H) == 0)
        printf("   wrote %s\n", szPath);
    else
        fprintf(stderr, "   FAILED to write %s\n", szPath);
}

/*
 * How far back a camera has to stand to have the whole machine in shot.
 * Framing off the roster's fHeight alone put a siege platform through the
 * edges of the picture and stood the camera inside the car, because how
 * tall a machine is says nothing about how wide it is or how far its legs
 * reach out to the side.
 */
static float mecha_test_portrait_range(tMechaWorld *pWorld, int iMechIdx)
{
    static tMechaQuad aShot[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
    float fSpan = 0.0f;
    int i;
    int v;

    mecha_quads_reset(&list, aShot, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, pWorld, iMechIdx, MECHA_DETAIL_FULL);
    for (i = 0; i < list.iCount; i++) {
        for (v = 0; v < 4; v++) {
            float fDx = aShot[i].afVert[v][0] - pMech->fX;
            float fDy = aShot[i].afVert[v][1] - pMech->fY;
            float fDz = aShot[i].afVert[v][2] - pMech->fZ;

            if (fDx < 0.0f) fDx = -fDx;
            if (fDz < 0.0f) fDz = -fDz;
            if (fDx > fSpan) fSpan = fDx;
            if (fDy > fSpan) fSpan = fDy;
            if (fDz > fSpan) fSpan = fDz;
        }
    }
    /* Half the machine's largest reach, so this is its half-span; the
     * multiplier is what leaves air round the edges of the picture. */
    return fSpan * 2.35f + MECHA_M(2.0f);
}

/* Whether two measurements are within iPercent of the larger. */
static bool near_within(int iA, int iB, int iPercent)
{
    int iBig = iA > iB ? iA : iB;
    int iGap = iA > iB ? iA - iB : iB - iA;

    return iBig > 0 && iGap * 100 <= iBig * iPercent;
}

/*
 * A machine posed to be looked at rather than to be fighting: standing,
 * still, square to the camera, and settled into a combat stance so its guns
 * are up. Everything the pilot would otherwise be doing -- walking away,
 * boosting, aiming off -- makes two machines impossible to compare.
 */
static void mecha_test_clear_debris(tMechaWorld *pWorld)
{
    int i;

    /* Three seconds of two pilots closing on each other leaves shots in the
     * air and blasts on the ground, and a portrait is not a fight. */
    for (i = 0; i < MECHA_MAX_PROJECTILES; i++)
        pWorld->aProjectiles[i].bActive = false;
    for (i = 0; i < MECHA_MAX_EFFECTS; i++)
        pWorld->aEffects[i].bActive = false;
}

static void mecha_test_pose_for_portrait(tMechaWorld *pWorld, int iMechIdx)
{
    tMechaMech *pMech = &pWorld->aMechs[iMechIdx];

    /*
     * Stood in the middle of the arena rather than wherever its pilot got
     * to. Left where it walked, a machine ends up behind a block as often
     * as not, and a silhouette measured through cover is not a silhouette.
     */
    pMech->fX = 0.0f;
    pMech->fZ = 0.0f;
    pMech->fY = mecha_arena_terrain_height(&pWorld->arena, 0.0f, 0.0f);
    /* And with the cover taken away. There is a block near the middle of
     * the first arena, and a machine measured from behind one is measured
     * as whatever of it sticks out. */
    pWorld->arena.iObstacleCount = 0;
    /*
     * And in its own colours. A computer pilot draws a paint scheme off the
     * match seed [SIM-22], so left alone the whole roster turns up in
     * whatever colour the draw handed out and every portrait is the same
     * green. Scheme zero is the works finish -- the machine's own palette,
     * which is the one worth photographing.
     */
    pMech->byScheme = 0;
    pMech->byMove = MECHA_MOVE_STAND;
    pMech->iFacing = 0;
    pMech->iLegYaw = 0;
    pMech->iAimPitch = 0;
    pMech->fCombat = 1.0f;
    pMech->fStepPhase = 0.0f;
    pMech->fVelX = 0.0f;
    pMech->fVelY = 0.0f;
    pMech->fVelZ = 0.0f;
    pMech->fLeanRoll = 0.0f;
}

/* Ticks past the READY announcement so the controls are live. */
static void run_to_fight(tMechaInput *paInputs)
{
    int i;

    for (i = 0; i < MECHA_TICK_HZ * 4
                && s_World.match.byPhase != MECHA_PHASE_FIGHT; i++)
        mecha_sim_tick(&s_World, paInputs, MECHA_MAX_MECHS);
}

//-------------------------------------------------------------------------------------------------

static void render_now(GameRenderer *pRenderer, int iViewMech)
{
    mecha_camera_update(&s_Camera, &s_World, iViewMech);
    mecha_render_frame(pRenderer, &s_World, &s_Camera, iViewMech,
                       s_aFrame, FRAME_W, FRAME_H,
                       s_aQuads, MECHA_QUAD_CAPACITY);
}

/*
 * How far apart two palette entries look, in an opponent-colour space:
 * brightness, red against green, and blue against the other two. A plain
 * channel-by-channel distance is no use here -- it calls a saturated blue
 * and a mid grey close, because every channel is near the middle, which is
 * exactly the mistake that would let a shot go invisible. [DEF-12]
 */
static int mecha_colour_gap(const tColor *pA, const tColor *pB)
{
    double dL = (0.30 * pA->byR + 0.59 * pA->byG + 0.11 * pA->byB)
              - (0.30 * pB->byR + 0.59 * pB->byG + 0.11 * pB->byB);
    double dRg = ((double)pA->byR - pA->byG) - ((double)pB->byR - pB->byG);
    double dB = (pA->byB - 0.5 * ((double)pA->byR + pA->byG))
              - (pB->byB - 0.5 * ((double)pB->byR + pB->byG));

    return (int)sqrt(dL * dL + dRg * dRg + dB * dB);
}

int main(int argc, char **argv)
{
    const char *szOutDir = argc > 1 ? argv[1] : NULL;
    GameRenderer *pRenderer;
    int aiCounts[256];
    int iPlayer;
    int iEnemy;
    int iSkyOnly;

    SDL_SetMainReady();

    /* Software mode needs neither a GPU device nor a window, which is the
     * whole reason this test can run on a headless runner. */
    pRenderer = game_render_create(NULL, NULL);
    CHECK(pRenderer != NULL);
    game_render_set_mode(pRenderer, GAME_RENDER_SOFTWARE);
    /* Picks up the retail HUD font when the data is next to the binary, and
     * quietly does nothing when it is not -- which is how this runs in CI,
     * and why the built-in font has to keep working. */
    mecha_render_init_assets(pRenderer);
    /* The retail palette when it is beside the binary, so the frames this
     * dumps are shaded the way the game shades them. */
    if (mecha_test_file_present("palette.pal")) {
        setpal("palette.pal");
        FindShades();
    }
    CHECK(game_render_get_mode(pRenderer) == GAME_RENDER_SOFTWARE);

    /* Same palette install the mode performs, so the frames this test
     * rasterises are shaded the way the real thing would be. */
    {
        static tColor aPalette[256];

        /* Only when the retail palette is not already loaded -- installing
         * the fallback over it is what made every textured surface come out
         * as noise, since those tiles are drawn in the retail indices. */
        if (!mecha_test_palette_loaded()) {
            mecha_render_build_palette(aPalette);
            memcpy(palette, aPalette, sizeof(palette));
            pal_addr = aPalette;
            FindShades();
        }
    }

    mecha_sim_init(&s_World, 0, 0x5EED1234u, 2);
    iPlayer = mecha_sim_add_mech(&s_World, 0, MECHA_CONTROL_HUMAN, 0);
    CHECK(iPlayer >= 0);
    iEnemy = mecha_sim_add_mech(&s_World, 1, MECHA_CONTROL_AI, 1);
    CHECK(iEnemy >= 0);
    mecha_sim_begin_match(&s_World);
    mecha_camera_reset(&s_Camera);

    /* --- an empty frame still has to draw the world ---------------------- */

    render_now(pRenderer, iPlayer);
    histogram(s_aFrame, aiCounts);
    dump_frame(szOutDir, "arena_ready.png");

    /* --- a rig sheet, for looking at the animation ----------------------
     * Four points of the step cycle, dumped rather than asserted: the
     * numbers that pin the rig live in the sim tests. */
    if (szOutDir) {
        static tMechaWorld worldSaved;
        tMechaInput aIdle[MECHA_MAX_MECHS];
        tMechaMech *pRig = &s_World.aMechs[iPlayer];
        tMechaMech *pEye = &s_World.aMechs[iEnemy];
        tMechaCamera savedCamera = s_Camera;
        int iStep;

        /* The whole world goes back afterwards, because posing a machine by
         * hand and running the clock on to clear the round announcement are
         * both things the assertions further down must not inherit. */
        worldSaved = s_World;
        memset(aIdle, 0, sizeof(aIdle));
        for (iStep = 0; iStep < MECHA_TICK_HZ * 5; iStep++)
            mecha_sim_tick(&s_World, aIdle, MECHA_MAX_MECHS);

        /* The chase camera is no use here -- it is parked behind a shoulder
         * and sees a back and two heels -- so the camera is placed by hand,
         * out in front of the machine and looking back at it, and the other
         * mech is sent to the far corner so it is not standing in the way. */
        pEye->fX = MECHA_M(80.0f);
        pEye->fZ = MECHA_M(80.0f);
        pEye->iFacing = 0;
        pEye->iLegYaw = 0;
        /* Whatever the five seconds of clock did to it, this is a machine
         * standing on the floor walking on the spot. */
        pRig->fX = 0.0f;
        pRig->fY = 0.0f;
        pRig->fZ = 0.0f;
        pRig->fVelX = 0.0f;
        pRig->fVelY = 0.0f;
        pRig->fVelZ = 0.0f;
        pRig->byMove = MECHA_MOVE_WALK;
        pRig->iStateTicks = 0;
        pRig->iStunTicks = 0;
        pRig->iInvulnTicks = 0;
        pRig->fLeanRoll = 0.0f;
        pRig->iFacing = MECHA_ANGLE_HALF;          /* facing the camera */
        pRig->iLegYaw = mecha_angle_wrap(pRig->iFacing + MECHA_DEG(40));
        pRig->byLock = MECHA_LOCK_HELD;
        pRig->iTargetIdx = iEnemy;

        /* Three quarters on, which shows the stride and the twist at once. */
        s_Camera.fX = -MECHA_M(19.0f);
        s_Camera.fY = MECHA_M(9.0f);
        s_Camera.fZ = -MECHA_M(19.0f);
        s_Camera.iYaw = MECHA_DEG(45);
        s_Camera.iPitch = -MECHA_DEG(6);
        s_Camera.bSettled = true;

        for (iStep = 0; iStep < 6; iStep++) {
            char szName[32];

            pRig->fStepPhase = (float)iStep / 6.0f;
            mecha_render_frame(pRenderer, &s_World, &s_Camera, iPlayer,
                               s_aFrame, FRAME_W, FRAME_H,
                               s_aQuads, MECHA_QUAD_CAPACITY);
            snprintf(szName, sizeof(szName), "arena_rig%d.png", iStep);
            dump_frame(szOutDir, szName);
        }
        /* One frame per gait, from the same camera: standing at ease,
         * standing with a lock to hold, walking, gliding, hanging, and
         * driving through the air. */
        {
            static const uint8 abyMove[6] = {
                MECHA_MOVE_STAND, MECHA_MOVE_STAND, MECHA_MOVE_WALK,
                MECHA_MOVE_DASH, MECHA_MOVE_JUMP, MECHA_MOVE_DASH
            };
            static const float afCombat[6] = {
                0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
            };
            int iGait;

            for (iGait = 0; iGait < 6; iGait++) {
                char szName[32];

                pRig->byMove = abyMove[iGait];
                pRig->fCombat = afCombat[iGait];
                pRig->fY = iGait >= 4 ? MECHA_M(9.0f) : 0.0f;
                pRig->fStepPhase = 0.12f;
                pRig->iLegYaw = mecha_angle_wrap(pRig->iFacing
                                                 + MECHA_DEG(40));
                mecha_render_frame(pRenderer, &s_World, &s_Camera, iPlayer,
                                   s_aFrame, FRAME_W, FRAME_H,
                                   s_aQuads, MECHA_QUAD_CAPACITY);
                snprintf(szName, sizeof(szName), "arena_gait%d.png", iGait);
                dump_frame(szOutDir, szName);
            }
            pRig->byMove = MECHA_MOVE_WALK;
            pRig->fCombat = 1.0f;
            pRig->fY = 0.0f;
        }

        /*
         * Four bolts in a row, one per recoloured copy of the plasma
         * frames. The tint is built out of the palette at run time, so the
         * only way to know it worked is to look at it.
         */
        {
            static const uint8 abyTracer[4] = { 218, 171, 192, 255 };
            int iShot;

            memset(s_World.aProjectiles, 0, sizeof(s_World.aProjectiles));
            for (iShot = 0; iShot < 4; iShot++) {
                tMechaProjectile *pShot = &s_World.aProjectiles[iShot];

                memset(pShot, 0, sizeof(*pShot));
                pShot->bActive = true;
                pShot->byKind = MECHA_PROJ_HOMING;   /* drawn as the sprite */
                pShot->byOwner = (uint8_t)iPlayer;
                pShot->byPalette = abyTracer[iShot];
                pShot->fX = MECHA_M(-9.0f) + MECHA_M(6.0f) * (float)iShot;
                pShot->fY = MECHA_M(7.0f);
                pShot->fZ = -MECHA_M(6.0f);
                pShot->fPrevX = pShot->fX;
                pShot->fPrevY = pShot->fY;
                pShot->fPrevZ = pShot->fZ;
                pShot->fRadius = MECHA_M(1.6f);
                pShot->iLife = MECHA_TICK_HZ;
                pShot->iTarget = -1;
            }
            mecha_render_frame(pRenderer, &s_World, &s_Camera, iPlayer,
                               s_aFrame, FRAME_W, FRAME_H,
                               s_aQuads, MECHA_QUAD_CAPACITY);
            dump_frame(szOutDir, "arena_tints.png");
            /*
             * Every recoloured bank is built out of the palette the moment
             * a shot first asks for one, so having drawn four of them, all
             * three tints must be up -- or the data was never there and
             * none of them are.
             */
            printf("   %d of 3 bolt tints built (%s)\n",
                   mecha_render_tints_active(),
                   mecha_render_sprites_active() ? "textured" : "flat");
            CHECK(mecha_render_tints_active() == 3
                  || !mecha_render_sprites_active());
            memset(s_World.aProjectiles, 0, sizeof(s_World.aProjectiles));
        }

        /* And a landing, from the same camera: the dust ring wants looking
         * at more than the walk cycle does, being the one effect that is
         * meant to be read from above. */
        {
            const tMechaMechDef *pDef =
                mecha_def_get((int)pRig->byDefIdx);

            pRig->fStepPhase = 0.0f;
            mecha_sim_spawn_effect(&s_World, MECHA_FX_DUST, pRig->fX,
                                   pRig->fY, pRig->fZ, pDef->fRadius * 2.4f,
                                   pDef->abyPalette[2], MECHA_SEC(0.45f));
            for (iStep = 0; iStep < 3; iStep++) {
                char szName[32];
                int iTick;

                for (iTick = 0; iTick < 6; iTick++)
                    mecha_sim_tick(&s_World, aIdle, MECHA_MAX_MECHS);
                mecha_render_frame(pRenderer, &s_World, &s_Camera, iPlayer,
                                   s_aFrame, FRAME_W, FRAME_H,
                                   s_aQuads, MECHA_QUAD_CAPACITY);
                snprintf(szName, sizeof(szName), "arena_dust%d.png", iStep);
                dump_frame(szOutDir, szName);
            }
        }

        s_World = worldSaved;
        s_Camera = savedCamera;
        render_now(pRenderer, iPlayer);
    }

    /* If one colour covered the frame, nothing rasterised. */
    iSkyOnly = single_colour(aiCounts);
    CHECK(!iSkyOnly);
    CHECK(distinct_colours(aiCounts) >= 6);

    /* The sky is a gradient, not a fill: row zero must differ from a row a
     * third of the way down, which at this pitch is still sky. */
    CHECK(row_colour(s_aFrame, 0) != row_colour(s_aFrame, FRAME_H / 3));

    /* The arena itself: both checkerboard tones and the walls. */
    CHECK(aiCounts[s_World.arena.byFloorPalette] > 0);
    CHECK(aiCounts[s_World.arena.byGridPalette] > 0);
    CHECK(aiCounts[s_World.arena.byWallPalette] > 0);

    /* The HUD: bar frames and text both reach pixels. These mirror
     * MECHA_HUD_FRAME and MECHA_HUD_TEXT, which are private to
     * mecha_render.c; the palette assertions below are what actually guard
     * against them drifting apart. */
    CHECK(aiCounts[115] > 0);
    CHECK(aiCounts[143] > 0);

    /* Checked forwards from the constants, never backwards from the frame.
     * [TEST-02] */
    CHECK(mecha_render_palette_defines(s_World.arena.byFloorPalette));
    CHECK(mecha_render_palette_defines(s_World.arena.byGridPalette));
    CHECK(mecha_render_palette_defines(s_World.arena.byWallPalette));
    for (int iDef = 0; iDef < mecha_def_count(); iDef++) {
        const tMechaMechDef *pDef = mecha_def_get(iDef);

        for (int iHue = 0; iHue < 4; iHue++)
            CHECK(mecha_render_palette_defines(pDef->abyPalette[iHue]));
        for (int iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
            for (int iStance = 0; iStance < MECHA_STANCE_COUNT; iStance++)
                CHECK(mecha_render_palette_defines(
                    pDef->aWeapons[iSlot][iStance].byPalette));
        }
    }

    /*
     * --- weapon fire has to clear the ground it flies over ---------------
     *
     * A green shot over a green field is invisible, which is what happened:
     * the tracer palette and the meadow's grass were two steps of the same
     * ramp. This walks every weapon the roster carries against every
     * arena's own three surfaces and insists on daylight between them, so
     * neither a retuned arena nor a new gun can quietly go dark. [DEF-12]
     */
    {
        tColor aPalette[256];
        int iArena;
        int iWorst = 1 << 20;
        int iWorstShot = -1;
        int iWorstGround = -1;

        mecha_render_build_palette(aPalette);

        for (iArena = 0; iArena < mecha_arena_count(); iArena++) {
            tMechaArena arena;
            uint8 abyGround[3];
            int iGround;
            int iDef;

            mecha_arena_init(&arena, iArena);
            abyGround[0] = arena.byFloorPalette;
            abyGround[1] = arena.byGridPalette;
            abyGround[2] = arena.byWallPalette;

            for (iDef = 0; iDef < mecha_def_count(); iDef++) {
                const tMechaMechDef *pDef = mecha_def_get(iDef);
                int iSlot;

                for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
                    int iStance;

                    for (iStance = 0; iStance < MECHA_STANCE_COUNT; iStance++) {
                        uint8 byShot =
                            pDef->aWeapons[iSlot][iStance].byPalette;

                        if (pDef->aWeapons[iSlot][iStance].fSpeed <= 0.0f)
                            continue;
                        /* A blade is held at arm's length, not spotted
                         * crossing the arena, and steel is the right colour
                         * for one. Everything that flies is in. [DEF-12] */
                        if (pDef->aWeapons[iSlot][iStance].byKind
                            == MECHA_PROJ_MELEE)
                            continue;
                        for (iGround = 0; iGround < 3; iGround++) {
                            const tColor *pA = &aPalette[byShot];
                            const tColor *pB = &aPalette[abyGround[iGround]];
                            int iDist = mecha_colour_gap(pA, pB);

                            if (iDist < iWorst) {
                                iWorst = iDist;
                                iWorstShot = byShot;
                                iWorstGround = abyGround[iGround];
                            }
                        }
                    }
                }
            }
        }

        printf("   closest shot to ground: index %d against %d, %d apart\n",
               iWorstShot, iWorstGround, iWorst);
        /*
         * Fifty. The set as painted bottoms out at 56, and the green shot
         * that started this scored 36 over the meadow it was fired across.
         */
        CHECK(iWorst >= 50);
    }

    /*
     * --- one shot of every colour, over the field ------------------------
     *
     * A reference frame rather than an assertion: the meadow is the arena
     * that started the complaint, and this is the whole weapon palette
     * flying across it at three ranges. [DEF-12]
     */
    {
        static const uint8 abyFire[] = { 171, 207, 195, 183, 219, 231 };
        static const float afAt[] = { 40.0f, 110.0f, 200.0f };
        int iShot = 0;
        int iBand;
        size_t iHue;

        tMechaInput aStage[MECHA_MAX_MECHS];

        mecha_sim_init(&s_World, 3, 0xF12Eu, 2);   /* COLDWATER MEADOW */
        iPlayer = mecha_sim_add_mech(&s_World, 0, MECHA_CONTROL_HUMAN, 0);
        CHECK(mecha_sim_add_mech(&s_World, 2, MECHA_CONTROL_AI, 1) >= 0);
        mecha_sim_begin_match(&s_World);
        mecha_camera_reset(&s_Camera);
        /* Past READY, or the banner is drawn across the middle of the
         * frame and over half the shots. */
        memset(aStage, 0, sizeof(aStage));
        run_to_fight(aStage);
        mecha_camera_update(&s_Camera, &s_World, iPlayer);

        for (iBand = 0; iBand < 3; iBand++) {
            for (iHue = 0; iHue < sizeof(abyFire) / sizeof(abyFire[0]);
                 iHue++) {
                tMechaProjectile *pShot = &s_World.aProjectiles[iShot++];
                /* Out along where the camera is actually looking, which
                 * is the machine's own heading. */
                float fFwdX = mecha_sin(s_World.aMechs[iPlayer].iFacing);
                float fFwdZ = mecha_cos(s_World.aMechs[iPlayer].iFacing);
                float fAhead = MECHA_M(afAt[iBand]);
                float fAcross = ((float)iHue - 2.5f) * MECHA_M(14.0f)
                                * (afAt[iBand] / 60.0f);

                memset(pShot, 0, sizeof(*pShot));
                pShot->bActive = true;
                pShot->byKind = MECHA_PROJ_BULLET;
                pShot->fRadius = MECHA_M(0.8f);
                pShot->byPalette = abyFire[iHue];
                pShot->fX = s_World.aMechs[iPlayer].fX
                            + fFwdX * fAhead + fFwdZ * fAcross;
                /* Above the machine's own head, so nothing in the middle
                 * of the frame stands in front of the middle of the row. */
                pShot->fY = MECHA_M(20.0f) + MECHA_M(afAt[iBand]) * 0.06f;
                pShot->fZ = s_World.aMechs[iPlayer].fZ
                            + fFwdZ * fAhead - fFwdX * fAcross;
                pShot->fPrevX = pShot->fX - fFwdX * MECHA_M(3.0f);
                pShot->fPrevY = pShot->fY;
                pShot->fPrevZ = pShot->fZ - fFwdZ * MECHA_M(3.0f);
            }
        }

        render_now(pRenderer, iPlayer);
        dump_frame(szOutDir, "arena_fire.png");

        /* Every one of them reached the screen. */
        {
            int aiFire[256];

            histogram(s_aFrame, aiFire);
            printf("   fire over the meadow, px per colour:");
            for (iHue = 0; iHue < sizeof(abyFire) / sizeof(abyFire[0]);
                 iHue++)
                printf(" %d:%d", abyFire[iHue], aiFire[abyFire[iHue]]);
            printf("\n");
            for (iHue = 0; iHue < sizeof(abyFire) / sizeof(abyFire[0]);
                 iHue++)
                CHECK(aiFire[abyFire[iHue]] > 0);
        }
    }

    /* --- the player's own machine is on screen --------------------------- */
    {
        const tMechaMechDef *pDef = mecha_def_get(0);
        int iHull = aiCounts[pDef->abyPalette[0]]
                  + aiCounts[pDef->abyPalette[1]]
                  + aiCounts[pDef->abyPalette[2]];

        CHECK(iHull > 0);
    }

    /* --- fight for a while, then look again ------------------------------ */
    {
        tMechaInput aInputs[MECHA_MAX_MECHS];
        int aiLater[256];
        int i;
        int iTracer = 0;

        for (i = 0; i < MECHA_TICK_HZ * 4; i++) {
            memset(aInputs, 0, sizeof(aInputs));
            aInputs[iPlayer].iMoveZ = 100;
            aInputs[iPlayer].bDash = (i % 90) < 30;
            aInputs[iPlayer].bFireLeft = (i % 20) < 2;
            aInputs[iPlayer].bFireCenter = (i % 47) < 2;
            aInputs[iPlayer].bFireRight = (i % 71) < 2;
            mecha_sim_tick(&s_World, aInputs, MECHA_MAX_MECHS);
        }

        render_now(pRenderer, iPlayer);
        histogram(s_aFrame, aiLater);
        dump_frame(szOutDir, "arena_fight.png");

        CHECK(!single_colour(aiLater));
        CHECK(distinct_colours(aiLater) >= 6);

        /* Shots in flight have to be visible, or the tracer geometry is
         * being built and then thrown away. */
        /* Every colour a weapon paints its shots in. Kept in step with the
         * PAL_TRACER_* set by the palette assertions further up, which walk
         * the roster rather than trusting this list. */
        iTracer = aiLater[171] + aiLater[207] + aiLater[195] + aiLater[183]
                + aiLater[148] + aiLater[219] + aiLater[231] + aiLater[143];
        CHECK(iTracer > 0);

        /* The camera followed the fight rather than staying put. */
        CHECK(memcmp(aiCounts, aiLater, sizeof(aiCounts)) != 0);
    }

    /*
     * --- HIT, over the clock ---------------------------------------------
     *
     * The word is drawn from the tick the sim recorded rather than from any
     * state of the HUD's own, so this can be posed: put a hit on the record
     * and the next frame has to carry it, and a frame far enough past it
     * has to not. [REND-14]
     */
    {
        int aiOff[256];
        int aiOn[256];
        int iRow;
        int iHitPixels = 0;
        int iBelow = 0;

        render_now(pRenderer, iPlayer);
        histogram(s_aFrame, aiOff);

        s_World.aMechs[iPlayer].iHitDealtTick = s_World.iTick;
        render_now(pRenderer, iPlayer);
        histogram(s_aFrame, aiOn);
        dump_frame(szOutDir, "arena_hit.png");

        /* Red, and more of it than the frame had a moment ago. */
        CHECK(aiOn[231] > aiOff[231]);

        /*
         * And in the right place: above the clock, which sits in the bottom
         * 24 scaled rows. Anything painted over the clock itself would be
         * the one thing the player still has to be able to read.
         */
        for (iRow = 0; iRow < FRAME_H; iRow++) {
            int iCol;

            for (iCol = 0; iCol < FRAME_W; iCol++) {
                if (s_aFrame[iRow * FRAME_W + iCol] != 231)
                    continue;
                if (iRow >= FRAME_H - 38 * (FRAME_W / 320)
                    && iRow < FRAME_H - 24 * (FRAME_W / 320))
                    iHitPixels++;
                else if (iRow >= FRAME_H - 24 * (FRAME_W / 320))
                    iBelow++;
            }
        }
        printf("   HIT drew %d px above the clock, %d below it\n",
               iHitPixels, iBelow);
        CHECK(iHitPixels > 0);

        /* Gone again once the flash has run out. */
        s_World.aMechs[iPlayer].iHitDealtTick =
            s_World.iTick - MECHA_SEC(2.0f);
        render_now(pRenderer, iPlayer);
        histogram(s_aFrame, aiOn);
        CHECK(aiOn[231] <= aiOff[231]);
    }

    /*
     * --- the death blast is a burst, not a wall --------------------------
     * An opaque billboard whose scale is a half-extent, so an over-large
     * figure slabs the screen. [TEST-03]
     */
    {
        /* A burning machine is painted in fire rather than in its own
         * livery [SIM-27], and the hot end of that ramp is an index no HUD
         * element paints in -- so the blast can still be counted across the
         * whole frame with nothing to subtract. */
        tMechaInput aInputs[MECHA_MAX_MECHS];
        uint8 byBlast = MECHA_PAL_BURN_HOT;
        int aiPeak[256];
        int iPeak = 0;
        int i;

        mecha_sim_init(&s_World, 0, 0xB1A57u, 2);
        iPlayer = mecha_sim_add_mech(&s_World, 2, MECHA_CONTROL_HUMAN, 0);
        CHECK(mecha_sim_add_mech(&s_World, 0, MECHA_CONTROL_AI, 1) >= 0);
        mecha_sim_begin_match(&s_World);
        mecha_camera_reset(&s_Camera);
        memset(aInputs, 0, sizeof(aInputs));
        run_to_fight(aInputs);

        /* Straight to zero armour, which is what spawns the death blast. */
        mecha_sim_damage(&s_World, iPlayer, 1, 100000.0f, 0.0f, 0.0f, 0.0f);

        /* Walk the blast's whole life and keep its widest frame. */
        for (i = 0; i < MECHA_TICK_HZ; i++) {
            render_now(pRenderer, iPlayer);
            histogram(s_aFrame, aiPeak);
            if (aiPeak[byBlast] > iPeak)
                iPeak = aiPeak[byBlast];
            /* Dumped on a fixed tick rather than on the peak: with the
             * texture bank loaded the peak count stays zero, so a dump tied
             * to it would never fire and the one frame worth looking at
             * would be the one never written. */
            if (i == MECHA_TICK_HZ / 6)
                dump_frame(szOutDir, "arena_blast.png");
            mecha_sim_tick(&s_World, aInputs, MECHA_MAX_MECHS);
        }

        /*
         * And it is still burning a couple of seconds later, which is the
         * whole difference between a kill and a wreck. [SIM-27]
         */
        {
            int aiLate[256];
            int iLate;

            for (i = 0; i < MECHA_TICK_HZ * 2; i++)
                mecha_sim_tick(&s_World, aInputs, MECHA_MAX_MECHS);
            render_now(pRenderer, iPlayer);
            dump_frame(szOutDir, "arena_wreck.png");
            histogram(s_aFrame, aiLate);
            iLate = aiLate[byBlast];
            printf("   still burning two seconds on: %d px\n", iLate);
            CHECK(s_World.aMechs[iPlayer].byMove == MECHA_MOVE_DESTROYED);
            CHECK(s_World.aMechs[iPlayer].iBurnTicks > 0);
        }

        /* Every colour the cooling debris walks through has to have one of
         * its own, or a dying machine sprays holes in the world. */
        {
            static const uint8 abyCool[] = {
                207, 204, 171, 170, 167, 230, 227, 224, 221
            };
            size_t iCool;

            for (iCool = 0; iCool < sizeof(abyCool) / sizeof(abyCool[0]);
                 iCool++)
                CHECK(mecha_render_palette_defines(abyCool[iCool]));
        }

        /* Which path drew it decides what there is to measure.
         * [TEST-03] */
        printf("   death blast peaks at %d px, %.1f%% of the frame (%s)\n",
               iPeak, 100.0 * iPeak / (double)(FRAME_W * FRAME_H),
               mecha_render_sprites_active() ? "textured" : "flat");
        if (mecha_render_sprites_active()) {
            int iForeign = 0;
            int iIdx;

            /* Zero here is the expected reading, not a missing explosion:
             * what proves the sprite drew is a pixel in an index the mode's
             * own palette does not define. [TEST-03] */
            histogram(s_aFrame, aiPeak);
            for (iIdx = 0; iIdx < 256; iIdx++) {
                if (aiPeak[iIdx] > 0 && !mecha_render_palette_defines(iIdx))
                    iForeign += aiPeak[iIdx];
            }
            printf("   %d px came out of the texture bank\n", iForeign);
            /* Bank pixels on screen is the whole assertion; the flat
             * colour is not required to vanish. [TEST-03] */
            CHECK(iForeign > 0);
        } else {
            CHECK(iPeak > 0);
            CHECK(iPeak < FRAME_W * FRAME_H / 16);   /* under 6.25% */
        }
    }

    /* --- a knocked-down mech still renders ------------------------------- */
    {
        tMechaInput aInputs[MECHA_MAX_MECHS];
        int i;

        mecha_sim_damage(&s_World, iPlayer, 1, 10.0f,
                         MECHA_STAGGER_DOWN * 2.0f, 0.0f, 0.0f);
        memset(aInputs, 0, sizeof(aInputs));
        for (i = 0; i < 6; i++)
            mecha_sim_tick(&s_World, aInputs, MECHA_MAX_MECHS);
        render_now(pRenderer, iPlayer);
        histogram(s_aFrame, aiCounts);
        dump_frame(szOutDir, "arena_down.png");
        CHECK(!single_colour(aiCounts));
    }

    /* --- every arena rasterises ------------------------------------------
     * The outdoor arenas are terrain rather than a flat floor and the roof
     * is nothing at all past its edge, so they are the ones most likely to
     * come out empty. */
    {
        int iArena;

        for (iArena = 0; iArena < mecha_arena_count(); iArena++) {
            char szName[64];
            int iPilot;
            int iFoe;

            mecha_sim_init(&s_World, iArena, 0x5EED1234u, 2);
            iPilot = mecha_sim_add_mech(&s_World, 0, MECHA_CONTROL_HUMAN, 0);
            CHECK(iPilot >= 0);
            iFoe = mecha_sim_add_mech(&s_World, 1, MECHA_CONTROL_AI, 1);
            CHECK(iFoe >= 0);
            mecha_sim_begin_match(&s_World);
            mecha_camera_reset(&s_Camera);
            render_now(pRenderer, iPilot);
            histogram(s_aFrame, aiCounts);
            snprintf(szName, sizeof(szName), "arena_stage%d.png", iArena);
            dump_frame(szOutDir, szName);
            printf("   %s: %d colours\n", mecha_arena_name(iArena),
                   distinct_colours(aiCounts));

            /*
             * And the same arena from outside and above it. The chase
             * camera stands a machine's height off the deck, which shows
             * the floor it is on and nothing of the shape the stage is --
             * and the shape is the whole point of some of them. The camera
             * is placed by hand here rather than driven, because there is
             * no machine standing where this wants to look from. [TEST-12]
             */
            s_Camera.fX = 0.0f;
            s_Camera.fY = s_World.arena.fHalfExtent * 0.75f;
            s_Camera.fZ = -s_World.arena.fHalfExtent * 1.9f;
            s_Camera.iYaw = 0;
            s_Camera.iPitch = -MECHA_DEG(22);
            s_Camera.bSettled = true;
            mecha_render_frame(pRenderer, &s_World, &s_Camera, iPilot,
                               s_aFrame, FRAME_W, FRAME_H, s_aQuads,
                               MECHA_QUAD_CAPACITY);
            snprintf(szName, sizeof(szName), "arena_survey%d.png", iArena);
            dump_frame(szOutDir, szName);
            histogram(s_aFrame, aiCounts);
            /* Something has to be there: an arena drawn from outside that
             * comes back one flat colour is one with no shape at all. */
            CHECK(!single_colour(aiCounts));
        }
    }

    /*
     * --- the sky over a stage that is not on a planet ---------------------
     *
     * Black, with stars in it and one world hanging among them, on a dome
     * standing on its side so the whole sky wheels past. Every part of it
     * is built rather than loaded, which is why it is here to be counted on
     * a checkout with no retail data at all. [MESH-49]
     */
    {
        int iSky = -1;
        int iArena;

        for (iArena = 0; iArena < mecha_arena_count(); iArena++) {
            tMechaArena probe;

            mecha_arena_init(&probe, iArena);
            if (probe.bySkyKind == MECHA_SKY_STARFIELD)
                iSky = iArena;
        }
        CHECK(iSky >= 0);

        mecha_sim_init(&s_World, iSky, 0x5EED1234u, 2);
        CHECK(mecha_sim_add_mech(&s_World, 0, MECHA_CONTROL_HUMAN, 0) >= 0);
        CHECK(mecha_sim_add_mech(&s_World, 1, MECHA_CONTROL_AI, 1) >= 0);
        mecha_sim_begin_match(&s_World);

        {
            static const int aiYaw[] = { 0, MECHA_ANGLE_QUARTER,
                                         MECHA_ANGLE_HALF,
                                         MECHA_ANGLE_HALF
                                           + MECHA_ANGLE_QUARTER };
            tMechaInput aIdle[MECHA_MAX_MECHS];
            int iStars = 0;
            int iWorld = 0;
            int i;
            size_t iTurn;

            memset(aIdle, 0, sizeof(aIdle));
            run_to_fight(aIdle);
            /* And past the FIGHT banner, which is drawn in the same white
             * the stars are. [TEST-16] */
            for (i = 0; i < MECHA_TICK_HZ * 2; i++)
                mecha_sim_tick(&s_World, aIdle, MECHA_MAX_MECHS);

            for (iTurn = 0; iTurn < sizeof(aiYaw) / sizeof(aiYaw[0]);
                 iTurn++) {
                char szName[64];
                int iStar;
                int iDisc;
                int iRow;
                int iCol;

                /* High over the middle of the stage and pitched up, which
                 * is the only place on a stage this size where the view is
                 * sky rather than deck. */
                s_Camera.fX = 0.0f;
                s_Camera.fY = MECHA_M(150.0f);
                s_Camera.fZ = 0.0f;
                s_Camera.iYaw = aiYaw[iTurn];
                s_Camera.iPitch = MECHA_DEG(26);
                s_Camera.bSettled = true;
                mecha_render_frame(pRenderer, &s_World, &s_Camera, 0,
                                   s_aFrame, FRAME_W, FRAME_H, s_aQuads,
                                   MECHA_QUAD_CAPACITY);
                snprintf(szName, sizeof(szName), "arena_sky%d.png",
                         (int)iTurn);
                dump_frame(szOutDir, szName);

                /*
                 * Counted over the middle band only. A star is white and so
                 * is the HUD font, so the rows the HUD owns would otherwise
                 * read as a sky full of stars -- which is exactly what the
                 * first version of this check was measuring. [TEST-16]
                 */
                iStar = 0;
                iDisc = 0;
                for (iRow = SKY_BAND_TOP; iRow < SKY_BAND_BOTTOM; iRow++) {
                    for (iCol = 0; iCol < FRAME_W; iCol++) {
                        uint8 byPixel = s_aFrame[iRow * FRAME_W + iCol];

                        if (byPixel == 143)
                            iStar++;
                        else if (byPixel == 148 || byPixel == 145)
                            iDisc++;
                    }
                }
                iStars += iStar;
                iWorld += iDisc;
                printf("   sky at yaw %d: %d star px, %d world px\n",
                       (int)iTurn, iStar, iDisc);
            }
            /* Stars all the way round, and the world somewhere in it. */
            CHECK(iStars > 0);
            CHECK(iWorld > 0);
        }
    }

    /*
     * --- inside a fort ----------------------------------------------------
     *
     * Two storeys, and the hole in the first one's ceiling that is the way
     * to the second. There is nothing to assert here that the simulation
     * does not already assert better -- this is the frame a person looks
     * at to see whether a room reads as a room. [TEST-12]
     */
    {
        int iFort = -1;
        int iArena;

        for (iArena = 0; iArena < mecha_arena_count(); iArena++) {
            tMechaArena probe;

            mecha_arena_init(&probe, iArena);
            if (probe.bySkyKind == MECHA_SKY_STARFIELD)
                iFort = iArena;
        }
        CHECK(iFort >= 0);

        mecha_sim_init(&s_World, iFort, 0x0F0Au, 2);
        CHECK(mecha_sim_add_mech(&s_World, 0, MECHA_CONTROL_HUMAN, 0) >= 0);
        CHECK(mecha_sim_add_mech(&s_World, 3, MECHA_CONTROL_AI, 1) >= 0);
        mecha_sim_begin_match(&s_World);
        {
            tMechaInput aIdle[MECHA_MAX_MECHS];

            memset(aIdle, 0, sizeof(aIdle));
            run_to_fight(aIdle);
        }

        /* A machine on the plinth under the opening, for scale. */
        s_World.aMechs[0].fX = -MECHA_M(520.0f);
        s_World.aMechs[0].fZ = 0.0f;
        s_World.aMechs[0].fY = MECHA_M(9.0f);
        s_World.aMechs[1].fX = -MECHA_M(560.0f);
        s_World.aMechs[1].fZ = MECHA_M(50.0f);
        s_World.aMechs[1].fY = 0.0f;

        /* From a corner of the ground floor, across the room: the plinth,
         * the opening over it and the underside of the floor above. */
        s_Camera.fX = -MECHA_M(566.0f);
        s_Camera.fY = MECHA_M(13.0f);
        s_Camera.fZ = -MECHA_M(62.0f);
        s_Camera.iYaw = MECHA_DEG(43);
        s_Camera.iPitch = MECHA_DEG(6);
        s_Camera.bSettled = true;
        mecha_render_frame(pRenderer, &s_World, &s_Camera, 0, s_aFrame,
                           FRAME_W, FRAME_H, s_aQuads, MECHA_QUAD_CAPACITY);
        dump_frame(szOutDir, "arena_fort_below.png");

        /* And from up on the floor above, looking back down over the
         * opening at the room underneath. */
        s_Camera.fX = -MECHA_M(548.0f);
        s_Camera.fY = MECHA_M(34.0f);
        s_Camera.fZ = MECHA_M(20.0f);
        s_Camera.iYaw = MECHA_DEG(120);
        s_Camera.iPitch = -MECHA_DEG(34);
        s_Camera.bSettled = true;
        mecha_render_frame(pRenderer, &s_World, &s_Camera, 0, s_aFrame,
                           FRAME_W, FRAME_H, s_aQuads, MECHA_QUAD_CAPACITY);
        dump_frame(szOutDir, "arena_fort_above.png");
    }

    /* --- the gun car -----------------------------------------------------
     *
     * It is the one machine on the roster with no skeleton at all, so it is
     * the one most likely to come out as nothing.
     */
    {
        int iCar = -1;
        int i;

        for (i = 0; i < mecha_def_count(); i++) {
            if (mecha_def_get(i)->bWheeled)
                iCar = i;
        }
        CHECK(iCar >= 0);

        mecha_sim_init(&s_World, 0, 0x5EED1234u, 2);
        CHECK(mecha_sim_add_mech(&s_World, iCar, MECHA_CONTROL_HUMAN, 0) >= 0);
        CHECK(mecha_sim_add_mech(&s_World, iCar, MECHA_CONTROL_AI, 1) >= 0);
        mecha_sim_begin_match(&s_World);
        s_World.aMechs[0].byLock = MECHA_LOCK_HELD;
        s_World.aMechs[0].iTargetIdx = 1;
        s_World.aMechs[0].iRecovery = 12;
        mecha_camera_reset(&s_Camera);
        /* A camera set for a fourteen-metre machine is inside a two-metre
         * one, so this shot places its own: back, up and looking down. */
        /* Placed by hand, three quarters on, so the body and the gun are
         * both in the shot. The chase camera has its own scaling and is
         * exercised by the sim tests; this one is here to be looked at. */
        s_World.aMechs[0].iFacing = MECHA_DEG(35);
        s_World.aMechs[1].fX = s_World.aMechs[0].fX + MECHA_M(60.0f);
        s_World.aMechs[1].fZ = s_World.aMechs[0].fZ + MECHA_M(40.0f);
        s_Camera.fX = s_World.aMechs[0].fX - MECHA_M(3.0f);
        s_Camera.fY = s_World.aMechs[0].fY + MECHA_M(3.4f);
        s_Camera.fZ = s_World.aMechs[0].fZ - MECHA_M(11.0f);
        s_Camera.iYaw = MECHA_DEG(14);
        s_Camera.iPitch = -MECHA_DEG(11);
        s_Camera.bSettled = true;
        mecha_render_frame(pRenderer, &s_World, &s_Camera, 0, s_aFrame,
                           FRAME_W, FRAME_H, s_aQuads, MECHA_QUAD_CAPACITY);
        histogram(s_aFrame, aiCounts);
        dump_frame(szOutDir, "arena_guncar.png");

        /*
         * The same car walked round, so its painted panels can be looked at
         * rather than guessed about. Parked nose along +Z; yaw zero looks
         * along +Z, so a camera out at fPhi looks back the other way.
         * [TEST-04]
         */
        {
            static const struct { const char *szName; int iPhi; } aOrbit[] = {
                /* Named for what the camera looks at, which is the far side
                 * of the car from where it stands. [TEST-04] */
                { "arena_car_front.png",           0 },
                { "arena_car_front_xpos.png",     45 },
                { "arena_car_side_xpos.png",      90 },
                { "arena_car_rear_xpos.png",     135 },
                { "arena_car_rear.png",          180 },
                { "arena_car_rear_xneg.png",     225 },
                { "arena_car_side_xneg.png",     270 },
                { "arena_car_front_xneg.png",    315 },
            };
            const float fRadius = MECHA_M(9.0f);
            const float fHeight = MECHA_M(2.6f);
            int iShot;

            s_World.aMechs[0].iFacing = 0;
            s_World.aMechs[0].iAimPitch = 0;
            /* Park the foe straight off the nose so the gun lies along
             * the car rather than across the glass. */
            s_World.aMechs[1].fX = s_World.aMechs[0].fX;
            s_World.aMechs[1].fZ = s_World.aMechs[0].fZ + MECHA_M(90.0f);

            for (iShot = 0; iShot < (int)(sizeof(aOrbit) / sizeof(aOrbit[0]));
                 iShot++) {
                float fPhi = (float)aOrbit[iShot].iPhi * 3.14159265f / 180.0f;

                s_Camera.fX = s_World.aMechs[0].fX + fRadius * sinf(fPhi);
                s_Camera.fY = s_World.aMechs[0].fY + fHeight;
                s_Camera.fZ = s_World.aMechs[0].fZ + fRadius * cosf(fPhi);
                s_Camera.iYaw = MECHA_DEG(aOrbit[iShot].iPhi + 180);
                s_Camera.iPitch = -MECHA_DEG(9);
                s_Camera.bSettled = true;
                mecha_render_frame(pRenderer, &s_World, &s_Camera, 0, s_aFrame,
                                   FRAME_W, FRAME_H, s_aQuads,
                                   MECHA_QUAD_CAPACITY);
                dump_frame(szOutDir, aOrbit[iShot].szName);
            }
            printf("   ZIZIN KLR 330: walked round in %d shots\n",
                   (int)(sizeof(aOrbit) / sizeof(aOrbit[0])));

            /*
             * The same eight shots with every panel wearing its own index,
             * so a panel can be pointed at rather than argued about. Facing
             * is read off the sign of the projected screen area. [TEST-04]
             */
            for (iShot = 0; iShot < (int)(sizeof(aOrbit) / sizeof(aOrbit[0]));
                 iShot++) {
                float fPhi = (float)aOrbit[iShot].iPhi * 3.14159265f / 180.0f;
                const float fNear = MECHA_M(6.2f);
                tMechaQuadList body;
                char szLabelled[64];
                int iPoly;
                int iLabels;
                int iDrawn;

                /* The gun is held out to the car's right and is big
                 * enough to hide a whole flank, so on each shot the foe
                 * goes to the far side of the car from the camera and the
                 * gun swings away with it. */
                s_World.aMechs[1].fX = s_World.aMechs[0].fX
                                       - MECHA_M(90.0f) * sinf(fPhi);
                s_World.aMechs[1].fZ = s_World.aMechs[0].fZ
                                       - MECHA_M(90.0f) * cosf(fPhi);

                s_Camera.fX = s_World.aMechs[0].fX + fNear * sinf(fPhi);
                s_Camera.fY = s_World.aMechs[0].fY + MECHA_M(2.0f);
                s_Camera.fZ = s_World.aMechs[0].fZ + fNear * cosf(fPhi);
                s_Camera.iYaw = MECHA_DEG(aOrbit[iShot].iPhi + 180);
                s_Camera.iPitch = -MECHA_DEG(6);
                s_Camera.bSettled = true;
                mecha_render_frame(pRenderer, &s_World, &s_Camera, 0, s_aFrame,
                                   FRAME_W, FRAME_H, s_aQuads,
                                   MECHA_QUAD_CAPACITY);

                memset(&body, 0, sizeof(body));
                body.paQuads   = s_aBodyQuads;
                body.iCapacity = MECHA_QUAD_CAPACITY;
                mecha_mesh_mech(&body, &s_World, 0, MECHA_DETAIL_FULL);

                /* Nearest panel wins the space, or the roof and the tail
                 * write over the windscreen. [TEST-04] */
                iLabels = 0;
                for (iPoly = 0; iPoly < MECHA_ZIZIN_BODY_QUADS
                                && iPoly < body.iCount; iPoly++) {
                    const tMechaQuad *pQ = &s_aBodyQuads[iPoly];
                    int aiX[4];
                    int aiY[4];
                    float fCx = 0.0f, fCy = 0.0f, fCz = 0.0f;
                    float fArea = 0.0f;
                    float fDx, fDy, fDz;
                    int c;
                    bool bOn = true;

                    for (c = 0; c < 4; c++) {
                        if (!mecha_render_project(&s_Camera, FRAME_W, FRAME_H,
                                                  pQ->afVert[c][0],
                                                  pQ->afVert[c][1],
                                                  pQ->afVert[c][2],
                                                  &aiX[c], &aiY[c]))
                            bOn = false;
                        fCx += pQ->afVert[c][0];
                        fCy += pQ->afVert[c][1];
                        fCz += pQ->afVert[c][2];
                    }
                    if (!bOn)
                        continue;
                    for (c = 0; c < 4; c++) {
                        int d = (c + 1) & 3;
                        fArea += (float)aiX[c] * (float)aiY[d]
                                 - (float)aiX[d] * (float)aiY[c];
                    }
                    if (fArea >= 0.0f)
                        continue;   /* turned away from the camera */

                    fCx *= 0.25f; fCy *= 0.25f; fCz *= 0.25f;
                    if (!mecha_render_project(&s_Camera, FRAME_W, FRAME_H,
                                              fCx, fCy, fCz,
                                              &aiX[0], &aiY[0]))
                        continue;
                    fDx = fCx - s_Camera.fX;
                    fDy = fCy - s_Camera.fY;
                    fDz = fCz - s_Camera.fZ;
                    aLabels[iLabels].iPoly = iPoly;
                    aLabels[iLabels].iX    = aiX[0];
                    aLabels[iLabels].iY    = aiY[0];
                    aLabels[iLabels].fDist = fDx * fDx + fDy * fDy
                                             + fDz * fDz;
                    iLabels++;
                }

                /* Nearest first, so the near panel claims the space and the
                 * far one is the one dropped. */
                for (iPoly = 1; iPoly < iLabels; iPoly++) {
                    tPolyLabel keep = aLabels[iPoly];
                    int iSlot = iPoly - 1;
                    while (iSlot >= 0 && aLabels[iSlot].fDist > keep.fDist) {
                        aLabels[iSlot + 1] = aLabels[iSlot];
                        iSlot--;
                    }
                    aLabels[iSlot + 1] = keep;
                }

                iDrawn = 0;
                for (iPoly = 0; iPoly < iLabels; iPoly++) {
                    char szNum[8];
                    int iW;
                    int iPrev;
                    bool bClash = false;

                    snprintf(szNum, sizeof(szNum), "%d",
                             aLabels[iPoly].iPoly);
                    iW = mecha_render_text_width(2, szNum) + 4;
                    for (iPrev = 0; iPrev < iDrawn; iPrev++) {
                        if (aLabels[iPoly].iX - 2 < aDrawn[iPrev].iX + aDrawn[iPrev].iW
                            && aDrawn[iPrev].iX < aLabels[iPoly].iX - 2 + iW
                            && aLabels[iPoly].iY - 2 < aDrawn[iPrev].iY + 18
                            && aDrawn[iPrev].iY < aLabels[iPoly].iY - 2 + 18) {
                            bClash = true;
                            break;
                        }
                    }
                    if (bClash)
                        continue;
                    mecha_render_fill(s_aFrame, FRAME_W, FRAME_H,
                                      aLabels[iPoly].iX - 2,
                                      aLabels[iPoly].iY - 2, iW, 18, 0);
                    mecha_render_text(s_aFrame, FRAME_W, FRAME_H,
                                      aLabels[iPoly].iX, aLabels[iPoly].iY,
                                      2, 255, szNum);
                    aDrawn[iDrawn].iX = aLabels[iPoly].iX - 2;
                    aDrawn[iDrawn].iY = aLabels[iPoly].iY - 2;
                    aDrawn[iDrawn].iW = iW;
                    iDrawn++;
                }

                snprintf(szLabelled, sizeof(szLabelled), "poly_%s",
                         aOrbit[iShot].szName + strlen("arena_car_"));
                dump_frame(szOutDir, szLabelled);
            }
            printf("   ZIZIN KLR 330: eight numbered shots\n");
        }
        printf("   %s: %d colours, wearing %s\n",
               mecha_def_get(iCar)->szName, distinct_colours(aiCounts),
               mecha_render_car_skin_active() ? "its own skin"
                                              : "flat paint");
        /*
         * With the retail data beside the binary it has to be wearing the
         * real thing. Nothing here can assert that in CI, where there is no
         * data to load -- but the moment there is, a silent fallback to
         * flat paint is the failure this catches.
         */
        if (mecha_test_file_present("xzizin.bm"))
            CHECK(mecha_render_car_skin_active());
        CHECK(!single_colour(aiCounts));
    }

    /* --- the roster, machine by machine ----------------------------------
     *
     * Four views of every machine on a bare arena, dumped rather than
     * asserted, because what these are for is being looked at: a silhouette
     * is not something a number can sign off. The line-up below is the half
     * that can be checked, and it is checked. [TEST-09]
     */
    if (szOutDir) {
        static const int aiPhi[4] = { 0, 45, 90, 180 };
        int iDef;

        for (iDef = 0; iDef < mecha_def_count(); iDef++) {
            const tMechaMechDef *pDef = mecha_def_get(iDef);
            tMechaInput aIdle[MECHA_MAX_MECHS];
            float fRange;
            float fEye;
            int iShot;
            int iSelf;
            int iFoe;

            /* A fresh world each time: the machine under the camera is the
             * one being looked at, and the arena is the plainest there is. */
            mecha_sim_init(&s_World, 0, 0x5EED1234u, 1);
            iSelf = mecha_sim_add_mech(&s_World, iDef, MECHA_CONTROL_AI, 0);
            iFoe = mecha_sim_add_mech(&s_World, iDef, MECHA_CONTROL_AI, 1);
            CHECK(iSelf >= 0 && iFoe >= 0);
            /* Nobody shoots: a melee hitbox swung across the lens is a
             * bright square over the machine being photographed. */
            mecha_sim_set_ai_hold_fire(&s_World, true);
            mecha_sim_begin_match(&s_World);
            memset(aIdle, 0, sizeof(aIdle));
            /* Long enough to be settled into a fighting stance with its
             * guns up, which is the pose worth looking at. */
            for (iShot = 0; iShot < MECHA_TICK_HZ * 3; iShot++)
                mecha_sim_tick(&s_World, aIdle, MECHA_MAX_MECHS);

            /*
             * Standing, and standing still. Left to itself the pilot walks
             * off to fight, and a machine photographed mid-boost is mostly
             * a thruster plume: the plume is self-lit, so the depth key
             * pulls it forward and it is drawn over the machine throwing
             * it. [MESH-24]
             */
            mecha_test_pose_for_portrait(&s_World, iSelf);
            mecha_test_clear_debris(&s_World);
            /* The other one straight off the nose and well away, so the
             * arms come up along the machine rather than across it. */
            s_World.aMechs[iFoe].fX = s_World.aMechs[iSelf].fX;
            s_World.aMechs[iFoe].fZ = s_World.aMechs[iSelf].fZ
                                      + MECHA_M(120.0f);
            s_World.aMechs[iFoe].bActive = false;

            /* Framed off the machine's own size, or the tall ones walk out
             * of the top of the picture and the car is a dot. */
            fRange = mecha_test_portrait_range(&s_World, iSelf);
            fEye = pDef->fHeight * 0.46f;

            for (iShot = 0; iShot < 4; iShot++) {
                float fPhi = (float)aiPhi[iShot] * 3.14159265f / 180.0f;
                char szName[96];

                s_Camera.fX = s_World.aMechs[iSelf].fX + fRange * sinf(fPhi);
                s_Camera.fY = s_World.aMechs[iSelf].fY + fEye;
                s_Camera.fZ = s_World.aMechs[iSelf].fZ + fRange * cosf(fPhi);
                s_Camera.iYaw = MECHA_DEG(aiPhi[iShot] + 180);
                s_Camera.iPitch = -MECHA_DEG(7);
                s_Camera.bSettled = true;
                /* No view machine, so no HUD and no round banner painted
                 * over the thing being looked at. */
                mecha_render_frame(pRenderer, &s_World, &s_Camera, -1,
                                   s_aFrame, FRAME_W, FRAME_H, s_aQuads,
                                   MECHA_QUAD_CAPACITY);
                snprintf(szName, sizeof(szName), "roster%d_%03d.png",
                         iDef, aiPhi[iShot]);
                dump_frame(szOutDir, szName);
            }

            /* And the pose it holds when it has won a round, which is the
             * one frame per machine that is not the same shape as every
             * other machine's. [MESH-42] */
            {
                char szName[96];
                float fPhi = 0.61f;

                s_World.match.byPhase = MECHA_PHASE_ROUND_OVER;
                s_World.match.iWinnerIdx = iSelf;
                s_World.match.iPhaseTicks = MECHA_POSE_EASE_TICKS;
                fRange = mecha_test_portrait_range(&s_World, iSelf);
                s_Camera.fX = s_World.aMechs[iSelf].fX
                              + fRange * sinf(fPhi);
                s_Camera.fY = s_World.aMechs[iSelf].fY + fEye;
                s_Camera.fZ = s_World.aMechs[iSelf].fZ
                              + fRange * cosf(fPhi);
                s_Camera.iYaw = MECHA_DEG(35 + 180);
                s_Camera.iPitch = -MECHA_DEG(7);
                mecha_render_frame(pRenderer, &s_World, &s_Camera, -1,
                                   s_aFrame, FRAME_W, FRAME_H, s_aQuads,
                                   MECHA_QUAD_CAPACITY);
                snprintf(szName, sizeof(szName), "roster%d_win.png", iDef);
                dump_frame(szOutDir, szName);
            }
            printf("   %-16s %s\n", pDef->szName, pDef->szClass);
        }
    }

    /* --- and all of them together, at the range they are told apart at ---
     *
     * The whole point of a silhouette is that it survives being small. Each
     * machine is drawn side on at the range the far tier starts, and then
     * drawn again with it taken away, and what differs between the two
     * frames is the machine and nothing else -- which is the only way to
     * measure one against an arena it is standing in. [TEST-09]
     */
    {
        static uint8 aEmpty[FRAME_W * FRAME_H];
        int aiWide[MECHA_MAX_MECHS];
        int aiTall[MECHA_MAX_MECHS];
        int aiInk[MECHA_MAX_MECHS];
        int iCount = mecha_def_count();
        int iDef;
        int iSame = 0;

        if (iCount > MECHA_MAX_MECHS)
            iCount = MECHA_MAX_MECHS;
        for (iDef = 0; iDef < iCount; iDef++) {
            tMechaInput aIdle[MECHA_MAX_MECHS];
            int iMinX = FRAME_W;
            int iMaxX = -1;
            int iMinY = FRAME_H;
            int iMaxY = -1;
            int iSelf;
            int i;

            mecha_sim_init(&s_World, 0, 0x5EED1234u, 1);
            iSelf = mecha_sim_add_mech(&s_World, iDef, MECHA_CONTROL_AI, 0);
            CHECK(mecha_sim_add_mech(&s_World, iDef, MECHA_CONTROL_AI, 1) >= 0);
            mecha_sim_set_ai_hold_fire(&s_World, true);
            mecha_sim_begin_match(&s_World);
            memset(aIdle, 0, sizeof(aIdle));
            for (i = 0; i < MECHA_TICK_HZ * 3; i++)
                mecha_sim_tick(&s_World, aIdle, MECHA_MAX_MECHS);
            mecha_test_pose_for_portrait(&s_World, iSelf);
            mecha_test_clear_debris(&s_World);
            s_World.aMechs[1].fX = s_World.aMechs[iSelf].fX;
            s_World.aMechs[1].fZ = s_World.aMechs[iSelf].fZ + MECHA_M(120.0f);
            s_World.aMechs[1].bActive = false;

            /* Side on, at the range the far tier starts, which is the
             * hardest case an outline has to survive. */
            s_Camera.fX = s_World.aMechs[iSelf].fX + MECHA_M(130.0f);
            s_Camera.fY = s_World.aMechs[iSelf].fY
                          + mecha_def_get(iDef)->fHeight * 0.5f;
            s_Camera.fZ = s_World.aMechs[iSelf].fZ;
            s_Camera.iYaw = MECHA_DEG(270);
            s_Camera.iPitch = 0;
            s_Camera.bSettled = true;
            mecha_render_frame(pRenderer, &s_World, &s_Camera, -1,
                               s_aFrame, FRAME_W, FRAME_H, s_aQuads,
                               MECHA_QUAD_CAPACITY);
            if (szOutDir) {
                char szName[96];

                snprintf(szName, sizeof(szName), "roster%d_far.png", iDef);
                dump_frame(szOutDir, szName);
            }
            memcpy(aEmpty, s_aFrame, sizeof(aEmpty));

            /* The same frame with the machine taken out from under the
             * camera, so what is left over is the arena behind it. */
            s_World.aMechs[iSelf].bActive = false;
            mecha_render_frame(pRenderer, &s_World, &s_Camera, -1,
                               s_aFrame, FRAME_W, FRAME_H, s_aQuads,
                               MECHA_QUAD_CAPACITY);
            s_World.aMechs[iSelf].bActive = true;

            aiInk[iDef] = 0;
            for (i = 0; i < FRAME_W * FRAME_H; i++) {
                if (aEmpty[i] == s_aFrame[i])
                    continue;
                aiInk[iDef]++;
                if (i % FRAME_W < iMinX) iMinX = i % FRAME_W;
                if (i % FRAME_W > iMaxX) iMaxX = i % FRAME_W;
                if (i / FRAME_W < iMinY) iMinY = i / FRAME_W;
                if (i / FRAME_W > iMaxY) iMaxY = i / FRAME_W;
            }
            CHECK(aiInk[iDef] > 0);
            aiWide[iDef] = iMaxX - iMinX + 1;
            aiTall[iDef] = iMaxY - iMinY + 1;
            printf("   %-16s %5d px, %3d x %3d, %d%% filled\n",
                   mecha_def_get(iDef)->szName, aiInk[iDef],
                   aiWide[iDef], aiTall[iDef],
                   100 * aiInk[iDef] / (aiWide[iDef] * aiTall[iDef]));
        }

        /*
         * No two machines may have the same outline, and an outline here is
         * three numbers: how wide it is, how tall, and how much of its own
         * box it fills. Two of the three matching is a coincidence; all
         * three matching is two machines a player cannot tell apart at the
         * range this was measured at. It is a weak test of a strong claim,
         * and it catches the failure that actually happens, which is a new
         * machine shipping as an old one with the numbers changed.
         */
        for (iDef = 1; iDef < iCount; iDef++) {
            int iOther;

            for (iOther = 0; iOther < iDef; iOther++) {
                int iFillA = 100 * aiInk[iDef] / (aiWide[iDef] * aiTall[iDef]);
                int iFillB = 100 * aiInk[iOther]
                             / (aiWide[iOther] * aiTall[iOther]);

                if (near_within(aiWide[iDef], aiWide[iOther], 10)
                    && near_within(aiTall[iDef], aiTall[iOther], 10)
                    && near_within(iFillA, iFillB, 10)) {
                    printf("   %s and %s have the same outline\n",
                           mecha_def_get(iDef)->szName,
                           mecha_def_get(iOther)->szName);
                    iSame++;
                }
            }
        }
        CHECK(iSame == 0);
    }

    /* --- the slender frame, moving --------------------------------------
     *
     * A walk is the one thing about a machine no still frame can show, so
     * this walks the step cycle round in eight and dumps each one, then
     * pulls a trigger and dumps the rock-back. Written for the slender
     * profile because that is the frame whose gait is its own; any machine
     * would go through here just as well. [TEST-10]
     */
    if (szOutDir) {
        int iSlim = -1;
        int iDef;

        for (iDef = 0; iDef < mecha_def_count(); iDef++) {
            if (mecha_def_get(iDef)->byProfile == MECHA_PROFILE_SLENDER) {
                iSlim = iDef;
                break;
            }
        }
        if (iSlim >= 0) {
            const tMechaMechDef *pDef = mecha_def_get(iSlim);
            tMechaInput aIdle[MECHA_MAX_MECHS];
            float fRange;
            int iSelf;
            int iStep;
            int i;

            mecha_sim_init(&s_World, 0, 0x5EED1234u, 1);
            iSelf = mecha_sim_add_mech(&s_World, iSlim, MECHA_CONTROL_AI, 0);
            CHECK(mecha_sim_add_mech(&s_World, iSlim, MECHA_CONTROL_AI, 1) >= 0);
            mecha_sim_set_ai_hold_fire(&s_World, true);
            mecha_sim_begin_match(&s_World);
            memset(aIdle, 0, sizeof(aIdle));
            for (i = 0; i < MECHA_TICK_HZ * 3; i++)
                mecha_sim_tick(&s_World, aIdle, MECHA_MAX_MECHS);
            mecha_test_pose_for_portrait(&s_World, iSelf);
            mecha_test_clear_debris(&s_World);
            s_World.aMechs[1].bActive = false;

            fRange = mecha_test_portrait_range(&s_World, iSelf);

            /* Three quarters on, the way the machine is worth looking at,
             * and near enough that a hip moving is a hip you can see. */
            s_Camera.fX = s_World.aMechs[iSelf].fX
                          + fRange * 0.80f * sinf(0.61f);
            s_Camera.fY = s_World.aMechs[iSelf].fY + pDef->fHeight * 0.46f;
            s_Camera.fZ = s_World.aMechs[iSelf].fZ
                          + fRange * 0.80f * cosf(0.61f);
            s_Camera.iYaw = MECHA_DEG(35 + 180);
            s_Camera.iPitch = -MECHA_DEG(5);
            s_Camera.bSettled = true;

            /*
             * The walk. fStepPhase is what the cycle is paced by [MESH-04],
             * so stepping it by hand is the cycle, with no clock to wait
             * for and nothing else in the machine's state changing.
             */
            for (iStep = 0; iStep < 8; iStep++) {
                char szName[96];

                s_World.aMechs[iSelf].byMove = MECHA_MOVE_WALK;
                s_World.aMechs[iSelf].fStepPhase = (float)iStep / 8.0f;
                mecha_render_frame(pRenderer, &s_World, &s_Camera, -1,
                                   s_aFrame, FRAME_W, FRAME_H, s_aQuads,
                                   MECHA_QUAD_CAPACITY);
                snprintf(szName, sizeof(szName), "slim_walk%d.png", iStep);
                dump_frame(szOutDir, szName);
            }

            /*
             * And the recoil. iRecovery counts down after a trigger pull,
             * and on this frame it rocks the whole machine back from the
             * waist rather than only kicking the arm that fired. [MESH-40]
             */
            for (iStep = 0; iStep < 4; iStep++) {
                char szName[96];

                s_World.aMechs[iSelf].byMove = MECHA_MOVE_STAND;
                s_World.aMechs[iSelf].fStepPhase = 0.0f;
                s_World.aMechs[iSelf].iLastFiredSlot = MECHA_SLOT_RIGHT;
                s_World.aMechs[iSelf].iRecovery = 8 - iStep * 2;
                mecha_render_frame(pRenderer, &s_World, &s_Camera, -1,
                                   s_aFrame, FRAME_W, FRAME_H, s_aQuads,
                                   MECHA_QUAD_CAPACITY);
                snprintf(szName, sizeof(szName), "slim_fire%d.png", iStep);
                dump_frame(szOutDir, szName);
            }
            s_World.aMechs[iSelf].iRecovery = 0;

            /*
             * And the nearest this rig gets to a hand-up pose: both arms
             * elevated to their stop, the weight on one leg with the other
             * crossed in front, and the head turned onto the camera. With
             * no lock the aim pitch is taken straight off the machine, so
             * setting it is how a pose is asked for. There is no V-sign in
             * here to find: a hand is a gun mount with no fingers on it,
             * and both arms read one aim between them.
             */
            s_World.aMechs[iSelf].byMove = MECHA_MOVE_STAND;
            s_World.aMechs[iSelf].fStepPhase = 0.0f;
            s_World.aMechs[iSelf].fCombat = 1.0f;
            s_World.match.byPhase = MECHA_PHASE_ROUND_OVER;
            s_World.match.iWinnerIdx = iSelf;
            /* A raised arm reaches higher than anything the machine
             * measures standing, so the shot is framed again with the pose
             * held rather than with the pose it was framed on. */
            s_World.match.iPhaseTicks = MECHA_POSE_EASE_TICKS;
            fRange = mecha_test_portrait_range(&s_World, iSelf);
            s_Camera.fX = s_World.aMechs[iSelf].fX
                          + fRange * 0.80f * sinf(0.61f);
            s_Camera.fZ = s_World.aMechs[iSelf].fZ
                          + fRange * 0.80f * cosf(0.61f);
            for (iStep = 0; iStep < 4; iStep++) {
                char szName[96];

                s_World.match.iPhaseTicks =
                    MECHA_POSE_EASE_TICKS * iStep / 3;
                mecha_render_frame(pRenderer, &s_World, &s_Camera, -1,
                                   s_aFrame, FRAME_W, FRAME_H, s_aQuads,
                                   MECHA_QUAD_CAPACITY);
                snprintf(szName, sizeof(szName), "slim_win%d.png", iStep);
                dump_frame(szOutDir, szName);
            }
            printf("   %s: eight of the walk, four of the recoil, four of"
                   " the win pose\n", pDef->szName);
        }
    }

    /* --- the briefing screen draws ---------------------------------------
     *
     * It is the first thing the mode shows and the thing every match returns
     * to, so a blank one strands the player with no way back to the race.
     */
    {
        tMechaBriefing brief;
        int aiBrief[256];
        int i;

        memset(&brief, 0, sizeof(brief));
        brief.szResult = "LAST MATCH:  VICTORY";
        brief.bResultWin = true;
        brief.iSelection = 1;
        /* Every row the mode actually builds, because the thing this test
         * is really guarding is that the whole screen fits. */
        brief.iRowCount = 9;
        brief.aRows[0].szLabel = "START MATCH";
        brief.aRows[1].szLabel = "YOUR MECH";
        brief.aRows[1].szValue = mecha_def_get(0)->szName;
        brief.aRows[2].szLabel = "OPPONENT";
        brief.aRows[2].szValue = mecha_def_get(2)->szName;
        brief.aRows[3].szLabel = "ARENA";
        brief.aRows[3].szValue = mecha_arena_name(0);
        brief.aRows[4].szLabel = "OPPONENT SKILL";
        brief.aRows[4].szValue = mecha_sim_ai_skill_name(MECHA_AI_VETERAN);
        brief.aRows[5].szLabel = "ROUND TIME";
        brief.aRows[5].szValue = "DEATHMATCH";
        brief.aRows[6].szLabel = "ENEMY WEAPONS";
        brief.aRows[6].szValue = "HELD - DEBUG";
        brief.aRows[7].szLabel = "VIEW CONTROLS";
        brief.aRows[8].szLabel = "EXIT TO WHIPLASH";

        /* An index nothing in the mode paints with, so anything still
         * carrying it afterwards is a pixel the briefing failed to cover.
         * It used to be 255, which stopped working the moment that became
         * the armour green. */
        memset(s_aFrame, MECHA_TEST_SENTINEL, sizeof(s_aFrame));
        mecha_render_briefing(&brief, s_aFrame, FRAME_W, FRAME_H);
        histogram(s_aFrame, aiBrief);
        dump_frame(szOutDir, "arena_briefing.png");

        /* Nothing left over from the frame before, and more than a fill. */
        CHECK(aiBrief[MECHA_TEST_SENTINEL] == 0);
        CHECK(distinct_colours(aiBrief) >= 4);

        /* The screen has to fit in both video modes; 320x200 is the one
         * that binds. [TEST-05] */
        {
            static uint8 aSmall[320 * 200];
            int iLast = last_ink_row(s_aFrame, FRAME_W, FRAME_H);

            CHECK(iLast > 0);
            CHECK(iLast < FRAME_H - 2);

            memset(aSmall, MECHA_TEST_SENTINEL, sizeof(aSmall));
            mecha_render_briefing(&brief, aSmall, 320, 200);
            iLast = last_ink_row(aSmall, 320, 200);
            CHECK(iLast > 0);
            CHECK(iLast < 198);
            /* And it still drew the whole thing rather than shrinking to
             * nothing: the rows and the footer under them have to be below
             * the middle of the screen. */
            CHECK(iLast > 110);
        }

        /*
         * And the controls, which are a page of their own now. Same two
         * questions: it drew something, and all of it fits in the small
         * video mode -- it is the longer of the two screens, ten controls
         * plus a double-height title.
         */
        {
            static uint8 aControls[320 * 200];
            int aiControls[256];
            int iLast;

            memset(aControls, MECHA_TEST_SENTINEL, sizeof(aControls));
            mecha_render_controls(aControls, 320, 200);
            histogram_of(aControls, 320 * 200, aiControls);
            CHECK(aiControls[MECHA_TEST_SENTINEL] == 0);
            CHECK(distinct_colours(aiControls) >= 3);
            iLast = last_ink_row(aControls, 320, 200);
            CHECK(iLast > 100);
            CHECK(iLast < 198);

            memset(s_aFrame, MECHA_TEST_SENTINEL, sizeof(s_aFrame));
            mecha_render_controls(s_aFrame, FRAME_W, FRAME_H);
            dump_frame(szOutDir, "arena_controls.png");
        }

        /* Only meaningful while the mode is choosing all the colours: the
         * retail font brings its own indices. [TEST-02] */
        if (!mecha_render_font_is_retail()) {
            for (i = 0; i < 256; i++) {
                if (aiBrief[i] > 0)
                    CHECK(mecha_render_palette_defines(i));
            }
        }
    }

    /*
     * --- a full arena, cycling targets, and nobody to follow --------------
     *
     * Survival fills every slot, so this is the crowded case: sixteen
     * machines, the reticle stepping through all of them, and then the same
     * world drawn the way a spectator sees it -- no view mech at all.
     * [TEST-09]
     */
    {
        static tMechaInput aInputs[MECHA_MAX_MECHS];
        int iSlot;
        int iTick;
        int aiSurvival[256];

        mecha_sim_init(&s_World, 0, 0x5A1Eu, 2);
        iPlayer = mecha_sim_add_mech(&s_World, 0, MECHA_CONTROL_HUMAN, 0);
        CHECK(iPlayer == 0);
        for (iSlot = 0; iSlot < MECHA_MAX_MECHS; iSlot++)
            if (mecha_sim_add_mech(&s_World, (1 + iSlot) % mecha_def_count(),
                                   MECHA_CONTROL_AI, (uint8)(iSlot + 1)) < 0)
                break;
        CHECK(s_World.iMechCount == MECHA_MAX_MECHS);
        mecha_sim_begin_match(&s_World);
        mecha_camera_reset(&s_Camera);
        memset(aInputs, 0, sizeof(aInputs));
        run_to_fight(aInputs);

        /* Tab held on alternate ticks is one target change every other tick,
         * which walks the lock right round the arena several times over. */
        for (iTick = 0; iTick < MECHA_TICK_HZ * 8; iTick++) {
            aInputs[iPlayer].bCycleTarget = (iTick & 1) == 0;
            mecha_sim_tick(&s_World, aInputs, MECHA_MAX_MECHS);
            render_now(pRenderer, iPlayer);
        }
        histogram(s_aFrame, aiSurvival);
        CHECK(distinct_colours(aiSurvival) >= 4);
        dump_frame(szOutDir, "arena_survival.png");

        /* Spectating: the same world with no machine of the player's own.
         * The scene still has to be drawn -- a frame that never arrives is
         * indistinguishable from a hung game. [TEST-10] */
        memset(s_aFrame, MECHA_TEST_SENTINEL, sizeof(s_aFrame));
        mecha_render_frame(pRenderer, &s_World, &s_Camera, -1,
                           s_aFrame, FRAME_W, FRAME_H,
                           s_aQuads, MECHA_QUAD_CAPACITY);
        histogram(s_aFrame, aiSurvival);
        CHECK(aiSurvival[MECHA_TEST_SENTINEL] == 0);
        CHECK(distinct_colours(aiSurvival) >= 4);
        dump_frame(szOutDir, "arena_spectate.png");
    }

    game_render_destroy(pRenderer);
    printf("mecha render: headless software frames rasterised\n");
    return 0;
}
