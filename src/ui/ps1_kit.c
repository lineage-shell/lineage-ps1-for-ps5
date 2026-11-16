/*
 * SwanStationPS5 - the PS1 shelf v3 drawing kit (see ps1_kit.h).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Cheap by design: the background is one 16-slice Gouraud fan (as the shell draws it), the spheres one generated
 * texture per colour, the ROM font one small atlas; words are the interface font's own glyph atlas. Nothing is drawn
 * at a fixed resolution: positions are in 1080p interface units and the platform maps them to the display's pixels.
 */
#include "ps1_kit.h"
#include "ps1.h"

#include "draw.h"
#include "text.h"

#include "sfx.h"

#include "../app.h"
#include "../platform/platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint8_t PS1K_CD_ROWS[4][5] = {{PS1K_GOLD, PS1K_GOLD, PS1K_YELLOW, PS1K_YELLOW, PS1K_GREEN},
                                    {PS1K_GOLD, PS1K_GOLD, PS1K_YELLOW, PS1K_GREEN, PS1K_GREEN},
                                    {PS1K_RED, PS1K_RED, PS1K_YELLOW, PS1K_GREEN, PS1K_BLUE},
                                    {PS1K_PURPLE, PS1K_PURPLE, PS1K_BLUE, PS1K_BLUE, PS1K_BLUE}};

static uint32_t grey_b(uint32_t rgb, int b)
{
    /* the console's texture brightness: 128 = as is */
    if (b == 128)
        return 0xff000000u | rgb;
    uint32_t out = 0xff000000u;
    for (int s = 0; s < 24; s += 8)
    {
        int v = (int)((rgb >> s) & 0xff) * b / 128;
        out |= (uint32_t)(v > 255 ? 255 : v) << s;
    }
    return out;
}

/* ---------------------------------------------------------------- the background */

static PlatTexture *bg_tex;

/* The shell's background: a 16-slice Gouraud fan from the light blue middle (128, 128, 255) to the clear colour
 * (0, 0, 32) at the rim, widened to an ellipse for 16:9 (rx 387, ry 290 units). Each slice is linear, as the GPU drew
 * it; made once into a picture (a mesh this big is slow on the CPU renderer) and stretched smoothly to the screen. */
static PlatTexture *make_bg(void)
{
    enum { W = 960, H = 540, N = 16 };
    uint8_t *px = malloc((size_t)W * H * 4);
    if (!px)
        return NULL;
    const float cx = W * 0.5f, cy = H * 0.5f, rx = 387.0f * W / 854.0f, ry = 290.0f * H / 480.0f;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            /* in the circle's space the fan is regular: the slice, then how far across it toward its rim chord */
            const float u = (x + 0.5f - cx) / rx, v = (y + 0.5f - cy) / ry;
            float a = atan2f(v, u);
            if (a < 0)
                a += 6.2831853f;
            const int i = (int)(a / (6.2831853f / N)) % N;
            const float mid_a = (i + 0.5f) * 6.2831853f / N, chord = cosf(3.14159265f / N);
            const float t = (u * cosf(mid_a) + v * sinf(mid_a)) / chord; /* 0 middle, 1 on the rim's chord */
            const float k = t >= 1.0f ? 0.0f : 1.0f - t;
            uint8_t *p = &px[((size_t)y * W + x) * 4];
            p[0] = (uint8_t)(128.0f * k + 0.5f);
            p[1] = (uint8_t)(128.0f * k + 0.5f);
            p[2] = (uint8_t)(32.0f + 223.0f * k + 0.5f);
            p[3] = 255;
        }
    PlatTexture *t = plat_texture_create(px, W, H, true);
    free(px);
    return t;
}

bool ps1k_narrow(void)
{
    return app.global.shelf_shape == 1;
}

float ps1k_ox(float x)
{
    if (ps1k_narrow())
        return x;
    return x < 214.0f ? x - 64.0f : x > 426.0f ? x + 64.0f : x;
}

float ps1k_wx(float x)
{
    return ps1k_narrow() ? 320.0f + (x - 320.0f) * (640.0f / 854.0f) : x;
}

float ps1k_left(void)
{
    return ps1k_narrow() ? 0.0f : -107.0f;
}

float ps1k_right(void)
{
    return ps1k_narrow() ? 640.0f : 747.0f;
}

