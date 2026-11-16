/*
 * Laptop preview: the interface preview (tools/ui-preview) in a real window with live input and sound.
 * Included by src/platform/plat_sdl.c in the preview build only. It is on when SwanStationPS5_WINDOW=1.
 *   - The 1920x1080 picture of the preview's software renderer is shown in a resizable 16:9 window (letterboxed).
 *   - Keyboard map below, controllers through SDL GameController (plat_sdl.c's own button map, as on the PS5).
 *   - F11 full screen, F1 the key map (also shown for a few seconds at start), F2 / F3 next / previous version:
 *     SwanStationPS5_VERSIONS="label|binary;label|binary..." and SwanStationPS5_VERSION_INDEX pick the list and
 *     which one this is; switching execs the other binary with the same environment.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#if !defined(_WIN32)
#include <unistd.h>
#endif

unsigned char *stbi_load(const char *filename, int *x, int *y, int *comp, int req_comp);
void stbi_image_free(void *retval_from_stbi_load);

static bool live_on;
static SDL_Window *live_window;
static SDL_Renderer *live_renderer;
static SDL_Texture *live_texture, *live_help;
static int live_help_w, live_help_h;
static uint64_t live_help_until, live_next_frame;
static bool live_help_pinned;

static uint64_t live_now_us(void)
{
    return SDL_GetPerformanceCounter() * 1000000ull / SDL_GetPerformanceFrequency();
}

static const struct
{
    SDL_Scancode key;
    int bit;
} live_keys[] = {
    {SDL_SCANCODE_UP, BTN_UP},          {SDL_SCANCODE_DOWN, BTN_DOWN},
    {SDL_SCANCODE_LEFT, BTN_LEFT},      {SDL_SCANCODE_RIGHT, BTN_RIGHT},
    {SDL_SCANCODE_RETURN, BTN_CROSS},   {SDL_SCANCODE_KP_ENTER, BTN_CROSS},  {SDL_SCANCODE_X, BTN_CROSS},
    {SDL_SCANCODE_BACKSPACE, BTN_CIRCLE}, {SDL_SCANCODE_ESCAPE, BTN_CIRCLE},
    {SDL_SCANCODE_S, BTN_SQUARE},       {SDL_SCANCODE_T, BTN_TRIANGLE},
    {SDL_SCANCODE_Q, BTN_L1},           {SDL_SCANCODE_E, BTN_R1},
    {SDL_SCANCODE_1, BTN_L2},           {SDL_SCANCODE_3, BTN_R2},
    {SDL_SCANCODE_TAB, BTN_START},      /* Options */
    {SDL_SCANCODE_SPACE, BTN_MENU},     {SDL_SCANCODE_P, BTN_MENU}, /* touchpad click */
    {SDL_SCANCODE_Z, BTN_L3},           {SDL_SCANCODE_C, BTN_R3},
};

static int live_version_count(void)
{
    const char *list = SDL_getenv("SwanStationPS5_VERSIONS");
    if (!list || !*list)
        return 0;
    int n = 1;
    for (const char *p = list; *p; ++p)
        n += *p == ';';
    return n;
}

/* the binary of version i in SwanStationPS5_VERSIONS (label|path;...) */
static bool live_version_path(int i, char *out, size_t size, char *label, size_t label_size)
{
    const char *p = SDL_getenv("SwanStationPS5_VERSIONS");
    for (int k = 0; p && k < i; ++k)
        p = (p = strchr(p, ';')) ? p + 1 : NULL;
    if (!p)
        return false;
    const char *bar = strchr(p, '|'), *end = strchr(p, ';');
    if (!end)
        end = p + strlen(p);
    if (!bar || bar > end || (size_t)(end - bar - 1) >= size)
        return false;
    memcpy(out, bar + 1, (size_t)(end - bar - 1));
    out[end - bar - 1] = 0;
    snprintf(label, label_size, "%.*s", (int)(bar - p), p);
    return true;
}

static void live_switch(int step)
{
#if defined(_WIN32)
    (void)step; /* the version switch is Linux only */
#else
    const int n = live_version_count();
    if (n < 2)
        return;
    const char *cur = SDL_getenv("SwanStationPS5_VERSION_INDEX");
    const int next = ((cur ? atoi(cur) : 0) + step + n) % n;
    char path[1024], label[200], index[16];
    if (!live_version_path(next, path, sizeof(path), label, sizeof(label)))
        return;
    setenv("SwanStationPS5_VERSION_NAME", label, 1);
    snprintf(index, sizeof(index), "%d", next);
    setenv("SwanStationPS5_VERSION_INDEX", index, 1);
    if (live_window && (SDL_GetWindowFlags(live_window) & SDL_WINDOW_FULLSCREEN_DESKTOP))
        setenv("SwanStationPS5_FULLSCREEN", "1", 1);
    else
        unsetenv("SwanStationPS5_FULLSCREEN");
    SwanStationPS5_log("laptop preview: switching to %s", path);
    plat_audio_close();
    SDL_Quit();
    execl(path, path, (char *)NULL);
    fprintf(stderr, "could not start %s\n", path);
    _exit(1);
#endif
}

