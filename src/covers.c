/*
 * SwanStationPS5 - cover art: local lookup, background download and decoding.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lookup order for a game:
 *   1. <root>/covers/custom/<id>.png|jpg, picked on the shelf (Details, then Square)
 *   2. in the game's own folder: cover.png|jpg, or an image named like the
 *      disc file ("Crash Bandicoot.cue" -> "Crash Bandicoot.png")
 *   3. in <root>/covers/: an image named like the game's title, the disc
 *      file or the serial (the way DuckStation finds covers)
 *   4. <root>/covers/default/<serial>.jpg or covers/3d/<serial>.png
 *   5. download from xlenore/psx-covers into (4), when enabled
 *   6. no serial (or nothing for it): by name, from libretro-thumbnails'
 *      Named_Boxarts (assets/boxarts-index.txt), into covers/boxart/<id>.png
 *   7. fallback-cover.png beside the game (picked up by the sync tool)
 *   8. a generated title card
 * One worker thread does the file I/O, downloads and JPEG/PNG decoding; the
 * main thread only uploads finished pixels, at most a few per frame.
 */
#include "covers.h"

#include "cheats.h"
#include "net.h"
#include "stb_image.h"
#include "ui/text.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEEP_RADIUS 22  /* textures kept around the cursor */
#define LOAD_RADIUS 16  /* covers requested around the cursor (the grid and spines show more) */
#define MAX_HEIGHT 640  /* decoded covers are scaled down to this */
#define QUEUE_SIZE 256
#define FAR (1 << 30)

typedef enum
{
    SLOT_EMPTY,
    SLOT_QUEUED,
    SLOT_READY, /* pixels decoded, waiting for upload */
    SLOT_LOADED,
} SlotState;

typedef struct
{
    SlotState state;
    PlatTexture *texture;
    PlatTexture *icon; /* the PS1 theme's memory-card icon: the cover in 32x32 pixels of 15-bit colour */
    bool placeholder; /* texture is a title card, real art may still arrive */
    uint8_t *pixels;
    int w, h;
    uint32_t color; /* average colour, for the shelf's background tint */
    int dist;          /* shelf positions from the centre, this frame (FAR: not on the shelf) */
    uint32_t used;     /* frame it was last within the load radius (the LRU) */
    uint32_t uploaded; /* frame its texture went up (covers_age) */
    /* copies, so the worker never touches the Library */
    char title[96];
    char serial[16];
    char id[64];
    char disc_name[160];
    char folder[SwanStationPS5_PATH_MAX];
} Slot;

static Slot *slots;
static int slot_count;
static Paths paths;
static int style;
static bool download_enabled;
static int network_failures;

static SDL_Thread *worker;
static SDL_mutex *lock;
static SDL_cond *wake;
static bool quit;
static int queue[QUEUE_SIZE];
static int queue_len;
static int downloading;
/* shelf v3: the window covers_set_window() asks for; -1 = the stock radii */
static int win_load = -1, win_keep;
static uint32_t frame;

void covers_url(char *out, size_t size, int cover_style, const char *serial)
{
    (void)cover_style;
    snprintf(out, size, "https://raw.githubusercontent.com/xlenore/psx-covers/main/covers/%s/%s.%s",
             /* cover_style == COVER_BOX3D ? "3d" : */ "default", serial,
             /* cover_style == COVER_BOX3D ? "png" : */ "jpg");
}

static void cache_path(char *out, size_t size, const char *serial)
{
    char file[64];
    snprintf(file, sizeof(file), "%s/%s.%s", /* style == COVER_BOX3D ? "3d" : */ "default", serial,
             /* style == COVER_BOX3D ? "png" : */ "jpg");
    path_join(out, size, paths.covers, file);
}

/* ---------------------------------------------------------------- worker */

