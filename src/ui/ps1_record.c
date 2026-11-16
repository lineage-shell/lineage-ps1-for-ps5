/*
 * SwanStationPS5 - the PS1 shelf's small per-game record: play time, last played, favourite and the boot disc, in
 * <states>/<game id>.shelf next to the game's save states (one short text file a game, so it travels with the saves).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The app's own stats.txt (stats.c) stays the in-memory record the rest of the app reads; this file mirrors it per
 * game. On load the two are merged (the larger play time, the later date, the file's favourite), so either one can be
 * missing or older without anything being lost.
 */
#include "ps1_record.h"

#include "../app.h"
#include "../stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void record_path(const Game *g, char *out, size_t size)
{
    char file[96];
    snprintf(file, sizeof(file), "%.80s.shelf", g->id);
    path_join(out, size, app.paths.states, file);
}

static bool read_file(const Game *g, Ps1Record *r)
{
    char path[SwanStationPS5_PATH_MAX];
    record_path(g, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[128];
    while (fgets(line, sizeof(line), f))
    {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq++ = '\0';
        if (!strcmp(line, "seconds"))
            r->seconds = (uint32_t)strtoul(eq, NULL, 10);
        else if (!strcmp(line, "last_played"))
            r->last_played = (int64_t)strtoll(eq, NULL, 10);
        else if (!strcmp(line, "favourite"))
            r->favourite = atoi(eq) != 0;
        else if (!strcmp(line, "boot_disc"))
            r->boot_disc = atoi(eq);
    }
    fclose(f);
    return true;
}

static void write_file(const Game *g, const Ps1Record *r)
{
    char path[SwanStationPS5_PATH_MAX], tmp[SwanStationPS5_PATH_MAX + 8];
    record_path(g, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    fprintf(f, "seconds=%u\nlast_played=%lld\nfavourite=%d\nboot_disc=%d\n", r->seconds, (long long)r->last_played,
            r->favourite ? 1 : 0, r->boot_disc);
    const bool ok = fclose(f) == 0;
    if (ok)
        rename(tmp, path);
    else
        remove(tmp);
}

/* the disc choice is the file's alone; the rest lives in stats too */
static int disc_of(const Game *g)
{
    Ps1Record r = {0};
    read_file(g, &r);
    return r.boot_disc;
}

void ps1_record_get(const Game *g, Ps1Record *out)
{
    memset(out, 0, sizeof(*out));
    if (!g || game_is_bios(g))
        return;
    GameStats *st = stats_get(g->id);
    if (st)
    {
        out->seconds = st->seconds;
        out->last_played = st->last_played;
        out->favourite = st->favorite;
    }
    out->boot_disc = disc_of(g);
}

void ps1_record_merge_all(void)
{
    for (int i = 0; i < app.library.count; ++i)
    {
        const Game *g = &app.library.games[i];
        Ps1Record r = {0};
        if (!read_file(g, &r))
            continue;
        GameStats *st = stats_get(g->id);
        if (!st)
            continue;
        if (r.seconds > st->seconds)
            st->seconds = r.seconds;
        if (r.last_played > st->last_played)
            st->last_played = r.last_played;
        st->favorite = r.favourite;
    }
}

void ps1_record_sync(const Game *g)
{
    if (!g || game_is_bios(g))
        return;
    Ps1Record r;
    ps1_record_get(g, &r);
    write_file(g, &r);
}

void ps1_record_set_favourite(const Game *g, bool on)
{
    GameStats *st = g ? stats_get(g->id) : NULL;
    if (!st)
        return;
    st->favorite = on;
    stats_save_async();
    ps1_record_sync(g);
}

void ps1_record_set_boot_disc(const Game *g, int disc)
{
    if (!g || game_is_bios(g))
        return;
    Ps1Record r;
    ps1_record_get(g, &r);
    r.boot_disc = disc;
    write_file(g, &r);
}