static void draw_bg(uint32_t argb)
{
    if (!bg_tex)
        bg_tex = make_bg();
    if (ps1k_narrow())
    {
        /* 4:3: the fan in the console's 640 x 480 (1440 x 1080), its ellipse a circle again; the sides dark */
        const uint32_t dark = argb & 0xff000000u;
        draw_rect(0, 0, 240, 1080, dark);
        draw_rect(1680, 0, 240, 1080, dark);
        if (bg_tex)
            plat_draw_texture(bg_tex, 240, 0, 1440, 1080, argb, (argb >> 24) != 0xff);
        else
            draw_rect(240, 0, 1440, 1080, (argb & 0xff000000u) | 0x000020u);
        return;
    }
    if (bg_tex)
        plat_draw_texture(bg_tex, 0, 0, 1920, 1080, argb, (argb >> 24) != 0xff);
    else
        draw_rect(0, 0, 1920, 1080, (argb & 0xff000000u) | 0x000020u);
}

void ps1k_background(void)
{
    draw_bg(0xffffffffu);
}

void ps1k_background_over(float alpha)
{
    if (alpha <= 0.0f)
        return;
    const uint32_t a = (uint32_t)(fminf(alpha, 1.0f) * 255.0f + 0.5f);
    draw_bg(a << 24 | 0xffffffu);
}

#define FADE_IN_TICKS 12
void ps1k_screen_fade(int screen_id)
{
    static int last_id = -1;
    static uint64_t last_us, start_us;
    const uint64_t now = plat_ticks_us();
    if (screen_id != last_id || now - last_us > 250000)
        start_us = now;
    last_id = screen_id, last_us = now;
    const float t = (float)(now - start_us) / (FADE_IN_TICKS * 20000.0f);
    if (t < 1.0f)
        ps1k_background_over(1.0f - t);
}

static bool intro_played;
void ps1k_intro_played(void)
{
    intro_played = true;
}

bool ps1k_was_intro(void)
{
    return intro_played;
}

static uint32_t nav_fresh;
void ps1k_nav_fresh(uint32_t fresh)
{
    nav_fresh = fresh;
}

void ps1k_move_sound(bool moved, uint32_t pressed, uint32_t dirs)
{
    (void)pressed, (void)dirs;
    if (moved)
        sfx_play(SFX_CLICK); /* at an edge: silence, always (the console makes no bump sound) */
}

/* ---------------------------------------------------------------- spheres */

static PlatTexture *sphere_tex[PS1K_COLOURS];

static PlatTexture *make_sphere(int colour)
{
    enum { N = 256 };
    static const float BASE[PS1K_COLOURS][3] = {
        {128, 128, 255}, {0, 255, 140}, {255, 0, 0}, {255, 204, 0}, {255, 255, 0}, {160, 24, 255}, {120, 120, 240}};
    const float *c = BASE[colour];
    /* The console's spheres (CD Player and main menu captures, PAL): lit almost from the front, a little from the top
     * left, so the shading darkens all the way round to the rim (about 0.27 of the colour) from a broad centre (about
     * 0.84), with no hard shadow side. k = 0.27 + 0.57 * max(n.L, 0)^3.1, L = (-0.10, -0.12, 0.99). */
    const float ll = sqrtf(0.10f * 0.10f + 0.12f * 0.12f + 0.99f * 0.99f);
    const float lx = -0.10f / ll, ly = -0.12f / ll, lz = 0.99f / ll;
    uint8_t *px = malloc((size_t)N * N * 4);
    if (!px)
        return NULL;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
            const float u = (x + 0.5f) / N * 2 - 1, w = (y + 0.5f) / N * 2 - 1, r2 = u * u + w * w;
            uint8_t *p = &px[((size_t)y * N + x) * 4];
            const float r = sqrtf(r2), edge = (1.0f - r) * N * 0.5f + 0.5f;
            /* outside the disc: the rim's colour at alpha 0, so the smaller mip levels don't darken toward black */
            const float rr = r > 1.0f ? 1.0f : r, nu = r > 0 ? u / r * rr : 0, nw = r > 0 ? w / r * rr : 0;
            const float nz = sqrtf(fmaxf(0.0f, 1.0f - nu * nu - nw * nw)), d = fmaxf(0.0f, nu * lx + nw * ly + nz * lz);
            const float k = 0.27f + 0.57f * powf(d, 3.1f);
            for (int i = 0; i < 3; ++i)
            {
                const float v = c[i] * k;
                p[i] = (uint8_t)(v > 255 ? 255 : v);
            }
            p[3] = (uint8_t)(edge <= 0.0f ? 0 : edge >= 1.0f ? 255 : edge * 255.0f);
        }
    PlatTexture *t = plat_texture_create_mips(px, N, N);
    free(px);
    return t;
}

