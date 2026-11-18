#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "theme.h"

///////////////////////////////

void LOG_note(int level, const char* fmt, ...) {
	char buf[1024] = {0};
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	switch(level) {
#ifdef DEBUG
	case LOG_DEBUG:
		printf("[DEBUG] %s", buf);
		break;
#endif
	case LOG_INFO:
		printf("[INFO] %s", buf);
		break;
	case LOG_WARN:
		fprintf(stderr, "[WARN] %s", buf);
		break;
	case LOG_ERROR:
		fprintf(stderr, "[ERROR] %s", buf);
		break;
	default:
		break;
	}
	fflush(stdout);
}

///////////////////////////////

uint32_t RGB_WHITE;
uint32_t RGB_BLACK;
uint32_t RGB_LIGHT_GRAY;
uint32_t RGB_GRAY;
uint32_t RGB_DARK_GRAY;

static struct GFX_Context {
	SDL_Surface* screen;
	SDL_Surface* assets;

	int mode;
	int vsync;
} gfx;

static SDL_Rect asset_rects[ASSET_COUNT];
static uint32_t asset_rgbs[ASSET_COLORS];
GFX_Fonts font;

///////////////////////////////

static struct PWR_Context {
	int initialized;
	
	int can_sleep;
	int can_poweroff;
	int can_autosleep;
	int requested_sleep;
	int requested_wake;
	
	pthread_t battery_pt;
	int is_charging;
	int charge;
	int should_warn;

	SDL_Surface* overlay;
} pwr = {0};

///////////////////////////////

static int _;

SDL_Surface* GFX_init(int mode) {
	// TODO: this doesn't really belong here...
	// tried adding to PWR_init() but that was no good (not sure why)
	PLAT_initLid();
	
	gfx.screen = PLAT_initVideo();
	gfx.vsync = VSYNC_STRICT;
	gfx.mode = mode;
	
	asset_rects[ASSET_WHITE_PILL]		= (SDL_Rect){SCALE4( 1, 1,30,30)};
	asset_rects[ASSET_BLACK_PILL]		= (SDL_Rect){SCALE4(33, 1,30,30)};
	asset_rects[ASSET_DARK_GRAY_PILL]	= (SDL_Rect){SCALE4(65, 1,30,30)};
	asset_rects[ASSET_OPTION]			= (SDL_Rect){SCALE4(97, 1,20,20)};
	asset_rects[ASSET_BUTTON]			= (SDL_Rect){SCALE4( 1,33,20,20)};
	asset_rects[ASSET_PAGE_BG]			= (SDL_Rect){SCALE4(64,33,15,15)};
	asset_rects[ASSET_STATE_BG]			= (SDL_Rect){SCALE4(23,54, 8, 8)};
	asset_rects[ASSET_PAGE]				= (SDL_Rect){SCALE4(39,54, 6, 6)};
	asset_rects[ASSET_BAR]				= (SDL_Rect){SCALE4(33,58, 4, 4)};
	asset_rects[ASSET_BAR_BG]			= (SDL_Rect){SCALE4(15,55, 4, 4)};
	asset_rects[ASSET_BAR_BG_MENU]		= (SDL_Rect){SCALE4(85,56, 4, 4)};
	asset_rects[ASSET_UNDERLINE]		= (SDL_Rect){SCALE4(85,51, 3, 3)};
	asset_rects[ASSET_DOT]				= (SDL_Rect){SCALE4(33,54, 2, 2)};
	asset_rects[ASSET_BRIGHTNESS]		= (SDL_Rect){SCALE4(23,33,19,19)};
	asset_rects[ASSET_VOLUME_MUTE]		= (SDL_Rect){SCALE4(44,33,10,16)};
	asset_rects[ASSET_VOLUME]			= (SDL_Rect){SCALE4(44,33,18,16)};
	asset_rects[ASSET_BATTERY]			= (SDL_Rect){SCALE4(47,51,17,10)};
	asset_rects[ASSET_BATTERY_LOW]		= (SDL_Rect){SCALE4(66,51,17,10)};
	asset_rects[ASSET_BATTERY_FILL]		= (SDL_Rect){SCALE4(81,33,12, 6)};
	asset_rects[ASSET_BATTERY_FILL_LOW]	= (SDL_Rect){SCALE4( 1,55,12, 6)};
	asset_rects[ASSET_BATTERY_BOLT]		= (SDL_Rect){SCALE4(81,41,12, 6)};
	asset_rects[ASSET_SCROLL_UP]		= (SDL_Rect){SCALE4(97,23,24, 6)};
	asset_rects[ASSET_SCROLL_DOWN]		= (SDL_Rect){SCALE4(97,31,24, 6)};
	asset_rects[ASSET_WIFI]				= (SDL_Rect){SCALE4(95,39,14,10)};
	asset_rects[ASSET_HOLE]				= (SDL_Rect){SCALE4( 1,63,20,20)};
	
	char asset_path[MAX_PATH];
	sprintf(asset_path, RES_PATH "/assets@%ix.png", FIXED_SCALE);
	if (!exists(asset_path)) LOG_info("missing assets, you're about to segfault dummy!\n");
	gfx.assets = IMG_Load(asset_path);
	
	TTF_Init();
	THEME_init(gfx.screen);
	GFX_reloadFonts();
	
	return gfx.screen;
}

void GFX_reloadFonts(void) {
	const char* path = THEME_fontPath();
	int bold = !THEME_usesCustomFont();

	if (font.large) TTF_CloseFont(font.large);
	if (font.medium) TTF_CloseFont(font.medium);
	if (font.small) TTF_CloseFont(font.small);
	if (font.tiny) TTF_CloseFont(font.tiny);

	font.large = TTF_OpenFont(path, SCALE1(FONT_LARGE));
	font.medium = TTF_OpenFont(path, SCALE1(FONT_MEDIUM));
	font.small = TTF_OpenFont(path, SCALE1(FONT_SMALL));
	font.tiny = TTF_OpenFont(path, SCALE1(FONT_TINY));

	if (!font.large) font.large = TTF_OpenFont(THEME_DEFAULT_FONT, SCALE1(FONT_LARGE));
	if (!font.medium) font.medium = TTF_OpenFont(THEME_DEFAULT_FONT, SCALE1(FONT_MEDIUM));
	if (!font.small) font.small = TTF_OpenFont(THEME_DEFAULT_FONT, SCALE1(FONT_SMALL));
	if (!font.tiny) font.tiny = TTF_OpenFont(THEME_DEFAULT_FONT, SCALE1(FONT_TINY));

	TTF_SetFontStyle(font.large, bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL);
	TTF_SetFontStyle(font.medium, bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL);
	TTF_SetFontStyle(font.small, bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL);
	TTF_SetFontStyle(font.tiny, bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL);
}
void GFX_quit(void) {
	TTF_CloseFont(font.large);
	TTF_CloseFont(font.medium);
	TTF_CloseFont(font.small);
	TTF_CloseFont(font.tiny);
	
	SDL_FreeSurface(gfx.assets);
	
	GFX_freeAAScaler();
	
	GFX_clearAll();

	PLAT_quitVideo();
}

void GFX_setMode(int mode) {
	gfx.mode = mode;
}
int GFX_getMode(void) {
	return gfx.mode;
}
int GFX_getVsync(void) {
	return gfx.vsync;
}
void GFX_setVsync(int vsync) {
	PLAT_setVsync(vsync);
	gfx.vsync = vsync;
}

void GFX_applyThemeAssets(SDL_PixelFormat* fmt, uint32_t pill_sel, uint32_t pill_unsel,
	uint32_t pill_menu, uint32_t option, uint32_t button, uint32_t bar,
	uint32_t bar_bg, uint32_t bar_menu) {
	asset_rgbs[ASSET_WHITE_PILL] = SDL_MapRGB(fmt, pill_sel >> 16, (pill_sel >> 8) & 0xff, pill_sel & 0xff);
	asset_rgbs[ASSET_BLACK_PILL] = SDL_MapRGB(fmt, pill_menu >> 16, (pill_menu >> 8) & 0xff, pill_menu & 0xff);
	asset_rgbs[ASSET_DARK_GRAY_PILL] = SDL_MapRGB(fmt, pill_unsel >> 16, (pill_unsel >> 8) & 0xff, pill_unsel & 0xff);
	asset_rgbs[ASSET_OPTION] = SDL_MapRGB(fmt, option >> 16, (option >> 8) & 0xff, option & 0xff);
	asset_rgbs[ASSET_BUTTON] = SDL_MapRGB(fmt, button >> 16, (button >> 8) & 0xff, button & 0xff);
	asset_rgbs[ASSET_PAGE_BG] = asset_rgbs[ASSET_BUTTON];
	asset_rgbs[ASSET_STATE_BG] = asset_rgbs[ASSET_BUTTON];
	asset_rgbs[ASSET_PAGE] = SDL_MapRGB(fmt, 0, 0, 0);
	asset_rgbs[ASSET_BAR] = SDL_MapRGB(fmt, bar >> 16, (bar >> 8) & 0xff, bar & 0xff);
	asset_rgbs[ASSET_BAR_BG] = SDL_MapRGB(fmt, bar_bg >> 16, (bar_bg >> 8) & 0xff, bar_bg & 0xff);
	asset_rgbs[ASSET_BAR_BG_MENU] = SDL_MapRGB(fmt, bar_menu >> 16, (bar_menu >> 8) & 0xff, bar_menu & 0xff);
	asset_rgbs[ASSET_UNDERLINE] = RGB_GRAY;
	asset_rgbs[ASSET_DOT] = RGB_LIGHT_GRAY;
	asset_rgbs[ASSET_HOLE] = RGB_BLACK;
}

int GFX_hdmiChanged(void) {
	static int had_hdmi = -1;
	int has_hdmi = GetHDMI();
	if (had_hdmi==-1) had_hdmi = has_hdmi;
	if (had_hdmi==has_hdmi) return 0;
	had_hdmi = has_hdmi;
	return 1;
}

#define FRAME_BUDGET 17 // 60fps
static uint32_t frame_start = 0;
void GFX_startFrame(void) {
	frame_start = SDL_GetTicks();
}

void GFX_flip(SDL_Surface* screen) {
	int should_vsync = (gfx.vsync!=VSYNC_OFF && (gfx.vsync==VSYNC_STRICT || frame_start==0 || SDL_GetTicks()-frame_start<FRAME_BUDGET));
	PLAT_flip(screen, should_vsync);
}
void GFX_sync(void) {
	uint32_t frame_duration = SDL_GetTicks() - frame_start;
	if (gfx.vsync!=VSYNC_OFF) {
		// this limiting condition helps SuperFX chip games
		if (gfx.vsync==VSYNC_STRICT || frame_start==0 || frame_duration<FRAME_BUDGET) { // only wait if we're under frame budget
			PLAT_vsync(FRAME_BUDGET-frame_duration);
		}
	}
	else {
		if (frame_duration<FRAME_BUDGET) SDL_Delay(FRAME_BUDGET-frame_duration);
	}
}

FALLBACK_IMPLEMENTATION int PLAT_supportsOverscan(void) { return 0; }
FALLBACK_IMPLEMENTATION void PLAT_setEffectColor(int next_color) { }

