/*
 * SwanStationPS5 - the PS1 theme's full screens for what other themes show as boxes over the shelf: the disc install
 * (screen A4, built on the console's CD Player), reading a disc, the question a disc put in asks, the message box,
 * the question after an update and the LibCrypt check. Each fills the whole screen. Included by main.c, which owns
 * their state.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* ▶ ■ ⏏ on the transport balls, drawn (the fonts carry Latin-1 only) */
static void ps1_transport_mark(int kind, float cx, float cy, float s, uint32_t c)
{
    PlatVertex v[9];
    int n = 0;
    if (kind == 0) /* play */
    {
        v[n++] = (PlatVertex){cx - s * 0.32f, cy - s * 0.4f, 0, 0, c};
        v[n++] = (PlatVertex){cx + s * 0.42f, cy, 0, 0, c};
        v[n++] = (PlatVertex){cx - s * 0.32f, cy + s * 0.4f, 0, 0, c};
    }
    else if (kind == 1) /* stop */
    {
        draw_rect(cx - s * 0.32f, cy - s * 0.32f, s * 0.64f, s * 0.64f, c);
        return;
    }
    else /* eject */
    {
        v[n++] = (PlatVertex){cx, cy - s * 0.42f, 0, 0, c};
        v[n++] = (PlatVertex){cx + s * 0.4f, cy + s * 0.06f, 0, 0, c};
        v[n++] = (PlatVertex){cx - s * 0.4f, cy + s * 0.06f, 0, 0, c};
        draw_rect(cx - s * 0.4f, cy + s * 0.2f, s * 0.8f, s * 0.16f, c);
    }
    plat_draw_mesh(NULL, v, n, NULL, 0);
}

/* the CD Player's track panel: a thin outline and a faint fill */
static void ps1_panel(float x, float y, float w, float h)
{
    draw_rect(x, y, w, h, 0x1f6e6ef0u);
    const uint32_t e = 0xff5f5fc6u;
    draw_rect(x, y, w, 4, e);
    draw_rect(x, y + h - 4, w, 4, e);
    draw_rect(x, y + 4, 4, h - 8, e);
    draw_rect(x + w - 4, y + 4, 4, h - 8, e);
}

/* "HOLD (O) CANCEL" on a splatter plaque, the circle drawn between the words */
static void ps1_cancel_plaque(float x, float y, float w, float h, bool dim)
{
    ps1_splat(x, y, w, h, 41, NULL, 0, dim);
    char a[32], b[32];
    ps1_caps(tr("Hold"), a, sizeof(a));
    ps1_caps(tr("Cancel"), b, sizeof(b));
    const float size = 48, ring = size * 0.9f, gap = 16;
    const float wa = text_width(size, FONT_BOLD, a), wb = text_width(size, FONT_BOLD, b);
    float tx = x + (w - (wa + gap + ring + gap + wb)) * 0.5f;
    const float ty = ps1_text_middle(y, h, size);
    const float alpha = dim ? 0.5f : 1.0f;
    ps1_text_outlined(tx, ty, size, FONT_BOLD, argb_alpha(0xffffffffu, alpha), ALIGN_LEFT, a, 2.2f);
    tx += wa + gap;
    draw_ring(tx + ring * 0.5f, y + h * 0.5f, ring * 0.36f, 9, argb_alpha(PS1_OUTLINE, alpha));
    draw_ring(tx + ring * 0.5f, y + h * 0.5f, ring * 0.36f, 5, argb_alpha(0xffff6e6eu, alpha));
    tx += ring + gap;
    ps1_text_outlined(tx, ty, size, FONT_BOLD, argb_alpha(0xffffffffu, alpha), ALIGN_LEFT, b, 2.2f);
}

