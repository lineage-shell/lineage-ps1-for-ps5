/*
 * SwanStationPS5 - shelf v3: the PlayStation screen of the console's power-on, drawn by us after the BIOS's own Sony
 * screen (the app's opening). Clean-room, from our clean-room spec (research ps1/spec/boot.md section 2,
 * public/LOADER-SPEC.md section 6): the 3D logo is the TMD model every licensed disc carries in sectors 5-11, read once
 * from a disc image of the user's library (never booted) and cached in <data>/cache/pslogo.tmd; the "PlayStation"
 * wordmark, the TM mark and the ROM font come from the user's own BIOS (its shell, unpacked at run time). The camera,
 * lights, fog, licence-line layout and timing are the spec's numbers. Nothing of Sony's is in the repository.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps1_kit.h"

#include "draw.h"
#include "sfx.h"

#include "../app.h"
#include "../platform/platform.h"

#include <SDL2/SDL.h>

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool ps1_chd_read(const char *path, int lba, int count, uint8_t *out); /* ps1_chd.c */

#define TMD_BYTES (7 * 2048)
#define SHELL_SIZE 0x5AD40
#define WORDMARK_AT 0x46B30 /* logo_PlayStation_TM_200x40 */
#define TM_AT 0x47B10       /* mark_TM_20x8 */
#define GLYPH_TABLE 0x586A4 /* the ROM font's per-glyph shift / width, in the shell */
#define SPACE_ENTRY 0x589E8

static struct
{
    bool active, pal, played, retried;
    double skip_s;
    uint64_t t0;
    uint8_t tmd[TMD_BYTES];
    uint8_t *rom;   /* the BIOS */
    uint8_t *shell; /* unpacked */
    PlatTexture *wordmark[30];
    PlatTexture *tm;
    int region; /* 0 Europe, 1 America, 2 Japan */
} L;

/* ------------------------------------------------------------------ the BIOS: shell, wordmark, TM, font */

static bool unpack_shell(const uint8_t *rom, uint8_t *out)
{
    /* the v4.x stub: lui a0,0x8003; addiu a0,a0,<offset of the packed shell> */
    const uint32_t w0 = rom[0x18000] | rom[0x18001] << 8 | rom[0x18002] << 16 | (uint32_t)rom[0x18003] << 24;
    const uint32_t w1 = rom[0x18004] | rom[0x18005] << 8 | rom[0x18006] << 16 | (uint32_t)rom[0x18007] << 24;
    if (w0 != 0x3C048003u || (w1 >> 16) != 0x2484u)
        return false;
    const uint8_t *s = rom + 0x18000 + (w1 & 0xffff), *end = rom + 512 * 1024;
    const uint32_t n = s[0] | s[1] << 8 | s[2] << 16;
    if (n != SHELL_SIZE)
        return false;
    s += 4;
    uint32_t o = 0;
    while (o < n)
    {
        if (s + 4 > end)
            return false;
        const uint32_t flags = (uint32_t)s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3];
        s += 4;
        const int k = flags & 3;
        for (int i = 0; i < 30 && o < n; ++i)
        {
            if (!(flags >> (31 - i) & 1))
            {
                if (s >= end)
                    return false;
                out[o++] = *s++;
                continue;
            }
            if (s + 2 > end)
                return false;
            const uint32_t v = (uint32_t)s[0] << 8 | s[1];
            s += 2;
            const uint32_t len = (v >> (14 - k)) + 3, dist = (v & (0x3FFFu >> k)) + 1;
            if (dist > o)
                return false;
            for (uint32_t j = 0; j < len && o < n; ++j, ++o)
                out[o] = out[o - dist];
        }
    }
    return true;
}

static uint32_t u32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* a raw 4-bit TIM of w x h pixels at `at` in the shell, as 5-bit colours (0x0000 = transparent); false if it isn't */
static bool tim4(int at, int w, int h, uint16_t *pixels)
{
    const uint8_t *t = L.shell + at;
    if (at + 64 + w * h / 2 > SHELL_SIZE || u32(t) != 0x10 || u32(t + 4) != 8 || u16(t + 16) != 16 || u16(t + 18) != 1)
        return false;
    const uint8_t *clut = t + 20, *img = t + 8 + u32(t + 8);
    if (u16(img + 8) * 4 != w || u16(img + 10) != h)
        return false;
    const uint8_t *px = img + 12;
    for (int i = 0; i < w * h; ++i)
        pixels[i] = u16(clut + 2 * ((px[i / 2] >> (i & 1 ? 4 : 0)) & 15));
    return true;
}

static uint8_t c5to8(int c)
{
    return (uint8_t)(c << 3 | c >> 2);
}

static PlatTexture *tim_texture(const uint16_t *pix, int w, int h, int bright)
{
    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba)
        return NULL;
    for (int i = 0; i < w * h; ++i)
    {
        const uint16_t c = pix[i];
        for (int k = 0; k < 3; ++k)
        {
            int v = (c >> (5 * k) & 31) * bright >> 7;
            rgba[i * 4 + k] = c5to8(v > 31 ? 31 : v);
        }
        rgba[i * 4 + 3] = c ? 255 : 0;
    }
    PlatTexture *t = plat_texture_create(rgba, w, h, false);
    free(rgba);
    return t;
}

static bool load_bios(void)
{
    if (L.shell)
        return true;
    DIR *d = opendir(app.paths.bios);
    if (!d)
        return false;
    struct dirent *e;
    char path[SwanStationPS5_PATH_MAX];
    while ((e = readdir(d)) && !L.shell)
    {
        if (str_icmp(path_ext(e->d_name), "bin") != 0)
            continue;
        path_join(path, sizeof(path), app.paths.bios, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || st.st_size != 512 * 1024)
            continue;
        FILE *f = fopen(path, "rb");
        if (!f)
            continue;
        uint8_t *rom = malloc(512 * 1024), *shell = malloc(SHELL_SIZE);
        const bool ok = rom && shell && fread(rom, 1, 512 * 1024, f) == 512 * 1024 && unpack_shell(rom, shell);
        fclose(f);
        if (ok)
        {
            L.rom = rom, L.shell = shell;
            static uint16_t probe[200 * 40];
            if (!tim4(WORDMARK_AT, 200, 40, probe) || !tim4(TM_AT, 20, 8, probe))
            {
                free(rom), free(shell);
                L.rom = L.shell = NULL;
            }
            else
                SwanStationPS5_log("ps1 logo: the wordmark and font from %s", e->d_name);
        }
        else
            free(rom), free(shell);
    }
    closedir(d);
    return L.shell != NULL;
}

/* ------------------------------------------------------------------ the disc: sectors 4-11 */

static bool tmd_valid(const uint8_t *b)
{
    if (u32(b) != 0x41 || u32(b + 4) != 0 || u32(b + 8) < 1)
        return false;
    const uint32_t pv = u32(b + 12), nv = u32(b + 16), pn = u32(b + 20), nn = u32(b + 24), pp = u32(b + 28),
                   np = u32(b + 32);
    if (np < 1 || np > 600 || pp != 0x1c || 12 + pv + 8 * nv > TMD_BYTES || 12 + pn + 8 * nn > TMD_BYTES)
        return false;
    uint32_t off = 0x28;
    for (uint32_t i = 0; i < np; ++i)
    {
        if (off + 20 > TMD_BYTES)
            return false;
        const int mode = b[off + 3];
        const uint8_t *q = b + off + 8;
        if (mode == 0x20)
        {
            if (u16(q) >= nn || u16(q + 2) >= nv || u16(q + 4) >= nv || u16(q + 6) >= nv)
                return false;
            off += 16;
        }
        else if (mode == 0x30)
        {
            for (int k = 0; k < 3; ++k)
                if (u16(q + 4 * k) >= nn || u16(q + 4 * k + 2) >= nv)
                    return false;
            off += 20;
        }
        else
            return false;
    }
    return true;
}

