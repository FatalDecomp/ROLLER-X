#include "groundbox.h"
#include "3d.h"
#include "loadtrak.h"
#include "drawtrk3.h"
#include "editor_api.h"
#include "editor_surface.h"
#include <math.h>
//-------------------------------------------------------------------------------------------------
/*
 * Answering "which section is nearest" once per ground cell, per frame, is
 * the whole cost of this feature, so it is answered out of a table rather
 * than by searching.
 *
 * The table is a coarse square grid laid over the track's own extent, each
 * bucket holding the section whose floor centre is nearest to it. Building
 * it is a brute-force sweep -- every bucket against every section -- which
 * is 64 x 64 x 500 distance tests at the very worst and runs once when a
 * track loads, not per frame. Walking outward from the section the camera
 * is on would be cheaper to build and much worse to reason about: a course
 * that doubles back passes close to itself, and "next section along" stops
 * meaning "next section in space" exactly where the answer starts to
 * matter.
 *
 * Queries outside the grid clamp to its edge instead of failing. That is
 * not a fallback, it is the behaviour: ground running out to the horizon
 * should keep the height and the artwork of the piece of course it is
 * running away from, and the nearest edge bucket is that piece.
 * [GBOX-01]
 */
//-------------------------------------------------------------------------------------------------

#define GBOX_GRID 64

static int16_t s_aiNearest[GBOX_GRID][GBOX_GRID];
/* The surface word each section's ground should wear -- its own, or a
 * borrowed one where it has none of its own. See gbox_build_surfaces. */
static int     s_aiSurface[MAX_TRACK_CHUNKS];
static float   s_fX0;
static float   s_fZ0;
static float   s_fStep;
static float   s_fCell;
static int     s_iBuiltLen = -1;
static float   s_fBuiltMark;
static bool    s_bReady;

//-------------------------------------------------------------------------------------------------

/*
 * The middle of a section's floor. Points 2 and 3 are its inner edges --
 * the pair the outer-floor quad itself is built from -- so their midpoint
 * is both where the section is and how high its floor sits, which is the
 * two things every query wants.
 */
static void gbox_section_centre(int iSec, float *pfX, float *pfY, float *pfZ)
{
    const tVec3 *pLeft = &GroundPt[iSec].pointAy[2];
    const tVec3 *pRight = &GroundPt[iSec].pointAy[3];

    *pfX = 0.5f * (pLeft->fX + pRight->fX);
    *pfY = 0.5f * (pLeft->fY + pRight->fY);
    *pfZ = 0.5f * (pLeft->fZ + pRight->fZ);
}

//-------------------------------------------------------------------------------------------------

/*
 * What each section's ground is painted with.
 *
 * Most sections name a surface and wear it. Two do not, and they mean
 * opposite things:
 *
 *  -1 is a track asking for no floor at all. That is a hole and it stays a
 *     hole; the query fails and nothing is drawn.
 *  -2 is the flag that detaches the outer floor from the shoulders, and the
 *     normal floor quad is skipped for it [drawtrk3.c]. The section still
 *     wants ground under it -- it is only the attached floor it has given
 *     up -- but it names no artwork, so it borrows from the nearest section
 *     around the loop that does. Searching both ways at once keeps it
 *     symmetric, so a detached stretch takes its look from whichever end of
 *     itself is closer rather than always from behind.
 */
static void gbox_build_surfaces(void)
{
    int iSec;

    for (iSec = 0; iSec < TRAK_LEN; iSec++) {
        int iOwn = GroundColour[iSec][GROUND_COLOUR_OFLOOR];
        int iStep;

        if (iOwn >= 0 || iOwn == -1) {
            s_aiSurface[iSec] = iOwn;
            continue;
        }
        s_aiSurface[iSec] = -1;
        for (iStep = 1; iStep <= TRAK_LEN / 2; iStep++) {
            int iBack = iSec - iStep;
            int iFwd = iSec + iStep;

            while (iBack < 0)
                iBack += TRAK_LEN;
            while (iFwd >= TRAK_LEN)
                iFwd -= TRAK_LEN;
            if (GroundColour[iBack][GROUND_COLOUR_OFLOOR] >= 0) {
                s_aiSurface[iSec] = GroundColour[iBack][GROUND_COLOUR_OFLOOR];
                break;
            }
            if (GroundColour[iFwd][GROUND_COLOUR_OFLOOR] >= 0) {
                s_aiSurface[iSec] = GroundColour[iFwd][GROUND_COLOUR_OFLOOR];
                break;
            }
        }
    }
}

