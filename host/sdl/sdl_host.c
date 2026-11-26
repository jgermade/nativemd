/*
 * sdl_host.c - the desktop host (sdl_host.h). The engine calls present_frame once per
 * frame: the picture goes to a streaming texture, then the host waits for the frame's
 * time slot (59.92 Hz NTSC, 49.70 Hz PAL; none while fast-forwarding), handling window
 * events. poll_pad reads the keyboard and the first game controller.
 *
 * VSync: with the display's refresh within 1.5 % of the game's rate, presenting with
 * VSync paces the game (no tearing, no drift beyond that 1.5 %); at other refresh rates
 * (144 Hz, 50 Hz...) VSync only avoids tearing and the clock keeps the game's own rate.
 * The frame rate reached and the most the machine could run (the engine's time per frame,
 * waits excluded) show in the title; with Caps Lock on, a HUD drawn at the window's
 * resolution (Inter, antialiased: hud_text.c) shows them with the region and
 * rate, VSync, the display's refresh, pause / fast forward. The capacity is printed at exit.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "sdl_host.h"
#include "hud_text.h"

struct nmd_sdl {
    SDL_Window         *window;
    SDL_Renderer       *renderer;
    SDL_Texture        *texture;
    SDL_GameController *pad;
    int                 tex_w, tex_h;
    double              frame_s;     /* seconds per frame */
    uint64_t            next;        /* performance counter of the next frame */
    bool                quit, paused, fullscreen;
    bool                vsync, vsync_paces, fast_forward;
    int                 display_hz;
    char                region[8];
    char                title[96];
    uint64_t            fps_t0;      /* frame rate measurement */
    unsigned            fps_frames;
    double              fps;
    uint64_t            resumed;     /* when the engine got control back (end of present) */
    uint64_t            work, work_total;   /* emulation + drawing time, no waits */
    uint64_t            frames_total;
    double              capacity;    /* frames per second the machine could run */
    hud_font           *font;        /* the HUD's text at its current size */
};

