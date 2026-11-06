// RetroAchievements sin conexión para minarch.
//
// minarch nunca habla con internet. rc_client cree que habla con el servidor,
// pero servidor() le contesta con archivos de LOGROS_PATH:
//   login.json             respuesta de login2 (la deja Sincronizar)
//   juegos/<hash>.json     definiciones de logros de cada juego (achievementsets)
//   web/<id>.json          logros ya desbloqueados en la web (respuesta de r=unlocks)
//   locales/<id>.txt       desbloqueados en la consola aún no confirmados ("id hora")
//   pendientes.txt         cola para la web ("hora logro hardcore hash juego firma")
//   sin-datos.txt          hashes de juegos abiertos que aún no tienen datos

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

#include "defines.h"
#include "api.h"
#include "logros.h"
#include "chd_reader.h"

#include <rc_client.h>
#include <rc_hash.h>
#include <rc_consoles.h>
#include "rc_libretro.h"

#define AVISO_MS 4000

static rc_client_t* client;
static rc_libretro_memory_regions_t regiones;
static int regiones_listas;
static struct retro_memory_map* mapa;
static void* (*core_get_memory_data)(unsigned id);
static size_t (*core_get_memory_size)(unsigned id);

static struct {
	char texto[256];
	uint32_t hasta;
	SDL_Surface* superficie;
	int visible;
} aviso;

///////////////////////////////

static char* leer(const char* path, size_t* len) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	rewind(f);
	char* buf = malloc(n + 1);
	if (buf && fread(buf, 1, n, f) != (size_t)n) { free(buf); buf = NULL; }
	fclose(f);
	if (!buf) return NULL;
	buf[n] = '\0';
	if (len) *len = n;
	return buf;
}
static void anotar(const char* path, const char* linea) {
	FILE* f = fopen(path, "a");
	if (!f) return;
	fputs(linea, f);
	fclose(f);
	sync();
}
static void parametro(const char* post, const char* nombre, char* out, size_t out_size) {
	size_t n = strlen(nombre);
	const char* p = post;
	out[0] = '\0';
	while (p && *p) {
		if (!strncmp(p, nombre, n) && p[n]=='=') {
			p += n + 1;
			size_t i = 0;
			while (*p && *p!='&' && i<out_size-1) out[i++] = *p++;
			out[i] = '\0';
			return;
		}
		p = strchr(p, '&');
		if (p) p++;
	}
}
static int json_texto(const char* json, const char* clave, char* out, size_t out_size) {
	char patron[64];
	snprintf(patron, sizeof(patron), "\"%s\":\"", clave);
	const char* p = strstr(json, patron);
	if (!p) return 0;
	p += strlen(patron);
	size_t i = 0;
	while (*p && *p!='"' && i<out_size-1) out[i++] = *p++;
	out[i] = '\0';
	return i>0;
}

static void avisar(const char* texto) {
	snprintf(aviso.texto, sizeof(aviso.texto), "%s", texto);
	aviso.hasta = SDL_GetTicks() + AVISO_MS;
	aviso.visible = 0; // la superficie se crea en el hilo de vídeo
	LOG_info("logros: %s\n", texto);
}

///////////////////////////////
// lectura de discos .chd para identificar juegos de PlayStation

typedef struct { int chd; void* h; } Pista;
static rc_hash_cdreader_t lector_cd;