//-------------------------------------------------------------------------------------------------

static void gbox_build(void)
{
    float fMinX, fMaxX, fMinZ, fMaxZ;
    float fSpanX, fSpanZ, fSpan;
    float fSeedY;
    int iSec, iRow, iCol;

    s_bReady = false;
    if (TRAK_LEN <= 0 || TRAK_LEN > MAX_TRACK_CHUNKS)
        return;

    gbox_section_centre(0, &fMinX, &fSeedY, &fMinZ);
    fMaxX = fMinX;
    fMaxZ = fMinZ;
    for (iSec = 1; iSec < TRAK_LEN; iSec++) {
        float fX, fY, fZ;

        gbox_section_centre(iSec, &fX, &fY, &fZ);
        if (fX < fMinX) fMinX = fX;
        if (fX > fMaxX) fMaxX = fX;
        if (fZ < fMinZ) fMinZ = fZ;
        if (fZ > fMaxZ) fMaxZ = fZ;
    }

    /* Square the extent and pad it, so a long thin course does not get a
     * grid one bucket wide across its short axis. */
    fSpanX = fMaxX - fMinX;
    fSpanZ = fMaxZ - fMinZ;
    fSpan = fSpanX > fSpanZ ? fSpanX : fSpanZ;
    if (fSpan < 1.0f)
        fSpan = 1.0f;
    fSpan *= 1.1f;
    s_fX0 = 0.5f * (fMinX + fMaxX) - 0.5f * fSpan;
    s_fZ0 = 0.5f * (fMinZ + fMaxZ) - 0.5f * fSpan;
    s_fStep = fSpan / (float)GBOX_GRID;

    for (iRow = 0; iRow < GBOX_GRID; iRow++) {
        for (iCol = 0; iCol < GBOX_GRID; iCol++) {
            float fAtX = s_fX0 + ((float)iCol + 0.5f) * s_fStep;
            float fAtZ = s_fZ0 + ((float)iRow + 0.5f) * s_fStep;
            float fBest = -1.0f;
            int iBest = 0;

            for (iSec = 0; iSec < TRAK_LEN; iSec++) {
                float fX, fY, fZ, fDx, fDz, fDist;

                gbox_section_centre(iSec, &fX, &fY, &fZ);
                fDx = fX - fAtX;
                fDz = fZ - fAtZ;
                fDist = fDx * fDx + fDz * fDz;
                if (fBest < 0.0f || fDist < fBest) {
                    fBest = fDist;
                    iBest = iSec;
                }
            }
            s_aiNearest[iRow][iCol] = (int16_t)iBest;
        }
    }

    /*
     * How big one patch of ground is, taken from the track's own scale
     * rather than named as a number: the mean distance between consecutive
     * section centres. A course built at any scale then gets ground whose
     * tiles are the size of its own sections, which is the only definition
     * of "the right size" that survives not knowing the units.
     */
    {
        float fTotal = 0.0f;
        int iSpans = 0;

        for (iSec = 0; iSec < TRAK_LEN; iSec++) {
            int iNext = iSec + 1 < TRAK_LEN ? iSec + 1 : 0;
            float fAx, fAy, fAz, fBx, fBy, fBz, fDx, fDz;

            gbox_section_centre(iSec, &fAx, &fAy, &fAz);
            gbox_section_centre(iNext, &fBx, &fBy, &fBz);
            fDx = fBx - fAx;
            fDz = fBz - fAz;
            fTotal += sqrtf(fDx * fDx + fDz * fDz);
            iSpans++;
        }
        s_fCell = iSpans > 0 ? fTotal / (float)iSpans : 1.0f;
        if (s_fCell < 1.0f)
            s_fCell = 1.0f;
    }

    gbox_build_surfaces();
    {
        float fX, fY, fZ;

        gbox_section_centre(0, &fX, &fY, &fZ);
        s_fBuiltMark = fX + fY + fZ;
    }
    s_iBuiltLen = TRAK_LEN;
    s_bReady = true;
}