/* ---- The HUD (Caps Lock on): Inter text drawn at the window's resolution over the picture -- */
static void draw_hud(nmd_sdl *s)
{
    char rows[7][2][40];
    int n = 0;
    double rate = 1.0 / s->frame_s;
#define ROW(label, ...) do { snprintf(rows[n][0], 40, "%s", label); snprintf(rows[n][1], 40, __VA_ARGS__); n++; } while (0)
    ROW("FPS", "%.1f / %.2f", s->fps, rate);
    if (s->capacity > 0) ROW("Max", "%.0f fps (%.1fx)", s->capacity, s->capacity / rate);
    ROW("Region", "%s, %s Hz", s->region, rate < 55 ? "50" : "60");
    ROW("VSync", "%s", !s->vsync ? "off" : s->vsync_paces ? "on" : "on (clock paces)");
    ROW("Display", "%d Hz", s->display_hz);
    ROW("", "%s", s->paused ? "Paused" : s->fast_forward ? "Fast forward" : "");   /* always a row */
#undef ROW
    /* A fixed size: the widest text each column can show (so the panel does not change
     * size with its content). */
    static const char *const label_widest[] = { "Display", "Region", "VSync" };
    static const char *const value_widest[] = { "000.0 / 00.00", "0000 fps (00.0x)", "Europe, 00 Hz",
                                                "on (clock paces)", "000 Hz", "Fast forward" };

    SDL_Renderer *r = s->renderer;
    SDL_RenderSetLogicalSize(r, 0, 0);                  /* window pixels from here */
    int ow, oh;
    SDL_GetRendererOutputSize(r, &ow, &oh);
    int px = oh / 36 > 12 ? oh / 36 : 12;
    s->font = hud_font_get(r, s->font, px);
    if (s->font) {
        float lw = 0, vw = 0, gap = (float)px * 0.8f, pad = (float)px * 0.7f, lh = hud_line_height(s->font);
        for (size_t i = 0; i < sizeof label_widest / sizeof *label_widest; i++) {
            float a = hud_text_width(s->font, label_widest[i]);
            if (a > lw) lw = a;
        }
        for (size_t i = 0; i < sizeof value_widest / sizeof *value_widest; i++) {
            float b = hud_text_width(s->font, value_widest[i]);
            if (b > vw) vw = b;
        }
        for (int i = 0; i < n; i++) {                   /* (a value wider than its template) */
            float b = hud_text_width(s->font, rows[i][1]);
            if (b > vw) vw = b;
        }
        float w = lw + gap + vw + 2 * pad, h = (float)n * lh + 2 * pad - lh * 0.15f;
        SDL_FRect panel = { (float)ow - w - pad, pad, w, h };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 10, 12, 20, 180);
        SDL_RenderFillRectF(r, &panel);
        for (int i = 0; i < n; i++) {
            float y = panel.y + pad + (float)i * lh;
            SDL_SetRenderDrawColor(r, 160, 170, 190, 255);                 /* labels */
            hud_text_draw(r, s->font, panel.x + pad, y, rows[i][0]);
            if (i == 0) SDL_SetRenderDrawColor(r, 255, 230, 90, 255);     /* the frame rate */
            else SDL_SetRenderDrawColor(r, 245, 245, 250, 255);
            hud_text_draw(r, s->font, panel.x + pad + lw + gap, y, rows[i][1]);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderSetLogicalSize(r, 320, s->tex_h);
}

static void measure_fps(nmd_sdl *s)
{
    uint64_t now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    if (!s->fps_t0) s->fps_t0 = now;
    s->fps_frames++;
    if (now - s->fps_t0 >= freq / 2) {                  /* twice a second */
        s->fps = s->fps_frames * (double)freq / (double)(now - s->fps_t0);
        s->capacity = s->work ? s->fps_frames * (double)freq / (double)s->work : 0;
        s->fps_t0 = now;
        s->fps_frames = 0;
        s->work = 0;
        char t[160];
        snprintf(t, sizeof t, "%s - %.1f fps (max %.0f)%s", s->title, s->fps, s->capacity,
                 s->vsync ? " vsync" : "");
        SDL_SetWindowTitle(s->window, t);
    }
}

/* Whether VSync can pace the game: the display's refresh close to the game's rate. */
static void update_vsync(nmd_sdl *s)
{
    SDL_RenderSetVSync(s->renderer, s->vsync ? 1 : 0);
    SDL_DisplayMode m;
    int hz = SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(s->window), &m) == 0 ? m.refresh_rate : 0;
    double rate = 1.0 / s->frame_s;
    s->vsync_paces = s->vsync && hz > 0 && fabs(hz - rate) / rate < 0.015;
    s->display_hz = hz;
    fprintf(stderr, "nativemd: vsync %s (display %d Hz, game %.2f Hz%s)\n", s->vsync ? "on" : "off", hz, rate,
            s->vsync_paces ? ": vsync paces the game" : s->vsync ? ": the clock paces the game" : "");
}

static void toggle_fullscreen(nmd_sdl *s)
{
    s->fullscreen = !s->fullscreen;
    SDL_SetWindowFullscreen(s->window, s->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static void open_first_pad(nmd_sdl *s)
{
    if (s->pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i) && (s->pad = SDL_GameControllerOpen(i))) {
            fprintf(stderr, "nativemd: controller: %s\n", SDL_GameControllerName(s->pad));
            return;
        }
}

static void handle_events(nmd_sdl *s)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: s->quit = true; break;
        case SDL_KEYDOWN:
            if (e.key.repeat) break;
            if (e.key.keysym.sym == SDLK_ESCAPE) s->quit = true;
            else if ((e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) &&
                     (e.key.keysym.mod & (KMOD_ALT | KMOD_GUI)))
                toggle_fullscreen(s);                   /* Alt+Enter (Cmd+Enter on macOS) */
            else if (e.key.keysym.sym == SDLK_p) s->paused = !s->paused;
            else if (e.key.keysym.sym == SDLK_v) { s->vsync = !s->vsync; update_vsync(s); }
            break;
        case SDL_CONTROLLERDEVICEADDED: open_first_pad(s); break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (s->pad && !SDL_GameControllerGetAttached(s->pad)) {
                SDL_GameControllerClose(s->pad);
                s->pad = NULL;
                open_first_pad(s);
            }
            break;
        }
    }
}