int GFX_truncateText(TTF_Font* font, const char* in_name, char* out_name, int max_width, int padding) {
	int text_width;
	strcpy(out_name, in_name);
	TTF_SizeUTF8(font, out_name, &text_width, NULL);
	text_width += padding;
	
	while (text_width>max_width) {
		int len = strlen(out_name);
		strcpy(&out_name[len-4], "...\0");
		TTF_SizeUTF8(font, out_name, &text_width, NULL);
		text_width += padding;
	}
	
	return text_width;
}

int GFX_rowLabelWidth(TTF_Font* font, const char* label, int pad_x) {
	int tw = 0;
	if (!label || !label[0])
		return pad_x;
	TTF_SizeUTF8(font, label, &tw, NULL);
	return tw + pad_x;
}

int GFX_wrapText(TTF_Font* font, char* str, int max_width, int max_lines) {
	if (!str) return 0;
	
	int line_width;
	int max_line_width = 0;
	char* line = str;
	char buffer[MAX_PATH];
	
	TTF_SizeUTF8(font, line, &line_width, NULL);
	if (line_width<=max_width) {
		line_width = GFX_truncateText(font,line,buffer,max_width,0);
		strcpy(line,buffer);
		return line_width;
	}
	
	char* prev = NULL;
	char* tmp = line;
	int lines = 1;
	int i = 0;
	while (!max_lines || lines<max_lines) {
		tmp = strchr(tmp, ' ');
		if (!tmp) {
			if (prev) {
				TTF_SizeUTF8(font, line, &line_width, NULL);
				if (line_width>=max_width) {
					if (line_width>max_line_width) max_line_width = line_width;
					prev[0] = '\n';
					line = prev + 1;
				}
			}
			break;
		}
		tmp[0] = '\0';
		
		TTF_SizeUTF8(font, line, &line_width, NULL);

		if (line_width>=max_width) { // wrap
			if (line_width>max_line_width) max_line_width = line_width;
			tmp[0] = ' ';
			tmp += 1;
			prev[0] = '\n';
			prev += 1;
			line = prev;
			lines += 1;
		}
		else { // continue
			tmp[0] = ' ';
			prev = tmp;
			tmp += 1;
		}
		i += 1;
	}
	
	line_width = GFX_truncateText(font,line,buffer,max_width,0);
	strcpy(line,buffer);
	
	if (line_width>max_line_width) max_line_width = line_width;
	return max_line_width;
}

///////////////////////////////

// scale_blend (and supporting logic) from picoarch

struct blend_args {
	int w_ratio_in;
	int w_ratio_out;
	uint16_t w_bp[2];
	int h_ratio_in;
	int h_ratio_out;
	uint16_t h_bp[2];
	uint16_t *blend_line;
} blend_args;

#if __ARM_ARCH >= 5
static inline uint32_t average16(uint32_t c1, uint32_t c2) {
	uint32_t ret, lowbits = 0x0821;
	asm ("eor %0, %2, %3\r\n"
	     "and %0, %0, %1\r\n"
	     "add %0, %3, %0\r\n"
	     "add %0, %0, %2\r\n"
	     "lsr %0, %0, #1\r\n"
	     : "=&r" (ret) : "r" (lowbits), "r" (c1), "r" (c2) : );
	return ret;
}
static inline uint32_t average32(uint32_t c1, uint32_t c2) {
	uint32_t ret, lowbits = 0x08210821;

	asm ("eor %0, %3, %1\r\n"
	     "and %0, %0, %2\r\n"
	     "adds %0, %1, %0\r\n"
	     "and %1, %1, #0\r\n"
	     "movcs %1, #0x80000000\r\n"
	     "adds %0, %0, %3\r\n"
	     "rrx %0, %0\r\n"
	     "orr %0, %0, %1\r\n"
	     : "=&r" (ret), "+r" (c2) : "r" (lowbits), "r" (c1) : "cc" );

	return ret;
}

#define AVERAGE16_NOCHK(c1, c2) (average16((c1), (c2)))
#define AVERAGE32_NOCHK(c1, c2) (average32((c1), (c2)))

#else

static inline uint32_t average16(uint32_t c1, uint32_t c2) {
	return (c1 + c2 + ((c1 ^ c2) & 0x0821))>>1;
}
static inline uint32_t average32(uint32_t c1, uint32_t c2) {
	uint32_t sum = c1 + c2;
	uint32_t ret = sum + ((c1 ^ c2) & 0x08210821);
	uint32_t of = ((sum < c1) | (ret < sum)) << 31;

	return (ret >> 1) | of;
}

#define AVERAGE16_NOCHK(c1, c2) (average16((c1), (c2)))
#define AVERAGE32_NOCHK(c1, c2) (average32((c1), (c2)))

#endif

#define AVERAGE16(c1, c2) ((c1) == (c2) ? (c1) : AVERAGE16_NOCHK((c1), (c2)))
#define AVERAGE16_1_3(c1, c2) ((c1) == (c2) ? (c1) : (AVERAGE16_NOCHK(AVERAGE16_NOCHK((c1), (c2)), (c2))))

#define AVERAGE32(c1, c2) ((c1) == (c2) ? (c1) : AVERAGE32_NOCHK((c1), (c2)))
#define AVERAGE32_1_3(c1, c2) ((c1) == (c2) ? (c1) : (AVERAGE32_NOCHK(AVERAGE32_NOCHK((c1), (c2)), (c2))))

static inline int gcd(int a, int b) {
	return b ? gcd(b, a % b) : a;
}

static void scaleAA(void* __restrict src, void* __restrict dst, uint32_t w, uint32_t h, uint32_t pitch, uint32_t dst_w, uint32_t dst_h, uint32_t dst_p) {
	int dy = 0;
	int lines = h;

	int rat_w = blend_args.w_ratio_in;
	int rat_dst_w = blend_args.w_ratio_out;
	uint16_t *bw = blend_args.w_bp;

	int rat_h = blend_args.h_ratio_in;
	int rat_dst_h = blend_args.h_ratio_out;
	uint16_t *bh = blend_args.h_bp;

	while (lines--) {
		while (dy < rat_dst_h) {
			uint16_t *dst16 = (uint16_t *)dst;
			uint16_t *pblend = (uint16_t *)blend_args.blend_line;
			int col = w;
			int dx = 0;

			uint16_t *pnext = (uint16_t *)(src + pitch);
			if (!lines)
				pnext -= (pitch / sizeof(uint16_t));

			if (dy > rat_dst_h - bh[0]) {
				pblend = pnext;
			} else if (dy <= bh[0]) {
				/* Drops const, won't get touched later though */
				pblend = (uint16_t *)src;
			} else {
				const uint32_t *src32 = (const uint32_t *)src;
				const uint32_t *pnext32 = (const uint32_t *)pnext;
				uint32_t *pblend32 = (uint32_t *)pblend;
				int count = w / 2;

				if (dy <= bh[1]) {
					const uint32_t *tmp = pnext32;
					pnext32 = src32;
					src32 = tmp;
				}

				if (dy > rat_dst_h - bh[1] || dy <= bh[1]) {
					while(count--) {
						*pblend32++ = AVERAGE32_1_3(*src32, *pnext32);
						src32++;
						pnext32++;
					}
				} else {
					while(count--) {
						*pblend32++ = AVERAGE32(*src32, *pnext32);
						src32++;
						pnext32++;
					}
				}
			}

			while (col--) {
				uint16_t a, b, out;

				a = *pblend;
				b = *(pblend+1);

				while (dx < rat_dst_w) {
					if (a == b) {
						out = a;
					} else if (dx > rat_dst_w - bw[0]) { // top quintile, bbbb
						out = b;
					} else if (dx <= bw[0]) { // last quintile, aaaa
						out = a;
					} else {
						if (dx > rat_dst_w - bw[1]) { // 2nd quintile, abbb
							a = AVERAGE16_NOCHK(a,b);
						} else if (dx <= bw[1]) { // 4th quintile, aaab
							b = AVERAGE16_NOCHK(a,b);
						}

						out = AVERAGE16_NOCHK(a, b); // also 3rd quintile, aabb
					}
					*dst16++ = out;
					dx += rat_w;
				}

				dx -= rat_dst_w;
				pblend++;
			}

			dy += rat_h;
			dst += dst_p;
		}

		dy -= rat_dst_h;
		src += pitch;
	}
}

scaler_t GFX_getAAScaler(GFX_Renderer* renderer) {
	int gcd_w, div_w, gcd_h, div_h;
	blend_args.blend_line = calloc(renderer->src_w, sizeof(uint16_t));

	gcd_w = gcd(renderer->src_w, renderer->dst_w);
	blend_args.w_ratio_in = renderer->src_w / gcd_w;
	blend_args.w_ratio_out = renderer->dst_w / gcd_w;
	
	double blend_denominator = (renderer->src_w>renderer->dst_w) ? 5 : 2.5; // TODO: these values are really only good for the nano...
	// blend_denominator = 5.0; // better for trimui
	// LOG_info("blend_denominator: %f (%i && %i)\n", blend_denominator, HAS_SKINNY_SCREEN, renderer->dst_w>renderer->src_w);
	
	div_w = round(blend_args.w_ratio_out / blend_denominator);
	blend_args.w_bp[0] = div_w;
	blend_args.w_bp[1] = blend_args.w_ratio_out >> 1;

	gcd_h = gcd(renderer->src_h, renderer->dst_h);
	blend_args.h_ratio_in = renderer->src_h / gcd_h;
	blend_args.h_ratio_out = renderer->dst_h / gcd_h;

	div_h = round(blend_args.h_ratio_out / blend_denominator);
	blend_args.h_bp[0] = div_h;
	blend_args.h_bp[1] = blend_args.h_ratio_out >> 1;
	
	return scaleAA;
}
void GFX_freeAAScaler(void) {
	if (blend_args.blend_line != NULL) {
		free(blend_args.blend_line);
		blend_args.blend_line = NULL;
	}
}

///////////////////////////////

void GFX_blitAsset(int asset, SDL_Rect* src_rect, SDL_Surface* dst, SDL_Rect* dst_rect) {
	SDL_Rect* rect = &asset_rects[asset];
	SDL_Rect adj_rect = {
		.x = rect->x,
		.y = rect->y,
		.w = rect->w,
		.h = rect->h,
	};
	if (src_rect) {
		adj_rect.x += src_rect->x;
		adj_rect.y += src_rect->y;
		adj_rect.w  = src_rect->w;
		adj_rect.h  = src_rect->h;
	}
	SDL_BlitSurface(gfx.assets, &adj_rect, dst, dst_rect);
}
static Uint32 map_px(SDL_Surface* dst, uint8_t r, uint8_t g, uint8_t b) {
	return SDL_MapRGB(dst->format, r, g, b);
}

