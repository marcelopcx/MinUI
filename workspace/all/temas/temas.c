#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "theme.h"

#define MAX_THEMES 32

typedef struct {
	char id[64];
	char name[96];
} ThemeEntry;

static int load_themes(ThemeEntry* out, int max) {
	int n = 0;
	strcpy(out[n].id, "Clasico");
	strcpy(out[n].name, "Clasico");
	n++;

	DIR* d = opendir(THEMES_PATH);
	if (!d) return n;
	struct dirent* e;
	while ((e = readdir(d)) && n < max) {
		if (e->d_name[0] == '.') continue;
		char path[MAX_PATH];
		sprintf(path, "%s/%s/theme.json", THEMES_PATH, e->d_name);
		if (!exists(path)) continue;
		strncpy(out[n].id, e->d_name, sizeof(out[n].id) - 1);
		out[n].name[0] = '\0';
		char line[256];
		char val[128];
		FILE* f = fopen(path, "r");
		if (f) {
			while (fgets(line, sizeof(line), f)) {
				if (strstr(line, "\"name\"")) {
					char* colon = strchr(line, ':');
					if (colon) {
						colon++;
						while (*colon == ' ' || *colon == '\t') colon++;
						if (*colon == '"') {
							colon++;
							char* end = strchr(colon, '"');
							if (end) {
								size_t len = (size_t)(end - colon);
								if (len >= sizeof(val)) len = sizeof(val) - 1;
								memcpy(val, colon, len);
								val[len] = '\0';
								strncpy(out[n].name, val, sizeof(out[n].name) - 1);
							}
						}
					}
					break;
				}
			}
			fclose(f);
		}
		if (out[n].name[0] == '\0')
			strncpy(out[n].name, e->d_name, sizeof(out[n].name) - 1);
		n++;
	}
	closedir(d);
	return n;
}

