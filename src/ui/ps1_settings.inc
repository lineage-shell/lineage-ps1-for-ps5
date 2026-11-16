/*
 * SwanStationPS5 - Settings in the PS1 shelf v3: the CD Player's button plates moved to the top row as the sections
 * (L1 / R1), the rows as label words with their values in the shell's vocabulary: levels on the CD track grid, choices
 * as mode plates (chosen full, others at brightness 64), switches as the memory card's YES / NO spheres as ON / OFF,
 * the help in the info bar (wrapped to two or three lines, a longer one scrolls a line at a time). Per game: a gold dot
 * (the CD colon dot) marks a row this game sets differently; Square puts the row back to the global value, and the RESET
 * TO GLOBAL plate under the rows puts every row back after the memory card's "Are you sure?" (NO first).
 * Screen shape 4:3: the rows and the info bar are laid out again in the console's 640 units, three sections shown.
 * Included by settings_screen.c (its rows, tabs and state S). Design: design notes ps1/new-ux 05a-05e.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ps1_kit.h"

#define PS1S_ROWS 6     /* rows shown at once */
#define PS1S_Y0 132.0f
#define PS1S_DY 46.0f
#define PS1S_VX ps1k_wx(270.0f) /* where the values start */
#define PS1S_LX ps1k_wx(-60.0f) /* the labels */

/* the per-game reset plate, its confirm, and the help's scroll */
static struct
{
    bool on_reset;    /* the arrow is on RESET TO GLOBAL (a row after the tab's last) */
    bool confirm;     /* "Are you sure?" showing */
    int confirm_sel;  /* 0 YES, 1 NO */
    int help_row, help_tab;
    uint64_t help_us, seen_us;
} PS = {.help_row = -1};

static void ps1_settings_backdrop(void)
{
    ps1k_background();
}

static bool ps1_per_game(void)
{
    return S.game_scope && scope_game();
}

/* Hook (ps1_shelf.inc): Settings opened from a game's page edit that game's own settings */
void ps1_settings_game_scope(void)
{
    if (!scope_game())
        return;
    app.game_has_own = true; /* starts as a copy of the console-wide ones, as "This game" does */
    S.game_scope = true;
}

/* this game sets the row differently from all games */
static bool ps1_overridden(const Row *r)
{
    if (!S.game_scope || !scope_game() || r->global_only || r->offset < 0 || r->special != SP_NONE)
        return false;
    const char *a = (const char *)&app.settings + r->offset, *b = (const char *)&app.global + r->offset;
    return r->is_bool ? *(const bool *)a != *(const bool *)b : *(const int *)a != *(const int *)b;
}

/* the title plate: SETTINGS, or the game's name when this game's own settings are edited */
static void ps1_settings_header(void)
{
    char caps[96];
    if (S.game_scope && scope_game())
    {
        ps1_caps(shelf_game_title(scope_game()), caps, sizeof(caps));
        if (strlen(caps) > 20)
            caps[20] = '\0';
    }
    else
        ps1_caps(tr(S.remap ? "Button mapping" : "Settings"), caps, sizeof(caps));
    /* a long name gets smaller letters, so the plate stays clear of R1 */
    float cap = 20;
    while (cap > 10 && ps1k_word_width(caps, cap) + 36 > 200)
        cap -= 1;
    ps1k_plate(caps, ps1k_narrow() ? 632 : 700, 67, cap);
}