static void* abrir_pista(const char* path, uint32_t track, const rc_hash_iterator_t* it) {
	Pista* p = calloc(1, sizeof(Pista));
	if (!p) return NULL;
	p->chd = chd_reader_is_chd(path);
	if (p->chd) p->h = chd_open_track_iterator(path, track, it);
	else if (lector_cd.open_track_iterator) p->h = lector_cd.open_track_iterator(path, track, it);
	else p->h = lector_cd.open_track(path, track);
	if (!p->h) { free(p); return NULL; }
	return p;
}
static size_t leer_sector(void* h, uint32_t sector, void* buf, size_t n) {
	Pista* p = h;
	return p->chd ? chd_read_sector(p->h, sector, buf, n) : lector_cd.read_sector(p->h, sector, buf, n);
}
static void cerrar_pista(void* h) {
	Pista* p = h;
	if (p->chd) chd_close_track(p->h);
	else lector_cd.close_track(p->h);
	free(p);
}
static uint32_t primer_sector(void* h) {
	Pista* p = h;
	return p->chd ? chd_first_track_sector(p->h) : lector_cd.first_track_sector(p->h);
}
static void* abrir_pista_simple(const char* path, uint32_t track) {
	return abrir_pista(path, track, NULL);
}
static void preparar_hash(rc_hash_callbacks_t* cb) {
	rc_hash_get_default_cdreader(&lector_cd);
	// rc_client solo copia el lector de discos si open_track está definido
	cb->cdreader.open_track = abrir_pista_simple;
	cb->cdreader.open_track_iterator = abrir_pista;
	cb->cdreader.read_sector = leer_sector;
	cb->cdreader.close_track = cerrar_pista;
	cb->cdreader.first_track_sector = primer_sector;
}

static uint32_t consola(const char* tag, const char* path) {
	static const struct { const char* tag; uint32_t id; } tabla[] = {
		{"GB", RC_CONSOLE_GAMEBOY}, {"GBC", RC_CONSOLE_GAMEBOY_COLOR}, {"GBA", RC_CONSOLE_GAMEBOY_ADVANCE},
		{"MGBA", RC_CONSOLE_GAMEBOY_ADVANCE}, {"SGB", RC_CONSOLE_GAMEBOY},
		{"FC", RC_CONSOLE_NINTENDO}, {"FDS", RC_CONSOLE_FAMICOM_DISK_SYSTEM},
		{"SFC", RC_CONSOLE_SUPER_NINTENDO}, {"SUPA", RC_CONSOLE_SUPER_NINTENDO},
		{"MD", RC_CONSOLE_MEGA_DRIVE}, {"32X", RC_CONSOLE_SEGA_32X}, {"SMS", RC_CONSOLE_MASTER_SYSTEM},
		{"GG", RC_CONSOLE_GAME_GEAR}, {"SG1000", RC_CONSOLE_SG1000},
		{"PS", RC_CONSOLE_PLAYSTATION}, {"PCE", RC_CONSOLE_PC_ENGINE},
		{"NGP", RC_CONSOLE_NEOGEO_POCKET}, {"NGPC", RC_CONSOLE_NEOGEO_POCKET},
		{"VB", RC_CONSOLE_VIRTUAL_BOY}, {"PKM", RC_CONSOLE_POKEMON_MINI}, {"LYNX", RC_CONSOLE_ATARI_LYNX},
		{"WS", RC_CONSOLE_WONDERSWAN}, {"WSC", RC_CONSOLE_WONDERSWAN},
		{"A2600", RC_CONSOLE_ATARI_2600}, {"A7800", RC_CONSOLE_ATARI_7800}, {"COLECO", RC_CONSOLE_COLECOVISION},
		{"CPS1", RC_CONSOLE_ARCADE}, {"CPS2", RC_CONSOLE_ARCADE}, {"CPS3", RC_CONSOLE_ARCADE},
		{"NEOGEO", RC_CONSOLE_ARCADE}, {"FBN", RC_CONSOLE_ARCADE}, {"MAME", RC_CONSOLE_ARCADE},
		{NULL, 0}
	};
	for (int i=0; tabla[i].tag; i++) {
		if (strcasecmp(tag, tabla[i].tag)) continue;
		uint32_t id = tabla[i].id;
		const char* ext = strrchr(path, '.');
		if (id==RC_CONSOLE_MEGA_DRIVE && ext && !strcasecmp(ext, ".32x")) id = RC_CONSOLE_SEGA_32X;
		if (id==RC_CONSOLE_PC_ENGINE && ext && (!strcasecmp(ext, ".chd") || !strcasecmp(ext, ".cue"))) id = RC_CONSOLE_PC_ENGINE_CD;
		return id;
	}
	return RC_CONSOLE_UNKNOWN;
}

