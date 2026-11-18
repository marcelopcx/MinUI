#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "theme.h"

// de libmsettings (ya enlazada en todos los programas que usan theme.c)
int GetVolume(void);
void SetVolume(int value);

SDL_Color COLOR_WHITE;
SDL_Color COLOR_GRAY;
SDL_Color COLOR_BLACK;
SDL_Color COLOR_LIGHT_TEXT;
SDL_Color COLOR_DARK_TEXT;
SDL_Color COLOR_BUTTON_TEXT;
SDL_Color COLOR_ON_PILL;

static char active_id[64] = "Clasico";
static char active_name[64] = "Clasico";
static char font_path[MAX_PATH];
static SDL_Surface* background = NULL;
/* Fondo animado: un archivo crudo RGB565 con los fotogramas uno tras otro
   (anim_w x anim_h cada uno) que se dibuja en (anim_x, anim_y) sobre
   background.png. Solo hay un fotograma en RAM; leer un bloque y copiarlo no
   cuesta casi CPU (decodificar PNG a 640x480 en cada fotograma era lentísimo). */
static FILE* bg_fp = NULL;
static Uint16* bg_buf = NULL;
static SDL_Surface* bg_surf = NULL;
static int bg_cur_idx = -1;
static int bg_ax = 0, bg_ay = 0, bg_aw = 0, bg_ah = 0;
static int bg_max_hz = 15; // tope de redibujados por segundo causados por la animación
static unsigned long bg_last_redraw = 0;
static int bg_frame_count = 1;

/* Animación de arranque de juego (solo temas que la definen, p. ej. PS2):
   boot_raw = archivo crudo RGB565 (boot_w x boot_h por fotograma). */
static char boot_raw_file[MAX_PATH] = "";
static char boot_audio_file[MAX_PATH] = "";
static int boot_frames = 0;
static int boot_fps = 20;
static int boot_w = 640, boot_h = 480;

/* Música de fondo del menú (wav en bucle). */
static char music_file[MAX_PATH] = "";
static int music_volume = 100;
static int bg_anim_fps = 0;
static int bg_last_frame_idx = -1;
/* Si > 0: los cuadros 0..loop_from-1 se reproducen una sola vez al activar
   el tema (la "aparición"), y luego el ciclo infinito es solo
   loop_from..count-1. 0 = todo el clip se repite desde el principio. */
static int bg_anim_loop_from = 0;
static unsigned long bg_anim_start_ticks = 0;
static int theme_custom_visual = 0;
static int theme_custom_font = 0;
static int theme_light_footer = 0;
static int theme_pill_style = THEME_PILL_CLASSIC;
static int theme_grid_cell = 0;
static int theme_grid_ox = 0;
static int theme_grid_oy = 0;
static int theme_grid_manual = 0;
SDL_Color COLOR_FOOTER_TEXT;
SDL_Color COLOR_ACCENT;

static uint32_t parse_hex(const char* hex) {
	unsigned r = 0xff, g = 0xff, b = 0xff;
	if (!hex) return 0xffffff;
	while (*hex && !isxdigit((unsigned char)hex[0])) hex++;
	if (strlen(hex) >= 6)
		sscanf(hex, "%2x%2x%2x", &r, &g, &b);
	return (r << 16) | (g << 8) | b;
}

