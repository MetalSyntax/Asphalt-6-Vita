/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/sys.h"

#include <sys/errno.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <string.h>
#include <stdint.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#include <stdlib.h>

#include "utils/breadcrumb.h"
#include "utils/logger.h"
#include "utils/perf.h"
#include "utils/utils.h"

#define BIONIC_CLOCK_REALTIME           0
#define BIONIC_CLOCK_MONOTONIC          1
#define BIONIC_CLOCK_PROCESS_CPUTIME_ID 2
#define BIONIC_CLOCK_THREAD_CPUTIME_ID  3
#define BIONIC_CLOCK_MONOTONIC_RAW      4
#define BIONIC_CLOCK_REALTIME_COARSE    5
#define BIONIC_CLOCK_MONOTONIC_COARSE   6
#define BIONIC_CLOCK_BOOTTIME           7
#define BIONIC_CLOCK_REALTIME_ALARM     8
#define BIONIC_CLOCK_BOOTTIME_ALARM     9
#define BIONIC_CLOCK_SGI_CYCLE         10
#define BIONIC_CLOCK_TAI               11

// 1969 years in microseconds, used to adjust SCE tick to UNIX timestamp
#define __epoch 62135587294000000

int clock_gettime_soloader(clockid_t clock_id, struct timespec * tp) {
    switch (clock_id) {
        case BIONIC_CLOCK_MONOTONIC:
        case BIONIC_CLOCK_MONOTONIC_RAW:
        case BIONIC_CLOCK_MONOTONIC_COARSE:
        case BIONIC_CLOCK_BOOTTIME:
        case BIONIC_CLOCK_BOOTTIME_ALARM:
        case BIONIC_CLOCK_SGI_CYCLE:
        case BIONIC_CLOCK_PROCESS_CPUTIME_ID:
        case BIONIC_CLOCK_THREAD_CPUTIME_ID: {
            uint64_t proctime = sceKernelGetProcessTimeWide();

            tp->tv_sec = (proctime / 1000000);
            tp->tv_nsec = ((proctime - (tp->tv_sec * 1000000)) * 1000);
            break;
        }
        case BIONIC_CLOCK_REALTIME:
        case BIONIC_CLOCK_REALTIME_COARSE:
        case BIONIC_CLOCK_REALTIME_ALARM:
        case BIONIC_CLOCK_TAI: {
            SceRtcTick tick;
            sceRtcGetCurrentTick(&tick);
            tick.tick -= __epoch;

            tp->tv_sec = (tick.tick / 1000000);
            tp->tv_nsec = ((tick.tick - (tp->tv_sec * 1000000)) * 1000);
            break;
        }
        default:
            l_error("clock_gettime / unexpected clock id %i", clock_id);
    }

    return 0;
}

int clock_getres_soloader(clockid_t clock_id, struct timespec * res) {
    res->tv_sec = 0;
    res->tv_nsec = 1000;
    return 0;
}

clock_t clock_soloader(void) {
    return sceKernelGetProcessTimeLow();
}

/*
 * Paridad con Asphalt-5-Vita. El motor (glitch::os::Timer::getRealTime/
 * getMicroSeconds, CFPSCounter, DisplayFrame cada 100 ms) vive de gettimeofday;
 * el newlib de stock no da un reloj monotonico fiable en Vita y los sleeps
 * sub-milisegundo / yields hunden el rendimiento del hilo principal.
 */
