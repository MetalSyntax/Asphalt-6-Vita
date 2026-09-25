/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  perf.h
 * @brief Perfilador barato de frame (Bug #045): a donde se va el tiempo de un frame de carrera.
 *
 * Los logs 072-078 solo dan "frames por 5 s" (watchdog): alcanza para ver que en carrera se
 * anda a ~20 fps, pero NO para saber si el frame lo come la CPU del motor (nativeRender), la
 * espera a la GPU/display (vglSwapBuffers), un readback con sceGxmFinish (glCopyTex* de
 * vitaGL), subidas de texturas en caliente (streaming) o recreacion de buffers
 * (thisAppendBatch -> glBufferData). Cada categoria acumula llamadas + microsegundos + el
 * peor caso; el hilo testigo imprime una linea `[perf]` por latido (siempre activa, tambien
 * en Release: una linea cada 5 s). Contadores de 32 bits acumulativos: el testigo resta
 * contra su copia anterior (la vuelta de 32 bits se cancela en la resta sin signo).
 *
 * Solo se instrumentan llamadas del hilo de render (el unico que toca GL); las escrituras
 * no son atomicas a proposito (un solo escritor, el lector tolera un valor desfasado).
 */

#ifndef SOLOADER_PERF_H
#define SOLOADER_PERF_H

#include <stdint.h>
#include <psp2/kernel/processmgr.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PERF_RENDER = 0,  // nativeRender() completo (main.c)
    PERF_SWAP,        // vglSwapBuffers (espera de GPU/display incluida)
    PERF_COPYTEX,     // glCopyTex(Sub)Image2D: readback + sceGxmFinish en vitaGL
    PERF_TEXUP,       // glTexImage2D / glCompressedTexImage2D / glTexSubImage2D
    PERF_BUFDATA,     // glBufferData
    PERF_FLUSH,       // glFlush (scene_reset en vitaGL)
    PERF_SLEEP,       // usleep/nanosleep que duermen de verdad en el hilo principal
    PERF_COUNT
} perf_slot_id;

typedef struct {
    uint32_t calls;
    uint32_t us;
    uint32_t max_us; // peor caso desde el ultimo latido (el testigo lo pone a 0)
} perf_slot;

extern perf_slot g_perf[PERF_COUNT];
extern uint32_t g_perf_draws;      // glDrawElements + glDrawArrays
extern uint32_t g_perf_bindfb;     // glBindFramebuffer
extern uint32_t g_perf_texup_bytes;
extern uint32_t g_perf_buf_bytes;
extern int g_perf_main_tid;
// Bug #045/#041: &DeviceConfig::s_GameplayFactorLOD y &BaseScene::m_currentTrack del .so
// (NULL si no se resolvieron). Se imprimen en el latido para ver en vivo el factor de LOD
// real que usa CustomSceneManager::drawAll en cada pista.
extern const volatile float *g_perf_lod_factor;
extern const volatile int *g_perf_cur_track;

static inline uint32_t perf_now(void) { return sceKernelGetProcessTimeLow(); }

static inline void perf_add(perf_slot_id id, uint32_t t0) {
    uint32_t d = perf_now() - t0;
    perf_slot *s = &g_perf[id];
    s->calls++;
    s->us += d;
    if (d > s->max_us) s->max_us = d;
}

/** Llamar desde gl_swap(): mide el intervalo entre frames presentados (hitches). */
void perf_frame_presented(void);

/** Llamar una vez desde el hilo principal (define que hilo cuenta para PERF_SLEEP). */
void perf_init(void);

/** Lo llama el hilo testigo en cada latido: imprime la linea [perf] con los deltas. */
void perf_report(unsigned int period_ms, uint32_t main_cpu_us);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_PERF_H