/* A4: the disc install, as the console's CD Player. p: 0..1; left_s: seconds left (-1 unknown) */
static void ps1_import_draw(const char *title, PlatTexture *cover, float p, long left_s, float hold)
{
    cover = (PlatTexture *)discimport_cover(); /* a cover found, not the stand-in picture */
    ps1_background(2);
    ps1_title_plate(tr("Disc install"), 1824, 64);
    /* the transport balls: play (dim, it plays by itself), stop under the arrow (hold O), eject */
    ps1_ball(PS1_BALL_GREEN, 148, 302, 104, NULL, 0, 0, false, 0.55f);
    ps1_transport_mark(0, 148, 302, 44, 0x8c111111u);
    ps1_ball(PS1_BALL_RED, 148, 460, 120, NULL, 0, 0, hold > 0.0f, 1.0f);
    ps1_transport_mark(1, 148, 460, 50, 0xff111111u);
    ps1_ball(PS1_BALL_GOLD, 148, 622, 104, NULL, 0, 0, false, 1.0f);
    ps1_transport_mark(2, 148, 622, 44, 0xff111111u);
    ps1_cursor(184, 484, 72);
    /* the percentage and the time left in number spheres */
    char num[16];
    const int pct = (int)(p * 100.0f);
    snprintf(num, sizeof(num), "%d", pct > 99 ? 99 : pct);
    ps1_ball(PS1_BALL_BLUE, 388, 158, 176, num, 84, 0xffffffffu, false, 1.0f);
    ps1_ball(PS1_BALL_BLUE, 477, 223, 74, "%", 34, 0xffffffffu, false, 1.0f);
    if (left_s >= 0)
    {
        snprintf(num, sizeof(num), "%02ld", left_s / 60 > 99 ? 99 : left_s / 60);
        ps1_ball(PS1_BALL_BLUE, 672, 156, 144, num, 64, 0xffffffffu, false, 1.0f);
        ps1_ball(PS1_BALL_BLUE, 614, 228, 76, "MIN", 22, 0xffffffffu, false, 1.0f);
        ps1_ball(PS1_BALL_GOLD, 776, 162, 32, NULL, 0, 0, false, 1.0f);
        snprintf(num, sizeof(num), "%02ld", left_s % 60);
        ps1_ball(PS1_BALL_BLUE, 880, 156, 144, num, 64, 0xffffffffu, false, 1.0f);
        ps1_ball(PS1_BALL_BLUE, 948, 228, 76, "SEC", 22, 0xffffffffu, false, 1.0f);
        char caps[32];
        ps1_text_drop(1000, 140, 30, FONT_BOLD, PS1_SOFT, ALIGN_LEFT, ps1_caps(tr("Left"), caps, sizeof(caps)), 2);
    }
    /* the disc in the old track panel, black side up, with the game's name */
    ps1_panel(300, 320, 720, 400);
    ps1_disc(486, 518, 300, ps1_time() * 2.6f);
    {
        char caps[160];
        ps1_text_tracked(676, 380, 30, FONT_BOLD, PS1_SOFT, ALIGN_LEFT, 0.06f, ps1_caps(tr("Installing"), caps, sizeof(caps)));
        ps1_caps(title[0] ? title : tr("Reading the disc..."), caps, sizeof(caps));
        /* the title on up to three lines, in the squarish face */
        const float size = text_width(58, FONT_SQUARE, caps) > 320 * 2.6f ? 44 : 58;
        char line[160];
        float ty = 424;
        const char *m = caps;
        int lines = 0;
        while (*m && lines < 3)
        {
            size_t best = 0, i = 0;
            for (;;)
            {
                size_t j = i;
                while (m[j] && m[j] != ' ')
                    ++j;
                size_t n = j < sizeof(line) - 1 ? j : sizeof(line) - 1;
                memcpy(line, m, n);
                line[n] = '\0';
                if (best > 0 && text_width(size, FONT_SQUARE, line) > 320)
                    break;
                best = n;
                if (!m[j])
                    break;
                i = j + 1;
            }
            if (!best)
                best = strlen(m) < sizeof(line) - 1 ? strlen(m) : sizeof(line) - 1;
            memcpy(line, m, best);
            line[best] = '\0';
            text_draw_fit(676, ty + 3, size, FONT_SQUARE, PS1_SHADOW, ALIGN_LEFT, 330, line);
            text_draw_fit(676, ty, size, FONT_SQUARE, 0xffffffffu, ALIGN_LEFT, 330, line);
            ty += size * 0.98f;
            ++lines;
            m += best;
            while (*m == ' ')
                ++m;
        }
        const char *serial = discimport_serial();
        if (serial && serial[0])
            ps1_text_tracked(676, ty + 16, 30, FONT_TECH, 0xffe6e6ffu, ALIGN_LEFT, 0.12f, serial);
    }
    /* the 5x4 dots: each is 5%, the gold ball is where the copy is */
    {
        const int lit = (int)(p * 20.0f);
        char next[8];
        snprintf(next, sizeof(next), "%d", (lit < 20 ? lit : 19) + 1);
        ps1_dot_grid(1140, 330, 134, 110, lit, next);
        char caps[48];
        ps1_text_tracked(1094, 690, 26, FONT_TECH, PS1_MUTED, ALIGN_LEFT, 0.1f, "0%");
        ps1_text_tracked(1404, 690, 26, FONT_TECH, PS1_MUTED, ALIGN_CENTER, 0.1f, ps1_caps(tr("Each dot 5%"), caps, sizeof(caps)));
        ps1_text_tracked(1714, 690, 26, FONT_TECH, PS1_MUTED, ALIGN_RIGHT, 0.1f, "100%");
    }
    /* facts, as flat buttons: how much is read, the drive's speed, the cover, what is written */
    {
        static double last_t;
        static int last_permille = -1;
        static float speed;
        const int total = discimport_total_sectors(), pm = discimport_permille();
        const double now = SDL_GetTicks() / 1000.0;
        if (last_permille < 0 || pm < last_permille)
            last_permille = pm, last_t = now, speed = 0;
        else if (now - last_t >= 2.0)
        {
            const double sectors = (double)(pm - last_permille) / 1000.0 * total;
            speed = (float)(sectors / (now - last_t) / 75.0); /* 1x reads 75 sectors a second */
            last_permille = pm, last_t = now;
        }
        char f[4][48];
        int n = 0;
        if (total > 0)
            snprintf(f[n++], sizeof(f[0]), "%d / %d MB", (int)((double)pm / 1000.0 * total * 2352 / 1048576),
                     (int)((double)total * 2352 / 1048576));
        if (speed > 0.05f)
            snprintf(f[n++], sizeof(f[0]), "%.1fX", speed);
        str_copy(f[n++], sizeof(f[0]), cover ? tr("Cover found") : discimport_cover_none() ? tr("No cover") : tr("Cover..."));
        str_copy(f[n++], sizeof(f[0]), "BIN + CUE");
        float x = 300;
        for (int i = 0; i < n; ++i)
            x += ps1_flat(x, 770, 0, 58, f[i], 30, false, false) + 20;
    }
    ps1_cancel_plaque(1310, 770, 500, 130, false);
    {
        const char *games = discimport_games_dir();
        char where[SwanStationPS5_PATH_MAX + 8];
        snprintf(where, sizeof(where), "\xc2\xbb %s/", games);
        ps1_text_tracked(300, 912, 26, FONT_TECH, PS1_MUTED, ALIGN_LEFT, 0.08f, where);
        if (auto_launch)
        {
            char caps[96];
            ps1_text_drop(300, 956, 24, FONT_BOLD, 0xff9a9ad8u, ALIGN_LEFT,
                          ps1_caps(tr("The game starts when the copy ends"), caps, sizeof(caps)), 2);
        }
    }
    if (hold > 0.3f)
    {
        /* O held: the red balls fill up to the cancel */
        const int lit = (int)(fminf(hold / CANCEL_HOLD, 1.0f) * 10.0f + 0.5f);
        for (int i = 0; i < 10; ++i)
            ps1_ball(i < lit ? PS1_BALL_RED : PS1_BALL_BLUE, 1330 + i * 46, 940, i < lit ? 34 : 20, NULL, 0, 0, false, 1.0f);
    }
}