void ps1k_sphere(int colour, float cx, float cy, float size, int b)
{
    if (colour < 0 || colour >= PS1K_COLOURS)
        colour = PS1K_BLUE;
    if (!sphere_tex[colour])
        sphere_tex[colour] = make_sphere(colour);
    const float s = size * PS1K_S;
    if (sphere_tex[colour])
        plat_draw_texture(sphere_tex[colour], PS1K_X(cx) - s * 0.5f, PS1K_Y(cy) - s * 0.5f, s, s, grey_b(0xffffff, b),
                          true);
}

/* ---------------------------------------------------------------- lettering */

/* Asap Black: cap height 0.611 of the line box, ascent 0.815 */
#define CAP_K 0.611f
#define ASC_K 0.815f

float ps1k_word_width(const char *text, float cap)
{
    return text_width(cap * PS1K_S / CAP_K, FONT_PS1, text) / PS1K_S;
}

void ps1k_word(const char *text, float x, float cy, float cap, int anchor, int b)
{
    if (!text || !*text)
        return;
    const float size = cap * PS1K_S / CAP_K, top = PS1K_Y(cy) + cap * PS1K_S * 0.5f - ASC_K * size;
    const int align = anchor == PS1K_LEFT ? ALIGN_LEFT : anchor == PS1K_RIGHT ? ALIGN_RIGHT : ALIGN_CENTER;
    const float drop = 2.0f * PS1K_S * (cap / 20.0f < 0.6f ? 0.6f : cap / 20.0f);
    text_draw(PS1K_X(x) + drop, top + drop, size, FONT_PS1, grey_b(0x181818, b), align, text);
    text_draw(PS1K_X(x), top, size, FONT_PS1, grey_b(0xffffff, b), align, text);
}

void ps1k_digits(const char *text, float cx, float cy, float k, int b)
{
    const float cap = 17.0f * k, size = cap * PS1K_S / CAP_K, top = PS1K_Y(cy) + cap * PS1K_S * 0.5f - ASC_K * size;
    ps1_text_outlined(PS1K_X(cx), top, size, FONT_PS1, grey_b(0xffffff, b), ALIGN_CENTER, text, size * 0.035f);
}

/* ---------------------------------------------------------------- the ROM font */

static PlatTexture *rom_atlas;
static bool rom_tried;
static uint8_t rom_adv[95];

static bool rom_ready(void)
{
    if (rom_tried)
        return rom_atlas != NULL;
    rom_tried = true;
    if (!ps1_bios_font_ready())
        return false;
    /* 95 glyphs of 17 x 16 (the widened column) in a 16 x 6 grid */
    enum { CW = 17, CH = 16, W = CW * 16, H = CH * 6 };
    uint8_t *px = calloc((size_t)W * H, 4);
    if (!px)
        return false;
    for (int c = 0; c < 95; ++c)
    {
        uint16_t rows[16];
        int adv = 8;
        ps1_bios_glyph(c + 32, rows, &adv);
        rom_adv[c] = (uint8_t)adv;
        const int ox = (c % 16) * CW, oy = (c / 16) * CH;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                if (rows[y] & (0x8000u >> x))
                {
                    uint8_t *p = &px[((size_t)(oy + y) * W + ox + x) * 4];
                    p[0] = p[1] = p[2] = p[3] = 255;
                }
    }
    rom_atlas = plat_texture_create(px, W, H, false);
    free(px);
    return rom_atlas != NULL;
}

static const char *rom_ascii(const char *text, char *out, size_t size);

float ps1k_rom_width(const char *text, float gw)
{
    char ascii[256];
    text = rom_ascii(text, ascii, sizeof(ascii));
    if (!rom_ready())
        return ps1_text_tracked_width(gw * PS1K_S * 1.25f, FONT_TECH, 0.12f, text) / PS1K_S;
    float w = 0;
    for (const unsigned char *s = (const unsigned char *)text; *s; ++s)
        w += (*s >= 32 && *s < 127 ? rom_adv[*s - 32] : 8);
    return (w + 1) * gw / 16.0f;
}