static uint8_t *downscale(uint8_t *src, int *w, int *h)
{
    if (*h <= MAX_HEIGHT)
        return src;
    int nh = MAX_HEIGHT, nw = *w * MAX_HEIGHT / *h;
    uint8_t *dst = malloc((size_t)nw * nh * 4);
    if (!dst)
        return src;
    /* box filter: average the source pixels covering each destination pixel */
    for (int y = 0; y < nh; ++y)
    {
        int sy0 = y * *h / nh, sy1 = (y + 1) * *h / nh;
        for (int x = 0; x < nw; ++x)
        {
            int sx0 = x * *w / nw, sx1 = (x + 1) * *w / nw;
            unsigned sum[4] = {0, 0, 0, 0}, n = 0;
            for (int sy = sy0; sy < sy1; ++sy)
                for (int sx = sx0; sx < sx1; ++sx, ++n)
                    for (int c = 0; c < 4; ++c)
                        sum[c] += src[((size_t)sy * *w + sx) * 4 + c];
            for (int c = 0; c < 4; ++c)
                dst[((size_t)y * nw + x) * 4 + c] = (uint8_t)(n ? sum[c] / n : 0);
        }
    }
    stbi_image_free(src);
    *w = nw;
    *h = nh;
    return dst;
}

static uint8_t *title_card(const char *title, int *w, int *h)
{
    *w = 480;
    *h = 480;
    uint8_t *px = malloc((size_t)*w * *h * 4);
    if (!px)
        return NULL;
    for (int y = 0; y < *h; ++y)
        for (int x = 0; x < *w; ++x)
        {
            uint8_t *p = &px[((size_t)y * *w + x) * 4];
            float t = (float)y / *h;
            p[0] = (uint8_t)(30 + 12 * t);
            p[1] = (uint8_t)(36 + 10 * t);
            p[2] = (uint8_t)(78 + 30 * t);
            p[3] = 255;
            if (x < 6 || y < 6 || x >= *w - 6 || y >= *h - 6)
                p[0] = 70, p[1] = 86, p[2] = 170; /* frame */
        }
    text_render_rgba(px, *w, *h, 30, 30, FONT_BOLD, 0xff9fb4ffu, *w - 60, "SwanStationPS5");
    text_render_rgba(px, *w, *h, 170, 46, FONT_BOLD, 0xffffffffu, *w - 70, title);
    return px;
}

/* A scan's border: one line (a row, or a column) of nearly one colour that is near white (the scanner's lid) or nearly
 * pure black. Artwork that is merely dark or pale keeps its texture and is never taken for a border. */
static bool border_line(const uint8_t *px, int w, int x0, int y0, int dx, int dy, int n)
{
    long sum = 0, sum2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const uint8_t *p = &px[((size_t)(y0 + dy * i) * w + x0 + dx * i) * 4];
        const int v = (p[0] + p[1] + p[2]) / 3;
        sum += v, sum2 += (long)v * v;
    }
    const float mean = (float)sum / n, var = (float)sum2 / n - mean * mean;
    return (mean > 232.0f && var < 64.0f) || (mean < 10.0f && var < 16.0f);
}

/* Cuts the uniform near-white or near-black border scans often have, so the art reaches the case's edges: at most 6% of
 * a side, and only lines that are a border all along (P1-15). In place: the rows move up to the buffer's start. */
static void trim_borders(uint8_t *px, int *w, int *h)
{
    const int W = *w, H = *h;
    if (W < 64 || H < 64)
        return;
    int top = 0, bottom = H, left = 0, right = W;
    const int max_x = W * 6 / 100, max_y = H * 6 / 100;
    while (top < max_y && border_line(px, W, 0, top, 1, 0, W))
        ++top;
    while (H - bottom < max_y && border_line(px, W, 0, bottom - 1, 1, 0, W))
        --bottom;
    while (left < max_x && border_line(px, W, left, top, 0, 1, bottom - top))
        ++left;
    while (W - right < max_x && border_line(px, W, right - 1, top, 0, 1, bottom - top))
        --right;
    if (!top && bottom == H && !left && right == W)
        return;
    const int nw = right - left, nh = bottom - top;
    for (int y = 0; y < nh; ++y)
        memmove(&px[(size_t)y * nw * 4], &px[((size_t)(top + y) * W + left) * 4], (size_t)nw * 4);
    *w = nw;
    *h = nh;
}

static uint8_t *load_image(const char *path, int *w, int *h)
{
    int comp;
    uint8_t *px = stbi_load(path, w, h, &comp, 4);
    if (px)
        trim_borders(px, w, h);
    return px ? downscale(px, w, h) : NULL;
}

uint8_t *covers_decode(const char *path, int *w, int *h)
{
    return load_image(path, w, h);
}