static void draw(nmd_sdl *s, const uint32_t *argb, int width, int height)
{
    if (width != s->tex_w || height != s->tex_h) {
        if (s->texture) SDL_DestroyTexture(s->texture);
        s->texture = SDL_CreateTexture(s->renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, width, height);
        s->tex_w = width;
        s->tex_h = height;
        SDL_RenderSetLogicalSize(s->renderer, 320, height);   /* 4:3 either way: H32 stretched */
    }
    SDL_UpdateTexture(s->texture, NULL, argb, 320 * (int)sizeof *argb);
    SDL_RenderClear(s->renderer);
    SDL_Rect src = { 0, 0, width, height }, dst = { 0, 0, 320, height };
    SDL_RenderCopy(s->renderer, s->texture, &src, &dst);
    if (SDL_GetModState() & KMOD_CAPS) draw_hud(s);     /* Caps Lock on: the HUD */
    SDL_RenderPresent(s->renderer);
}

static void pace(nmd_sdl *s, const uint32_t *argb, int width, int height);

static void present(void *user, uint64_t frame, const uint32_t *argb, int width, int height)
{
    (void)frame;
    nmd_sdl *s = user;
    uint64_t now = SDL_GetPerformanceCounter();
    if (s->resumed) {                                   /* the engine's work for this frame */
        s->work += now - s->resumed;
        s->work_total += now - s->resumed;
        s->frames_total++;
    }
    measure_fps(s);
    pace(s, argb, width, height);
    s->resumed = SDL_GetPerformanceCounter();
}

/* Show the frame, handle events and pause, then wait for the frame's time slot. */
static void pace(nmd_sdl *s, const uint32_t *argb, int width, int height)
{
    draw(s, argb, width, height);
    handle_events(s);
    while (s->paused && !s->quit) {                     /* P: hold the game here */
        SDL_Delay(16);
        handle_events(s);
        draw(s, argb, width, height);
    }
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint64_t freq = SDL_GetPerformanceFrequency(), now = SDL_GetPerformanceCounter();
    uint64_t step = (uint64_t)(s->frame_s * (double)freq);
    bool ff = keys[SDL_SCANCODE_TAB] != 0;
    if (ff != s->fast_forward) {                        /* no VSync while fast-forwarding */
        s->fast_forward = ff;
        SDL_RenderSetVSync(s->renderer, s->vsync && !ff ? 1 : 0);
    }
    if (ff) { s->next = now; return; }
    if (s->vsync_paces) { s->next = 0; return; }        /* SDL_RenderPresent waited for it */
    if (s->next == 0 || now > s->next + 4 * step) s->next = now;   /* late: resynchronise */
    s->next += step;
    while ((now = SDL_GetPerformanceCounter()) < s->next) {
        uint64_t left_ms = (s->next - now) * 1000 / freq;
        if (left_ms > 2) SDL_Delay((uint32_t)(left_ms - 1));
    }
}

static uint16_t poll_pad(void *user, int port, uint64_t frame)
{
    (void)frame;
    nmd_sdl *s = user;
    if (port != 0) return 0;
    const uint8_t *k = SDL_GetKeyboardState(NULL);
    uint16_t b = 0;
    if (k[SDL_SCANCODE_UP])    b |= NMD_PAD_UP;
    if (k[SDL_SCANCODE_DOWN])  b |= NMD_PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT])  b |= NMD_PAD_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) b |= NMD_PAD_RIGHT;
    if (k[SDL_SCANCODE_A]) b |= NMD_PAD_A;
    if (k[SDL_SCANCODE_S]) b |= NMD_PAD_B;
    if (k[SDL_SCANCODE_D]) b |= NMD_PAD_C;
    if (k[SDL_SCANCODE_Z]) b |= NMD_PAD_X;               /* 6-button pad (not yet seen by games: */
    if (k[SDL_SCANCODE_X]) b |= NMD_PAD_Y;               /* io.c has no 6-button protocol yet) */
    if (k[SDL_SCANCODE_C]) b |= NMD_PAD_Z;
    bool fullscreen_combo = (SDL_GetModState() & (KMOD_ALT | KMOD_GUI)) != 0;
    if ((k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]) && !fullscreen_combo) b |= NMD_PAD_START;
    if (s->pad) {
        SDL_GameController *c = s->pad;
        const int dead = 12000;
        int ax = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
        int ay = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -dead)    b |= NMD_PAD_UP;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > dead)   b |= NMD_PAD_DOWN;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -dead)  b |= NMD_PAD_LEFT;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > dead)  b |= NMD_PAD_RIGHT;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A)) b |= NMD_PAD_A;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B)) b |= NMD_PAD_B;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X)) b |= NMD_PAD_C;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y)) b |= NMD_PAD_Y;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) b |= NMD_PAD_X;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= NMD_PAD_Z;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK)) b |= NMD_PAD_MODE;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START)) b |= NMD_PAD_START;
    }
    return b;
}

