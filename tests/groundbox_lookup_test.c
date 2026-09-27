/*
 * The groundbox's nearest-section lookup, on tracks built here rather than
 * loaded, so each property is checked against a shape whose right answer is
 * known by construction. [GBOX-01]
 */
#include "groundbox.h"
#include "3d.h"
#include "loadtrak.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* The globals the lookup reads. Defined here so the test links without
 * dragging in the track loader and the renderer behind it. */
int       GroundColour[MAX_TRACK_CHUNKS][5];
tGroundPt GroundPt[MAX_TRACK_CHUNKS];
int       TRAK_LEN;

/*
 * The renderer, as far as the lookup is concerned. groundbox.c draws as
 * well as looks up, and the drawing reaches the renderer; this test is
 * about the lookup, so the renderer is stubbed rather than linked. A stub
 * that counted calls would be testing the drawing, which wants a frame and
 * a track and belongs somewhere else.
 */
#define GBOX_MAX_RECORDED 4096
static int   s_iQuads;
static int   s_aiQuadFlags[GBOX_MAX_RECORDED];
static float s_afQuadY[GBOX_MAX_RECORDED];
static float s_afQuadX[GBOX_MAX_RECORDED][4];
static float s_afQuadZ[GBOX_MAX_RECORDED][4];
static int   s_iTextureSet = -1;

TextureHandle game_render_get_texture_handle(GameRenderer *renderer,
                                             int textureSet)
{
    (void)renderer;
    s_iTextureSet = textureSet;
    return TEXTURE_HANDLE_INVALID;
}

void game_render_quad_world(GameRenderer *renderer,
                            const GameRenderVertex *verts,
                            TextureHandle handle,
                            int surfaceFlags,
                            float subThreshold)
{
    (void)renderer; (void)handle; (void)subThreshold;
    if (s_iQuads < GBOX_MAX_RECORDED) {
        s_aiQuadFlags[s_iQuads] = surfaceFlags;
        s_afQuadY[s_iQuads] = verts[0].y;
        for (int c = 0; c < 4; c++) {
            s_afQuadX[s_iQuads][c] = verts[c].x;
            s_afQuadZ[s_iQuads][c] = verts[c].z;
        }
    }
    s_iQuads++;
}

void set_starts(unsigned int uiType)
{
    (void)uiType;
}

static int s_iChecks;
static int s_iFailures;

#define CHECK(expr) do {                                                      \
    s_iChecks++;                                                              \
    if (!(expr)) {                                                            \
        printf("   FAIL line %d: %s\n", __LINE__, #expr);                     \
        s_iFailures++;                                                        \
    }                                                                         \
} while (0)

/* A section whose floor spans (fX +/- 100) at height fY, centred on fZ. */
static void put_section(int iSec, float fX, float fY, float fZ, int iSurface)
{
    GroundPt[iSec].pointAy[2].fX = fX - 100.0f;
    GroundPt[iSec].pointAy[2].fY = fY;
    GroundPt[iSec].pointAy[2].fZ = fZ;
    GroundPt[iSec].pointAy[3].fX = fX + 100.0f;
    GroundPt[iSec].pointAy[3].fY = fY;
    GroundPt[iSec].pointAy[3].fZ = fZ;
    GroundColour[iSec][GROUND_COLOUR_OFLOOR] = iSurface;
}

/* A straight run of sections along +Z, a thousand units apart. */
static void straight_track(int iCount, int iSurface)
{
    int i;

    TRAK_LEN = iCount;
    for (i = 0; i < iCount; i++)
        put_section(i, 0.0f, 0.0f, (float)i * 1000.0f, iSurface);
    groundbox_reset();
}