//-------------------------------------------------------------------------------------------------

/*
 * Rebuild when the track under us has changed. Length alone does not catch
 * a different course of the same length, so the first section's own centre
 * is carried as a mark beside it -- cheap, and wrong only if two tracks
 * share both their length and their start point, which is the same track.
 */
static bool gbox_ready(void)
{
    if (TRAK_LEN <= 0)
        return false;
    if (s_bReady && TRAK_LEN == s_iBuiltLen) {
        float fX, fY, fZ;

        gbox_section_centre(0, &fX, &fY, &fZ);
        if (fX + fY + fZ == s_fBuiltMark)
            return true;
    }
    gbox_build();
    return s_bReady;
}

//-------------------------------------------------------------------------------------------------

void groundbox_reset(void)
{
    s_bReady = false;
    s_iBuiltLen = -1;
}

//-------------------------------------------------------------------------------------------------

int groundbox_section_at(float fX, float fZ)
{
    int iCol, iRow;

    if (!gbox_ready())
        return -1;
    iCol = (int)floorf((fX - s_fX0) / s_fStep);
    iRow = (int)floorf((fZ - s_fZ0) / s_fStep);
    if (iCol < 0) iCol = 0;
    if (iRow < 0) iRow = 0;
    if (iCol >= GBOX_GRID) iCol = GBOX_GRID - 1;
    if (iRow >= GBOX_GRID) iRow = GBOX_GRID - 1;
    return (int)s_aiNearest[iRow][iCol];
}

//-------------------------------------------------------------------------------------------------

bool groundbox_floor_at(float fX, float fZ, float *pfY, int *piSurface)
{
    int iSec = groundbox_section_at(fX, fZ);
    float fCx, fCy, fCz;

    if (iSec < 0)
        return false;
    if (s_aiSurface[iSec] < 0)
        return false;                       /* the track asked for a hole */
    gbox_section_centre(iSec, &fCx, &fCy, &fCz);
    if (pfY)
        *pfY = fCy;
    if (piSurface)
        *piSurface = s_aiSurface[iSec];
    return true;
}

//-------------------------------------------------------------------------------------------------

/*
 * How far the ground reaches, and how it pays for it.
 *
 * A single grid fine enough to texture convincingly underfoot and wide
 * enough to reach the horizon is thousands of quads, which no software
 * rasteriser is going to give up for scenery. So the ground is drawn as
 * rings: a block of small patches around the camera, and around that a
 * ring of patches twice the size, and so on outward. Each ring costs the
 * same handful of quads and covers four times the area of the one inside
 * it, so the reach grows geometrically while the count grows linearly --
 * five rings past the middle reach 128 patches out for about three hundred
 * quads.
 *
 * Coarser artwork further away is not a compromise being accepted here; it
 * is the same argument [ARENA-30] makes about merging ground tiles, seen
 * from the other end. Nobody reads the grain of the ground at the horizon.
 */
/* Three, and the arena's own reason for three [ARENA-28]: two alternating
 * tiles read as a checkerboard, which is what a floor wants and the
 * opposite of what ground stretching to the horizon wants. */
#define GBOX_TILE_CYCLE 3
#define GBOX_TEXTURE_SET ROLLER_ED_TEXTURE_SET_TRACK

#define GBOX_RING_HALF 4
#define GBOX_RINGS     6

/*
 * One patch. Its corners are each asked for their own height, so a patch
 * spanning a change in the course is a facet rather than a flat lid over
 * it, and two patches sharing an edge agree along it because the height is
 * a function of position and nothing else.
 *
 * The tile is the track's own floor index plus the arena's cycle
 * [ARENA-28]: three consecutive indices chosen by (row + col) % 3, on the
 * world's own grid rather than the camera's, so the pattern stays put on
 * the ground instead of crawling with the viewer. Only the low byte moves;
 * every flag the track set on its floor is carried through untouched.
 */