static SDL_Color rgb32_to_color(uint32_t rgb) {
	return (SDL_Color){(rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 0xff};
}

static int color_luma(uint32_t rgb) {
	int r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
	return (299 * r + 587 * g + 114 * b) / 1000;
}

static void set_on_pill(uint32_t pill_rgb) {
	COLOR_ON_PILL = color_luma(pill_rgb) > 160
		? (SDL_Color){0x00, 0x00, 0x00, 0xff}
		: (SDL_Color){0xff, 0xff, 0xff, 0xff};
}

static void darken_surface(SDL_Surface* s, int percent) {
	if (!s || percent <= 0) return;
	if (SDL_MUSTLOCK(s)) SDL_LockSurface(s);
	Uint8* row = (Uint8*)s->pixels;
	for (int y = 0; y < s->h; y++) {
		if (s->format->BytesPerPixel == 2) {
			Uint16* px = (Uint16*)row;
			for (int x = 0; x < s->w; x++) {
				Uint16 p = px[x];
				Uint8 r = ((p >> s->format->Rshift) & s->format->Rmask) >> (s->format->Rloss);
				Uint8 g = ((p >> s->format->Gshift) & s->format->Gmask) >> (s->format->Gloss);
				Uint8 b = ((p >> s->format->Bshift) & s->format->Bmask) >> (s->format->Bloss);
				r = (r * (100 - percent)) / 100;
				g = (g * (100 - percent)) / 100;
				b = (b * (100 - percent)) / 100;
				px[x] = SDL_MapRGB(s->format, r, g, b);
			}
		}
		row += s->pitch;
	}
	if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
}

/** Libera los fotogramas extra del fondo animado (background_1.png, ...). */
static void free_bg_frames(void) {
	if (bg_surf) { SDL_FreeSurface(bg_surf); bg_surf = NULL; }
	if (bg_buf) { free(bg_buf); bg_buf = NULL; }
	if (bg_fp) { fclose(bg_fp); bg_fp = NULL; }
	bg_cur_idx = -1;
	bg_frame_count = 1;
	bg_anim_fps = 0;
	bg_last_frame_idx = -1;
	bg_anim_loop_from = 0;
}

static void apply_default(SDL_PixelFormat* fmt) {
	uint32_t w = parse_hex("ffffff");
	uint32_t k = parse_hex("000000");
	uint32_t lg = parse_hex("7f7f7f");
	uint32_t g = parse_hex("999999");
	uint32_t dg = parse_hex("262626");
	uint32_t lt = parse_hex("cccccc");
	uint32_t dt = parse_hex("666666");

	RGB_WHITE = SDL_MapRGB(fmt, w >> 16, (w >> 8) & 0xff, w & 0xff);
	RGB_BLACK = SDL_MapRGB(fmt, 0, 0, 0);
	RGB_LIGHT_GRAY = SDL_MapRGB(fmt, lg >> 16, (lg >> 8) & 0xff, lg & 0xff);
	RGB_GRAY = SDL_MapRGB(fmt, g >> 16, (g >> 8) & 0xff, g & 0xff);
	RGB_DARK_GRAY = SDL_MapRGB(fmt, dg >> 16, (dg >> 8) & 0xff, dg & 0xff);

	COLOR_WHITE = rgb32_to_color(w);
	COLOR_GRAY = rgb32_to_color(g);
	COLOR_BLACK = rgb32_to_color(k);
	COLOR_LIGHT_TEXT = rgb32_to_color(lt);
	COLOR_DARK_TEXT = rgb32_to_color(dt);
	COLOR_BUTTON_TEXT = rgb32_to_color(g);
	COLOR_ACCENT = COLOR_WHITE;

	strcpy(font_path, THEME_DEFAULT_FONT);
	strcpy(active_name, "Clasico");
	strcpy(active_id, "Clasico");
	theme_custom_visual = 0;
	theme_custom_font = 0;
	theme_light_footer = 0;
	theme_pill_style = THEME_PILL_CLASSIC;
	theme_grid_cell = 0;
	theme_grid_ox = 0;
	theme_grid_oy = 0;
	theme_grid_manual = 0;
	COLOR_FOOTER_TEXT = (SDL_Color){0xff, 0xff, 0xff, 0xff};

	GFX_applyThemeAssets(fmt, w, dg, k, dg, w, w, k, dg);
	set_on_pill(w);
}

static char* json_value(const char* line, char* buf, size_t len) {
	char* colon = strchr(line, ':');
	if (!colon) return NULL;
	colon++;
	while (*colon == ' ' || *colon == '\t') colon++;
	if (*colon != '"') return NULL;
	colon++;
	char* end = strchr(colon, '"');
	if (!end) return NULL;
	size_t n = (size_t)(end - colon);
	if (n >= len) n = len - 1;
	memcpy(buf, colon, n);
	buf[n] = '\0';
	return buf;
}

static int sample_luma(SDL_Surface* s, int x, int y) {
	if (!s || x < 0 || y < 0 || x >= s->w || y >= s->h) return 0;
	Uint8* row = (Uint8*)s->pixels + y * s->pitch;
	if (s->format->BytesPerPixel == 2) {
		Uint16 p = ((Uint16*)row)[x];
		int r = (p >> s->format->Rshift) & s->format->Rmask;
		int g = (p >> s->format->Gshift) & s->format->Gmask;
		int b = (p >> s->format->Bshift) & s->format->Bmask;
		r = (r >> s->format->Rloss) << s->format->Rloss;
		g = (g >> s->format->Gloss) << s->format->Gloss;
		b = (b >> s->format->Bloss) << s->format->Bloss;
		return (299 * r + 587 * g + 114 * b) / 1000;
	}
	if (s->format->BytesPerPixel >= 3) {
		Uint32 p;
		memcpy(&p, row + x * s->format->BytesPerPixel, s->format->BytesPerPixel);
		Uint8 r, g, b;
		SDL_GetRGB(p, s->format, &r, &g, &b);
		return (299 * r + 587 * g + 114 * b) / 1000;
	}
	return 0;
}

static int collect_edge_peaks(SDL_Surface* s, int horiz, int pos, int peaks[], int max_peaks) {
	int count = 0;
	int last = -999;
	if (horiz) {
		for (int x = 1; x < s->w - 1; x++) {
			int e = abs(sample_luma(s, x, pos) - sample_luma(s, x - 1, pos));
			if (e < 14) continue;
			if (count > 0 && x - peaks[count - 1] < 3) {
				if (e > abs(sample_luma(s, peaks[count - 1], pos) - sample_luma(s, peaks[count - 1] - 1, pos)))
					peaks[count - 1] = x;
				continue;
			}
			if (x - last < 3) continue;
			if (count < max_peaks) peaks[count++] = x;
			last = x;
		}
	} else {
		for (int y = 1; y < s->h - 1; y++) {
			int e = abs(sample_luma(s, pos, y) - sample_luma(s, pos, y - 1));
			if (e < 14) continue;
			if (count > 0 && y - peaks[count - 1] < 3) {
				if (e > abs(sample_luma(s, pos, peaks[count - 1]) - sample_luma(s, pos, peaks[count - 1] - 1)))
					peaks[count - 1] = y;
				continue;
			}
			if (y - last < 3) continue;
			if (count < max_peaks) peaks[count++] = y;
			last = y;
		}
	}
	return count;
}

static int vote_cell_period(SDL_Surface* s, int horiz, int votes[]) {
	int peaks[96];
	int np;
	int lines[4];
	int nlines = 0;
	if (horiz) {
		lines[nlines++] = s->h / 4;
		lines[nlines++] = s->h / 2;
		lines[nlines++] = 3 * s->h / 4;
	} else {
		lines[nlines++] = s->w / 4;
		lines[nlines++] = s->w / 2;
		lines[nlines++] = 3 * s->w / 4;
	}
	for (int li = 0; li < nlines; li++) {
		np = collect_edge_peaks(s, horiz, lines[li], peaks, 96);
		for (int i = 0; i + 1 < np; i++) {
			int d = peaks[i + 1] - peaks[i];
			if (d >= 12 && d < 80) votes[d]++;
		}
	}
	int best = 0, best_v = 0;
	for (int d = 12; d < 80; d++) {
		if (votes[d] > best_v) {
			best_v = votes[d];
			best = d;
		}
	}
	return best_v > 0 ? best : 0;
}

static void theme_find_origins(SDL_Surface* s, int cell) {
	int peaks[96];
	int np = collect_edge_peaks(s, 1, s->h / 2, peaks, 96);
	if (np > 0)
		theme_grid_ox = peaks[0] % cell;
	np = collect_edge_peaks(s, 0, s->w / 4, peaks, 96);
	if (np > 0)
		theme_grid_oy = peaks[0] % cell;
}

static void theme_calibrate_grid(SDL_Surface* s) {
	int vh[80], vv[80];
	memset(vh, 0, sizeof(vh));
	memset(vv, 0, sizeof(vv));
	int cell_h = vote_cell_period(s, 1, vh);
	int cell_v = vote_cell_period(s, 0, vv);
	int cell = cell_h;
	if (cell_v && (!cell || vv[cell_v] > vh[cell])) cell = cell_v;
	if (!cell) cell = 32;
	theme_grid_cell = cell;
	theme_find_origins(s, cell);
}

int THEME_gridActive(void) {
	return theme_pill_style == THEME_PILL_PSX && theme_grid_cell > 0;
}

int THEME_gridCell(void) {
	return theme_grid_cell;
}

static int theme_snap_floor(int v) {
	if (!THEME_gridActive()) return v;
	int c = theme_grid_cell;
	int o = theme_grid_ox;
	return o + ((v - o) / c) * c;
}

static int theme_snap_nearest(int v) {
	if (!THEME_gridActive()) return v;
	int c = theme_grid_cell;
	int o = theme_grid_ox;
	int n = ((v - o) + c / 2) / c;
	return o + n * c;
}

int THEME_marginTop(void) {
	return SCALE1(PADDING);
}

int THEME_listX(void) {
	int x = SCALE1(PADDING);
	if (THEME_gridActive())
		x = theme_snap_nearest(x);
	return x;
}

int THEME_listRowH(void) {
	/* Altura fija: 6 filas + footer; no inflar por la retícula. */
	return SCALE1(PILL_SIZE);
}

int THEME_listY(int row) {
	return THEME_marginTop() + row * THEME_listRowH();
}

int THEME_alignWidth(int width_px) {
	if (!THEME_gridActive()) return width_px;
	int c = theme_grid_cell;
	int o = theme_grid_ox;
	int rel = width_px - o;
	if (rel < 0) rel = 0;
	return o + ((rel + c - 1) / c) * c;
}

int THEME_alignRight(int x) {
	return THEME_gridActive() ? theme_snap_floor(x) : x;
}

int THEME_footerY(int screen_h) {
	int y = screen_h - SCALE1(PADDING) - THEME_listRowH();
	if (THEME_gridActive())
		y = theme_snap_floor(y);
	return y;
}

int THEME_listRowKind(void) {
	if (!THEME_hasBackdrop() || !theme_custom_visual)
		return THEME_LIST_NONE;
	if (exactMatch(active_id, "PlayStation"))
		return THEME_LIST_PLAYSTATION;
	if (exactMatch(active_id, "DS"))
		return THEME_LIST_DS;
	return THEME_LIST_NONE;
}

int THEME_uniformListRows(void) {
	return THEME_listRowKind() != THEME_LIST_NONE;
}

static void load_theme_file(const char* dir, SDL_PixelFormat* fmt) {
	char path[MAX_PATH];
	char line[256];
	char val[128];
	uint32_t text = parse_hex("ffffff");
	uint32_t text_dim = parse_hex("cccccc");
	uint32_t pill_sel = parse_hex("ffffff");
	uint32_t pill_unsel = parse_hex("262626");
	uint32_t pill_menu = parse_hex("000000");
	uint32_t button = parse_hex("ffffff");
	uint32_t bar = parse_hex("ffffff");
	uint32_t bar_bg = parse_hex("000000");
	uint32_t bar_menu = parse_hex("262626");
	uint32_t bg_solid = 0;
	int bg_dim = 0;
	char pill_style_name[16] = "";
	char bg_file[64] = "";
	char font_file[64] = "";
	int grid_cell_cfg = 0;
	int grid_ox_cfg = -1;
	int grid_oy_cfg = -1;
	int anim_frames_cfg = 0;
	int anim_fps_cfg = 0;
	int anim_loop_from_cfg = 0;
	char anim_raw_cfg[64] = "";
	int anim_x_cfg = 0, anim_y_cfg = 0, anim_w_cfg = 0, anim_h_cfg = 0;

	theme_grid_cell = 0;
	theme_grid_ox = 0;
	theme_grid_oy = 0;
	theme_grid_manual = 0;
	boot_raw_file[0] = '\0';
	boot_audio_file[0] = '\0';
	boot_frames = 0;
	boot_fps = 20;
	boot_w = 640;
	boot_h = 480;
	music_file[0] = '\0';
	music_volume = 100;

	sprintf(path, "%s/theme.json", dir);
	FILE* f = fopen(path, "r");
	if (!f) return;
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "\"name\"") && json_value(line, val, sizeof(val)))
			strncpy(active_name, val, sizeof(active_name) - 1);
		else if (strstr(line, "\"color_text\"") && json_value(line, val, sizeof(val)))
			text = parse_hex(val);
		else if (strstr(line, "\"color_text_dim\"") && json_value(line, val, sizeof(val)))
			text_dim = parse_hex(val);
		else if (strstr(line, "\"color_pill_selected\"") && json_value(line, val, sizeof(val)))
			pill_sel = parse_hex(val);
		else if (strstr(line, "\"color_pill_unselected\"") && json_value(line, val, sizeof(val)))
			pill_unsel = parse_hex(val);
		else if (strstr(line, "\"color_pill_menu\"") && json_value(line, val, sizeof(val)))
			pill_menu = parse_hex(val);
		else if (strstr(line, "\"color_button\"") && json_value(line, val, sizeof(val)))
			button = parse_hex(val);
		else if (strstr(line, "\"color_bar\"") && json_value(line, val, sizeof(val)))
			bar = parse_hex(val);
		else if (strstr(line, "\"color_bar_bg\"") && json_value(line, val, sizeof(val)))
			bar_bg = parse_hex(val);
		else if (strstr(line, "\"color_bar_bg_menu\"") && json_value(line, val, sizeof(val)))
			bar_menu = parse_hex(val);
		else if (strstr(line, "\"color_background\"") && json_value(line, val, sizeof(val)) && val[0])
			bg_solid = parse_hex(val);
		else if (strstr(line, "\"background\"") && json_value(line, val, sizeof(val)) && val[0])
			strncpy(bg_file, val, sizeof(bg_file) - 1);
		else if (strstr(line, "\"background_dim\"")) {
			char* colon = strchr(line, ':');
			if (colon) bg_dim = atoi(colon + 1);
		}
		else if (strstr(line, "\"font\"") && json_value(line, val, sizeof(val)) && val[0])
			strncpy(font_file, val, sizeof(font_file) - 1);
		else if (strstr(line, "\"pill_style\"") && json_value(line, val, sizeof(val)))
			strncpy(pill_style_name, val, sizeof(pill_style_name) - 1);
		else if (strstr(line, "\"grid_cell\"")) {
			char* colon = strchr(line, ':');
			if (colon) grid_cell_cfg = atoi(colon + 1);
		}
		else if (strstr(line, "\"grid_origin_x\"")) {
			char* colon = strchr(line, ':');
			if (colon) grid_ox_cfg = atoi(colon + 1);
		}
		else if (strstr(line, "\"grid_origin_y\"")) {
			char* colon = strchr(line, ':');
			if (colon) grid_oy_cfg = atoi(colon + 1);
		}
		else if (strstr(line, "\"anim_frames\"")) {
			char* colon = strchr(line, ':');
			if (colon) anim_frames_cfg = atoi(colon + 1);
		}
		else if (strstr(line, "\"anim_fps\"")) {
			char* colon = strchr(line, ':');
			if (colon) anim_fps_cfg = atoi(colon + 1);
		}
		else if (strstr(line, "\"anim_loop_from\"")) {
			char* colon = strchr(line, ':');
			if (colon) anim_loop_from_cfg = atoi(colon + 1);
		}
		else if (strstr(line, "\"anim_raw\"") && json_value(line, val, sizeof(val)) && val[0])
			strncpy(anim_raw_cfg, val, sizeof(anim_raw_cfg) - 1);
		else if (strstr(line, "\"anim_x\"")) {
			char* colon = strchr(line, ':');
			if (colon) anim_x_cfg = atoi(colon + 1);
		}
		else if (strstr(line, "\"anim_y\"")) {
			char* colon = strchr(line, ':');
			if (colon) anim_y_cfg = atoi(colon + 1);
		}
		else if (strstr(line, "\"anim_w\"")) {
			char* colon = strchr(line, ':');
			if (colon) anim_w_cfg = atoi(colon + 1);
		}
		else if (strstr(line, "\"anim_h\"")) {
			char* colon = strchr(line, ':');
			if (colon) anim_h_cfg = atoi(colon + 1);
		}
		else if (strstr(line, "\"anim_max_hz\"")) {
			char* colon = strchr(line, ':');
			if (colon) bg_max_hz = atoi(colon + 1);
		}
		else if (strstr(line, "\"boot_raw\"") && json_value(line, val, sizeof(val)) && val[0])
			snprintf(boot_raw_file, sizeof(boot_raw_file), "%s/%s", dir, val);
		else if (strstr(line, "\"music\"") && json_value(line, val, sizeof(val)) && val[0])
			snprintf(music_file, sizeof(music_file), "%s/%s", dir, val);
		else if (strstr(line, "\"music_volume\"")) {
			char* colon = strchr(line, ':');
			if (colon) music_volume = atoi(colon + 1);
		}
		else if (strstr(line, "\"boot_w\"")) {
			char* colon = strchr(line, ':');
			if (colon) boot_w = atoi(colon + 1);
		}
		else if (strstr(line, "\"boot_h\"")) {
			char* colon = strchr(line, ':');
			if (colon) boot_h = atoi(colon + 1);
		}
		else if (strstr(line, "\"boot_audio\"") && json_value(line, val, sizeof(val)) && val[0])
			snprintf(boot_audio_file, sizeof(boot_audio_file), "%s/%s", dir, val);
		else if (strstr(line, "\"boot_frames\"")) {
			char* colon = strchr(line, ':');
			if (colon) boot_frames = atoi(colon + 1);
		}
		else if (strstr(line, "\"boot_fps\"")) {
			char* colon = strchr(line, ':');
			if (colon) boot_fps = atoi(colon + 1);
		}
	}
	fclose(f);

	COLOR_WHITE = rgb32_to_color(text);
	COLOR_LIGHT_TEXT = rgb32_to_color(text_dim);
	COLOR_DARK_TEXT = rgb32_to_color(text_dim);
	COLOR_GRAY = rgb32_to_color(text_dim);
	COLOR_BLACK = rgb32_to_color(pill_menu);

	RGB_WHITE = SDL_MapRGB(fmt, text >> 16, (text >> 8) & 0xff, text & 0xff);
	RGB_GRAY = SDL_MapRGB(fmt, text_dim >> 16, (text_dim >> 8) & 0xff, text_dim & 0xff);
	RGB_LIGHT_GRAY = RGB_GRAY;
	RGB_BLACK = SDL_MapRGB(fmt, pill_menu >> 16, (pill_menu >> 8) & 0xff, pill_menu & 0xff);
	RGB_DARK_GRAY = SDL_MapRGB(fmt, pill_unsel >> 16, (pill_unsel >> 8) & 0xff, pill_unsel & 0xff);
	button = parse_hex("ffffff");
	GFX_applyThemeAssets(fmt, pill_sel, pill_unsel, pill_menu, pill_unsel, button, bar, bar_bg, bar_menu);
	set_on_pill(pill_sel);
	COLOR_ACCENT = rgb32_to_color(pill_sel);

	theme_light_footer = color_luma(pill_unsel) > 140;
	COLOR_FOOTER_TEXT = theme_light_footer
		? (SDL_Color){0x22, 0x22, 0x28, 0xff}
		: (SDL_Color){0xff, 0xff, 0xff, 0xff};
	if (theme_light_footer)
		COLOR_BUTTON_TEXT = (SDL_Color){0x11, 0x11, 0x11, 0xff};
	else
		COLOR_BUTTON_TEXT = rgb32_to_color(text);

	if (exactMatch(pill_style_name, "psx"))
		theme_pill_style = THEME_PILL_PSX;
	else if (exactMatch(pill_style_name, "text"))
		theme_pill_style = THEME_PILL_TEXT;
	else
		theme_pill_style = THEME_PILL_SOLID;

	if (theme_pill_style == THEME_PILL_PSX) {
		COLOR_FOOTER_TEXT = (SDL_Color){0x34, 0x38, 0x44, 0xff};
		COLOR_BUTTON_TEXT = (SDL_Color){0xff, 0xff, 0xff, 0xff};
	}

	theme_custom_visual = theme_pill_style != THEME_PILL_CLASSIC;
	theme_custom_font = 0;
	strcpy(font_path, THEME_DEFAULT_FONT);
	if (font_file[0]) {
		char fp[MAX_PATH];
		sprintf(fp, "%s/%s", dir, font_file);
		if (exists(fp)) {
			strncpy(font_path, fp, sizeof(font_path) - 1);
			theme_custom_font = 1;
		}
	}
	if (background) {
		SDL_FreeSurface(background);
		background = NULL;
	}
	free_bg_frames();
	if (bg_file[0]) {
		sprintf(path, "%s/%s", dir, bg_file);
		if (exists(path)) {
			SDL_Surface* img = IMG_Load(path);
			if (img) {
				background = SDL_ConvertSurface(img, fmt, 0);
				SDL_FreeSurface(img);
				if (background && bg_dim > 0)
					darken_surface(background, bg_dim);
			}
		}
	}
	if (!background && bg_solid) {
		background = SDL_CreateRGBSurface(SDL_SWSURFACE, FIXED_WIDTH, FIXED_HEIGHT,
			fmt->BitsPerPixel, fmt->Rmask, fmt->Gmask, fmt->Bmask, fmt->Amask);
		SDL_FillRect(background, NULL, SDL_MapRGB(fmt, bg_solid >> 16, (bg_solid >> 8) & 0xff, bg_solid & 0xff));
	}

	/* Fondo animado: archivo crudo anim_raw (RGB565) con anim_frames fotogramas
	   de anim_w x anim_h, dibujados en (anim_x, anim_y) sobre background. */
	if (background && anim_raw_cfg[0] && anim_frames_cfg > 1 && anim_fps_cfg > 0
		&& anim_w_cfg > 0 && anim_h_cfg > 0) {
		char fpath[MAX_PATH];
		sprintf(fpath, "%s/%s", dir, anim_raw_cfg);
		size_t fsize = (size_t)anim_w_cfg * anim_h_cfg * 2;
		bg_fp = fopen(fpath, "rb");
		if (bg_fp) {
			fseek(bg_fp, 0, SEEK_END);
			long total = ftell(bg_fp);
			bg_buf = (Uint16*)malloc(fsize);
			if (bg_buf && total >= (long)(fsize * anim_frames_cfg)) {
				bg_surf = SDL_CreateRGBSurfaceFrom(bg_buf, anim_w_cfg, anim_h_cfg, 16,
					anim_w_cfg * 2, 0xF800, 0x07E0, 0x001F, 0);
			}
			if (bg_surf) {
				bg_ax = anim_x_cfg; bg_ay = anim_y_cfg;
				bg_aw = anim_w_cfg; bg_ah = anim_h_cfg;
				bg_frame_count = anim_frames_cfg;
				bg_anim_fps = anim_fps_cfg;
				bg_anim_loop_from = (anim_loop_from_cfg > 0 && anim_loop_from_cfg < anim_frames_cfg) ? anim_loop_from_cfg : 0;
				bg_anim_start_ticks = SDL_GetTicks();
				bg_last_frame_idx = -1;
				bg_last_redraw = 0;
			} else {
				free_bg_frames();
			}
		}
	}

	/* Retícula: solo si grid_cell está en theme.json (sin autodetección). */
	if (theme_pill_style == THEME_PILL_PSX && background && grid_cell_cfg > 0) {
		theme_grid_cell = grid_cell_cfg;
		theme_grid_manual = 1;
		if (grid_ox_cfg >= 0)
			theme_grid_ox = grid_ox_cfg;
		if (grid_oy_cfg >= 0)
			theme_grid_oy = grid_oy_cfg;
		if (grid_ox_cfg < 0 || grid_oy_cfg < 0)
			theme_find_origins(background, theme_grid_cell);
	}
}

