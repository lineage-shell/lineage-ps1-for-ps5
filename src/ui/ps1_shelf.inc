/*
 * SwanStationPS5 - the PS1 shelf v3: the SCPH-7002 blue shell turned into a game shelf. Included by coverflow.c,
 * which owns the shelf's state (S, P, F) and its helpers; it is not built on its own. The drawing kit is ps1_kit.c,
 * the per-game record ps1_record.c. Design: design notes ps1/new-ux (index.json), in the shell's units.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *  MAIN MENU   GAME LIBRARY / SETTINGS, as the console's own main menu (the BIOS's startup plays before it).
 *  LIBRARY     the memory card screen's dot lattice with covers between the dots: 9 x 3 or 6 x 2 a page (Settings >
 *              Library > Library layout; 7 x 3 or 5 x 2 when Screen shape is 4:3). L1 / R1 page, L2 filter (ALL /
 *              RECENT / FAVOURITES), R2 sort, Triangle search (the CD Player's keypad: the typed letters are looked for
 *              anywhere in the titles, punctuation and spaces left out), X the game's page, O the main menu.
 *  GAME PAGE   the CD Player rebuilt (v7): PLAY, FAVOURITE, SETTINGS, RESTART spheres; SAVES (or DISC n/m) and
 *              HRS:MIN on top; the box with the cover; AUTO SAVE, FAST BOOT, DISC, WIDESCREEN, SCANLINES plates; the
 *              20 save-state slots as the track grid. X loads a slot, Square deletes it (the memory card's dialog).
 *              Triangle chooses the cover.
 * Motion as the shell: the arrow jumps, the chosen thing snaps to 1.25x, a confirm fades out over 32 PAL ticks (the
 * main menu: everything on it together, no flash back; the library: the chosen cover), then the background alone; the
 * next screen fades in over the background in 12 ticks instead of the shell's hard cut. Sounds: a click only when the arrow moves (ps1k_move_sound).
 */

#include "ps1_kit.h"
#include "ps1_record.h"
#include "../core/bios_choices.h"
#include "../core/swanstation_options.h"

#define PS1_SLOTS 20

enum
{
    V_MENU,
    V_LIBRARY,
    V_GAME,
    V_TO_SETTINGS /* a confirm's target only: Settings opens after the flash */
};
enum
{
    F_SPHERE,
    F_PLATE,
    F_SLOT,
    F_EXIT /* the EXIT splatter: back to the library */
};
/* the game page's plates, at their places in the CD Player's mode-plate rows */
enum
{
    PL_AUTO,
    PL_FAST,
    PL_DISC,
    PL_WIDE,
    PL_SCAN,
    PL_COUNT
};
static const float PLATE_XY[PL_COUNT][2] = {{156, 342}, {272, 342}, {388, 342}, {156, 402}, {272, 402}};
static const char *const PLATE_WORD[PL_COUNT] = {"AUTO SAVE", "FAST BOOT", "DISC", "WIDESCREEN", "SCANLINES"};
#define SPHERES 4
#define COLUMN_SLOTS 7 /* the CD Player's column: the last three are unlit, as the original's spare transport spheres */
static const char *const SPHERE_WORD[SPHERES] = {"PLAY", "FAVOURITE", "SETTINGS", "RESTART"};

static struct
{
    int mode;
    int menu_sel;      /* 0 GAME LIBRARY, 1 SETTINGS */
    int focus, item;   /* the game page: F_* and which */
    bool confirm;      /* the memory card's DELETE FILE dialog */
    int confirm_sel;   /* 0 YES, 1 NO */
    /* the confirm: flash, then the background alone, then the cut */
    float trans_t;
    int trans_to;      /* the mode after it, -1 none */
    int records_for;   /* library size the records were merged for (-1 never) */
    /* the game page's game */
    int game;
    bool slot_used[PS1_SLOTS];
    time_t slot_time[PS1_SLOTS];
    int slots_used, newest;
    int thumb_slot;
    PlatTexture *thumb;
    Settings own;      /* its own settings (the plates) */
    Ps1Record rec;
    /* what to do once the game is up */
    char pending_path[SwanStationPS5_PATH_MAX];
    int pending_slot;
    bool open_last;    /* started again after a game: its page first */
    float seen_t;      /* when the shelf was last drawn: back from Settings, the page reads its settings again */
    int bg_ticks;      /* the confirm's background-only ticks (the game page: only a few, it fades in at once) */
    int kp_key;        /* the search keypad: the key under the arrow */
    bool kp_digits;    /* the keypad shows 0-9 (the # key) */
} V = {.mode = V_MENU, .trans_to = -1, .records_for = -1, .game = -1, .thumb_slot = -1, .pending_slot = -1};

#define TICK 0.02f
#define FLASH_TICKS 32
#define BG_TICKS 35

/* ---------------------------------------------------------------- the library's layouts */

typedef struct
{
    int cols, rows;
    float pitch, cover, dot, top;
} Layout;
static const Layout LAYOUTS3[2] = {{9, 3, 80, 64, 10, 108}, {6, 2, 112, 96, 14, 110}};
static const Layout LAYOUTS43[2] = {{7, 3, 80, 64, 10, 108}, {5, 2, 112, 96, 14, 110}}; /* Screen shape 4:3 */

static const Layout *layout(void)
{
    return &(ps1k_narrow() ? LAYOUTS43 : LAYOUTS3)[app.global.shelf_layout == 1 ? 1 : 0];
}

static int per_page(void)
{
    return layout()->cols * layout()->rows;
}

static void cell_centre(int i, float *cx, float *cy)
{
    const Layout *L = layout();
    const float x0 = 320.0f - L->cols * L->pitch * 0.5f;
    *cx = x0 + (i % L->cols + 0.5f) * L->pitch;
    *cy = L->top + (i / L->cols + 0.5f) * L->pitch;
}

/* the covers module loads around this position: the middle of the page, so the whole page streams in */
static int ps1_cover_centre(void)
{
    if (V.mode != V_LIBRARY)
        return S.cursor;
    const int per = per_page(), page = S.cursor / per;
    int c = page * per + per / 2;
    return c >= S.view_count ? (S.view_count ? S.view_count - 1 : 0) : c;
}

/* the covers module keeps this page and the pages either side decoded and up (the centre is the page's middle, so
 * per/2 + per reaches the far end of a neighbour), and up to one more page of recently shown ones (the LRU) */
static void ps1_cover_window(void)
{
    if (!TH_PS1)
    {
        covers_set_window(-1, 0);
        return;
    }
    const int per = per_page();
    covers_set_window(per + per / 2 + 1, per * 4);
}

/* O only quits (held) from the main menu */
static bool ps1_quit_allowed(void)
{
    return V.mode == V_MENU && V.trans_to < 0;
}

/* ---------------------------------------------------------------- small helpers */

static void upper(const char *s, char *out, size_t size)
{
    ps1_caps(s, out, size);
}