int Logros_hashMain(int argc, char* argv[]) {
	if (argc<4) {
		fprintf(stderr, "uso: %s --ra-hash <TAG> <rom>\n", argv[0]);
		return 1;
	}
	uint32_t id = consola(argv[2], argv[3]);
	if (id==RC_CONSOLE_UNKNOWN) return 2;
	char hash[33] = {0};
	const char* ext = strrchr(argv[3], '.');
	if (id!=RC_CONSOLE_ARCADE && ext && !strcasecmp(ext, ".zip")) return 4; // se identifica al jugarlo
	rc_hash_iterator_t it;
	rc_hash_initialize_iterator(&it, argv[3], NULL, 0);
	preparar_hash(&it.callbacks);
	int ok = rc_hash_generate(hash, id, &it);
	rc_hash_destroy_iterator(&it);
	if (!ok) return 3;
	printf("%s\n", hash);
	return 0;
}

///////////////////////////////
// el "servidor" sin conexión

static char* sesion(const char* juego) {
	char path[MAX_PATH];
	char* entradas = calloc(1, 1);
	size_t largo = 0;
	unsigned id, cuando;
	char e[64];

	// desbloqueados en la consola que la web aún no conoce
	snprintf(path, sizeof(path), LOGROS_PATH "/locales/%s.txt", juego);
	FILE* f = fopen(path, "r");
	if (f) {
		while (fscanf(f, "%u %u", &id, &cuando)==2) {
			int n = snprintf(e, sizeof(e), "%s{\"ID\":%u,\"When\":%u}", largo?",":"", id, cuando);
			entradas = realloc(entradas, largo + n + 1);
			strcpy(entradas + largo, e);
			largo += n;
		}
		fclose(f);
	}

	// desbloqueados en la web (respuesta de r=unlocks guardada al sincronizar)
	snprintf(path, sizeof(path), LOGROS_PATH "/web/%s.json", juego);
	char* web = leer(path, NULL);
	char* p = web ? strstr(web, "\"UserUnlocks\":[") : NULL;
	if (p) {
		p += strlen("\"UserUnlocks\":[");
		while (*p && *p!=']') {
			char* fin;
			id = strtoul(p, &fin, 10);
			if (fin==p) { p++; continue; }
			p = fin;
			int n = snprintf(e, sizeof(e), "%s{\"ID\":%u,\"When\":0}", largo?",":"", id);
			entradas = realloc(entradas, largo + n + 1);
			strcpy(entradas + largo, e);
			largo += n;
		}
	}
	free(web);

	char* out = malloc(largo + 128);
	sprintf(out, "{\"Success\":true,\"Unlocks\":[%s],\"HardcoreUnlocks\":[],\"ServerNow\":%u}", entradas, (unsigned)time(NULL));
	free(entradas);
	return out;
}

static char* premiar(const char* post) {
	char logro[16], hardcore[4], hash[40], linea[128], path[MAX_PATH];
	parametro(post, "a", logro, sizeof(logro));
	parametro(post, "h", hardcore, sizeof(hardcore));
	parametro(post, "m", hash, sizeof(hash));
	unsigned ahora = (unsigned)time(NULL);

	const rc_client_game_t* juego = rc_client_get_game_info(client);
	unsigned juego_id = juego ? juego->id : 0;

	char firma[40];
	parametro(post, "v", firma, sizeof(firma));
	snprintf(linea, sizeof(linea), "%u %s %s %s %u %s\n", ahora, logro, hardcore[0]?hardcore:"0", hash[0]?hash:"-", juego_id, firma[0]?firma:"-");
	anotar(LOGROS_PATH "/pendientes.txt", linea);

	if (juego_id) {
		snprintf(path, sizeof(path), LOGROS_PATH "/locales/%u.txt", juego_id);
		snprintf(linea, sizeof(linea), "%s %u\n", logro, ahora);
		anotar(path, linea);
	}

	char* out = malloc(160);
	sprintf(out, "{\"Success\":true,\"Score\":0,\"SoftcoreScore\":0,\"AchievementID\":%s,\"AchievementsRemaining\":1}", logro);
	return out;
}