uint8_t *covers_for_serial(const char *serial, int *w, int *h)
{
    char path[SwanStationPS5_PATH_MAX];
    cache_path(path, sizeof(path), serial);
    if (!path_exists(path) && download_enabled && network_failures < 3)
    {
        char url[256], dir[SwanStationPS5_PATH_MAX];
        covers_url(url, sizeof(url), style, serial);
        str_copy(dir, sizeof(dir), path);
        *strrchr(dir, '/') = '\0';
        make_dirs(dir);
        net_download(url, path);
    }
    return path_exists(path) ? load_image(path, w, h) : NULL;
}

static const char *const IMAGE_EXTS[] = {"png", "jpg", "jpeg"};

bool covers_is_image(const char *name)
{
    for (size_t i = 0; i < 3; ++i)
        if (str_icmp(path_ext(name), IMAGE_EXTS[i]) == 0)
            return true;
    return false;
}

/* <dir>/<name>.png|jpg|jpeg, the first that decodes. */
static uint8_t *load_named(const char *dir, const char *name, int *w, int *h)
{
    if (!dir[0] || !name[0])
        return NULL;
    for (size_t i = 0; i < 3; ++i)
    {
        char file[200], path[SwanStationPS5_PATH_MAX];
        snprintf(file, sizeof(file), "%s.%s", name, IMAGE_EXTS[i]);
        path_join(path, sizeof(path), dir, file);
        uint8_t *px = path_exists(path) ? load_image(path, w, h) : NULL;
        if (px)
            return px;
    }
    return NULL;
}

void covers_custom_dir(char *out, size_t size)
{
    path_join(out, size, paths.covers, "custom");
}

/* Finds or fetches the art for one slot. Runs on the worker thread. */
static uint8_t *produce(const Slot *job, int *w, int *h, bool *placeholder)
{
    char path[SwanStationPS5_PATH_MAX];
    *placeholder = false;
    covers_custom_dir(path, sizeof(path));
    const char *names[][2] = {
        {path, job->id},                        /* picked on the shelf */
        {job->folder, "cover"},                 /* beside the game */
        {job->folder, job->disc_name},
        {paths.covers, job->title},             /* in covers/, by name */
        {paths.covers, job->disc_name},
        {paths.covers, job->serial},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        uint8_t *px = load_named(names[i][0], names[i][1], w, h);
        if (px)
            return px;
    }
    if (job->serial[0])
    {
        cache_path(path, sizeof(path), job->serial);
        if (!path_exists(path) && download_enabled && network_failures < 3)
        {
            char url[256], dir[SwanStationPS5_PATH_MAX];
            covers_url(url, sizeof(url), style, job->serial);
            str_copy(dir, sizeof(dir), path);
            *strrchr(dir, '/') = '\0';
            make_dirs(dir);
            NetResult r = net_download(url, path);
            if (r == NET_UNAVAILABLE && ++network_failures == 3)
                SwanStationPS5_log("covers: network unavailable, downloads paused");
            if (r == NET_OK)
                network_failures = 0;
        }
        uint8_t *px = path_exists(path) ? load_image(path, w, h) : NULL;
        if (px)
            return px;
    }
    /* by name: libretro-thumbnails' box art, for discs without a (known) serial */
    {
        char file[140];
        snprintf(file, sizeof(file), "boxart/%.100s.png", job->id);
        path_join(path, sizeof(path), paths.covers, file);
        if (!path_exists(path) && download_enabled && network_failures < 3)
        {
            Game g;
            memset(&g, 0, sizeof(g));
            str_copy(g.title, sizeof(g.title), job->title);
            str_copy(g.disc_name, sizeof(g.disc_name), job->disc_name);
            str_copy(g.serial, sizeof(g.serial), job->serial);
            char name[256];
            if (cheats_best_in_index(&g, "boxarts-index.txt", name, sizeof(name)))
            {
                char url[700], dir[SwanStationPS5_PATH_MAX];
                size_t w = (size_t)snprintf(url, sizeof(url), "%s",
                                            "https://raw.githubusercontent.com/libretro-thumbnails/Sony_-_PlayStation/master/Named_Boxarts/");
                for (const unsigned char *p = (const unsigned char *)name; *p && w + 4 < sizeof(url); ++p)
                {
                    if (isalnum(*p) || strchr("-._~", *p))
                        url[w++] = (char)*p;
                    else
                        w += (size_t)snprintf(url + w, sizeof(url) - w, "%%%02X", *p);
                }
                url[w] = '\0';
                str_copy(dir, sizeof(dir), path);
                *strrchr(dir, '/') = '\0';
                make_dirs(dir);
                NetResult r = net_download(url, path);
                SwanStationPS5_log("covers: %s by name (%s): %s", job->title, name, r == NET_OK ? "ok" : "failed");
                if (r == NET_UNAVAILABLE && ++network_failures == 3)
                    SwanStationPS5_log("covers: network unavailable, downloads paused");
            }
        }
        uint8_t *px = path_exists(path) ? load_image(path, w, h) : NULL;
        if (px)
            return px;
    }
    /* art found beside the game files by tools/SwanStationPS5_sync.py: better than nothing */
    if (job->folder[0])
    {
        path_join(path, sizeof(path), job->folder, "fallback-cover.png");
        uint8_t *px = path_exists(path) ? load_image(path, w, h) : NULL;
        if (px)
            return px;
    }
    *placeholder = true;
    return title_card(job->title, w, h);
}

