/*
 * SwanStationPS5 - the memory card manager in the PS1 shelf v3: the console's own MEMORY CARD screen in the CD Player /
 * memory card shell vocabulary. The cards (one per game) as rows on the left (the chosen one on the 50 % band), the
 * EXIT / BACK UP / RESTORE / DELETE plates in the middle, the chosen card's fifteen blocks on the right as the original
 * 3 x 5 block lattice (8-unit dots, the saves' own animated icons), what the arrow is on in the info bar, and the
 * memory card's own "Are you sure?" with NO first. Included by memcards_screen.c (its cards C and state M).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ps1_kit.h"

#define MC_ROWS 7
#define MC_ROW_Y0 116.0f
#define MC_ROW_DY 38.0f

static float mc_list_l(void)
{
    return ps1k_wx(-60);
}

static float mc_list_r(void)
{
    return ps1k_wx(230);
}

static float mc_blocks_x(void)
{
    return ps1k_ox(510);
}

static void mc_trim(char *s, float room, float gw)
{
    size_t n = strlen(s);
    while (n > 4 && ps1k_rom_width(s, gw) > room)
        s[n - 3] = '.', s[n - 2] = '.', s[--n] = '\0';
}

/* the shell's YES / NO dialog over the screen */
static void mc_confirm_box(const char *what)
{
    ps1k_plaque(320, 240, 280, 120, false, 128);
    float cap = 15;
    while (cap > 9 && ps1k_word_width(what, cap) > 260)
        cap -= 0.5f;
    ps1k_word(what, 320, 200, cap, PS1K_CENTRE, 128);
    ps1k_word("Are you sure?", 320, 228, 14, PS1K_CENTRE, 128);
    static const char *const YN[2] = {"YES", "NO"};
    for (int i = 0; i < 2; ++i)
    {
        const float x = i ? 367 : 273;
        ps1k_sphere(PS1K_BLUE, x, 266, 40, 128);
        ps1k_word(YN[i], x, 266, 12, PS1K_CENTRE, 128);
    }
    ps1k_cursor(M.yes ? 273 : 367, 266);
}