/* the ROM font is ASCII: a multiplication sign becomes x, other non-ASCII letters are left out */
static const char *rom_ascii(const char *text, char *out, size_t size)
{
    size_t n = 0;
    for (const unsigned char *s = (const unsigned char *)text; *s && n + 1 < size; ++s)
    {
        if (*s < 0x80)
            out[n++] = (char)*s;
        else if (s[0] == 0xc3 && s[1] == 0x97)
            out[n++] = 'x', ++s;
    }
    out[n] = '\0';
    return out;
}

void ps1k_rom(const char *text, float x, float cy, int anchor, float gw, float gh)
{
    if (!text || !*text)
        return;
    char ascii[256];
    text = rom_ascii(text, ascii, sizeof(ascii));
    const float w = ps1k_rom_width(text, gw);
    float left = anchor == PS1K_LEFT ? x : anchor == PS1K_RIGHT ? x - w : x - w * 0.5f;
    if (!rom_ready())
    {
        /* no BIOS: a fixed-pitch face in the same place */
        const float size = gw * PS1K_S * 1.25f;
        ps1_text_tracked(PS1K_X(left), PS1K_Y(cy) - size * 0.52f, size, FONT_TECH, 0xffffffffu, ALIGN_LEFT, 0.12f,
                         text);
        return;
    }
    const float kx = gw / 16.0f * PS1K_S, ky = gh / 16.0f * PS1K_S, top = PS1K_Y(cy) - 8.0f * ky;
    float pen = PS1K_X(left);
    for (const unsigned char *s = (const unsigned char *)text; *s; ++s)
    {
        const int c = *s >= 32 && *s < 127 ? *s - 32 : 0;
        if (c)
            plat_draw_texture_region(rom_atlas, (c % 16) * 17, (c / 16) * 16, 17, 16, pen, top, 17 * kx, 16 * ky,
                                     0xffffffffu);
        pen += rom_adv[c] * kx;
    }
}

/* ---------------------------------------------------------------- plaques, plates, splatters */

void ps1k_plaque(float cx, float cy, float w, float h, bool semi, int b)
{
    const uint32_t c = grey_b(0x3e3e8c, b);
    draw_rect(PS1K_X(cx - w * 0.5f), PS1K_Y(cy - h * 0.5f), w * PS1K_S, h * PS1K_S, semi ? (c & 0xffffffu) | 0x80000000u : c);
}

/* the plate's box (rim, dark gap, banded glow) at x0, y0 (pixels), W x H */
static void plate_box(float x0, float y0, float W, float H)
{
    const float r = 2.0f * PS1K_S;
    draw_rect(x0, y0, W, H, 0xff394ae5u);                       /* the rim */
    draw_rect(x0 + r, y0 + r, W - 2 * r, H - 2 * r, 0xff000010u); /* the dark gap */
    /* the banded glow: brighter in the middle, a circle cut by the plate */
    enum { GX = 24, GY = 4 };
    PlatVertex v[GX * GY * 6];
    int k = 0;
    const float ix = x0 + 2 * r, iy = y0 + 2 * r, iw = W - 4 * r, ih = H - 4 * r, mx = x0 + W * 0.5f, my = y0 + H * 0.5f;
    const float reach = fmaxf(iw * 0.5f, ih * 1.4f);
    uint32_t col[GX + 1][GY + 1];
    float px[GX + 1], py[GY + 1];
    for (int i = 0; i <= GX; ++i)
        px[i] = ix + iw * i / GX;
    for (int j = 0; j <= GY; ++j)
        py[j] = iy + ih * j / GY;
    for (int i = 0; i <= GX; ++i)
        for (int j = 0; j <= GY; ++j)
        {
            const float d = fminf(1.0f, hypotf(px[i] - mx, (py[j] - my) * 1.0f) / reach);
            const float t = 1.0f - d;
            col[i][j] = 0xff000000u | (uint32_t)(4 + 46 * t) << 16 | (uint32_t)(4 + 62 * t) << 8 | (uint32_t)(40 + 168 * t);
        }
    for (int i = 0; i < GX; ++i)
        for (int j = 0; j < GY; ++j)
        {
            PlatVertex a = {px[i], py[j], 0, 0, col[i][j]}, b = {px[i + 1], py[j], 0, 0, col[i + 1][j]},
                       c = {px[i + 1], py[j + 1], 0, 0, col[i + 1][j + 1]}, d = {px[i], py[j + 1], 0, 0, col[i][j + 1]};
            v[k++] = a, v[k++] = b, v[k++] = c, v[k++] = a, v[k++] = c, v[k++] = d;
        }
    plat_draw_mesh(NULL, v, k, NULL, 0);
}

