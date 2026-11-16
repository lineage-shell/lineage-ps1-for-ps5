/*
 * SwanStationPS5 - the in-game menu in the PS1 shelf v3: the CD Player / memory card shell. The paused game in the game
 * page's box (left), this game's 20 save-state slots as the CD track grid under it (the chosen slot big with its number,
 * L1 / R1 move it), RESUME on a splatter, the other actions as plates two to a row, QUIT TO SHELF on a dim splatter at
 * the bottom, GAME MENU in the title plate and what the arrow is on in the info bar. Slots are numbered 1-20 as on the
 * game page. Included by menu_screen.c (its state M and helpers). Design: design notes ps1/new-ux.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ps1_kit.h"

/* the menu's items laid out: row 0 RESUME, then the shown actions two to a row, then QUIT */
typedef struct
{
    int item[16][2];
    int cols[16];
    int rows;
} Ps1MenuGrid;

static void ps1_menu_grid(Ps1MenuGrid *g)
{
    memset(g, 0, sizeof(*g));
    g->item[0][0] = MI_RESUME;
    g->cols[0] = 1;
    int row = 1, col = 0;
    for (int i = MI_SAVE; i < MI_QUIT; ++i)
    {
        if (!item_shown(i))
            continue;
        g->item[row][col] = i;
        g->cols[row] = col + 1;
        if (++col == 2)
            col = 0, ++row;
    }
    if (col)
        ++row;
    g->item[row][0] = MI_QUIT;
    g->cols[row] = 1;
    g->rows = row + 1;
}

static void ps1_menu_where(const Ps1MenuGrid *g, int item, int *row, int *col)
{
    for (int r = 0; r < g->rows; ++r)
        for (int c = 0; c < g->cols[r]; ++c)
            if (g->item[r][c] == item)
            {
                *row = r, *col = c;
                return;
            }
    *row = 0, *col = 0;
}

/* The arrows move on the grid (no wrapping: at an edge it stays, silent when held); left / right change the value of
 * DISC and AUTO-SAVES (as on the list), L1 / R1 the state slot (1-20). Returns what is left for the menu's own handling
 * (X, O, the touchpad). */
static uint32_t ps1_menu_input(uint32_t pressed)
{
    Ps1MenuGrid g;
    ps1_menu_grid(&g);
    if (!item_shown(M.cursor))
        M.cursor = MI_RESUME;
    int row, col;
    ps1_menu_where(&g, M.cursor, &row, &col);
    const int before = M.cursor;
    if ((pressed & BIT(BTN_UP)) && row > 0)
        --row;
    else if ((pressed & BIT(BTN_DOWN)) && row < g.rows - 1)
        ++row;
    if (col >= g.cols[row])
        col = g.cols[row] - 1;
    M.cursor = g.item[row][col];
    const bool has_value = (M.cursor == MI_DISC && host_disc_count() > 1) || M.cursor == MI_AUTO;
    uint32_t keep = pressed & ~(BIT(BTN_UP) | BIT(BTN_DOWN) | BIT(BTN_L1) | BIT(BTN_R1));
    uint32_t dirs = BIT(BTN_UP) | BIT(BTN_DOWN);
    if ((pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT))) && !has_value)
    {
        if ((pressed & BIT(BTN_LEFT)) && col == 1)
            M.cursor = g.item[row][0];
        if ((pressed & BIT(BTN_RIGHT)) && col == 0 && g.cols[row] > 1)
            M.cursor = g.item[row][1];
        keep &= ~(BIT(BTN_LEFT) | BIT(BTN_RIGHT));
        dirs |= BIT(BTN_LEFT) | BIT(BTN_RIGHT);
    }
    ps1k_move_sound(M.cursor != before, pressed, dirs);
    if (pressed & (BIT(BTN_L1) | BIT(BTN_R1)))
    {
        const int was = app.settings.state_slot;
        int s = was + ((pressed & BIT(BTN_R1)) ? 1 : -1);
        s = s < 0 ? 0 : s >= MENU_SLOTS ? MENU_SLOTS - 1 : s;
        app.settings.state_slot = s;
        app.global.state_slot = s;
        ps1k_move_sound(s != was, pressed, BIT(BTN_L1) | BIT(BTN_R1));
    }
    return keep;
}

