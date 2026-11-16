/*
 * SwanStationPS5 - the PS1 theme's drawing kit (see ps1.h).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The pictures (the light, the spheres, the splatter, the tile wall, the disc) are generated once, the first time
 * they are drawn, and kept: a frame only blits them. The splatter plaques are drawn at a fifth of their size and
 * blown up without smoothing, like the console's own 320-line screens; the light and the spheres are smooth
 * gradients, as the console's Gouraud shading was.
 */
#include "ps1.h"

#include "draw.h"
#include "icons.h"
#include "text.h"
#include "theme.h"

#include "../platform/platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clock_s, last_dt;
static unsigned frame_no;

void ps1_tick(float dt)
{
    if (dt > 0.0f)
        ++frame_no, last_dt = dt > 0.1f ? 0.1f : dt;
    clock_s += dt > 0.1f ? 0.1f : dt;
    if (clock_s > 3600.0f)
        clock_s -= 3600.0f;
}

float ps1_time(void)
{
    return clock_s;
}

/* ---------------------------------------------------------------- the fade between screens */

/* Every change of screen comes up out of black, as the console's menus did: fast at first, then easing in. */
#define PS1_FADE_TIME 0.3f
static float fade_left;

void ps1_fade_start(void)
{
    fade_left = PS1_FADE_TIME;
}

void ps1_fade_draw(void)
{
    if (fade_left <= 0.0f)
        return;
    const float t = 1.0f - fade_left / PS1_FADE_TIME, a = (1.0f - t) * (1.0f - t);
    draw_rect(0, 0, (float)plat_width(), (float)plat_height(), ((uint32_t)(a * 255.0f + 0.5f) << 24));
    fade_left -= last_dt;
}

/* ---------------------------------------------------------------- small helpers */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static uint8_t u8(float v)
{
    return (uint8_t)clampf(v * 255.0f + 0.5f, 0.0f, 255.0f);
}

/* colour stops: position 0..1 and 0xRRGGBB */
typedef struct
{
    float at;
    uint32_t rgb;
} Stop;

static void stops_eval(const Stop *s, int n, float t, float out[3])
{
    if (t <= s[0].at)
        t = s[0].at;
    int i = 0;
    while (i + 1 < n - 1 && t > s[i + 1].at)
        ++i;
    float a = s[i].at, b = s[i + 1].at, k = b > a ? clampf((t - a) / (b - a), 0.0f, 1.0f) : 1.0f;
    for (int c = 0; c < 3; ++c)
    {
        float ca = (float)((s[i].rgb >> (16 - 8 * c)) & 0xff), cb = (float)((s[i + 1].rgb >> (16 - 8 * c)) & 0xff);
        out[c] = (ca + (cb - ca) * k) / 255.0f;
    }
}

static uint32_t with_alpha(uint32_t argb, float a)
{
    return argb_alpha(argb, a);
}

static void tri(PlatVertex *v, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c)
{
    v[0] = (PlatVertex){x0, y0, 0, 0, c};
    v[1] = (PlatVertex){x1, y1, 0, 0, c};
    v[2] = (PlatVertex){x2, y2, 0, 0, c};
}

/* mulberry32, as the concept's generator: the same seed gives the same plaque */
typedef struct
{
    uint32_t s;
} Rng;

static float rng_next(Rng *r)
{
    r->s += 0x6D2B79F5u;
    uint32_t t = r->s;
    t = (t ^ (t >> 15)) * (1u | t);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((t ^ (t >> 14)) >> 0) / 4294967296.0f;
}

/* ---------------------------------------------------------------- the light */

static PlatTexture *bg_tex;

static PlatTexture *make_background(void)
{
    enum { W = 960, H = 540 };
    uint8_t *px = malloc((size_t)W * H * 4);
    if (!px)
        return NULL;
    /* radial-gradient(ellipse 56% 74% at 55% 48%, #8282fa 0, #6464d8 15%, #4848a8 34%, #2e2e78 56%, #16164c 77%, #000120) */
    static const Stop S[] = {{0.0f, 0x8282fa}, {0.15f, 0x6464d8}, {0.34f, 0x4848a8},
                             {0.56f, 0x2e2e78}, {0.77f, 0x16164c}, {1.0f, 0x000120}};
    uint32_t noise = 0x9e3779b9u;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            float dx = ((x + 0.5f) / W - 0.55f) / 0.56f, dy = ((y + 0.5f) / H - 0.48f) / 0.74f;
            float d = sqrtf(dx * dx + dy * dy);
            float c[3];
            stops_eval(S, 6, d, c);
            noise = noise * 1664525u + 1013904223u;
            float dither = ((float)(noise >> 24) / 255.0f - 0.5f) / 255.0f; /* no bands in the slow fall */
            uint8_t *p = &px[((size_t)y * W + x) * 4];
            p[0] = u8(c[0] + dither);
            p[1] = u8(c[1] + dither);
            p[2] = u8(c[2] + dither);
            p[3] = 255;
        }
    PlatTexture *t = plat_texture_create(px, W, H, true);
    free(px);
    return t;
}