static void servidor(const rc_api_request_t* req, rc_client_server_callback_t callback, void* data, rc_client_t* c) {
	const char* post = req->post_data ? req->post_data : "";
	char r[32], valor[64], path[MAX_PATH];
	char* body = NULL;
	parametro(post, "r", r, sizeof(r));

	if (!strcmp(r, "login2")) {
		body = leer(LOGROS_PATH "/login.json", NULL);
	}
	else if (!strcmp(r, "achievementsets")) {
		parametro(post, "m", valor, sizeof(valor));
		snprintf(path, sizeof(path), LOGROS_PATH "/juegos/%s.json", valor);
		body = leer(path, NULL);
		if (!body && valor[0]) {
			char linea[80];
			snprintf(linea, sizeof(linea), "%s\n", valor);
			char* ya = leer(LOGROS_PATH "/sin-datos.txt", NULL);
			if (!ya || !strstr(ya, valor)) anotar(LOGROS_PATH "/sin-datos.txt", linea);
			free(ya);
		}
	}
	else if (!strcmp(r, "startsession")) {
		parametro(post, "g", valor, sizeof(valor));
		body = sesion(valor);
	}
	else if (!strcmp(r, "awardachievement")) {
		body = premiar(post);
	}
	else {
		body = strdup("{\"Success\":true}"); // ping, marcadores...: se ignoran sin conexión
	}

	if (!body) body = strdup("{\"Success\":false,\"Error\":\"Sin datos: sincroniza con Wi-Fi\"}");

	rc_api_server_response_t res;
	res.body = body;
	res.body_length = strlen(body);
	res.http_status_code = 200;
	callback(&res, data);
	free(body);
}

///////////////////////////////
// sonido de logro: dos notas rápidas (re y la) con brillo de octava y caída
// suave, al estilo de los avisos de logro de las consolas. Se sintetiza al
// iniciar y se mezcla con el audio del juego.

static int16_t* sonido;
static int sonido_largo;
static volatile int sonido_pos = -1;

static void crear_sonido(double rate) {
	if (rate<8000) rate = 44100;
	sonido_largo = (int)(rate * 0.75);
	sonido = calloc(sonido_largo, sizeof(int16_t));
	if (!sonido) return;
	const struct { double inicio, frecuencia, volumen, caida; } notas[] = {
		{0.00, 587.33, 0.55, 9.0},  // re
		{0.09, 880.00, 0.60, 5.0},  // la
		{0.09, 1760.0, 0.18, 7.0},  // brillo
	};
	for (int i=0; i<sonido_largo; i++) {
		double t = i / rate, v = 0;
		for (int n=0; n<3; n++) {
			double d = t - notas[n].inicio;
			if (d<0) continue;
			double ataque = d<0.006 ? d/0.006 : 1; // sin chasquido al empezar
			v += sin(2 * M_PI * notas[n].frecuencia * d) * notas[n].volumen * ataque * exp(-d * notas[n].caida);
		}
		sonido[i] = (int16_t)(v * 0.45 * 32767);
	}
}

int Logros_sonando(void) {
	return sonido_pos>=0;
}

void Logros_mezclar(int16_t* frames, size_t count) {
	int pos = sonido_pos;
	if (pos<0 || !sonido) return;
	for (size_t i=0; i<count && pos<sonido_largo; i++, pos++) {
		for (int c=0; c<2; c++) {
			int v = frames[i*2+c] + sonido[pos];
			frames[i*2+c] = v>32767 ? 32767 : (v<-32768 ? -32768 : v);
		}
	}
	sonido_pos = pos<sonido_largo ? pos : -1;
}