static int worker_main(void *unused)
{
    (void)unused;
    SDL_LockMutex(lock);
    while (!quit)
    {
        if (queue_len == 0)
        {
            SDL_CondWait(wake, lock);
            continue;
        }
        /* shelf v3: the nearest queued cover first, so a page that just settled jumps ahead of older requests */
        int best = 0;
        for (int q = 1; q < queue_len; ++q)
            if (queue[q] < slot_count && (queue[best] >= slot_count || slots[queue[q]].dist < slots[queue[best]].dist))
                best = q;
        int index = queue[best];
        memmove(queue + best, queue + best + 1, (size_t)(--queue_len - best) * sizeof(int));
        if (index >= slot_count || slots[index].state != SLOT_QUEUED)
            continue;
        Slot job = slots[index]; /* copy: strings stay valid without the lock */
        ++downloading;
        SDL_UnlockMutex(lock);

        int w = 0, h = 0;
        bool placeholder;
        uint8_t *px = produce(&job, &w, &h, &placeholder);
#ifdef SwanStationPS5_PREVIEW
        {
            /* the laptop preview: SwanStationPS5_COVER_DELAY_MS slows each cover down like a USB stick on the PS5 */
            const char *delay = getenv("SwanStationPS5_COVER_DELAY_MS");
            if (delay)
                SDL_Delay((uint32_t)atoi(delay));
        }
#endif

        SDL_LockMutex(lock);
        --downloading;
        if (index < slot_count && slots[index].state == SLOT_QUEUED && px)
        {
            slots[index].pixels = px;
            slots[index].w = w;
            slots[index].h = h;
            /* average of a sparse grid, darkened towards the theme */
            uint64_t sum[3] = {0, 0, 0};
            int samples = 0;
            for (int y = h / 16; y < h; y += h / 8 + 1)
                for (int x = w / 16; x < w; x += w / 8 + 1, ++samples)
                    for (int c = 0; c < 3; ++c)
                        sum[c] += px[((size_t)y * w + x) * 4 + c];
            if (samples)
                slots[index].color = 0xff000000u | (uint32_t)(sum[0] / samples) << 16 |
                                     (uint32_t)(sum[1] / samples) << 8 | (uint32_t)(sum[2] / samples);
            slots[index].placeholder = placeholder;
            slots[index].state = SLOT_READY;
        }
        else
        {
            free(px);
            if (index < slot_count && slots[index].state == SLOT_QUEUED)
                slots[index].state = SLOT_EMPTY;
        }
    }
    SDL_UnlockMutex(lock);
    return 0;
}

/* ---------------------------------------------------------------- main thread */

static void free_slots(void)
{
    for (int i = 0; i < slot_count; ++i)
    {
        plat_texture_free(slots[i].texture);
        plat_texture_free(slots[i].icon);
        free(slots[i].pixels);
    }
    free(slots);
    slots = NULL;
    slot_count = 0;
}