/* the sections: four plates at a time on the top row, L1 / R1 either side; the shown one grows 1.25x */
static void ps1_settings_tabs(void)
{
    /* 4:3: three narrower plates at a time, so L1, R1 and the title plate stay inside 0..640 */
    const bool narrow = ps1k_narrow();
    const int shown = narrow ? 3 : 4;
    int first = S.tab - 1;
    first = first > TAB_COUNT - shown ? TAB_COUNT - shown : first;
    first = first < 0 ? 0 : first;
    const float x0 = narrow ? 92 : 10, pitch = narrow ? 104 : 124, w = narrow ? 88 : 100;
    ps1k_label_sphere("L1", narrow ? 16 : -86, 67, 30, 10);
    for (int k = 0; k < shown && first + k < TAB_COUNT; ++k)
    {
        const int i = first + k;
        const float x = x0 + k * pitch;
        char caps[48];
        ps1_caps(tr(TABS[i].name), caps, sizeof(caps));
        const bool on = i == S.tab;
        float cap = 11;
        while (cap > 7 && ps1k_word_width(caps, cap * (on ? 1.25f : 1)) > (on ? w * 1.18f : w - 6))
            cap -= 0.5f;
        if (on)
        {
            ps1k_plaque(x, 67, w * 1.25f, 30, false, 128);
            ps1k_word(caps, x, 67, cap * 1.25f, PS1K_CENTRE, 128);
        }
        else
        {
            ps1k_plaque(x, 67, w, 24, false, 64);
            ps1k_word(caps, x, 67, cap, PS1K_CENTRE, 64);
        }
    }
    ps1k_label_sphere("R1", narrow ? 374 : 458, 67, 30, 10);
}

/* levels: one 8-unit dot a step, the current step 34 with its number */
static void ps1_level(float x, float y, int n, int v)
{
    const float pitch = ps1k_narrow() ? 22 : 30;
    for (int i = 0; i < n; ++i)
    {
        const float xx = x + i * pitch;
        const int c = PS1K_CD_ROWS[i % 20 / 5][i % 5];
        if (i == v)
        {
            char num[8];
            snprintf(num, sizeof(num), "%d", v + 1);
            ps1k_sphere(c, xx, y, 34, 128);
            ps1k_digits(num, xx, y, 0.85f, 128);
        }
        else
            ps1k_sphere(c, xx, y, 8, 128);
    }
}

/* a choice's plates, chosen full and the others at 64; false when they don't fit (one plate with the value instead) */
static bool ps1_choice_plates(const Row *r, float y, bool dim)
{
    if (!r->labels || r->count < 2 || r->count > 3 || r->special != SP_NONE)
        return false;
    char caps[3][64];
    float w = 92;
    for (int i = 0; i < r->count; ++i)
    {
        ps1_caps(tr(r->labels[i]), caps[i], sizeof(caps[i]));
        w = fmaxf(w, ps1k_word_width(caps[i], 11) + 16);
    }
    if (w > 140 || PS1S_VX - 20 + r->count * (w + 12) > ps1k_right() - 8)
        return false; /* (4:3: three plates may not fit) */
    const int v = read_value(r);
    for (int i = 0; i < r->count; ++i)
    {
        const float cx = PS1S_VX - 20 + i * (w + 12) + w * 0.5f;
        const int b = i == v && !dim ? 128 : 64;
        ps1k_plaque(cx, y, w, 24, false, b);
        ps1k_word(caps[i], cx, y, 11, PS1K_CENTRE, b);
    }
    return true;
}

static void ps1_value_plate(const char *value, float y, bool arrows, bool dim)
{
    char caps[200];
    ps1_caps(value, caps, sizeof(caps));
    float cap = 11;
    const float most = fminf(400, ps1k_right() - (PS1S_VX - 20) - 24);
    while (cap > 7 && ps1k_word_width(caps, cap) > most - 20)
        cap -= 0.5f;
    const float w = fminf(most, fmaxf(92, ps1k_word_width(caps, cap) + 24)), cx = PS1S_VX - 20 + w * 0.5f;
    ps1k_plaque(cx, y, w, 24, false, dim ? 64 : 128);
    ps1k_word(caps, cx, y, cap, PS1K_CENTRE, dim ? 64 : 128);
    if (arrows)
    {
        /* the little triangles either side: left / right change it */
        const float ly = PS1K_Y(y), a = 5 * PS1K_S, lx = PS1K_X(PS1S_VX - 30), rx = PS1K_X(PS1S_VX - 20 + w + 10);
        const uint32_t c = 0xffffffffu;
        PlatVertex v[6] = {{lx, ly - a, 0, 0, c}, {lx - a * 1.4f, ly, 0, 0, c}, {lx, ly + a, 0, 0, c},
                           {rx, ly - a, 0, 0, c}, {rx + a * 1.4f, ly, 0, 0, c}, {rx, ly + a, 0, 0, c}};
        plat_draw_mesh(NULL, v, 6, NULL, 0);
    }
}

