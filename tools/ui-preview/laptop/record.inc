/*
 * Laptop preview: recording (SwanStationPS5_RECORD=<dir>, without SwanStationPS5_WINDOW). Every frame of the off-screen
 * 1920 x 1080 picture goes to ffmpeg (60 fps, the preview's fixed clock), and the sound the app queues is kept on the
 * same clock (an emulated audio queue: pushes append after what is queued, a clear drops the rest) and written as
 * <dir>/audio.wav at the end. Included by src/platform/plat_sdl.c in the preview build only.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <signal.h>

static FILE *rec_video;
static const char *rec_dir;
static bool rec_tried;
static int16_t *rec_pcm;
static size_t rec_cap, rec_write; /* frames */
static int rec_rate = 44100;

static bool rec_on(void)
{
    if (!rec_tried)
    {
        rec_tried = true;
        rec_dir = SDL_getenv("SwanStationPS5_RECORD");
        if (rec_dir && !*rec_dir)
            rec_dir = NULL;
    }
    return rec_dir != NULL;
}

static size_t rec_now(void)
{
    return (size_t)(preview_clock * (uint64_t)rec_rate / 1000000ull);
}

static void rec_reserve(size_t frames)
{
    if (frames <= rec_cap)
        return;
    size_t cap = rec_cap ? rec_cap : (size_t)rec_rate * 30;
    while (cap < frames)
        cap *= 2;
    int16_t *g = realloc(rec_pcm, cap * 4);
    if (!g)
        return;
    memset(g + rec_cap * 2, 0, (cap - rec_cap) * 4);
    rec_pcm = g, rec_cap = cap;
}

static void rec_audio_rate(int rate)
{
    if (rate > 0)
        rec_rate = rate;
}

static void rec_audio_push(const int16_t *frames, size_t n)
{
    size_t at = rec_write > rec_now() ? rec_write : rec_now();
    rec_reserve(at + n);
    if (at + n > rec_cap)
        return;
    memcpy(rec_pcm + at * 2, frames, n * 4);
    rec_write = at + n;
}

static size_t rec_audio_queued(void)
{
    const size_t now = rec_now();
    return rec_write > now ? rec_write - now : 0;
}

static void rec_audio_clear(void)
{
    const size_t now = rec_now();
    if (rec_write > now && rec_pcm)
        memset(rec_pcm + now * 2, 0, (rec_write - now) * 4);
    rec_write = now;
}

static void rec_finish(void)
{
    static bool done;
    if (done || !rec_dir)
        return;
    done = true;
    SwanStationPS5_log("record: writing %s/audio.wav (%zu frames)", rec_dir, rec_now());
    char path[600];
    snprintf(path, sizeof(path), "%s/audio.wav", rec_dir);
    FILE *w = fopen(path, "wb");
    if (!w)
        return;
    const size_t n = rec_now() < rec_cap ? rec_now() : rec_cap;
    const uint32_t bytes = (uint32_t)(n * 4), rate = (uint32_t)rec_rate, byte_rate = rate * 4, chunk = 36 + bytes, sixteen = 16;
    const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, w), fwrite(&chunk, 4, 1, w), fwrite("WAVEfmt ", 1, 8, w), fwrite(&sixteen, 4, 1, w);
    fwrite(&pcm, 2, 1, w), fwrite(&ch, 2, 1, w), fwrite(&rate, 4, 1, w), fwrite(&byte_rate, 4, 1, w);
    fwrite(&align, 2, 1, w), fwrite(&bits, 2, 1, w), fwrite("data", 1, 4, w), fwrite(&bytes, 4, 1, w);
    if (rec_pcm)
        fwrite(rec_pcm, 4, n, w);
    fclose(w);
    if (rec_video)
        pclose(rec_video), rec_video = NULL;
}

static void rec_frame(SDL_Surface *picture)
{
    if (!rec_on())
        return;
    if (!rec_video)
    {
        char cmd[900];
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgra -s 1920x1080 -r 60 -i - -c:v libx264 -preset medium "
                 "-crf 14 -pix_fmt yuv420p '%s/video.mp4'",
                 rec_dir);
        rec_video = popen(cmd, "w");
        if (!rec_video)
            return;
        atexit(rec_finish);
    }
    const uint8_t *p = (const uint8_t *)picture->pixels + 36 * picture->pitch;
    for (int y = 0; y < 1080; ++y)
        fwrite(p + (size_t)y * picture->pitch, 1, 1920 * 4, rec_video);
}
