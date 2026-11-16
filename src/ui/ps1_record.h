/*
 * SwanStationPS5 - the PS1 shelf's per-game record (<states>/<id>.shelf): see ps1_record.c.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SwanStationPS5_PS1_RECORD_H
#define SwanStationPS5_PS1_RECORD_H

#include "../library.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t seconds;    /* total play time */
    int64_t last_played; /* unix time, 0 = never */
    bool favourite;
    int boot_disc;       /* 0-based disc a multi-disc game starts from */
} Ps1Record;

void ps1_record_get(const Game *g, Ps1Record *out);
/* Once the library is known: the files' values into the app's stats (the larger time, the later date) */
void ps1_record_merge_all(void);
/* Hook (main.c): when a game starts and when it stops, the stats it now has go to its file */
void ps1_record_sync(const Game *g);
void ps1_record_set_favourite(const Game *g, bool on);
void ps1_record_set_boot_disc(const Game *g, int disc);

#endif