void THEME_init(SDL_Surface* screen) {
	char picked[64] = "";
	char dir[MAX_PATH];
	SDL_PixelFormat* fmt = screen->format;

	apply_default(fmt);
	if (background) {
		SDL_FreeSurface(background);
		background = NULL;
	}
	free_bg_frames();
	boot_raw_file[0] = '\0';
	boot_audio_file[0] = '\0';
	boot_frames = 0;
	music_file[0] = '\0';

	getFile(THEME_ACTIVE_PATH, picked, sizeof(picked));
	char* nl = strchr(picked, '\n');
	if (nl) *nl = '\0';
	nl = strchr(picked, '\r');
	if (nl) *nl = '\0';
	while (picked[0] == ' ' || picked[0] == '\t') memmove(picked, picked + 1, strlen(picked));

	if (picked[0] == '\0' || exactMatch(picked, "Clasico")) {
		strcpy(active_id, "Clasico");
		strcpy(active_name, "Clasico");
		return;
	}

	strncpy(active_id, picked, sizeof(active_id) - 1);
	sprintf(dir, "%s/%s", THEMES_PATH, picked);
	if (!exists(dir)) {
		strcpy(active_id, "Clasico");
		strcpy(active_name, "Clasico");
		apply_default(fmt);
		return;
	}
	load_theme_file(dir, fmt);

	if (GFX_getMode() == MODE_MENU) {
		char keep_id[sizeof(active_id)];
		char keep_name[sizeof(active_name)];
		strncpy(keep_id, active_id, sizeof(keep_id) - 1);
		keep_id[sizeof(keep_id) - 1] = '\0';
		strncpy(keep_name, active_name, sizeof(keep_name) - 1);
		keep_name[sizeof(keep_name) - 1] = '\0';
		if (background) {
			SDL_FreeSurface(background);
			background = NULL;
		}
		free_bg_frames();
		apply_default(fmt);
		strncpy(active_id, keep_id, sizeof(active_id) - 1);
		strncpy(active_name, keep_name, sizeof(active_name) - 1);
	}
}