/* plate text has no drop */
static void plate_text(const char *text, float cx_px, float cy, float cap)
{
    const float size = cap * PS1K_S / CAP_K, top = PS1K_Y(cy) + cap * PS1K_S * 0.5f - ASC_K * size + PS1K_S;
    text_draw(cx_px, top, size, FONT_PS1, 0xffffffffu, ALIGN_CENTER, text);
}

void ps1k_plate(const char *text, float right, float cy, float cap)
{
    const float h = 47.0f, w = fmaxf(172.0f, ps1k_word_width(text, cap) + 36.0f);
    const float x0 = PS1K_X(right - w), W = w * PS1K_S;
    plate_box(x0, PS1K_Y(cy - h * 0.5f), W, h * PS1K_S);
    plate_text(text, x0 + W * 0.5f, cy, cap);
}

void ps1k_plate_fit(const char *text, float right, float cy, float cap, float max_w)
{
    /* one line, as small as half the size */
    for (float c = cap; c >= cap * 0.5f - 0.01f; c -= 0.5f)
        if (ps1k_word_width(text, c) + 36.0f <= max_w)
        {
            ps1k_plate(text, right, cy, c);
            return;
        }
    /* two lines, broken at the space that makes the longer line shortest, at the biggest size that fits */
    char a[256], b[256];
    str_copy(a, sizeof(a), text);
    b[0] = '\0';
    float best = 1e9f;
    for (const char *sp = strchr(text, ' '); sp; sp = strchr(sp + 1, ' '))
    {
        char l1[256];
        snprintf(l1, sizeof(l1), "%.*s", (int)(sp - text), text);
        const float m = fmaxf(ps1k_word_width(l1, cap), ps1k_word_width(sp + 1, cap));
        if (m < best)
            best = m, str_copy(a, sizeof(a), l1), str_copy(b, sizeof(b), sp + 1);
    }
    float c = cap;
    while (c > cap * 0.5f && fmaxf(ps1k_word_width(a, c), ps1k_word_width(b, c)) + 36.0f > max_w)
        c -= 0.5f;
    const float lh = c * 1.5f, h = fmaxf(47.0f, 2 * lh + 18.0f);
    const float w = fmaxf(172.0f, fminf(max_w, fmaxf(ps1k_word_width(a, c), ps1k_word_width(b, c)) + 36.0f));
    const float x0 = PS1K_X(right - w), W = w * PS1K_S;
    plate_box(x0, PS1K_Y(cy - h * 0.5f), W, h * PS1K_S);
    plate_text(a, x0 + W * 0.5f, cy - lh * 0.5f, c);
    plate_text(b, x0 + W * 0.5f, cy + lh * 0.5f, c);
}

void ps1k_plate_centred(const char *text, float cx, float cy, float cap)
{
    const float w = fmaxf(172.0f, ps1k_word_width(text, cap) + 36.0f);
    ps1k_plate(text, cx + w * 0.5f, cy, cap);
}

void ps1k_splat(int seed, float cx, float cy, float w, float h, int b)
{
    ps1_splat_smooth(app.global.shelf_splat == 1); /* Settings > Library > Splatters */
    ps1_splat(PS1K_X(cx - w * 0.5f), PS1K_Y(cy - h * 0.5f), w * PS1K_S, h * PS1K_S, seed, NULL, 0, b < 100);
}

/* ---------------------------------------------------------------- the arrow */

