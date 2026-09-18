/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/soundpack.h"

#include "utils/logger.h"

#include <stdio.h>
#include <string.h>

#define SOUNDPACK_PATH DATA_PATH "data/file00a.bin"
#define SOUNDPACK_MAGIC "QL\x04\x05"
#define SOUNDPACK_MAX_ENTRIES 1024

static SoundPackEntry g_entries[SOUNDPACK_MAX_ENTRIES];
static int g_entry_count = 0;
static int g_ready = 0;      // 1 = escaneado con exito, -1 = fallo (no reintentar)

int soundpack_init(void) {
    if (g_ready) return g_ready > 0 ? 0 : -1;

    FILE *f = fopen(SOUNDPACK_PATH, "rb");
    if (!f) {
        l_error("[soundpack] no se pudo abrir %s", SOUNDPACK_PATH);
        g_ready = -1;
        return -1;
    }

    long off = 0;
    int count = 0;
    while (count < SOUNDPACK_MAX_ENTRIES) {
        // Header de la entrada: magic(4) + ver(4) + flags(2) + hash1(4) + hash2(4) +
        // sizeA(4) + sizeB(4) + nameLen(4) = 30 bytes, ver comentario de soundpack.h.
        unsigned char hdr[30];
        if (fseek(f, off, SEEK_SET) != 0) break;
        if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) break;
        if (memcmp(hdr, SOUNDPACK_MAGIC, 4) != 0) break;

        uint32_t sizeA   = (uint32_t) hdr[18] | ((uint32_t) hdr[19] << 8) |
                           ((uint32_t) hdr[20] << 16) | ((uint32_t) hdr[21] << 24);
        uint32_t nameLen = (uint32_t) hdr[26] | ((uint32_t) hdr[27] << 8) |
                           ((uint32_t) hdr[28] << 16) | ((uint32_t) hdr[29] << 24);

        if (nameLen == 0 || nameLen >= sizeof(g_entries[0].name)) break;

        char name[sizeof(g_entries[0].name)];
        if (fread(name, 1, nameLen, f) != nameLen) break;
        name[nameLen] = '\0';

        strcpy(g_entries[count].name, name);
        g_entries[count].dataOffset = off + 30 + (long) nameLen;
        g_entries[count].dataSize = sizeA;
        count++;

        off += 30 + (long) nameLen + (long) sizeA;
    }

    fclose(f);

    g_entry_count = count;
    g_ready = (count > 0) ? 1 : -1;
    l_info("[soundpack] %s: %d entradas indexadas", SOUNDPACK_PATH, count);
    return g_ready > 0 ? 0 : -1;
}

const SoundPackEntry *soundpack_find(const char *name) {
    if (soundpack_init() != 0 || !name) return NULL;

    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].name, name) == 0) return &g_entries[i];
    }
    return NULL;
}

int soundpack_read(const SoundPackEntry *entry, void *dest) {
    if (!entry || !dest) return 0;

    FILE *f = fopen(SOUNDPACK_PATH, "rb");
    if (!f) return 0;

    int ok = (fseek(f, entry->dataOffset, SEEK_SET) == 0) &&
             (fread(dest, 1, entry->dataSize, f) == entry->dataSize);
    fclose(f);

    if (!ok) {
        l_error("[soundpack] fallo leyendo '%s' (offset=%ld size=%u)",
                entry->name, entry->dataOffset, (unsigned) entry->dataSize);
    }
    return ok;
}