/* Lista COPY/DELETE: degradado suave con colores del tema (fondo/rejilla). */
static void GFX_blitPsxMenuMetal(SDL_Surface* dst, SDL_Rect* in) {
	Uint8 r0, g0, b0, r1, g1, b1, rh, gh, bh, rd, gd, bd;
	SDL_GetRGB(asset_rgbs[ASSET_BAR_BG], dst->format, &r0, &g0, &b0);
	SDL_GetRGB(asset_rgbs[ASSET_BAR_BG_MENU], dst->format, &r1, &g1, &b1);
	r0 = (uint8_t)MIN(255, r0 + 18);
	g0 = (uint8_t)MIN(255, g0 + 18);
	b0 = (uint8_t)MIN(255, b0 + 14);
	r1 = (uint8_t)MAX(0, r1 - 8);
	g1 = (uint8_t)MAX(0, g1 - 8);
	b1 = (uint8_t)MAX(0, b1 - 6);
	rh = (uint8_t)MIN(255, r0 + 28);
	gh = (uint8_t)MIN(255, g0 + 28);
	bh = (uint8_t)MIN(255, b0 + 24);
	/* Borde/tapa de los botones: cromo azul-marino oscuro, igual a la sombra
	   del texto, como en los botones redondeados del menú de tarjeta de
	   memoria real (no el gris claro del pill sin seleccionar). */
	rd = 0x28; gd = 0x28; bd = 0x38;
	int y0 = in->y;
	int border = SCALE1(2);
	if (border * 2 >= in->w) border = in->w / 4;
	if (border < 1) border = 1;

	for (int y = 0; y < in->h; y++) {
		int t = in->h > 1 ? (y * 255) / (in->h - 1) : 0;
		uint8_t r = (uint8_t)(r0 + (r1 - r0) * t / 255);
		uint8_t g = (uint8_t)(g0 + (g1 - g0) * t / 255);
		uint8_t b = (uint8_t)(b0 + (b1 - b0) * t / 255);
		SDL_FillRect(dst, &(SDL_Rect){in->x, y0 + y, in->w, 1}, map_px(dst, r, g, b));
	}
	SDL_FillRect(dst, &(SDL_Rect){in->x, in->y, in->w, 1}, map_px(dst, rh, gh, bh));
	SDL_FillRect(dst, &(SDL_Rect){in->x, in->y, border, in->h}, map_px(dst, rd, gd, bd));
	SDL_FillRect(dst, &(SDL_Rect){in->x + in->w - border, in->y, border, in->h}, map_px(dst, rd, gd, bd));
	SDL_FillRect(dst, &(SDL_Rect){in->x, in->y + in->h - 1, in->w, 1}, map_px(dst, rd, gd, bd));
}

static void GFX_blitPsxMenuButton(SDL_Surface* dst, SDL_Rect* box, int selected) {
	Uint32 yellow = asset_rgbs[ASSET_WHITE_PILL];
	int border = selected ? 2 : 0;

	if (selected)
		SDL_FillRect(dst, box, yellow);
	if (box->w <= border * 2 || box->h <= border * 2) {
		if (!selected && box->w > 0 && box->h > 0)
			GFX_blitPsxMenuMetal(dst, box);
		return;
	}

	SDL_Rect in = {box->x + border, box->y + border, box->w - border * 2, box->h - border * 2};
	GFX_blitPsxMenuMetal(dst, &in);
}

/* Cromo del menú CD / barra superior (piedra gris clara, sin amarillo). */
static void GFX_blitPsxChromeBar(SDL_Surface* dst, SDL_Rect* box) {
	Uint32 metal_hi = map_px(dst, 0xd4, 0xd8, 0xe0);
	Uint32 metal_lo = map_px(dst, 0x90, 0x94, 0xa4);
	Uint32 shade = map_px(dst, 0x50, 0x54, 0x60);
	Uint32 hilite = map_px(dst, 0xf4, 0xf6, 0xfc);
	int half = box->h / 2;

	SDL_FillRect(dst, &(SDL_Rect){box->x, box->y, box->w, half}, metal_hi);
	SDL_FillRect(dst, &(SDL_Rect){box->x, box->y + half, box->w, box->h - half}, metal_lo);
	SDL_FillRect(dst, &(SDL_Rect){box->x, box->y, box->w, 1}, shade);
	SDL_FillRect(dst, &(SDL_Rect){box->x, box->y, 1, box->h}, shade);
	SDL_FillRect(dst, &(SDL_Rect){box->x, box->y + box->h - 1, box->w, 1}, hilite);
	SDL_FillRect(dst, &(SDL_Rect){box->x + box->w - 1, box->y, 1, box->h}, hilite);
}

/* Pastilla plana de un solo color, como "MEMORY CARD" / "CD PLAYER" del menú
   principal real de PS1: relleno sólido + texto blanco con sombra negra,
   sin degradado metálico. Se usa para elementos de estado/atajos, no listas. */
static void GFX_blitPsxFlatLabel(SDL_Surface* dst, SDL_Rect* box, Uint32 fill_px, TTF_Font* f, const char* label) {
	SDL_FillRect(dst, box, fill_px);
	if (!label || !label[0] || !f) return;
	SDL_Color white = {0xff, 0xff, 0xff, 0xff};
	SDL_Color shadow = {0x10, 0x10, 0x10, 0xff};
	int max_w = box->w - SCALE1(BUTTON_PADDING);
	if (max_w < SCALE1(8)) max_w = box->w;
	char fit[128];
	GFX_truncateText(f, label, fit, max_w, 0);
	int tw = 0, th = 0;
	TTF_SizeUTF8(f, fit, &tw, &th);
	int tx = box->x + (box->w - tw) / 2;
	int ty = box->y + (box->h - th) / 2;
	SDL_Rect prev_clip;
	SDL_GetClipRect(dst, &prev_clip);
	SDL_SetClipRect(dst, box);
	SDL_Surface* text = TTF_RenderUTF8_Blended(f, fit, shadow);
	if (text) {
		SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){tx + SCALE1(1), ty + SCALE1(1)});
		SDL_FreeSurface(text);
	}
	text = TTF_RenderUTF8_Blended(f, fit, white);
	if (text) {
		SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){tx, ty});
		SDL_FreeSurface(text);
	}
	SDL_SetClipRect(dst, &prev_clip);
}

/* Apila los atajos de botones (B Atrás, A Abrir, ...) a la derecha, en rojo,
   cada uno ocupando 2 celdas de la retícula, como "CD PLAYER" en el BIOS. */
int GFX_blitPsxHintStack(SDL_Surface* dst, char** pairs) {
	int n = 0;
	while (pairs[n * 2]) n++;
	if (n == 0) return 0;

	Uint32 red = map_px(dst, 0xa8, 0x20, 0x20);
	int cell = THEME_gridCell();
	if (cell <= 0) cell = SCALE1(PILL_SIZE);
	int w = cell * 2;
	int right_edge = THEME_alignRight(dst->w - SCALE1(PADDING));
	int x = right_edge - w;
	int y = THEME_footerY(dst->h);

	for (int i = n - 1; i >= 0; i--) {
		char label[128];
		snprintf(label, sizeof(label), "%s %s", pairs[i * 2], pairs[i * 2 + 1]);
		GFX_blitPsxFlatLabel(dst, &(SDL_Rect){x, y, w, cell}, red, font.tiny, label);
		y -= cell;
	}
	return w;
}

void GFX_blitRowPill(SDL_Surface* dst, SDL_Rect* dst_rect, int selected) {
	SDL_Rect r = *dst_rect;
	if (r.h == 0) r.h = SCALE1(PILL_SIZE);

	if (THEME_pillStyle() != THEME_PILL_PSX) {
		GFX_blitPill(selected ? ASSET_WHITE_PILL : ASSET_DARK_GRAY_PILL, dst, &r);
		return;
	}

	GFX_blitPsxMenuButton(dst, &r, selected);
}

void GFX_blitPlayStationListRow(SDL_Surface* dst, SDL_Rect* dst_rect, int selected) {
	SDL_Rect r = *dst_rect;
	if (r.h == 0) r.h = SCALE1(PILL_SIZE);
	GFX_blitPsxMenuButton(dst, &r, selected);
}

/* Botones estilo firmware DS Lite (plástico gris claro). */
static void GFX_blitDsMenuFill(SDL_Surface* dst, SDL_Rect* in) {
	Uint8 r0, g0, b0, r1, g1, b1;
	SDL_GetRGB(asset_rgbs[ASSET_BAR_BG], dst->format, &r0, &g0, &b0);
	SDL_GetRGB(asset_rgbs[ASSET_BAR_BG_MENU], dst->format, &r1, &g1, &b1);
	Uint8 rh = (uint8_t)MIN(255, r0 + 20);
	Uint8 gh = (uint8_t)MIN(255, g0 + 20);
	Uint8 bh = (uint8_t)MIN(255, b0 + 16);
	/* Borde: gris acero azulado, como el marco de plástico de la DS Lite,
	   en vez del pill claro (casi invisible sobre el relleno del botón). */
	Uint8 er = 0x5a, eg = 0x62, eb = 0x72;
	int y0 = in->y;
	int border = SCALE1(2);
	if (border * 2 >= in->w) border = in->w / 4;
	if (border < 1) border = 1;

	for (int y = 0; y < in->h; y++) {
		int t = in->h > 1 ? (y * 255) / (in->h - 1) : 0;
		uint8_t r = (uint8_t)(r0 + (r1 - r0) * t / 255);
		uint8_t g = (uint8_t)(g0 + (g1 - g0) * t / 255);
		uint8_t b = (uint8_t)(b0 + (b1 - b0) * t / 255);
		SDL_FillRect(dst, &(SDL_Rect){in->x, y0 + y, in->w, 1}, map_px(dst, r, g, b));
	}
	SDL_FillRect(dst, &(SDL_Rect){in->x, in->y, in->w, 1}, map_px(dst, rh, gh, bh));
	SDL_FillRect(dst, &(SDL_Rect){in->x, in->y, border, in->h}, map_px(dst, er, eg, eb));
	SDL_FillRect(dst, &(SDL_Rect){in->x + in->w - border, in->y, border, in->h}, map_px(dst, er, eg, eb));
	SDL_FillRect(dst, &(SDL_Rect){in->x, in->y + in->h - 1, in->w, 1}, map_px(dst, er, eg, eb));
}

static void GFX_blitDsMenuButton(SDL_Surface* dst, SDL_Rect* box, int selected) {
	Uint32 sel = asset_rgbs[ASSET_WHITE_PILL];
	int border = selected ? 2 : 0;
	if (selected)
		SDL_FillRect(dst, box, sel);
	if (box->w <= border * 2 || box->h <= border * 2) {
		if (!selected)
			GFX_blitDsMenuFill(dst, box);
		return;
	}
	SDL_Rect in = {box->x + border, box->y + border, box->w - border * 2, box->h - border * 2};
	GFX_blitDsMenuFill(dst, &in);
}

void GFX_blitDsListRow(SDL_Surface* dst, SDL_Rect* dst_rect, int selected) {
	SDL_Rect r = *dst_rect;
	if (r.h == 0) r.h = SCALE1(PILL_SIZE);
	GFX_blitDsMenuButton(dst, &r, selected);
}

void GFX_blitDsMenuTextInButton(TTF_Font* f, const char* in_name, SDL_Surface* dst, SDL_Rect* btn, int selected) {
	char label[256];
	int max_w = GFX_psxButtonTextMaxWidth(btn->w, 0);
	GFX_truncateText(f, in_name, label, max_w, 0);

	int tw = 0, th = 0;
	TTF_SizeUTF8(f, label, &tw, &th);
	int border = selected ? 2 : 0;
	int ix = btn->x + border;
	int iw = btn->w - border * 2;
	SDL_Rect inner = {ix, btn->y, iw, btn->h};
	int tx = inner.x + (inner.w - tw) / 2;
	int ty = btn->y + (btn->h - th) / 2;

	SDL_Rect prev_clip;
	SDL_GetClipRect(dst, &prev_clip);
	SDL_SetClipRect(dst, &inner);

	SDL_Surface* text = TTF_RenderUTF8_Blended(f, label, COLOR_DARK_TEXT);
	if (text) {
		SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){tx, ty});
		SDL_FreeSurface(text);
	}
	SDL_SetClipRect(dst, &prev_clip);
}