/* 2048-byte user data from a .cue (its first file), raw .bin / .img, .iso or .chd */
static bool read_sectors(const char *path, int lba, int count, uint8_t *out)
{
    const char *ext = path_ext(path);
    if (!str_icmp(ext, "chd"))
        return ps1_chd_read(path, lba, count, out);
    char file[SwanStationPS5_PATH_MAX];
    str_copy(file, sizeof(file), path);
    if (!str_icmp(ext, "cue"))
    {
        FILE *c = fopen(path, "r");
        if (!c)
            return false;
        char line[600];
        file[0] = '\0';
        while (fgets(line, sizeof(line), c) && !file[0])
        {
            char *q = strstr(line, "FILE \"");
            char *e2 = q ? strchr(q + 6, '"') : NULL;
            if (e2)
            {
                *e2 = '\0';
                char dir[SwanStationPS5_PATH_MAX];
                str_copy(dir, sizeof(dir), path);
                char *slash = strrchr(dir, '/');
                if (slash)
                    *slash = '\0';
                if (q[6] == '/')
                    str_copy(file, sizeof(file), q + 6);
                else
                    path_join(file, sizeof(file), dir, q + 6);
            }
        }
        fclose(c);
        if (!file[0])
            return false;
    }
    else if (str_icmp(ext, "bin") && str_icmp(ext, "img") && str_icmp(ext, "iso"))
        return false;
    FILE *f = fopen(file, "rb");
    if (!f)
        return false;
    static const uint8_t SYNC[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
    uint8_t raw[2352];
    bool ok = fread(raw, 1, 16, f) == 16;
    const bool is_raw = ok && !memcmp(raw, SYNC, 12);
    const int size = is_raw ? 2352 : 2048, off = !is_raw ? 0 : raw[15] == 2 ? 24 : 16;
    for (int i = 0; ok && i < count; ++i)
        ok = fseeko(f, (off_t)(lba + i) * size, SEEK_SET) == 0 && fread(raw, 1, (size_t)size, f) == (size_t)size &&
             (memcpy(out + i * 2048, raw + off, 2048), true);
    fclose(f);
    return ok;
}

/* 0 Europe, 1 America, 2 Japan, -1 unknown, from the serial */
static int serial_region3(const char *s)
{
    if (!strncmp(s, "SLES", 4) || !strncmp(s, "SCES", 4) || !strncmp(s, "SCED", 4))
        return 0;
    if (!strncmp(s, "SLUS", 4) || !strncmp(s, "SCUS", 4))
        return 1;
    return s[0] ? 2 : -1;
}

static bool scan_discs(void);

/* the logo model: the cache file only (placed there, or written by the optional disc read on an earlier run) */
static bool load_tmd(void)
{
    char cache[SwanStationPS5_PATH_MAX];
    path_join(cache, sizeof(cache), app.paths.cache, "pslogo.tmd");
    FILE *f = fopen(cache, "rb");
    if (f)
    {
        const bool ok = fread(L.tmd, 1, TMD_BYTES, f) == TMD_BYTES && tmd_valid(L.tmd);
        fclose(f);
        if (ok)
            return true;
    }
    SwanStationPS5_log("ps1 logo: no %s (the PlayStation screen is skipped)", cache);
    return false;
}

/* Settings > Library > PlayStation screen: once, with the main menu up, on a thread: a disc of the library, its region
 * first (never booted; only sectors 5-11 read), into the cache for the next start */
static bool scan_discs(void)
{
    char cache[SwanStationPS5_PATH_MAX];
    path_join(cache, sizeof(cache), app.paths.cache, "pslogo.tmd");
    FILE *f;
    static uint8_t buf[TMD_BYTES];
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < app.library.count; ++i)
        {
            const Game *g = &app.library.games[i];
            if ((serial_region3(g->serial) == L.region) != (pass == 0) || !strncmp(g->path, "/nfs", 4))
                continue;
            char path[SwanStationPS5_PATH_MAX];
            str_copy(path, sizeof(path), g->path);
            if (!str_icmp(path_ext(path), "m3u"))
            {
                /* its first disc */
                FILE *m = fopen(g->path, "r");
                char line[400] = "";
                while (m && fgets(line, sizeof(line), m) && (line[0] == '#' || line[0] == '\r' || line[0] == '\n'))
                    ;
                if (m)
                    fclose(m);
                line[strcspn(line, "\r\n")] = '\0';
                if (!line[0])
                    continue;
                path_join(path, sizeof(path), g->folder, line);
            }
            if (!read_sectors(path, 5, 7, buf) || !tmd_valid(buf))
                continue;
            make_dirs(app.paths.cache);
            if ((f = fopen(cache, "wb")))
            {
                fwrite(buf, 1, TMD_BYTES, f);
                fclose(f);
            }
            SwanStationPS5_log("ps1 logo: the logo model read from %s, cached", path);
            return true;
        }
    SwanStationPS5_log("ps1 logo: no disc image with the logo data");
    return false;
}

static SDL_atomic_t scan_started;
static int scan_thread(void *arg)
{
    (void)arg;
    scan_discs();
    return 0;
}

void ps1_pslogo_scan_later(void)
{
    if (!app.global.shelf_logo_scan || !SDL_AtomicCAS(&scan_started, 0, 1))
        return;
    char cache[SwanStationPS5_PATH_MAX];
    struct stat st;
    path_join(cache, sizeof(cache), app.paths.cache, "pslogo.tmd");
    if (stat(cache, &st) == 0 && st.st_size == TMD_BYTES)
        return;
    SDL_Thread *t = SDL_CreateThread(scan_thread, "pslogo-scan", NULL);
    if (t)
        SDL_DetachThread(t);
}

/* ------------------------------------------------------------------ the 3D logo (spec 2b-2d, integer GTE steps) */

static int clampi(int v, int a, int b)
{
    return v < a ? a : v > b ? b : v;
}

static int R[3][3];
static int UNR[0x101];

static void setup_gte(void)
{
    for (int i = 0; i < 0x101; ++i)
    {
        const int v = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
        UNR[i] = v > 0 ? v : 0;
    }
    const double k = 2 * 3.14159265358979323846 / 4096;
    const int rx = 260, ry = -30, rz = 0;
    const int cx = (int)lround(4096 * cos(rx * k)), sx = (int)lround(4096 * sin(rx * k));
    const int cy = (int)lround(4096 * cos(ry * k)), sy = (int)lround(4096 * sin(ry * k));
    const int cz = (int)lround(4096 * cos(rz * k)), sz = (int)lround(4096 * sin(rz * k));
#define T(a, b) (((a) * (b)) >> 12)
    const int m[3][3] = {{T(cz, cy), -T(sz, cy), sy},
                         {T(sz, cx) + T(T(cz, sy), sx), T(cz, cx) - T(T(sz, sy), sx), -T(cy, sx)},
                         {T(sz, sx) - T(T(cz, sy), cx), T(cz, sx) + T(T(sz, sy), cx), T(cy, cx)}};
#undef T
    memcpy(R, m, sizeof(R));
}

static int gte_div(int h, int sz)
{
    if (h >= sz * 2)
        return 0x1ffff;
    int bits = 0;
    for (int v = sz; v; v >>= 1)
        ++bits;
    const int z = 16 - bits;
    const int64_t n = (int64_t)h << z;
    int64_t d = (int64_t)sz << z;
    const int64_t u = UNR[(d - 0x7fc0) >> 7] + 0x101;
    d = (0x2000080 - d * u) >> 8;
    d = (0x0000080 + d * u) >> 8;
    const int64_t r = (n * d + 0x8000) >> 16;
    return r > 0x1ffff ? 0x1ffff : (int)r;
}

