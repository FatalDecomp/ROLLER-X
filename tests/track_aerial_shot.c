/*
 * Render one frame of a real track from an arbitrary camera, to a PNG.
 *
 * The arena has had a headless renderer since it was written, and the race
 * game has not: its pictures come from playing it. That is fine until a
 * change is to the scenery rather than to the car, at which point the only
 * way to see whether it worked is to load a real track and look -- which
 * is what this does, on the editor core's track-only path, with the
 * software renderer so it runs where there is no GPU.
 *
 * It takes the camera as arguments rather than hard-coding a shot, because
 * finding the useful angle takes more tries than rebuilding should. [GBOX-03]
 *
 * usage: track_aerial_shot TRACK ASSET_ROOT OUT.png [height] [pitch] [yaw]
 */
#include "3d.h"
#include "editor_api.h"
#include "editor_camera.h"
#include "loadtrak.h"
#include "png_writer.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum { WIDTH = 960, HEIGHT = 640, ROW_PITCH = WIDTH * 4 };

typedef struct
{
    const char *szTrackPath;
    const char *szAssetRoot;
    const char *szOutPath;
    float fHeight;
    float fPitch;
    float fYaw;
    int iResult;
} tShotContext;

static int SDLCALL shot_worker(void *pUserData)
{
    tShotContext *pContext = (tShotContext *)pUserData;
    tRollerEdInitInfo InitInfo = {
        .uiStructSize = sizeof(InitInfo),
        .uiVersion = ROLLER_ED_INIT_INFO_VERSION,
        .szAssetRoot = pContext->szAssetRoot,
        /* Software, and fallback allowed: this has to run in a container
         * with no GPU backend at all. */
        .ePreferredRenderer = ROLLER_ED_RENDERER_SOFTWARE,
        .uiAllowSoftwareFallback = 1u
    };
    uint8_t *pPixels = NULL;
    double dCx = 0.0, dCy = 0.0, dCz = 0.0;
    int iSection;

    pContext->iResult = 1;
    if (RollerEd_Init(&InitInfo) != ROLLER_ED_RESULT_OK) {
        fprintf(stderr, "RollerEd_Init failed: %s\n", RollerEd_GetLastError());
        return 1;
    }
    if (RollerEd_LoadTrackFile(pContext->szTrackPath, pContext->szAssetRoot)
            != ROLLER_ED_RESULT_OK) {
        fprintf(stderr, "RollerEd_LoadTrackFile failed: %s\n",
                RollerEd_GetLastError());
        goto shutdown;
    }
    if (TRAK_LEN <= 0) {
        fprintf(stderr, "track loaded with no sections\n");
        goto shutdown;
    }

    /* The middle of the whole course, so an aerial sees the shape of it
     * rather than whichever corner section zero happens to be. */
    for (iSection = 0; iSection < TRAK_LEN; iSection++) {
        int iPoint;

        for (iPoint = 0; iPoint < 6; iPoint++) {
            dCx += TrakPt[iSection].pointAy[iPoint].fX;
            dCy += TrakPt[iSection].pointAy[iPoint].fY;
            dCz += TrakPt[iSection].pointAy[iPoint].fZ;
        }
    }
    dCx /= (double)(TRAK_LEN * 6);
    dCy /= (double)(TRAK_LEN * 6);
    dCz /= (double)(TRAK_LEN * 6);
    printf("track: %d sections, centre (%.0f, %.0f, %.0f)\n",
           TRAK_LEN, dCx, dCy, dCz);
    {
        int iHole = 0, iDetach = 0, iReal = 0, i;
        int iFirstReal = -1;

        for (i = 0; i < TRAK_LEN; i++) {
            int iF = GroundColour[i][GROUND_COLOUR_OFLOOR];

            if (iF == -1) iHole++;
            else if (iF == -2) iDetach++;
            else { iReal++; if (iFirstReal < 0) iFirstReal = iF; }
        }
        printf("outer floor: %d holes(-1), %d detached(-2), %d real"
               " (first real surface word %d, texture %d)\n",
               iHole, iDetach, iReal, iFirstReal,
               iFirstReal >= 0 ? (iFirstReal & 0xFF) : -1);
    }

    /* The ground's reach, by the same definition groundbox.c uses for its
     * cell: the mean spacing of consecutive section centres, times the
     * outermost ring's half-width. Worth printing beside the floor indices
     * because from high enough up the edge of it is in shot. */
    {
        double dTotal = 0.0;
        int iSpans = 0, i;

        for (i = 0; i < TRAK_LEN; i++) {
            int iNext = i + 1 < TRAK_LEN ? i + 1 : 0;
            double dAx = 0.5 * (GroundPt[i].pointAy[2].fX
                                + GroundPt[i].pointAy[3].fX);
            double dAz = 0.5 * (GroundPt[i].pointAy[2].fZ
                                + GroundPt[i].pointAy[3].fZ);
            double dBx = 0.5 * (GroundPt[iNext].pointAy[2].fX
                                + GroundPt[iNext].pointAy[3].fX);
            double dBz = 0.5 * (GroundPt[iNext].pointAy[2].fZ
                                + GroundPt[iNext].pointAy[3].fZ);

            dTotal += sqrt((dBx - dAx) * (dBx - dAx)
                           + (dBz - dAz) * (dBz - dAz));
            iSpans++;
        }
        if (iSpans > 0) {
            double dCell = dTotal / (double)iSpans;

            printf("ground: cell %.0f, reaches %.0f units from the camera\n",
                   dCell, dCell * 128.0);
        }
    }

    /* "-" as the output asks only for the report above: scanning a data set
     * for the tracks that name a floor at all should not pay for a frame. */
    if (strcmp(pContext->szOutPath, "-") == 0) {
        pContext->iResult = 0;
        goto shutdown;
    }

    pPixels = (uint8_t *)malloc((size_t)ROW_PITCH * HEIGHT);
    if (!pPixels) {
        fprintf(stderr, "pixel allocation failed\n");
        goto shutdown;
    }

    {
        tEdCameraState Camera = {
            .uiStructSize = sizeof(Camera),
            .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION,
            /* Height goes on Y: roller_ed_camera_apply feeds fPosition[1]
             * to worldy, and worldy is the vertical axis -- 3d.c hands it
             * to the renderer as viewY, which is the axis the ground's own
             * quads measure their height along. Putting it on Z instead
             * slides the camera along the map at ground level, and under
             * the terrain wherever the course climbs. */
            .fPosition = { (float)dCx, (float)dCy + pContext->fHeight,
                           (float)dCz },
            .fYawDegrees = pContext->fYaw,
            .fPitchDegrees = pContext->fPitch
        };

        printf("camera: (%.0f, %.0f, %.0f) yaw %.0f pitch %.0f\n",
               Camera.fPosition[0], Camera.fPosition[1], Camera.fPosition[2],
               Camera.fYawDegrees, Camera.fPitchDegrees);
        if (RollerEd_SetCamera(&Camera) != ROLLER_ED_RESULT_OK) {
            fprintf(stderr, "RollerEd_SetCamera failed: %s\n",
                    RollerEd_GetLastError());
            goto shutdown;
        }
    }

    memset(pPixels, 0, (size_t)ROW_PITCH * HEIGHT);
    if (RollerEd_RenderFrame(pPixels, ROW_PITCH * HEIGHT, ROW_PITCH,
                             WIDTH, HEIGHT, ROLLER_ED_PIXEL_RGBA8)
            != ROLLER_ED_RESULT_OK) {
        fprintf(stderr, "RollerEd_RenderFrame failed: %s\n",
                RollerEd_GetLastError());
        goto shutdown;
    }

    if (RollerWriteRgbaPng(pContext->szOutPath, pPixels, WIDTH, HEIGHT) != 0) {
        fprintf(stderr, "writing %s failed\n", pContext->szOutPath);
        goto shutdown;
    }
    printf("wrote %s\n", pContext->szOutPath);
    pContext->iResult = 0;

shutdown:
    free(pPixels);
    if (RollerEd_Shutdown() != ROLLER_ED_RESULT_OK)
        fprintf(stderr, "RollerEd_Shutdown failed: %s\n",
                RollerEd_GetLastError());
    return pContext->iResult;
}