void GFX_blitPsxMenuText(TTF_Font* f, const char* str, SDL_Surface* dst, SDL_Rect* at, SDL_Color color) {
	SDL_Surface* text;
	SDL_Color white = {0xff, 0xff, 0xff, 0xff};
	SDL_Color shadow = {0x28, 0x28, 0x38, 0xff};
	(void)color;
	text = TTF_RenderUTF8_Blended(f, str, shadow);
	if (text) {
		SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){at->x + SCALE1(1), at->y + SCALE1(1)});
		SDL_FreeSurface(text);
	}
	text = TTF_RenderUTF8_Blended(f, str, white);
	if (text) {
		SDL_BlitSurface(text, NULL, dst, at);
		SDL_FreeSurface(text);
	}
}

#define PSX_LIST_BORDER_PX 4

int GFX_psxButtonTextMaxWidth(int button_w, int selected) {
	(void)selected;
	int pad = SCALE1(BUTTON_PADDING * 2);
	int inner = button_w - pad - PSX_LIST_BORDER_PX;
	return inner > SCALE1(8) ? inner : MAX(0, button_w - PSX_LIST_BORDER_PX);
}

void GFX_blitPsxMenuTextInButton(TTF_Font* f, const char* in_name, SDL_Surface* dst, SDL_Rect* btn, int selected) {
	char label[256];
	int max_w = GFX_psxButtonTextMaxWidth(btn->w, 0);
	GFX_truncateText(f, in_name, label, max_w, 0);

	int tw = 0, th = 0;
	TTF_SizeUTF8(f, label, &tw, &th);
	int border = selected ? 2 : 0;
	int ix = btn->x + border;
	int iw = btn->w - border * 2;
	SDL_Rect inner = {ix, btn->y, iw, btn->h};
	int tx = inner.x + (inner.w - tw) / 2;
	int ty = btn->y + (btn->h - th) / 2;

	SDL_Rect prev_clip;
	SDL_GetClipRect(dst, &prev_clip);
	SDL_SetClipRect(dst, &inner);

	SDL_Rect at = {tx, ty, 0, 0};
	GFX_blitPsxMenuText(f, label, dst, &at, COLOR_WHITE);

	SDL_SetClipRect(dst, &prev_clip);
}

void GFX_blitPill(int asset, SDL_Surface* dst, SDL_Rect* dst_rect) {
	int x = dst_rect->x;
	int y = dst_rect->y;
	int w = dst_rect->w;
	int h = dst_rect->h;

	if (h==0) h = asset_rects[asset].h;

	if (THEME_customPills() && (asset == ASSET_WHITE_PILL || asset == ASSET_DARK_GRAY_PILL || asset == ASSET_BLACK_PILL)) {
		if (w < h) w = h;
		SDL_FillRect(dst, &(SDL_Rect){x, y, w, h}, asset_rgbs[asset]);
		return;
	}
	
	int r = h / 2;
	if (w < h) w = h;
	w -= h;
	
	GFX_blitAsset(asset, &(SDL_Rect){0,0,r,h}, dst, &(SDL_Rect){x,y});
	x += r;
	if (w>0) {
		SDL_FillRect(dst, &(SDL_Rect){x,y,w,h}, asset_rgbs[asset]);
		x += w;
	}
	GFX_blitAsset(asset, &(SDL_Rect){r,0,r,h}, dst, &(SDL_Rect){x,y});
}
void GFX_blitRect(int asset, SDL_Surface* dst, SDL_Rect* dst_rect) {
	int x = dst_rect->x;
	int y = dst_rect->y;
	int w = dst_rect->w;
	int h = dst_rect->h;
	int c = asset_rgbs[asset];
	
	SDL_Rect* rect = &asset_rects[asset];
	int d = rect->w;
	int r = d / 2;

	GFX_blitAsset(asset, &(SDL_Rect){0,0,r,r}, dst, &(SDL_Rect){x,y});
	SDL_FillRect(dst, &(SDL_Rect){x+r,y,w-d,r}, c);
	GFX_blitAsset(asset, &(SDL_Rect){r,0,r,r}, dst, &(SDL_Rect){x+w-r,y});
	SDL_FillRect(dst, &(SDL_Rect){x,y+r,w,h-d}, c);
	GFX_blitAsset(asset, &(SDL_Rect){0,r,r,r}, dst, &(SDL_Rect){x,y+h-r});
	SDL_FillRect(dst, &(SDL_Rect){x+r,y+h-r,w-d,r}, c);
	GFX_blitAsset(asset, &(SDL_Rect){r,r,r,r}, dst, &(SDL_Rect){x+w-r,y+h-r});
}
void GFX_blitBattery(SDL_Surface* dst, SDL_Rect* dst_rect) {
	// LOG_info("dst: %p\n", dst);
	int x = 0;
	int y = 0;
	if (dst_rect) {
		x = dst_rect->x;
		y = dst_rect->y;
	}
	SDL_Rect rect = asset_rects[ASSET_BATTERY];
	x += (SCALE1(PILL_SIZE) - (rect.w + FIXED_SCALE)) / 2;
	y += (SCALE1(PILL_SIZE) - rect.h) / 2;
	
	if (pwr.is_charging) {
		GFX_blitAsset(ASSET_BATTERY, NULL, dst, &(SDL_Rect){x,y});
		GFX_blitAsset(ASSET_BATTERY_BOLT, NULL, dst, &(SDL_Rect){x+SCALE1(3),y+SCALE1(2)});
	}
	else {
		int percent = pwr.charge;
		GFX_blitAsset(percent<=10?ASSET_BATTERY_LOW:ASSET_BATTERY, NULL, dst, &(SDL_Rect){x,y});
		
		rect = asset_rects[ASSET_BATTERY_FILL];
		SDL_Rect clip = rect;
		clip.w *= percent;
		clip.w /= 100;
		if (clip.w<=0) return;
		clip.x = rect.w - clip.w;
		clip.y = 0;
		
		GFX_blitAsset(percent<=20?ASSET_BATTERY_FILL_LOW:ASSET_BATTERY_FILL, &clip, dst, &(SDL_Rect){x+SCALE1(3)+clip.x,y+SCALE1(2)});
	}
}
int GFX_getButtonWidth(char* hint, char* button) {
	int button_width = 0;
	int width;
	
	int special_case = !strcmp(button,BRIGHTNESS_BUTTON_LABEL); // TODO: oof
	
	if (strlen(button)==1) {
		button_width += SCALE1(BUTTON_SIZE);
	}
	else {
		button_width += SCALE1(BUTTON_SIZE) / 2;
		TTF_SizeUTF8(special_case ? font.large : font.tiny, button, &width, NULL);
		button_width += width;
	}
	button_width += SCALE1(BUTTON_MARGIN);
	
	TTF_SizeUTF8(font.small, hint, &width, NULL);
	button_width += width + SCALE1(BUTTON_MARGIN);
	return button_width;
}
void GFX_blitButton(char* hint, char*button, SDL_Surface* dst, SDL_Rect* dst_rect) {
	SDL_Surface* text;
	int ox = 0;
	
	int special_case = !strcmp(button,BRIGHTNESS_BUTTON_LABEL); // TODO: oof
	int psx_chrome = THEME_pillStyle() == THEME_PILL_PSX;
	
	// button
	if (strlen(button)==1) {
		GFX_blitAsset(ASSET_BUTTON, NULL, dst, dst_rect);

		// label
		SDL_Color glyph = THEME_customPills()
			? (SDL_Color){0x11, 0x11, 0x11, 0xff}
			: COLOR_BUTTON_TEXT;
		text = TTF_RenderUTF8_Blended(font.medium, button, glyph);
		SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){dst_rect->x+(SCALE1(BUTTON_SIZE)-text->w)/2,dst_rect->y+(SCALE1(BUTTON_SIZE)-text->h)/2});
		ox += SCALE1(BUTTON_SIZE);
		SDL_FreeSurface(text);
	}
	else {
		// ASSET_BUTTON siempre es un pill blanco: si el tema es claro/oscuro
		// y COLOR_BUTTON_TEXT también queda claro, el texto se volvía
		// ilegible (blanco sobre blanco). Igual que el glifo de 1 letra de
		// arriba, forzamos texto oscuro cuando se dibuja ese pill.
		SDL_Color btn_color = (!psx_chrome && THEME_customPills())
			? (SDL_Color){0x11, 0x11, 0x11, 0xff}
			: COLOR_BUTTON_TEXT;
		text = TTF_RenderUTF8_Blended(special_case ? font.large : font.tiny, button, btn_color);
		if (!psx_chrome)
			GFX_blitPill(ASSET_BUTTON, dst, &(SDL_Rect){dst_rect->x,dst_rect->y,SCALE1(BUTTON_SIZE)/2+text->w,SCALE1(BUTTON_SIZE)});
		ox += psx_chrome ? 0 : SCALE1(BUTTON_SIZE)/4;
		
		int oy = special_case ? SCALE1(-2) : 0;
		SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){ox+dst_rect->x,oy+dst_rect->y+(SCALE1(BUTTON_SIZE)-text->h)/2,text->w,text->h});
		ox += text->w;
		ox += psx_chrome ? SCALE1(BUTTON_MARGIN) : SCALE1(BUTTON_SIZE)/4;
		SDL_FreeSurface(text);
	}
	
	ox += SCALE1(BUTTON_MARGIN);

	// hint text
	text = TTF_RenderUTF8_Blended(font.small, hint, COLOR_FOOTER_TEXT);
	SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){ox+dst_rect->x,dst_rect->y+(SCALE1(BUTTON_SIZE)-text->h)/2,text->w,text->h});
	SDL_FreeSurface(text);
}
void GFX_blitMessage(TTF_Font* font, char* msg, SDL_Surface* dst, SDL_Rect* dst_rect) {
	if (!dst_rect) dst_rect = &(SDL_Rect){0,0,dst->w,dst->h};
	
	// LOG_info("GFX_blitMessage: %p (%ix%i)", dst, dst_rect->w,dst_rect->h);
	
	SDL_Surface* text;
#define TEXT_BOX_MAX_ROWS 16
#define LINE_HEIGHT 24
	char* rows[TEXT_BOX_MAX_ROWS];
	int row_count = 0;

	char* tmp;
	rows[row_count++] = msg;
	while ((tmp=strchr(rows[row_count-1], '\n'))!=NULL) {
		if (row_count+1>=TEXT_BOX_MAX_ROWS) return; // TODO: bail
		rows[row_count++] = tmp+1;
	}
	
	int rendered_height = SCALE1(LINE_HEIGHT) * row_count;
	int y = dst_rect->y;
	y += (dst_rect->h - rendered_height) / 2;
	
	char line[256];
	for (int i=0; i<row_count; i++) {
		int len;
		if (i+1<row_count) {
			len = rows[i+1]-rows[i]-1;
			if (len) strncpy(line, rows[i], len);
			line[len] = '\0';
		}
		else {
			len = strlen(rows[i]);
			strcpy(line, rows[i]);
		}
		
		
		if (len) {
			text = TTF_RenderUTF8_Blended(font, line, COLOR_WHITE);
			int x = dst_rect->x;
			x += (dst_rect->w - text->w) / 2;
			SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){x,y});
			SDL_FreeSurface(text);
		}
		y += SCALE1(LINE_HEIGHT);
	}
}