typedef struct
{
    int x, y, z, ir0;
} Proj;

static Proj rtp(const int16_t *v, int dqa)
{
    static const int TR[3] = {0, -340, 5888};
    int mac[3];
    for (int i = 0; i < 3; ++i)
        mac[i] = (int)(((int64_t)TR[i] * 4096 + (int64_t)R[i][0] * v[0] + (int64_t)R[i][1] * v[1] + (int64_t)R[i][2] * v[2]) >> 12);
    const int ir1 = clampi(mac[0], -0x8000, 0x7fff), ir2 = clampi(mac[1], -0x8000, 0x7fff), sz = clampi(mac[2], 0, 0xffff);
    const int n = gte_div(1024, sz);
    Proj p;
    p.x = clampi((int)(((int64_t)320 * 65536 + (int64_t)ir1 * n) >> 16), -0x400, 0x3ff);
    p.y = clampi((int)(((int64_t)240 * 65536 + (int64_t)ir2 * n) >> 16), -0x400, 0x3ff);
    p.z = sz;
    p.ir0 = clampi((int)((0x1400000 + (int64_t)dqa * n) >> 12), 0, 0x1000);
    return p;
}

static int ncds(const int16_t *nrm, int rgb, int ir0)
{
    static const int LLM[3][3] = {{0, 0, 4000}, {0, -4000, 0}, {-4000, 0, 0}};
    static const int LCM[3] = {4000, 3000, 4000};
    int ir[3], out = 0;
    for (int i = 0; i < 3; ++i)
        ir[i] = clampi((LLM[i][0] * nrm[0] + LLM[i][1] * nrm[1] + LLM[i][2] * nrm[2]) >> 12, 0, 0x7fff);
    for (int i = 0; i < 3; ++i)
    {
        const int l = clampi(((100 << 4) * 4096 + LCM[0] * ir[0] + LCM[1] * ir[1] + LCM[2] * ir[2]) >> 12, 0, 0x7fff);
        int64_t mac = ((int64_t)(rgb >> (8 * i) & 255) * l) << 4;
        const int64_t d = clampi((int)((0 - mac) >> 12), -0x8000, 0x7fff);
        mac = (d * ir0 + mac) >> 12;
        out |= clampi((int)(mac >> 4), 0, 255) >> 3 << (5 * i); /* the GPU's 5 bits */
    }
    return out;
}

typedef struct
{
    int otz, idx;
    int x[3], y[3], c5;
} Face;

static int cmp_face(const void *a, const void *b)
{
    const Face *p = a, *q = b;
    return p->otz != q->otz ? q->otz - p->otz : q->idx - p->idx; /* far first; in a slot, the later face first */
}

/* screen position of a VRAM coordinate: the 640 x 480 draw area on the 4:3 area of the 1920 x 1080 interface; the
 * display starts at VRAM row 2 */
#define SX(x) (240.0f + (x) * 2.25f)
#define YS 2.25f                /* the PlayStation screen is a 640 x 480 picture (capture), on the 4:3 area */
#define SY(y) (((y) + 6) * YS)  /* its display window starts 6 lines above VRAM row 0 (capture) */

static void draw_logo(int frame)
{
    const uint8_t *b = L.tmd;
    const int dqa = (int)(((int64_t)(frame * 133 - 6980) * -320) / 1024);
    const uint32_t pv = u32(b + 12), pn = u32(b + 20), np = u32(b + 32);
    static Face faces[600];
    int n = 0;
    uint32_t off = 0x28;
    for (uint32_t i = 0; i < np && i < 600; ++i)
    {
        const int mode = b[off + 3];
        const int rgb = (int)(u32(b + off + 4) & 0xffffff);
        int16_t v[3][3], nrm[3];
        const uint8_t *q = b + off + 8;
        if (mode == 0x20)
        {
            for (int k = 0; k < 3; ++k)
                nrm[k] = (int16_t)u16(b + 12 + pn + u16(q) * 8 + 2 * k);
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    v[j][k] = (int16_t)u16(b + 12 + pv + u16(q + 2 + 2 * j) * 8 + 2 * k);
            off += 16;
        }
        else
        {
            /* Gouraud: the shell averages the three normals (unsigned sums) and draws the face flat */
            uint32_t sx = 0, sy = 0, szw = 0;
            for (int j = 0; j < 3; ++j)
            {
                const uint8_t *nn = b + 12 + pn + u16(q + 4 * j) * 8;
                sx += u16(nn), sy += u16(nn + 2), szw += u32(nn + 4);
                for (int k = 0; k < 3; ++k)
                    v[j][k] = (int16_t)u16(b + 12 + pv + u16(q + 4 * j + 2) * 8 + 2 * k);
            }
            nrm[0] = (int16_t)(sx / 3), nrm[1] = (int16_t)(sy / 3), nrm[2] = (int16_t)(szw / 3);
            off += 20;
        }
        Proj p[3];
        for (int j = 0; j < 3; ++j)
            p[j] = rtp(v[j], dqa);
        const int64_t nclip = (int64_t)p[0].x * p[1].y + (int64_t)p[1].x * p[2].y + (int64_t)p[2].x * p[0].y -
                              (int64_t)p[0].x * p[2].y - (int64_t)p[1].x * p[0].y - (int64_t)p[2].x * p[1].y;
        const int otz = clampi((0x155 * (p[0].z + p[1].z + p[2].z)) >> 12, 0, 0xffff) >> 2;
        if (otz <= 0 || otz >= 0x400 || nclip <= 0)
            continue;
        Face *f = &faces[n++];
        f->otz = otz, f->idx = (int)i, f->c5 = ncds(nrm, rgb, p[2].ir0);
        for (int j = 0; j < 3; ++j)
            f->x[j] = p[j].x, f->y[j] = p[j].y;
    }
    qsort(faces, (size_t)n, sizeof(Face), cmp_face);
    static PlatVertex vtx[600 * 3];
    int k = 0;
    for (int i = 0; i < n; ++i)
    {
        const int c = faces[i].c5;
        const uint32_t argb = 0xff000000u | (uint32_t)c5to8(c & 31) << 16 | (uint32_t)c5to8(c >> 5 & 31) << 8 | c5to8(c >> 10 & 31);
        for (int j = 0; j < 3; ++j)
            vtx[k++] = (PlatVertex){SX(faces[i].x[j]), SY(faces[i].y[j] + 1), 0, 0, argb}; /* a row lower (capture) */
    }
    if (k)
        plat_draw_mesh(NULL, vtx, k, NULL, 0);
}

/* ------------------------------------------------------------------ the text (the shell's ROM-font sprites) */

static int glyph_index(char ch)
{
    if (ch >= '0' && ch <= '9')
        return 0x93 + ch - '0';
    if (ch >= 'A' && ch <= 'Z')
        return 0x9d + ch - 'A';
    if (ch >= 'a' && ch <= 'z')
        return 0xb7 + ch - 'a';
    switch (ch)
    {
    case '.': return 4;
    case ',': return 3;
    case '-': return 0x1d;
    case '\'': return 0x26;
    default: return 0;
    }
}