static uint64_t time_us(void *user)
{
    (void)user;
    return SDL_GetPerformanceCounter() * 1000000u / SDL_GetPerformanceFrequency();
}

nmd_sdl *nmd_sdl_open(const char *title, const char *region, int scale, bool fullscreen, bool vsync,
                      nmd_video video)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "nativemd: SDL_Init: %s\n", SDL_GetError());
        return NULL;
    }
    nmd_sdl *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    if (scale < 1) scale = 3;
    s->frame_s = video == NMD_VIDEO_PAL ? 1.0 / 49.701 : 1.0 / 59.922;
    s->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 320 * scale, 224 * scale, SDL_WINDOW_RESIZABLE);
    if (!s->window) {
        fprintf(stderr, "nativemd: SDL_CreateWindow: %s\n", SDL_GetError());
        free(s);
        return NULL;
    }
    s->renderer = SDL_CreateRenderer(s->window, -1, SDL_RENDERER_ACCELERATED);
    if (!s->renderer) s->renderer = SDL_CreateRenderer(s->window, -1, SDL_RENDERER_SOFTWARE);
    if (!s->renderer) {
        fprintf(stderr, "nativemd: SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(s->window);
        free(s);
        return NULL;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetIntegerScale(s->renderer, SDL_FALSE);
    snprintf(s->title, sizeof s->title, "%s", title);
    for (int i = 0; i < 7 && region && region[i]; i++)
        s->region[i] = (char)(region[i] >= 'a' && region[i] <= 'z' ? region[i] - 32 : region[i]);
    if (!strcmp(s->region, "US")) snprintf(s->region, sizeof s->region, "USA");
    else if (!strcmp(s->region, "JP")) snprintf(s->region, sizeof s->region, "Japan");
    else if (!strcmp(s->region, "EU")) snprintf(s->region, sizeof s->region, "Europe");
    s->vsync = vsync;
    update_vsync(s);
    if (fullscreen) toggle_fullscreen(s);
    open_first_pad(s);
    return s;
}

nmd_host nmd_sdl_host(nmd_sdl *s)
{
    nmd_host h = { .user = s, .time_us = time_us, .present_frame = present, .poll_pad = poll_pad };
    return h;
}

bool nmd_sdl_quit_requested(const nmd_sdl *s) { return s->quit; }

void nmd_sdl_close(nmd_sdl *s)
{
    if (!s) return;
    if (s->frames_total)
        fprintf(stderr, "nativemd: %llu frames; the machine could run %.0f frames per second (%.1fx)\n",
                (unsigned long long)s->frames_total,
                (double)s->frames_total * (double)SDL_GetPerformanceFrequency() / (double)s->work_total,
                (double)s->frames_total * (double)SDL_GetPerformanceFrequency() / (double)s->work_total * s->frame_s);
    if (s->pad) SDL_GameControllerClose(s->pad);
    hud_font_free(s->font);
    if (s->texture) SDL_DestroyTexture(s->texture);
    if (s->renderer) SDL_DestroyRenderer(s->renderer);
    if (s->window) SDL_DestroyWindow(s->window);
    free(s);
    SDL_Quit();
}