static void date_text(int64_t t, char *out, size_t size, bool year)
{
    if (t <= 0)
    {
        str_copy(out, size, "NEVER");
        return;
    }
    static const char *const MON[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    time_t tt = (time_t)t;
    struct tm tmv;
    localtime_r(&tt, &tmv);
    if (year)
        snprintf(out, size, "%d %s %d", tmv.tm_mday, MON[tmv.tm_mon], tmv.tm_year + 1900);
    else
        snprintf(out, size, "%d %s", tmv.tm_mday, MON[tmv.tm_mon]);
}

/* a cover's confirm: the same 32 ticks as a plain fade out */
static int fade_b(void)
{
    const int b = 128 - 4 * (int)(V.trans_t / TICK);
    return b < 0 ? 0 : b;
}

static bool in_flash(void)
{
    return V.trans_to >= 0 && V.trans_t < FLASH_TICKS * TICK;
}

static bool in_bg(void)
{
    return V.trans_to >= 0 && V.trans_t >= FLASH_TICKS * TICK;
}

static void go(int mode)
{
    V.trans_to = mode;
    V.trans_t = 0.0f;
    V.bg_ticks = mode == V_GAME ? 3 : BG_TICKS; /* the game page comes in quickly */
    sfx_play(SFX_SELECT);
}

static void cut_to(int mode)
{
    V.mode = mode;
    V.trans_to = -1;
    S.details = mode == V_GAME;
}

/* ---------------------------------------------------------------- the game page's data */

static void slot_path(const Game *g, int slot, char *out, size_t size)
{
    char file[96];
    snprintf(file, sizeof(file), "%.80s.state%d", g->id, slot);
    path_join(out, size, app.paths.states, file);
}

static void free_thumb(void)
{
    plat_texture_free(V.thumb);
    V.thumb = NULL;
    V.thumb_slot = -1;
}

static void own_settings_path(const Game *g, char *out, size_t size)
{
    app_game_config_path(out, size, g);
}

static void load_page(int game)
{
    free_thumb();
    V.game = game;
    V.slots_used = 0;
    V.newest = -1;
    const Game *g = &app.library.games[game];
    time_t best = 0;
    for (int i = 0; i < PS1_SLOTS; ++i)
    {
        char path[SwanStationPS5_PATH_MAX];
        slot_path(g, i, path, sizeof(path));
        struct stat st;
        V.slot_used[i] = stat(path, &st) == 0;
        V.slot_time[i] = V.slot_used[i] ? st.st_mtime : 0;
        if (V.slot_used[i])
        {
            ++V.slots_used;
            if (st.st_mtime >= best)
                best = st.st_mtime, V.newest = i;
        }
    }
    char own[SwanStationPS5_PATH_MAX];
    own_settings_path(g, own, sizeof(own));
    config_load_game(&V.own, &app.global, own);
    ps1_record_get(g, &V.rec);
    if (V.rec.boot_disc < 0 || V.rec.boot_disc >= (g->discs > 1 ? g->discs : 1))
        V.rec.boot_disc = 0;
}

static void save_own(void)
{
    const Game *g = &app.library.games[V.game];
    char dir[SwanStationPS5_PATH_MAX], path[SwanStationPS5_PATH_MAX];
    path_join(dir, sizeof(dir), app.paths.user, "game-settings");
    make_dirs(dir);
    own_settings_path(g, path, sizeof(path));
    Settings s = V.own;
    for (int i = 0; i < SS_OPT_COUNT; ++i)
        if (ss_opt_is_bios(i))
            s.ss_opt[i] = SS_OPTS[i].def; /* there is no BIOS per game */
    config_save(&s, path);
}

/* the plates on this game's page, in order (DISC only for a multi-disc game) */
static int plate_list(int out[PL_COUNT])
{
    int n = 0;
    out[n++] = PL_AUTO, out[n++] = PL_FAST;
    if (V.game >= 0 && app.library.games[V.game].discs > 1)
        out[n++] = PL_DISC;
    out[n++] = PL_WIDE, out[n++] = PL_SCAN;
    return n;
}

static int plate_on(int p)
{
    switch (p)
    {
    case PL_AUTO: return V.own.quick_resume;
    case PL_FAST: return !V.own.boot_intro;
    case PL_WIDE: return V.own.widescreen;
    case PL_SCAN: return V.own.crt != 0;
    default: return -1; /* DISC: no on / off */
    }
}

static void open_page(int game)
{
    load_page(game);
    V.focus = F_SPHERE;
    V.item = 0;
    V.confirm = false;
}

/* ---------------------------------------------------------------- starting a game */

static void launch(int game, bool resume, const char *state_path, int slot)
{
    const Game *g = &app.library.games[game];
    S.resume = resume;
    str_copy(V.pending_path, sizeof(V.pending_path), state_path ? state_path : "");
    V.pending_slot = slot;
    Ps1Record r;
    ps1_record_get(g, &r);
    host_set_initial_disc(g->discs > 1 && r.boot_disc > 0 && r.boot_disc < g->discs ? r.boot_disc : -1);
    S.launch_t = 0.0001f;
    sfx_play(SFX_SELECT);
}

/* PLAY: the newest of the exit save, the auto saves and the save states; nothing saved yet: from the start */
static void play_newest(int game)
{
    const Game *g = &app.library.games[game];
    if (ra_hardcore())
    {
        launch(game, false, NULL, -1);
        return;
    }
    time_t best = 0;
    int kind = 0, slot = -1; /* 0 boot, 1 the exit save, 2 a file */
    char best_path[SwanStationPS5_PATH_MAX] = "", path[SwanStationPS5_PATH_MAX], file[112];
    struct stat st;
    play_resume_path(g, path, sizeof(path));
    if (stat(path, &st) == 0 && st.st_mtime > best)
        best = st.st_mtime, kind = 1;
    for (int i = 0; i < AUTO_SLOTS; ++i)
    {
        snprintf(file, sizeof(file), "%.80s.auto%d", g->id, i + 1);
        path_join(path, sizeof(path), app.paths.states, file);
        if (stat(path, &st) == 0 && st.st_mtime > best)
            best = st.st_mtime, kind = 2, slot = -1, str_copy(best_path, sizeof(best_path), path);
    }
    for (int i = 0; i < PS1_SLOTS; ++i)
    {
        slot_path(g, i, path, sizeof(path));
        if (stat(path, &st) == 0 && st.st_mtime > best)
            best = st.st_mtime, kind = 2, slot = i, str_copy(best_path, sizeof(best_path), path);
    }
    if (kind == 1)
        launch(game, true, NULL, -1);
    else if (kind == 2)
        launch(game, false, best_path, slot);
    else
        launch(game, false, NULL, -1);
}

/* called by coverflow.c when the game is up */
static void ps1_after_start(void)
{
    if (V.pending_path[0] && app.game)
    {
        const bool ok = host_load_state(V.pending_path);
        if (!ok)
            app_toast("Couldn't load the save.");
        else if (V.pending_slot >= 0)
        {
            app.settings.state_slot = V.pending_slot;
            app.global.state_slot = V.pending_slot;
        }
    }
    V.pending_path[0] = '\0';
    V.pending_slot = -1;
    cut_to(V_GAME); /* back from the game: its page */
}

/* ---------------------------------------------------------------- input */

static void bump(void)
{
    sfx_play(SFX_ERROR);
}

static int filter_next(int c)
{
    /* ALL -> RECENT -> FAVOURITES -> ALL, skipping an empty one */
    static const int ORDER[3] = {CAT_ALL, CAT_RECENT, CAT_FAVORITES};
    int at = 0;
    for (int i = 0; i < 3; ++i)
        if (ORDER[i] == c)
            at = i;
    for (int k = 1; k <= 3; ++k)
    {
        const int n = ORDER[(at + k) % 3];
        if (n == CAT_ALL || category_size(n) > 0)
            return n;
    }
    return CAT_ALL;
}

static uint32_t menu_input(uint32_t pressed)
{
    const int before = V.menu_sel;
    if (pressed & BIT(BTN_UP))
        V.menu_sel = 0;
    if (pressed & BIT(BTN_DOWN))
        V.menu_sel = 1;
    ps1k_move_sound(V.menu_sel != before, pressed, BIT(BTN_UP) | BIT(BTN_DOWN));
    if (pressed & BIT(BTN_CROSS))
        go(V.menu_sel == 0 ? V_LIBRARY : V_TO_SETTINGS);
    return pressed & BIT(BTN_L3);
}

static uint32_t library_input(uint32_t pressed)
{
    const Layout *L = layout();
    const int per = per_page(), before = S.cursor, page = S.cursor / per, in = S.cursor % per;
    const int pages = (S.view_count + per - 1) / per, row = in / L->cols, col = in % L->cols;
    if (pressed & BIT(BTN_LEFT))
    {
        if (col > 0)
            --S.cursor;
        else if (page > 0)
            S.cursor = (page - 1) * per + row * L->cols + L->cols - 1; /* the previous page, last column, same row */
    }
    if (pressed & BIT(BTN_RIGHT))
    {
        if (col < L->cols - 1 && S.cursor + 1 < S.view_count)
            ++S.cursor;
        else if (col == L->cols - 1 && page + 1 < pages)
        {
            /* the next page, first column, same row (a short last page: its nearest filled cell) */
            const int to = (page + 1) * per + row * L->cols;
            S.cursor = to < S.view_count ? to : S.view_count - 1;
        }
    }
    if ((pressed & BIT(BTN_UP)) && in >= L->cols)
        S.cursor -= L->cols;
    if ((pressed & BIT(BTN_DOWN)) && in + L->cols < per)
        S.cursor = S.cursor + L->cols < S.view_count ? S.cursor + L->cols : S.cursor;
    if (pressed & BIT(BTN_L1))
        S.cursor = page > 0 ? (page - 1) * per + in : S.cursor;
    if (pressed & BIT(BTN_R1))
    {
        if (page + 1 < pages)
            S.cursor = (page + 1) * per + in < S.view_count ? (page + 1) * per + in : S.view_count - 1;
    }
    S.cursor = S.cursor < 0 ? 0 : S.cursor >= S.view_count ? (S.view_count ? S.view_count - 1 : 0) : S.cursor;
    if (S.cursor != before && before / per != S.cursor / per)
        sfx_play(SFX_PAGE);
    else
        ps1k_move_sound(S.cursor != before, pressed,
                        BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_UP) | BIT(BTN_DOWN) | BIT(BTN_L1) | BIT(BTN_R1));
    const int game = selected_game();
    if (pressed & BIT(BTN_L2))
    {
        app.global.shelf_category = filter_next(app.global.shelf_category);
        app.settings.shelf_category = app.global.shelf_category;
        S.filter[0] = '\0';
        build_view(game);
        sfx_play(SFX_PAGE);
    }
    if (pressed & BIT(BTN_R2))
    {
        app.global.sort_mode = (app.global.sort_mode + 1) % SORT_COUNT;
        build_view(game);
        sfx_play(SFX_PAGE);
    }
    if (pressed & BIT(BTN_TRIANGLE))
    {
        filter_open(); /* the search keypad */
        V.kp_key = 0;
        V.kp_digits = false;
    }
    if ((pressed & BIT(BTN_CROSS)) && game >= 0)
    {
        if (!app_disc_pending(game))
        {
            open_page(game);
            go(V_GAME);
        }
    }
    else if ((pressed & BIT(BTN_CROSS)))
        bump();
    if (pressed & BIT(BTN_CIRCLE))
    {
        if (S.filter[0])
        {
            /* a search on: O shows all the games again first */
            S.filter[0] = '\0';
            F.text[0] = '\0';
            build_view(game);
            sfx_play(SFX_BACK);
        }
        else
        {
            cut_to(V_MENU);
            sfx_play(SFX_CLOSE);
        }
    }
    if ((pressed & BIT(BTN_SQUARE)))
    {
        sfx_play(SFX_OPEN);
        config_save(&app.global, app.paths.config);
        app_open_shelf_settings(game >= 0 ? &app.library.games[game] : NULL);
    }
    return pressed & (BIT(BTN_L3) | BIT(BTN_MENU));
}

static void delete_slot(int slot)
{
    const Game *g = &app.library.games[V.game];
    char path[SwanStationPS5_PATH_MAX], thumb[SwanStationPS5_PATH_MAX + 8];
    slot_path(g, slot, path, sizeof(path));
    snprintf(thumb, sizeof(thumb), "%s.thumb", path);
    const bool ok = remove(path) == 0;
    remove(thumb);
    if (!ok)
        app_toast("The save could not be deleted.");
    const int keep_focus = V.focus, keep_item = V.item;
    load_page(V.game);
    V.focus = keep_focus, V.item = keep_item;
}