int GFX_blitHardwareGroup(SDL_Surface* dst, int show_setting) {
	int ox;
	int oy;
	int ow = 0;
	int bar_h = THEME_listRowH();
	
	int setting_value;
	int setting_min;
	int setting_max;
	
	if (show_setting && !GetHDMI()) {
		ow = SCALE1(PILL_SIZE + SETTINGS_WIDTH + 10 + 4);
		oy = THEME_marginTop();
		if (THEME_pillStyle() == THEME_PILL_PSX) {
			ow = THEME_alignWidth(ow);
			ox = THEME_alignRight(dst->w - SCALE1(PADDING) - ow);
			GFX_blitPsxChromeBar(dst, &(SDL_Rect){ox, oy, ow, bar_h});
		} else {
			ox = dst->w - SCALE1(PADDING) - ow;
			GFX_blitPill(gfx.mode==MODE_MAIN ? ASSET_DARK_GRAY_PILL : ASSET_BLACK_PILL, dst, &(SDL_Rect){
				ox,
				oy,
				ow,
				bar_h
			});
		}

		if (show_setting==1) {
			setting_value = GetBrightness();
			setting_min = BRIGHTNESS_MIN;
			setting_max = BRIGHTNESS_MAX;
		}
		else {
			setting_value = GetVolume();
			setting_min = VOLUME_MIN;
			setting_max = VOLUME_MAX;
		}
		
		int asset = show_setting==1?ASSET_BRIGHTNESS:(setting_value>0?ASSET_VOLUME:ASSET_VOLUME_MUTE);
		int ax = ox + (show_setting==1 ? SCALE1(6) : SCALE1(8));
		int ay = oy + (show_setting==1 ? SCALE1(5) : SCALE1(7));
		GFX_blitAsset(asset, NULL, dst, &(SDL_Rect){ax,ay});
		
		ox += SCALE1(PILL_SIZE);
		oy += SCALE1((PILL_SIZE - SETTINGS_SIZE) / 2);
		GFX_blitPill(gfx.mode==MODE_MAIN ? ASSET_BAR_BG : ASSET_BAR_BG_MENU, dst, &(SDL_Rect){
			ox,
			oy,
			SCALE1(SETTINGS_WIDTH),
			SCALE1(SETTINGS_SIZE)
		});
		
		float percent = ((float)(setting_value-setting_min) / (setting_max-setting_min));
		if (show_setting==1 || setting_value>0) {
			GFX_blitPill(ASSET_BAR, dst, &(SDL_Rect){
				ox,
				oy,
				SCALE1(SETTINGS_WIDTH) * percent,
				SCALE1(SETTINGS_SIZE)
			});
		}
	}
	else {
		// TODO: handle wifi
		int show_wifi = PLAT_isOnline(); // NOOOOO! not every frame!
		int psx_chrome = THEME_pillStyle() == THEME_PILL_PSX;

		int ww = SCALE1(PILL_SIZE-3);
		ow = SCALE1(PILL_SIZE);
		if (show_wifi) ow += ww;

		// porcentaje de batería a la izquierda del icono
		char battery_text[8];
		sprintf(battery_text, "%i%%", pwr.charge);
		SDL_Surface* battery_txt = NULL;
		int btw = 0, bth = 0;
		if (psx_chrome)
			TTF_SizeUTF8(font.tiny, battery_text, &btw, &bth);
		else {
			battery_txt = TTF_RenderUTF8_Blended(font.tiny, battery_text, COLOR_WHITE);
			btw = battery_txt ? battery_txt->w : 0;
		}
		int bw = btw ? btw + SCALE1(6) : 0;
		ow += bw;

		oy = THEME_marginTop();
		if (psx_chrome) {
			// pastilla plana de un solo color, como "MEMORY CARD" / "CD PLAYER"
			// del BIOS real, alineada a toda celda de la retícula.
			ow = THEME_alignWidth(ow);
			ox = THEME_alignRight(dst->w - SCALE1(PADDING) - ow);
			SDL_FillRect(dst, &(SDL_Rect){ox, oy, ow, bar_h}, asset_rgbs[ASSET_WHITE_PILL]);
		} else {
			ox = dst->w - SCALE1(PADDING) - ow;
			GFX_blitPill(gfx.mode==MODE_MAIN ? ASSET_DARK_GRAY_PILL : ASSET_BLACK_PILL, dst, &(SDL_Rect){
				ox,
				oy,
				ow,
				bar_h
			});
		}
		if (show_wifi) {
			SDL_Rect rect = asset_rects[ASSET_WIFI];
			int x = ox;
			int y = oy;
			x += (bar_h - rect.w) / 2;
			y += (bar_h - rect.h) / 2;

			GFX_blitAsset(ASSET_WIFI, NULL, dst, &(SDL_Rect){x,y});
			ox += ww;
		}
		if (bw) {
			if (psx_chrome) {
				GFX_blitPsxMenuText(font.tiny, battery_text, dst, &(SDL_Rect){
					ox + SCALE1(6),
					oy + (bar_h - bth) / 2
				}, COLOR_WHITE);
			} else if (battery_txt) {
				SDL_BlitSurface(battery_txt, NULL, dst, &(SDL_Rect){
					ox + SCALE1(6),
					oy + (bar_h - battery_txt->h) / 2
				});
				SDL_FreeSurface(battery_txt);
			}
			ox += bw;
		}
		GFX_blitBattery(dst, &(SDL_Rect){ox,oy});
	}
	
	return ow;
}
void GFX_blitHardwareHints(SDL_Surface* dst, int show_setting) {
	if (BTN_MOD_VOLUME==BTN_SELECT && BTN_MOD_BRIGHTNESS==BTN_START) {
		if (show_setting==1) GFX_blitButtonGroup((char*[]){ "SELECT","VOLUMEN",  NULL }, 0, dst, 0);
		else GFX_blitButtonGroup((char*[]){ "START","BRILLO",  NULL }, 0, dst, 0);
	}
	else {
		if (show_setting==1) GFX_blitButtonGroup((char*[]){ BRIGHTNESS_BUTTON_LABEL,"BRILLO",  NULL }, 0, dst, 0);
		else GFX_blitButtonGroup((char*[]){ "MENÚ","BRILLO",  NULL }, 0, dst, 0);
	}
	
}

int GFX_blitButtonGroup(char** pairs, int primary, SDL_Surface* dst, int align_right) {
	int ox;
	int oy;
	int ow;
	int bar_h = THEME_listRowH();
	char* hint;
	char* button;

	struct Hint {
		char* hint;
		char* button;
		int ow;
	} hints[2]; 
	int w = 0; // individual button dimension
	int h = 0; // hints index
	ow = 0; // full pill width
	ox = align_right ? dst->w - SCALE1(PADDING) : THEME_listX();
	oy = THEME_footerY(dst->h);
	
	for (int i=0; i<2; i++) {
		if (!pairs[i*2]) break;
		if (HAS_SKINNY_SCREEN && i!=primary) continue; // space saving
		
		button = pairs[i * 2];
		hint = pairs[i * 2 + 1];
		w = GFX_getButtonWidth(hint, button);
		hints[h].hint = hint;
		hints[h].button = button;
		hints[h].ow = w;
		h += 1;
		ow += SCALE1(BUTTON_MARGIN) + w;
	}
	
	ow += SCALE1(BUTTON_MARGIN);
	if (align_right) {
		if (THEME_pillStyle() == THEME_PILL_PSX)
			ox = THEME_alignRight(dst->w - SCALE1(PADDING) - ow);
		else
			ox -= ow;
	}
	if (THEME_pillStyle() == THEME_PILL_PSX) {
		GFX_blitPsxChromeBar(dst, &(SDL_Rect){ox, oy, ow, bar_h});
	} else {
		GFX_blitPill(gfx.mode==MODE_MAIN ? ASSET_DARK_GRAY_PILL : ASSET_BLACK_PILL, dst, &(SDL_Rect){
			ox,
			oy,
			ow,
			bar_h
		});
	}
	
	ox += SCALE1(BUTTON_MARGIN);
	oy += SCALE1(BUTTON_MARGIN);
	for (int i=0; i<h; i++) {
		GFX_blitButton(hints[i].hint, hints[i].button, dst, &(SDL_Rect){ox,oy});
		ox += hints[i].ow + SCALE1(BUTTON_MARGIN);
	}
	return ow;
}

#define MAX_TEXT_LINES 16
void GFX_sizeText(TTF_Font* font, char* str, int leading, int* w, int* h) {
	char* lines[MAX_TEXT_LINES];
	int count = 0;

	char* tmp;
	lines[count++] = str;
	while ((tmp=strchr(lines[count-1], '\n'))!=NULL) {
		if (count+1>MAX_TEXT_LINES) break; // TODO: bail?
		lines[count++] = tmp+1;
	}
	*h = count * leading;
	
	int mw = 0;
	char line[256];
	for (int i=0; i<count; i++) {
		int len;
		if (i+1<count) {
			len = lines[i+1]-lines[i]-1;
			if (len) strncpy(line, lines[i], len);
			line[len] = '\0';
		}
		else {
			len = strlen(lines[i]);
			strcpy(line, lines[i]);
		}
		
		if (len) {
			int lw;
			TTF_SizeUTF8(font, line, &lw, NULL);
			if (lw>mw) mw = lw;
		}
	}
	*w = mw;
}
void GFX_blitText(TTF_Font* font, char* str, int leading, SDL_Color color, SDL_Surface* dst, SDL_Rect* dst_rect) {
	if (dst_rect==NULL) dst_rect = &(SDL_Rect){0,0,dst->w,dst->h};
	
	char* lines[MAX_TEXT_LINES];
	int count = 0;

	char* tmp;
	lines[count++] = str;
	while ((tmp=strchr(lines[count-1], '\n'))!=NULL) {
		if (count+1>MAX_TEXT_LINES) break; // TODO: bail?
		lines[count++] = tmp+1;
	}
	int x = dst_rect->x;
	int y = dst_rect->y;
	
	SDL_Surface* text;
	char line[256];
	for (int i=0; i<count; i++) {
		int len;
		if (i+1<count) {
			len = lines[i+1]-lines[i]-1;
			if (len) strncpy(line, lines[i], len);
			line[len] = '\0';
		}
		else {
			len = strlen(lines[i]);
			strcpy(line, lines[i]);
		}
		
		if (len) {
			text = TTF_RenderUTF8_Blended(font, line, color);
			SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){x+((dst_rect->w-text->w)/2),y+(i*leading)});
			SDL_FreeSurface(text);
		}
	}
}

///////////////////////////////

// based on picoarch's audio 
// implementation, rewritten 
// to (try to) understand it 
// better

#define MAX_SAMPLE_RATE 48000
#define BATCH_SIZE 100
#ifndef SAMPLES
	#define SAMPLES 512 // default
#endif

#define ms SDL_GetTicks

typedef int (*SND_Resampler)(const SND_Frame frame);
static struct SND_Context {
	int initialized;
	double frame_rate;
	
