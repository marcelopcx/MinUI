#ifndef LOGROS_H
#define LOGROS_H

// RetroAchievements sin conexión: los logros se detectan en la consola con los
// datos que "Sincronizar" dejó en LOGROS_PATH, y los desbloqueos se guardan en
// una cola que la misma sincronización envía a retroachievements.org.

#include <stddef.h>
#include <stdint.h>
#include "sdl.h"

#define LOGROS_PATH SHARED_USERDATA_PATH "/.ra"

// minarch.elf --ra-hash <TAG> <rom>: imprime el hash de RetroAchievements
int  Logros_hashMain(int argc, char* argv[]);

void Logros_init(void* (*get_memory_data)(unsigned id), size_t (*get_memory_size)(unsigned id), double sample_rate);
void Logros_setMemoryMap(const void* mmap);
void Logros_loadGame(const char* path, const void* data, size_t size, const char* tag);
void Logros_doFrame(void);
void Logros_reset(void);
void Logros_stateSaved(const char* state_path);
void Logros_stateLoaded(const char* state_path);
int  Logros_draw(SDL_Surface* dst); // 1 cuando un aviso desaparece (hay que limpiar la pantalla)
int  Logros_resumen(char* out, size_t size); // "Logros: 3 de 40"; 0 si el juego no tiene
int  Logros_sonando(void);
void Logros_mezclar(int16_t* frames, size_t count); // suma el sonido de logro (estéreo)
void Logros_quit(void);

#endif