static void open_game_settings(const Game *g)
{
    sfx_play(SFX_OPEN);
    config_save(&app.global, app.paths.config);
    app_open_shelf_settings(g);
    ps1_settings_game_scope(); /* this game's own settings */
}

static void game_input(uint32_t pressed)
{
    const int game = V.game;
    const Game *g = &app.library.games[game];
    if (V.confirm)
    {
        const int was = V.confirm_sel;
        if (pressed & BIT(BTN_LEFT))
            V.confirm_sel = 0;
        if (pressed & BIT(BTN_RIGHT))
            V.confirm_sel = 1;
        ps1k_move_sound(V.confirm_sel != was, pressed, BIT(BTN_LEFT) | BIT(BTN_RIGHT));
        if (pressed & BIT(BTN_CROSS))
        {
            if (V.confirm_sel == 0)
            {
                delete_slot(V.item - 1);
                sfx_play(SFX_SELECT);
            }
            else
                sfx_play(SFX_BACK);
            V.confirm = false;
        }
        if (pressed & BIT(BTN_CIRCLE))
        {
            V.confirm = false;
            sfx_play(SFX_BACK);
        }
        return;
    }
    int plates[PL_COUNT];
    const int np = plate_list(plates), row1 = plates[2] == PL_DISC ? 3 : 2;
    const int f = V.focus, it = V.item;
    switch (V.focus)
    {
    case F_SPHERE:
        if (pressed & BIT(BTN_UP))
            V.item = it > 0 ? it - 1 : it;
        if (pressed & BIT(BTN_DOWN))
        {
            if (it < SPHERES - 1)
                V.item = it + 1;
            else
                V.focus = F_PLATE, V.item = 0;
        }
        if (pressed & BIT(BTN_RIGHT)) /* into the slots: on the newest save, as the CD Player's track */
            V.focus = F_SLOT, V.item = V.newest >= 0 ? V.newest + 1 : 1 + (it < 4 ? it : 3) * 5;
        break;
    case F_PLATE:
    {
        const bool top = it < row1;
        const int col = top ? it : it - row1, len = top ? row1 : np - row1;
        if (pressed & BIT(BTN_LEFT))
        {
            if (col > 0)
                V.item = it - 1;
            else
                V.focus = F_SPHERE, V.item = SPHERES - 1;
        }
        if (pressed & BIT(BTN_RIGHT))
        {
            if (col + 1 < len)
                V.item = it + 1;
            else if (top)
                V.focus = F_SLOT, V.item = 16;
            else
                V.focus = F_EXIT, V.item = 0; /* the bottom row's end: EXIT, as the CD Player's */
        }
        if (pressed & BIT(BTN_UP))
        {
            if (top)
                V.focus = F_SPHERE, V.item = SPHERES - 1;
            else
                V.item = col < row1 ? col : row1 - 1;
        }
        if ((pressed & BIT(BTN_DOWN)) && top)
            V.item = row1 + (col < np - row1 ? col : np - row1 - 1);
        else if ((pressed & BIT(BTN_DOWN)) && !top)
            V.focus = F_EXIT, V.item = 0;
        break;
    }
    case F_EXIT:
        if (pressed & BIT(BTN_UP))
            V.focus = F_SLOT, V.item = 18;
        if (pressed & BIT(BTN_LEFT))
            V.focus = F_PLATE, V.item = np - 1;
        break;
    default:
    {
        const int r = (it - 1) / 5, c = (it - 1) % 5;
        if (pressed & BIT(BTN_LEFT))
        {
            if (c > 0)
                V.item = it - 1;
            else
                V.focus = F_SPHERE, V.item = r;
        }
        if ((pressed & BIT(BTN_RIGHT)) && c < 4)
            V.item = it + 1;
        if ((pressed & BIT(BTN_UP)) && r > 0)
            V.item = it - 5;
        if ((pressed & BIT(BTN_DOWN)) && r < 3)
            V.item = it + 5;
        else if (pressed & BIT(BTN_DOWN))
            V.focus = F_EXIT, V.item = 0;
        break;
    }
    }
    ps1k_move_sound(V.focus != f || V.item != it, pressed, BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_UP) | BIT(BTN_DOWN));
    if ((pressed & BIT(BTN_CROSS)) && V.focus == F_EXIT)
        pressed = (pressed & ~BIT(BTN_CROSS)) | BIT(BTN_CIRCLE); /* EXIT: as O, back to the library */
    if (pressed & BIT(BTN_CROSS))
    {
        if (V.focus == F_SPHERE)
        {
            switch (V.item)
            {
            case 0:
                if (!app_disc_pending(game))
                    play_newest(game);
                break;
            case 1:
                V.rec.favourite = !V.rec.favourite;
                ps1_record_set_favourite(g, V.rec.favourite);
                sfx_play(SFX_SELECT);
                break;
            case 2:
                open_game_settings(g);
                break;
            default:
                if (!app_disc_pending(game))
                    launch(game, false, NULL, -1); /* a clean boot; the saves stay */
                break;
            }
        }
        else if (V.focus == F_PLATE)
        {
            switch (plates[V.item])
            {
            case PL_AUTO: V.own.quick_resume = !V.own.quick_resume; break;
            case PL_FAST: V.own.boot_intro = !V.own.boot_intro; break;
            case PL_WIDE: V.own.widescreen = !V.own.widescreen; break;
            case PL_SCAN: V.own.crt = V.own.crt ? 0 : 1; break;
            default:
                V.rec.boot_disc = (V.rec.boot_disc + 1) % (g->discs > 1 ? g->discs : 1);
                ps1_record_set_boot_disc(g, V.rec.boot_disc);
                break;
            }
            if (plates[V.item] != PL_DISC)
                save_own();
            sfx_play(SFX_SELECT);
        }
        else if (V.slot_used[V.item - 1] && !ra_hardcore())
        {
            char path[SwanStationPS5_PATH_MAX];
            slot_path(g, V.item - 1, path, sizeof(path));
            launch(game, false, path, V.item - 1);
        }
        else
            bump();
    }
    if ((pressed & BIT(BTN_SQUARE)) && V.focus != F_SLOT)
        open_game_settings(g); /* Square away from the slots: this game's settings, as Square on the library */
    if ((pressed & BIT(BTN_SQUARE)) && V.focus == F_SLOT)
    {
        if (V.slot_used[V.item - 1])
        {
            V.confirm = true;
            V.confirm_sel = 1; /* NO first */
            sfx_play(SFX_OPEN);
        }
        else
            bump();
    }
    if (pressed & BIT(BTN_TRIANGLE))
    {
        sfx_play(SFX_OPEN);
        picker_open(game); /* the cover: the picker's screen (ps1_picker_draw) */
    }
    if (pressed & BIT(BTN_CIRCLE))
    {
        cut_to(V_LIBRARY);
        free_thumb();
        sfx_play(SFX_CLOSE);
    }
}

/* ---------------------------------------------------------------- no BIOS: the setup screen */

/* Shown at start when no folder holds a BIOS file: where to put one, which files, CHECK AGAIN (then the normal start,
 * the opening included) or PLAY WITH OPENBIOS (the core's built-in BIOS, less compatible). Our own drawing and the
 * bundled faces only: without a BIOS there is no ROM font, so the info lines use the fallback face. */
static struct
{
    bool tried, open;
    int sel;          /* 0 CHECK AGAIN, 1 PLAY WITH OPENBIOS */
    double missed_t;  /* when CHECK AGAIN last found nothing (a line says so) */
} SU;

static bool ps1_setup_active(void)
{
    if (!SU.tried)
    {
        SU.tried = true;
        SU.open = !app_bios_present();
        if (SU.open)
        {
            SwanStationPS5_log("ps1 shelf: no BIOS: the setup screen");
            ps1_pslogo_skip(); /* no opening without a BIOS */
            const char *dirs[1];
            if (app_bios_dirs(dirs, 1) == 1)
                make_dirs(dirs[0]); /* the folder it asks for is there to copy into */
        }
    }
    return SU.open;
}

static void ps1_setup_input(uint32_t pressed)
{
    const int was = SU.sel;
    if (pressed & BIT(BTN_UP))
        SU.sel = 0;
    if (pressed & BIT(BTN_DOWN))
        SU.sel = 1;
    ps1k_move_sound(SU.sel != was, pressed, BIT(BTN_UP) | BIT(BTN_DOWN));
    if (!(pressed & BIT(BTN_CROSS)))
        return;
    if (SU.sel == 1)
    {
        SU.open = false;
        sfx_play(SFX_SELECT);
        SwanStationPS5_log("ps1 shelf: no BIOS: playing with OpenBIOS");
        return;
    }
    if (app_bios_recheck())
    {
        SU.open = false;
        sfx_play(SFX_SELECT);
        ps1_opening_retry();
        ps1_opening_begin(0); /* the normal start: the opening, then the menu */
        return;
    }
    SU.missed_t = ps1_time();
    sfx_play(SFX_ERROR);
}