int main(int argc, char **argv)
{
    tRollerEdBootstrapInfo BootstrapInfo = {
        .uiStructSize = sizeof(BootstrapInfo),
        .uiVersion = ROLLER_ED_BOOTSTRAP_INFO_VERSION,
        .uiFlags = 0u
    };
    tShotContext Context;
    SDL_Thread *pWorker;
    int iWorkerResult = 1;

    if (argc < 4) {
        fprintf(stderr,
                "usage: %s TRACK ASSET_ROOT OUT.png [height] [pitch] [yaw]\n",
                argv[0]);
        return 2;
    }
    memset(&Context, 0, sizeof(Context));
    Context.szTrackPath = argv[1];
    Context.szAssetRoot = argv[2];
    Context.szOutPath = argv[3];
    Context.fHeight = argc > 4 ? (float)atof(argv[4]) : 20000.0f;
    Context.fPitch = argc > 5 ? (float)atof(argv[5]) : -60.0f;
    Context.fYaw = argc > 6 ? (float)atof(argv[6]) : 0.0f;

    SDL_SetMainReady();
    if (RollerEd_Bootstrap(&BootstrapInfo) != ROLLER_ED_RESULT_OK) {
        fprintf(stderr, "RollerEd_Bootstrap failed: %s\n",
                RollerEd_GetLastError());
        return 1;
    }
    pWorker = SDL_CreateThread(shot_worker, "track-aerial-shot", &Context);
    if (!pWorker) {
        fprintf(stderr, "worker creation failed: %s\n", SDL_GetError());
        RollerEd_Teardown();
        return 1;
    }
    SDL_WaitThread(pWorker, &iWorkerResult);
    if (RollerEd_Teardown() != ROLLER_ED_RESULT_OK && iWorkerResult == 0)
        fprintf(stderr, "RollerEd_Teardown failed: %s\n",
                RollerEd_GetLastError());
    return iWorkerResult;
}
