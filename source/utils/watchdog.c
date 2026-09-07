/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/watchdog.h"

#include "utils/breadcrumb.h"
#include "utils/glutil.h"
#include "utils/logger.h"

#include <stdatomic.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#define WATCHDOG_PERIOD_US (5 * 1000 * 1000)

// Latidos seguidos sin un solo frame nuevo antes de considerar que esto se colgó de verdad
// (y no que un nativeRender está tardando mucho, cosa normal durante la carga).
#define WATCHDOG_STALL_BEATS 2

// Cada cuántos latidos se repite el volcado completo mientras siga colgado. Repetirlo sirve
// para distinguir "trabado en la MISMA llamada" de "girando y cambiando de llamada".
#define WATCHDOG_REDUMP_EVERY 6

#define WD_MAX_THREADS 32

// El tag siempre es un literal, así que guardar el puntero es seguro y no hace falta copiar
// ni bloquear: en el peor caso el hilo testigo lee el hito anterior y late una vez de más.
static const char *_Atomic s_tag = ATOMIC_VAR_INIT("arranque");
static atomic_int s_value = ATOMIC_VAR_INIT(0);
static atomic_uint s_tag_time = ATOMIC_VAR_INIT(0);

typedef struct {
    int uid;
    const char *name;
    uint64_t last_clocks;
    int reported_bad;
} wd_thread;

static wd_thread s_threads[WD_MAX_THREADS];
static atomic_int s_thread_count = ATOMIC_VAR_INIT(0);

void watchdog_mark(const char *tag, int value) {
    atomic_store_explicit(&s_tag, tag, memory_order_relaxed);
    atomic_store_explicit(&s_value, value, memory_order_relaxed);
    atomic_store_explicit(&s_tag_time, sceKernelGetProcessTimeLow(), memory_order_relaxed);
}

void watchdog_register_thread(int uid, const char *name) {
    if (uid <= 0)
        return;

    // Sin duplicados: bc_push auto-registra en cada evento (ver breadcrumb.c), asi que
    // el mismo tid llegaria cientos de veces por segundo. El escaneo es corto (<=32) y
    // sin locks, igual que el resto de este modulo.
    int count = atomic_load_explicit(&s_thread_count, memory_order_relaxed);
    if (count > WD_MAX_THREADS)
        count = WD_MAX_THREADS;
    for (int i = 0; i < count; ++i) {
        if (__atomic_load_n(&s_threads[i].uid, __ATOMIC_ACQUIRE) == uid)
            return;
    }

    int slot = atomic_fetch_add_explicit(&s_thread_count, 1, memory_order_relaxed);
    if (slot >= WD_MAX_THREADS) {
        atomic_store_explicit(&s_thread_count, WD_MAX_THREADS, memory_order_relaxed);
        return;
    }

    s_threads[slot].name = name;
    s_threads[slot].last_clocks = 0;
    s_threads[slot].reported_bad = 0;
    // El uid se publica ÚLTIMO: el testigo corre en paralelo y solo mira ranuras con uid != 0,
    // así nunca lee una a medio llenar.
    __atomic_store_n(&s_threads[slot].uid, uid, __ATOMIC_RELEASE);
}

static const char *wd_status_name(unsigned status) {
    switch (status) {
        case SCE_THREAD_RUNNING: return "CORRIENDO";
        case SCE_THREAD_READY:   return "LISTO";
        case SCE_THREAD_STANDBY: return "STANDBY";
        case SCE_THREAD_WAITING: return "BLOQUEADO";
        case SCE_THREAD_DORMANT: return "DORMIDO";
        case SCE_THREAD_DELETED: return "MATADO(stack?)";
        case SCE_THREAD_DEAD:    return "MUERTO";
        default:                 return "?";
    }
}

/*
 * Vuelca el estado de cada hilo conocido. Las dos columnas que importan:
 *
 *  - estado: BLOQUEADO significa que el kernel lo tiene esperando un objeto (waitType/waitId
 *    dicen cuál) -- deadlock. CORRIENDO/LISTO significa que gasta CPU: está girando en un
 *    bucle cerrado, no bloqueado.
 *  - cpu: microsegundos de CPU consumidos desde el latido anterior. ~0 en un hilo BLOQUEADO
 *    confirma el deadlock; cerca de 5.000.000 en un hilo CORRIENDO confirma el bucle cerrado
 *    (y, si ese hilo tiene prioridad más alta que el principal, explica por qué el principal
 *    no avanza aunque no esté bloqueado en nada).
 */