static void ps1_setup_draw(void)
{
    ps1k_background();
    ps1k_plate_centred("PS1 BIOS NEEDED", 320, 56, 20);
    /* Kept simple (kurolume, 2026-11-16): one instruction, the folder, the choices; the other folders still searched are not listed */
    const char *dirs[8];
    app_bios_dirs(dirs, 8);
    const float x0 = ps1k_wx(30);
    ps1k_word("Copy your own PS1 BIOS file into this folder:", x0, 128, 13, PS1K_LEFT, 128);
    ps1k_sphere(PS1K_GREEN, x0 + 6, 158, 12, 128);
    ps1k_rom(dirs[0], x0 + 22, 158, PS1K_LEFT, 11, 16);
    static const char *const CHOICES[2] = {"CHECK AGAIN", "PLAY WITH OPENBIOS"};
    for (int i = 0; i < 2; ++i)
    {
        const float cy = 230 + i * 34, cap = SU.sel == i ? 16 : 12;
        ps1k_word(CHOICES[i], ps1k_wx(70), cy, cap, PS1K_LEFT, SU.sel == i ? 128 : 96);
    }
    ps1k_cursor(ps1k_wx(70) - 22, 230 + SU.sel * 34);
    /* Why it is needed, in its own box above the info bar: no Sony data ships with the app (kurolume, 2026-11-16) */
    ps1k_plaque(ps1k_wx(320), 374, ps1k_wx(640) - ps1k_wx(0), 66, true, 112);
    ps1k_word("Why?", x0, 352, 10, PS1K_LEFT, 112);
    ps1k_rom("SONY'S ARTWORK, FONTS AND SOUNDS ARE COPYRIGHTED. WE'VE REBUILT THE MENUS TO WORK ON", x0, 368, PS1K_LEFT, 9, 13);
    ps1k_rom("PS5, BUT WE CAN'T SHARE SONY'S ASSETS, SO THEY COME FROM YOUR OWN BIOS.", x0, 382, PS1K_LEFT, 9, 13);
    ps1k_rom("NOT SURE HOW TO GET YOUR OWN PS1 BIOS? SEARCH ONLINE.", x0, 396, PS1K_LEFT, 9, 13);
    const bool missed = SU.missed_t > 0 && ps1_time() - SU.missed_t < 4.0;
    ps1k_info_bar(missed ? "NO BIOS FOUND YET" : SU.sel ? "THE BUILT-IN BIOS: SOME GAMES MAY NOT RUN" : "LOOK IN THE FOLDER AGAIN",
                  "", ps1k_wx(-40), ps1k_wx(680), 438);
}

/* The shelf's buttons in the PS1 theme; returns what is left for coverflow.c's own handling */
static uint32_t ps1_shelf_input(uint32_t pressed)
{
    if (SU.open)
    {
        ps1_setup_input(pressed);
        return 0;
    }
    if (ps1_pslogo_active())
    {
        if (pressed)
            ps1_pslogo_skip(); /* a button skips the PlayStation screen, as the opening */
        return 0;
    }
    if (V.records_for != app.library.count)
    {
        V.records_for = app.library.count;
        ps1_record_merge_all();
    }
    if (V.open_last)
    {
        V.open_last = false;
        if (selected_game() >= 0)
        {
            open_page(selected_game());
            cut_to(V_GAME);
        }
    }
    if (V.trans_to >= 0)
        return 0; /* a confirm is playing */
    if (V.mode == V_GAME && (V.game < 0 || V.game >= app.library.count))
        cut_to(V_LIBRARY);
    switch (V.mode)
    {
    case V_MENU: return menu_input(pressed);
    case V_LIBRARY: return library_input(pressed);
    default: game_input(pressed); return 0;
    }
}

/* ---------------------------------------------------------------- drawing: the main menu */

static void draw_menu(void)
{
    ps1k_background();
    if (in_bg())
        return; /* the confirm's background-only ticks: spheres, plate and words all gone together */
    /* the ornamental spheres, the biggest first */
    static const float SPH[4][3] = {{299, 379, 120}, {149, 67, 104}, {213, 105, 79}, {236, 356, 59}};
    for (int i = 0; i < 4; ++i)
        ps1k_sphere(PS1K_BLUE_BIG, SPH[i][0] < 214 ? ps1k_ox(SPH[i][0]) : SPH[i][0], SPH[i][1], SPH[i][2], 128);
    ps1k_plate("MAIN MENU", ps1k_ox(626), 67, 20);
    static const char *const WORDS[2] = {"GAME LIBRARY", "SETTINGS"};
    for (int i = 0; i < 2; ++i)
    {
        const float y = 232 + 100 * i, x = ps1k_ox(160);
        const bool on = V.menu_sel == i;
        ps1k_splat(i + 1, x, y - 1, 216, 70, 128);
        ps1k_word(WORDS[i], x, y, on ? 25 : 20, PS1K_CENTRE, 128);
    }
    if (!in_flash())
        ps1k_cursor(ps1k_ox(160), 232 + 100 * V.menu_sel);
    else
        ps1k_background_over(1.0f - fade_b() / 128.0f); /* the whole menu fades out over the 32 ticks, no flash back */
}

/* ---------------------------------------------------------------- drawing: the library */

static PlatTexture *cover_of(int game)
{
    PlatTexture *t = shelf_cover(game);
    return t ? t : no_cover_icon();
}

/* A game's cover in a slot: an empty case until it is up (never the no-cover picture for art still loading), then it
 * fades in over a few frames. A cover already up (prefetched) shows at once. */
#define PS1_COVER_FADE 10
static void ps1_draw_cover(int game, float cx, float cy, float w, float h, int b)
{
    const int age = covers_age(game);
    const float a = age < 0 ? 0.0f : age >= PS1_COVER_FADE ? 1.0f : (float)age / PS1_COVER_FADE;
    if (a < 1.0f)
        ps1k_empty_slot(cx, cy, w, h, (int)(b * (1.0f - a)));
    if (age >= 0)
        ps1k_image_a(cover_of(game), cx, cy, w, h, b, true, a);
}

static void draw_library(void)
{
    /* behind the search keypad: the matches, everything at brightness 64 (the dim-others rule) */
    const int db = F.open ? 64 : 128;
    const Layout *L = layout();
    const int per = per_page(), page = S.cursor / per, pages = S.view_count ? (S.view_count + per - 1) / per : 1;
    const float x0 = 320.0f - L->cols * L->pitch * 0.5f;
    const bool empty = S.view_count == 0 && !S.filter[0];
    ps1k_background();
    if (in_bg())
        return; /* the confirm's background-only frames */
    if (empty)
    {
        static const float SPH[5][3] = {{400, 142, 58}, {366, 135, 30}, {475, 337, 132}, {574, 332, 44}, {508, 430, 44}};
        for (int i = 0; i < 5; ++i)
            ps1k_sphere(PS1K_BLUE_BIG, ps1k_ox(SPH[i][0]), SPH[i][1], SPH[i][2], 128);
    }
    ps1k_plate_centred("GAME LIBRARY", 320, 62, 20);
    const bool narrow = ps1k_narrow();
    if (!empty)
    {
        /* the filter and the sort on splatters where CARD 1 / CARD 2 sit */
        static const char *const FILTERS[3] = {"ALL GAMES", "RECENT", "FAVOURITES"};
        static const char *const SORTS3[SORT_COUNT] = {"A-Z", "RECENT", "MOST PLAYED", "REGION"};
        const int c = app.global.shelf_category;
        char word[64];
        if (S.filter[0])
            snprintf(word, sizeof(word), "\"%.14s\"", S.filter), upper(word, word, sizeof(word));
        else
            str_copy(word, sizeof(word), FILTERS[c == CAT_RECENT ? 1 : c == CAT_FAVORITES ? 2 : 0]);
        /* RECENT always lists the last played first */
        const int sm = c == CAT_RECENT ? SORT_RECENT : app.global.sort_mode;
        const char *sort = SORTS3[sm >= 0 && sm < SORT_COUNT ? sm : 0];
        for (int k = 0; k < 2; ++k)
        {
            /* 4:3: a little narrower and in, so the L2 / R2 spheres stay inside the console's area */
            const float sw = narrow ? 150 : 172, x = k ? ps1k_ox(533) + (narrow ? 6 : 20) : ps1k_ox(107) - (narrow ? 6 : 20);
            const char *w = k ? sort : word;
            float cap = 17;
            while (cap > 9 && ps1k_word_width(w, cap) > sw - 2)
                cap -= 1;
            ps1k_splat(k, x, 62, sw, 50, 128);
            ps1k_word(w, x, 62, cap, PS1K_CENTRE, 128);
            const float off = narrow ? 86 : 100;
            ps1k_label_sphere(k ? "R2" : "L2", x + (k ? off : -off), 76, narrow ? 30 : 36, 10);
        }
    }
    /* the dot lattice */
    for (int r = 0; r <= L->rows; ++r)
        for (int c = 0; c <= L->cols; ++c)
            ps1k_sphere(PS1K_BLUE, x0 + c * L->pitch, L->top + r * L->pitch, L->dot, empty ? 64 : db);
    if (empty)
    {
        ps1k_splat(5, ps1k_ox(540), 330, 96, 70, 128);
        ps1k_word("Please add", 310, 190, 19, PS1K_CENTRE, 128);
        ps1k_word("PlayStation", 320, 245, 30, PS1K_CENTRE, 128);
        ps1k_word("disc images.", 320, 297, 19, PS1K_CENTRE, 128);
        char dir[SwanStationPS5_PATH_MAX];
        upper(app.paths.games, dir, sizeof(dir));
        ps1k_info_bar("COPY .CHD .CUE .PBP TO", dir, ps1k_wx(-40), ps1k_wx(680), 411);
        return;
    }
    const int sel = S.cursor - page * per;
    const bool bg_only = in_bg();
    for (int i = 0; i < per && !bg_only; ++i)
    {
        const int k = page * per + i;
        if (k >= S.view_count || i == sel)
            continue;
        float cx, cy;
        cell_centre(i, &cx, &cy);
        ps1_draw_cover(S.view[k], cx, cy, L->cover, L->cover, db);
    }
    const int game = selected_game();
    if (game >= 0 && !bg_only)
    {
        float cx, cy;
        cell_centre(sel, &cx, &cy);
        /* behind the keypad nothing is chosen: the cover sits at its plain size, dimmed like the others */
        const float s = L->cover * (F.open ? 1.0f : 1.25f);
        ps1_draw_cover(game, cx, cy, s, s, in_flash() ? fade_b() : db);
    }
    /* the pages: CD track-grid dots, this page grown with its number */
    if (pages > 1)
    {
        /* halfway between the lattice's last dots and the info bar's top edge */
        const float py = (L->top + L->rows * L->pitch + L->dot * 0.5f + (411 - 16)) * 0.5f;
        const int shown = pages > 20 ? 20 : pages;
        for (int p = 0; p < shown; ++p)
        {
            const float x = 320 + (p - (shown - 1) * 0.5f) * 28;
            const int col = PS1K_CD_ROWS[p / 5 % 4][p % 5];
            if (p == page % 20)
            {
                char n[8];
                snprintf(n, sizeof(n), "%d", page + 1);
                ps1k_sphere(col, x, py, 22, db);
                ps1k_digits(n, x, py, 0.7f, db);
            }
            else
                ps1k_sphere(col, x, py, 8, db);
        }
        ps1k_label_sphere("L1", 320 - (shown + 1) * 0.5f * 28 - 10, py, 26, 9);
        ps1k_label_sphere("R1", 320 + (shown + 1) * 0.5f * 28 + 10, py, 26, 9);
    }
    if (F.open)
        return; /* the keypad's own info bar */
    if (game >= 0)
    {
        char title[160], right[48];
        upper(display_title(&app.library.games[game]), title, sizeof(title));
        GameStats *st = stats_get(app.library.games[game].id);
        const unsigned secs = st ? st->seconds : 0;
        if (secs >= 60)
            snprintf(right, sizeof(right), "PLAYED %uH %02uM", secs / 3600, secs / 60 % 60);
        else
            str_copy(right, sizeof(right), "NOT PLAYED");
        ps1k_info_bar(title, right, ps1k_wx(-40), ps1k_wx(680), 411);
        if (!in_flash() && !bg_only)
        {
            float cx, cy;
            cell_centre(sel, &cx, &cy);
            ps1k_cursor(cx, cy);
        }
    }
    else
        ps1k_info_bar("NO GAME MATCHES", "O: ALL GAMES", ps1k_wx(-40), ps1k_wx(680), 411);
}