int gettimeofday_soloader(struct timeval *tv, void *tz) {
    // Contador para el testigo (ver bc_spin_time): una espera activa de tiempo
    // con un reloj que no avanzara seria invisible sin esto. Además se guarda
    // DE DONDE vino la consulta (ver bc_clock_site): si el juego gira
    // preguntando la hora, el volcado del testigo dice en qué función.
    // ra0 = Timer::getRealTime (hoja), ra1 = el bucle, ra2 = el marco de arriba.
    bc_spin_time();
    bc_clock_site((uint32_t)(uintptr_t)__builtin_return_address(0),
                  (uint32_t)(uintptr_t)__builtin_return_address(1),
                  (uint32_t)(uintptr_t)__builtin_return_address(2));
    if (tv) {
        uint64_t proctime = sceKernelGetProcessTimeWide();
        tv->tv_sec = proctime / 1000000;
        tv->tv_usec = proctime % 1000000;
    }
    return 0;
}

int usleep_soloader(useconds_t usec) {
    // Las migas van ANTES del corte de <1ms a proposito: un bucle de espera del motor que
    // duerme 100 us es invisible en el log (no llega a dormir) pero deja rastro en el anillo.
    bc_event("usleep", BC_RA);
    if (usec < 1000) {
        return 0; // Just return immediately to keep the spinlock fast.
    }
    if (sceKernelGetThreadId() == g_perf_main_tid) {
        uint32_t _pt = perf_now();
        int r = sceKernelDelayThread(usec);
        perf_add(PERF_SLEEP, _pt);
        return r;
    }
    return sceKernelDelayThread(usec);
}

int nanosleep_soloader(const struct timespec *req, struct timespec *rem) {
    bc_event("nanosleep", BC_RA);
    if (req && req->tv_sec == 0 && req->tv_nsec < 1000000) {
        return 0; // Skip sleeps under 1ms
    }
    if (sceKernelGetThreadId() == g_perf_main_tid) {
        uint32_t _pt = perf_now();
        int r = nanosleep(req, rem);
        perf_add(PERF_SLEEP, _pt);
        return r;
    }
    return nanosleep(req, rem);
}

int sched_yield_soloader(void) {
    bc_event("sched_yield", BC_RA);
    return 0; // Do nothing. Yielding on a single-core dominant engine tanks FPS.
}

int sigaction(int signum, const struct sigaction * act, struct sigaction * oldact) {
    l_warn("sigaction(%i, ...): not implemented", signum);
    return 0;
}

int __system_property_get_soloader(const char *name, char *value) {
    l_warn("__system_property_get(%s, %p): not implemented", name, value);
    strncpy(value, "psvita", 7);
    return 7;
}

void assert2(const char* f, int l, const char* func, const char* msg) {
    l_fatal("[%s:%i][%s] Assertion failed: %s", f, l, func, msg);
}

void syscall(int c) {
    l_warn("syscall(%i): not implemented", c);
}

void __stack_chk_fail_soloader() {
    l_fatal("Stack collapsed at address %p", __builtin_return_address(0));
}

void abort_soloader() {
    l_fatal("Abort called from address %p", __builtin_return_address(0));
    abort();
}

void exit_soloader(int status) {
    l_fatal("Exit(%i) called from %p", status, __builtin_return_address(0));
    exit(status);
}

int __atomic_dec(volatile int *ptr) {
    return __sync_fetch_and_sub(ptr, 1);
}

int __atomic_inc(volatile int *ptr) {
    return __sync_fetch_and_add(ptr, 1);
}

int __atomic_swap(int new_value, volatile int *ptr) {
    int old_value;
    do {
        old_value = *ptr;
    } while (__sync_val_compare_and_swap(ptr, old_value, new_value) != old_value);
    return old_value;
}

int __atomic_cmpxchg(int old_value, int new_value, volatile int* ptr) {
    /* We must return 0 on success */
    return __sync_val_compare_and_swap(ptr, old_value, new_value) != old_value;
}

char * getenv_soloader(const char * var) {
    l_warn("getenv(\"%s\"): not implemented.", var);
    return NULL;
}

int setenv_soloader(const char * name, const char * value, int overwrite) {
    l_warn("setenv(\"%s\", \"%s\"): not implemented.", name, value);
    return 0;
}

int getpagesize(void) {
    return PAGE_SIZE;
}
