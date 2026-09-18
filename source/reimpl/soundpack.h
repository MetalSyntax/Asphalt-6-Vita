/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  soundpack.h
 * @brief Lector de `file00a.bin` (143 MB, el pack de audio del juego).
 *
 * Todos los `.wav` que `vox::DriverAndroid`/`GLMediaPlayer` piden por nombre relativo
 * (`sfx_menu_start_race.wav`, `vfx_car_ferrari_458_italia.wav`, etc. -- ver CLAUDE.md)
 * fallan con `fopen(...): 0x0` en el log porque NO son archivos sueltos: viven empaquetados
 * dentro de `file00a.bin`. Formato confirmado leyendo los bytes crudos del archivo real
 * (no documentado en ningún lado, Gameloft-propio): una secuencia plana de entradas
 * autocontenidas, cada una con su propia marca:
 *
 *   char     magic[4];   // "QL\x04\x05"
 *   uint32_t ver;         // 10 en este build
 *   uint16_t flags;       // 0 en todas las entradas vistas
 *   uint32_t hash1, hash2;// hash del nombre (algoritmo sin confirmar, no se necesita:
 *                         // se busca por nombre, no por hash)
 *   uint32_t sizeA, sizeB;// tamaño de los datos (iguales en las 630 entradas vistas --
 *                         // sin compresión)
 *   uint32_t nameLen;
 *   char     name[nameLen];
 *   uint8_t  data[sizeA]; // el archivo real (WAV con su propio header RIFF), sin envolver
 *
 * 630 entradas confirmadas escaneando el archivo real completo con un script Python,
 * verificado además contra el chunk `data` del WAV de la primera entrada (tamaño de la
 * entrada = 44 bytes de header RIFF + tamaño del chunk `data`, coincide exacto).
 */

#ifndef SOLOADER_SOUNDPACK_H
#define SOLOADER_SOUNDPACK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[64];
    long dataOffset;      // offset absoluto en file00a.bin donde arrancan los datos
    uint32_t dataSize;
} SoundPackEntry;

// Escanea file00a.bin una sola vez (llamada perezosa, con guarda interna -- no hace falta
// invocarla a mano). Devuelve 0 si el pack quedo listo para usarse.
int soundpack_init(void);

// Busca una entrada por nombre exacto (case-sensitive, tal cual la pide el motor).
// Devuelve NULL si no esta en el pack.
const SoundPackEntry *soundpack_find(const char *name);

// Lee los datos crudos (el archivo WAV completo, con su propio header RIFF) de una entrada
// ya encontrada con soundpack_find(). `dest` debe tener al menos entry->dataSize bytes.
// Devuelve 1 si se leyo todo, 0 si algo fallo (fopen/fseek/fread).
int soundpack_read(const SoundPackEntry *entry, void *dest);

#ifdef __cplusplus
}
#endif

#endif // SOLOADER_SOUNDPACK_H