/* ---------------------------------------------------------------- drawing: the game page */

/* glyphs on the action spheres: dark ink, as the shell's transport glyphs */
#define INK 0xff101010u

static void glyph_play(float cx, float cy, float s)
{
    const float x = PS1K_X(cx), y = PS1K_Y(cy), k = s * PS1K_S / 48.0f;
    PlatVertex v[3] = {{x - 9 * k, y - 10 * k, 0, 0, INK}, {x + 12 * k, y, 0, 0, INK}, {x - 9 * k, y + 10 * k, 0, 0, INK}};
    plat_draw_mesh(NULL, v, 3, NULL, 0);
}

static void glyph_star(float cx, float cy, bool filled)
{
    const float x = PS1K_X(cx), y = PS1K_Y(cy) + 0.4f * PS1K_S, ro = 10.5f * PS1K_S, ri = 5.4f * PS1K_S, t = 1.8f * PS1K_S;
    float px[10], py[10], qx[10], qy[10];
    for (int i = 0; i < 10; ++i)
    {
        const float a = i * 3.14159265f / 5, r = i % 2 ? ri : ro;
        px[i] = x + r * sinf(a), py[i] = y - r * cosf(a);
        /* the inner edge of the outline: each point pulled toward the middle by the stroke */
        const float rr = (i % 2 ? ri : ro) - t * (i % 2 ? 1.2f : 2.2f);
        qx[i] = x + rr * sinf(a), qy[i] = y - rr * cosf(a);
    }
    PlatVertex v[60];
    int n = 0;
    for (int i = 0; i < 10; ++i)
    {
        const int j = (i + 1) % 10;
        v[n++] = (PlatVertex){px[i], py[i], 0, 0, INK}, v[n++] = (PlatVertex){px[j], py[j], 0, 0, INK},
        v[n++] = (PlatVertex){qx[j], qy[j], 0, 0, INK};
        v[n++] = (PlatVertex){px[i], py[i], 0, 0, INK}, v[n++] = (PlatVertex){qx[j], qy[j], 0, 0, INK},
        v[n++] = (PlatVertex){qx[i], qy[i], 0, 0, INK};
    }
    plat_draw_mesh(NULL, v, n, NULL, 0);
    if (!filled)
        return;
    const uint32_t gold = 0xffffce30u;
    n = 0;
    for (int i = 0; i < 10; ++i)
    {
        const int j = (i + 1) % 10;
        v[n++] = (PlatVertex){x, y, 0, 0, gold}, v[n++] = (PlatVertex){qx[i], qy[i], 0, 0, gold},
        v[n++] = (PlatVertex){qx[j], qy[j], 0, 0, gold};
    }
    plat_draw_mesh(NULL, v, n, NULL, 0);
}

static void glyph_cog(float cx, float cy)
{
    const float x = PS1K_X(cx), y = PS1K_Y(cy), r = 6.5f * PS1K_S;
    draw_circle(x, y, r, INK);
    for (int i = 0; i < 8; ++i)
    {
        const float a = i * 3.14159265f / 4, c = cosf(a), s = sinf(a), w = 2.4f * PS1K_S, l = 9.5f * PS1K_S;
        PlatVertex v[6];
        const float ax = x + c * r * 0.6f - s * w * 0.5f, ay = y + s * r * 0.6f + c * w * 0.5f;
        const float bx = x + c * r * 0.6f + s * w * 0.5f, by = y + s * r * 0.6f - c * w * 0.5f;
        v[0] = (PlatVertex){ax, ay, 0, 0, INK}, v[1] = (PlatVertex){bx, by, 0, 0, INK};
        v[2] = (PlatVertex){bx + c * (l - r * 0.6f), by + s * (l - r * 0.6f), 0, 0, INK};
        v[3] = v[0], v[4] = v[2];
        v[5] = (PlatVertex){ax + c * (l - r * 0.6f), ay + s * (l - r * 0.6f), 0, 0, INK};
        plat_draw_mesh(NULL, v, 6, NULL, 0);
    }
    draw_circle(x, y, 2.6f * PS1K_S, 0xff2a2a90u);
}

static void glyph_restart(float cx, float cy)
{
    const float x = PS1K_X(cx), y = PS1K_Y(cy), r = 7.0f * PS1K_S, w = 3.0f * PS1K_S;
    enum { N = 20 };
    PlatVertex v[N * 6 + 3];
    int n = 0;
    const float a0 = -1.2f, a1 = 4.2f; /* most of a circle, open at the top right */
    for (int i = 0; i < N; ++i)
    {
        const float a = a0 + (a1 - a0) * i / N, b = a0 + (a1 - a0) * (i + 1) / N;
        const float ro = r + w * 0.5f, ri = r - w * 0.5f;
        PlatVertex p0 = {x + cosf(a) * ro, y + sinf(a) * ro, 0, 0, INK}, p1 = {x + cosf(b) * ro, y + sinf(b) * ro, 0, 0, INK},
                   p2 = {x + cosf(b) * ri, y + sinf(b) * ri, 0, 0, INK}, p3 = {x + cosf(a) * ri, y + sinf(a) * ri, 0, 0, INK};
        v[n++] = p0, v[n++] = p1, v[n++] = p2, v[n++] = p0, v[n++] = p2, v[n++] = p3;
    }
    /* the arrowhead at the start, pointing along the turn */
    const float hx = x + cosf(a0) * r, hy = y + sinf(a0) * r, h = 5.0f * PS1K_S;
    v[n++] = (PlatVertex){hx - h, hy - h * 0.2f, 0, 0, INK};
    v[n++] = (PlatVertex){hx + h * 0.7f, hy - h * 0.9f, 0, 0, INK};
    v[n++] = (PlatVertex){hx + h * 0.5f, hy + h * 0.9f, 0, 0, INK};
    plat_draw_mesh(NULL, v, n, NULL, 0);
}

static void wrap2(const char *s, float width, char a[64], char b[64])
{
    /* the title on two ROM-font lines at most, broken at a space */
    a[0] = b[0] = '\0';
    char words[160];
    str_copy(words, sizeof(words), s);
    char *save = NULL;
    int line = 0;
    for (char *w = strtok_r(words, " ", &save); w; w = strtok_r(NULL, " ", &save))
    {
        char *dst = line ? b : a;
        char trial[130];
        snprintf(trial, sizeof(trial), "%s%s%s", dst, dst[0] ? " " : "", w);
        if (ps1k_rom_width(trial, 8) > width && dst[0] && line == 0)
        {
            line = 1;
            dst = b;
            snprintf(trial, sizeof(trial), "%s", w);
        }
        else if (ps1k_rom_width(trial, 8) > width && line == 1)
        {
            size_t n = strlen(dst);
            if (n > 2)
                dst[n - 1] = '.';
            break;
        }
        str_copy(dst, 64, trial);
    }
}

