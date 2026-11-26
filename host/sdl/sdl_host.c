/*
 * sdl_host.c - the desktop host (sdl_host.h). The engine calls present_frame once per
 * frame: the picture goes to a streaming texture, then the host waits for the frame's
 * time slot (59.92 Hz NTSC, 49.70 Hz PAL; none while fast-forwarding), handling window
 * events. poll_pad reads the keyboard and the first game controller.
 *
 * VSync: with the display's refresh within 1.5 % of the game's rate, presenting with
 * VSync paces the game (no tearing, no drift beyond that 1.5 %); at other refresh rates
 * (144 Hz, 50 Hz...) VSync only avoids tearing and the clock keeps the game's own rate.
 * The frame rate actually reached shows in the title, and on the picture with F.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "sdl_host.h"

struct nmd_sdl {
    SDL_Window         *window;
    SDL_Renderer       *renderer;
    SDL_Texture        *texture;
    SDL_GameController *pad;
    int                 tex_w, tex_h;
    double              frame_s;     /* seconds per frame */
    uint64_t            next;        /* performance counter of the next frame */
    bool                quit, paused, fullscreen;
    bool                vsync, vsync_paces, show_fps, fast_forward;
    char                title[96];
    uint64_t            fps_t0;      /* frame rate measurement */
    unsigned            fps_frames;
    double              fps;
    uint32_t            pic[320 * 240];   /* the picture with the overlay */
};

/* ---- The frame rate on the picture: a 3x5 font for "0-9 . FPS" -------------------------- */
static const uint16_t glyph3x5[14] = {
    075557, 022222, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717, /* 0-9 */
    000002, 074744, 075744, 074717,                                                  /* . F P S */
};

static void overlay_fps(nmd_sdl *s, int width)
{
    char text[16];
    snprintf(text, sizeof text, "%.1f FPS", s->fps);
    int n = (int)strlen(text), scale = 2, x0 = width - n * 4 * scale - 4, y0 = 4;
    for (int i = 0; i < n; i++) {
        char c = text[i];
        int g = c >= '0' && c <= '9' ? c - '0' : c == '.' ? 10 : c == 'F' ? 11 : c == 'P' ? 12 : c == 'S' ? 13 : -1;
        if (g < 0) continue;
        for (int y = -1; y <= 5; y++)                   /* shadow box, then the glyph */
            for (int x = -1; x <= 3; x++) {
                bool on = y >= 0 && y < 5 && x >= 0 && x < 3 && (glyph3x5[g] >> ((4 - y) * 3 + (2 - x)) & 1);
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++) {
                        int px = x0 + (i * 4 + x) * scale + dx, py = y0 + y * scale + dy;
                        if (px < 0 || px >= width || py < 0 || py >= 240) continue;
                        uint32_t *p = &s->pic[py * 320 + px];
                        *p = on ? 0xFFFFFF40u : (*p >> 2 & 0x3F3F3Fu) | 0xFF000000u;
                    }
            }
    }
}

static void measure_fps(nmd_sdl *s)
{
    uint64_t now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    if (!s->fps_t0) s->fps_t0 = now;
    s->fps_frames++;
    if (now - s->fps_t0 >= freq / 2) {                  /* twice a second */
        s->fps = s->fps_frames * (double)freq / (double)(now - s->fps_t0);
        s->fps_t0 = now;
        s->fps_frames = 0;
        char t[160];
        snprintf(t, sizeof t, "%s - %.1f fps%s", s->title, s->fps, s->vsync ? " (vsync)" : "");
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
            else if (e.key.keysym.sym == SDLK_f) s->show_fps = !s->show_fps;
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
    if (s->show_fps) {
        memcpy(s->pic, argb, (size_t)height * 320 * sizeof *argb);
        overlay_fps(s, width);
        argb = s->pic;
    }
    SDL_UpdateTexture(s->texture, NULL, argb, 320 * (int)sizeof *argb);
    SDL_RenderClear(s->renderer);
    SDL_Rect dst = { 0, 0, 320, height };
    SDL_RenderCopy(s->renderer, s->texture, NULL, &dst);
    SDL_RenderPresent(s->renderer);
}

static void present(void *user, uint64_t frame, const uint32_t *argb, int width, int height)
{
    (void)frame;
    nmd_sdl *s = user;
    measure_fps(s);
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

nmd_sdl *nmd_sdl_open(const char *title, int scale, bool fullscreen, bool vsync, nmd_video video)
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
    if (s->pad) SDL_GameControllerClose(s->pad);
    if (s->texture) SDL_DestroyTexture(s->texture);
    if (s->renderer) SDL_DestroyRenderer(s->renderer);
    if (s->window) SDL_DestroyWindow(s->window);
    free(s);
    SDL_Quit();
}