static void watchdog_dump_threads(void) {
    int count = atomic_load_explicit(&s_thread_count, memory_order_relaxed);
    if (count > WD_MAX_THREADS)
        count = WD_MAX_THREADS;

    for (int i = 0; i < count; ++i) {
        int uid = __atomic_load_n(&s_threads[i].uid, __ATOMIC_ACQUIRE);
        if (uid == 0)
            continue;

        SceKernelThreadInfo info;
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);

        int ret = sceKernelGetThreadInfo(uid, &info);
        if (ret < 0) {
            if (!s_threads[i].reported_bad) {
                s_threads[i].reported_bad = 1;
                l_error("[wd] hilo '%s' uid=0x%08X: sceKernelGetThreadInfo -> 0x%08X "
                        "(no es un UID de hilo; ignorado)",
                        s_threads[i].name, (unsigned)uid, (unsigned)ret);
            }
            continue;
        }

        uint64_t clocks = (uint64_t)info.runClocks;
        uint64_t delta = clocks - s_threads[i].last_clocks;
        s_threads[i].last_clocks = clocks;

        l_error("[wd]   %-14s uid=0x%08X '%s' estado=%s(0x%X) espera(tipo=0x%08X id=0x%08X) "
                "prio=0x%X core=%d cpu=+%u us",
                s_threads[i].name, (unsigned)uid, info.name,
                wd_status_name(info.status), (unsigned)info.status,
                (unsigned)info.waitType, (unsigned)info.waitId,
                (unsigned)info.currentPriority, (int)info.currentCpuId,
                (unsigned)delta);
    }
}

static int watchdog_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    unsigned int last_swaps = 0;
    unsigned int last_hot = 0;
    unsigned int last_spin = 0;
    unsigned int last_cmp = 0;
    unsigned int last_time = 0;
    unsigned int stalled_beats = 0;

    while (1) {
        sceKernelDelayThread(WATCHDOG_PERIOD_US);

        unsigned int swaps = gl_swap_count;
        unsigned int hot = bc_hot_count();
        unsigned int spin = bc_spin_strstr_count();
        unsigned int cmp = bc_spin_strcmp_count();
        unsigned int time = bc_spin_time_count();
        unsigned int since = (sceKernelGetProcessTimeLow()
                              - atomic_load_explicit(&s_tag_time, memory_order_relaxed)) / 1000;

        // "+N reservas" es la señal barata de bucle caliente: si los frames no avanzan pero
        // las reservas sí, el motor está girando en código propio, no bloqueado.
        // "+N strstr" distingue DENTRO de ese caso el bucle de re-parenting de
        // MenuScene::MenuScene (strstr por nodo, sin malloc) de otros giros.
        // "+N strcmp" cubre parseos (constructAnimator/XML: strcmp por tag, sin malloc
        // visible si opera sobre buffer ya cargado) y "+N gettod" las esperas activas
        // de tiempo (pacing de DisplayFrame con reloj congelado).
        l_error("[wd] %u frames (+%u en %ds) | +%u reservas +%u strstr +%u strcmp +%u gettod | último hito: %s #%d hace %u ms",
                swaps, swaps - last_swaps, WATCHDOG_PERIOD_US / 1000000,
                hot - last_hot,
                spin - last_spin,
                cmp - last_cmp,
                time - last_time,
                atomic_load_explicit(&s_tag, memory_order_relaxed),
                atomic_load_explicit(&s_value, memory_order_relaxed),
                since);

        if (swaps == last_swaps) {
            stalled_beats++;
            if (stalled_beats == WATCHDOG_STALL_BEATS
                || (stalled_beats > WATCHDOG_STALL_BEATS
                    && ((stalled_beats - WATCHDOG_STALL_BEATS) % WATCHDOG_REDUMP_EVERY) == 0)) {
                l_error("[wd] === sin frames nuevos hace %u s: volcando estado ===",
                        stalled_beats * (WATCHDOG_PERIOD_US / 1000000));
                watchdog_dump_threads();
                bc_dump("cuelgue detectado por el hilo testigo");
            }
        } else {
            stalled_beats = 0;
        }

        last_swaps = swaps;
        last_hot = hot;
        last_spin = spin;
        last_cmp = cmp;
        last_time = time;
    }

    return 0;
}

void watchdog_start(void) {
    // El hilo principal es el que nos interesa medir: se registra desde acá, que todavía
    // corre en él.
    watchdog_register_thread(sceKernelGetThreadId(), "principal");

    /*
     * Prioridad MÁS ALTA que la del hilo principal (números más chicos = más prioridad).
     * Suena al revés, pero es lo que hace útil al testigo: si el hilo principal se queda
     * girando en un bucle cerrado sin bloquearse nunca, un hilo de menor prioridad en el
     * mismo core no llegaría a correr y no habría latido -- justo en el caso que queremos
     * diagnosticar. Duerme 5 s de cada 5 s, así que no le roba tiempo a nadie.
     */
    SceUID thid = sceKernelCreateThread("a6_watchdog", watchdog_thread,
                                        0x10000100 - 10, 0x4000, 0, 0, NULL);
    if (thid < 0) {
        l_error("[wd] no se pudo crear el hilo testigo: 0x%08X", (unsigned)thid);
        return;
    }
    sceKernelStartThread(thid, 0, NULL);
    watchdog_mark("watchdog listo", 0);
}
