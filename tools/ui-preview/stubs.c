#include <stdio.h>
/*
 * SwanStationPS5 interface preview - stand-ins for the emulator and RetroAchievements,
 * so the screens run on a PC with a fake game picture.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../../src/core/host.h"
#include "../../src/ra/achievements.h"
#include "../../src/platform/platform.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

/* ---- the core: a striped test picture instead of a game, or (SwanStationPS5_STUB_MOVIE=<dir>) a recording of a
 * real one played back: <dir>/f%05d.png frames from SwanStationPS5_STUB_FIRST, <dir>/audio.wav and <dir>/marks.txt
 * (the sample where each frame starts), as tools/ui-preview's libretro harness writes them */
static bool loaded;
static uint32_t frame[640 * 480];
static int frame_w = 320, frame_h = 240;
static const char *movie;
static int movie_first, movie_at, movie_count;
static int16_t *movie_pcm;
static long *movie_marks;
static int movie_marks_n;
void plat_audio_push(const int16_t *frames, size_t frame_count);
unsigned char *stbi_load(const char *filename, int *x, int *y, int *comp, int req_comp);
void stbi_image_free(void *retval_from_stbi_load);

static bool movie_frame(int i)
{
    char path[600];
    snprintf(path, sizeof(path), "%s/f%05d.png", movie, movie_first + i);
    int w, h, c;
    unsigned char *px = stbi_load(path, &w, &h, &c, 4);
    if (!px || w > 640 || h > 480)
    {
        if (px)
            stbi_image_free(px);
        return false;
    }
    for (int k = 0; k < w * h; ++k)
        frame[k] = 0xff000000u | (uint32_t)px[k * 4] << 16 | (uint32_t)px[k * 4 + 1] << 8 | px[k * 4 + 2];
    frame_w = w, frame_h = h;
    stbi_image_free(px);
    return true;
}

static void movie_open(void)
{
    movie = getenv("SwanStationPS5_STUB_MOVIE");
    if (!movie || !*movie)
    {
        movie = NULL;
        return;
    }
    movie_first = getenv("SwanStationPS5_STUB_FIRST") ? atoi(getenv("SwanStationPS5_STUB_FIRST")) : 0;
    movie_count = 0;
    char path[600];
    for (;; ++movie_count)
    {
        snprintf(path, sizeof(path), "%s/f%05d.png", movie, movie_first + movie_count);
        FILE *f = fopen(path, "rb");
        if (!f)
            break;
        fclose(f);
    }
    snprintf(path, sizeof(path), "%s/marks.txt", movie);
    FILE *m = fopen(path, "r");
    long v;
    while (m && fscanf(m, "%ld", &v) == 1)
    {
        movie_marks = realloc(movie_marks, sizeof(long) * (size_t)(movie_marks_n + 1));
        movie_marks[movie_marks_n++] = v;
    }
    if (m)
        fclose(m);
    snprintf(path, sizeof(path), "%s/audio.wav", movie);
    FILE *w = fopen(path, "rb");
    if (w)
    {
        fseek(w, 0, SEEK_END);
        const long n = ftell(w) - 44;
        fseek(w, 44, SEEK_SET);
        movie_pcm = malloc((size_t)n);
        if (movie_pcm && fread(movie_pcm, 1, (size_t)n, w) != (size_t)n)
            free(movie_pcm), movie_pcm = NULL;
        fclose(w);
    }
    movie_at = 0;
    if (movie_count)
        movie_frame(0);
}