static void draw_game(void)
{
    const int game = V.game;
    const Game *g = &app.library.games[game];
    GameStats *st = stats_get(g->id);
    const bool multi = g->discs > 1, bg_only = in_bg();
    ps1k_background();
    if (bg_only)
        return;
    const float COLX = ps1k_ox(58);
    /* the action column, at the original transport slots (66 + 57 n) */
    static const int SPH_COL[SPHERES] = {PS1K_GREEN, PS1K_GREEN, PS1K_BLUE, PS1K_RED};
    for (int n = 0; n < SPHERES; ++n)
    {
        const float y = 66 + 57 * n;
        ps1k_sphere(SPH_COL[n], COLX, y, n == 0 ? 60 : 48, 128);
        switch (n)
        {
        case 0: glyph_play(COLX, y, 60); break;
        case 1: glyph_star(COLX, y, V.rec.favourite); break;
        case 2: glyph_cog(COLX, y); break;
        default: glyph_restart(COLX, y); break;
        }
    }
    /* slots 5-7: the original column's spheres (gold, yellow, yellow), without a function: the column complete */
    static const int SPARE[COLUMN_SLOTS - SPHERES] = {PS1K_GOLD, PS1K_YELLOW, PS1K_YELLOW};
    for (int n = SPHERES; n < COLUMN_SLOTS; ++n)
        ps1k_sphere(SPARE[n - SPHERES], COLX, 66 + 57 * n, 48, 128);
    /* top: SAVES (or DISC n/m), HRS : MIN */
    char buf[32];
    ps1k_sphere(PS1K_BLUE, 128, 94, 56, 128);
    if (multi)
    {
        snprintf(buf, sizeof(buf), "%d/%d", V.rec.boot_disc + 1, g->discs);
        ps1k_digits(buf, 128, 94, 0.85f, 128);
        ps1k_label_sphere("DISC", 99, 121, 36, 9);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%d", V.slots_used);
        ps1k_digits(buf, 128, 94, 1.0f, 128);
        ps1k_label_sphere("SAVES", 99, 121, 36, 8);
    }
    const unsigned secs = st ? st->seconds : 0;
    snprintf(buf, sizeof(buf), "%u", secs / 3600 > 999 ? 999 : secs / 3600);
    ps1k_sphere(PS1K_BLUE, 225, 94, 56, 128);
    ps1k_digits(buf, 225, 94, secs / 3600 > 99 ? 0.8f : 1.0f, 128);
    snprintf(buf, sizeof(buf), "%02u", secs / 60 % 60);
    ps1k_sphere(PS1K_BLUE, 300, 94, 56, 128);
    ps1k_digits(buf, 300, 94, 1.0f, 128);
    ps1k_sphere(PS1K_GOLD, 263, 116, 14, 128);
    ps1k_label_sphere("HRS", 196, 121, 36, 10);
    ps1k_label_sphere("MIN", 328, 121, 36, 10);
    /* the box: its frame, the cover (or the slot's preview) left, the words right */
    ps1k_plaque(110, 224, 2, 130, false, 128);
    ps1k_plaque(230, 160, 242, 2, false, 128);
    ps1k_plaque(350, 224, 2, 130, false, 128);
    ps1k_plaque(230, 288, 242, 2, false, 128);
    const float CX = 174, CY = 224, CS = 112, TX = 240;
    char line[96];
    if (V.focus == F_SLOT && V.slot_used[V.item - 1])
    {
        const int slot = V.item - 1;
        if (V.thumb_slot != slot)
        {
            free_thumb();
            V.thumb_slot = slot;
            static uint8_t rgba[THUMB_W * THUMB_H * 4];
            char path[SwanStationPS5_PATH_MAX];
            slot_path(g, slot, path, sizeof(path));
            if (play_load_thumb(path, rgba))
                V.thumb = plat_texture_create(rgba, THUMB_W, THUMB_H, true);
        }
        if (V.thumb)
            ps1k_image(V.thumb, CX, CY, CS, CS * THUMB_H / THUMB_W, 128, false);
        else
            ps1_draw_cover(game, CX, CY, CS, CS, 64);
        snprintf(line, sizeof(line), "SAVE %d", V.item);
        ps1k_rom(line, TX, 180, PS1K_LEFT, 8, 12);
        date_text(V.slot_time[slot], line, sizeof(line), false);
        ps1k_rom(line, TX, 197, PS1K_LEFT, 8, 12);
        struct tm tmv;
        time_t tt = V.slot_time[slot];
        localtime_r(&tt, &tmv);
        snprintf(line, sizeof(line), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        ps1k_rom(line, TX, 214, PS1K_LEFT, 8, 12);
        if (slot == V.newest)
            ps1k_word("NEWEST", TX, 236, 9, PS1K_LEFT, 128);
    }
    else
    {
        ps1_draw_cover(game, CX, CY, CS, CS, 128);
        char title[160], a[64], b[64];
        upper(display_title(g), title, sizeof(title));
        wrap2(title, 104, a, b);
        ps1k_rom(a, TX, 180, PS1K_LEFT, 8, 12);
        ps1k_rom(b, TX, 197, PS1K_LEFT, 8, 12);
        ps1k_word("LAST", TX, 222, 9, PS1K_LEFT, 128);
        ps1k_word("PLAYED", TX, 236, 9, PS1K_LEFT, 128);
        date_text(st ? st->last_played : 0, line, sizeof(line), true);
        ps1k_rom(line, TX, 252, PS1K_LEFT, 8, 12);
    }
    /* the bottom line names what the arrow is on; a toggle adds ON / OFF; never discs */
    int plates[PL_COUNT];
    const int np = plate_list(plates);
    line[0] = '\0';
    bool small = false;
    if (V.focus == F_SPHERE)
    {
        if (V.item == 1)
            snprintf(line, sizeof(line), "FAVOURITE %s", V.rec.favourite ? "ON" : "OFF"), small = true;
        else
            str_copy(line, sizeof(line), SPHERE_WORD[V.item]);
    }
    else if (V.focus == F_PLATE && plates[V.item] != PL_DISC)
        snprintf(line, sizeof(line), "%s %s", PLATE_WORD[plates[V.item]], plate_on(plates[V.item]) ? "ON" : "OFF"), small = true;
    else if (V.focus == F_EXIT)
        str_copy(line, sizeof(line), "EXIT");
    else if (V.focus == F_SLOT)
    {
        if (V.slot_used[V.item - 1])
            snprintf(line, sizeof(line), "LOAD SAVE %d", V.item);
        else
            snprintf(line, sizeof(line), "SAVE %d EMPTY", V.item), small = true;
    }
    if (line[0])
        ps1k_word(line, TX, 276, small ? 9 : 11, PS1K_LEFT, 128);
    /* the plates: lit = on, 64 = off */
    for (int i = 0; i < np; ++i)
    {
        const int p = plates[i], on = plate_on(p);
        const int b = on == 0 ? 64 : 128;
        ps1k_plaque(PLATE_XY[p][0], PLATE_XY[p][1], 92, 24, false, b);
        float cap = 11;
        while (cap > 7 && ps1k_word_width(PLATE_WORD[p], cap) > 84)
            cap -= 0.5f;
        ps1k_word(PLATE_WORD[p], PLATE_XY[p][0], PLATE_XY[p][1], cap, PS1K_CENTRE, b);
    }
    /* the save slots: the track grid; empty = 8 dot, used = 40 with its number, newest brightest */
    int order[PS1_SLOTS], used = 0;
    for (int i = 0; i < PS1_SLOTS; ++i)
        if (V.slot_used[i])
            order[used++] = i;
    for (int a = 0; a < used; ++a) /* newest first */
        for (int b = a + 1; b < used; ++b)
            if (V.slot_time[order[b]] > V.slot_time[order[a]])
            {
                const int t = order[a];
                order[a] = order[b], order[b] = t;
            }
    for (int i = 0; i < PS1_SLOTS; ++i)
    {
        const int r = i / 5, c = i % 5, col = PS1K_CD_ROWS[r][c];
        const float x = ps1k_ox(402) + 48 * c, y = (float)(136 + 48 * r);
        if (!V.slot_used[i])
        {
            ps1k_sphere(col, x, y, 8, 128);
            continue;
        }
        int rank = 0;
        for (int k = 0; k < used; ++k)
            if (order[k] == i)
                rank = k;
        const int b = rank == 0 ? 176 : (128 - 16 * (rank - 1) < 72 ? 72 : 128 - 16 * (rank - 1));
        ps1k_sphere(col, x, y, 40, b);
        snprintf(buf, sizeof(buf), "%d", i + 1);
        ps1k_digits(buf, x, y, 1.0f, 128);
    }
    /* the title plate and EXIT */
    {
        /* the whole title, its trailing region / language tags left out ("(Europe) (En,Fr,De)", "[SLES-...]": the box
         * below names the release); smaller to half size, then two lines. Room: from the MIN sphere to the plate's
         * right edge. */
        char title[256];
        upper(display_title(g), title, sizeof(title));
        for (size_t n = strlen(title); n > 0;)
        {
            while (n > 0 && title[n - 1] == ' ')
                title[--n] = '\0';
            const char close = n ? title[n - 1] : 0, open = close == ')' ? '(' : close == ']' ? '[' : 0;
            char *o = open ? strrchr(title, open) : NULL;
            if (!o || o == title)
                break;
            *o = '\0';
            n = strlen(title);
        }
        const float right = ps1k_ox(622);
        ps1k_plate_fit(title, right, 67, 14, right - 352);
    }
    ps1k_splat(5, ps1k_ox(480), 410, 220, 60, 128);
    ps1k_word("EXIT", ps1k_ox(480), 410, V.focus == F_EXIT ? 22 * 1.25f : 22, PS1K_CENTRE, 128);
    if (V.confirm)
    {
        /* the memory card's dialog: DELETE FILE / Are you sure? / YES NO */
        ps1k_plaque(320, 240, 260, 120, false, 128);
        ps1k_word("DELETE", 280, 200, 15, PS1K_CENTRE, 128);
        ps1k_word("FILE", 370, 200, 15, PS1K_CENTRE, 128);
        ps1k_word("Are you sure?", 320, 228, 14, PS1K_CENTRE, 128);
        static const char *const YN[2] = {"YES", "NO"};
        for (int i = 0; i < 2; ++i)
        {
            const float x = i ? 367 : 273;
            ps1k_sphere(PS1K_BLUE, x, 266, 40, 128);
            ps1k_word(YN[i], x, 266, 12, PS1K_CENTRE, 128);
        }
        ps1k_cursor(V.confirm_sel ? 367 : 273, 266);
        return;
    }
    if (V.focus == F_SPHERE)
        ps1k_cursor(COLX, 66 + 57 * V.item);
    else if (V.focus == F_PLATE)
        ps1k_cursor(PLATE_XY[plates[V.item]][0], PLATE_XY[plates[V.item]][1]);
    else if (V.focus == F_EXIT)
        ps1k_cursor(ps1k_ox(480), 410);
    else
        ps1k_cursor(ps1k_ox(402) + 48 * ((V.item - 1) % 5), (float)(136 + 48 * ((V.item - 1) / 5)));
}

/* ---------------------------------------------------------------- shown titles */

static bool ci_prefix(const char *s, const char *word)
{
    for (; *word; ++s, ++word)
        if (tolower((unsigned char)*s) != *word)
            return false;
    return true;
}

/* is s (case-insensitive) "disc" or "cd" followed by an optional space and a digit? its length, else 0 */
static int disc_word(const char *s)
{
    int n = 0;
    if (ci_prefix(s, "disc"))
        n = 4;
    else if (ci_prefix(s, "cd"))
        n = 2;
    else
        return 0;
    if (s[n] == ' ')
        ++n;
    if (!isdigit((unsigned char)s[n]))
        return 0;
    while (isdigit((unsigned char)s[n]))
        ++n;
    if (ci_prefix(s + n, " of ") && isdigit((unsigned char)s[n + 4]))
        for (n += 4; isdigit((unsigned char)s[n]); ++n)
            ;
    return n;
}

/* A title as the PS1 shelf shows it: the disc tags of a grouped multi-disc game left out ("(Disc 1)", "(Disc 1 of 3)",
 * "[Disc 1]", "(CD1)", "- Disc 1", "CD 2"; the game page's DISC n/m bubble says it), and an article moved back to the
 * front as people say it ("Legend of Dragoon, The (Europe)" -> "The Legend of Dragoon (Europe)") */
static const char *ps1_title(const char *name)
{
    static char ring[4][256];
    static int at;
    char *out = ring[at = (at + 1) % 4];
    size_t w = 0;
    for (const char *p = name; *p && w + 1 < sizeof(ring[0]);)
    {
        const char open = *p, close = open == '(' ? ')' : open == '[' ? ']' : 0;
        int n;
        if (close && (n = disc_word(p + 1)) > 0 && p[1 + n] == close)
        {
            p += n + 2;
            continue;
        }
        const bool word_start = p == name || p[-1] == ' ' || p[-1] == '-';
        if (word_start && (n = disc_word(p)) > 0 && (p[n] == '\0' || p[n] == ' ' || p[n] == '('))
        {
            /* a bare "Disc 1" / "CD2" word, with its " - " before it */
            while (w > 0 && (out[w - 1] == ' ' || out[w - 1] == '-'))
                --w;
            p += n;
            continue;
        }
        out[w++] = *p++;
    }
    out[w] = '\0';
    /* tidy the spaces left behind */
    char *d = out;
    for (const char *c = out; *c; ++c)
        if (!(*c == ' ' && (d == out || d[-1] == ' ')))
            *d++ = *c;
    while (d > out && d[-1] == ' ')
        --d;
    *d = '\0';
    /* ", The" before the end or before the tags: to the front */
    char *the = strstr(out, ", The");
    if (the && (the[5] == '\0' || the[5] == ' '))
    {
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "The %.*s%s", (int)(the - out), out, the + 5);
        str_copy(out, sizeof(ring[0]), tmp);
    }
    return out;
}