/** Cuadro actual de la animación: 0..count-1 se reproduce una vez desde que
    se activó el tema (la "aparición"); después, si hay anim_loop_from, el
    ciclo eterno queda acotado a loop_from..count-1 en vez de repetir todo
    el clip desde el principio. */
static int bg_current_frame_index(void) {
	unsigned long elapsed_ms = SDL_GetTicks() - bg_anim_start_ticks;
	int elapsed_frames = (int)((unsigned long)elapsed_ms * (unsigned)bg_anim_fps / 1000);
	if (elapsed_frames < bg_frame_count)
		return elapsed_frames;
	if (bg_anim_loop_from <= 0)
		return elapsed_frames % bg_frame_count;
	int loop_len = bg_frame_count - bg_anim_loop_from;
	return bg_anim_loop_from + ((elapsed_frames - bg_frame_count) % loop_len);
}

/** Lee el fotograma idx del archivo crudo (solo si cambió) y devuelve su superficie. */
static SDL_Surface* bg_frame_surface(int idx) {
	if (!bg_surf || !bg_fp) return NULL;
	if (idx != bg_cur_idx) {
		size_t fsize = (size_t)bg_aw * bg_ah * 2;
		if (fseek(bg_fp, (long)((size_t)idx * fsize), SEEK_SET) == 0
			&& fread(bg_buf, 1, fsize, bg_fp) == fsize)
			bg_cur_idx = idx;
	}
	return bg_surf;
}

