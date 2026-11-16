/*
 * SwanStationPS5 - the PS1 theme's drawing kit: the original console's system screens, redrawn
 * (the ps1-console-shelf fork). Every shape is generated here; nothing is copied from the console.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Rules, from the console's own screens:
 *  - one radial indigo light (#7575EC in the middle, #000120 in the corners) and shaded spheres as the only ornament;
 *  - rainbow "splatter" plaques for the main choices, flat slate rectangles for the others, an outlined navy plate
 *    with the screen's name in the top-right corner; nothing has rounded corners, only spheres are round;
 *  - selection is a violet 3D arrow pointing up-left at the chosen thing, which animates; what can't be chosen is dimmed;
 *  - lists of games are memory-card blocks, names go in one mono bar at the bottom;
 *  - progress is numbers in spheres, coloured balls and the CD Player's 5x4 dot grid, never a thin bar;
 *  - Settings use the grey PS one dialect: a bevelled tile wall, coloured section plates, black slots, tiny plates.
 * All sizes are 1920x1080 screen pixels.
 */
#ifndef SwanStationPS5_PS1_H
#define SwanStationPS5_PS1_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct PlatTexture;

/* colours */
#define PS1_NAVY 0xff000120u      /* the corners of every blue screen */
#define PS1_INDIGO 0xff3a3a84u    /* mid indigo, the flat buttons */
#define PS1_LABEL 0xffdedfecu     /* label white (never pure white) */
#define PS1_SOFT 0xffdcdcf6u      /* secondary labels */
#define PS1_MUTED 0xffb8b8eeu     /* small print */
#define PS1_OUTLINE 0xff0c0c44u   /* the dark outline of plaque labels */
#define PS1_SHADOW 0xff0a0a3au    /* the drop under labels */

enum Ps1Ball
{
    PS1_BALL_BLUE, /* the console's spheres and number balls */
    PS1_BALL_GREEN,
    PS1_BALL_RED,
    PS1_BALL_GOLD,
    PS1_BALL_SPHERE, /* the big ornamental sphere (lit a little differently) */
    PS1_BALL_COUNT
};

enum Ps1Plate
{
    PS1_PLATE_GOLD,
    PS1_PLATE_RED,
    PS1_PLATE_BLUE,
    PS1_PLATE_GREEN,
    PS1_PLATE_VIOLET,
    PS1_PLATE_SLATE,
    PS1_PLATE_COUNT
};

/* Seconds since start, for the motion (the cursor's bob, the icon flicker, the disc). Advance once a frame. */
void ps1_tick(float dt);
float ps1_time(void);
/* The fade in from black after a change of screen: start it, and draw it once a frame over everything but notices. */
void ps1_fade_start(void);
void ps1_fade_draw(void);

/* The blue screen: the light and up to `count` spheres from a layout (0..3); fade (0..1) dims it to black. */
void ps1_background(int layout);
/* The grey PS one wall of bevelled tiles, lit from the top. */
void ps1_tiles(void);
void ps1_sphere(float x, float y, float d, float alpha); /* top-left x, y and diameter */
/* A ball with an optional label (numbers, %, MIN...) centred in it; `pulse` makes it breathe. */
void ps1_ball(enum Ps1Ball kind, float cx, float cy, float d, const char *label, float text_size, uint32_t text_argb,
              bool pulse, float alpha);

/* The screen's name in its outlined navy plate, in the top-right corner (right edge at `right`). */
void ps1_title_plate(const char *text, float right, float top);
/* A rainbow splatter plaque with its label; `seed` picks its pattern (the same seed always looks the same). */
void ps1_splat(float x, float y, float w, float h, int seed, const char *label, float text_size, bool dim);
/* shelf v3: smooth splatters (Settings > Library > Splatters); the kit sets it before it draws one */
void ps1_splat_smooth(bool on);
/* The same plaque in a list of choices: its label is white when it is the chosen one, grey otherwise. */
void ps1_splat_choice(float x, float y, float w, float h, int seed, const char *label, float text_size, bool chosen,
                      bool dim);
