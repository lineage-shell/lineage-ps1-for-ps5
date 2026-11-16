/*
 * SwanStationPS5 - interface sounds, synthesised at start-up, and the console's own sounds when
 * /data/SwanStationPS5/console-sounds/<event>.wav are there (the ps1-console-shelf fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SwanStationPS5_SFX_H
#define SwanStationPS5_SFX_H

#include "../SwanStationPS5.h"

typedef enum
{
    SFX_CLICK,  /* moving along the shelf or a list (console-sounds/move.wav) */
    SFX_SELECT, /* choosing something (select.wav) */
    SFX_BACK,   /* back.wav */
    SFX_COUNT,
    /* events with a console sound of their own; without its file each plays the synthesised sound named here */
    SFX_ERROR = SFX_COUNT, /* error.wav, else Back: something that can't be done */
    SFX_OPEN,              /* open.wav, else Select: a screen or a panel opens */
    SFX_CLOSE,             /* close.wav, else Back: it closes */
    SFX_PAGE,              /* page.wav, else Click: another page or category */
    SFX_STARTUP,           /* startup.wav, else nothing: the shelf appears for the first time */
    SFX_DISC,              /* disc.wav, else assets/psxdiscsound.wav: a disc found, an install begins */
    SFX_DONE,              /* done.wav, else Select: an install finished */
    SFX_EVENT_COUNT
} Sfx;

/* The console sounds' folder, under the data folder; the files are named as in the list above. */
#define SFX_CONSOLE_DIR "console-sounds"

/* Settings > Interface sound. SFX_STYLE_OFF must stay last. */
enum
{
    SFX_STYLE_SOFT = 0,
    SFX_STYLE_WOOD,
    SFX_STYLE_POP,
    SFX_STYLE_CHIME,
    SFX_STYLE_CLASSIC,
    SFX_STYLE_COUNT,
    SFX_STYLE_OFF = SFX_STYLE_COUNT,
};

void sfx_init(int sample_rate);
/* Loads <dir>/move.wav, select.wav... (16-bit PCM, mono or stereo, any rate: resampled). Missing files keep the
 * synthesised sound. Call after sfx_init; again to reload. Returns how many were found. */
int sfx_load_console(const char *dir);
/* How many console sounds are loaded (Settings shows it). */
int sfx_console_count(void);
void sfx_play(Sfx sound);
/* assets/psxdiscsound.wav (the CD spinning up in the 3D case), with the interface's volume; nothing when the interface
 * sound is off or the file is not there. */
void sfx_play_disc(void);
/* assets/notification.wav, with the interface's volume (also when the interface sound is off); nothing when the file
 * is not there. */
void sfx_play_notification(void);
/* The sounds play in parallel, each one on its own and mixed into what is playing. While a game runs, they are added to
 * the game's sound by sfx_mix (the host calls it on every block); otherwise sfx_pump, once a frame, plays them. */
void sfx_mix(int16_t *frames, size_t frame_count);
void sfx_game_audio(bool on);
void sfx_pump(void);
/* While on, the app plays no sounds (Nostalgia mode during a game). */
void sfx_silence(bool on);
/* shelf v3: a sound made at run time (interleaved stereo, 44.1 kHz), and stopping what plays */
void sfx_play_pcm(const int16_t *frames, size_t count);
void sfx_stop_all(void);
/* style: SFX_STYLE_*, or SFX_STYLE_OFF; volume 0..100 */
void sfx_configure(int style, int volume_percent);
void sfx_set_enabled(bool enabled); /* temporary mute, keeps the style */

#endif