static void ps1_onoff_spheres(float y, bool on, bool dim)
{
    for (int i = 0; i < 2; ++i)
    {
        const bool sel = (i == 0) == on;
        const float x = PS1S_VX + i * 64;
        const int b = sel && !dim ? 128 : 64;
        ps1k_sphere(PS1K_BLUE, x, y, sel ? 40 : 34, b);
        ps1k_word(i ? "OFF" : "ON", x, y, sel ? 11 : 9, PS1K_CENTRE, b);
    }
}

static void ps1_settings_rows(void)
{
    const Tab *t = tab();
    const bool per_game = ps1_per_game();
    if (!per_game)
        PS.on_reset = false;
    /* the rows that are shown: the cursor's window (per game, the RESET TO GLOBAL plate is one row more) */
    const int count = t->count + (per_game ? 1 : 0), cur = PS.on_reset ? t->count : S.cursor;
    int top = (int)S.scroll;
    if (cur >= 0)
    {
        if (cur < top)
            top = cur;
        if (cur >= top + PS1S_ROWS)
            top = cur - PS1S_ROWS + 1;
    }
    if (top > count - PS1S_ROWS)
        top = count - PS1S_ROWS;
    if (top < 0)
        top = 0;
    S.scroll = (float)top;
    float cur_x = PS1S_LX + 40, cur_y = -1;
    const float band = ps1k_right() - ps1k_left() - 94, mid = (ps1k_left() + ps1k_right()) * 0.5f;
    for (int k = 0; k < PS1S_ROWS && top + k < count; ++k)
    {
        const int i = top + k;
        const float y = PS1S_Y0 + k * PS1S_DY;
        if (i == t->count)
        {
            /* RESET TO GLOBAL: a plate under the rows, like the game page's */
            const bool f = PS.on_reset;
            const float cx = PS1S_LX + 84, w = f ? 200 : 160;
            if (f)
                ps1k_plaque(mid, y, band, 38, true, 128), cur_y = y, cur_x = cx;
            ps1k_plaque(cx, y, w, f ? 30 : 24, false, 128);
            ps1k_word("RESET TO GLOBAL", cx, y, f ? 11 * 1.25f : 11, PS1K_CENTRE, 128);
            continue;
        }
        const Row *r = &t->rows[i];
        const bool f = i == S.cursor && !PS.on_reset, locked = row_locked(r);
        if (f)
        {
            ps1k_plaque(mid, y, band, 38, true, 128);
            cur_y = y;
        }
        if (per_game)
        {
            if (ps1_overridden(r))
                ps1k_sphere(PS1K_GOLD, PS1S_LX - 18, y, 14, 128);
            else
                ps1k_sphere(PS1K_BLUE, PS1S_LX - 18, y, 8, 64);
        }
        char caps[200], buf[SwanStationPS5_PATH_MAX + 64];
        ps1_caps(tr(row_name(r)), caps, sizeof(caps));
        float cap = f ? 13 * 1.25f : 13;
        const float label_room = PS1S_VX - 30 - PS1S_LX - 10;
        while (cap > 8 && ps1k_word_width(caps, cap) > label_room)
            cap -= 0.5f;
        const bool dim_label = (per_game && !f && !ps1_overridden(r)) || locked || r->kind == K_INFO;
        ps1k_word(caps, PS1S_LX, y, cap, PS1K_LEFT, dim_label && !f ? 96 : 128);
        if (locked)
        {
            ps1_value_plate(tr("Global setting."), y, false, true);
            continue;
        }
        switch (r->kind)
        {
        case K_TOGGLE:
            ps1_onoff_spheres(y, read_value(r) != 0, false);
            break;
        case K_SLIDER:
        {
            const int n = r->count > 1 ? r->count : 2;
            if (n <= 14)
                ps1_level(PS1S_VX, y, n, read_value(r));
            else
                ps1_value_plate(value_label(r, buf, sizeof(buf)), y, f, false);
            break;
        }
        case K_CHOICE:
            if (!ps1_choice_plates(r, y, false))
                ps1_value_plate(value_label(r, buf, sizeof(buf)), y, f, false);
            break;
        case K_ACTION:
        {
            const char *v = (r->special == SP_UPDATE || r->special == SP_USBFOLDER || r->special == SP_NFSHARES ||
                             r->special == SP_BIOSFOLDER)
                                ? value_label(r, buf, sizeof(buf))
                                : tr("Open");
            ps1_value_plate(v, y, false, !f);
            break;
        }
        default: /* K_INFO */
        {
            ps1_caps(value_label(r, buf, sizeof(buf)), caps, sizeof(caps));
            size_t n = strlen(caps);
            while (n > 4 && ps1k_rom_width(caps, 10) > ps1k_right() - (PS1S_VX - 20) - 12)
                caps[n - 3] = '.', caps[n - 2] = '.', caps[--n] = '\0';
            ps1k_rom(caps, PS1S_VX - 20, y, PS1K_LEFT, 10, 14);
            break;
        }
        }
    }
    if (config_imported() && !strcmp(t->name, "System"))
        ps1k_rom("SETTINGS IMPORTED FROM SWANSTATIONPS5", PS1S_LX, PS1S_Y0 - 24, PS1K_LEFT, 9, 13);
    if (cur_y >= 0)
        ps1k_cursor(cur_x, cur_y);
    /* more rows above or below: small dots, as the memory card's scroll marks */
    if (top > 0)
        ps1k_sphere(PS1K_BLUE, ps1k_wx(700), PS1S_Y0 - 22, 10, 128);
    if (top + PS1S_ROWS < count)
        ps1k_sphere(PS1K_BLUE, ps1k_wx(700), PS1S_Y0 + PS1S_ROWS * PS1S_DY - 24, 10, 128);
}

