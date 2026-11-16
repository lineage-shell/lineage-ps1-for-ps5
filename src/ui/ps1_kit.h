/*
 * SwanStationPS5 - the PS1 shelf v3 drawing kit: the SCPH-7002 blue shell's parts in its own coordinates.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Coordinates are the shell's NTSC 640x480 "units", widened to 16:9 (x -107..747, y 0..480), as in the v3 mockups
 * (design notes ps1/new-ux). One unit is 2.25 px of the 1920x1080 interface space, which the platform draws at
 * the display's real resolution. Every part is drawn from geometry or generated once at start; nothing is copied
 * from Sony. What the BIOS itself holds (the ROM font) is read from the user's own BIOS file (ps1_bios.c), with a
 * fallback when there is none.
 */
#ifndef SwanStationPS5_PS1_KIT_H
#define SwanStationPS5_PS1_KIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PS1K_S 2.25f
#define PS1K_X(x) (960.0f + ((x) - 320.0f) * PS1K_S)
#define PS1K_Y(y) ((y) * PS1K_S)
/* Settings > Library > Screen shape: 16:9 (the default) or 4:3, the console's own 640 x 480 area with dark sides */
bool ps1k_narrow(void);
/* the 16:9 rule: a group in the outer thirds of the 4:3 frame slides 64 units outward (4:3: it stays) */
float ps1k_ox(float x);
/* a position laid out again on the full 16:9 width (grids, rows, the info bar): 4:3 draws it into 0..640 */
float ps1k_wx(float x);
/* the usable width's edges, in units: -107 / 747, or 0 / 640 for 4:3 */
float ps1k_left(void);
float ps1k_right(void);

enum Ps1kColour
{
    PS1K_BLUE,
    PS1K_GREEN,
    PS1K_RED,
    PS1K_GOLD,
    PS1K_YELLOW,
    PS1K_PURPLE,
    PS1K_BLUE_BIG, /* the main menu's ornamental spheres */
    PS1K_COLOURS
};

/* The CD Player's track-grid colours, row by row (5 x 4), as the save slots, levels and pages use them */
extern const uint8_t PS1K_CD_ROWS[4][5];

enum
{
    PS1K_CENTRE = 0,
    PS1K_LEFT = 1, /* x is the left edge, y the middle */
    PS1K_RIGHT = 2
};

/* `b` is the console's texture brightness: 128 as is, 64 half (the "dim others" rule), up to 255 */
void ps1k_background(void);
/* The background again over what is drawn, `alpha` 1 = only the background: a screen's words fading in over it */
void ps1k_background_over(float alpha);
/* Call once a frame at the end of a screen's drawing: a new screen (another id, or one not drawn for a moment) fades in
 * over the background in 12 ticks */
void ps1k_screen_fade(int screen_id);
/* the menus' move sounds: one click when the cursor moved; at an edge silence, always. `dirs` = the buttons that move
 * this cursor (kept for the callers). */
/* main.c: the BIOS's startup played (or the app restarted after a game): the shelf plays no startup.wav after it */
void ps1k_intro_played(void);
bool ps1k_was_intro(void);
void ps1k_nav_fresh(uint32_t fresh); /* main.c: the buttons pressed this frame for real (not the auto-repeat) */
void ps1k_move_sound(bool moved, uint32_t pressed, uint32_t dirs);
void ps1k_sphere(int colour, float cx, float cy, float size, int b);
/* New lettering: white caps-style word with the shell's dark drop (2 units), cap height `cap` units */
void ps1k_word(const char *text, float x, float cy, float cap, int anchor, int b);
float ps1k_word_width(const char *text, float cap);
/* The ROM font line (info bar, the game box): glyph cell gw x gh units */
void ps1k_rom(const char *text, float x, float cy, int anchor, float gw, float gh);
float ps1k_rom_width(const char *text, float gw);
/* The CD Player's digits on a sphere: white, no drop */
void ps1k_digits(const char *text, float cx, float cy, float k, int b);
/* The flat plaque (62, 62, 140); semi = the 50 % info-bar blend */
void ps1k_plaque(float cx, float cy, float w, float h, bool semi, int b);
/* A title plate (the MAIN MENU plate re-made at any width), right edge at `right`, middle at cy */
void ps1k_plate(const char *text, float right, float cy, float cap);
void ps1k_plate_centred(const char *text, float cx, float cy, float cap);
/* A title plate that always shows the whole text: smaller down to half of `cap`, then two lines (plate no wider than
 * max_w units) */
void ps1k_plate_fit(const char *text, float right, float cy, float cap, float max_w);
/* A splatter label's paint */
void ps1k_splat(int seed, float cx, float cy, float w, float h, int b);
/* The arrow, its tip at (x, y); it jumps (the shell's cursor never glides) */
void ps1k_cursor(float x, float y);
/* The info bar: 50 % plaque between x0 and x1, the corner dots, left and right text */
void ps1k_info_bar(const char *left, const char *right, float x0, float x1, float y);
/* A small labelled sphere (L1, R1, SAVES, HRS...) */
void ps1k_label_sphere(const char *text, float cx, float cy, float size, float cap);
/* A picture (cover, preview) in a box of w x h units, centred; b dims it */
struct PlatTexture;
void ps1k_image(struct PlatTexture *tex, float cx, float cy, float w, float h, int b, bool square_crop);
/* The same at opacity `alpha` 0..1 (a cover fading in) */
void ps1k_image_a(struct PlatTexture *tex, float cx, float cy, float w, float h, int b, bool square_crop, float alpha);
/* An empty case slot: a dim square where a cover is still loading */
void ps1k_empty_slot(float cx, float cy, float w, float h, int b);

/* Hooks for the shelf v3 (docs/SHELF-HOOKS.md) */
void ps1_shelf_after_game(void);     /* ps1_shelf.inc: main.c, when the app starts again after a game */
void ps1_settings_game_scope(void);  /* ps1_settings.inc: Settings opened for the shelf's game edit its own settings */

/* ps1_pslogo.c: the PS1 theme's opening, drawn and played from the user's BIOS file (no emulation): the Sony screen,
 * then (with <data>/cache/pslogo.tmd) the PlayStation screen; the shelf draws it while it is active, a button skips it */
void ps1_opening_preload(void);     /* main.c, once the settings are read: its data and sound, on a thread */
bool ps1_opening_begin(int region); /* main.c, at start (region 0 Europe, 1 America, 2 Japan) */
bool ps1_pslogo_active(void);
void ps1_opening_retry(void);      /* the setup screen found a BIOS: read it again */
void ps1_pslogo_skip(void);
bool ps1_pslogo_frame(void);
void ps1_pslogo_scan_later(void);   /* the shelf, with its menu up: the optional disc read, on a thread */

/* ps1_bios.c: the ROM font read from the user's BIOS (false when there is none: the fallback face is used) */
bool ps1_bios_font_ready(void);
/* glyph mask for an ASCII character: 16 x 16 bits, row-major, bit 15 = left; advance in source pixels */
bool ps1_bios_glyph(int ch, uint16_t rows[16], int *advance);
void ps1_bios_font_reload(void); /* after the BIOS folder changes */

#endif