static void gbox_draw_patch(GameRenderer *pRenderer, int iCellX, int iCellZ,
                            float fSize)
{
    GameRenderVertex aVertices[4];
    float afX[4];
    float afZ[4];
    float fX0 = (float)iCellX * fSize;
    float fZ0 = (float)iCellZ * fSize;
    int iSurface = 0;
    int iBase;
    int iTile;
    int i;

    /* Wound so the face looks up, matching the track's own floor. */
    afX[0] = fX0;         afZ[0] = fZ0;
    afX[1] = fX0;         afZ[1] = fZ0 + fSize;
    afX[2] = fX0 + fSize; afZ[2] = fZ0 + fSize;
    afX[3] = fX0 + fSize; afZ[3] = fZ0;

    /*
     * The centre query is here for the surface word; failing it early is
     * only an economy, saving four more queries on a patch that is over a
     * hole anyway. It is not what keeps ground off a hole -- the corners
     * below are, and they catch the case that matters, a patch whose
     * middle is over solid ground and whose corner overhangs the gap.
     */
    if (!groundbox_floor_at(fX0 + 0.5f * fSize, fZ0 + 0.5f * fSize,
                            NULL, &iSurface))
        return;

    for (i = 0; i < 4; i++) {
        float fY = 0.0f;

        if (!groundbox_floor_at(afX[i], afZ[i], &fY, NULL))
            return;                         /* a corner over a hole */
        aVertices[i].x = afX[i];
        aVertices[i].y = fY;
        aVertices[i].z = afZ[i];
        aVertices[i].u = 0.0f;
        aVertices[i].v = 0.0f;
    }

    iBase = iSurface & SURFACE_MASK_TEXTURE_INDEX;
    iTile = iBase;
    /* Only cycle where all three indices exist; near the top of the bank
     * the run would wrap onto unrelated artwork. */
    if (iBase + (GBOX_TILE_CYCLE - 1) <= SURFACE_MASK_TEXTURE_INDEX) {
        int iStep = (iCellX + iCellZ) % GBOX_TILE_CYCLE;

        if (iStep < 0)
            iStep += GBOX_TILE_CYCLE;
        iTile = iBase + iStep;
    }

    set_starts(ROLLER_ED_RENDER_UV_TILE);
    game_render_quad_world(
        pRenderer, aVertices,
        game_render_get_texture_handle(pRenderer, GBOX_TEXTURE_SET),
        (iSurface & SURFACE_MASK_FLAGS) | iTile,
        0.0f);
}

//-------------------------------------------------------------------------------------------------

void groundbox_draw(GameRenderer *pRenderer, const GameRenderCamera *pCamera)
{
    int iRing;

    if (!pRenderer || !pCamera || !gbox_ready())
        return;

    for (iRing = 0; iRing < GBOX_RINGS; iRing++) {
        float fSize = s_fCell * (float)(1 << iRing);
        int iCx = (int)floorf(pCamera->viewX / fSize);
        int iCz = (int)floorf(pCamera->viewZ / fSize);
        int iX, iZ;

        for (iZ = iCz - GBOX_RING_HALF; iZ < iCz + GBOX_RING_HALF; iZ++) {
            for (iX = iCx - GBOX_RING_HALF; iX < iCx + GBOX_RING_HALF; iX++) {
                if (iRing > 0) {
                    /* The middle of this ring is the ring inside it, drawn
                     * finer. Skip any patch wholly covered by it. */
                    float fInner = s_fCell * (float)(1 << (iRing - 1));
                    int iIx = (int)floorf(pCamera->viewX / fInner);
                    int iIz = (int)floorf(pCamera->viewZ / fInner);
                    float fLoX = (float)(iIx - GBOX_RING_HALF) * fInner;
                    float fHiX = (float)(iIx + GBOX_RING_HALF) * fInner;
                    float fLoZ = (float)(iIz - GBOX_RING_HALF) * fInner;
                    float fHiZ = (float)(iIz + GBOX_RING_HALF) * fInner;
                    float fX0 = (float)iX * fSize;
                    float fZ0 = (float)iZ * fSize;

                    if (fX0 >= fLoX && fX0 + fSize <= fHiX
                            && fZ0 >= fLoZ && fZ0 + fSize <= fHiZ)
                        continue;
                }
                gbox_draw_patch(pRenderer, iX, iZ, fSize);
            }
        }
    }
}