	int sample_rate_in;
	int sample_rate_out;
	
	int buffer_seconds;     // current_audio_buffer_size
	SND_Frame* buffer;		// buf
	size_t frame_count; 	// buf_len
	
	int frame_in;     // buf_w
	int frame_out;    // buf_r
	int frame_filled; // max_buf_w
	
	SND_Resampler resample;
} snd = {0};
static void SND_audioCallback(void* userdata, uint8_t* stream, int len) { // plat_sound_callback
	
	// return (void)memset(stream,0,len); // TODO: tmp, silent
	
	if (snd.frame_count==0) return;
	
	int16_t *out = (int16_t *)stream;
	len /= (sizeof(int16_t) * 2);
	// int full_len = len;
	
	// if (snd.frame_out!=snd.frame_in) LOG_info("%8i consuming samples (%i frames)\n", ms(), len);
	
	while (snd.frame_out!=snd.frame_in && len>0) {
		*out++ = snd.buffer[snd.frame_out].left;
		*out++ = snd.buffer[snd.frame_out].right;
		
		snd.frame_filled = snd.frame_out;
		
		snd.frame_out += 1;
		len -= 1;
		
		if (snd.frame_out>=snd.frame_count) snd.frame_out = 0;
	}
	
	int zero = len>0 && len==SAMPLES;
	if (zero) return (void)memset(out,0,len*(sizeof(int16_t) * 2));
	// else if (len>=5) LOG_info("%8i BUFFER UNDERRUN (%i/%i frames)\n", ms(), len,full_len);

	int16_t *in = out-1;
	while (len>0) {
		*out++ = (void*)in>(void*)stream ? *--in : 0;
		*out++ = (void*)in>(void*)stream ? *--in : 0;
		len -= 1;
	}
}
static void SND_resizeBuffer(void) { // plat_sound_resize_buffer
	snd.frame_count = snd.buffer_seconds * snd.sample_rate_in / snd.frame_rate;
	if (snd.frame_count==0) return;
	
	// LOG_info("frame_count: %i (%i * %i / %f)\n", snd.frame_count, snd.buffer_seconds, snd.sample_rate_in, snd.frame_rate);
	// snd.frame_count *= 2; // no help
	
	SDL_LockAudio();
	
	int buffer_bytes = snd.frame_count * sizeof(SND_Frame);
	snd.buffer = realloc(snd.buffer, buffer_bytes);
	
	memset(snd.buffer, 0, buffer_bytes);
	
	snd.frame_in = 0;
	snd.frame_out = 0;
	snd.frame_filled = snd.frame_count - 1;
	
	SDL_UnlockAudio();
}
static int SND_resampleNone(SND_Frame frame) { // audio_resample_passthrough
	snd.buffer[snd.frame_in++] = frame;
	if (snd.frame_in >= snd.frame_count) snd.frame_in = 0;
	return 1;
}
static int SND_resampleNear(SND_Frame frame) { // audio_resample_nearest
	static int diff = 0;
	int consumed = 0;

	if (diff < snd.sample_rate_out) {
		snd.buffer[snd.frame_in++] = frame;
		if (snd.frame_in >= snd.frame_count) snd.frame_in = 0;
		diff += snd.sample_rate_in;
	}

	if (diff >= snd.sample_rate_out) {
		consumed++;
		diff -= snd.sample_rate_out;
	}

	return consumed;
}
static void SND_selectResampler(void) { // plat_sound_select_resampler
	if (snd.sample_rate_in==snd.sample_rate_out) {
		snd.resample =  SND_resampleNone;
	}
	else {
		snd.resample = SND_resampleNear;
	}
}
size_t SND_batchSamples(const SND_Frame* frames, size_t frame_count) { // plat_sound_write / plat_sound_write_resample
	
	// return frame_count; // TODO: tmp, silent
	
	if (snd.frame_count==0) return 0;
	
	// LOG_info("%8i batching samples (%i frames)\n", ms(), frame_count);
	
	SDL_LockAudio();

	int consumed = 0;
	int consumed_frames = 0;
	while (frame_count > 0) {
		int tries = 0;
		int amount = MIN(BATCH_SIZE, frame_count);
		
		while (tries < 10 && snd.frame_in==snd.frame_filled) {
			tries++;
			SDL_UnlockAudio();
			SDL_Delay(1);
			SDL_LockAudio();
		}
		// if (tries) LOG_info("%8i waited %ims for buffer to get low...\n", ms(), tries);

		while (amount && snd.frame_in != snd.frame_filled) {
			consumed_frames = snd.resample(*frames);
			
			frames += consumed_frames;
			amount -= consumed_frames;
			frame_count -= consumed_frames;
			consumed += consumed_frames;
		}
	}
	SDL_UnlockAudio();
	
	return consumed;
}

void SND_init(double sample_rate, double frame_rate) { // plat_sound_init
	LOG_info("SND_init\n");
	
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	
#if defined(USE_SDL2)
	LOG_info("Available audio drivers:\n");
	for (int i=0; i<SDL_GetNumAudioDrivers(); i++) {
		LOG_info("- %s\n", SDL_GetAudioDriver(i));
	}
	LOG_info("Current audio driver: %s\n", SDL_GetCurrentAudioDriver());
#endif	
	
	memset(&snd, 0, sizeof(struct SND_Context));
	snd.frame_rate = frame_rate;

	SDL_AudioSpec spec_in;
	SDL_AudioSpec spec_out;

	spec_in.freq = PLAT_pickSampleRate(sample_rate, MAX_SAMPLE_RATE);
	spec_in.format = AUDIO_S16;
	spec_in.channels = 2;
	spec_in.samples = SAMPLES;
	spec_in.callback = SND_audioCallback;
	
	if (SDL_OpenAudio(&spec_in, &spec_out)<0) LOG_info("SDL_OpenAudio error: %s\n", SDL_GetError());
	
	snd.buffer_seconds = 5;
	snd.sample_rate_in  = sample_rate;
	snd.sample_rate_out = spec_out.freq;
	
	SND_selectResampler();
	SND_resizeBuffer();
	
	SDL_PauseAudio(0);

	LOG_info("sample rate: %i (req) %i (rec) [samples %i]\n", snd.sample_rate_in, snd.sample_rate_out, SAMPLES);
	snd.initialized = 1;
}
void SND_quit(void) { // plat_sound_finish
	if (!snd.initialized) return;
	
	SDL_PauseAudio(1);
	SDL_CloseAudio();
	
	if (snd.buffer) {
		free(snd.buffer);
		snd.buffer = NULL;
	}
}

///////////////////////////////

LID_Context lid = {
	.has_lid = 0,
	.is_open = 1,
};

FALLBACK_IMPLEMENTATION void PLAT_initLid(void) {  }
FALLBACK_IMPLEMENTATION int PLAT_lidChanged(int* state) { return 0; }

///////////////////////////////

PAD_Context pad;

#define AXIS_DEADZONE 0x4000
void PAD_setAnalog(int neg_id,int pos_id,int value,int repeat_at) {
	// LOG_info("neg %i pos %i value %i\n", neg_id, pos_id, value);
	int neg = 1 << neg_id;
	int pos = 1 << pos_id;	
	if (value>AXIS_DEADZONE) { // pressing
		if (!(pad.is_pressed&pos)) { // not pressing
			pad.is_pressed 		|= pos; // set
			pad.just_pressed	|= pos; // set
			pad.just_repeated	|= pos; // set
			pad.repeat_at[pos_id]= repeat_at;
		
			if (pad.is_pressed&neg) { // was pressing opposite
				pad.is_pressed 		&= ~neg; // unset
				pad.just_repeated 	&= ~neg; // unset
				pad.just_released	|=  neg; // set
			}
		}
	}
	else if (value<-AXIS_DEADZONE) { // pressing
		if (!(pad.is_pressed&neg)) { // not pressing
			pad.is_pressed		|= neg; // set
			pad.just_pressed	|= neg; // set
			pad.just_repeated	|= neg; // set
			pad.repeat_at[neg_id]= repeat_at;
		
			if (pad.is_pressed&pos) { // was pressing opposite
				pad.is_pressed 		&= ~pos; // unset
				pad.just_repeated 	&= ~pos; // unset
				pad.just_released	|=  pos; // set
			}
		}
	}
	else { // not pressing
		if (pad.is_pressed&neg) { // was pressing
			pad.is_pressed 		&= ~neg; // unset
			pad.just_repeated	&=  neg; // unset
			pad.just_released	|=  neg; // set
		}
		if (pad.is_pressed&pos) { // was pressing
			pad.is_pressed 		&= ~pos; // unset
			pad.just_repeated	&=  pos; // unset
			pad.just_released	|=  pos; // set
		}
	}
}

