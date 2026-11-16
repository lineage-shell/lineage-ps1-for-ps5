/*
 * SwanStationPS5 - what the PS1 shelf v3 reads from the user's own BIOS file at runtime: the ROM font (the shell's
 * info-bar and licence-text face). Nothing from the BIOS is shipped; without a BIOS the kit uses its fallback face.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every retail PS1 BIOS (512 KB) carries the same 16 x 15 1-bit font at 0x66000, 30 bytes a glyph, in JIS order
 * (only the valid Shift-JIS cells, packed): the full-width digits start at 0x93, A-Z at 0x9d, a-z at 0xb7, and the
 * symbols of row 0x81 at their Shift-JIS offset (minus one past 0x817f, which Shift-JIS skips).
 */
#include "ps1_kit.h"

#include "../app.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BIOS_SIZE (512 * 1024)
#define FONT_AT 0x66000
#define GLYPH_BYTES 30

static struct
{
    bool tried, ok;
    uint16_t rows[95][16]; /* ASCII 32..126 */
    uint8_t adv[95];
} F;

static int sjis_cell(int sj)
{
    return sj <= 0x817e ? sj - 0x8140 : sj - 0x8141;
}

/* the font cell of an ASCII character, -1 when the ROM has none */
static int cell_of(int c)
{
    if (c >= '0' && c <= '9')
        return 0x93 + c - '0';
    if (c >= 'A' && c <= 'Z')
        return 0x9d + c - 'A';
    if (c >= 'a' && c <= 'z')
        return 0xb7 + c - 'a';
    static const struct
    {
        char c;
        int sj;
    } SYM[] = {{' ', 0x8140}, {',', 0x8143}, {'.', 0x8144}, {':', 0x8146}, {';', 0x8147}, {'?', 0x8148},
               {'!', 0x8149}, {'^', 0x814f}, {'_', 0x8151}, {'-', 0x815d}, {'/', 0x815e}, {'\\', 0x815f},
               {'~', 0x8160}, {'|', 0x8162}, {'\'', 0x8166}, {'"', 0x8168}, {'(', 0x8169}, {')', 0x816a},
               {'[', 0x816d}, {']', 0x816e}, {'{', 0x816f}, {'}', 0x8170}, {'+', 0x817b}, {'=', 0x8181},
               {'<', 0x8183}, {'>', 0x8184}, {'$', 0x8190}, {'%', 0x8193}, {'#', 0x8194}, {'&', 0x8195},
               {'*', 0x8196}, {'@', 0x8197}};
    for (size_t i = 0; i < sizeof(SYM) / sizeof(SYM[0]); ++i)
        if (SYM[i].c == c)
            return sjis_cell(SYM[i].sj);
    return -1;
}

static bool load_from(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size != BIOS_SIZE)
        return false;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    uint8_t *rom = malloc(BIOS_SIZE);
    const bool read = rom && fread(rom, 1, BIOS_SIZE, f) == BIOS_SIZE;
    fclose(f);
    if (!read)
    {
        free(rom);
        return false;
    }
    for (int c = 32; c < 127; ++c)
    {
        uint16_t *r = F.rows[c - 32];
        memset(r, 0, sizeof(F.rows[0]));
        const int cell = cell_of(c);
        int lo = 16, hi = -1;
        if (cell >= 0)
        {
            const uint8_t *g = rom + FONT_AT + cell * GLYPH_BYTES;
            for (int y = 0; y < 15; ++y)
            {
                uint16_t bits = (uint16_t)(g[2 * y] << 8 | g[2 * y + 1]);
                bits |= bits >> 1; /* the shell widens each glyph by one pixel */
                r[y] = bits;
                for (int x = 0; x < 16; ++x)
                    if (bits & (0x8000u >> x))
                    {
                        lo = x < lo ? x : lo;
                        hi = x > hi ? x : hi;
                    }
            }
        }
        if (hi < 0)
            F.adv[c - 32] = 8; /* a space (or a character the ROM lacks) */
        else
        {
            /* packed from the left, two clear pixels after the (widened) glyph, as the shell spaces its lines */
            for (int y = 0; y < 16; ++y)
                r[y] = (uint16_t)(r[y] << lo);
            const int w = hi - lo + 1;
            F.adv[c - 32] = (uint8_t)(w + 3 < 6 ? 6 : w + 3);
        }
    }
    free(rom);
    /* a sanity check: 'A' and '0' have pixels, the space has none */
    int a = 0, z = 0, sp = 0;
    for (int y = 0; y < 16; ++y)
        a += F.rows['A' - 32][y] != 0, z += F.rows['0' - 32][y] != 0, sp += F.rows[0][y] != 0;
    return a >= 8 && z >= 8 && sp == 0;
}

static void load(void)
{
    F.tried = true;
    F.ok = false;
    DIR *d = opendir(app.paths.bios);
    if (!d)
        return;
    struct dirent *e;
    char path[SwanStationPS5_PATH_MAX];
    while (!F.ok && (e = readdir(d)))
    {
        if (str_icmp(path_ext(e->d_name), "bin") != 0)
            continue;
        path_join(path, sizeof(path), app.paths.bios, e->d_name);
        F.ok = load_from(path);
        if (F.ok)
            SwanStationPS5_log("ps1 shelf: the ROM font from %s", e->d_name);
    }
    closedir(d);
    if (!F.ok)
        SwanStationPS5_log("ps1 shelf: no BIOS with the ROM font in %s (the fallback face is used)", app.paths.bios);
}

bool ps1_bios_font_ready(void)
{
    if (!F.tried)
        load();
    return F.ok;
}

bool ps1_bios_glyph(int ch, uint16_t rows[16], int *advance)
{
    if (!ps1_bios_font_ready() || ch < 32 || ch > 126)
        return false;
    memcpy(rows, F.rows[ch - 32], sizeof(F.rows[0]));
    *advance = F.adv[ch - 32];
    return true;
}

void ps1_bios_font_reload(void)
{
    F.tried = false;
}
