/* hud_text.h - antialiased text for the desktop host's HUD: Inter (embedded) rasterised
 * with stb_truetype into a glyph atlas texture, one per pixel size. */
#ifndef NMD_HUD_TEXT_H
#define NMD_HUD_TEXT_H

#include <SDL.h>

typedef struct hud_font hud_font;

/* A font at px pixels (the atlas is rebuilt when px changes). NULL on failure. */
hud_font *hud_font_get(SDL_Renderer *r, hud_font *f, int px);
void      hud_font_free(hud_font *f);
float     hud_text_width(const hud_font *f, const char *text);
float     hud_line_height(const hud_font *f);
/* Draws text with its top-left at (x, y), in the renderer's current draw colour. */
void      hud_text_draw(SDL_Renderer *r, hud_font *f, float x, float y, const char *text);

#endif