/* Reading the disc to play it from the drive: the disc turns in the middle */
static void ps1_reading_draw(void)
{
    ps1_background(2);
    ps1_title_plate(tr("Disc"), 1824, 64);
    ps1_disc(960, 470, 380, ps1_time() * 3.2f);
    char caps[96];
    ps1_text_outlined(960, 740, 48, FONT_BOLD, 0xffffffffu, ALIGN_CENTER, ps1_caps(tr("Reading the disc..."), caps, sizeof(caps)), 2.2f);
}

/* A disc put in: what to do with it, as a page of its own */
static void ps1_disc_ask_draw(void)
{
    ps1_background(1);
    ps1_title_plate(tr("Disc"), 1824, 64);
    /* the cover, or the disc when there is none */
    PlatTexture *cover = NULL;
    const int cover_game = dq.known ? dq.game : dp_index();
    if (cover_game >= 0 && cover_game < app.library.count)
        cover = covers_get(cover_game);
    if (cover)
    {
        int tw, th;
        plat_texture_size(cover, &tw, &th);
        const float box = 420, k = fminf(box / tw, box / th), dw = tw * k, dh = th * k;
        draw_rect(140 + (box - dw) * 0.5f + 10, 170 + (box - dh) * 0.5f + 18, dw, dh, 0x50000014u);
        plat_draw_texture(cover, 140 + (box - dw) * 0.5f, 170 + (box - dh) * 0.5f, dw, dh, 0xffffffffu, true);
    }
    else
        ps1_disc(350, 380, 360, ps1_time() * 1.2f);
    char caps[200];
    ps1_text_tracked(660, 200, 30, FONT_BOLD, PS1_SOFT, ALIGN_LEFT, 0.06f, ps1_caps(tr("Disc in the drive"), caps, sizeof(caps)));
    text_draw_fit(660, 248, 58, FONT_SQUARE, 0xffffffffu, ALIGN_LEFT, 1160, ps1_caps(dq.id.title, caps, sizeof(caps)));
    if (dq.id.serial[0])
        ps1_text_tracked(660, 324, 30, FONT_TECH, 0xffe6e6ffu, ALIGN_LEFT, 0.12f, dq.id.serial);
    if (dq.known)
    {
        ps1_text_drop(660, 400, 30, FONT_BOLD, PS1_SOFT, ALIGN_LEFT,
                      ps1_caps(tr("This game is already in your games. Do you want to start it?"), caps, sizeof(caps)), 2);
        ps1_splat(660, 470, 420, 116, 61, tr("Start"), 56, false);
        ps1_cursor(660 + 384, 470 + 80, 86);
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE};
        const char *labels[] = {tr("Start"), tr("Cancel")};
        ps1_prompts(96, 946, glyphs, labels, 2, 27, false);
        return;
    }
    ps1_text_drop(660, 400, 30, FONT_BOLD, PS1_SOFT, ALIGN_LEFT, ps1_caps(tr("How do you want to play it?"), caps, sizeof(caps)), 2);
    ps1_splat_choice(660, 470, 460, 116, 62, tr("Play from disc"), 50, dq.sel == 0, false);
    ps1_splat_choice(1160, 470, 460, 116, 63, tr("Copy to storage"), 50, dq.sel == 1, false);
    ps1_text_tracked(680, 610, 24, FONT_TECH, PS1_MUTED, ALIGN_LEFT, 0.06f, ps1_caps(tr("Starts right now."), caps, sizeof(caps)));
    ps1_text_tracked(680, 644, 24, FONT_TECH, PS1_MUTED, ALIGN_LEFT, 0.06f, ps1_caps(tr("The disc stays in the drive."), caps, sizeof(caps)));
    ps1_text_tracked(1180, 610, 24, FONT_TECH, PS1_MUTED, ALIGN_LEFT, 0.06f, ps1_caps(tr("A few minutes, then it plays."), caps, sizeof(caps)));
    ps1_text_tracked(1180, 644, 24, FONT_TECH, PS1_MUTED, ALIGN_LEFT, 0.06f, ps1_caps(tr("After that the disc can come out."), caps, sizeof(caps)));
    ps1_cursor((dq.sel ? 1160 : 660) + 424, 470 + 80, 86);
    /* left and right both choose: show both */
    const float gap = 27 * 46.0f / 27.0f;
    const float w = ps1_prompt_pair(96, 946, GLYPH_LEFT, GLYPH_RIGHT, tr("Choose"), 27) + gap;
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE};
    const char *labels[] = {tr("Select"), tr("Cancel")};
    ps1_prompts(96 + w, 946, glyphs, labels, 2, 27, false);
}