int Logros_resumen(char* out, size_t size) {
	if (!client || !rc_client_get_game_info(client)) return 0;
	rc_client_user_game_summary_t r;
	rc_client_get_user_game_summary(client, &r);
	if (!r.num_core_achievements) return 0;
	snprintf(out, size, "Logros: %u de %u", r.num_unlocked_achievements, r.num_core_achievements);
	return 1;
}

///////////////////////////////
// memoria del núcleo

static void info_memoria(uint32_t id, rc_libretro_core_memory_info_t* info) {
	info->data = core_get_memory_data ? core_get_memory_data(id) : NULL;
	info->size = core_get_memory_size ? core_get_memory_size(id) : 0;
}
static uint32_t leer_memoria(uint32_t address, uint8_t* buffer, uint32_t num_bytes, rc_client_t* c) {
	if (!regiones_listas) {
		const rc_client_game_t* juego = rc_client_get_game_info(client);
		if (!juego || !juego->console_id) return 0;
		regiones_listas = rc_libretro_memory_init(&regiones, mapa, info_memoria, juego->console_id) ? 1 : -1;
	}
	if (regiones_listas<0) return 0;
	return rc_libretro_memory_read(&regiones, address, buffer, num_bytes);
}

///////////////////////////////

static void evento(const rc_client_event_t* e, rc_client_t* c) {
	char texto[256];
	switch (e->type) {
		case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
			snprintf(texto, sizeof(texto), "Logro desbloqueado: %s", e->achievement->title);
			avisar(texto);
			sonido_pos = 0;
			break;
		case RC_CLIENT_EVENT_GAME_COMPLETED:
			avisar("¡Todos los logros conseguidos!");
			break;
		default:
			break;
	}
}

static void juego_cargado(int result, const char* error, rc_client_t* c, void* userdata) {
	// el contador se ve en el menú del juego (Logros_resumen), no al empezar
	if (result!=RC_OK) LOG_info("logros: juego sin datos (%s)\n", error ? error : "?");
}

static void registro(const char* mensaje, const rc_client_t* c) {
	LOG_info("rcheevos: %s\n", mensaje);
}

static void sesion_iniciada(int result, const char* error, rc_client_t* c, void* userdata) {
	if (result!=RC_OK) LOG_info("logros: no se pudo iniciar sesión (%s)\n", error ? error : "?");
}

void Logros_init(void* (*get_memory_data)(unsigned id), size_t (*get_memory_size)(unsigned id), double sample_rate) {
	crear_sonido(sample_rate);
	core_get_memory_data = get_memory_data;
	core_get_memory_size = get_memory_size;

	char* login = leer(LOGROS_PATH "/login.json", NULL);
	if (!login) return; // sin cuenta configurada
	char usuario[64], token[64];
	int ok = json_texto(login, "User", usuario, sizeof(usuario)) && json_texto(login, "Token", token, sizeof(token));
	free(login);
	if (!ok) return;

	client = rc_client_create(leer_memoria, servidor);
	if (!client) return;
	rc_client_enable_logging(client, RC_CLIENT_LOG_LEVEL_INFO, registro);
	rc_client_set_hardcore_enabled(client, 0);
	rc_client_set_event_handler(client, evento);

	rc_hash_callbacks_t cb;
	memset(&cb, 0, sizeof(cb));
	preparar_hash(&cb);
	rc_client_set_hash_callbacks(client, &cb);

	mkdir(LOGROS_PATH "/locales", 0755);
	rc_client_begin_login_with_token(client, usuario, token, sesion_iniciada, NULL);
}