bool host_load(const char *game_path, const char *serial, const Paths *paths, const Settings *settings,
               char *error, size_t error_size)
{
    (void)game_path;
    (void)serial; (void)paths; (void)settings; (void)error; (void)error_size;
    movie_open();
    if (movie && movie_count)
    {
        loaded = true;
        return true;
    }
    frame_w = 320, frame_h = 240;
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x)
        {
            uint32_t sky = 0xff203a8au + (uint32_t)(y / 4) * 0x010203u;
            bool ground = y > 160, block = ((x / 32) + (y / 32)) % 2 == 0;
            frame[y * 320 + x] = ground ? (block ? 0xff3f8a3fu : 0xff2f6a2fu) : sky;
            if ((x - 230) * (x - 230) + (y - 70) * (y - 70) < 500)
                frame[y * 320 + x] = 0xfff0d050u;
        }
    loaded = true;
    return true;
}
void host_unload(void) { loaded = false; }
bool host_loaded(void) { return loaded; }
const char *host_core_name(void) { return "SwanStation"; }
const char *host_emulator_for(const Settings *s, const char *serial)
{ (void)s; (void)serial; return "SwanStation"; }
bool host_hash_disc_begin(const char *p) { (void)p; return false; }
void host_hash_disc_end(void) {}
void *host_memory_data(unsigned id) { (void)id; return NULL; }
size_t host_memory_size(unsigned id) { (void)id; return 0; }
void host_set_pads(const PadState pads[SwanStationPS5_MAX_PADS]) { (void)pads; }
void host_set_gun(int device) { (void)device; }
void host_set_special(int device) { (void)device; }
void host_set_fixes(unsigned flags, const GameInfo *info) { (void)flags; (void)info; }
float host_rumble_level(int port) { (void)port; return 0.0f; }
bool host_pad_digital(int port) { (void)port; return false; }
void host_run_frame(void)
{
    if (!movie || !movie_count)
        return;
    const int i = movie_at % movie_count, g = movie_first + i;
    movie_frame(i);
    if (movie_pcm && g + 1 < movie_marks_n)
        plat_audio_push(movie_pcm + movie_marks[g] * 2, (size_t)(movie_marks[g + 1] - movie_marks[g]));
    ++movie_at;
}
void host_set_speculative(bool on) { (void)on; }
void host_reset(void) {}
void host_apply_settings(const Settings *settings) { (void)settings; }
double host_fps(void) { return movie ? 50.0 : 59.94; }
int host_sample_rate(void) { return 44100; }
float host_aspect(void) { return 4.0f / 3.0f; }
const void *host_frame(int *width, int *height, size_t *pitch, int *pixel_format, bool *fresh)
{
    *width = frame_w;
    *height = frame_h;
    *pitch = (size_t)frame_w * 4;
    *pixel_format = 1;
    *fresh = true;
    return loaded ? frame : NULL;
}
bool host_save_state(const char *path)
{
    FILE *f = fopen(path, "wb"); /* an empty file, so the slot shows as used */
    if (f)
        fclose(f);
    return true;
}
bool host_load_state(const char *path) { (void)path; return true; }
const struct retro_memory_map *host_memory_map(void) { return NULL; }
bool host_read_sector(uint32_t lba, uint8_t out[2048]) { (void)lba; (void)out; return false; }
int host_disc_count(void) { return 2; }
int host_disc_index(void) { return 0; }
bool host_disc_select(int index) { (void)index; return true; }

void retro_cheat_reset(void) {}
void host_cheat_reset(void) {}
void host_cheat_set(unsigned index, const char *code) { (void)index, (void)code; }
void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index; (void)enabled; (void)code;
}