void THEME_blitBackground(SDL_Surface* screen) {
	if (!background || !screen) return;
	SDL_BlitSurface(background, NULL, screen, NULL);
	if (bg_surf && bg_frame_count > 1 && bg_anim_fps > 0) {
		SDL_Surface* frame = bg_frame_surface(bg_current_frame_index());
		if (frame) SDL_BlitSurface(frame, NULL, screen, &(SDL_Rect){bg_ax, bg_ay});
	}
}

/** true si el fondo animado avanzó de fotograma desde la última consulta;
    el bucle principal lo usa para forzar un redibujado aunque no haya
    entrada del usuario. No hace nada si el tema no tiene animación. */
int THEME_bgFrameChanged(void) {
	if (bg_frame_count <= 1 || bg_anim_fps <= 0) return 0;
	unsigned long now = SDL_GetTicks();
	if (bg_max_hz > 0 && now - bg_last_redraw < 1000UL / (unsigned)bg_max_hz) return 0;
	int idx = bg_current_frame_index();
	if (idx != bg_last_frame_idx) {
		bg_last_frame_idx = idx;
		bg_last_redraw = now;
		return 1;
	}
	return 0;
}

const char* THEME_fontPath(void) {
	return font_path[0] ? font_path : THEME_DEFAULT_FONT;
}