static void ps1_memcards_draw(void)
{
    ps1k_background();
    if (C.picking)
    {
        /* choosing a card file to import: rows of file names */
        float cap = 20;
        while (cap > 10 && ps1k_word_width("IMPORT CARD", cap) + 36 > 220)
            cap -= 1;
        ps1k_plate("IMPORT CARD", ps1k_ox(626), 67, cap);
        const float l = ps1k_wx(-60), r = ps1k_wx(680);
        int top = C.import_cursor - MC_ROWS / 2;
        top = top > C.import_count - MC_ROWS ? C.import_count - MC_ROWS : top;
        top = top < 0 ? 0 : top;
        for (int k = 0; k < MC_ROWS && top + k < C.import_count; ++k)
        {
            const int i = top + k;
            const float y = MC_ROW_Y0 + 16 + k * MC_ROW_DY;
            const bool f = i == C.import_cursor;
            if (f)
                ps1k_plaque((l + r) * 0.5f, y, r - l + 20, 32, true, 128);
            char name[160];
            ps1_caps(C.imports[i], name, sizeof(name));
            mc_trim(name, r - l - 40, f ? 12 : 10);
            ps1k_sphere(PS1K_GOLD, l, y, f ? 18 : 12, 128);
            ps1k_rom(name, l + 18, y, PS1K_LEFT, f ? 12 : 10, f ? 17 : 14);
            if (f)
                ps1k_cursor(l + 40, y);
        }
        char left[SwanStationPS5_PATH_MAX + 80];
        if (!C.import_count)
            snprintf(left, sizeof(left), "PUT .MCR .MCD .SRM .GME IN %s/IMPORT", app.paths.root);
        else
            snprintf(left, sizeof(left), "REPLACES THE CARD OF %s", C.count ? C.cards[C.cursor].title : "");
        char caps[sizeof(left)];
        ps1k_info_bar(ps1_caps(left, caps, sizeof(caps)), NULL, ps1k_wx(-40), ps1k_wx(680), 411);
        ps1k_screen_fade(4001);
        app_draw_toast();
        return;
    }
    ps1k_plate("MEMORY CARD", ps1k_ox(626), 67, 20);
    /* the cards */
    static int top_row;
    top_row = list_top_row(top_row, C.cursor, MC_ROWS, C.count);
    const float l = mc_list_l(), r = mc_list_r();
    float cur_x = 0, cur_y = -1;
    for (int i = top_row; i < C.count && i < top_row + MC_ROWS; ++i)
    {
        const Card *c = &C.cards[i];
        const float y = MC_ROW_Y0 + (i - top_row) * MC_ROW_DY;
        const bool sel = i == C.cursor;
        if (sel)
        {
            ps1k_plaque((l + r) * 0.5f, y, r - l + 20, 32, true, M.zone == Z_CARDS ? 128 : 96);
            if (M.zone == Z_CARDS)
                cur_x = l + 40, cur_y = y;
        }
        char title[128], used[16];
        ps1_caps(c->title, title, sizeof(title));
        snprintf(used, sizeof(used), "%d/15", c->used);
        const float uw = ps1k_rom_width(used, 9);
        mc_trim(title, r - l - uw - 14, 10);
        ps1k_rom(title, l, y, PS1K_LEFT, 10, 14);
        ps1k_rom(used, r, y, PS1K_RIGHT, 9, 13);
    }
    if (top_row > 0)
        ps1k_sphere(PS1K_BLUE, (l + r) * 0.5f, MC_ROW_Y0 - 22, 10, 128);
    if (top_row + MC_ROWS < C.count)
        ps1k_sphere(PS1K_BLUE, (l + r) * 0.5f, MC_ROW_Y0 + MC_ROWS * MC_ROW_DY - 16, 10, 128);
    /* the plates: EXIT, BACK UP, RESTORE, DELETE (lit = can be done now) */
    static const char *const ACTS[ACT_COUNT] = {"EXIT", "BACK UP", "RESTORE", "DELETE"};
    const Card *cc = C.count ? &C.cards[C.cursor] : NULL;
    const bool save_chosen = cc && block_save(cc, M.block, NULL) >= 0;
    const bool act_ok[ACT_COUNT] = {true, C.count > 0, C.count > 0 && !app.game, save_chosen && !app.game};
    for (int i = 0; i < ACT_COUNT; ++i)
    {
        const float x = 330, y = 150 + i * 46;
        const bool f = M.zone == Z_PLAQUES && M.plaque == i;
        const int b = act_ok[i] ? 128 : 64;
        const float k = f ? 1.25f : 1.0f;
        ps1k_plaque(x, y, 112 * k, 26 * k, false, b);
        ps1k_word(ACTS[i], x, y, 11 * k, PS1K_CENTRE, b);
        if (f)
            cur_x = x, cur_y = y;
    }
    /* the chosen card: the 3 x 5 block lattice, 50 apart, the dots between; a save's icon in its first block */
    const float bx = mc_blocks_x(), by = 140;
    for (int rr = 0; rr <= 5; ++rr)
        for (int c = 0; c <= 3; ++c)
            ps1k_sphere(PS1K_BLUE, bx - 75 + c * 50, by - 25 + rr * 50, 8, 128);
    const int chosen_save = cc ? block_save(cc, M.block, NULL) : -1;
    for (int block = 0; block < 15; ++block)
    {
        const float x = bx + (block % 3 - 1) * 50, y = by + (block / 3) * 50;
        const bool on = M.zone == Z_BLOCKS && M.block == block;
        bool first = false;
        const int sv = cc ? block_save(cc, block, &first) : -1;
        /* the save the DELETE plate would remove: its blocks on the band */
        if (sv >= 0 && sv == chosen_save && (on || (M.zone == Z_PLAQUES && M.plaque == ACT_DELETE)))
            ps1k_plaque(x, y, 46, 46, true, 128);
        if (sv >= 0)
        {
            const float s = on ? 40 : 32;
            if (first && cc->icon_frames[sv])
                draw_save_icon(cc, sv, PS1K_X(x - s * 0.5f), PS1K_Y(y - s * 0.5f), s * PS1K_S);
            else
                ps1k_plaque(x, y, 26, 4, false, 128); /* a block the save goes on in */
        }
        if (on)
            cur_x = x, cur_y = y;
    }
    /* the info bar: what is under the arrow */
    char left[240], right[48] = "";
    if (M.confirm)
    {
        static const char *const ASK[ACT_COUNT] = {"", "BACK UP THIS CARD TO THE EXPORT FOLDER", "REPLACE THIS CARD WITH %s",
                                                   "DELETE THIS SAVE"};
        if (M.action == ACT_RESTORE)
            snprintf(left, sizeof(left), ASK[ACT_RESTORE], C.imports[M.import_pick]);
        else
            str_copy(left, sizeof(left), ASK[M.action]);
    }
    else if (!C.count)
        str_copy(left, sizeof(left), "NO MEMORY CARDS YET: THEY APPEAR ONCE A GAME SAVES");
    else if (M.zone == Z_CARDS)
        str_copy(left, sizeof(left), cc->title);
    else if (M.zone == Z_BLOCKS)
        str_copy(left, sizeof(left), chosen_save >= 0 ? cc->saves[chosen_save] : "FREE BLOCK");
    else
    {
        static const char *const SAY[ACT_COUNT] = {"LEAVE THE MEMORY CARDS", "COPY THIS CARD TO THE EXPORT FOLDER",
                                                   "REPLACE THIS CARD WITH A FILE FROM IMPORT", "DELETE THE CHOSEN SAVE"};
        if (!act_ok[M.plaque])
            str_copy(left, sizeof(left), app.game ? "LEAVE THE GAME FIRST: ITS CARD IS IN USE" : "CHOOSE A SAVE FIRST");
        else
            str_copy(left, sizeof(left), SAY[M.plaque]);
    }
    if (cc && !M.confirm)
        snprintf(right, sizeof(right), "%d/15 BLOCKS", cc->used);
    char caps[240];
    ps1k_info_bar(ps1_caps(left, caps, sizeof(caps)), right, ps1k_wx(-40), ps1k_wx(680), 411);
    if (M.confirm)
    {
        static const char *const WHAT[ACT_COUNT] = {"", "BACK UP CARD", "RESTORE CARD", "DELETE FILE"};
        mc_confirm_box(WHAT[M.action]);
    }
    else if (cur_y >= 0)
        ps1k_cursor(cur_x, cur_y);
    ps1k_screen_fade(4000);
    app_draw_toast();
}