void PAD_reset(void) {
	// LOG_info("PAD_reset");
	pad.just_pressed = BTN_NONE;
	pad.is_pressed = BTN_NONE;
	pad.just_released = BTN_NONE;
	pad.just_repeated = BTN_NONE;
}
FALLBACK_IMPLEMENTATION void PLAT_pollInput(void) {
	// reset transient state
	pad.just_pressed = BTN_NONE;
	pad.just_released = BTN_NONE;
	pad.just_repeated = BTN_NONE;

	uint32_t tick = SDL_GetTicks();
	for (int i=0; i<BTN_ID_COUNT; i++) {
		int btn = 1 << i;
		if ((pad.is_pressed & btn) && (tick>=pad.repeat_at[i])) {
			pad.just_repeated |= btn; // set
			pad.repeat_at[i] += PAD_REPEAT_INTERVAL;
		}
	}
	
	// the actual poll
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		int btn = BTN_NONE;
		int pressed = 0; // 0=up,1=down
		int id = -1;
		if (event.type==SDL_KEYDOWN || event.type==SDL_KEYUP) {
			uint8_t code = event.key.keysym.scancode;
			pressed = event.type==SDL_KEYDOWN;
			// LOG_info("key event: %i (%i)\n", code,pressed);
				 if (code==CODE_UP) 		{ btn = BTN_DPAD_UP; 		id = BTN_ID_DPAD_UP; }
 			else if (code==CODE_DOWN)		{ btn = BTN_DPAD_DOWN; 		id = BTN_ID_DPAD_DOWN; }
			else if (code==CODE_LEFT)		{ btn = BTN_DPAD_LEFT; 		id = BTN_ID_DPAD_LEFT; }
			else if (code==CODE_RIGHT)		{ btn = BTN_DPAD_RIGHT; 	id = BTN_ID_DPAD_RIGHT; }
			else if (code==CODE_A)			{ btn = BTN_A; 				id = BTN_ID_A; }
			else if (code==CODE_B)			{ btn = BTN_B; 				id = BTN_ID_B; }
			else if (code==CODE_X)			{ btn = BTN_X; 				id = BTN_ID_X; }
			else if (code==CODE_Y)			{ btn = BTN_Y; 				id = BTN_ID_Y; }
			else if (code==CODE_START)		{ btn = BTN_START; 			id = BTN_ID_START; }
			else if (code==CODE_SELECT)		{ btn = BTN_SELECT; 		id = BTN_ID_SELECT; }
			else if (code==CODE_MENU)		{ btn = BTN_MENU; 			id = BTN_ID_MENU; }
			else if (code==CODE_MENU_ALT)	{ btn = BTN_MENU; 			id = BTN_ID_MENU; }
			else if (code==CODE_L1)			{ btn = BTN_L1; 			id = BTN_ID_L1; }
			else if (code==CODE_L2)			{ btn = BTN_L2; 			id = BTN_ID_L2; }
			else if (code==CODE_L3)			{ btn = BTN_L3; 			id = BTN_ID_L3; }
			else if (code==CODE_R1)			{ btn = BTN_R1; 			id = BTN_ID_R1; }
			else if (code==CODE_R2)			{ btn = BTN_R2; 			id = BTN_ID_R2; }
			else if (code==CODE_R3)			{ btn = BTN_R3; 			id = BTN_ID_R3; }
			else if (code==CODE_PLUS)		{ btn = BTN_PLUS; 			id = BTN_ID_PLUS; }
			else if (code==CODE_MINUS)		{ btn = BTN_MINUS; 			id = BTN_ID_MINUS; }
			else if (code==CODE_POWER)		{ btn = BTN_POWER; 			id = BTN_ID_POWER; }
			else if (code==CODE_POWEROFF)	{ btn = BTN_POWEROFF;		id = BTN_ID_POWEROFF; } // nano-only
		}
		else if (event.type==SDL_JOYBUTTONDOWN || event.type==SDL_JOYBUTTONUP) {
			uint8_t joy = event.jbutton.button;
			pressed = event.type==SDL_JOYBUTTONDOWN;
			// LOG_info("joy event: %i (%i)\n", joy,pressed);
				 if (joy==JOY_UP) 		{ btn = BTN_DPAD_UP; 		id = BTN_ID_DPAD_UP; }
 			else if (joy==JOY_DOWN)		{ btn = BTN_DPAD_DOWN; 		id = BTN_ID_DPAD_DOWN; }
			else if (joy==JOY_LEFT)		{ btn = BTN_DPAD_LEFT; 		id = BTN_ID_DPAD_LEFT; }
			else if (joy==JOY_RIGHT)	{ btn = BTN_DPAD_RIGHT; 	id = BTN_ID_DPAD_RIGHT; }
			else if (joy==JOY_A)		{ btn = BTN_A; 				id = BTN_ID_A; }
			else if (joy==JOY_B)		{ btn = BTN_B; 				id = BTN_ID_B; }
			else if (joy==JOY_X)		{ btn = BTN_X; 				id = BTN_ID_X; }
			else if (joy==JOY_Y)		{ btn = BTN_Y; 				id = BTN_ID_Y; }
			else if (joy==JOY_START)	{ btn = BTN_START; 			id = BTN_ID_START; }
			else if (joy==JOY_SELECT)	{ btn = BTN_SELECT; 		id = BTN_ID_SELECT; }
			else if (joy==JOY_MENU)		{ btn = BTN_MENU; 			id = BTN_ID_MENU; }
			else if (joy==JOY_MENU_ALT) { btn = BTN_MENU; 			id = BTN_ID_MENU; }
			else if (joy==JOY_MENU_ALT2){ btn = BTN_MENU; 			id = BTN_ID_MENU; }
			else if (joy==JOY_L1)		{ btn = BTN_L1; 			id = BTN_ID_L1; }
			else if (joy==JOY_L2)		{ btn = BTN_L2; 			id = BTN_ID_L2; }
			else if (joy==JOY_L3)		{ btn = BTN_L3; 			id = BTN_ID_L3; }
			else if (joy==JOY_R1)		{ btn = BTN_R1; 			id = BTN_ID_R1; }
			else if (joy==JOY_R2)		{ btn = BTN_R2; 			id = BTN_ID_R2; }
			else if (joy==JOY_R3)		{ btn = BTN_R3; 			id = BTN_ID_R3; }
			else if (joy==JOY_PLUS)		{ btn = BTN_PLUS; 			id = BTN_ID_PLUS; }
			else if (joy==JOY_MINUS)	{ btn = BTN_MINUS; 			id = BTN_ID_MINUS; }
			else if (joy==JOY_POWER)	{ btn = BTN_POWER; 			id = BTN_ID_POWER; }
		}
		else if (event.type==SDL_JOYHATMOTION) {
			int hats[4] = {-1,-1,-1,-1}; // -1=no change,0=up,1=down,2=left,3=right btn_ids
			int hat = event.jhat.value;
			// LOG_info("hat event: %i\n", hat);
			// TODO: safe to assume hats will always be the primary dpad?
			// TODO: this is literally a bitmask, make it one (oh, except there's 3 states...)
			switch (hat) {
				case SDL_HAT_UP:			hats[0]=1;	  hats[1]=0;	hats[2]=0;	  hats[3]=0;	break;
				case SDL_HAT_DOWN:			hats[0]=0;	  hats[1]=1;	hats[2]=0;	  hats[3]=0;	break;
				case SDL_HAT_LEFT:			hats[0]=0;	  hats[1]=0;	hats[2]=1;	  hats[3]=0;	break;
				case SDL_HAT_RIGHT:			hats[0]=0;	  hats[1]=0;	hats[2]=0;	  hats[3]=1;	break;
				case SDL_HAT_LEFTUP:		hats[0]=1;	  hats[1]=0;	hats[2]=1;	  hats[3]=0;	break;
				case SDL_HAT_LEFTDOWN:		hats[0]=0;	  hats[1]=1;	hats[2]=1;	  hats[3]=0;	break;
				case SDL_HAT_RIGHTUP:		hats[0]=1;	  hats[1]=0;	hats[2]=0;	  hats[3]=1;	break;
				case SDL_HAT_RIGHTDOWN:		hats[0]=0;	  hats[1]=1;	hats[2]=0;	  hats[3]=1;	break;
				case SDL_HAT_CENTERED:		hats[0]=0;	  hats[1]=0;	hats[2]=0;	  hats[3]=0;	break;
				default: break;
			}
			
			for (id=0; id<4; id++) {
				int state = hats[id];
				btn = 1 << id;
				if (state==0) {
					pad.is_pressed		&= ~btn; // unset
					pad.just_repeated	&= ~btn; // unset
					pad.just_released	|= btn; // set
				}
				else if (state==1 && (pad.is_pressed & btn)==BTN_NONE) {
					pad.just_pressed	|= btn; // set
					pad.just_repeated	|= btn; // set
					pad.is_pressed		|= btn; // set
					pad.repeat_at[id]	= tick + PAD_REPEAT_DELAY;
				}
			}
			btn = BTN_NONE; // already handled, force continue
		}
		else if (event.type==SDL_JOYAXISMOTION) {
			int axis = event.jaxis.axis;
			int val = event.jaxis.value;
			// LOG_info("axis: %i (%i)\n", axis,val);
			
			// triggers on tg5040
			if (axis==AXIS_L2) {
				btn = BTN_L2;
				id = BTN_ID_L2;
				pressed = val>0;
			}
			else if (axis==AXIS_R2) {
				btn = BTN_R2;
				id = BTN_ID_R2;
				pressed = val>0;
			}
			
			else if (axis==AXIS_LX) { pad.laxis.x = val; PAD_setAnalog(BTN_ID_ANALOG_LEFT, BTN_ID_ANALOG_RIGHT, val, tick+PAD_REPEAT_DELAY); }
			else if (axis==AXIS_LY) { pad.laxis.y = val; PAD_setAnalog(BTN_ID_ANALOG_UP,   BTN_ID_ANALOG_DOWN,  val, tick+PAD_REPEAT_DELAY); }
			else if (axis==AXIS_RX) pad.raxis.x = val;
			else if (axis==AXIS_RY) pad.raxis.y = val;
			
			// axis will fire off what looks like a release
			// before the first press but you can't release
			// a button that wasn't pressed
			if (!pressed && btn!=BTN_NONE && !(pad.is_pressed & btn)) {
				// LOG_info("cancel: %i\n", axis);
				btn = BTN_NONE;
			}
		}
		// else if (event.type==SDL_QUIT) PWR_powerOff(); // added for macOS debug
		
		if (btn==BTN_NONE) continue;
		
		if (!pressed) {
			pad.is_pressed		&= ~btn; // unset
			pad.just_repeated	&= ~btn; // unset
			pad.just_released	|= btn; // set
		}
		else if ((pad.is_pressed & btn)==BTN_NONE) {
			pad.just_pressed	|= btn; // set
			pad.just_repeated	|= btn; // set
			pad.is_pressed		|= btn; // set
			pad.repeat_at[id]	= tick + PAD_REPEAT_DELAY;
		}
	}
	
	if (lid.has_lid && PLAT_lidChanged(NULL)) pad.just_released |= BTN_SLEEP;
}
FALLBACK_IMPLEMENTATION int PLAT_shouldWake(void) {
	int lid_open = 1; // assume open by default
	if (lid.has_lid && PLAT_lidChanged(&lid_open) && lid_open) return 1;
	
	
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type==SDL_KEYUP) {
			uint8_t code = event.key.keysym.scancode;
			if ((BTN_WAKE==BTN_POWER && code==CODE_POWER) || (BTN_WAKE==BTN_MENU && (code==CODE_MENU || code==CODE_MENU_ALT))) {
				// ignore input while lid is closed
				if (lid.has_lid && !lid.is_open) return 0;  // do it here so we eat the input
				return 1;
			}
		}
		else if (event.type==SDL_JOYBUTTONUP) {
			uint8_t joy = event.jbutton.button;
			if ((BTN_WAKE==BTN_POWER && joy==JOY_POWER) || (BTN_WAKE==BTN_MENU && (joy==JOY_MENU || joy==JOY_MENU_ALT))) {
				// ignore input while lid is closed
				if (lid.has_lid && !lid.is_open) return 0;  // do it here so we eat the input
				return 1;
			}
		} 
	}
	return 0;
}

int PAD_anyJustPressed(void)	{ return pad.just_pressed!=BTN_NONE; }
int PAD_anyPressed(void)		{ return pad.is_pressed!=BTN_NONE; }
int PAD_anyJustReleased(void)	{ return pad.just_released!=BTN_NONE; }

int PAD_justPressed(int btn)	{ return pad.just_pressed & btn; }
int PAD_isPressed(int btn)		{ return pad.is_pressed & btn; }
int PAD_justReleased(int btn)	{ return pad.just_released & btn; }
int PAD_justRepeated(int btn)	{ return pad.just_repeated & btn; }

int PAD_tappedMenu(uint32_t now) {
	#define MENU_DELAY 250 // also in PWR_update()
	static uint32_t menu_start = 0;
	static int ignore_menu = 0; 
	if (PAD_justPressed(BTN_MENU)) {
		ignore_menu = 0;
		menu_start = now;
	}
	else if (PAD_isPressed(BTN_MENU) && BTN_MOD_BRIGHTNESS==BTN_MENU && (PAD_justPressed(BTN_MOD_PLUS) || PAD_justPressed(BTN_MOD_MINUS))) {
		ignore_menu = 1;
	}
	return (!ignore_menu && PAD_justReleased(BTN_MENU) && now-menu_start<MENU_DELAY);
}

///////////////////////////////