void ps1k_cursor(float x, float y)
{
    /* 42 x 32 units, tip at the top left; the shell's magenta, lit from the top */
    const float k = PS1K_S * 42.0f / 70.0f, ox = PS1K_X(x), oy = PS1K_Y(y);
#define P(px_, py_) ox + (px_) * k, oy + (py_) * k
    PlatVertex v[12];
    const uint32_t sh = 0x90101018u;
    const float s = 2.0f * k;
    v[0] = (PlatVertex){ox + s, oy + s, 0, 0, sh};
    v[1] = (PlatVertex){ox + 70 * k + s, oy + 30 * k + s, 0, 0, sh};
    v[2] = (PlatVertex){ox + 30 * k + s, oy + 52 * k + s, 0, 0, sh};
    plat_draw_mesh(NULL, v, 3, NULL, 0);
    const PlatVertex a = {P(0, 0), 0, 0, 0}, b = {P(70, 30), 0, 0, 0}, c = {P(36, 30), 0, 0, 0}, d = {P(30, 52), 0, 0, 0};
    v[0] = a, v[1] = b, v[2] = c;
    v[0].argb = v[1].argb = v[2].argb = 0xffffb0f8u;
    v[3] = a, v[4] = c, v[5] = d;
    v[3].argb = v[4].argb = v[5].argb = 0xffea41f7u;
    v[6] = c, v[7] = b, v[8] = d;
    v[6].argb = v[7].argb = v[8].argb = 0xffb031beu;
#undef P
    plat_draw_mesh(NULL, v, 9, NULL, 0);
}

/* ---------------------------------------------------------------- the info bar, labels, pictures */

void ps1k_info_bar(const char *left, const char *right, float x0, float x1, float y)
{
    ps1k_plaque((x0 + x1) * 0.5f, y, x1 - x0, 32, true, 128);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            ps1k_sphere(PS1K_BLUE, i ? x1 : x0, y + (j ? 16 : -16), 16, 128);
    char rbuf[160];
    if (right && *right)
    {
        /* a long right text (a folder) keeps its end */
        const char *r = right;
        while (strlen(r) > 4 && ps1k_rom_width(r, 16) > (x1 - x0) * 0.6f)
            ++r;
        snprintf(rbuf, sizeof(rbuf), "%s%s", r != right ? ".." : "", r);
        right = rbuf;
    }
    const float rw = right && *right ? ps1k_rom_width(right, 16) : 0;
    if (left && *left)
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "%s", left);
        /* too long: cut with a full stop, as the mockups do */
        size_t n = strlen(buf);
        while (n > 2 && ps1k_rom_width(buf, 16) > x1 - x0 - 48 - rw)
        {
            buf[n - 2] = '.';
            buf[n - 1] = '\0';
            --n;
        }
        ps1k_rom(buf, x0 + 16, y, PS1K_LEFT, 16, 22);
    }
    if (rw > 0)
        ps1k_rom(right, x1 - 16, y, PS1K_RIGHT, 16, 22);
}

void ps1k_label_sphere(const char *text, float cx, float cy, float size, float cap)
{
    ps1k_sphere(PS1K_BLUE, cx, cy, size, 128);
    ps1k_word(text, cx, cy, cap, PS1K_CENTRE, 128);
}

void ps1k_image(PlatTexture *tex, float cx, float cy, float w, float h, int b, bool square_crop)
{
    ps1k_image_a(tex, cx, cy, w, h, b, square_crop, 1.0f);
}

void ps1k_empty_slot(float cx, float cy, float w, float h, int b)
{
    /* a dark, slightly blue square, as faint as the lattice behind it allows */
    const uint32_t a = (uint32_t)(0x50 * b / 128);
    plat_fill_rectf(PS1K_X(cx - w * 0.5f), PS1K_Y(cy - h * 0.5f), w * PS1K_S, h * PS1K_S, a << 24 | 0x0a0e24u);
}

void ps1k_image_a(PlatTexture *tex, float cx, float cy, float w, float h, int b, bool square_crop, float alpha)
{
    if (alpha <= 0.0f)
        return;
    if (!tex)
        return;
    int tw = 1, th = 1;
    plat_texture_size(tex, &tw, &th);
    int sx = 0, sy = 0, sw = tw, sh = th;
    if (square_crop && tw != th)
    {
        const int side = tw < th ? tw : th;
        sx = (tw - side) / 2, sy = (th - side) / 2, sw = sh = side;
    }
    plat_draw_texture_region(tex, sx, sy, sw, sh, PS1K_X(cx - w * 0.5f), PS1K_Y(cy - h * 0.5f), w * PS1K_S, h * PS1K_S,
                             (grey_b(0xffffff, b) & 0xffffffu) | (uint32_t)(alpha >= 1.0f ? 255 : alpha * 255) << 24);
}