/* The help broken into lines of at most `room` units at glyph width gw; returns the count */
#define HELP_LINES 24
static int ps1_wrap(const char *text, float room, float gw, char lines[HELP_LINES][200])
{
    int n = 0;
    lines[0][0] = '\0';
    for (const char *s = text; *s && n < HELP_LINES;)
    {
        while (*s == ' ')
            ++s;
        if (!*s)
            break;
        const char *sp = strchr(s, ' ');
        const int len = sp ? (int)(sp - s) : (int)strlen(s);
        char trial[420];
        snprintf(trial, sizeof(trial), "%s%s%.*s", lines[n], lines[n][0] ? " " : "", len, s);
        if (lines[n][0] && ps1k_rom_width(trial, gw) > room)
        {
            if (++n == HELP_LINES)
                break;
            snprintf(trial, sizeof(trial), "%.*s", len, s);
        }
        str_copy(lines[n], 200, trial);
        s += len;
    }
    return lines[n][0] ? n + 1 : n;
}

/* The help in the info bar: one line in the big ROM size when it fits, else two or three smaller lines; longer help
 * scrolls up a line at a time (a pause at the start and the end), so nothing is cut */
static void ps1_settings_bar(const Row *r)
{
    const float x0 = ps1k_wx(-60), x1 = ps1k_wx(700), y = 411;
    char help[600] = "", right[64] = "";
    if (PS.on_reset)
        str_copy(help, sizeof(help), "EVERY SETTING OF THIS GAME BACK TO THE GLOBAL ONE (THE GOLD DOTS GO).");
    else if (r)
        ps1_caps(tr(row_help(r)), help, sizeof(help));
    if (ps1_per_game())
        str_copy(right, sizeof(right), r && !PS.on_reset && ps1_overridden(r) ? "SQUARE: GLOBAL" : "GOLD DOT: THIS GAME");
    else if (scope_game())
        str_copy(right, sizeof(right), "L2: THIS GAME");
    /* no button names in braces in the ROM font */
    for (char *p = help; *p; ++p)
        if (*p == '{' || *p == '}')
            *p = ' ';
    const float rw = right[0] ? ps1k_rom_width(right, 10) + 24 : 0;
    if (ps1k_rom_width(help, 16) <= x1 - x0 - 48 - rw)
    {
        ps1k_info_bar(help, NULL, x0, x1, y);
        if (right[0])
            ps1k_rom(right, x1 - 16, y, PS1K_RIGHT, 10, 14);
        return;
    }
    ps1k_info_bar(NULL, NULL, x0, x1, y);
    if (right[0])
        ps1k_rom(right, x1 - 16, y, PS1K_RIGHT, 10, 14);
    const float room = x1 - x0 - 40 - rw;
    static char lines[HELP_LINES][200];
    int n = ps1_wrap(help, room, 9, lines);
    if (n <= 2)
    {
        if (n == 1)
            ps1k_rom(lines[0], x0 + 16, y, PS1K_LEFT, 9, 13);
        else
        {
            ps1k_rom(lines[0], x0 + 16, y - 8, PS1K_LEFT, 9, 13);
            ps1k_rom(lines[1], x0 + 16, y + 8, PS1K_LEFT, 9, 13);
        }
        return;
    }
    n = ps1_wrap(help, room, 8, lines);
    int first = 0;
    if (n > 3)
    {
        /* three lines at a time: 2.5 s on the first ones, then a line every 1.6 s, 2.5 s on the last, again */
        const int row = PS.on_reset ? -2 : S.cursor;
        const uint64_t now = plat_ticks_us();
        if (row != PS.help_row || S.tab != PS.help_tab)
            PS.help_row = row, PS.help_tab = S.tab, PS.help_us = now;
        const float t = (float)(now - PS.help_us) / 1e6f, steps = (float)(n - 3), cycle = 2.5f + steps * 1.6f + 2.5f;
        const float u = fmodf(t, cycle);
        first = u < 2.5f ? 0 : u >= 2.5f + steps * 1.6f ? n - 3 : 1 + (int)((u - 2.5f) / 1.6f);
        first = first > n - 3 ? n - 3 : first;
        /* the scroll marks: more above / below */
        if (first > 0)
            ps1k_sphere(PS1K_BLUE, x1 - rw - 10, y - 10, 6, 128);
        if (first + 3 < n)
            ps1k_sphere(PS1K_BLUE, x1 - rw - 10, y + 10, 6, 128);
    }
    for (int k = 0; k < 3 && first + k < n; ++k)
        ps1k_rom(lines[first + k], x0 + 16, y - 10 + k * 10, PS1K_LEFT, 8, 10);
}