static void apply_theme(const char* id) {
	if (exactMatch((char*)id, "Clasico")) {
		if (exists(THEME_ACTIVE_PATH)) remove(THEME_ACTIVE_PATH);
		return;
	}
	mkdir(USERDATA_PATH, 0755);
	char buf[128];
	snprintf(buf, sizeof(buf), "%s\n", id);
	putFile(THEME_ACTIVE_PATH, buf);
}

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	PWR_setCPUSpeed(CPU_SPEED_MENU);
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();

	ThemeEntry themes[MAX_THEMES];
	int count = load_themes(themes, MAX_THEMES);
	int selected = 0;
	char active_buf[64];
	strncpy(active_buf, THEME_activeId(), sizeof(active_buf) - 1);
	for (int i = 0; i < count; i++) {
		if (exactMatch(themes[i].id, active_buf)) {
			selected = i;
			break;
		}
	}

	int quit = 0;
	int dirty = 1;
	int start = 0;
	int end = count;
	if (end > MAIN_ROW_COUNT) end = MAIN_ROW_COUNT;
	char preview_id[64] = "";

	while (!quit) {
		GFX_startFrame();
		PAD_poll();

		if (PAD_justPressed(BTN_B)) quit = 1;
		if (PAD_justPressed(BTN_A)) {
			apply_theme(themes[selected].id);
			quit = 1;
		}
		if (PAD_justPressed(BTN_UP)) {
			selected -= 1;
			if (selected < 0) selected = count - 1;
			if (selected < start) start = selected;
			if (selected >= end) { start = selected - MAIN_ROW_COUNT + 1; end = selected + 1; }
			dirty = 1;
		}
		if (PAD_justPressed(BTN_DOWN)) {
			selected += 1;
			if (selected >= count) selected = 0;
			if (selected >= end) { end = selected + 1; start = end - MAIN_ROW_COUNT; }
			if (selected < start) { start = selected; end = start + MAIN_ROW_COUNT; }
			dirty = 1;
		}

		PWR_update(&dirty, NULL, NULL, NULL);

		if (dirty) {
			if (!exactMatch(preview_id, themes[selected].id)) {
				THEME_preview(themes[selected].id, screen->format);
				GFX_reloadFonts();
				strncpy(preview_id, themes[selected].id, sizeof(preview_id) - 1);
				preview_id[sizeof(preview_id) - 1] = '\0';
			}

			GFX_clearThemed(screen);

			int ow = GFX_blitHardwareGroup(screen, 0);
			int list_kind = THEME_listRowKind();
			int uniform = list_kind != THEME_LIST_NONE;
			int list_x = uniform ? SCALE1(PADDING) : THEME_listX();
			int row_h = uniform ? SCALE1(PILL_SIZE) : THEME_listRowH();
			int list_w = screen->w - list_x - SCALE1(PADDING);
			if (ow > 0)
				list_w -= ow + SCALE1(PADDING);

			if (count > 0) {
				int selected_row = selected - start;
				int uniform_row_w = 0;
				int pad2 = SCALE1(BUTTON_PADDING * 2);
				if (uniform || THEME_pillStyle() == THEME_PILL_PSX) {
					for (int i = 0; i < count; i++) {
						int w = GFX_rowLabelWidth(font.large, themes[i].name, pad2) + 4;
						if (w > uniform_row_w)
							uniform_row_w = w;
					}
					if (uniform_row_w > list_w)
						uniform_row_w = list_w;
					if (THEME_pillStyle() == THEME_PILL_PSX)
						uniform_row_w = THEME_alignWidth(uniform_row_w);
				}
				for (int i = start, j = 0; i < end && i < count; i++, j++) {
					char display[96];
					GFX_truncateText(font.large, themes[i].name, display, list_w, SCALE1(BUTTON_PADDING * 2));
					int text_w = 0;
					TTF_SizeUTF8(font.large, display, &text_w, NULL);
					int row_y = uniform ? SCALE1(PADDING + j * PILL_SIZE) : THEME_listY(j);
					int max_w = (uniform || THEME_pillStyle() == THEME_PILL_PSX)
						? uniform_row_w
						: MIN(list_w, text_w + SCALE1(BUTTON_PADDING * 2));

					SDL_Color text_color = COLOR_WHITE;
					if (list_kind == THEME_LIST_PLAYSTATION) {
						GFX_blitPlayStationListRow(screen, &(SDL_Rect){
							list_x, row_y, max_w, row_h
						}, j == selected_row);
						text_color = COLOR_WHITE;
					}
					else if (list_kind == THEME_LIST_DS) {
						GFX_blitDsListRow(screen, &(SDL_Rect){
							list_x, row_y, max_w, row_h
						}, j == selected_row);
						text_color = COLOR_DARK_TEXT;
					} else if (THEME_pillStyle() == THEME_PILL_PSX) {
						GFX_blitRowPill(screen, &(SDL_Rect){
							list_x, row_y, max_w, row_h
						}, j == selected_row);
						text_color = COLOR_WHITE;
					} else if (THEME_pillStyle() == THEME_PILL_TEXT) {
						// sin pastilla: el seleccionado solo se ilumina (como en minui)
						text_color = (j == selected_row) ? COLOR_ACCENT : COLOR_DARK_TEXT;
					} else if (THEME_hasBackdrop()) {
						GFX_blitRowPill(screen, &(SDL_Rect){
							SCALE1(PADDING), SCALE1(PADDING + j * PILL_SIZE), max_w, SCALE1(PILL_SIZE)
						}, j == selected_row);
						text_color = COLOR_WHITE;
					} else if (j == selected_row) {
						GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){
							SCALE1(PADDING), SCALE1(PADDING + j * PILL_SIZE), max_w, SCALE1(PILL_SIZE)});
						text_color = COLOR_ON_PILL;
					} else {
						text_color = COLOR_LIGHT_TEXT;
					}
					SDL_Rect text_at = {
						list_x + SCALE1(BUTTON_PADDING),
						row_y + SCALE1(4),
						0,
						0
					};
					if (list_kind == THEME_LIST_PLAYSTATION || THEME_pillStyle() == THEME_PILL_PSX) {
						GFX_blitPsxMenuTextInButton(font.large, themes[i].name, screen,
							&(SDL_Rect){list_x, row_y, max_w, row_h}, j == selected_row);
					}
					else if (list_kind == THEME_LIST_DS) {
						GFX_blitDsMenuTextInButton(font.large, themes[i].name, screen,
							&(SDL_Rect){list_x, row_y, max_w, row_h}, j == selected_row);
					}
					else {
						SDL_Surface* text = TTF_RenderUTF8_Blended(font.large, display, text_color);
						if (text) {
							SDL_BlitSurface(text, NULL, screen, &text_at);
							SDL_FreeSurface(text);
						}
					}
				}
			}

			GFX_blitButtonGroup((char*[]){ "B","ATRAS", "A","APLICAR", NULL }, 1, screen, 1);
			GFX_flip(screen);
			dirty = 0;
		} else {
			// fondo animado (PS2): redibuja cuando cambia el fotograma
			if (THEME_bgFrameChanged()) dirty = 1;
			GFX_sync();
		}
	}

	GFX_quit();
	PAD_quit();
	PWR_quit();
	return 0;
}