static void ps1_memcards_act(int action)
{
    Card *c = C.count ? &C.cards[C.cursor] : NULL;
    switch (action)
    {
    case ACT_BACKUP:
        export_card(c);
        break;
    case ACT_RESTORE:
        import_card(c, C.imports[M.import_pick]);
        break;
    case ACT_DELETE:
    {
        const int sv = block_save(c, M.block, NULL);
        app_toast(sv >= 0 && delete_save(c, sv) ? "Save deleted (the card is kept as .bak)" : "The save could not be deleted");
        M.block = 0;
        break;
    }
    default:
        break;
    }
}

/* The controls; true when the screen was left. No wrapping: at the end of a list the arrow stays (ps1k_move_sound). */
static bool ps1_memcards_input(uint32_t pressed)
{
    const uint32_t DIRS = BIT(BTN_UP) | BIT(BTN_DOWN) | BIT(BTN_LEFT) | BIT(BTN_RIGHT);
    const bool up = pressed & BIT(BTN_UP), down = pressed & BIT(BTN_DOWN), left = pressed & BIT(BTN_LEFT),
               right = pressed & BIT(BTN_RIGHT), cross = pressed & BIT(BTN_CROSS), circle = pressed & BIT(BTN_CIRCLE);
    Card *c = C.count ? &C.cards[C.cursor] : NULL;
    if (M.confirm)
    {
        const bool was = M.yes;
        if (left)
            M.yes = true;
        if (right)
            M.yes = false;
        ps1k_move_sound(M.yes != was, pressed, BIT(BTN_LEFT) | BIT(BTN_RIGHT));
        if (cross)
        {
            M.confirm = false;
            if (M.yes)
            {
                sfx_play(SFX_SELECT);
                ps1_memcards_act(M.action);
            }
            else
                sfx_play(SFX_BACK);
        }
        else if (circle)
        {
            M.confirm = false;
            sfx_play(SFX_BACK);
        }
        return false;
    }
    if (C.picking)
    {
        const int was = C.import_cursor;
        if (up && C.import_cursor > 0)
            --C.import_cursor;
        if (down && C.import_cursor + 1 < C.import_count)
            ++C.import_cursor;
        ps1k_move_sound(C.import_cursor != was, pressed, BIT(BTN_UP) | BIT(BTN_DOWN));
        if (cross && C.import_count && c)
        {
            C.picking = false;
            M.import_pick = C.import_cursor;
            M.action = ACT_RESTORE, M.confirm = true, M.yes = false;
            sfx_play(SFX_OPEN);
        }
        else if (cross)
            sfx_play(SFX_ERROR);
        if (circle)
        {
            C.picking = false;
            sfx_play(SFX_BACK);
        }
        return false;
    }
    const int zone_before = M.zone, card_before = C.cursor, block_before = M.block, plaque_before = M.plaque;
    switch (M.zone)
    {
    case Z_CARDS:
        if (up && C.cursor > 0)
            --C.cursor, M.block = 0;
        if (down && C.cursor + 1 < C.count)
            ++C.cursor, M.block = 0;
        if (right)
            M.zone = Z_PLAQUES;
        if (cross)
            M.zone = c && c->save_count ? Z_BLOCKS : Z_PLAQUES;
        if (circle)
        {
            sfx_play(SFX_CLOSE);
            app.screen = C.back_to;
            return true;
        }
        break;
    case Z_BLOCKS:
        if (up && M.block >= 3)
            M.block -= 3;
        if (down && M.block + 3 < 15)
            M.block += 3;
        if (right && M.block % 3 < 2)
            ++M.block;
        if (left)
        {
            if (M.block % 3)
                --M.block;
            else
                M.zone = Z_PLAQUES;
        }
        if (cross)
        {
            if (c && block_save(c, M.block, NULL) >= 0)
                M.zone = Z_PLAQUES, M.plaque = ACT_DELETE;
            else
                sfx_play(SFX_ERROR);
        }
        if (circle)
            M.zone = Z_CARDS;
        break;
    default:
        if (up && M.plaque > 0)
            --M.plaque;
        if (down && M.plaque + 1 < ACT_COUNT)
            ++M.plaque;
        if (left)
            M.zone = Z_CARDS;
        if (right && c)
            M.zone = Z_BLOCKS;
        if (circle)
            M.zone = Z_CARDS;
        if (cross)
        {
            const bool save_chosen = c && block_save(c, M.block, NULL) >= 0;
            if (M.plaque == ACT_EXIT)
            {
                sfx_play(SFX_CLOSE);
                app.screen = C.back_to;
                return true;
            }
            else if (!c || ((M.plaque == ACT_RESTORE || M.plaque == ACT_DELETE) && app.game) ||
                     (M.plaque == ACT_DELETE && !save_chosen))
                sfx_play(SFX_ERROR);
            else if (M.plaque == ACT_RESTORE)
            {
                list_imports();
                C.import_cursor = 0;
                C.picking = true;
                sfx_play(SFX_OPEN);
            }
            else
            {
                M.action = M.plaque, M.confirm = true, M.yes = false;
                sfx_play(SFX_OPEN);
            }
        }
        break;
    }
    ps1k_move_sound(M.zone != zone_before || C.cursor != card_before || M.block != block_before || M.plaque != plaque_before,
                    pressed, DIRS);
    return false;
}
