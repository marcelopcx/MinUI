#ifndef __THEME_H__
#define __THEME_H__

typedef struct SDL_Surface SDL_Surface;

#define THEMES_PATH SDCARD_PATH "/Temas"
#define THEME_ACTIVE_PATH USERDATA_PATH "/theme.txt"
#define THEME_DEFAULT_FONT RES_PATH "/BPreplayBold-unhinted.otf"

void THEME_init(SDL_Surface* screen);
void THEME_blitBackground(SDL_Surface* screen);
int THEME_bgFrameChanged(void);
const char* THEME_fontPath(void);
const char* THEME_activeId(void);
const char* THEME_activeName(void);
int THEME_hasBackdrop(void);
/** Música de fondo del menú (solo en minui; el tema la define con "music"). */
int THEME_hasMusic(void);
void THEME_musicStart(void);
void THEME_musicStop(void);
/** Animación de arranque de juego del tema (solo PS2). Salta con cualquier botón. */
int THEME_hasBootAnim(void);
int THEME_startBootAnim(SDL_Surface* screen); // en segundo plano, mientras se carga el juego
void THEME_finishBootAnim(int abort); // espera a que acabe (o la corta con abort=1)
void THEME_preview(const char* theme_id, SDL_PixelFormat* fmt);
int THEME_customPills(void);
int THEME_usesCustomFont(void);

enum {
	THEME_PILL_CLASSIC = 0,
	THEME_PILL_SOLID = 1,
	THEME_PILL_PSX = 2,
	/** Sin pastilla nunca: el seleccionado solo cambia de color (COLOR_ACCENT). */
	THEME_PILL_TEXT = 3,
};
int THEME_pillStyle(void);

/** Alineación a la retícula del background (tema PSX). */
int THEME_gridActive(void);
int THEME_gridCell(void);
int THEME_listX(void);
int THEME_listY(int row);
int THEME_listRowH(void);
int THEME_marginTop(void);
int THEME_alignWidth(int width_px);
int THEME_alignRight(int x);
int THEME_footerY(int screen_h);

enum {
	THEME_LIST_NONE = 0,
	THEME_LIST_PLAYSTATION = 1,
	THEME_LIST_DS = 2,
};
/** Lista con botones temáticos (PlayStation / DS). */
int THEME_listRowKind(void);
int THEME_uniformListRows(void);

void GFX_clearThemed(SDL_Surface* screen);
void GFX_reloadFonts(void);

#endif