/* ---- RetroAchievements: a signed-in account with a game in progress */
static bool hardcore;
void ra_init(const Paths *paths) { (void)paths; }
void ra_shutdown(void) {}
void ra_game_loaded(const char *game_path) {}
void ra_game_unloaded(void) {}
void ra_frame(void) {}
void ra_idle(void) {}
void ra_reset(void) {}
bool ra_signed_in(void) { return true; }
const char *ra_user(void) { return "SynoPiia"; }
unsigned ra_user_score(void) { return 1240; }
void ra_use_token(const char *u, const char *t) { (void)u; (void)t; }
void ra_sign_out(void) {}
unsigned ra_user_hardcore_score(void) { return 1240; }
unsigned ra_user_softcore_score(void) { return 85; }
int ra_recent(RaRecent *out, int max)
{
    static const char *const t[] = {"Wumpa Collector", "Dragon Rescuer", "Bonus Round", "Gem Hunter"};
    int n = max < 4 ? max : 4;
    for (int i = 0; i < n; ++i)
    {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].when = (long long)time(NULL) - i * 90000;
        out[i].points = 5 + i * 5;
        snprintf(out[i].game, sizeof(out[i].game), "%s", i < 2 ? "Crash Bandicoot" : "Spyro the Dragon");
        snprintf(out[i].title, sizeof(out[i].title), "%s", t[i]);
    }
    return n;
}
bool ra_game_progress(int *unlocked, int *total)
{
    if (!loaded)
        return false;
    *unlocked = 3;
    *total = 12;
    return true;
}
bool ra_hardcore(void) { return hardcore; }
void ra_set_hardcore(bool on) { hardcore = on; }
void ra_game_summary(char *out, size_t size) { if (size) out[0] = '\0'; }
bool ra_next_message(char *title, size_t title_size, char *detail, size_t detail_size, unsigned *badge_id)
{
    static bool shown;
    if (badge_id)
        *badge_id = 0;
    const char *banner = getenv("SwanStationPS5_BANNER");
    if (shown || !loaded || !banner)
        return false;
    shown = true;
    strncpy(title, "Wumpa Collector", title_size - 1);
    strncpy(detail, "Collect 100 Wumpa fruit in one level  (10 points)", detail_size - 1);
    return true;
}

bool ra_tracker(char *title, size_t ts, char *progress, size_t ps, float *percent)
{
    snprintf(title, ts, "Dragon Rescuer");
    snprintf(progress, ps, "18/80");
    *percent = 22.5f;
    return true;
}

int ra_challenges(RaChallenge *out, int max)
{
    static const char *const titles[] = {"Perfect Run", "No Mask Needed"};
    int n = max < 2 ? max : 2;
    for (int i = 0; i < n; ++i)
    {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].id = 1000u + (unsigned)i;
        strncpy(out[i].title, titles[i], sizeof(out[i].title) - 1);
    }
    return n;
}

int ra_lb_trackers(RaLbTracker *out, int max)
{
    if (max < 1)
        return 0;
    memset(&out[0], 0, sizeof(out[0]));
    out[0].id = 1;
    strcpy(out[0].display, "01:23.45");
    return 1;
}

int ra_leaderboards(RaLeaderboard *out, int max)
{
    static const char *const names[][2] = {{"Dr. Light's Lab", "Reach the end of the first stage as fast as you can"},
                                           {"Highway Run", "Finish the highway stage without taking damage"}};
    int n = max < 2 ? max : 2;
    for (int i = 0; i < n; ++i)
    {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].id = 1u + (unsigned)i;
        strncpy(out[i].title, names[i][0], sizeof(out[i].title) - 1);
        strncpy(out[i].description, names[i][1], sizeof(out[i].description) - 1);
    }
    return n;
}

void ra_clear_indicators(void) {}
PlatTexture *ra_badge(unsigned id, const char *url) { (void)id; (void)url; return NULL; }
void ra_badges_shutdown(void) {}

int ra_list(RaAchievement *out, int max)
{
    static const char *const names[][2] = {
        {"Wumpa Collector", "Collect 100 Wumpa fruit in one level"},
        {"Box Breaker", "Break every box in N. Sanity Beach"},
        {"Bonus Round", "Reach a bonus round"},
        {"Gem Hunter", "Collect your first clear gem"},
        {"No Mask Needed", "Finish a level without an Aku Aku mask"},
        {"Boss Fight", "Defeat Papu Papu"},
        {"Island Hopper", "Reach the second island"},
        {"Perfect Run", "Finish a level without dying"},
    };
    int n = 0;
    for (int i = 0; i < 8 && n < max; ++i, ++n)
    {
        RaAchievement *r = &out[n];
        memset(r, 0, sizeof(*r));
        strncpy(r->title, names[i][0], sizeof(r->title) - 1);
        strncpy(r->description, names[i][1], sizeof(r->description) - 1);
        r->points = (unsigned)(5 + i * 5);
        r->unlocked = i < 3;
        r->challenge = i == 4 || i == 7;
        if (i == 3)
            strcpy(r->progress, "1/3"), r->percent = 33.3f;
        if (i == 6)
            strcpy(r->progress, "18/80"), r->percent = 22.5f;
    }
    return n;
}

