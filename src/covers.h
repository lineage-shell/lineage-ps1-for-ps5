/*
 * SwanStationPS5 - cover art: local lookup, background download and decoding.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SwanStationPS5_COVERS_H
#define SwanStationPS5_COVERS_H

#include "library.h"
#include "platform/platform.h"

/* (Re)binds the cover cache to a freshly scanned library. */
void covers_start(const Library *lib, const Paths *paths, const Settings *settings);
void covers_stop(void);

/* Main thread, once per frame: uploads finished images, requests the covers
 * around `center` and frees the ones far away. */
void covers_update(int center);
/* The same for a filtered or sorted shelf: view[k] is the library index at
 * shelf position k, center_pos the selected position. */
void covers_update_view(const int *view, int count, int center_pos);

/* shelf v3: covers requested within `load_radius` positions of the centre and kept there; beyond it the least
 * recently shown stay until more than `max_textures` are up. Set before covers_update_view; -1 = the stock radii. */
void covers_set_window(int load_radius, int max_textures);
/* Frames since game `index`'s texture went up (for a fade in); -1 while there is none. */
int covers_age(int index);

/* Texture for game `index`; a generated title card until the art is ready. */
PlatTexture *covers_get(int index);
/* The PS1 theme's memory-card icon of game `index`'s cover (32x32, nearest-neighbour); NULL until the real cover
 * has loaded, or when there is none. */
PlatTexture *covers_icon(int index);
/* true when the picture is only a generated title card (no real cover found) */
bool covers_is_placeholder(int index);
/* Average colour of a loaded cover (0 until it has loaded). */
uint32_t covers_color(int index);

/* Downloads still queued, for the "Downloading covers" indicator. */
int covers_downloading(void);

/* URL of a cover in xlenore/psx-covers (also used by tools/SwanStationPS5_sync.py). */
void covers_url(char *out, size_t size, int style, const char *serial);

/* The cover for a disc serial, from the cache or downloaded; RGBA to free(), NULL when there is none. It blocks:
 * call it from a thread. The file stays in the cache, so the shelf finds it when the game is added. */
uint8_t *covers_for_serial(const char *serial, int *w, int *h);

/* Forgets game `index`'s cover so it is looked up again (after a pick). */
void covers_reload(int index);
/* Where picked covers are kept: <root>/covers/custom/<game id>.<ext> */
void covers_custom_dir(char *out, size_t size);
/* .png / .jpg / .jpeg */
bool covers_is_image(const char *name);
/* Decodes an image to RGBA, scaled down like the shelf's covers; free() it. */
uint8_t *covers_decode(const char *path, int *w, int *h);

#endif