/* ---------------------------------------------------------------- search: the CD Player's keypad */

/* Full text: the typed letters anywhere in the title, upper or lower case alike, punctuation and spaces left out */
static void search_key(const char *in, char *out, size_t size)
{
    size_t n = 0;
    for (const unsigned char *c = (const unsigned char *)in; *c && n + 1 < size; ++c)
        if (isalnum(*c))
            out[n++] = (char)tolower(*c);
    out[n] = '\0';
}

static bool ps1_search_match(const Game *g, const char *text)
{
    char needle[FILTER_MAX + 1], hay[256];
    search_key(text, needle, sizeof(needle));
    if (!needle[0])
        return true;
    search_key(display_title(g), hay, sizeof(hay));
    if (strstr(hay, needle))
        return true;
    search_key(g->title, hay, sizeof(hay));
    return strstr(hay, needle) != NULL;
}

/* 7 x 4 keys as the '>20' keypad: A-Z, # (the digits page) and DEL; the digits page has 1-0, ABC and DEL */
#define KP_COLS 7
#define KP_KEYS 28
static const char *kp_label(int k)
{
    static const char *const LETTERS[KP_KEYS] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N",
                                                 "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "#", "DEL"};
    static const char *const DIGITS[KP_KEYS] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", NULL, NULL, NULL, NULL,
                                                NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                                "ABC", "DEL"};
    return k < 0 || k >= KP_KEYS ? NULL : (V.kp_digits ? DIGITS : LETTERS)[k];
}

/* the next key that way: along the row, or the nearest one in the next row that has any; -1 at the edge */
static int kp_step(int k, int dc, int dr)
{
    const int r = k / KP_COLS, c = k % KP_COLS;
    if (dc)
    {
        for (int cc = c + dc; cc >= 0 && cc < KP_COLS; cc += dc)
            if (kp_label(r * KP_COLS + cc))
                return r * KP_COLS + cc;
        return -1;
    }
    for (int rr = r + dr; rr >= 0 && rr < KP_KEYS / KP_COLS; rr += dr)
    {
        int best = -1;
        for (int cc = 0; cc < KP_COLS; ++cc)
            if (kp_label(rr * KP_COLS + cc) && (best < 0 || abs(cc - c) < abs(best % KP_COLS - c)))
                best = rr * KP_COLS + cc;
        if (best >= 0)
            return best;
    }
    return -1;
}

static void kp_close(void)
{
    F.open = false;
    if (!F.text[0])
    {
        /* nothing typed: the filter it had before */
        app.global.shelf_category = F.before_category;
        app.settings.shelf_category = F.before_category;
        S.filter[0] = '\0';
        build_view(selected_game());
    }
}

static void ps1_search_input(uint32_t pressed)
{
    const int before = V.kp_key;
    int to = -1;
    if (pressed & BIT(BTN_LEFT))
        to = kp_step(V.kp_key, -1, 0);
    else if (pressed & BIT(BTN_RIGHT))
        to = kp_step(V.kp_key, 1, 0);
    else if (pressed & BIT(BTN_UP))
        to = kp_step(V.kp_key, 0, -1);
    else if (pressed & BIT(BTN_DOWN))
        to = kp_step(V.kp_key, 0, 1);
    if (to >= 0)
        V.kp_key = to;
    ps1k_move_sound(V.kp_key != before, pressed, BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_UP) | BIT(BTN_DOWN));
    size_t n = strlen(F.text);
    if (pressed & BIT(BTN_CROSS))
    {
        const char *k = kp_label(V.kp_key);
        if (V.kp_key == KP_KEYS - 2)
        {
            V.kp_digits = !V.kp_digits; /* # / ABC: the other page */
            sfx_play(SFX_PAGE);
        }
        else if (V.kp_key == KP_KEYS - 1 || !k)
            pressed |= BIT(BTN_SQUARE); /* DEL */
        else if (n < FILTER_MAX)
        {
            F.text[n] = k[0];
            F.text[n + 1] = '\0';
            sfx_play(SFX_SELECT);
            filter_changed();
        }
    }
    n = strlen(F.text);
    if (pressed & BIT(BTN_SQUARE))
    {
        if (n)
        {
            F.text[n - 1] = '\0';
            sfx_play(SFX_BACK);
            filter_changed();
        }
    }
    if ((pressed & BIT(BTN_TRIANGLE)) && n)
    {
        F.text[0] = '\0';
        sfx_play(SFX_BACK);
        filter_changed();
    }
    if (pressed & (BIT(BTN_CIRCLE) | BIT(BTN_START)))
    {
        kp_close(); /* the matches stay on the shelf (O there shows all the games again) */
        sfx_play(SFX_CLOSE);
    }
}

static void ps1_search_draw(void)
{
    const float cy = 252;
    ps1k_plaque(320, cy, 404, 236, false, 128);
    ps1k_word("SEARCH", 320, cy - 92, 17, PS1K_CENTRE, 128);
    float fx = 320, fy = cy;
    for (int k = 0; k < KP_KEYS; ++k)
    {
        const char *lab = kp_label(k);
        const int r = k / KP_COLS, c = k % KP_COLS;
        const float x = 320 + (c - 3) * 52, y = cy - 46 + r * 46;
        if (!lab)
        {
            ps1k_sphere(PS1K_CD_ROWS[r][c < 5 ? c : 4], x, y, 8, 64);
            continue;
        }
        static const int TAIL[4] = {PS1K_GREEN, PS1K_GREEN, PS1K_BLUE, PS1K_BLUE};
        const int col = c < 5 ? PS1K_CD_ROWS[r][c] : TAIL[r];
        const bool f = k == V.kp_key;
        ps1k_sphere(col, x, y, f ? 50 : 40, 128);
        const float cap = strlen(lab) == 1 ? 13 : 9;
        ps1k_word(lab, x, y, f ? cap * 1.25f : cap, PS1K_CENTRE, 128);
        if (f)
            fx = x, fy = y;
    }
    char left[FILTER_MAX + 16], right[32];
    snprintf(left, sizeof(left), "SEARCH: %s", F.text);
    for (char *c = left; *c; ++c)
        *c = (char)toupper((unsigned char)*c);
    if (F.text[0])
        snprintf(right, sizeof(right), "%d FOUND", S.view_count);
    else
        snprintf(right, sizeof(right), "%d GAMES", S.view_count);
    ps1k_info_bar(left, right, ps1k_wx(-40), ps1k_wx(680), 411);
    ps1k_cursor(fx, fy);
}