/* ---------------------------------------------------------------- per game: back to the global values */

static void ps1_reset_row(const Row *r)
{
    if (!ps1_overridden(r))
        return;
    char *a = (char *)&app.settings + r->offset;
    const char *b = (const char *)&app.global + r->offset;
    if (r->is_bool)
        *(bool *)a = *(const bool *)b;
    else
        *(int *)a = *(const int *)b;
}

static void ps1_apply_game(void)
{
    if (app.game)
    {
        host_apply_settings(&app.settings);
        controls_reconfigure(app.game, &app.settings);
    }
}

/* the selectable row after (or before) the cursor; -1 at the end */
static int ps1_next_row(int from, int step)
{
    const Tab *t = tab();
    for (int c = from + step; c >= 0 && c < t->count; c += step)
        if (selectable(&t->rows[c]))
            return c;
    return -1;
}

/* Hook (settings_screen.c, shelf v3): the arrows on the rows (no wrapping, the sound rule), Square and the reset plate.
 * Returns the buttons left for settings_screen.c. */
static uint32_t ps1_settings_input(uint32_t pressed)
{
    const uint64_t now = plat_ticks_us();
    if (now - PS.seen_us > 250000)
        PS.on_reset = PS.confirm = false; /* Settings opened again */
    PS.seen_us = now;
    const bool per_game = ps1_per_game();
    if (PS.confirm)
    {
        const int was = PS.confirm_sel;
        if (pressed & BIT(BTN_LEFT))
            PS.confirm_sel = 0;
        if (pressed & BIT(BTN_RIGHT))
            PS.confirm_sel = 1;
        ps1k_move_sound(PS.confirm_sel != was, pressed, BIT(BTN_LEFT) | BIT(BTN_RIGHT));
        if (pressed & BIT(BTN_CROSS))
        {
            PS.confirm = false;
            if (PS.confirm_sel == 0)
            {
                ensure_rows();
                for (int ti = 0; ti < TAB_COUNT; ++ti)
                    for (int k = 0; k < TABS[ti].count; ++k)
                        ps1_reset_row(&TABS[ti].rows[k]);
                ps1_apply_game();
                app_toast("This game uses the global settings again");
                sfx_play(SFX_SELECT);
            }
            else
                sfx_play(SFX_BACK);
        }
        else if (pressed & BIT(BTN_CIRCLE))
        {
            PS.confirm = false;
            sfx_play(SFX_BACK);
        }
        return 0;
    }
    if (!per_game || (pressed & (BIT(BTN_L1) | BIT(BTN_R1))))
        PS.on_reset = false;
    const uint32_t vert = BIT(BTN_UP) | BIT(BTN_DOWN);
    if ((pressed & vert) && S.cursor >= 0)
    {
        const int was = S.cursor;
        const bool was_reset = PS.on_reset;
        if (pressed & BIT(BTN_DOWN))
        {
            const int c = PS.on_reset ? -1 : ps1_next_row(S.cursor, 1);
            if (c >= 0)
                S.cursor = c;
            else if (per_game)
                PS.on_reset = true;
        }
        else if (PS.on_reset)
            PS.on_reset = false; /* back on the last row (the cursor stayed there) */
        else
        {
            const int c = ps1_next_row(S.cursor, -1);
            if (c >= 0)
                S.cursor = c;
        }
        ps1k_move_sound(S.cursor != was || PS.on_reset != was_reset, pressed, vert);
    }
    pressed &= ~vert;
    if (PS.on_reset)
    {
        if (pressed & BIT(BTN_CROSS))
        {
            PS.confirm = true;
            PS.confirm_sel = 1; /* NO first */
            sfx_play(SFX_OPEN);
        }
        return pressed & ~(BIT(BTN_CROSS) | BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_TRIANGLE) | BIT(BTN_SQUARE));
    }
    if (pressed & BIT(BTN_SQUARE))
    {
        /* Square: this row back to the global value (the gold dot goes) */
        const Row *r = S.cursor >= 0 ? &tab()->rows[S.cursor] : NULL;
        if (per_game && r && ps1_overridden(r))
        {
            ps1_reset_row(r);
            ps1_apply_game();
            sfx_play(SFX_BACK);
        }
        else
            sfx_play(SFX_ERROR);
    }
    return pressed & ~BIT(BTN_SQUARE);
}

/* the memory card's dialog: RESET TO GLOBAL / Are you sure? / YES NO */
static void ps1_settings_confirm(void)
{
    ps1k_plaque(320, 240, 260, 120, false, 128);
    ps1k_word("RESET TO GLOBAL", 320, 200, 15, PS1K_CENTRE, 128);
    ps1k_word("Are you sure?", 320, 228, 14, PS1K_CENTRE, 128);
    static const char *const YN[2] = {"YES", "NO"};
    for (int i = 0; i < 2; ++i)
    {
        const float x = i ? 367 : 273;
        ps1k_sphere(PS1K_BLUE, x, 266, 40, 128);
        ps1k_word(YN[i], x, 266, 12, PS1K_CENTRE, 128);
    }
    ps1k_cursor(PS.confirm_sel ? 367 : 273, 266);
}

static void ps1_settings_body(const Row *r)
{
    ps1_settings_rows();
    ps1_settings_bar(r);
    if (PS.confirm)
        ps1_settings_confirm();
    ps1k_screen_fade(2000);
}