int THEME_customPills(void) { return theme_custom_visual; }
int THEME_usesCustomFont(void) { return theme_custom_font; }
int THEME_pillStyle(void) { return theme_pill_style; }

const char* THEME_activeId(void) { return active_id; }
const char* THEME_activeName(void) { return active_name; }

int THEME_hasBackdrop(void) { return background != NULL; }

void THEME_preview(const char* theme_id, SDL_PixelFormat* fmt) {
	char keep_id[sizeof(active_id)];
	char keep_name[sizeof(active_name)];
	strncpy(keep_id, active_id, sizeof(keep_id) - 1);
	keep_id[sizeof(keep_id) - 1] = '\0';
	strncpy(keep_name, active_name, sizeof(keep_name) - 1);
	keep_name[sizeof(keep_name) - 1] = '\0';

	if (background) {
		SDL_FreeSurface(background);
		background = NULL;
	}
	free_bg_frames();

	if (!theme_id || theme_id[0] == '\0' || exactMatch((char*)theme_id, "Clasico"))
		apply_default(fmt);
	else {
		char dir[MAX_PATH];
		sprintf(dir, "%s/%s", THEMES_PATH, theme_id);
		if (exists(dir))
			load_theme_file(dir, fmt);
		else
			apply_default(fmt);
	}

	strncpy(active_id, keep_id, sizeof(active_id) - 1);
	active_id[sizeof(active_id) - 1] = '\0';
	strncpy(active_name, keep_name, sizeof(active_name) - 1);
	active_name[sizeof(active_name) - 1] = '\0';
}