/* sphere pairs, big and small, in opposite corners (x, y, diameter, alpha) */
static const float LAYOUTS[][4][4] = {
    /* 0: the library */
    {{40, -30, 230, 1}, {240, 150, 120, 1}, {1610, 560, 250, 1}, {1720, 820, 110, 1}},
    /* 1: a game's page */
    {{1660, -60, 220, 1}, {1560, 110, 96, 1}, {-60, 860, 240, 1}, {0, 0, 0, 0}},
    /* 2: the disc install */
    {{1740, 430, 130, 1}, {-80, -90, 200, 0.7f}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    /* 3: the game menu */
    {{1700, 860, 200, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    /* 4: none */
    {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    /* 5: notices and small screens: one pair */
    {{1650, 780, 210, 1}, {1560, 960, 90, 1}, {-70, -60, 180, 0.8f}, {0, 0, 0, 0}},
};

void ps1_background(int layout)
{
    if (!bg_tex)
        bg_tex = make_background();
    if (bg_tex)
        plat_draw_texture(bg_tex, 0, 0, (float)plat_width(), (float)plat_height(), 0xffffffffu, false);
    else
        draw_rect(0, 0, (float)plat_width(), (float)plat_height(), 0xff2e2e78u);
    if (layout < 0 || layout >= (int)(sizeof(LAYOUTS) / sizeof(LAYOUTS[0])))
        return;
    for (int i = 0; i < 4; ++i)
        if (LAYOUTS[layout][i][2] > 0)
            ps1_sphere(LAYOUTS[layout][i][0], LAYOUTS[layout][i][1], LAYOUTS[layout][i][2], LAYOUTS[layout][i][3]);
}

/* ---------------------------------------------------------------- spheres and balls */

static PlatTexture *ball_tex[PS1_BALL_COUNT];
static PlatTexture *shadow_tex;

static PlatTexture *make_ball(int kind)
{
    enum { N = 192 };
    /* radial-gradient(circle at 33..34% 28%, ...) to the farthest corner, cut to the circle */
    static const Stop BLUE[] = {{0, 0xdadcf6}, {0.14f, 0x8a8ee4}, {0.50f, 0x4c4da6}, {1, 0x1a1a50}};
    static const Stop GREEN[] = {{0, 0xb8ffd8}, {0.18f, 0x2ee08c}, {0.52f, 0x05a95a}, {1, 0x02522a}};
    static const Stop RED[] = {{0, 0xffc0c0}, {0.18f, 0xff3b3b}, {0.52f, 0xc20101}, {1, 0x560000}};
    static const Stop GOLD[] = {{0, 0xfff5c0}, {0.18f, 0xffd92e}, {0.52f, 0xd9a805}, {1, 0x5e4400}};
    static const Stop SPHERE[] = {{0, 0xdadcf6}, {0.08f, 0x9094e8}, {0.28f, 0x5a5cbe},
                                  {0.56f, 0x37388e}, {0.82f, 0x1e1e5c}, {1, 0x11113c}};
    const Stop *s = kind == PS1_BALL_GREEN ? GREEN : kind == PS1_BALL_RED ? RED : kind == PS1_BALL_GOLD ? GOLD
                    : kind == PS1_BALL_SPHERE ? SPHERE : BLUE;
    const int n = kind == PS1_BALL_SPHERE ? 6 : 4;
    const float hx = kind == PS1_BALL_SPHERE ? 0.33f : 0.34f, hy = 0.28f;
    const float far = sqrtf((1 - hx) * (1 - hx) + (1 - hy) * (1 - hy));
    uint8_t *px = malloc(N * N * 4);
    if (!px)
        return NULL;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
            float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            float d = sqrtf((u - hx) * (u - hx) + (v - hy) * (v - hy)) / far;
            float c[3];
            stops_eval(s, n, d, c);
            float r = sqrtf((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * N; /* from the middle, px */
            float a = clampf(N * 0.5f - r + 0.5f, 0.0f, 1.0f);
            uint8_t *p = &px[(y * N + x) * 4];
            p[0] = u8(c[0]);
            p[1] = u8(c[1]);
            p[2] = u8(c[2]);
            p[3] = u8(a);
        }
    PlatTexture *t = plat_texture_create(px, N, N, true);
    free(px);
    return t;
}

static PlatTexture *make_shadow(void)
{
    enum { N = 128 };
    uint8_t *px = malloc(N * N * 4);
    if (!px)
        return NULL;
    /* a disc of half the texture, blurred over the rest */
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
            float r = sqrtf((x + 0.5f - N * 0.5f) * (x + 0.5f - N * 0.5f) + (y + 0.5f - N * 0.5f) * (y + 0.5f - N * 0.5f)) /
                      (N * 0.5f);
            float a = r < 0.5f ? 1.0f : expf(-(r - 0.5f) * (r - 0.5f) / 0.045f);
            uint8_t *p = &px[(y * N + x) * 4];
            p[0] = 0;
            p[1] = 0;
            p[2] = 24;
            p[3] = u8(a * (r < 1.0f ? 1.0f : 0.0f));
        }
    PlatTexture *t = plat_texture_create(px, N, N, true);
    free(px);
    return t;
}

/* The soft shadow under a sphere of diameter d at (x, y): the texture's inner disc is half its width, so it is drawn
 * at twice the size, moved by the offset (box-shadow 10px 16px for a 230 px sphere). */
static void shadow_at(float x, float y, float d, float ox, float oy, float alpha)
{
    if (!shadow_tex)
        shadow_tex = make_shadow();
    if (shadow_tex)
        plat_draw_texture_region(shadow_tex, 0, 0, 128, 128, x - d * 0.5f + ox, y - d * 0.5f + oy, d * 2, d * 2,
                                 with_alpha(0xffffffffu, alpha));
}

static void ball_shadow(float x, float y, float d, float alpha)
{
    shadow_at(x, y, d, d * 10.0f / 230.0f, d * 16.0f / 230.0f, 0.5f * alpha);
}

static PlatTexture *ball(int kind)
{
    if (!ball_tex[kind])
        ball_tex[kind] = make_ball(kind);
    return ball_tex[kind];
}

void ps1_sphere(float x, float y, float d, float alpha)
{
    ball_shadow(x, y, d, alpha);
    PlatTexture *t = ball(PS1_BALL_SPHERE);
    if (t)
        plat_draw_texture_region(t, 0, 0, 192, 192, x, y, d, d, with_alpha(0xffffffffu, alpha));
}

void ps1_ball(enum Ps1Ball kind, float cx, float cy, float d, const char *label, float text_size, uint32_t text_argb,
              bool pulse, float alpha)
{
    if (pulse)
        d *= 1.0f + 0.12f * (0.5f - 0.5f * cosf(ps1_time() * 6.2831853f)); /* 1 s, 1 -> 1.12 -> 1 */
    const float x = cx - d * 0.5f, y = cy - d * 0.5f;
    shadow_at(x, y, d, d * 6.0f / 104.0f, d * 10.0f / 104.0f, 0.5f * alpha); /* .ball: 6px 10px 18px */
    PlatTexture *t = ball(kind);
    if (t)
        plat_draw_texture_region(t, 0, 0, 192, 192, x, y, d, d, with_alpha(0xffffffffu, alpha));
    if (label && *label)
    {
        const bool light = (text_argb & 0xffffff) > 0x808080;
        if (light)
            ps1_text_drop(cx, ps1_text_middle(y, d, text_size), text_size, FONT_BOLD, with_alpha(text_argb, alpha),
                          ALIGN_CENTER, label, text_size * 0.05f);
        else
            text_draw(cx, ps1_text_middle(y, d, text_size), text_size, FONT_BOLD, with_alpha(text_argb, alpha), ALIGN_CENTER,
                      label);
    }
}

/* ---------------------------------------------------------------- text */

const char *ps1_caps(const char *s, char *out, size_t size)
{
    size_t n = 0;
    if (!size)
        return out;
    for (const unsigned char *p = (const unsigned char *)s; *p && n + 1 < size; ++p)
    {
        unsigned char c = *p;
        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - 32);
        else if (c == 0xc3 && p[1] >= 0xa0 && p[1] <= 0xbe && p[1] != 0xb7 && n + 2 < size)
        {
            /* Latin-1 small letters in UTF-8 (C3 A0..BE) to capitals (C3 80..9E) */
            out[n++] = (char)c;
            out[n++] = (char)(p[1] - 0x20);
            ++p;
            continue;
        }
        out[n++] = (char)c;
    }
    out[n] = '\0';
    return out;
}

/* the bytes of the character at s, never past the end of the string (a cut sequence counts as what is left) */
static int utf8_len(const char *s)
{
    unsigned char c = (unsigned char)*s;
    int n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
    for (int k = 1; k < n; ++k)
        if (!s[k])
            return k;
    return n;
}

float ps1_text_tracked_width(float size, int weight, float tracking, const char *s)
{
    float w = 0;
    int chars = 0;
    char one[8];
    while (*s)
    {
        int n = utf8_len(s);
        memcpy(one, s, (size_t)n);
        one[n] = '\0';
        w += text_width(size, weight, one);
        ++chars;
        s += n;
    }
    return w + (chars > 1 ? (chars - 1) * tracking * size : 0.0f);
}

void ps1_text_tracked(float x, float y, float size, int weight, uint32_t argb, int align, float tracking, const char *s)
{
    if (align != ALIGN_LEFT)
    {
        float w = ps1_text_tracked_width(size, weight, tracking, s);
        x -= align == ALIGN_CENTER ? w * 0.5f : w;
    }
    char one[8];
    while (*s)
    {
        int n = utf8_len(s);
        memcpy(one, s, (size_t)n);
        one[n] = '\0';
        if (one[0] != ' ')
            text_draw(x, y, size, weight, argb, ALIGN_LEFT, one);
        x += text_width(size, weight, one) + tracking * size;
        s += n;
    }
}

void ps1_text_outlined(float x, float y, float size, int weight, uint32_t argb, int align, const char *s, float outline)
{
    const float a = (float)(argb >> 24) / 255.0f;
    const uint32_t dark = with_alpha(PS1_OUTLINE, a);
    text_draw(x, y + outline * 2.0f, size, weight, with_alpha(0xff00001eu, 0.6f * a), align, s); /* the soft drop */
    text_draw(x + outline * 1.5f, y + outline * 1.5f, size, weight, dark, align, s);
    text_draw(x - outline, y - outline, size, weight, dark, align, s);
    text_draw(x + outline, y - outline, size, weight, dark, align, s);
    text_draw(x - outline, y + outline, size, weight, dark, align, s);
    text_draw(x, y, size, weight, argb, align, s);
}

void ps1_text_drop(float x, float y, float size, int weight, uint32_t argb, int align, const char *s, float drop)
{
    const float a = (float)(argb >> 24) / 255.0f;
    text_draw(x, y + drop, size, weight, with_alpha(PS1_SHADOW, a), align, s);
    text_draw(x, y, size, weight, argb, align, s);
}

float ps1_text_middle(float y, float h, float size)
{
    /* text_draw's y is the top of the line box; the capitals sit a little low in it */
    return y + (h - size) * 0.5f - size * 0.06f;
}

/* ---------------------------------------------------------------- plates and buttons */

void ps1_title_plate(const char *text, float right, float top)
{
    char caps[96];
    ps1_caps(text, caps, sizeof(caps));
    const float size = 40, track = 0.03f;
    const float tw = ps1_text_tracked_width(size, FONT_BOLD, track, caps);
    const float w = tw + 60 + 8, h = 82, x = right - w;
    /* radial-gradient(ellipse 80% 120% at 60% 50%, #262c86, #0f1462 75%): lighter right of the middle */
    PlatVertex v[12];
    const uint32_t edge = 0xff0f1462u, mid = 0xff262c86u;
    const float mx = x + w * 0.6f;
    PlatVertex a = {x, top, 0, 0, edge}, b = {mx, top, 0, 0, mid}, c = {mx, top + h, 0, 0, mid}, d = {x, top + h, 0, 0, edge};
    PlatVertex e = {x + w, top, 0, 0, argb_lerp(mid, edge, 0.55f)}, f = {x + w, top + h, 0, 0, argb_lerp(mid, edge, 0.55f)};
    v[0] = a, v[1] = b, v[2] = c, v[3] = a, v[4] = c, v[5] = d;
    v[6] = b, v[7] = e, v[8] = f, v[9] = b, v[10] = f, v[11] = c;
    plat_draw_mesh(NULL, v, 12, NULL, 0);
    const uint32_t border = 0xff7272deu;
    draw_rect(x, top, w, 4, border);
    draw_rect(x, top + h - 4, w, 4, border);
    draw_rect(x, top + 4, 4, h - 8, border);
    draw_rect(x + w - 4, top + 4, 4, h - 8, border);
    const float ty = ps1_text_middle(top, h, size);
    ps1_text_tracked(x + 34, ty + 3, size, FONT_BOLD, 0xff050838u, ALIGN_LEFT, track, caps);
    ps1_text_tracked(x + 34, ty, size, FONT_BOLD, PS1_LABEL, ALIGN_LEFT, track, caps);
}

typedef struct
{
    int seed, w, h, q;
    PlatTexture *tex;
} Splat;

/* shelf v3: Settings > Library > Splatters: Pixel (the low resolution blown up) or Smooth (the same paint at the
 * screen's own resolution, soft edges) */
static bool splat_smooth;
void ps1_splat_smooth(bool on)
{
    splat_smooth = on;
}

static Splat splats[40];
static int splat_count;

static void hsl(float h, float s, float l, uint8_t *p)
{
    for (int i = 0; i < 3; ++i)
    {
        static const float N[3] = {0, 8, 4};
        float k = fmodf(N[i] + h / 30.0f, 12.0f);
        float m = fminf(fminf(k - 3.0f, 9.0f - k), 1.0f);
        if (m < -1.0f)
            m = -1.0f;
        p[i] = u8(l - s * fminf(l, 1.0f - l) * m);
    }
}

/* Smooth value noise on a lattice (seeded), 0..1: the splatter's edge and its colour patches */
static float splat_hash(uint32_t seed, int x, int y)
{
    uint32_t h = seed * 0x9e3779b1u ^ (uint32_t)x * 0x85ebca6bu ^ (uint32_t)y * 0xc2b2ae35u;
    h ^= h >> 15, h *= 0x2c1b3c6du, h ^= h >> 12, h *= 0x297a2d39u, h ^= h >> 15;
    return (float)(h & 0xffffff) / 16777215.0f;
}

static float splat_noise(uint32_t seed, float x, float y)
{
    const int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
    const float a = splat_hash(seed, ix, iy), b = splat_hash(seed, ix + 1, iy);
    const float c = splat_hash(seed, ix, iy + 1), d = splat_hash(seed, ix + 1, iy + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

/* The console's splatter, as on its main menu: one solid smear of paint with a ragged, lumpy edge at the low
 * resolution's pixel scale (a few overlapping blobs plus noise), no holes, colour flowing in soft patches (a smooth
 * noise field over red, orange, yellow, green and cyan, with now and then magenta or blue), and a few loose specks
 * just outside the edge. The same seed gives the same plaque. */
static PlatTexture *make_splat(int seed, int W, int H, int q)
{
    uint8_t *px = calloc((size_t)W * H, 4);
    float *field = malloc(sizeof(float) * (size_t)W * H);
    uint8_t *mark = calloc((size_t)W * H, 1);
    int *stack = malloc(sizeof(int) * (size_t)W * H);
    if (!px || !field || !mark || !stack)
    {
        free(px), free(field), free(mark), free(stack);
        return NULL;
    }
    Rng r = {(uint32_t)seed};
    const uint32_t ns = (uint32_t)seed * 7919u + 17u;
    /* the blobs: five to seven along the plaque, each a little higher or lower, wider or narrower */
    enum { MAXB = 7 };
    const int nb = 5 + (int)(rng_next(&r) * 3.0f);
    float blobs[MAXB][4];
    for (int i = 0; i < nb; ++i)
    {
        blobs[i][0] = W * (0.13f + 0.74f * i / (nb - 1)) + (rng_next(&r) - 0.5f) * W * 0.05f;
        blobs[i][1] = H * (0.5f + (rng_next(&r) - 0.5f) * 0.12f);
        blobs[i][2] = W * (0.10f + rng_next(&r) * 0.05f);
        blobs[i][3] = H * (0.33f + rng_next(&r) * 0.07f);
    }
    const float edge_cell = 3.4f; /* the lumps: a few pixels across */
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            float f = 0;
            for (int b = 0; b < nb; ++b)
            {
                const float dx = (x + 0.5f - blobs[b][0]) / blobs[b][2], dy = (y + 0.5f - blobs[b][1]) / blobs[b][3];
                f = fmaxf(f, 1.0f - sqrtf(dx * dx + dy * dy));
            }
            const float n = splat_noise(ns, x / edge_cell, y / edge_cell) * 0.75f +
                            splat_noise(ns + 1, x / (edge_cell * 0.4f), y / (edge_cell * 0.4f)) * 0.25f;
            field[y * W + x] = f - 0.12f + (n - 0.5f) * 0.62f;
        }
    for (int i = 0; i < W * H; ++i)
        mark[i] = field[i] > 0.0f;
    /* no holes: what the outside can't reach from the border is paint */
    int sp = 0;
    for (int x = 0; x < W; ++x)
        for (int k = 0; k < 2; ++k)
        {
            const int i = (k ? H - 1 : 0) * W + x;
            if (!mark[i])
                mark[i] = 2, stack[sp++] = i;
        }
    for (int y = 0; y < H; ++y)
        for (int k = 0; k < 2; ++k)
        {
            const int i = y * W + (k ? W - 1 : 0);
            if (!mark[i])
                mark[i] = 2, stack[sp++] = i;
        }
    while (sp)
    {
        const int i = stack[--sp], x = i % W, y = i / W;
        const int nbr[4] = {x > 0 ? i - 1 : -1, x < W - 1 ? i + 1 : -1, y > 0 ? i - W : -1, y < H - 1 ? i + W : -1};
        for (int k = 0; k < 4; ++k)
            if (nbr[k] >= 0 && !mark[nbr[k]])
                mark[nbr[k]] = 2, stack[sp++] = nbr[k];
    }
    /* the colour: a smooth field, a little left-to-right drift, mapped to a hue range that differs per label */
    const float hue_lo = -25.0f + rng_next(&r) * 30.0f, hue_span = 175.0f + rng_next(&r) * 35.0f;
    const float cell = W / (2.5f + rng_next(&r) * 1.5f), drift = 0.55f + rng_next(&r) * 0.15f;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const int i = y * W + x;
            bool on = mark[i] != 2;
            if (!on)
            {
                /* a speck: sparse, only in a narrow ring just outside the edge */
                const float v = field[i];
                on = v > -0.08f && splat_hash(ns + 2, x, y) > 0.94f;
            }
            if (!on)
                continue;
            const float c = splat_noise(ns + 3, x / cell, y / (cell * 0.7f));
            float t = drift * (float)x / W + (1.0f - drift) * c;
            t = fminf(fmaxf((t - 0.2f) / 0.6f, 0.0f), 1.0f);
            const float h = fmodf(hue_lo + t * hue_span + 360.0f, 360.0f);
            const float l = 0.5f + (splat_noise(ns + 4, x / 6.0f, y / 4.0f) - 0.5f) * 0.08f;
            uint8_t *p = &px[(size_t)i * 4];
            hsl(h, 0.95f, l, p);
            for (int k = 0; k < 3; ++k) /* the console's 15-bit colour */
                p[k] = (uint8_t)((p[k] & 0xf8) | (p[k] >> 5));
            p[3] = 255;
        }
    PlatTexture *tex = NULL;
    if (q > 1)
    {
        /* Smooth: the same field and colours sampled q times finer, the edge anti-aliased, specks as round dots */
        const int QW = W * q, QH = H * q;
        uint8_t *hi = calloc((size_t)QW * QH, 4);
        if (hi)
        {
            for (int Y = 0; Y < QH; ++Y)
                for (int X = 0; X < QW; ++X)
                {
                    const float fx = (X + 0.5f) / q, fy = (Y + 0.5f) / q; /* low-resolution pixel space */
                    const int ix = (int)fx < W ? (int)fx : W - 1, iy = (int)fy < H ? (int)fy : H - 1, i = iy * W + ix;
                    float f = 0;
                    for (int b = 0; b < nb; ++b)
                    {
                        const float dx = (fx - blobs[b][0]) / blobs[b][2], dy = (fy - blobs[b][1]) / blobs[b][3];
                        f = fmaxf(f, 1.0f - sqrtf(dx * dx + dy * dy));
                    }
                    const float x = fx - 0.5f, y = fy - 0.5f;
                    const float n = splat_noise(ns, x / edge_cell, y / edge_cell) * 0.75f +
                                    splat_noise(ns + 1, x / (edge_cell * 0.4f), y / (edge_cell * 0.4f)) * 0.25f;
                    f = f - 0.12f + (n - 0.5f) * 0.62f;
                    float a = fminf(fmaxf(f / 0.05f + 0.5f, 0.0f), 1.0f);
                    {
                        /* the filled holes (paint the outside can't reach), blended between the cells so they keep no
                         * square corners */
                        const float gx = fx - 0.5f, gy = fy - 0.5f;
                        const int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
                        const float tx = gx - x0, ty = gy - y0;
                        float hole = 0;
                        for (int k = 0; k < 4; ++k)
                        {
                            const int cx = x0 + (k & 1), cy = y0 + (k >> 1);
                            if (cx < 0 || cy < 0 || cx >= W || cy >= H || mark[cy * W + cx] == 2)
                                continue;
                            hole += ((k & 1) ? tx : 1 - tx) * ((k >> 1) ? ty : 1 - ty);
                        }
                        hole = fminf(fmaxf((hole - 0.35f) / 0.3f, 0.0f), 1.0f);
                        a = fmaxf(a, hole);
                    }
                    if (mark[i] == 2 && field[i] > -0.08f && splat_hash(ns + 2, ix, iy) > 0.94f)
                    {
                        const float d = hypotf(fx - ix - 0.5f, fy - iy - 0.5f);
                        a = fmaxf(a, fminf(fmaxf((0.55f - d) * q + 0.5f, 0.0f), 1.0f));
                    }
                    const float c = splat_noise(ns + 3, x / cell, y / (cell * 0.7f));
                    float t = drift * x / W + (1.0f - drift) * c;
                    t = fminf(fmaxf((t - 0.2f) / 0.6f, 0.0f), 1.0f);
                    const float h = fmodf(hue_lo + t * hue_span + 360.0f, 360.0f);
                    const float l = 0.5f + (splat_noise(ns + 4, x / 6.0f, y / 4.0f) - 0.5f) * 0.08f;
                    uint8_t *p = &hi[((size_t)Y * QW + X) * 4];
                    hsl(h, 0.95f, l, p);
                    p[3] = (uint8_t)(a * 255.0f + 0.5f);
                }
            tex = plat_texture_create(hi, QW, QH, true);
            free(hi);
        }
    }
    else
        tex = plat_texture_create(px, W, H, false);
    free(px), free(field), free(mark), free(stack);
    return tex;
}

static PlatTexture *splat_texture(int seed, float w, float h)
{
    const int W = (int)(w / 5.0f + 0.5f), H = (int)(h / 5.0f + 0.5f);
    if (W < 2 || H < 2)
        return NULL;
    /* Smooth: one texel per real pixel (5 interface pixels a low-resolution one, times the display's scale) */
    int q = 1;
    if (splat_smooth)
    {
        q = (int)ceilf(5.0f * plat_ui_scale());
        q = q < 2 ? 2 : q > 12 ? 12 : q;
    }
    for (int i = 0; i < splat_count; ++i)
        if (splats[i].seed == seed && splats[i].w == W && splats[i].h == H && splats[i].q == q)
            return splats[i].tex;
    PlatTexture *t = make_splat(seed, W, H, q);
    if (splat_count < (int)(sizeof(splats) / sizeof(splats[0])))
        splats[splat_count++] = (Splat){seed, W, H, q, t};
    else
    {
        /* full: the oldest goes */
        plat_texture_free(splats[0].tex);
        memmove(splats, splats + 1, sizeof(splats[0]) * (size_t)(splat_count - 1));
        splats[splat_count - 1] = (Splat){seed, W, H, q, t};
    }
    return t;
}

static void splat_draw(float x, float y, float w, float h, int seed, const char *label, float text_size, bool dim,
                       uint32_t label_argb);

void ps1_splat(float x, float y, float w, float h, int seed, const char *label, float text_size, bool dim)
{
    splat_draw(x, y, w, h, seed, label, text_size, dim, 0xffffffffu);
}

void ps1_splat_choice(float x, float y, float w, float h, int seed, const char *label, float text_size, bool chosen,
                      bool dim)
{
    /* the console's menus: the splatter stays bright, only the chosen label is white; the others are grey */
    splat_draw(x, y, w, h, seed, label, text_size, dim, chosen ? 0xffffffffu : 0xff9c9cb4u);
}

static void splat_draw(float x, float y, float w, float h, int seed, const char *label, float text_size, bool dim,
                       uint32_t label_argb)
{
    const float a = dim ? 0.5f : 1.0f;
    PlatTexture *t = splat_texture(seed, w, h);
    if (t)
    {
        int tw, th;
        plat_texture_size(t, &tw, &th);
        plat_draw_texture_region(t, 0, 0, tw, th, x, y, w, h, with_alpha(0xffffffffu, a));
    }
    if (label && *label)
    {
        char caps[96];
        ps1_caps(label, caps, sizeof(caps));
        float size = text_size;
        while (size > 20 && text_width(size, FONT_BOLD, caps) > w - 24)
            size -= 2;
        ps1_text_outlined(x + w * 0.5f, ps1_text_middle(y, h, size), size, FONT_BOLD, with_alpha(label_argb, a),
                          ALIGN_CENTER, caps, size * 0.045f);
    }
}

float ps1_flat(float x, float y, float w, float h, const char *label, float text_size, bool selected, bool dim)
{
    char caps[96];
    /* "★ ..." : the star is drawn as an icon (the fonts' packed range is Latin-1) */
    const bool star = label && !strncmp(label, "\xe2\x98\x85", 3);
    ps1_caps(label ? (star ? label + 3 : label) : "", caps, sizeof(caps));
    const float track = 0.02f, star_w = star ? text_size * 1.1f : 0.0f;
    if (w <= 0)
        w = ps1_text_tracked_width(text_size, FONT_BOLD, track, caps) + text_size * 1.34f + star_w;
    const float a = dim ? 0.5f : 1.0f;
    draw_rect(x, y, w, h, with_alpha(selected ? 0xff5656b8u : 0xff3b3b8cu, a));
    const float ty = ps1_text_middle(y, h, text_size);
    float size = text_size;
    while (size > 16 && ps1_text_tracked_width(size, FONT_BOLD, track, caps) > w - 16 - star_w)
        size -= 2;
    const float tx = x + star_w * 0.5f + w * 0.5f;
    if (star)
    {
        const float tw = ps1_text_tracked_width(size, FONT_BOLD, track, caps);
        icon_draw(ICON_STAR, tx - tw * 0.5f - star_w, y + (h - size) * 0.5f, size, with_alpha(0xffffd92eu, a));
    }
    ps1_text_tracked(tx, ty + text_size * 0.07f, size, FONT_BOLD, with_alpha(0xff0a0a3cu, a), ALIGN_CENTER, track, caps);
    ps1_text_tracked(tx, ty, size, FONT_BOLD, with_alpha(0xffffffffu, a), ALIGN_CENTER, track, caps);
    return w;
}

/* The arrow glides to what is chosen, as the console's did, instead of jumping: each arrow drawn in a frame (a
 * screen may have two) keeps where it was and eases towards its new place, about 90% of the way in 0.13 s. It
 * jumps when it was not on screen the frame before, or after ps1_cursor_snap() (a new screen). */
enum { GLIDES = 4 };
static struct
{
    float x, y, size;
    unsigned frame;
} glides[GLIDES];
static unsigned glide_frame, glide_snap_frame;
static int glide_index;

void ps1_cursor_snap(void)
{
    glide_snap_frame = frame_no;
}

static void glide(float *x, float *y, float *size)
{
    if (glide_frame != frame_no)
        glide_frame = frame_no, glide_index = 0;
    const int i = glide_index < GLIDES - 1 ? glide_index : GLIDES - 1;
    ++glide_index;
    if (!glides[i].frame || glides[i].frame + 1 < frame_no || glide_snap_frame == frame_no)
        glides[i].x = *x, glides[i].y = *y, glides[i].size = *size;
    else if (glides[i].frame != frame_no)
    {
        const float k = 1.0f - expf(-last_dt * 17.0f);
        glides[i].x += (*x - glides[i].x) * k;
        glides[i].y += (*y - glides[i].y) * k;
        glides[i].size += (*size - glides[i].size) * k;
        if (fabsf(*x - glides[i].x) < 0.5f && fabsf(*y - glides[i].y) < 0.5f)
            glides[i].x = *x, glides[i].y = *y, glides[i].size = *size;
    }
    glides[i].frame = frame_no;
    *x = glides[i].x, *y = glides[i].y, *size = glides[i].size;
}

void ps1_cursor(float x, float y, float size)
{
    glide(&x, &y, &size);
    /* the concept's SVG, 80 units: tip (4,4); a slow diagonal bob of 6 px */
    const float bob = (0.5f - 0.5f * cosf(ps1_time() * 6.2831853f / 1.6f)) * 6.0f;
    const float k = size / 80.0f;
    const float ox = x - 4 * k - bob, oy = y - 4 * k - bob;
#define P(px_, py_) ox + (px_) * k, oy + (py_) * k
    PlatVertex v[9];
    /* drop-shadow(4px 6px 3px rgba(0,0,30,.55)) */
    const float sx = 4 * k, sy = 6 * k;
    const uint32_t sh = 0x8c00001eu;
    tri(&v[0], ox + 4 * k + sx, oy + 4 * k + sy, ox + 70 * k + sx, oy + 30 * k + sy, ox + 30 * k + sx, oy + 72 * k + sy, sh);
    plat_draw_mesh(NULL, v, 3, NULL, 0);
    tri(&v[0], P(4, 4), P(70, 30), P(40, 40), 0xffb79cffu);
    tri(&v[3], P(4, 4), P(40, 40), P(30, 72), 0xff7f4fe3u);
    tri(&v[6], P(40, 40), P(70, 30), P(30, 72), 0xff4b2a9eu);
#undef P
    plat_draw_mesh(NULL, v, 9, NULL, 0);
}

void ps1_monobar(float x, float y, float w, float h, const char *left, const char *right)
{
    static const uint32_t C[2] = {0xff17125au, 0xff0b0940u};
    static const float S[2] = {0, 1};
    draw_vgradient(x, y, w, h, C, S, 2);
    const float size = h >= 80 ? 46 : 36, small = size * 0.74f;
    float right_w = 0;
    char caps[200];
    if (right && *right)
    {
        ps1_caps(right, caps, sizeof(caps));
        right_w = ps1_text_tracked_width(small, FONT_TECH, 0.1f, caps);
        ps1_text_tracked(x + w - 40, ps1_text_middle(y, h, small), small, FONT_TECH, PS1_MUTED, ALIGN_RIGHT, 0.1f, caps);
    }
    if (left && *left)
    {
        ps1_caps(left, caps, sizeof(caps));
        float avail = w - 80 - (right_w > 0 ? right_w + 40 : 0), sz = size, track = 0.14f;
        while (sz > 22 && ps1_text_tracked_width(sz, FONT_TECH, track, caps) > avail)
        {
            if (track > 0.06f)
                track -= 0.02f;
            else
                sz -= 2;
        }
        /* still too long: cut, with dots */
        if (ps1_text_tracked_width(sz, FONT_TECH, track, caps) > avail)
        {
            size_t n = strlen(caps);
            while (n > 3 && ps1_text_tracked_width(sz, FONT_TECH, track, caps) > avail)
            {
                caps[--n] = '\0';
                while (n > 0 && ((unsigned char)caps[n - 1] & 0xc0) == 0x80)
                    caps[--n] = '\0';
                if (n > 3)
                    memcpy(caps + n - 3, "...", 3);
            }
        }
        ps1_text_tracked(x + 40, ps1_text_middle(y, h, sz), sz, FONT_TECH, 0xfff2f2ffu, ALIGN_LEFT, track, caps);
    }
}

float ps1_block(float x, float y, float s, bool selected)
{
    draw_rect(x, y, s, s, 0x6b000020u);
    /* the chosen block's frame is lit, so it reads from the sofa even without the arrow */
    const float b = (selected ? 5 : 3) * s / 152.0f;
    const uint32_t edge = selected ? 0xe6c4c4ffu : 0x388282e6u;
    draw_rect(x, y, s, b, edge);
    draw_rect(x, y + s - b, s, b, edge);
    draw_rect(x, y + b, b, s - 2 * b, edge);
    draw_rect(x + s - b, y + b, b, s - 2 * b, edge);
    if (!selected)
        return 1.0f;
    /* iconflick: 1.2 s in three steps, brightness 1, 1.45, 0.8 */
    const int step = (int)(fmodf(ps1_time(), 1.2f) / 0.4f);
    return step == 0 ? 1.0f : step == 1 ? 1.45f : 0.8f;
}

/* ---------------------------------------------------------------- the disc */

static PlatTexture *disc_tex;
static PlatTexture *disc_icon_tex;

static PlatTexture *make_disc(void)
{
    enum { N = 256 };
    uint8_t *px = malloc(N * N * 4);
    if (!px)
        return NULL;
    /* the concept's disc: a grey hub, a black ring, the black underside with faint colours turning (a conic gradient),
     * a light sheen, and a silver edge (#9aa0b4) with a dark line round it */
    static const Stop CONIC[] = {{0, 0x0c0c10}, {0.12f, 0x2a1030}, {0.25f, 0x0a0a0e}, {0.37f, 0x102a30}, {0.50f, 0x0b0b0f},
                                 {0.62f, 0x2a2410}, {0.75f, 0x0a0a0e}, {0.88f, 0x1a1040}, {1.0f, 0x0c0c10}};
    const float R = N * 0.5f, disc_r = R / 1.06f; /* the edge rings are outside the disc */
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
            float dx = x + 0.5f - R, dy = y + 0.5f - R, d = sqrtf(dx * dx + dy * dy);
            float ang = atan2f(dx, -dy); /* 0 at the top, clockwise */
            float turn = fmodf(ang / 6.2831853f + 1.0f - 20.0f / 360.0f, 1.0f);
            float c[3];
            float u = d / disc_r;
            float a = 1.0f;
            if (u < 0.127f)
                c[0] = 0xc8 / 255.0f, c[1] = 0xcc / 255.0f, c[2] = 0xd8 / 255.0f;
            else if (u < 0.134f)
                c[0] = c[1] = c[2] = 0.3f;
            else if (u < 0.156f)
                c[0] = 0x6c / 255.0f, c[1] = 0x6f / 255.0f, c[2] = 0x7c / 255.0f;
            else if (u < 0.214f)
                c[0] = c[1] = c[2] = 0.0f;
            else if (u <= 1.0f)
            {
                stops_eval(CONIC, 9, turn, c);
                /* ::after: two soft white sheens */
                float t2 = fmodf(ang / 6.2831853f + 1.0f, 1.0f), sheen = 0;
                if (t2 > 0.08f && t2 < 0.14f)
                    sheen = 0.18f * (1.0f - fabsf(t2 - 0.10f) / 0.04f);
                if (t2 > 0.58f && t2 < 0.64f)
                    sheen = 0.12f * (1.0f - fabsf(t2 - 0.60f) / 0.04f);
                sheen = clampf(sheen, 0.0f, 1.0f);
                for (int k = 0; k < 3; ++k)
                    c[k] = c[k] + (1.0f - c[k]) * sheen;
            }
            else if (u <= 1.04f)
                c[0] = 0x9a / 255.0f, c[1] = 0xa0 / 255.0f, c[2] = 0xb4 / 255.0f;
            else
            {
                c[0] = c[1] = 0x2a / 255.0f, c[2] = 0x3a / 255.0f;
                a = clampf(R - d + 0.5f, 0.0f, 1.0f);
            }
            if (u > 0.98f && u < 1.0f) /* soften the cut */
                a = 1.0f;
            uint8_t *p = &px[(y * N + x) * 4];
            p[0] = u8(c[0]);
            p[1] = u8(c[1]);
            p[2] = u8(c[2]);
            p[3] = u8(a);
        }
    PlatTexture *t = plat_texture_create(px, N, N, true);
    free(px);
    return t;
}

void ps1_disc(float cx, float cy, float d, float angle)
{
    if (!disc_tex)
        disc_tex = make_disc();
    const float r = d * 0.5f * 1.06f;
    shadow_at(cx - r, cy - r, 2 * r, d * 10.0f / 300.0f, d * 18.0f / 300.0f, 0.55f);
    if (!disc_tex)
    {
        draw_circle(cx, cy, r, 0xff111116u);
        return;
    }
    const float c = cosf(angle), s = sinf(angle);
    float px[4], py[4];
    static const float U[4] = {0, 1, 1, 0}, V[4] = {0, 0, 1, 1};
    for (int i = 0; i < 4; ++i)
    {
        float lx = (U[i] - 0.5f) * 2 * r, ly = (V[i] - 0.5f) * 2 * r;
        px[i] = cx + lx * c - ly * s;
        py[i] = cy + lx * s + ly * c;
    }
    PlatVertex v[6] = {{px[0], py[0], 0, 0, 0xffffffffu}, {px[1], py[1], 1, 0, 0xffffffffu}, {px[2], py[2], 1, 1, 0xffffffffu},
                       {px[0], py[0], 0, 0, 0xffffffffu}, {px[2], py[2], 1, 1, 0xffffffffu}, {px[3], py[3], 0, 1, 0xffffffffu}};
    plat_draw_mesh(disc_tex, v, 6, NULL, 0);
}

/* 16x16 pixel disc for a game without a cover: black underside, a band of the game's colour (white here, tinted when
 * drawn), a grey shine, the silver hub and the hole */
static PlatTexture *make_disc_icon(void)
{
    uint8_t px[16 * 16 * 4];
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
        {
            const float dx = x + 0.5f - 8.0f, dy = y + 0.5f - 8.0f, d = sqrtf(dx * dx + dy * dy);
            const float ang = atan2f(dy, dx); /* -pi..pi, 0 to the right */
            uint32_t rgb;
            uint8_t a = 255;
            if (d > 7.6f || d < 1.4f)
                a = 0, rgb = 0;
            else if (d < 2.6f)
                rgb = 0xc8ccd8; /* hub */
            else if (d < 3.4f)
                rgb = 0x6c6f7c;
            else if (d > 6.7f)
                rgb = 0x9aa0b4; /* the edge */
            else if (ang > -2.6f && ang < -1.2f)
                rgb = 0xffffff; /* the coloured shine, top left */
            else if (ang > 0.5f && ang < 1.4f)
                rgb = 0xb8bcd0; /* a grey one, bottom right */
            else
                rgb = 0x16161c;
            uint8_t *p = &px[(y * 16 + x) * 4];
            p[0] = (uint8_t)(rgb >> 16);
            p[1] = (uint8_t)(rgb >> 8);
            p[2] = (uint8_t)rgb;
            p[3] = a;
        }
    return plat_texture_create(px, 16, 16, false);
}

void ps1_disc_icon(float x, float y, float s, uint32_t seed, float bright)
{
    if (!disc_icon_tex)
        disc_icon_tex = make_disc_icon();
    if (!disc_icon_tex)
        return;
    /* a hue from the seed: the same game always gets the same colour */
    uint8_t c[3];
    hsl((float)(seed % 360u), 0.85f, 0.6f, c);
    float k = clampf(bright, 0.0f, 1.6f);
    uint32_t tint = 0xff000000u | (uint32_t)clampf(c[0] * k, 0, 255) << 16 | (uint32_t)clampf(c[1] * k, 0, 255) << 8 |
                    (uint32_t)clampf(c[2] * k, 0, 255);
    plat_draw_texture_region(disc_icon_tex, 0, 0, 16, 16, x, y, s, s, tint);
}

void ps1_dot_grid(float x, float y, float col_gap, float row_gap, int lit, const char *next_label)
{
    static const int ROW_BALL[4] = {PS1_BALL_GOLD, PS1_BALL_GOLD, PS1_BALL_RED, PS1_BALL_GREEN};
    for (int i = 0; i < 20; ++i)
    {
        const int col = i % 5, row = i / 5;
        const float cx = x + col * col_gap, cy = y + row * row_gap;
        if (i == lit && lit < 20)
            ps1_ball(PS1_BALL_GOLD, cx, cy, 92, next_label, 40, 0xff111111u, true, 1.0f);
        else if (i < lit)
            ps1_ball((enum Ps1Ball)ROW_BALL[row], cx, cy, 30, NULL, 0, 0, false, 1.0f);
        else
            ps1_ball(PS1_BALL_BLUE, cx, cy, 18, NULL, 0, 0, false, 1.0f);
    }
}

/* ---------------------------------------------------------------- the grey PS one dialect */

static PlatTexture *tiles_tex;

static PlatTexture *make_tiles(void)
{
    enum { W = 1920, H = 1080, T = 96 };
    uint8_t *px = malloc((size_t)W * H * 4);
    if (!px)
        return NULL;
    for (int ty = 0; ty < H / T + 1; ++ty)
        for (int tx = 0; tx < W / T; ++tx)
        {
            float cxn = (tx + 0.5f) / 20.0f - 0.5f, cyn = (ty + 0.5f) / 12.0f - 0.28f;
            float L = clampf(196.0f - sqrtf(cxn * cxn * 1.4f + cyn * cyn) * 95.0f, 112.0f, 200.0f);
            for (int y = 0; y < T; ++y)
                for (int x = 0; x < T; ++x)
                {
                    int X = tx * T + x, Y = ty * T + y;
                    if (X >= W || Y >= H)
                        continue;
                    float r, g, b;
                    if (x < 3 || y < 3 || x >= T - 3 || y >= T - 3)
                        r = g = L - 58, b = L - 54; /* grout */
                    else
                    {
                        float t = (float)(y - 3) / (T - 6);
                        r = (L + 16) + ((L - 16) - (L + 16)) * t;
                        g = (L + 15) + ((L - 17) - (L + 15)) * t;
                        b = (L + 21) + ((L - 10) - (L + 21)) * t;
                        if (y < 5 || x < 5) /* lit from the top left */
                            r += (255 - r) * 0.28f, g += (255 - g) * 0.28f, b += (255 - b) * 0.28f;
                        else if (y >= T - 5 || x >= T - 5)
                            r *= 0.78f, g *= 0.78f, b *= 0.78f;
                    }
                    uint8_t *p = &px[((size_t)Y * W + X) * 4];
                    p[0] = (uint8_t)clampf(r, 0, 255);
                    p[1] = (uint8_t)clampf(g, 0, 255);
                    p[2] = (uint8_t)clampf(b, 0, 255);
                    p[3] = 255;
                }
        }
    PlatTexture *t = plat_texture_create(px, W, H, false);
    free(px);
    return t;
}

void ps1_tiles(void)
{
    if (!tiles_tex)
        tiles_tex = make_tiles();
    if (tiles_tex)
        plat_draw_texture(tiles_tex, 0, 0, (float)plat_width(), (float)plat_height(), 0xffffffffu, false);
    else
        draw_rect(0, 0, (float)plat_width(), (float)plat_height(), 0xff86858bu);
}

void ps1_gplate(float x, float y, float w, float h, enum Ps1Plate colour, const char *label, float text_size, bool dim)
{
    static const uint32_t TOP[PS1_PLATE_COUNT] = {0xffd9aa20u, 0xffc81e1eu, 0xff3c4ccfu, 0xff1aa84au, 0xff7a3cc8u, 0xff5d6070u};
    static const uint32_t BOTTOM[PS1_PLATE_COUNT] = {0xffa77d06u, 0xff8c0c0cu, 0xff232e94u, 0xff0b6e2cu, 0xff4c1d8au, 0xff3c3e4au};
    static const uint32_t TEXT[PS1_PLATE_COUNT] = {0xfff6e7a6u, 0xffffdadau, 0xffdfe3ffu, 0xffd8ffe4u, 0xffeadcffu, 0xffe6e8f0u};
    const float a = dim ? 0.55f : 1.0f;
    draw_rect(x + 4, y + 6, w, h, with_alpha(0x40000000u, a)); /* 4px 6px 0 rgba(0,0,0,.25) */
    const uint32_t C[2] = {with_alpha(TOP[colour], a), with_alpha(BOTTOM[colour], a)};
    static const float S[2] = {0, 1};
    draw_vgradient(x, y, w, h, C, S, 2);
    const float b = h >= 50 ? 3 : 2;
    draw_rect(x, y, w, b, with_alpha(0x59ffffffu, a));
    draw_rect(x, y + b, b, h - b, with_alpha(0x59ffffffu, a));
    draw_rect(x, y + h - b, w, b, with_alpha(0x59000000u, a));
    draw_rect(x + w - b, y + b, b, h - 2 * b, with_alpha(0x59000000u, a));
    if (label && *label)
    {
        char caps[64];
        ps1_caps(label, caps, sizeof(caps));
        float size = text_size;
        while (size > 14 && ps1_text_tracked_width(size, FONT_BOLD, 0.05f, caps) > w - 16)
            size -= 2;
        const float ty = ps1_text_middle(y, h, size);
        ps1_text_tracked(x + w * 0.5f, ty + 2, size, FONT_BOLD, with_alpha(0x66000000u, a), ALIGN_CENTER, 0.05f, caps);
        ps1_text_tracked(x + w * 0.5f, ty, size, FONT_BOLD, with_alpha(TEXT[colour], a), ALIGN_CENTER, 0.05f, caps);
    }
}

void ps1_slot(float x, float y, float w, float h)
{
    draw_rect(x + 6, y + 8, w, h, 0x40000000u);
    draw_rect(x, y, w, h, 0xff0b0b0du);
    draw_rect(x, y, w, 4, 0xff2a2a30u);
    draw_rect(x, y + h - 4, w, 4, 0xff2a2a30u);
    draw_rect(x, y + 4, 4, h - 8, 0xff2a2a30u);
    draw_rect(x + w - 4, y + 4, 4, h - 8, 0xff2a2a30u);
}

static float mini(float x, float y, const char *label, bool on)
{
    const float size = 22, w = text_width(size, FONT_BOLD, label) + 32, h = 44;
    draw_rect(x, y, w, h, on ? 0xffc11c1cu : 0xff3a3a44u);
    draw_rect(x, y, w, 2, 0x4dffffffu);
    draw_rect(x, y + 2, 2, h - 2, 0x4dffffffu);
    draw_rect(x, y + h - 2, w, 2, 0x66000000u);
    draw_rect(x + w - 2, y + 2, 2, h - 4, 0x66000000u);
    text_draw(x + w * 0.5f, ps1_text_middle(y, h, size), size, FONT_BOLD, on ? 0xffffffffu : 0xff9a9aa6u, ALIGN_CENTER, label);
    return w;
}

float ps1_onoff(float right, float y, bool on)
{
    const char *l_on = "ON", *l_off = "OFF";
    const float w_on = text_width(22, FONT_BOLD, l_on) + 32, w_off = text_width(22, FONT_BOLD, l_off) + 32, gap = 10;
    const float x = right - w_off - gap - w_on;
    mini(x, y, l_on, on);
    mini(x + w_on + gap, y, l_off, !on);
    return w_on + gap + w_off;
}

float ps1_meter(float x, float y, int filled, int total)
{
    for (int i = 0; i < total; ++i)
        draw_rect(x + i * 28, y, 22, 30, i < filled ? 0xffe0b020u : 0xff2a2a32u);
    return total * 28 - 6;
}

/* ---------------------------------------------------------------- prompts */

static bool face_glyph(int glyph)
{
    return glyph == GLYPH_CROSS || glyph == GLYPH_CIRCLE || glyph == GLYPH_SQUARE || glyph == GLYPH_TRIANGLE;
}

/* the label in a wide glyph's pill (shoulders, sticks, OPTIONS, the touchpad); NULL for the round ones */
static const char *wide_label(int glyph)
{
    switch (glyph)
    {
    case GLYPH_L1: return "L1";
    case GLYPH_R1: return "R1";
    case GLYPH_L2: return "L2";
    case GLYPH_R2: return "R2";
    case GLYPH_L3: return "L3";
    case GLYPH_R3: return "R3";
    case GLYPH_START: return "OPTIONS";
    case GLYPH_SELECT: return "SELECT";
    case GLYPH_PS1_START: return "START";
    case GLYPH_TOUCHPAD: return "TOUCHPAD";
    default: return NULL;
    }
}

static float glyph_w(int glyph, float size)
{
    const float d = size * 44.0f / 27.0f;
    const char *wide = wide_label(glyph);
    return wide ? text_width(size * 20.0f / 27.0f, FONT_BOLD, wide) + d * 0.64f : d;
}

/* the face buttons in the controller's colours, inside a small navy (or light, on the grey wall) ball */
static void draw_glyph(int glyph, float cx, float cy, float size, bool grey)
{
    const float d = size * 44.0f / 27.0f;
    const char *wide = wide_label(glyph);
    if (wide)
    {
        /* .gl.w: a pill with the button's name */
        const float w = glyph_w(glyph, size), ts = size * 20.0f / 27.0f;
        draw_keep_round(true);
        draw_rrect(cx - w * 0.5f, cy - d * 0.5f, w, d, d * 0.5f, grey ? 0xff6a6a78u : 0xff3c3c90u);
        draw_rrect(cx - w * 0.5f + 3, cy - d * 0.5f + 3, w - 6, d - 6, d * 0.5f - 3, grey ? 0xffe8e8eeu : 0xff14144au);
        draw_keep_round(false);
        text_draw(cx, ps1_text_middle(cy - d * 0.5f, d, ts), ts, FONT_BOLD, grey ? 0xff1b1b28u : PS1_SOFT, ALIGN_CENTER, wide);
        return;
    }
    if (!face_glyph(glyph))
    {
        /* the d-pad: a chevron in a small ball */
        draw_circle(cx, cy, d * 0.5f, grey ? 0xff6a6a78u : 0xff3c3c90u);
        draw_circle(cx, cy, d * 0.5f - 3, grey ? 0xffe8e8eeu : 0xff14144au);
        static const int ICONS[4] = {ICON_CHEVRON_UP, ICON_CHEVRON_DOWN, ICON_CHEVRON_LEFT, ICON_CHEVRON_RIGHT};
        if (glyph >= GLYPH_UP && glyph <= GLYPH_RIGHT)
            icon_draw(ICONS[glyph - GLYPH_UP], cx - d * 0.3f, cy - d * 0.3f, d * 0.6f, grey ? 0xff1b1b28u : PS1_SOFT);
        else
            draw_pad_glyph((enum PadGlyph)glyph, cx, cy, d * 0.6f);
        return;
    }
    const uint32_t ball = grey ? 0xffe8e8eeu : 0xff14144au;
    draw_circle(cx, cy, d * 0.5f, grey ? 0xff6a6a78u : 0xff3c3c90u);
    draw_circle(cx, cy, d * 0.5f - 3, ball);
    /* P1-22: the four symbols in one stroke weight, each centred in its ring by its own drawn bounds (the triangle a
     * touch below its box's middle, where it looks centred), all of a size to the eye */
    const float r = d * 0.21f, t = d * 0.068f;
    uint32_t c;
    switch (glyph)
    {
    case GLYPH_CROSS:
    {
        c = grey ? 0xff2a4cc8u : 0xff8fb2ffu;
        const float a = r * 0.92f; /* the strokes' ends reach r with their width */
        draw_line(cx - a, cy - a, cx + a, cy + a, t, c);
        draw_line(cx + a, cy - a, cx - a, cy + a, t, c);
        break;
    }
    case GLYPH_CIRCLE:
        c = grey ? 0xffc81e1eu : 0xffff6e6eu; /* a red ring, as on the pad */
        draw_ring(cx, cy, r * 1.04f - t * 0.5f, t, c);
        break;
    case GLYPH_SQUARE:
    {
        c = grey ? 0xffb0308eu : 0xffff8fd8u;
        const float a = r * 0.9f;
        draw_rect(cx - a, cy - a, 2 * a, 2 * a, c);
        draw_rect(cx - a + t, cy - a + t, 2 * a - 2 * t, 2 * a - 2 * t, ball);
        break;
    }
    default:
    {
        /* filled outer triangle, the ball's colour inside: even stroke, crisp corners */
        c = grey ? 0xff128a5au : 0xff3fe3a3u;
        const float R = r * 1.28f, h = R * 1.5f, oy = cy - h * 0.5f + R * 0.06f; /* its box's top */
        const float inset = t * 2.0f; /* an even stroke t: the inner triangle's circumradius is R - 2t */
        const float Ri = R - inset;
        const float ccy = oy + R; /* the centroid */
        const PlatVertex v[6] = {
            {cx, ccy - R, 0, 0, c}, {cx + R * 0.866f, ccy + R * 0.5f, 0, 0, c}, {cx - R * 0.866f, ccy + R * 0.5f, 0, 0, c},
            {cx, ccy - Ri, 0, 0, ball}, {cx + Ri * 0.866f, ccy + Ri * 0.5f, 0, 0, ball},
            {cx - Ri * 0.866f, ccy + Ri * 0.5f, 0, 0, ball}};
        plat_draw_mesh(NULL, v, 6, NULL, 0);
        break;
    }
    }
}

float ps1_prompt_width(int glyph, const char *label, float size)
{
    char caps[96];
    ps1_caps(label ? label : "", caps, sizeof(caps));
    return (glyph >= 0 ? glyph_w(glyph, size) + size * 0.45f : 0.0f) + text_width(size, FONT_BOLD, caps);
}

float ps1_prompt(float x, float y, int glyph, const char *label, float size, bool grey)
{
    char caps[96];
    ps1_caps(label ? label : "", caps, sizeof(caps));
    float w = 0;
    if (glyph >= 0)
    {
        const float gw = glyph_w(glyph, size);
        draw_glyph(glyph, x + gw * 0.5f, y + size * 0.46f, size, grey); /* level with the label's capitals */
        w = gw + size * 0.45f;
    }
    const float ty = y - size * 0.06f;
    if (grey)
    {
        text_draw(x + w, ty + 2, size, FONT_BOLD, 0x8c000000u, ALIGN_LEFT, caps);
        text_draw(x + w, ty, size, FONT_BOLD, 0xffffffffu, ALIGN_LEFT, caps);
    }
    else
        text_draw(x + w, ty, size, FONT_BOLD, PS1_SOFT, ALIGN_LEFT, caps);
    return w + text_width(size, FONT_BOLD, caps);
}

float ps1_prompt_pair(float x, float y, int first, int second, const char *label, float size)
{
    const float w = ps1_prompt(x, y, first, "", size, false) + size * 0.2f;
    return w + ps1_prompt(x + w, y, second, label, size, false);
}

float ps1_prompts(float x, float y, const int *glyphs, const char *const *labels, int count, float size, bool grey)
{
    const float gap = size * 46.0f / 27.0f;
    float start = x;
    for (int i = 0; i < count; ++i)
        x += ps1_prompt(x, y, glyphs[i], labels[i], size, grey) + (labels[i] && *labels[i] ? gap : size * 0.3f);
    return x - start - (count ? gap : 0);
}

/* ---------------------------------------------------------------- a notice, full screen */

void ps1_notice_screen(const char *title, const char *text, const int *glyphs, const char *const *labels, int count)
{
    ps1_background(5);
    ps1_title_plate(title, 1824, 64);
    /* the text, word-wrapped, in the console's own way: a dark bar per line */
    const float size = 34, line_h = 64, w = 1400, x = (plat_width() - w) * 0.5f;
    char lines[8][200];
    int n = 0;
    const char *m = text;
    while (*m && n < 8)
    {
        size_t best = 0, i = 0;
        char line[200];
        for (;;)
        {
            size_t j = i;
            while (m[j] && m[j] != ' ' && m[j] != '\n')
                ++j;
            size_t len = j < sizeof(line) - 1 ? j : sizeof(line) - 1;
            memcpy(line, m, len);
            line[len] = '\0';
            if (best > 0 && text_width(size, FONT_BOLD, line) > w - 120)
                break;
            best = len;
            if (!m[j] || m[j] == '\n')
                break;
            i = j + 1;
        }
        if (best == 0)
            best = strlen(m) < sizeof(line) - 1 ? strlen(m) : sizeof(line) - 1;
        memcpy(lines[n], m, best);
        lines[n][best] = '\0';
        ++n;
        m += best;
        while (*m == ' ' || *m == '\n')
            ++m;
    }
    float y = (plat_height() - n * line_h) * 0.5f - 40;
    static const uint32_t C[2] = {0xff17125au, 0xff0b0940u};
    static const float S[2] = {0, 1};
    draw_vgradient(x, y - 30, w, n * line_h + 60, C, S, 2);
    for (int i = 0; i < n; ++i)
    {
        char caps[220];
        ps1_caps(lines[i], caps, sizeof(caps));
        ps1_text_drop(x + w * 0.5f, y + i * line_h + (line_h - size) * 0.5f, size, FONT_BOLD, PS1_LABEL, ALIGN_CENTER, caps, 3);
    }
    if (count > 0)
    {
        float pw = 0;
        for (int i = 0; i < count; ++i)
            pw += ps1_prompt_width(glyphs[i], labels[i], 27) + (i ? 46 : 0);
        ps1_prompts((plat_width() - pw) * 0.5f, 946, glyphs, labels, count, 27, false);
    }
}

void ps1_or_title(const char *text)
{
    if (TH_PS1)
        ps1_title_plate(text, 1824, 64);
    else
        text_draw(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, text);
}