static struct VIB_Context {
	int initialized;
	pthread_t pt;
	int queued_strength;
	int strength;
} vib = {0};
static void* VIB_thread(void *arg) {
#define DEFER_FRAMES 3
	static int defer = 0;
	while(1) {
		SDL_Delay(17);
		if (vib.queued_strength!=vib.strength) {
			if (defer<DEFER_FRAMES && vib.queued_strength==0) { // minimize vacillation between 0 and some number (which this motor doesn't like)
				defer += 1;
				continue;
			}
			vib.strength = vib.queued_strength;
			defer = 0;

			PLAT_setRumble(vib.strength);
		}
	}
	return 0;
}
void VIB_init(void) {
	vib.queued_strength = vib.strength = 0;
	pthread_create(&vib.pt, NULL, &VIB_thread, NULL);
	vib.initialized = 1;
}
void VIB_quit(void) {
	if (!vib.initialized) return;
	
	VIB_setStrength(0);
	pthread_cancel(vib.pt);
	pthread_join(vib.pt, NULL);
}
void VIB_setStrength(int strength) {
	if (vib.queued_strength==strength) return;
	vib.queued_strength = strength;
}
int VIB_getStrength(void) {
	return vib.strength;
}

///////////////////////////////

static void PWR_initOverlay(void) {
	// setup surface
	pwr.overlay = PLAT_initOverlay();

	// draw battery
	SDLX_SetAlpha(gfx.assets, 0,0);
	GFX_blitAsset(ASSET_BLACK_PILL, NULL, pwr.overlay, NULL);
	SDLX_SetAlpha(gfx.assets, SDL_SRCALPHA,0);
	GFX_blitBattery(pwr.overlay, NULL);
}

static void PWR_updateBatteryStatus(void) {
	PLAT_getBatteryStatus(&pwr.is_charging, &pwr.charge);
	PLAT_enableOverlay(pwr.should_warn && pwr.charge<=PWR_LOW_CHARGE);
}

static void* PWR_monitorBattery(void *arg) {
	while(1) {
		// TODO: the frequency of checking could depend on whether 
		// we're in game (less frequent) or menu (more frequent)
		sleep(5);
		PWR_updateBatteryStatus();
	}
	return NULL;
}

void PWR_init(void) {
	pwr.can_sleep = 1;
	pwr.can_poweroff = 1;
	pwr.can_autosleep = 1;
	
	pwr.requested_sleep = 0;
	pwr.requested_wake = 0;
	
	pwr.should_warn = 0;
	pwr.charge = PWR_LOW_CHARGE;
	
	PWR_initOverlay();

	PWR_updateBatteryStatus();
	pthread_create(&pwr.battery_pt, NULL, &PWR_monitorBattery, NULL);
	pwr.initialized = 1;
}
void PWR_quit(void) {
	if (!pwr.initialized) return;
	
	PLAT_quitOverlay();
	
	// cancel battery thread
	pthread_cancel(pwr.battery_pt);
	pthread_join(pwr.battery_pt, NULL);
}
void PWR_warn(int enable) {
	pwr.should_warn = enable;
	PLAT_enableOverlay(pwr.should_warn && pwr.charge<=PWR_LOW_CHARGE);
}

int PWR_ignoreSettingInput(int btn, int show_setting) {
	return show_setting && (btn==BTN_MOD_PLUS || btn==BTN_MOD_MINUS);
}

void PWR_update(int* _dirty, int* _show_setting, PWR_callback_t before_sleep, PWR_callback_t after_sleep) {
	int dirty = _dirty ? *_dirty : 0;
	int show_setting = _show_setting ? *_show_setting : 0;
	
	static uint32_t last_input_at = 0; // timestamp of last input (autosleep)
	static uint32_t checked_charge_at = 0; // timestamp of last time checking charge
	static uint32_t setting_shown_at = 0; // timestamp when settings started being shown
	static uint32_t power_pressed_at = 0; // timestamp when power button was just pressed
	static uint32_t mod_unpressed_at = 0; // timestamp of last time settings modifier key was NOT down
	static uint32_t was_muted = -1;
	if (was_muted==-1) was_muted = GetMute();
	
	static int was_charging = -1;
	if (was_charging==-1) was_charging = pwr.is_charging;

	uint32_t now = SDL_GetTicks();
	if (was_charging || PAD_anyPressed() || last_input_at==0) last_input_at = now;
	
	#define CHARGE_DELAY 1000
	if (dirty || now-checked_charge_at>=CHARGE_DELAY) {
		int is_charging = pwr.is_charging;
		if (was_charging!=is_charging) {
			was_charging = is_charging;
			dirty = 1;
		}
		checked_charge_at = now;
	}
	
	if (PAD_justReleased(BTN_POWEROFF) || (power_pressed_at && now-power_pressed_at>=1000)) {
		if (before_sleep) before_sleep();
		PWR_powerOff();
	}
	
	if (PAD_justPressed(BTN_POWER)) {
		power_pressed_at = now;
	}
	
	#define SLEEP_DELAY 30000 // 30 seconds
	if (now-last_input_at>=SLEEP_DELAY && PWR_preventAutosleep()) last_input_at = now;
	
	if (
		pwr.requested_sleep || // hardware requested sleep
		now-last_input_at>=SLEEP_DELAY || // autosleep
		(pwr.can_sleep && PAD_justReleased(BTN_SLEEP)) // manual sleep
	) {
		pwr.requested_sleep = 0;
		if (before_sleep) before_sleep();
		PWR_fauxSleep();
		if (after_sleep) after_sleep();
		
		last_input_at = now = SDL_GetTicks();
		power_pressed_at = 0;
		dirty = 1;
	}
	
	int was_dirty = dirty; // dirty list (not including settings/battery)
	
	// TODO: only delay hiding setting changes if that setting didn't require a modifier button be held, otherwise release as soon as modifier is released
	
	int delay_settings = BTN_MOD_BRIGHTNESS==BTN_MENU; // when both volume and brighness require a modifier hide settings as soon as it is released
	#define SETTING_DELAY 500
	if (show_setting && (now-setting_shown_at>=SETTING_DELAY || !delay_settings) && !PAD_isPressed(BTN_MOD_VOLUME) && !PAD_isPressed(BTN_MOD_BRIGHTNESS)) {
		show_setting = 0;
		dirty = 1;
	}
	
	if (!show_setting && !PAD_isPressed(BTN_MOD_VOLUME) && !PAD_isPressed(BTN_MOD_BRIGHTNESS)) {
		mod_unpressed_at = now; // this feels backwards but is correct
	}
	
	#define MOD_DELAY 250
	if (
		(
			(PAD_isPressed(BTN_MOD_VOLUME) || PAD_isPressed(BTN_MOD_BRIGHTNESS)) && 
			(!delay_settings || now-mod_unpressed_at>=MOD_DELAY)
		) || 
		((!BTN_MOD_VOLUME || !BTN_MOD_BRIGHTNESS) && (PAD_justRepeated(BTN_MOD_PLUS) || PAD_justRepeated(BTN_MOD_MINUS)))
	) {
		setting_shown_at = now;
		if (PAD_isPressed(BTN_MOD_BRIGHTNESS)) {
			show_setting = 1;
		}
		else {
			show_setting = 2;
		}
	}
	
	int muted = GetMute();
	if (muted!=was_muted) {
		was_muted = muted;
		show_setting = 2;
		setting_shown_at = now;
	}
	
	if (show_setting) dirty = 1; // shm is slow or keymon is catching input on the next frame
	if (_dirty) *_dirty = dirty;
	if (_show_setting) *_show_setting = show_setting;
}

// TODO: this isn't whether it can sleep but more if it should sleep in response to the sleep button
void PWR_disableSleep(void) {
	pwr.can_sleep = 0;
}
void PWR_enableSleep(void) {
	pwr.can_sleep = 1;
}

void PWR_disablePowerOff(void) {
	pwr.can_poweroff = 0;
}
void PWR_powerOff(void) {
	if (pwr.can_poweroff) {
		
		int w = FIXED_WIDTH;
		int h = FIXED_HEIGHT;
		int p = FIXED_PITCH;
		if (GetHDMI()) {
			w = HDMI_WIDTH;
			h = HDMI_HEIGHT;
			p = HDMI_PITCH;
		}
		gfx.screen = GFX_resize(w,h,p);
		
		char* msg;
		if (HAS_POWER_BUTTON || HAS_POWEROFF_BUTTON) msg = exists(AUTO_RESUME_PATH) ? "Partida guardada,\napagando" : "Apagando";
		else msg = exists(AUTO_RESUME_PATH) ? "Quicksave created,\npower off now" : "Power off now";
		
		// LOG_info("PWR_powerOff %s (%ix%i)\n", gfx.screen, gfx.screen->w, gfx.screen->h);

		// TODO: for some reason screen's dimensions end up being 0x0 in GFX_blitMessage...
		PLAT_clearVideo(gfx.screen);
		if (THEME_pillStyle() == THEME_PILL_PSX && THEME_hasBackdrop() && !GetHDMI())
			THEME_blitBackground(gfx.screen);
		GFX_blitMessage(font.large, msg, gfx.screen,&(SDL_Rect){0,0,gfx.screen->w,gfx.screen->h}); //, NULL);
		GFX_flip(gfx.screen);
		PLAT_powerOff();
	}
}

static void PWR_enterSleep(void) {
	SDL_PauseAudio(1);
	if (GetHDMI()) {
		PLAT_clearVideo(gfx.screen);
		PLAT_flip(gfx.screen, 0);
	}
	else {
		SetRawVolume(MUTE_VOLUME_RAW);
		PLAT_enableBacklight(0);
	}
	system("killall -STOP keymon.elf");
	
	sync();
}
static void PWR_exitSleep(void) {
	system("killall -CONT keymon.elf");
	if (GetHDMI()) {
		// buh
	}
	else {
		PLAT_enableBacklight(1);
		SetVolume(GetVolume());
	}
	SDL_PauseAudio(0);
	
	sync();
}

static void PWR_waitForWake(void) {
	uint32_t sleep_ticks = SDL_GetTicks();
	while (!PAD_wake()) {
		if (pwr.requested_wake) {
			pwr.requested_wake = 0;
			break;
		}
		SDL_Delay(200);
		if (pwr.can_poweroff && SDL_GetTicks()-sleep_ticks>=120000) { // increased to two minutes
			if (pwr.is_charging) sleep_ticks += 60000; // check again in a minute
			else PWR_powerOff();
		}
	}
	
	return;
}
void PWR_fauxSleep(void) {
	GFX_clear(gfx.screen);
	PAD_reset();
	PWR_enterSleep();
	PWR_waitForWake();
	PWR_exitSleep();
	PAD_reset();
}

void PWR_disableAutosleep(void) {
	pwr.can_autosleep = 0;
}
void PWR_enableAutosleep(void) {
	pwr.can_autosleep = 1;
}
int PWR_preventAutosleep(void) {
	return pwr.is_charging || !pwr.can_autosleep || GetHDMI();
}

// updated by PWR_updateBatteryStatus()
int PWR_isCharging(void) {
	return pwr.is_charging;
}
int PWR_getBattery(void) { // 10-100 in 10-20% fragments
	return pwr.charge;
}

///////////////////////////////

// TODO: tmp? move to individual platforms or allow overriding like PAD_poll/PAD_wake?
int PLAT_setDateTime(int y, int m, int d, int h, int i, int s) {
	char cmd[512];
	sprintf(cmd, "date -s '%d-%d-%d %d:%d:%d'; hwclock --utc -w", y,m,d,h,i,s);
	system(cmd);
	return 0; // why does this return an int?
}