void GFX_clearThemed(SDL_Surface* screen) {
	GFX_clear(screen);
	if (GFX_getMode() == MODE_MAIN)
		THEME_blitBackground(screen);
}

/* ---- Animación de arranque de juego ---- */

typedef struct {
	Uint8* buf;
	Uint32 len;
	Uint32 pos;
} BootAudio;

static void boot_audio_cb(void* userdata, Uint8* stream, int len) {
	BootAudio* a = (BootAudio*)userdata;
	int n = (int)(a->len - a->pos);
	if (n > len) n = len;
	if (n > 0) {
		memcpy(stream, a->buf + a->pos, n);
		a->pos += n;
	}
	if (n < len) memset(stream + (n > 0 ? n : 0), 0, len - (n > 0 ? n : 0));
}

int THEME_hasBootAnim(void) {
	return boot_frames > 0 && boot_raw_file[0];
}

static pthread_t boot_thread;
static int boot_thread_on = 0;
static volatile int boot_abort = 0;
static SDL_Surface* boot_screen = NULL;

/* Hilo de la animación: hace de "cortinilla de carga" mientras el hilo
   principal carga el núcleo y el juego. Solo toca la pantalla y los botones;
   el hilo principal no debe hacerlo hasta THEME_finishBootAnim(). */
static void* boot_anim_main(void* arg) {
	(void)arg;
	SDL_Surface* screen = boot_screen;
	if (!THEME_hasBootAnim() || !screen) return NULL;

	FILE* fp = fopen(boot_raw_file, "rb");
	if (!fp) return NULL;
	size_t fsize = (size_t)boot_w * boot_h * 2;
	Uint16* buf = (Uint16*)malloc(fsize);
	SDL_Surface* frame = buf ? SDL_CreateRGBSurfaceFrom(buf, boot_w, boot_h, 16,
		boot_w * 2, 0xF800, 0x07E0, 0x001F, 0) : NULL;
	if (!frame) {
		if (buf) free(buf);
		fclose(fp);
		return NULL;
	}

	BootAudio audio = {0};
	int audio_open = 0;
	if (boot_audio_file[0] && exists(boot_audio_file)) {
		SDL_AudioSpec spec;
		if (SDL_LoadWAV(boot_audio_file, &spec, &audio.buf, &audio.len)) {
			SDL_InitSubSystem(SDL_INIT_AUDIO);
			spec.callback = boot_audio_cb;
			spec.userdata = &audio;
			if (SDL_OpenAudio(&spec, NULL) == 0) {
				audio_open = 1;
			} else {
				SDL_FreeWAV(audio.buf);
				audio.buf = NULL;
			}
		}
	}

	PAD_poll(); // descarta cualquier pulsación previa (el A con el que se abrió el juego)
	GFX_clear(screen);
	GFX_flip(screen);

	unsigned long start = SDL_GetTicks();
	if (audio_open) SDL_PauseAudio(0);

	int last = -1;
	SDL_Rect at = {(screen->w - boot_w) / 2, (screen->h - boot_h) / 2, 0, 0};
	int armed = 0; // solo salta con una pulsación nueva
	for (;;) {
		int idx = (int)((SDL_GetTicks() - start) * (unsigned)boot_fps / 1000);
		if (idx >= boot_frames || boot_abort) break;

		PAD_poll();
		if (!armed) {
			if (!PAD_anyPressed()) armed = 1;
		}
		else if (PAD_anyJustPressed()) break;

		if (idx != last) {
			if (fseek(fp, (long)((size_t)idx * fsize), SEEK_SET) == 0
				&& fread(buf, 1, fsize, fp) == fsize) {
				GFX_clear(screen);
				SDL_BlitSurface(frame, NULL, screen, &at);
				GFX_flip(screen);
			}
			last = idx;
		}
		else GFX_sync();
	}

	SDL_FreeSurface(frame);
	free(buf);
	fclose(fp);
	if (audio_open) {
		SDL_CloseAudio();
		SDL_FreeWAV(audio.buf);
	}
	// se deja el último fotograma en pantalla: sigue de cortinilla hasta que
	// el juego empiece a dibujar.
	return NULL;
}