/* A flat slate button; w 0 sizes it to the label. Returns its width. */
float ps1_flat(float x, float y, float w, float h, const char *label, float text_size, bool selected, bool dim);
/* The violet arrow, its tip at (x, y), pointing up-left at what is chosen; it bobs. */
void ps1_cursor(float x, float y, float size);
/* The next arrows jump to their place instead of gliding there (call it when the screen changes). */
void ps1_cursor_snap(void);
/* The dark mono bar for a name: left text, and a smaller right one (either may be NULL). */
void ps1_monobar(float x, float y, float w, float h, const char *left, const char *right);
/* A memory-card block (the frame and the dark well) of side s; `selected` flickers it. Returns the flicker's brightness
 * (1 = as is) to tint the icon drawn in it. */
float ps1_block(float x, float y, float s, bool selected);
/* The pixel icon drawn for a game without a cover (a disc in a colour from the name), nearest-neighbour. */
void ps1_disc_icon(float x, float y, float s, uint32_t seed, float bright);
/* A PS1 disc, black side up, turning by `angle` radians. */
void ps1_disc(float cx, float cy, float d, float angle);
/* The CD Player's 5x4 dots as a meter: `lit` dots on (0..20), the next one a gold ball with `next_label`. */
void ps1_dot_grid(float x, float y, float col_gap, float row_gap, int lit, const char *next_label);

/* The PS one dialect (Settings) */
void ps1_gplate(float x, float y, float w, float h, enum Ps1Plate colour, const char *label, float text_size, bool dim);
void ps1_slot(float x, float y, float w, float h); /* a black recessed slot */
/* The tiny ON / OFF pair (the lit one red); returns the width used, ending at `right`. */
float ps1_onoff(float right, float y, bool on);
/* A meter of 10 blocks, `filled` gold. Returns its width. */
float ps1_meter(float x, float y, int filled, int total);

/* The button prompts along the bottom: glyphs in small navy balls, labels in caps.
 * glyph -1 ends a list. `grey`: the shadowed white of the grey screens. Returns the width used. */
/* Two buttons for one action (up/down, left/right), side by side, then the label. Returns the width. */
float ps1_prompt_pair(float x, float y, int first, int second, const char *label, float size);
float ps1_prompts(float x, float y, const int *glyphs, const char *const *labels, int count, float size, bool grey);
/* One prompt; returns its width. */
float ps1_prompt(float x, float y, int glyph, const char *label, float size, bool grey);
float ps1_prompt_width(int glyph, const char *label, float size);

/* Text helpers: caps (Latin-1 aware, into `out`), and text with letter spacing (em) for the mono bars. */
const char *ps1_caps(const char *s, char *out, size_t size);
void ps1_text_tracked(float x, float y, float size, int weight, uint32_t argb, int align, float tracking, const char *s);
float ps1_text_tracked_width(float size, int weight, float tracking, const char *s);
/* A label with the console's dark outline and drop (plaques, numbers). */
void ps1_text_outlined(float x, float y, float size, int weight, uint32_t argb, int align, const char *s, float outline);
/* A label with only the drop under it. */
void ps1_text_drop(float x, float y, float size, int weight, uint32_t argb, int align, const char *s, float drop);
/* Puts a label of `size` in the middle of a box height h starting at y: the y to give text_draw. */
float ps1_text_middle(float y, float h, float size);

/* A screen's name: the outlined plate top right in the PS1 theme, else the other themes' big title top left. */
void ps1_or_title(const char *text);

/* A small full-screen notice in the console's way, for screens that used to pop a box: title plate, the text in a mono
 * bar style block, and the prompts. */
void ps1_notice_screen(const char *title, const char *text, const int *glyphs, const char *const *labels, int count);

#endif