/* one text image, drawn centred at (cx, cy) in draw-area coordinates (its ink starts at cx - 8n) */
static void draw_text(const char *s, int cx, int cy)
{
    const int n = (int)strlen(s);
    int x = cx - 8 * n;
    const uint32_t grey = 0xff000000u | 0x010101u * c5to8(22);
    for (int c = 0; c < n; ++c)
    {
        const int idx = glyph_index(s[c]);
        int shift = 0, width = 16;
        if (idx > 0x93 && idx < 0xd1)
            shift = u16(L.shell + GLYPH_TABLE + idx * 4), width = u16(L.shell + GLYPH_TABLE + idx * 4 + 2);
        else if (idx == 0)
            shift = u16(L.shell + SPACE_ENTRY), width = u16(L.shell + SPACE_ENTRY + 2);
        const uint8_t *g = L.rom + 0x66000 + idx * 30;
        for (int r = 0; r < 15; ++r)
        {
            uint16_t bits = (uint16_t)(((g[2 * r] << 8 | g[2 * r + 1]) << shift) & 0xffff);
            bits = (uint16_t)(bits | bits >> 1 | (bits & 1) << 15); /* widened by a pixel, as the shell does */
            for (int p = 0; p < 16; ++p)
                if (bits & (0x8000u >> p))
                {
                    int q = p;
                    while (q + 1 < 16 && (bits & (0x8000u >> (q + 1))))
                        ++q;
                    draw_rect(SX(x + p), SY(cy - 8 + r + 2), (q - p + 1) * 2.25f, YS, grey);
                    p = q;
                }
        }
        x += (width >> 2) * 4;
    }
}

static const char *const LICENCE[3] = {
    "          Licensed  by          Sony Computer Entertainment Euro pe   ",
    "          Licensed  by          Sony Computer Entertainment Amer  ica ",
    "          Licensed  by          Sony Computer Entertainment Inc."};
static const char *const REGION_CODE[3] = {"SCEE", "SCEA", "SCEI"};

static void draw_licence(void)
{
    const char *s = LICENCE[L.region];
    char f[5][24] = {"", "", "", "", ""};
    const int len = (int)strlen(s);
    for (int i = 0; i < 4; ++i)
        snprintf(f[i], sizeof(f[i]), "%.16s", i * 16 < len ? s + i * 16 : "");
    snprintf(f[4], sizeof(f[4]), "%s", len > 64 ? s + 64 : "");
    const int l4 = (int)strlen(f[4]);
    const int d = l4 == 3 ? -4 : l4 == 6 ? -14 : 22, c4 = l4 == 3 ? 532 : 530;
    draw_text(f[0], 349, 340);
    draw_text(f[1], 461, 340);
    draw_text(f[2], 260 + d, 360);
    draw_text(f[3], 448 + d, 360);
    if (l4 == 3 || l4 == 6)
        draw_text(f[4], c4, 360);
    draw_text(REGION_CODE[L.region], 320, 400);
    if (L.tm)
        plat_draw_texture(L.tm, SX(364 - 10), SY(403 - 4 + 2), 20 * 2.25f, 8 * YS, 0xffffffffu, true);
}

/* ------------------------------------------------------------------ the sound (spec 2h; the BIOS's own VAB bank) */

#define VAB_AT 0x47BB0
#define SOUND_GAIN 0.75f /* the capture's level: the Sony part's RMS 0.158 (PCSX, full scale) */
#define TAIL_S 1.1f /* the sound's fade after the picture's black, over the menu's fade-in */
#define LPF_HZ 2200.0 /* between PCSX (3 kHz fit) and the real SCEA console filmed (YouTube BOPViekdacQ) */
#define REVERB_GAIN 0.55f
#define SR 44100

typedef struct
{
    int prog, vag, vol, pan, center, fine, lo, hi, mode;
    uint16_t adsr1, adsr2;
} Tone;

static float *vag_pcm[4];
static int vag_len[4], vag_loop[4];
static Tone tones[32];
static int tone_count, prog_vol[16], master_vol;