/* ---------------------------------------------------------------- the cover picker (Triangle on the game page) */

#define PK_ROWS 7
static void ps1_picker_draw(void)
{
    ps1k_background();
    float cap = 20;
    while (cap > 10 && ps1k_word_width("CHOOSE COVER", cap) + 36 > 220)
        cap -= 1;
    ps1k_plate("CHOOSE COVER", ps1k_ox(626), 67, cap);
    /* the rows: a dot for where the picture is from (gold automatic, green the game's folder, blue covers/) */
    int top = P.cursor - PK_ROWS / 2;
    if (top > P.count - PK_ROWS)
        top = P.count - PK_ROWS;
    if (top < 0)
        top = 0;
    const float lx = ps1k_wx(-60), rx = ps1k_wx(440), y0 = 132, dy = 40;
    float cur_y = -1;
    const size_t covers_len = strlen(app.paths.covers);
    for (int k = 0; k < PK_ROWS && top + k < P.count; ++k)
    {
        const int i = top + k;
        const float y = y0 + k * dy;
        const bool f = i == P.cursor;
        if (f)
        {
            ps1k_plaque((lx + rx) * 0.5f - 6, y, rx - lx + 28, 32, true, 128);
            cur_y = y;
        }
        char name[160];
        int dot = PS1K_GOLD;
        if (i == 0)
            str_copy(name, sizeof(name), "AUTOMATIC");
        else
        {
            const char *slash = strrchr(P.files[i], '/');
            upper(slash ? slash + 1 : P.files[i], name, sizeof(name));
            dot = strncmp(P.files[i], app.paths.covers, covers_len) == 0 ? PS1K_BLUE : PS1K_GREEN;
        }
        size_t n = strlen(name);
        while (n > 4 && ps1k_rom_width(name, f ? 12 : 10) > rx - lx - 30)
        {
            name[n - 3] = '.', name[n - 2] = '.', name[n - 1] = '\0';
            --n;
        }
        ps1k_sphere(dot, lx, y, f ? 18 : 12, 128);
        ps1k_rom(name, lx + 18, y, PS1K_LEFT, f ? 12 : 10, f ? 17 : 14);
    }
    if (top > 0)
        ps1k_sphere(PS1K_BLUE, (lx + rx) * 0.5f, y0 - 24, 10, 128);
    if (top + PK_ROWS < P.count)
        ps1k_sphere(PS1K_BLUE, (lx + rx) * 0.5f, y0 + PK_ROWS * dy - 16, 10, 128);
    /* the picture in the game page's box frame */
    const float bx = ps1k_ox(532), by = 236, bs = 190;
    ps1k_plaque(bx - bs * 0.5f - 6, by, 2, bs + 12, false, 128);
    ps1k_plaque(bx + bs * 0.5f + 6, by, 2, bs + 12, false, 128);
    ps1k_plaque(bx, by - bs * 0.5f - 6, bs + 14, 2, false, 128);
    ps1k_plaque(bx, by + bs * 0.5f + 6, bs + 14, 2, false, 128);
    if (P.open && P.preview_for != P.cursor)
    {
        plat_texture_free(P.preview);
        P.preview = NULL;
        P.preview_for = P.cursor;
        int iw, ih;
        uint8_t *rgba = P.cursor > 0 ? covers_decode(P.files[P.cursor], &iw, &ih) : NULL;
        if (rgba)
        {
            P.preview = plat_texture_create_mips(rgba, iw, ih);
            free(rgba);
        }
    }
    PlatTexture *shown = P.cursor > 0 ? P.preview : shelf_cover_or_none(P.game);
    if (shown)
    {
        int iw, ih;
        plat_texture_size(shown, &iw, &ih);
        const float k = fminf(bs / iw, bs / ih);
        ps1k_image(shown, bx, by, iw * k, ih * k, 128, false);
    }
    else
        ps1k_rom("NO PICTURE", bx, by, PS1K_CENTRE, 10, 14);
    /* the info bar: what the row is, and where */
    char left[160], right[32];
    if (P.count == 1)
        str_copy(left, sizeof(left), P.unlisted ? "UNLOCK /DATA IN SETTINGS > SYSTEM" : "PUT .PNG / .JPG IN /DATA/SWANSTATIONPS5/COVERS");
    else if (P.cursor == 0)
        str_copy(left, sizeof(left), "BY SERIAL OR NAME");
    else
        str_copy(left, sizeof(left), strncmp(P.files[P.cursor], app.paths.covers, covers_len) == 0 ? "COVERS FOLDER" : "GAME FOLDER");
    snprintf(right, sizeof(right), "%d / %d", P.cursor + 1, P.count);
    ps1k_info_bar(left, right, ps1k_wx(-40), ps1k_wx(680), 411);
    if (cur_y >= 0)
        ps1k_cursor(lx + 40, cur_y);
}

/* ---------------------------------------------------------------- the frame */

/* Holding a button to do something drastic (O to close the app): ten balls fill up */
static void ps1_hold_meter(const char *what, float t)
{
    char caps[96];
    ps1_caps(what, caps, sizeof(caps));
    const float y = 1010;
    draw_rect(0, y - 20, 1920, 90, 0xc0000120u);
    ps1_text_drop(96, y, 28, FONT_PS1, PS1_LABEL, ALIGN_LEFT, caps, 3);
    const float x0 = 96 + text_width(28, FONT_PS1, caps) + 40;
    const int lit = (int)(fminf(t, 1.0f) * 10.0f + 0.5f);
    for (int i = 0; i < 10; ++i)
        ps1_ball(i < lit ? PS1_BALL_RED : PS1_BALL_BLUE, x0 + i * 44 + 16, y + 18, i < lit ? 34 : 20, NULL, 0, 0, false, 1.0f);
}

static void ps1_shelf_draw(int game)
{
    if (ps1_setup_active())
    {
        ps1_setup_draw();
        return;
    }
    (void)game;
    ps1_tick(0);
    if (ps1_pslogo_frame())
        return; /* the opening's PlayStation screen; the main menu fades in after it */
    if (V.mode == V_MENU)
        ps1_pslogo_scan_later(); /* once, with the menu up: the logo read from a disc for the next start (a setting) */
    if (V.trans_to >= 0)
    {
        V.trans_t += app.dt;
        if (V.trans_t >= (FLASH_TICKS + V.bg_ticks) * TICK)
        {
            const int to = V.trans_to;
            if (to == V_TO_SETTINGS)
            {
                cut_to(V_MENU);
                config_save(&app.global, app.paths.config);
                app_open_shelf_settings(NULL);
                /* this frame stays the background alone: no frame of the menu before Settings fades in */
                ps1k_background();
                return;
            }
            else
                cut_to(to);
        }
    }
    if (V.mode == V_GAME && (V.game < 0 || V.game >= app.library.count))
        cut_to(V_LIBRARY);
    if (V.mode == V_GAME && ps1_time() - V.seen_t > 0.25f)
    {
        /* back from another screen (this game's settings): its plates and saves as they are now */
        const int f = V.focus, it = V.item;
        load_page(V.game);
        V.focus = f, V.item = it;
    }
    V.seen_t = ps1_time();
    switch (V.mode)
    {
    case V_MENU: draw_menu(); break;
    case V_LIBRARY: draw_library(); break;
    default: draw_game(); break;
    }
    picker_draw();
    filter_draw();
    /* a new screen (or back from another one) fades in over the background: no hard cut */
    ps1k_screen_fade(1000 + V.mode * 2 + (P.open ? 1 : 0));
    if (start_black)
    {
        start_black = false;
        draw_rect(0, 0, 1920, 1080, 0xff000000u);
    }
    if (S.launch_t > 0.0f)
    {
        /* the screen darkens and a disc spins up in the middle before the fade to black */
        const float total = launch_total(), t = S.launch_t;
        const float launch = fmaxf(t - (total - LAUNCH_TIME), 0.0f) / LAUNCH_TIME;
        const bool disc = false; /* a clean fade to black, then the game (no disc spinning up) */
        draw_rect(0, 0, 1920, 1080, argb_alpha(0xff000000u, disc ? fminf(t / 0.3f, 1.0f) * 0.75f + launch * 0.25f : launch));
        if (disc)
        {
            const float grow = fminf(t / DISC_TIME, 1.0f), e = 1.0f - (1.0f - grow) * (1.0f - grow);
            ps1_disc(CENTER_X, 520, 260 + 160 * e, t * t * 9.0f);
            draw_rect(0, 0, 1920, 1080, argb_alpha(0xff000000u, launch));
        }
    }
    if (S.quit_hold > 0.5f)
        ps1_hold_meter(tr("Keep holding to close SwanStationPS5..."), S.quit_hold / QUIT_HOLD);
    app_draw_toast();
}

/* Hook (main.c): the app was started again after a game (it restarts to give the game's memory back): the shelf opens
 * on that game's page, as if it had never left */
void ps1_shelf_after_game(void)
{
    V.open_last = true;
}