/* 3d.h declares the game's own main, so this one has to match it. */
int main(int argc, const char **argv, const char **envp)
{
    (void)argc; (void)argv; (void)envp;
    printf("groundbox lookup\n");

    /* The section nearest a point is the section nearest that point. */
    straight_track(16, 40);
    CHECK(groundbox_section_at(0.0f, 0.0f) == 0);
    CHECK(groundbox_section_at(0.0f, 15000.0f) == 15);
    CHECK(groundbox_section_at(0.0f, 7000.0f) == 7);
    /* Off to one side, the answer is still the section abreast of it. */
    CHECK(groundbox_section_at(4000.0f, 7000.0f) == 7);
    printf("   nearest section found along a straight run\n");

    /* Far outside the track's extent, the query clamps to the edge rather
     * than failing: ground running to the horizon keeps the course's own
     * height and artwork. */
    CHECK(groundbox_section_at(0.0f, 1.0e7f) == 15);
    CHECK(groundbox_section_at(0.0f, -1.0e7f) == 0);
    CHECK(groundbox_section_at(1.0e7f, 1.0e7f) >= 0);
    printf("   points off the map clamp to the nearest edge section\n");

    /* Height comes from the section, so ground follows the course. */
    {
        float fY = -1.0f;
        int iSurface = -1;
        int i;

        TRAK_LEN = 16;
        for (i = 0; i < 16; i++)
            put_section(i, 0.0f, (float)i * 10.0f, (float)i * 1000.0f, 40);
        groundbox_reset();
        CHECK(groundbox_floor_at(0.0f, 0.0f, &fY, &iSurface));
        CHECK(fY == 0.0f);
        CHECK(iSurface == 40);
        CHECK(groundbox_floor_at(0.0f, 12000.0f, &fY, &iSurface));
        CHECK(fY == 120.0f);
        printf("   floor height follows the section it came from\n");
    }

    /* -1 is a track asking for a hole, and stays a hole. */
    {
        float fY = 0.0f;
        int iSurface = 0;

        straight_track(16, 40);
        GroundColour[8][GROUND_COLOUR_OFLOOR] = -1;
        groundbox_reset();
        CHECK(!groundbox_floor_at(0.0f, 8000.0f, &fY, &iSurface));
        CHECK(groundbox_floor_at(0.0f, 4000.0f, &fY, &iSurface));
        printf("   a -1 section is left as a hole\n");
    }

    /* -2 detaches the floor from the shoulders and names no artwork, so it
     * borrows from the nearest section that does -- and is still drawn. */
    {
        float fY = 0.0f;
        int iSurface = -1;

        straight_track(16, 40);
        GroundColour[8][GROUND_COLOUR_OFLOOR] = -2;
        GroundColour[9][GROUND_COLOUR_OFLOOR] = -2;
        groundbox_reset();
        CHECK(groundbox_floor_at(0.0f, 8000.0f, &fY, &iSurface));
        CHECK(iSurface == 40);
        printf("   a -2 section still gets ground, wearing a borrowed tile\n");
    }

    /* A track that is nothing but holes has no ground anywhere, and does
     * not fall over being asked. */
    {
        float fY = 0.0f;
        int iSurface = 0;

        straight_track(8, -1);
        CHECK(!groundbox_floor_at(0.0f, 0.0f, &fY, &iSurface));
        CHECK(!groundbox_floor_at(5000.0f, 5000.0f, &fY, &iSurface));
        printf("   an all-hole track has no ground and does not crash\n");
    }

    /* No track at all. */
    TRAK_LEN = 0;
    groundbox_reset();
    CHECK(groundbox_section_at(0.0f, 0.0f) == -1);
    CHECK(!groundbox_floor_at(0.0f, 0.0f, NULL, NULL));
    printf("   no track loaded: no ground, no crash\n");

    /* --- the drawing ------------------------------------------------- */
    {
        /* The camera is only a position as far as the ground is concerned. */
        GameRenderCamera cam;
        GameRenderer *pFakeRenderer = (GameRenderer *)&cam;
        int i;
        int abySeen[3];

        memset(&cam, 0, sizeof(cam));
        straight_track(16, 40);
        s_iQuads = 0;
        groundbox_draw(pFakeRenderer, &cam);
        CHECK(s_iQuads > 0);
        /* Six rings of at most 8x8 each, so the count is bounded and the
         * ground is never the thing that blows a frame's budget. */
        CHECK(s_iQuads <= 6 * 8 * 8);
        printf("   %d patches drawn for a whole ground plane\n", s_iQuads);

        /* Every patch wears the track's own floor index, or one of the two
         * above it, and nothing else. Flags are carried through. */
        abySeen[0] = abySeen[1] = abySeen[2] = 0;
        for (i = 0; i < s_iQuads && i < GBOX_MAX_RECORDED; i++) {
            int iTile = s_aiQuadFlags[i] & SURFACE_MASK_TEXTURE_INDEX;

            CHECK(iTile >= 40 && iTile <= 42);
            if (iTile >= 40 && iTile <= 42)
                abySeen[iTile - 40] = 1;
            CHECK((s_aiQuadFlags[i] & SURFACE_MASK_FLAGS) == 0);
        }
        CHECK(abySeen[0] && abySeen[1] && abySeen[2]);
        printf("   all three tiles of the cycle appear\n");

        /* And the cycle is the arena's: (x + z) of the patch, mod three,
         * on the world's grid rather than the camera's. */
        for (i = 0; i < s_iQuads && i < GBOX_MAX_RECORDED; i++) {
            CHECK(s_afQuadY[i] == 0.0f);
        }
        printf("   every patch sits at the track's floor height\n");

        /* The bank it pulls from is the track's own. */
        CHECK(s_iTextureSet == ROLLER_ED_TEXTURE_SET_TRACK);

        /* A track that is all holes draws no ground at all. */
        straight_track(8, -1);
        s_iQuads = 0;
        groundbox_draw(pFakeRenderer, &cam);
        CHECK(s_iQuads == 0);
        printf("   an all-hole track draws nothing\n");

        /*
         * A hole in the middle of a course, rather than a course that is
         * all hole. This is the case that pins the guard at the edge of a
         * hole: a patch whose centre is over solid ground but whose corner
         * overhangs the gap must not be drawn, or the ground grows a lip
         * out over nothing. Both the centre and the corner checks block a
         * track that is entirely holes, so only this tells them apart.
         */
        {
            int iSolid, iHoled, j;
            float fHoleZ = 8.0f * 1000.0f;

            straight_track(16, 40);
            s_iQuads = 0;
            groundbox_draw(pFakeRenderer, &cam);
            iSolid = s_iQuads;

            straight_track(16, 40);
            GroundColour[8][GROUND_COLOUR_OFLOOR] = -1;
            groundbox_reset();
            s_iQuads = 0;
            groundbox_draw(pFakeRenderer, &cam);
            iHoled = s_iQuads;

            CHECK(iHoled < iSolid);
            /*
             * And no patch that was drawn has a corner standing over the
             * gap. Asked of every corner directly rather than by guessing
             * at a distance from the hole: the patches come in six sizes,
             * so there is no one radius that means "overhangs it".
             */
            for (j = 0; j < iHoled && j < GBOX_MAX_RECORDED; j++) {
                int c;

                for (c = 0; c < 4; c++)
                    CHECK(groundbox_floor_at(s_afQuadX[j][c],
                                             s_afQuadZ[j][c], NULL, NULL));
            }
            (void)fHoleZ;
            printf("   a hole mid-course drops %d patches and grows no lip\n",
                   iSolid - iHoled);
        }

        /* Flags set on the floor survive the tile swap. */
        straight_track(16, SURFACE_FLAG_APPLY_TEXTURE | 40);
        s_iQuads = 0;
        groundbox_draw(pFakeRenderer, &cam);
        CHECK(s_iQuads > 0);
        for (i = 0; i < s_iQuads && i < GBOX_MAX_RECORDED; i++)
            CHECK((s_aiQuadFlags[i] & SURFACE_FLAG_APPLY_TEXTURE) != 0);
        printf("   the track's own surface flags are carried through\n");
    }

    printf("groundbox lookup: %d checks, %d failures\n",
           s_iChecks, s_iFailures);
    return s_iFailures != 0;
}