/* SPU ADPCM, one sample: stops at the end flag; the loop start from the flags */
static void decode_vag(const uint8_t *p, const uint8_t *end, int k)
{
    static const int F[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    int cap = 0, n = 0, h1 = 0, h2 = 0, loop = -1;
    float *out = NULL;
    for (; p + 16 <= end; p += 16)
    {
        int shift = p[0] & 15, filt = p[0] >> 4 & 7;
        const int flags = p[1];
        shift = shift > 12 ? 9 : shift, filt = filt > 4 ? 0 : filt;
        if (flags & 4)
            loop = n;
        if (n + 28 > cap)
        {
            cap = cap ? cap * 2 : 4096;
            float *g = realloc(out, sizeof(float) * (size_t)cap);
            if (!g)
                break;
            out = g;
        }
        for (int b = 2; b < 16; ++b)
            for (int h = 0; h < 2; ++h)
            {
                const int nib = h ? p[b] >> 4 : p[b] & 15;
                int v = ((nib >= 8 ? nib - 16 : nib) << 12) >> shift;
                v += (h1 * F[filt][0] + h2 * F[filt][1] + 32) >> 6;
                v = clampi(v, -32768, 32767);
                out[n++] = v / 32768.0f;
                h2 = h1, h1 = v;
            }
        if (flags & 1)
        {
            if (!(flags & 2))
                loop = -1;
            else if (loop < 0)
                loop = 0;
            break;
        }
    }
    vag_pcm[k] = out, vag_len[k] = n, vag_loop[k] = loop;
}

static bool load_bank(void)
{
    if (tone_count)
        return true;
    const uint8_t *v = L.shell + VAB_AT;
    if (memcmp(v, "pBAV", 4))
        return false;
    const int ps = u16(v + 18), vs = u16(v + 22);
    master_vol = v[24];
    const uint8_t *P = v + 32, *T = P + 128 * 16, *V = T + ps * 16 * 32, *body = V + 512;
    for (int i = 0; i < 16; ++i)
        prog_vol[i] = P[16 * i + 1];
    for (int i = 0; i < ps * 16 && tone_count < 32; ++i)
    {
        const uint8_t *t = T + 32 * i;
        const int vag = (int16_t)u16(t + 22);
        if (vag <= 0 || vag > 3)
            continue;
        tones[tone_count++] = (Tone){(int16_t)u16(t + 20), vag, t[2], t[3], t[4], t[5], t[6], t[7], t[1], u16(t + 16), u16(t + 18)};
    }
    if (vs < 1 || vs > 3)
        return false;
    const uint8_t *start = body, *end = L.shell + SHELL_SIZE;
    for (int k = 1; k <= vs; ++k)
    {
        const int size = u16(V + 2 * k) * 8;
        if (start + size > end)
            return false;
        decode_vag(start, start + size, k);
        start += size;
    }
    return tone_count > 0;
}

/* the SPU's ADSR (psx-spx), one step per output sample at 44.1 kHz */
static float adsr_rate(int shift, int step, bool exp, bool dec, float level)
{
    float cyc = (float)(1 << (shift > 11 ? shift - 11 : 0));
    float st = (float)(step * (1 << (shift < 11 ? 11 - shift : 0)));
    if (exp && !dec && level > 0x6000)
        cyc *= 4;
    if (exp && dec)
        st = st * level / 0x8000;
    return st / cyc;
}

static void play_tone(float *mixL, float *mixR, float *wetL, float *wetR, int n, const Tone *t, int on, int off, int note,
                      int vel, int kpan)
{
    const float *pcm = vag_pcm[t->vag];
    const int len = vag_len[t->vag], loop = vag_loop[t->vag];
    if (!pcm || len < 2)
        return;
    const double step = pow(2.0, (note - t->center + t->fine / 128.0) / 12.0);
    const float amp = vel / 127.0f * t->vol / 127.0f * prog_vol[t->prog & 15] / 127.0f * master_vol / 127.0f;
    const int pan = clampi(t->pan + kpan - 64, 0, 127);
    const float gl = fminf(1.0f, (127 - pan) / 64.0f) * amp, gr = fminf(1.0f, pan / 64.0f) * amp;
    const uint16_t a1 = t->adsr1, a2 = t->adsr2;
    const bool am = a1 >> 15 & 1, sm = a2 >> 15 & 1, sdir = a2 >> 14 & 1, rm = a2 >> 5 & 1;
    const int ash = a1 >> 10 & 31, ast = a1 >> 8 & 3, dsh = a1 >> 4 & 15, ssh = a2 >> 8 & 31, sst = a2 >> 6 & 3, rsh = a2 & 31;
    const float sl = (float)(((a1 & 15) + 1) * 0x800);
    float level = 0;
    int phase = 0; /* attack, decay, sustain, release */
    double pos = 0;
    for (int i = on; i < n; ++i)
    {
        if (phase < 3 && i >= off)
            phase = 3;
        switch (phase)
        {
        case 0:
            level += adsr_rate(ash, 7 - ast, am, false, level);
            if (level >= 0x7fff)
                level = 0x7fff, phase = 1;
            break;
        case 1:
            level += adsr_rate(dsh, -8, true, true, level);
            if (level <= sl)
                level = fmaxf(level, 0), phase = 2;
            break;
        case 2:
            level += sdir ? adsr_rate(ssh, -8 + sst, sm, true, level) : adsr_rate(ssh, 7 - sst, sm, false, level);
            level = fminf(0x7fff, fmaxf(0, level));
            break;
        default:
            level = fmaxf(0, level + adsr_rate(rsh, -8, rm, true, level));
            break;
        }
        if (phase == 3 && level <= 1)
            return;
        int a = (int)pos;
        if (a >= len - 1)
        {
            if (loop < 0)
                return;
            pos = loop + fmod(pos - loop, (double)(len - loop));
            a = (int)pos;
        }
        const int b = a + 1 < len ? a + 1 : loop >= 0 ? loop : a;
        const float fr = (float)(pos - a), y = (pcm[a] * (1 - fr) + pcm[b] * fr) * level / 0x7fff;
        mixL[i] += y * gl, mixR[i] += y * gr;
        if (t->mode & 4)
            wetL[i] += y * gl, wetR[i] += y * gr;
        pos += step;
    }
}

/* reverb mode 6 ("Space Echo" in the library's numbering), psx-spx's algorithm at half rate */
static void reverb_space(const float *inL, const float *inR, float *outL, float *outR, int n)
{
    static const uint16_t P[32] = {0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000, 0x6000, 0x5400, 0x1ED6,
                                   0x1A31, 0x1D14, 0x183B, 0x1BC2, 0x16B2, 0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D,
                                   0x11F6, 0x0C5D, 0x1056, 0x0AE1, 0x0AE0, 0x07A2, 0x0464, 0x0232, 0x8000, 0x8000};
    const int N = 0xF6C0 / 2;
    float *buf = calloc((size_t)N, sizeof(float));
    if (!buf)
        return;
#define VOL(i) ((int16_t)P[i] / 32768.0f)
#define ADR(i) ((int)P[i] * 4)
    const float vIIR = VOL(2), vC1 = VOL(3), vC2 = VOL(4), vC3 = VOL(5), vC4 = VOL(6), vWALL = VOL(7), vAPF1 = VOL(8),
                vAPF2 = VOL(9), vLIN = VOL(30), vRIN = VOL(31);
    const int dAPF1 = ADR(0), dAPF2 = ADR(1), mLSAME = ADR(10), mRSAME = ADR(11), mLC1 = ADR(12), mRC1 = ADR(13),
              mLC2 = ADR(14), mRC2 = ADR(15), dLSAME = ADR(16), dRSAME = ADR(17), mLDIFF = ADR(18), mRDIFF = ADR(19),
              mLC3 = ADR(20), mRC3 = ADR(21), mLC4 = ADR(22), mRC4 = ADR(23), dLDIFF = ADR(24), dRDIFF = ADR(25),
              mLAPF1 = ADR(26), mRAPF1 = ADR(27), mLAPF2 = ADR(28), mRAPF2 = ADR(29);
#undef VOL
#undef ADR
    int cur = 0;
#define RD(o) buf[((cur + (o)) % N + N) % N]
#define WR(o, v) buf[((cur + (o)) % N + N) % N] = fminf(1.0f, fmaxf(-1.0f, (v)))
    float pl = 0, pr = 0;
    for (int i = 0; i + 1 < n; i += 2)
    {
        const float Lin = vLIN * (inL[i] + inL[i + 1]) * 0.5f, Rin = vRIN * (inR[i] + inR[i + 1]) * 0.5f;
        WR(mLSAME, (Lin + RD(dLSAME) * vWALL - RD(mLSAME - 1)) * vIIR + RD(mLSAME - 1));
        WR(mRSAME, (Rin + RD(dRSAME) * vWALL - RD(mRSAME - 1)) * vIIR + RD(mRSAME - 1));
        WR(mLDIFF, (Lin + RD(dRDIFF) * vWALL - RD(mLDIFF - 1)) * vIIR + RD(mLDIFF - 1));
        WR(mRDIFF, (Rin + RD(dLDIFF) * vWALL - RD(mRDIFF - 1)) * vIIR + RD(mRDIFF - 1));
        float Lo = vC1 * RD(mLC1) + vC2 * RD(mLC2) + vC3 * RD(mLC3) + vC4 * RD(mLC4);
        float Ro = vC1 * RD(mRC1) + vC2 * RD(mRC2) + vC3 * RD(mRC3) + vC4 * RD(mRC4);
        Lo = Lo - vAPF1 * RD(mLAPF1 - dAPF1), WR(mLAPF1, Lo), Lo = Lo * vAPF1 + RD(mLAPF1 - dAPF1);
        Ro = Ro - vAPF1 * RD(mRAPF1 - dAPF1), WR(mRAPF1, Ro), Ro = Ro * vAPF1 + RD(mRAPF1 - dAPF1);
        Lo = Lo - vAPF2 * RD(mLAPF2 - dAPF2), WR(mLAPF2, Lo), Lo = Lo * vAPF2 + RD(mLAPF2 - dAPF2);
        Ro = Ro - vAPF2 * RD(mRAPF2 - dAPF2), WR(mRAPF2, Ro), Ro = Ro * vAPF2 + RD(mRAPF2 - dAPF2);
        outL[i] = (pl + Lo) * 0.5f, outL[i + 1] = Lo, outR[i] = (pr + Ro) * 0.5f, outR[i + 1] = Ro;
        pl = Lo, pr = Ro;
        cur = (cur + 1) % N;
    }
#undef RD
#undef WR
    free(buf);
}

/* the notes: at the first 3D tick, prog 2 G1 and prog 0 C2 for 31 ticks; then table B from the wordmark phase (its
 * 60 Hz times x 5/6 on PAL, in the screen's ticks) */
/* The whole opening's notes, at their frames (F_* below, 50 or 60 per second): table A of the Sony screen (the two
 * drones at power-on, keyed off at the first emblem tick; the rest when the clock, jumped to 74 [89] there and
 * stepped once a tick, reaches their times, which PAL scales by 5/6), then the PlayStation screen's two notes at its
 * first 3D tick and table B from its wordmark phase. `logo`: false = the Sony screen alone. */
#define F_DRONES 56   /* the drones, keyed at power-on (capture: first sound at frame 56) */
#define F_FADE 72     /* the Sony screen's grey ramp starts (capture: first step at 75, full grey at 121) */
/* The holds are the drive reading the disc, so they follow a real console (an SCEA PS1 filmed booting, YouTube
 * BOPViekdacQ: from the Sony screen's first light, 6.68 s to the grey fade, then the PlayStation screen 5.96 s until
 * the game's black), not PCSX, whose disc reads are instant: first light at 75 here (50 Hz) */
#define F_PHASE_A 409 /* the PlayStation screen's grey fade: 75 + 6.68 s */
#define F_PHASE_B 422 /* its 3D logo, 13 ticks later */
enum { PH_B = 31, PH_C = 30, PH_D = 212, PH_OUT = 12 }; /* a + b + c + d + out = 5.96 s, the real screen's length */

static int16_t *render_sound(bool logo, size_t *frames)
{
    if (!load_bank())
        return NULL;
    const float hz = L.pal ? 50.0f : 60.0f;
    /* the first emblem tick: its picture shows at frame F_FADE + fade + 2 (124), the tick itself a frame before */
    const int fade = L.pal ? 50 : 60, emblem0 = F_FADE + fade + 1, jump = L.pal ? 74 : 89;
    /* the picture's end (black), then the sound rings on and fades out over TAIL_S (equal power) under the menu */
    const int black_frame = logo ? F_PHASE_B + PH_B + PH_C + PH_D + PH_OUT : emblem0 + (L.pal ? 150 : 180) + 12;
    const int end_frame = black_frame + (int)(TAIL_S * hz) + 1;
    const int n = (int)(end_frame / hz * SR);
    float *m = calloc((size_t)n * 6, sizeof(float));
    if (!m)
        return NULL;
    float *dl = m, *dr = m + n, *wl = m + 2 * n, *wr = m + 3 * n, *rl = m + 4 * n, *rr = m + 5 * n;
    static const int A[22][6] = {
        {0, 30, 1, 36, 127, 80},    {0, 30, 1, 48, 50, 48},     {90, 105, 3, 50, 35, 40},   {100, 115, 3, 55, 45, 56},
        {110, 125, 3, 60, 50, 72},  {120, 135, 3, 62, 60, 88},  {130, 145, 3, 67, 80, 72},  {140, 155, 3, 60, 55, 56},
        {150, 165, 3, 62, 60, 40},  {160, 175, 3, 67, 95, 56},  {169, 199, 1, 29, 127, 48}, {170, 200, 1, 41, 127, 80},
        {170, 185, 3, 62, 55, 72},  {180, 195, 3, 64, 60, 88},  {190, 205, 3, 67, 90, 72},  {200, 215, 3, 62, 45, 56},
        {210, 225, 3, 64, 50, 40},  {220, 235, 3, 67, 80, 56},  {230, 245, 3, 62, 30, 72},  {240, 265, 3, 64, 45, 80},
        {0, 0, 0, 0, 0, 0},         {0, 0, 0, 0, 0, 0}};
    static const int B[10][6] = {{0, -1, 0, 31, 80, 64},  {12, 25, 3, 48, 50, 40}, {16, 22, 3, 58, 55, 56},
                                 {20, 26, 3, 63, 65, 72}, {24, 31, 3, 65, 70, 88}, {28, 38, 3, 70, 80, 72},
                                 {30, 36, 3, 60, 60, 56}, {34, 41, 3, 65, 74, 40}, {40, 48, 3, 67, 78, 56},
                                 {44, 50, 3, 72, 70, 72}};
    static const int PAIR[2][6] = {{0, 31, 2, 31, 127, 64}, {0, 31, 0, 36, 60, 64}};
    for (int e = 0; e < 20 + (logo ? 12 : 0); ++e)
    {
        const int *x;
        int on, off; /* frames */
        if (e < 20)
        {
            x = A[e];
            const int t_on = L.pal ? x[0] * 5 / 6 : x[0], t_off = L.pal ? x[1] * 5 / 6 : x[1];
            if (x[0] == 0)
                on = F_DRONES, off = emblem0; /* keyed at power-on, off at the first clock check */
            else
                on = emblem0 + (t_on > jump ? t_on - jump : 0), off = emblem0 + (t_off > jump ? t_off - jump : 0);
        }
        else if (e < 22)
            x = PAIR[e - 20], on = F_PHASE_B, off = F_PHASE_B + 31;
        else
        {
            x = B[e - 22];
            const int t_on = L.pal ? x[0] * 5 / 6 : x[0], t_off = x[1] < 0 ? -1 : L.pal ? x[1] * 5 / 6 : x[1];
            on = F_PHASE_B + 31 + t_on, off = t_off < 0 ? end_frame * 4 : F_PHASE_B + 31 + t_off;
        }
        const int s_on = (int)(on / hz * SR), s_off = (int)fminf((float)n, off / hz * SR);
        for (int k = 0; k < tone_count; ++k)
            if (tones[k].prog == x[2] && tones[k].lo <= x[3] && x[3] <= tones[k].hi)
                play_tone(dl, dr, wl, wr, n, &tones[k], s_on, s_off, x[3], x[4], x[5]);
    }
    reverb_space(wl, wr, rl, rr, n);
    /* the SPU's Gaussian interpolation softens the top end: a 2-pole low-pass (fitted to the capture's bands) */
    {
        const double fc = LPF_HZ, q = 0.7071, w0 = 2 * 3.14159265358979 * fc / SR, al = sin(w0) / (2 * q), c0 = cos(w0);
        const double b0 = (1 - c0) / 2, b1 = 1 - c0, b2 = (1 - c0) / 2, a0 = 1 + al, a1 = -2 * c0, a2 = 1 - al;
        float *ch[4] = {dl, dr, rl, rr};
        for (int c = 0; c < 4; ++c)
        {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (int i = 0; i < n; ++i)
            {
                const double x = ch[c][i], y = (b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
                x2 = x1, x1 = x, y2 = y1, y1 = y;
                ch[c][i] = (float)y;
            }
        }
    }
    int16_t *out = malloc((size_t)n * 2 * sizeof(int16_t));
    if (out)
        for (int i = 0; i < n; ++i)
        {
            /* master volume L 127 / R 111 (spec 0.3), the reverb's return; the gain fitted to the capture */
            const float L0 = (dl[i] + rl[i] * REVERB_GAIN) * SOUND_GAIN,
                        R0 = (dr[i] + rr[i] * REVERB_GAIN) * (111 / 127.0f) * SOUND_GAIN;
            const float fade_at = black_frame / hz * SR, x = (i - fade_at) / (TAIL_S * SR);
            const float k = x <= 0 ? 1.0f : x >= 1 ? 0.0f : cosf(x * 1.5707963f); /* equal-power fade out */
            out[2 * i] = (int16_t)clampi((int)(L0 * k * 32767), -32768, 32767);
            out[2 * i + 1] = (int16_t)clampi((int)(R0 * k * 32767), -32768, 32767);
        }
    free(m);
    *frames = (size_t)n;
    return out;
}

/* ------------------------------------------------------------------ the Sony screen (spec 1) */

#define SONY_PAL_AT 0x557E8 /* logo_SONY_240x44 */
#define SCE_PAL_AT 0x56CC8  /* logo_SCE_240x54 */
#define SONY_NTSC_AT 0x53820
#define SCE_NTSC_AT 0x54674
#define TM24_AT 0x58658
static PlatTexture *sony_tex[18], *sce_tex[18], *tm24_tex[18];
static int sony_w, sony_h, sce_w, sce_h;

/* the text's palette fade (spec 1): each 5-bit channel (22 (17 - t) + c t) / 17; 0 and channels of 22 stay */
static PlatTexture *fade_texture(const uint16_t *pix, int w, int h, int t)
{
    static uint16_t tmp[240 * 54];
    for (int i = 0; i < w * h; ++i)
    {
        const uint16_t c = pix[i];
        uint16_t o = c;
        if (c)
        {
            o = c & 0x8000;
            for (int k = 0; k < 3; ++k)
            {
                const int v = c >> (5 * k) & 31;
                o |= (uint16_t)((v == 22 ? v : (22 * (17 - t) + v * t) / 17) << (5 * k));
            }
        }
        tmp[i] = o;
    }
    return tim_texture(tmp, w, h, 128);
}

static bool load_sony(void)
{
    static uint16_t pix[240 * 54];
    sony_w = L.pal ? 240 : 200, sony_h = L.pal ? 44 : 36, sce_w = L.pal ? 240 : 200, sce_h = L.pal ? 54 : 44;
    bool ok = true;
    if (!sony_tex[0])
    {
        ok = tim4(L.pal ? SONY_PAL_AT : SONY_NTSC_AT, sony_w, sony_h, pix);
        for (int t = 0; ok && t < 18; ++t)
            sony_tex[t] = fade_texture(pix, sony_w, sony_h, t);
        ok = ok && tim4(L.pal ? SCE_PAL_AT : SCE_NTSC_AT, sce_w, sce_h, pix);
        for (int t = 0; ok && t < 18; ++t)
            sce_tex[t] = fade_texture(pix, sce_w, sce_h, t);
        if (ok && !L.pal && tim4(TM24_AT, 24, 10, pix))
            for (int t = 0; t < 18; ++t)
                tm24_tex[t] = fade_texture(pix, 24, 10, t);
    }
    return ok;
}

/* the TV frame: the 640-wide draw area on the 4:3 area, 512 (PAL) or 480 lines high, of which the PAL screens light
 * the first 506 (capture); black sides, as a PS1 on a 16:9 TV */
#define LINES (L.pal ? 512.0f : 480.0f)
#define FX(x) (240.0f + (x) * 2.25f)
#define FY(y) ((y) * 1080.0f / LINES)

static void sony_tri(const float x[3], const float y[3], const uint32_t c[3])
{
    PlatVertex v[3];
    for (int i = 0; i < 3; ++i)
        v[i] = (PlatVertex){FX(x[i]), FY(y[i] - 3), 0, 0, c[i]}; /* 3 lines up (capture) */
    plat_draw_mesh(NULL, v, 3, NULL, 0);
}

/* frame f of the Sony screen, f = 0 at its grey ramp's first step */
static void draw_sony(int f)
{
    const int fade = L.pal ? 50 : 60, T = L.pal ? 50 : 60, H = L.pal ? 512 : 480;
    if (f <= fade)
    {
        const int g = 180 * f / fade >> 3;
        draw_rect(240, 0, 1440, FY(506), 0xff000000u | 0x010101u * c5to8(g));
        return;
    }
    draw_rect(240, 0, 1440, FY(506), 0xff000000u | 0x010101u * c5to8(22));
    if (f - fade < 2)
        return; /* the full grey alone for a frame (capture: the diamond shows at 124) */
    const int t = f - fade - 1 > T ? T : f - fade - 1;
    const float w = 256, h = L.pal ? 307 : 256, cx = 320, cy = H / 2.0f, dy = L.pal ? 57 : 48;
    const uint32_t red = 0xff000000u | (uint32_t)c5to8(178 >> 3) << 16, orange = red | (uint32_t)c5to8(140 >> 3) << 8;
    /* the backing diamond (a Gouraud quad, never moves) */
    const float qx[4] = {cx - w / 2, cx, cx + w / 2, cx}, qy[4] = {cy, cy - h / 2, cy, cy + h / 2};
    const uint32_t qc[4] = {red, orange, red, orange};
    sony_tri((float[]){qx[0], qx[1], qx[3]}, (float[]){qy[0], qy[1], qy[3]}, (uint32_t[]){qc[0], qc[1], qc[3]});
    sony_tri((float[]){qx[2], qx[1], qx[3]}, (float[]){qy[2], qy[1], qy[3]}, (uint32_t[]){qc[2], qc[1], qc[3]});
    /* the halves: scaled about their red apex, sliding in (right first, the left on top) */
    const float sc = (4096 - 2258.0f * t / T) / 4096;
    for (int side = 1; side >= -1; side -= 2)
    {
        const float ox = cx + side * (128 - 80.0f * t / T), oy = cy + side * (dy * t / T);
        const float px[3] = {ox, ox - side * (w / 2) * sc, ox - side * (w / 2) * sc};
        const float py[3] = {oy, oy - (h / 2) * sc, oy + (h / 2) * sc};
        sony_tri(px, py, (uint32_t[]){red, orange, orange});
    }
    /* the words, from the first hold tick, fading in through their palettes over 17 ticks */
    const int ht = f - fade - 1 - T + 2; /* capture: the words' palette reaches its end at 188 */
    if (ht >= 1)
    {
        const int k = ht > 17 ? 17 : ht;
        const float sx = L.pal ? 338 : 320, sy = L.pal ? 69 : 84, ex = L.pal ? 338 : 320, ey = L.pal ? 448 : 400;
        if (sony_tex[k])
            plat_draw_texture(sony_tex[k], FX(sx - sony_w / 2), FY(sy - sony_h / 2 - 4), sony_w * 2.25f, sony_h * 1080.0f / LINES,
                              0xffffffffu, true);
        if (sce_tex[k])
            plat_draw_texture(sce_tex[k], FX(ex - sce_w / 2), FY(ey - sce_h / 2 - 4), sce_w * 2.25f, sce_h * 1080.0f / LINES,
                              0xffffffffu, true);
        if (tm24_tex[k])
            plat_draw_texture(tm24_tex[k], FX(436 - 12), FY(417 - 5), 24 * 2.25f, 10 * 1080.0f / LINES, 0xffffffffu, true);
    }
}

/* ------------------------------------------------------------------ the opening */

/* Frames of the real power-on (PCSX-ReARMed, SCPH-7002, Silent Hill (Europe), 50 Hz; the same counts at 60 Hz):
 * black, the drones from 56, the Sony screen's grey ramp from 72, emblem, words, hold; its picture stays while the
 * drive reads the disc, until 477; the PlayStation screen's grey fade (12 + 1 stale frame), the 3D logo from 490
 * (31), the wordmark ramp (30), the hold (21); then our fade to black (12) and the main menu fades in. Without the
 * logo model: the Sony screen, then the fade to black. */

static SDL_Thread *prep;
static SDL_atomic_t prep_done;
static int16_t *sound;
static size_t sound_frames;
static bool has_logo, prep_ok;
static int last_frame;

static int prepare_thread(void *arg)
{
    (void)arg;
    const uint64_t t_start = plat_ticks_us();
    prep_ok = load_bios() && load_bank();
    if (prep_ok)
    {
        has_logo = load_tmd();
        if (has_logo)
            setup_gte();
        sound = render_sound(has_logo, &sound_frames);
    }
    SwanStationPS5_log("ps1 opening: prepared in %.0f ms (%s, logo %s, sound %d frames)",
                       (double)(plat_ticks_us() - t_start) / 1000.0, prep_ok ? "ok" : "no BIOS data", has_logo ? "yes" : "no",
                       (int)sound_frames);
    SDL_AtomicSet(&prep_done, 1);
    return 0;
}

static int sony_end(void)
{
    return F_FADE + (L.pal ? 50 : 60) + 2 + (L.pal ? 150 : 180);
}

/* main.c, as soon as the settings are read: the BIOS file and the notes, on a thread, while the app starts */
void ps1_opening_preload(void)
{
    if (prep)
        return;
    L.region = 0;
    L.pal = true;
    SDL_AtomicSet(&prep_done, 0);
    prep = SDL_CreateThread(prepare_thread, "opening", NULL);
    if (!prep)
        prepare_thread(NULL);
}

/* shelf v3: a BIOS was added after the start (the setup screen): the data and the notes made again from it */
void ps1_opening_retry(void)
{
    if (prep)
        SDL_WaitThread(prep, NULL);
    prep = NULL;
    free(sound);
    sound = NULL;
    sound_frames = 0;
    prep_ok = has_logo = false;
    L.retried = true; /* its black is not the app's start-up any more */
    ps1_opening_preload();
}

bool ps1_opening_begin(int region)
{
    L.active = false;
    /* the BIOS layout read here is v4.1 (SCPH-7002 / 7502 / 9002): a PAL console, so 50 Hz and the European licence
     * line, whatever the games' regions */
    (void)region;
    L.region = 0;
    L.pal = true;
    /* the BIOS file is read and the notes made on a thread (started early by ps1_opening_preload); the screen stays
     * black (as at power-on) meanwhile */
    if (!prep && !SDL_AtomicGet(&prep_done))
        ps1_opening_preload();
    L.active = true;
    L.played = false;
    L.t0 = 0;
    last_frame = -1;
    return true;
}

bool ps1_pslogo_active(void)
{
    return L.active;
}

static void finish(void)
{
    if (prep)
        SDL_WaitThread(prep, NULL);
    prep = NULL;
    L.active = false;
}

void ps1_pslogo_skip(void)
{
    if (L.active)
        sfx_stop_all();
    finish();
}

/* one frame of the opening; false once it is over (the shelf's menu then fades in) */
bool ps1_pslogo_frame(void)
{
    if (!L.active)
        return false;
    draw_rect(0, 0, 1920, 1080, 0xff000000u);
    if (!SDL_AtomicGet(&prep_done))
        return true; /* black, as the console before its picture */
    if (!L.t0)
    {
        if (prep)
            SDL_WaitThread(prep, NULL);
        prep = NULL;
        if (!prep_ok || !load_sony())
        {
            SwanStationPS5_log("ps1 opening: no BIOS graphics or sounds: straight to the menu");
            L.active = false;
            return false;
        }
        if (has_logo)
        {
            static uint16_t pix[200 * 40];
            if (!L.tm && tim4(TM_AT, 20, 8, pix))
                L.tm = tim_texture(pix, 20, 8, 128);
            if (!L.wordmark[0] && tim4(WORDMARK_AT, 200, 40, pix))
                for (int k = 0; k < 30; ++k)
                    L.wordmark[k] = tim_texture(pix, 200, 40, 5 * k);
        }
        /* the power-on's own black (frames before the first note) counts the app's start-up already spent, so the
         * screen isn't black twice as long (at most up to a few frames before the drones) */
        const double hz0 = L.pal ? 50.0 : 60.0, spent = L.retried ? 0.0 : SwanStationPS5_uptime(), most = (F_DRONES - 4) / hz0;
        L.skip_s = spent < most ? spent : most;
        L.t0 = plat_ticks_us() - (uint64_t)(L.skip_s * 1e6);
        SwanStationPS5_log("ps1 opening: %s, %s, %s; %.2f s of its black already spent starting", L.pal ? "50 Hz" : "60 Hz",
                           REGION_CODE[L.region], has_logo ? "with the PlayStation screen" : "the Sony screen only", L.skip_s);
    }
    const float hz = L.pal ? 50.0f : 60.0f;
    int f = (int)((float)(plat_ticks_us() - L.t0) / 1e6f * hz);
#if defined(SwanStationPS5_PREVIEW)
    {
        /* SwanStationPS5_OPENING_STEP=1: one opening frame per drawn frame, so scripted shots land on exact frames */
        static int stepped = -1;
        if (getenv("SwanStationPS5_OPENING_STEP"))
            f = ++stepped;
    }
#endif
    if (!L.played)
    {
        L.played = true;
        {
            /* the whole opening's sound (its first note at frame F_DRONES), from where the clock starts */
            const size_t skip = (size_t)(L.skip_s * SR);
            if (sound && skip < sound_frames)
                sfx_play_pcm(sound + skip * 2, sound_frames - skip);
        }
#if defined(SwanStationPS5_PREVIEW)
        if (getenv("SwanStationPS5_SHOTS") && sound)
        {
            /* the preview: the opening's sound as a WAV beside the screenshots (it starts at frame F_DRONES) */
            char path[600];
            snprintf(path, sizeof(path), "%s/opening-sound.wav", getenv("SwanStationPS5_SHOTS"));
            FILE *w = fopen(path, "wb");
            if (w)
            {
                const uint32_t bytes = (uint32_t)(sound_frames * 4), rate = SR, byte_rate = SR * 4, chunk = 36 + bytes, sixteen = 16;
                const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
                fwrite("RIFF", 1, 4, w), fwrite(&chunk, 4, 1, w), fwrite("WAVEfmt ", 1, 8, w), fwrite(&sixteen, 4, 1, w);
                fwrite(&pcm, 2, 1, w), fwrite(&ch, 2, 1, w), fwrite(&rate, 4, 1, w), fwrite(&byte_rate, 4, 1, w);
                fwrite(&align, 2, 1, w), fwrite(&bits, 2, 1, w), fwrite("data", 1, 4, w), fwrite(&bytes, 4, 1, w);
                fwrite(sound, 4, sound_frames, w);
                fclose(w);
            }
        }
#endif
    }
#if defined(SwanStationPS5_PREVIEW)
    if (f != last_frame)
        SwanStationPS5_log("ps1 opening: frame %d", f);
#endif
    last_frame = f;
    const int end_sony = sony_end();
    if (f < F_FADE)
        return true;
    if (!has_logo)
    {
        if (f >= end_sony + PH_OUT)
        {
            finish();
            return false;
        }
        draw_sony(f - F_FADE);
        if (f >= end_sony)
            draw_rect(0, 0, 1920, 1080, (uint32_t)(255 * (f - end_sony + 1) / PH_OUT) << 24);
        return true;
    }
    if (f < F_PHASE_A)
    {
        draw_sony(f < end_sony ? f - F_FADE : end_sony - 1 - F_FADE); /* held while the drive reads the disc */
        return true;
    }
    if (f < F_PHASE_B)
    {
        /* a: the clear colour 180 -> 0 over 12 ticks, then the stale grey frame */
        const int t = f - F_PHASE_A, g = 180 * (12 - (t < 12 ? t : 10)) / 12 >> 3; /* the stale frame: tick 10's */
        draw_rect(240, 0, 1440, FY(506), 0xff000000u | 0x010101u * c5to8(g));
        return true;
    }
    const int tick = f - F_PHASE_B;
    if (tick >= PH_B + PH_C + PH_D + PH_OUT)
    {
        finish();
        return false;
    }
    draw_logo(60 + (tick < PH_B ? tick : PH_B - 1));
    if (tick >= PH_B)
    {
        const int k = tick - PH_B < PH_C ? tick - PH_B : PH_C - 1;
        if (L.wordmark[k])
            plat_draw_texture(L.wordmark[k], SX(328 - 100), SY(290 - 20 + 2), 200 * 2.25f, 40 * YS, 0xffffffffu, true);
        draw_licence();
    }
    if (tick >= PH_B + PH_C + PH_D)
        draw_rect(0, 0, 1920, 1080, (uint32_t)(255 * (tick - PH_B - PH_C - PH_D + 1) / PH_OUT) << 24);
    return true;
}