static void ps1_message_box_draw(void)
{
    static const int glyphs[] = {GLYPH_CROSS};
    const char *labels[] = {tr("Close")};
    ps1_notice_screen(tr("Message"), box_text, glyphs, labels, 1);
}

static void ps1_question_draw(void)
{
    char text[600];
    snprintf(text, sizeof(text), "%s %s", tr("SwanStationPS5 was updated to the latest version!"),
             tr("Resetting to the factory settings is recommended because of the new improvements. Do you want to do it?"));
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE};
    const char *labels[] = {tr("Reset to factory settings"), tr("Keep my settings")};
    ps1_notice_screen(tr("New version"), text, glyphs, labels, 2);
}

static void ps1_sbi_draw(float p)
{
    ps1_background(2);
    ps1_title_plate(tr("LibCrypt"), 1824, 64);
    ps1_disc(560, 470, 360, ps1_time() * 2.0f);
    char caps[160];
    ps1_text_drop(96, 830, 30, FONT_BOLD, PS1_SOFT, ALIGN_LEFT,
                  ps1_caps(tr("Extracting the CRC-32 to try to apply the LibCrypt patch..."), caps, sizeof(caps)), 2);
    const int lit = (int)(fminf(p, 1.0f) * 20.0f);
    char next[8];
    snprintf(next, sizeof(next), "%d", (lit < 20 ? lit : 19) + 1);
    ps1_dot_grid(1040, 420, 134, 110, lit, next);
}

#if defined(SwanStationPS5_PREVIEW)
/* The interface preview: SwanStationPS5_FAKE_SCREEN=1 shows the install at 37%, 2 a disc put in, 3 a message box,
 * 4 a disc being read (no drive is needed) */
static void ps1_preview_fake(int which)
{
    if (which == 1)
        ps1_import_draw("Metal Gear Solid", NULL, 0.37f, 330, 0.0f);
    else if (which == 2)
    {
        memset(&dq, 0, sizeof(dq));
        dq.open = true;
        dq.game = -1;
        str_copy(dq.id.title, sizeof(dq.id.title), "Metal Gear Solid");
        str_copy(dq.id.serial, sizeof(dq.id.serial), "SLUS-00594");
        ps1_disc_ask_draw();
        dq.open = false;
    }
    else if (which == 3)
    {
        str_copy(box_text, sizeof(box_text), tr("The CD drive has no disc ready. Put in a disc, wait for it to spin up and try again."));
        ps1_message_box_draw();
    }
    else if (which == 4)
        ps1_reading_draw();
}
#endif