/** Arranca la animación en segundo plano. Llamar antes de cargar el juego. */
int THEME_startBootAnim(SDL_Surface* screen) {
	if (!THEME_hasBootAnim() || !screen || boot_thread_on) return 0;
	boot_screen = screen;
	boot_abort = 0;
	if (pthread_create(&boot_thread, NULL, boot_anim_main, NULL) != 0) return 0;
	boot_thread_on = 1;
	return 1;
}

/** Espera a que la animación termine (o se salte con un botón). Con abort=1 la
    corta de inmediato. Llamar antes de SND_init() (usa el audio por su cuenta)
    y antes de dibujar nada más. Sin efecto si no hay animación en marcha. */
void THEME_finishBootAnim(int abort) {
	if (!boot_thread_on) return;
	if (abort) boot_abort = 1;
	pthread_join(boot_thread, NULL);
	boot_thread_on = 0;
}

/* ---- Música de fondo del menú ---- */

typedef struct {
	Uint8* buf;
	Uint32 len;
	Uint32 pos;
	Uint32 fade; // bytes ya reproducidos del fundido de entrada
	Uint32 fade_len;
	int volume; // 0..128
} MusicState;
static MusicState music = {0};
static int music_open = 0;

static void music_cb(void* userdata, Uint8* stream, int len) {
	MusicState* m = (MusicState*)userdata;
	int done = 0;
	while (done < len) {
		int n = (int)(m->len - m->pos);
		if (n > len - done) n = len - done;
		memcpy(stream + done, m->buf + m->pos, n);
		m->pos += n;
		if (m->pos >= m->len) m->pos = 0; // bucle
		done += n;
	}
	// volumen y fundido de entrada (evita el golpe al empezar en mitad del audio)
	if (m->volume < 128 || m->fade < m->fade_len) {
		Sint16* p = (Sint16*)stream;
		int count = len / 2;
		for (int i = 0; i < count; i++) {
			int g = m->volume;
			if (m->fade < m->fade_len) {
				g = g * (int)(((unsigned long long)m->fade * 128) / m->fade_len) / 128;
				if ((i & 1) == 1) m->fade += 4; // un fotograma estéreo = 4 bytes
			}
			p[i] = (Sint16)((p[i] * g) >> 7);
		}
	}
}

int THEME_hasMusic(void) { return music_file[0] != '\0'; }

/** Empieza la música de fondo del tema en bucle desde un punto al azar. Llamar
    después de GFX_init(). No hace nada si el tema no trae música o si existe
    USERDATA/sin-musica.txt (para silenciarla sin cambiar de tema). */
void THEME_musicStart(void) {
	if (music_open || !THEME_hasMusic() || !exists(music_file)) return;
	if (exists(USERDATA_PATH "/sin-musica.txt")) return;

	SDL_AudioSpec spec;
	Uint8* buf = NULL;
	Uint32 len = 0;
	if (!SDL_LoadWAV(music_file, &spec, &buf, &len)) return;
	if (spec.format != AUDIO_S16 || spec.channels != 2 || len < 4096) {
		SDL_FreeWAV(buf);
		return;
	}
	music.buf = buf;
	music.len = len & ~3u;
	music.pos = (Uint32)((SDL_GetTicks() * 7919u) % (music.len / 4)) * 4;
	music.fade = 0;
	music.fade_len = (Uint32)spec.freq * 4; // ~1 s
	int vol = music_volume < 0 ? 0 : (music_volume > 100 ? 100 : music_volume);
	music.volume = vol * 128 / 100;

	SDL_InitSubSystem(SDL_INIT_AUDIO);
	spec.callback = music_cb;
	spec.userdata = &music;
	spec.samples = 2048;
	if (SDL_OpenAudio(&spec, NULL) != 0) {
		SDL_FreeWAV(buf);
		music.buf = NULL;
		return;
	}
	music_open = 1;
	SetVolume(GetVolume()); // el volumen del sistema se aplica al abrir el dispositivo de audio
	SDL_PauseAudio(0);
}

void THEME_musicStop(void) {
	if (!music_open) return;
	SDL_CloseAudio();
	SDL_FreeWAV(music.buf);
	music.buf = NULL;
	music_open = 0;
}
