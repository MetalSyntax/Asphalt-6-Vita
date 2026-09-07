/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/mem.h"
#include "utils/breadcrumb.h"
#include "utils/logger.h"

#include <string.h>
#include <malloc.h>
#include <psp2/kernel/clib.h>

void *sceClibMemclr(void *dst, size_t len) {
    return sceClibMemset(dst, 0, len);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offs) {
    l_warn("mmap(%p, %i, %i, %i, %i, %li)", addr, length, prot, flags, fd, offs);

    if (length <= 0) {
        return MAP_FAILED;
    }
    void* ret= malloc(length);
    memset(ret, 0, length);
    return ret;
}

int munmap(void *addr, size_t length) {
    if (addr) free(addr);
    return 0;
}

/*
 * Envoltorios de malloc/free que solo dejan migas (utils/breadcrumb.h).
 *
 * Sirven para el caso peor de un cuelgue: el motor girando en un bucle de codigo puro que no
 * toca ni GL ni archivos. Casi cualquier bucle de este motor (construccion de escena, batching
 * de mallas, contenedores de la STL) reserva memoria, asi que el anillo caliente termina lleno
 * de direcciones de retorno DE ESE BUCLE -- que es exactamente el offset del .so que hay que
 * buscar en el pseudo-C de Ghidra.
 */
void *malloc_soloader(size_t size) {
    bc_hot("malloc", BC_RA);
    return malloc(size);
}

void *calloc_soloader(size_t nmemb, size_t size) {
    bc_hot("calloc", BC_RA);
    return calloc(nmemb, size);
}

void *realloc_soloader(void *ptr, size_t size) {
    bc_hot("realloc", BC_RA);
    return realloc(ptr, size);
}

void free_soloader(void *ptr) {
    bc_hot("free", BC_RA);
    free(ptr);
}