void Logros_setMemoryMap(const void* mmap) {
	const struct retro_memory_map* src = mmap;
	if (mapa) {
		free((void*)mapa->descriptors);
		free(mapa);
		mapa = NULL;
	}
	if (!src || !src->num_descriptors) return;
	mapa = malloc(sizeof(*mapa));
	struct retro_memory_descriptor* d = malloc(src->num_descriptors * sizeof(*d));
	memcpy(d, src->descriptors, src->num_descriptors * sizeof(*d));
	mapa->descriptors = d;
	mapa->num_descriptors = src->num_descriptors;
}

void Logros_loadGame(const char* path, const void* data, size_t size, const char* tag) {
	if (!client) return;
	regiones_listas = 0;
	rc_client_begin_identify_and_load_game(client, consola(tag, path), path, data, size, juego_cargado, NULL);
}

void Logros_doFrame(void) {
	if (client) rc_client_do_frame(client);
}

void Logros_reset(void) {
	if (client) rc_client_reset(client);
}

// el progreso de los logros se guarda junto a cada estado para que cargar
// una partida no dispare (ni pierda) logros a medio cumplir
void Logros_stateSaved(const char* state_path) {
	if (!client || !rc_client_get_game_info(client)) return;
	size_t n = rc_client_progress_size(client);
	if (!n) return;
	uint8_t* buf = malloc(n);
	if (buf && rc_client_serialize_progress_sized(client, buf, n)==RC_OK) {
		char path[MAX_PATH];
		snprintf(path, sizeof(path), "%s.ra", state_path);
		FILE* f = fopen(path, "wb");
		if (f) { fwrite(buf, 1, n, f); fclose(f); }
	}
	free(buf);
}
void Logros_stateLoaded(const char* state_path) {
	if (!client) return;
	char path[MAX_PATH];
	size_t n = 0;
	snprintf(path, sizeof(path), "%s.ra", state_path);
	char* buf = leer(path, &n);
	if (!buf || rc_client_deserialize_progress_sized(client, (uint8_t*)buf, n)!=RC_OK) rc_client_reset(client);
	free(buf);
}

int Logros_draw(SDL_Surface* dst) {
	if (!aviso.texto[0]) return 0;
	if (SDL_GetTicks()>aviso.hasta) {
		aviso.texto[0] = '\0';
		if (aviso.superficie) SDL_FreeSurface(aviso.superficie);
		aviso.superficie = NULL;
		aviso.visible = 0;
		return 1;
	}
	if (!aviso.visible) {
		if (aviso.superficie) SDL_FreeSurface(aviso.superficie);
		aviso.superficie = TTF_RenderUTF8_Blended(font.small, aviso.texto, COLOR_WHITE);
		aviso.visible = 1;
	}
	if (!aviso.superficie) return 0;
	int w = aviso.superficie->w + SCALE1(PADDING * 2);
	int max = dst->w - SCALE1(PADDING * 2);
	if (w>max) w = max;
	int h = SCALE1(PILL_SIZE);
	int x = (dst->w - w) / 2;
	int y = SCALE1(PADDING);
	GFX_blitPill(ASSET_BLACK_PILL, dst, &(SDL_Rect){x, y, w, h});
	SDL_BlitSurface(aviso.superficie, &(SDL_Rect){0, 0, w - SCALE1(PADDING * 2), aviso.superficie->h}, dst,
		&(SDL_Rect){x + SCALE1(PADDING), y + (h - aviso.superficie->h) / 2});
	return 0;
}

void Logros_quit(void) {
	if (client) {
		rc_client_unload_game(client);
		rc_client_destroy(client);
		client = NULL;
	}
	if (regiones_listas>0) rc_libretro_memory_destroy(&regiones);
	regiones_listas = 0;
	Logros_setMemoryMap(NULL);
	if (aviso.superficie) SDL_FreeSurface(aviso.superficie);
	aviso.superficie = NULL;
	sonido_pos = -1;
	free(sonido);
	sonido = NULL;
}