static void live_init(void)
{
    const char *on = SDL_getenv("SwanStationPS5_WINDOW");
    live_on = on && *on && strcmp(on, "0");
    if (!live_on)
        return;
    const char *label = SDL_getenv("SwanStationPS5_VERSION_NAME");
    char title[200];
    snprintf(title, sizeof(title), "PS1 Shelf Preview - %s", label && *label ? label : "local build");
    const char *full = SDL_getenv("SwanStationPS5_FULLSCREEN");
    live_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                       (full && *full == '1' ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!live_window)
    {
        SwanStationPS5_log("laptop preview: no window (%s)", SDL_GetError());
        live_on = false;
        return;
    }
    live_renderer = SDL_CreateRenderer(live_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!live_renderer)
        live_renderer = SDL_CreateRenderer(live_window, -1, 0);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_RenderSetLogicalSize(live_renderer, 1920, 1080); /* 16:9 with black bars in any window shape */
    live_texture = SDL_CreateTexture(live_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 1920, 1080);
    const char *help = SDL_getenv("SwanStationPS5_KEYMAP");
    int comp = 0;
    unsigned char *px = help ? stbi_load(help, &live_help_w, &live_help_h, &comp, 4) : NULL;
    if (px)
    {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, live_help_w, live_help_h, 32, live_help_w * 4,
                                                            SDL_PIXELFORMAT_ABGR8888);
        live_help = s ? SDL_CreateTextureFromSurface(live_renderer, s) : NULL;
        if (live_help)
            SDL_SetTextureBlendMode(live_help, SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(s);
        stbi_image_free(px);
    }
    live_help_until = live_now_us() + 7000000ull;
    SDL_ShowCursor(full && *full == '1' ? SDL_DISABLE : SDL_ENABLE);
    SwanStationPS5_log("laptop preview: window open (%s)", title);
}

static void live_event(const SDL_Event *e)
{
    if (live_on && (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) && SDL_getenv("SwanStationPS5_KEYLOG"))
        SwanStationPS5_log("key %s %s%s", e->type == SDL_KEYDOWN ? "down" : "up", SDL_GetKeyName(e->key.keysym.sym),
                           e->key.repeat ? " (repeat)" : "");
    if (!live_on || e->type != SDL_KEYDOWN || e->key.repeat)
        return;
    switch (e->key.keysym.sym)
    {
    case SDLK_F11:
    {
        const bool full = (SDL_GetWindowFlags(live_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (SDL_SetWindowFullscreen(live_window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) == 0)
            SDL_ShowCursor(full ? SDL_ENABLE : SDL_DISABLE);
        break;
    }
    case SDLK_F1:
        live_help_pinned = !(live_help_pinned || live_now_us() < live_help_until);
        live_help_until = 0;
        break;
    case SDLK_F2: live_switch(1); break;
    case SDLK_F3: live_switch(-1); break;
    default: break;
    }
}

static void live_read_keys(PadState *pad)
{
    if (!live_on)
        return;
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    for (size_t k = 0; k < sizeof(live_keys) / sizeof(live_keys[0]); ++k)
        if (keys[live_keys[k].key])
            pad->buttons |= BIT(live_keys[k].bit);
    /* J / L: the right stick, left / right (turns the game page's case) */
    if (keys[SDL_SCANCODE_J] != keys[SDL_SCANCODE_L])
        pad->rx = keys[SDL_SCANCODE_L] ? 32767 : -32767;
    pad->connected = true;
}

static void live_present(SDL_Surface *picture)
{
    if (!live_on)
        return;
    /* the 1080 lines inside the 1152-line canvas, as the screenshots take them */
    SDL_UpdateTexture(live_texture, NULL, (const uint8_t *)picture->pixels + 36 * picture->pitch, picture->pitch);
    SDL_SetRenderDrawColor(live_renderer, 0, 0, 0, 255);
    SDL_RenderClear(live_renderer);
    SDL_RenderCopy(live_renderer, live_texture, NULL, NULL);
    const uint64_t now = live_now_us();
    if (live_help && (live_help_pinned || now < live_help_until))
    {
        const uint64_t left = live_help_pinned ? 1000000ull : live_help_until - now;
        SDL_SetTextureAlphaMod(live_help, (Uint8)(left > 1000000ull ? 255 : left * 255 / 1000000ull));
        SDL_Rect r = {(1920 - live_help_w) / 2, (1080 - live_help_h) / 2, live_help_w, live_help_h};
        SDL_RenderCopy(live_renderer, live_help, NULL, &r);
    }
    SDL_RenderPresent(live_renderer);
    /* 60 frames a second, like the PS5's interface, whatever the screen's rate (and when vsync is off) */
    if (!live_next_frame || now > live_next_frame + 50000)
        live_next_frame = now;
    live_next_frame += 16667;
    const uint64_t after = live_now_us();
    if (live_next_frame > after + 1000)
        SDL_Delay((Uint32)((live_next_frame - after) / 1000));
}