static const char *ps1_menu_word(int item, char *out, size_t size)
{
    static const char *const NAMES[MI_COUNT] = {"RESUME", "SAVE STATE", "LOAD STATE", "AUTO-SAVES", "DISC", "CHEATS",
                                                "ACHIEVEMENTS", "MANUAL", "GUIDE", "SETTINGS", "RESET", "QUIT TO SHELF"};
    if (item == MI_DISC && host_disc_count() > 1)
        snprintf(out, size, "DISC %d/%d", M.disc_choice + 1, host_disc_count());
    else
        str_copy(out, size, NAMES[item]);
    return out;
}

static void ps1_menu_draw(int autos, const long *auto_ages)
{
    M.open_t = fminf(M.open_t + app.dt * 7.0f, 1.0f);
    ps1k_background();
    ps1k_plate("GAME MENU", ps1k_ox(626), 67, 20);
    /* the time and how long it has been played, the controllers' batteries: ROM font, top left */
    {
        char clock_text[32], line[160];
        plat_clock(clock_text, sizeof(clock_text));
        const int minutes = (int)(app.play_seconds / 60.0);
        snprintf(line, sizeof(line), "%s  PLAYING %d:%02d", clock_text, minutes / 60, minutes % 60);
        for (int k = 0; k < SwanStationPS5_MAX_PADS; ++k)
        {
            const int level = app.pads[k].connected ? plat_pad_battery(k) : -1;
            if (level >= 0)
                snprintf(line + strlen(line), sizeof(line) - strlen(line), "  P%d %d%%", k + 1, level);
        }
        char caps[200];
        ps1k_rom(ps1_caps(line, caps, sizeof(caps)), (ps1k_narrow() ? 132 : ps1k_ox(140)) - 112, 67, PS1K_LEFT, 10, 14);
    }
    /* the box: the paused game (LOAD STATE on a used slot: that slot's picture) */
    const bool narrow = ps1k_narrow();
    const float BX = narrow ? 132 : ps1k_ox(140), BY = 178, BW = 224, BH = 168;
    ps1k_plaque(BX - BW * 0.5f - 6, BY, 2, BH + 12, false, 128);
    ps1k_plaque(BX + BW * 0.5f + 6, BY, 2, BH + 12, false, 128);
    ps1k_plaque(BX, BY - BH * 0.5f - 6, BW + 14, 2, false, 128);
    ps1k_plaque(BX, BY + BH * 0.5f + 6, BW + 14, 2, false, 128);
    const int slot = app.settings.state_slot;
    char age[48];
    slot_age(slot, age, sizeof(age));
    PlatTexture *thumb = M.cursor == MI_LOAD && age[0] ? slot_thumb(slot) : NULL;
    const float px = PS1K_X(BX - BW * 0.5f), py = PS1K_Y(BY - BH * 0.5f);
    draw_rect(px, py, BW * PS1K_S, BH * PS1K_S, 0xff000000u);
    if (thumb)
        ps1k_image(thumb, BX, BY, BW, BH, 128, false);
    else
        app_draw_game_in(px, py, BW * PS1K_S, BH * PS1K_S);
    /* the 20 slots: the CD track grid; empty 8, used 24 with the number, the chosen one 34 lit */
    for (int s = 0; s < MENU_SLOTS; ++s)
    {
        const int r = s / 5, c = s % 5, colr = PS1K_CD_ROWS[r][c];
        const float x = BX + (c - 2) * 40, y = 290 + r * 27;
        char a[48], num[4];
        slot_age(s, a, sizeof(a));
        snprintf(num, sizeof(num), "%d", s + 1);
        if (s == slot)
        {
            ps1k_sphere(colr, x, y, 34, 176);
            ps1k_digits(num, x, y, 0.9f, 128);
        }
        else if (a[0])
        {
            ps1k_sphere(colr, x, y, 24, 128);
            ps1k_digits(num, x, y, 0.62f, 128);
        }
        else
            ps1k_sphere(colr, x, y, 8, 128);
    }
    ps1k_label_sphere("L1", BX - 118, 330, 26, 9);
    ps1k_label_sphere("R1", BX + 118, 330, 26, 9);
    /* the actions */
    Ps1MenuGrid g;
    ps1_menu_grid(&g);
    const float C0 = narrow ? 322 : 304, C1 = ps1k_ox(452), PW = narrow ? 104 : 128, mid = (C0 + C1) * 0.5f;
    float cur_x = 0, cur_y = 0, y = 166;
    char word[64];
    for (int r = 0; r < g.rows; ++r)
    {
        for (int c = 0; c < g.cols[r]; ++c)
        {
            const int item = g.item[r][c];
            const bool sel = item == M.cursor;
            ps1_menu_word(item, word, sizeof(word));
            if (item == MI_RESUME || item == MI_QUIT)
            {
                /* splatters: RESUME at the top, QUIT TO SHELF dim at the bottom so it is not chosen by accident */
                const float sy = item == MI_RESUME ? 120 : y + 10, sw = item == MI_RESUME ? 240 : 220;
                ps1k_splat(item == MI_RESUME ? 3 : 4, mid, sy, sw, item == MI_RESUME ? 52 : 46, 128);
                const float cap = item == MI_RESUME ? 20 : 14;
                ps1k_word(word, mid, sy + 1, sel ? cap * 1.25f : cap, PS1K_CENTRE, sel || item == MI_RESUME ? 128 : 64);
                if (sel)
                    cur_x = mid, cur_y = sy;
                continue;
            }
            const bool dim = (item == MI_LOAD && ra_hardcore()) || (item == MI_CHEATS && ra_hardcore());
            const float x = c ? C1 : C0, k = sel ? 1.25f : 1.0f;
            float cap = 11;
            while (cap > 7 && ps1k_word_width(word, cap) > PW - 10)
                cap -= 0.5f;
            ps1k_plaque(x, y, PW * k, 26 * k, false, dim ? 64 : 128);
            ps1k_word(word, x, y, cap * k, PS1K_CENTRE, dim ? 64 : 128);
            if (sel)
                cur_x = x, cur_y = y;
        }
        if (g.item[r][0] != MI_RESUME)
            y += 36;
    }
    /* the info bar: what the arrow is on; the chosen slot on the right */
    char left[200] = "", right[48], caps[200];
    switch (M.cursor)
    {
    case MI_SAVE:
    case MI_LOAD:
    {
        const char *note = age[0] ? slot_note(slot) : "";
        snprintf(left, sizeof(left), "%s", age[0] ? (note[0] ? note : age) : "EMPTY");
        break;
    }
    case MI_AUTO:
        if (autos > 0)
        {
            const long ago = auto_ages[M.auto_choice];
            if (ago < 3600)
                snprintf(left, sizeof(left), "AUTO-SAVE %ld MIN AGO", ago / 60);
            else if (ago < 86400)
                snprintf(left, sizeof(left), "AUTO-SAVE %ld H AGO", ago / 3600);
            else
                snprintf(left, sizeof(left), "AUTO-SAVE %ld DAYS AGO", ago / 86400);
            if (autos > 1)
                snprintf(left + strlen(left), sizeof(left) - strlen(left), "  %d/%d", M.auto_choice + 1, autos);
        }
        else
            str_copy(left, sizeof(left), "NO AUTO-SAVES YET");
        break;
    case MI_DISC:
        str_copy(left, sizeof(left), "LEFT / RIGHT: THE OTHER DISC");
        break;
    case MI_CHEATS:
    {
        int on = 0;
        for (int k = 0; k < app.cheats.count; ++k)
            on += app.cheats.items[k].enabled;
        if (ra_hardcore())
            str_copy(left, sizeof(left), "CHEATS: OFF IN HARDCORE MODE");
        else
            snprintf(left, sizeof(left), "%d CHEATS, %d ON", app.cheats.count, on);
        break;
    }
    case MI_ACHIEVEMENTS:
    {
        int unlocked, total;
        if (ra_game_progress(&unlocked, &total))
            snprintf(left, sizeof(left), "%d OF %d UNLOCKED", unlocked, total);
        break;
    }
    default:
        break;
    }
    if (!left[0] && app.game)
        str_copy(left, sizeof(left), shelf_game_title(app.game));
    snprintf(right, sizeof(right), "SLOT %d", slot + 1);
    ps1k_info_bar(ps1_caps(left, caps, sizeof(caps)), right, ps1k_wx(-40), ps1k_wx(680), 411);
    if (cur_x != 0 || cur_y != 0)
        ps1k_cursor(cur_x, cur_y);
    ps1k_screen_fade(3000);
    app_draw_toast();
}