void host_set_patches_dir(const char *dir) { (void)dir; }
size_t host_state_size(void) { return loaded ? 64 : 0; }
bool host_serialize(void *buffer, size_t size) { memset(buffer, 0, size); return loaded; }
bool host_unserialize(const void *buffer, size_t size) { (void)buffer; (void)size; return loaded; }
bool host_capture(uint8_t *rgba, int w, int h)
{
    if (!loaded)
        return false;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint32_t c = frame[(y * frame_h / h) * frame_w + x * frame_w / w];
            uint8_t *o = &rgba[(y * w + x) * 4];
            o[0] = (c >> 16) & 0xff, o[1] = (c >> 8) & 0xff, o[2] = c & 0xff, o[3] = 255;
        }
    return true;
}

/* ---- added for the Linux preview build: newer core / NFS / NTFS / RA entry points */
#include "../../src/nfs/nfs.h"
#include "../../src/ntfs/ntfsfs.h"
bool host_analog_mode(int port) { (void)port; return true; }
void host_apply_devices(const Settings *settings) { (void)settings; }
int host_capture_begin(uint8_t *rgba, int w, int h) { (void)rgba; (void)w; (void)h; return 0; }
int host_capture_poll(uint8_t *rgba, int w, int h, bool wait) { (void)rgba; (void)w; (void)h; (void)wait; return 0; }
uint32_t host_frames_new(void) { static uint32_t n; return ++n; }
uint32_t host_frames_repeated(void) { return 0; }
void host_set_initial_disc(int index) { (void)index; }
void host_set_region_override(const char *region) { (void)region; }
bool nfs_set_shares(const char *list, char *error, size_t error_size) { (void)list; if (error_size) error[0] = '\0'; (void)error; return true; }
int nfs_mount_points(char out[][NFS_PATH_MAX], int max) { (void)out; (void)max; return 0; }
void nfs_mount_begin(int timeout_ms) { (void)timeout_ms; }
bool nfs_mount_finished(void) { return false; }
int nfs_first_failure(char *detail, size_t size) { if (size) detail[0] = '\0'; return 0; }
bool nfs_connection_lost(void) { return false; }
void nfs_set_read_timeout(int seconds) { (void)seconds; }
bool nfs_reconnecting(void) { return false; }
void nfs_set_sbi_dir(const char *dir) { (void)dir; }
int ntfsfs_scan(void) { return 0; }
int ntfsfs_mount_points(char out[][NTFSFS_PATH_MAX], int max) { (void)out; (void)max; return 0; }
bool ntfsfs_connection_lost(void) { return false; }
bool ra_warning(bool *emulator, bool *version) { if (emulator) *emulator = false; if (version) *version = false; return false; }
bool swanstation_retro_ds(const char *path, char *out, size_t size) { (void)path; (void)out; (void)size; return false; }
bool ra_password_login(const char *name, const char *password, char *token, size_t token_size, char *who,
                       size_t who_size, char *error, size_t error_size)
{
    (void)name; (void)password; (void)token; (void)token_size; (void)who; (void)who_size;
    if (error_size) snprintf(error, error_size, "preview");
    return false;
}

/* ---- added after the 1.6.5 merge: badges and the per-mode progress */
#include "../../src/ra/badges.h"
bool ra_game_progress_modes(int *soft, int *hard, int *total)
{
    if (soft) *soft = 3;
    if (hard) *hard = 1;
    if (total) *total = 12;
    return true;
}
PlatTexture *ra_badge_cached(unsigned id) { (void)id; return NULL; }
void ra_badges_prefetch(const RaBadgeJob *jobs, int count) { (void)jobs; (void)count; }
void ra_badges_prefetch_stop(void) {}
int ra_badge_jobs(RaBadgeJob *out, int max) { (void)out; (void)max; return 0; }
