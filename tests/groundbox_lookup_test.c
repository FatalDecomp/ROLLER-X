/*
 * The groundbox's nearest-section lookup, on tracks built here rather than
 * loaded, so each property is checked against a shape whose right answer is
 * known by construction. [GBOX-01]
 */
#include "groundbox.h"
#include "3d.h"
#include "loadtrak.h"
#include <stdio.h>
#include <math.h>

/* The globals the lookup reads. Defined here so the test links without
 * dragging in the track loader and the renderer behind it. */
int       GroundColour[MAX_TRACK_CHUNKS][5];
tGroundPt GroundPt[MAX_TRACK_CHUNKS];
int       TRAK_LEN;

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

    printf("groundbox lookup: %d checks, %d failures\n",
           s_iChecks, s_iFailures);
    return s_iFailures != 0;
}