void covers_start(const Library *lib, const Paths *p, const Settings *settings)
{
    if (!lock)
    {
        lock = SDL_CreateMutex();
        wake = SDL_CreateCond();
    }
    SDL_LockMutex(lock);
    free_slots();
    queue_len = 0;
    paths = *p;
    style = settings->cover_style;
    download_enabled = settings->cover_download && net_available();
    network_failures = 0;
    slot_count = lib->count;
    slots = calloc((size_t)(slot_count ? slot_count : 1), sizeof(Slot));
    for (int i = 0; i < slot_count; ++i)
    {
        str_copy(slots[i].title, sizeof(slots[i].title), lib->games[i].title);
        str_copy(slots[i].serial, sizeof(slots[i].serial), lib->games[i].serial);
        str_copy(slots[i].id, sizeof(slots[i].id), lib->games[i].id);
        str_copy(slots[i].disc_name, sizeof(slots[i].disc_name), lib->games[i].disc_name);
        str_copy(slots[i].folder, sizeof(slots[i].folder), lib->games[i].folder);
    }
    SDL_UnlockMutex(lock);
    if (!worker)
        worker = SDL_CreateThread(worker_main, "covers", NULL);
    SwanStationPS5_log("covers: %d games, style %d, downloads %s", slot_count, style,
              download_enabled ? "on" : "off");
}

void covers_stop(void)
{
    if (!lock)
        return;
    SDL_LockMutex(lock);
    quit = true;
    SDL_CondSignal(wake);
    SDL_UnlockMutex(lock);
    if (worker)
        SDL_WaitThread(worker, NULL);
    worker = NULL;
    free_slots();
}

/* The PS1 theme's block icon: the cover's middle square averaged down to ICON x ICON pixels and cut to the console's
 * 15-bit colour, drawn later without smoothing (a memory card's save icon, more or less). */
#define ICON 32
static PlatTexture *make_icon(const uint8_t *px, int w, int h)
{
    if (!px || w < ICON || h < ICON)
        return NULL;
    uint8_t out[ICON * ICON * 4];
    const int side = w < h ? w : h, x0 = (w - side) / 2, y0 = (h - side) / 2;
    for (int y = 0; y < ICON; ++y)
        for (int x = 0; x < ICON; ++x)
        {
            const int sx0 = x0 + x * side / ICON, sx1 = x0 + (x + 1) * side / ICON;
            const int sy0 = y0 + y * side / ICON, sy1 = y0 + (y + 1) * side / ICON;
            uint32_t sum[3] = {0, 0, 0}, n = 0;
            for (int yy = sy0; yy < sy1; yy += 2)
                for (int xx = sx0; xx < sx1; xx += 2)
                {
                    const uint8_t *p = &px[((size_t)yy * w + xx) * 4];
                    sum[0] += p[0], sum[1] += p[1], sum[2] += p[2], ++n;
                }
            uint8_t *o = &out[(y * ICON + x) * 4];
            for (int c = 0; c < 3; ++c)
            {
                uint32_t v = n ? sum[c] / n : 0;
                /* a little more contrast, then 5 bits a channel */
                int k = (int)(((int)v - 128) * 1.12f + 128);
                k = k < 0 ? 0 : k > 255 ? 255 : k;
                o[c] = (uint8_t)((k & 0xf8) | (k >> 5));
            }
            o[3] = 255;
        }
    return plat_texture_create(out, ICON, ICON, false);
}

void covers_update(int center)
{
    covers_update_view(NULL, slot_count, center);
}

void covers_set_window(int load_radius, int max_textures)
{
    win_load = load_radius;
    win_keep = max_textures;
}

int covers_age(int index)
{
    if (!slots || index < 0 || index >= slot_count || !slots[index].texture)
        return -1;
    return (int)(frame - slots[index].uploaded);
}

