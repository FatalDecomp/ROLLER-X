#ifndef _ROLLER_GROUNDBOX_H
#define _ROLLER_GROUNDBOX_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------
/*
 * The ground under everything: floor geometry generated for any track,
 * whether or not it set the flag that detaches its outer floor from the
 * shoulders. It is to the horizon what the sky half already is -- scenery
 * rather than track -- so it carries no collision and adds nothing to the
 * .trk. Its artwork is the floor index the track already names.
 *
 * This header is the part of it that answers "what is the ground doing at
 * this point on the map", which the drawing needs once per cell and which
 * has to be cheap. [GBOX-01]
 */
//-------------------------------------------------------------------------------------------------

/* Throw away the lookup, so the next query rebuilds it. Safe to call at any
 * time; call it when a track is loaded or unloaded. */
void groundbox_reset(void);

/*
 * The track section whose floor is nearest (fX, fZ), or -1 when there is no
 * track loaded. Points outside the track's own extent take the nearest
 * section on its edge, which is what makes ground that runs off to the
 * horizon keep the height and artwork of the course it is running away
 * from.
 */
int groundbox_section_at(float fX, float fZ);

/*
 * Where the ground sits at (fX, fZ) and what it is painted with.
 *
 * Returns false when the nearest section says it has no outer floor at all
 * -- GroundColour's -1 -- which is a track asking for a hole, and a hole is
 * left alone. Otherwise *pfY is the floor height there and *piSurface is
 * the surface word the track names for it, ready for the tile cycle to be
 * applied on top.
 */
bool groundbox_floor_at(float fX, float fZ, float *pfY, int *piSurface);

//-------------------------------------------------------------------------------------------------
#endif
