/*
 * hud_text.c - the HUD's text (hud_text.h). ASCII 32-126 of the embedded Inter Medium is
 * packed into one alpha texture at the requested size, oversampled 2x horizontally for
 * sub-pixel positioning. Digits get the widest digit's advance (tabular figures), so
 * changing numbers do not make the text jitter.
 */
#include <stdlib.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "hud_text.h"

extern const unsigned char nmd_inter_medium[];
extern const size_t nmd_inter_medium_size;

enum { FIRST = 32, COUNT = 95, ATLAS = 512 };

struct hud_font {
    int              px;
    SDL_Texture     *tex;
    stbtt_packedchar chars[COUNT];
    float            ascent, line, digit_adv;
};

void hud_font_free(hud_font *f)
{
    if (!f) return;
    if (f->tex) SDL_DestroyTexture(f->tex);
    free(f);
}

hud_font *hud_font_get(SDL_Renderer *r, hud_font *f, int px)
{
    if (f && f->px == px) return f;
    hud_font_free(f);
    f = calloc(1, sizeof *f);
    unsigned char *alpha = calloc(ATLAS, ATLAS);
    uint32_t *argb = malloc(ATLAS * ATLAS * 4);
    stbtt_fontinfo info;
    stbtt_pack_context pc;
    if (!f || !alpha || !argb || !stbtt_InitFont(&info, nmd_inter_medium, 0) ||
        !stbtt_PackBegin(&pc, alpha, ATLAS, ATLAS, 0, 1, NULL)) {
        free(alpha); free(argb); free(f);
        return NULL;
    }
    stbtt_PackSetOversampling(&pc, 2, 1);
    int ok = stbtt_PackFontRange(&pc, nmd_inter_medium, 0, (float)px, FIRST, COUNT, f->chars);
    stbtt_PackEnd(&pc);
    if (ok) {
        for (int i = 0; i < ATLAS * ATLAS; i++) argb[i] = (uint32_t)alpha[i] << 24 | 0xFFFFFF;
        f->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ATLAS, ATLAS);
    }
    if (f->tex) {
        SDL_UpdateTexture(f->tex, NULL, argb, ATLAS * 4);
        SDL_SetTextureBlendMode(f->tex, SDL_BLENDMODE_BLEND);
        float scale = stbtt_ScaleForPixelHeight(&info, (float)px);
        int asc, desc, gap;
        stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
        f->ascent = (float)asc * scale;
        f->line = (float)(asc - desc + gap) * scale;
        for (int d = '0'; d <= '9'; d++)
            if (f->chars[d - FIRST].xadvance > f->digit_adv) f->digit_adv = f->chars[d - FIRST].xadvance;
        f->px = px;
    }
    free(alpha);
    free(argb);
    if (!f->tex) { free(f); return NULL; }
    return f;
}

static float advance(const hud_font *f, int c)
{
    return c >= '0' && c <= '9' ? f->digit_adv : f->chars[c - FIRST].xadvance;
}

float hud_text_width(const hud_font *f, const char *text)
{
    float w = 0;
    for (const char *p = text; *p; p++)
        if (*p >= FIRST && *p < FIRST + COUNT) w += advance(f, *p);
    return w;
}

float hud_line_height(const hud_font *f) { return f->line; }

void hud_text_draw(SDL_Renderer *r, hud_font *f, float x, float y, const char *text)
{
    Uint8 cr, cg, cb, ca;
    SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
    SDL_SetTextureColorMod(f->tex, cr, cg, cb);
    SDL_SetTextureAlphaMod(f->tex, ca);
    float base = y + f->ascent;
    for (const char *p = text; *p; p++) {
        int c = *p;
        if (c < FIRST || c >= FIRST + COUNT) continue;
        const stbtt_packedchar *g = &f->chars[c - FIRST];
        float shift = c >= '0' && c <= '9' ? (f->digit_adv - g->xadvance) / 2 : 0;
        if (c != ' ') {
            SDL_Rect src = { g->x0, g->y0, g->x1 - g->x0, g->y1 - g->y0 };
            SDL_FRect dst = { x + shift + g->xoff, base + g->yoff, g->xoff2 - g->xoff, g->yoff2 - g->yoff };
            SDL_RenderCopyF(r, f->tex, &src, &dst);
        }
        x += advance(f, c);
    }
}