/* view[k] = library index shown at shelf position k (NULL: the library order). */
void covers_update_view(const int *view, int count, int center_pos)
{
    if (!slots)
        return;
    SDL_LockMutex(lock);
    ++frame;
    const int radius = win_load >= 0 ? win_load : LOAD_RADIUS;
    for (int i = 0; i < slot_count; ++i)
        slots[i].dist = FAR;
    for (int k = 0; k < count; ++k)
    {
        int i = view ? view[k] : k;
        if (i >= 0 && i < slot_count)
        {
            slots[i].dist = k > center_pos ? k - center_pos : center_pos - k;
            if (slots[i].dist <= radius)
                slots[i].used = frame;
        }
    }
    /* upload a few finished images per frame, the nearest first */
    for (int uploads = 0; uploads < 3; ++uploads)
    {
        Slot *s = NULL;
        for (int i = 0; i < slot_count; ++i)
            if (slots[i].state == SLOT_READY && (!s || slots[i].dist < s->dist))
                s = &slots[i];
        if (!s)
            break;
        PlatTexture *t = plat_texture_create_mips(s->pixels, s->w, s->h);
        plat_texture_free(s->icon);
        s->icon = s->placeholder ? NULL : make_icon(s->pixels, s->w, s->h);
        free(s->pixels);
        s->pixels = NULL;
        /* a replaced picture (after a pick) swaps at once; a new one fades in */
        s->uploaded = s->texture ? frame - 1000u : frame;
        plat_texture_free(s->texture);
        s->texture = t;
        s->state = SLOT_LOADED;
    }
    /* shelf v3: requests that fell out of the window (fast paging) are dropped, not decoded */
    int kept = 0;
    for (int q = 0; q < queue_len; ++q)
    {
        int i = queue[q];
        if (i < slot_count && slots[i].state == SLOT_QUEUED && slots[i].dist > radius)
            slots[i].state = SLOT_EMPTY;
        else
            queue[kept++] = i;
    }
    queue_len = kept;
    /* free far textures */
    if (win_load < 0)
    {
        for (int i = 0; i < slot_count; ++i)
            if (slots[i].dist > KEEP_RADIUS && slots[i].state == SLOT_LOADED)
                slots[i].state = SLOT_EMPTY, plat_texture_free(slots[i].texture), slots[i].texture = NULL,
                plat_texture_free(slots[i].icon), slots[i].icon = NULL;
    }
    else
    {
        /* shelf v3: the window stays; beyond it the least recently shown go once there are more than win_keep */
        int loaded = 0;
        for (int i = 0; i < slot_count; ++i)
            loaded += slots[i].state == SLOT_LOADED;
        while (loaded > win_keep)
        {
            Slot *old = NULL;
            for (int i = 0; i < slot_count; ++i)
                if (slots[i].state == SLOT_LOADED && slots[i].dist > radius && (!old || slots[i].used < old->used))
                    old = &slots[i];
            if (!old)
                break;
            plat_texture_free(old->texture);
            old->texture = NULL;
            plat_texture_free(old->icon);
            old->icon = NULL;
            old->state = SLOT_EMPTY;
            --loaded;
        }
    }
    /* request near ones nearest-first */
    for (int d = 0; d <= radius; ++d)
        for (int side = -1; side <= 1; side += 2)
        {
            int k = center_pos + d * side;
            if (k < 0 || k >= count)
                continue;
            int i = view ? view[k] : k;
            if (i < 0 || i >= slot_count || slots[i].state != SLOT_EMPTY || queue_len >= QUEUE_SIZE)
                continue;
            slots[i].state = SLOT_QUEUED;
            queue[queue_len++] = i;
            if (d == 0)
                break;
        }
    if (queue_len)
        SDL_CondSignal(wake);
    SDL_UnlockMutex(lock);
}

PlatTexture *covers_icon(int index)
{
    return (slots && index >= 0 && index < slot_count && slots[index].texture) ? slots[index].icon : NULL;
}

PlatTexture *covers_get(int index)
{
    return (slots && index >= 0 && index < slot_count) ? slots[index].texture : NULL;
}

bool covers_is_placeholder(int index)
{
    return slots && index >= 0 && index < slot_count && slots[index].texture && slots[index].placeholder;
}

uint32_t covers_color(int index)
{
    return (slots && index >= 0 && index < slot_count && slots[index].texture) ? slots[index].color
                                                                               : 0;
}

void covers_reload(int index)
{
    if (!slots || index < 0 || index >= slot_count)
        return;
    SDL_LockMutex(lock);
    Slot *s = &slots[index];
    if (s->state == SLOT_READY || s->state == SLOT_LOADED)
    {
        free(s->pixels);
        s->pixels = NULL;
        s->state = SLOT_EMPTY; /* the old texture stays on screen until the new one is up */
    }
    SDL_UnlockMutex(lock);
}

int covers_downloading(void)
{
    if (!lock)
        return 0;
    SDL_LockMutex(lock);
    int n = queue_len + downloading;
    SDL_UnlockMutex(lock);
    return n;
}
