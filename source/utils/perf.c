/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/perf.h"
#include "utils/logger.h"

#include <string.h>
#include <psp2/kernel/threadmgr.h>

perf_slot g_perf[PERF_COUNT];
uint32_t g_perf_draws = 0;
uint32_t g_perf_bindfb = 0;
uint32_t g_perf_texup_bytes = 0;
uint32_t g_perf_buf_bytes = 0;
int g_perf_main_tid = -1;
const volatile float *g_perf_lod_factor = NULL;
const volatile int *g_perf_cur_track = NULL;

// Intervalo entre frames presentados: el peor del latido y cuantos pasaron de 50/100/250 ms.
// 50 ms = por debajo de 20 fps; 250 ms = tiron visible (auto/escenario "desaparece").
static uint32_t s_last_present = 0;
static uint32_t s_frame_max_us = 0;
static uint32_t s_hitch50 = 0, s_hitch100 = 0, s_hitch250 = 0;

void perf_init(void) {
    g_perf_main_tid = sceKernelGetThreadId();
}

void perf_frame_presented(void) {
    uint32_t now = perf_now();
    if (s_last_present) {
        uint32_t d = now - s_last_present;
        if (d > s_frame_max_us) s_frame_max_us = d;
        if (d >= 50000) s_hitch50++;
        if (d >= 100000) s_hitch100++;
        if (d >= 250000) s_hitch250++;
    }
    s_last_present = now;
}

void perf_report(unsigned int period_ms, uint32_t main_cpu_us) {
    static perf_slot last[PERF_COUNT];
    static uint32_t last_draws, last_bindfb, last_texb, last_bufb;
    static uint32_t last_h50, last_h100, last_h250;

    perf_slot d[PERF_COUNT];
    for (int i = 0; i < PERF_COUNT; ++i) {
        perf_slot cur = g_perf[i];
        d[i].calls = cur.calls - last[i].calls;
        d[i].us = cur.us - last[i].us;
        d[i].max_us = g_perf[i].max_us;
        g_perf[i].max_us = 0;
        last[i] = cur;
    }
    uint32_t draws = g_perf_draws, bindfb = g_perf_bindfb;
    uint32_t texb = g_perf_texup_bytes, bufb = g_perf_buf_bytes;
    uint32_t h50 = s_hitch50, h100 = s_hitch100, h250 = s_hitch250;
    uint32_t fmax = s_frame_max_us;
    s_frame_max_us = 0;

    uint32_t frames = d[PERF_SWAP].calls ? d[PERF_SWAP].calls : 1;
    // ms totales por categoria en el latido (de period_ms), + promedio por frame en us.
#define MS(i) (unsigned)(d[i].us / 1000)
#define PF(i) (unsigned)(d[i].us / frames)
    l_error("[perf] %ums: cpu_main=%ums | render %ums (motor sin swap %uus/f) | swap %ums (%uus/f max %ums) | "
            "sleep %ux %ums | frame max %ums, >50ms %u >100ms %u >250ms %u",
            period_ms, (unsigned)(main_cpu_us / 1000),
            MS(PERF_RENDER),
            (unsigned)((d[PERF_RENDER].us > d[PERF_SWAP].us ? d[PERF_RENDER].us - d[PERF_SWAP].us : 0) / frames),
            MS(PERF_SWAP), PF(PERF_SWAP), (unsigned)(d[PERF_SWAP].max_us / 1000),
            (unsigned)d[PERF_SLEEP].calls, MS(PERF_SLEEP),
            (unsigned)(fmax / 1000), (unsigned)(h50 - last_h50),
            (unsigned)(h100 - last_h100), (unsigned)(h250 - last_h250));
    l_error("[perf]   draws %u (%u/f) bindfb %u | copytex %ux %ums (max %ums) | "
            "texup %ux %uKiB %ums (max %ums) | bufdata %ux %uKiB %ums (max %ums) | flush %ux %ums",
            (unsigned)(draws - last_draws), (unsigned)((draws - last_draws) / frames),
            (unsigned)(bindfb - last_bindfb),
            (unsigned)d[PERF_COPYTEX].calls, MS(PERF_COPYTEX), (unsigned)(d[PERF_COPYTEX].max_us / 1000),
            (unsigned)d[PERF_TEXUP].calls, (unsigned)((texb - last_texb) / 1024), MS(PERF_TEXUP),
            (unsigned)(d[PERF_TEXUP].max_us / 1000),
            (unsigned)d[PERF_BUFDATA].calls, (unsigned)((bufb - last_bufb) / 1024), MS(PERF_BUFDATA),
            (unsigned)(d[PERF_BUFDATA].max_us / 1000),
            (unsigned)d[PERF_FLUSH].calls, MS(PERF_FLUSH));
    if (g_perf_lod_factor) {
        static float last_lod = -12345.0f;
        static int last_track = -12345;
        float lod = *g_perf_lod_factor;
        int track = g_perf_cur_track ? *g_perf_cur_track : -1;
        if (lod != last_lod || track != last_track) // solo cuando cambia (una vez por pista)
            l_error("[perf] s_GameplayFactorLOD=%d/1000 m_currentTrack=%d", (int)(lod * 1000.0f), track);
        last_lod = lod;
        last_track = track;
    }
#undef MS
#undef PF
    last_draws = draws; last_bindfb = bindfb; last_texb = texb; last_bufb = bufb;
    last_h50 = h50; last_h100 = h100; last_h250 = h250;
}
