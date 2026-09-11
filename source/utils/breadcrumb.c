/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/breadcrumb.h"

#include "utils/logger.h"
#include "utils/watchdog.h"

#include <stdatomic.h>
#include <stdio.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

// Potencia de dos: el modulo se vuelve un AND.
#define BC_RING 128
#define BC_MASK (BC_RING - 1)

enum { BC_KIND_EVENT = 0, BC_KIND_ENTER = 1, BC_KIND_EXIT = 2 };

typedef struct {
    const char *name;   // siempre un literal, no hace falta copiarlo
    uint32_t    ra;     // direccion de retorno cruda
    uint32_t    t;      // sceKernelGetProcessTimeLow()
    int32_t     tid;    // hilo que la emitio
    uint32_t    kind;
} bc_entry;

static bc_entry s_ring[BC_RING];
static atomic_uint s_seq = ATOMIC_VAR_INIT(0);

// Anillo aparte solo para el hilo principal (ver bc_set_main_tid en el .h): los workers
// del .so generan cientos de eventos por segundo y expulsan sus entradas del anillo
// comun en milisegundos. 32 entradas alcanzan para ver la ultima llamada del principal
// (normalmente una ENTRA sin su sale) aunque lleve colgado varios latidos.
#define BC_MAIN_RING 32
#define BC_MAIN_MASK (BC_MAIN_RING - 1)
static bc_entry s_main_ring[BC_MAIN_RING];
static atomic_uint s_main_seq = ATOMIC_VAR_INIT(0);
static int s_main_tid = 0;

void bc_set_main_tid(int tid) {
    s_main_tid = tid;
}

// Contador de strstr() del .so (ver el .h): un atomico, sin syscalls, para no cambiar el
// timing del bucle que se quiere medir.
static atomic_uint s_spin_strstr = ATOMIC_VAR_INIT(0);

void bc_spin_strstr(void) {
    atomic_fetch_add_explicit(&s_spin_strstr, 1, memory_order_relaxed);
}

unsigned bc_spin_strstr_count(void) {
    return atomic_load_explicit(&s_spin_strstr, memory_order_relaxed);
}

// Mismo patron para strcmp()/strncmp()/memcmp() (parseos) y consultas de reloj
// (esperas activas de tiempo): ver el .h.
static atomic_uint s_spin_strcmp = ATOMIC_VAR_INIT(0);

void bc_spin_strcmp(void) {
    atomic_fetch_add_explicit(&s_spin_strcmp, 1, memory_order_relaxed);
}

unsigned bc_spin_strcmp_count(void) {
    return atomic_load_explicit(&s_spin_strcmp, memory_order_relaxed);
}

static atomic_uint s_spin_time = ATOMIC_VAR_INIT(0);

void bc_spin_time(void) {
    atomic_fetch_add_explicit(&s_spin_time, 1, memory_order_relaxed);
}

unsigned bc_spin_time_count(void) {
    return atomic_load_explicit(&s_spin_time, memory_order_relaxed);
}

// De donde vino la ultima consulta de reloj (ver el .h): el giro del log 021
// solo toca gettimeofday (~1850/s) y nada mas envuelto, asi que el contador
// dice QUE clase de giro es pero no en QUE funcion. Se guarda el ultimo sitio
// (el giro lo sobrescribe continuamente: al volcar, es el del bucle).
// Sin locks ni syscalls, igual que el resto de este modulo.
static atomic_uint s_clock_ra0 = ATOMIC_VAR_INIT(0);
static atomic_uint s_clock_ra1 = ATOMIC_VAR_INIT(0);
static atomic_uint s_clock_ra2 = ATOMIC_VAR_INIT(0);
static atomic_int s_clock_tid = ATOMIC_VAR_INIT(0);

void bc_clock_site(uint32_t ra0, uint32_t ra1, uint32_t ra2) {
    atomic_store_explicit(&s_clock_ra0, ra0, memory_order_relaxed);
    atomic_store_explicit(&s_clock_ra1, ra1, memory_order_relaxed);
    atomic_store_explicit(&s_clock_ra2, ra2, memory_order_relaxed);
    atomic_store_explicit(&s_clock_tid, sceKernelGetThreadId(), memory_order_relaxed);
}

void bc_clock_site_get(uint32_t *ra0, uint32_t *ra1, uint32_t *ra2) {
    if (ra0)
        *ra0 = atomic_load_explicit(&s_clock_ra0, memory_order_relaxed);
    if (ra1)
        *ra1 = atomic_load_explicit(&s_clock_ra1, memory_order_relaxed);
    if (ra2)
        *ra2 = atomic_load_explicit(&s_clock_ra2, memory_order_relaxed);
}

int bc_clock_site_tid(void) {
    return atomic_load_explicit(&s_clock_tid, memory_order_relaxed);
}

// Anillo aparte para las llamadas muy frecuentes (malloc/free): en el comun taparian todo.
#define BC_HOT_RING 32
#define BC_HOT_MASK (BC_HOT_RING - 1)
static bc_entry s_hot[BC_HOT_RING];
static atomic_uint s_hot_seq = ATOMIC_VAR_INIT(0);

static uintptr_t s_text_base = 0;
static uint32_t s_text_size = 0;

void bc_set_base(uintptr_t text_base, uint32_t text_size) {
    s_text_base = text_base;
    s_text_size = text_size;
}

int bc_in_so(uint32_t addr) {
    return s_text_base && addr >= s_text_base && addr < s_text_base + s_text_size;
}

/*
 * Sin locks a proposito: el anillo se lee UNA vez, desde el hilo testigo, cuando el juego
 * ya esta trabado. Una carrera en el peor caso mezcla dos entradas vecinas; el costo de un
 * mutex por llamada interceptada, en cambio, cambiaria el comportamiento que queremos medir.
 */
static void bc_push(const char *name, const void *ra, uint32_t kind) {
    int tid = sceKernelGetThreadId();
    unsigned i = atomic_fetch_add_explicit(&s_seq, 1, memory_order_relaxed) & BC_MASK;
    s_ring[i].name = name;
    s_ring[i].ra = (uint32_t)(uintptr_t)ra;
    s_ring[i].t = sceKernelGetProcessTimeLow();
    s_ring[i].tid = tid;
    s_ring[i].kind = kind;

    // Todo hilo que toca un wrapper queda registrado en el testigo, venga de donde
    // venga (algunos hilos del .so nacen en JNI_OnLoad o por vias que no pasan por
    // pthread_create_soloader: en el log 015 el volcado solo mostraba "principal"
    // aunque 3 hilos del .so seguian activos). watchdog_register_thread ignora
    // duplicados, asi que llamar en cada evento solo cuesta un escaneo corto.
    watchdog_register_thread(tid, "auto");

    if (s_main_tid != 0 && tid == s_main_tid) {
        unsigned m = atomic_fetch_add_explicit(&s_main_seq, 1, memory_order_relaxed)
                     & BC_MAIN_MASK;
        s_main_ring[m].name = name;
        s_main_ring[m].ra = (uint32_t)(uintptr_t)ra;
        s_main_ring[m].t = s_ring[i].t;
        s_main_ring[m].tid = tid;
        s_main_ring[m].kind = kind;
    }
}

void bc_event(const char *name, const void *ra) { bc_push(name, ra, BC_KIND_EVENT); }

/*
 * A diferencia de bc_push(), esta NO pide hora ni id de hilo: son dos syscalls, y a razon de
 * una reserva de memoria cada pocos microsegundos eso solo cambiaria el problema a medir.
 * Con el nombre y la direccion de retorno alcanza; el "cuando" lo da bc_hot_count(), que el
 * hilo testigo imprime como delta por latido ("sigue reservando memoria" vs "no reserva
 * nada"), y eso ya distingue bucle caliente de bloqueo.
 */
void bc_hot(const char *name, const void *ra) {
    unsigned i = atomic_fetch_add_explicit(&s_hot_seq, 1, memory_order_relaxed) & BC_HOT_MASK;
    s_hot[i].name = name;
    s_hot[i].ra = (uint32_t)(uintptr_t)ra;
    s_hot[i].t = 0;
    s_hot[i].tid = 0;
    s_hot[i].kind = BC_KIND_EVENT;
}

unsigned bc_hot_count(void) {
    return atomic_load_explicit(&s_hot_seq, memory_order_relaxed);
}
void bc_enter(const char *name, const void *ra) { bc_push(name, ra, BC_KIND_ENTER); }
void bc_exit(const char *name)                  { bc_push(name, 0, BC_KIND_EXIT); }

static void bc_dump_ring(const bc_entry *ring, unsigned mask, unsigned seq, unsigned cap) {
    unsigned count = seq < cap ? seq : cap;
    unsigned now = sceKernelGetProcessTimeLow();

    for (unsigned n = count; n > 0; --n) {
        const bc_entry *e = &ring[(seq - n) & mask];
        if (!e->name)
            continue;

        const char *kind = e->kind == BC_KIND_ENTER ? "ENTRA"
                         : e->kind == BC_KIND_EXIT  ? "sale "
                                                    : "     ";

        // La direccion de retorno solo sirve si cae adentro del .so del juego: ahi se puede
        // buscar el offset en el pseudo-C de Ghidra y sacar el nombre de la funcion real.
        // El anillo caliente no guarda hora ni hilo (ver bc_hot): se imprimen en blanco.
        char when[32];
        if (e->t)
            snprintf(when, sizeof(when), "-%6u ms tid=0x%08X",
                     (now - e->t) / 1000, (unsigned)e->tid);
        else
            snprintf(when, sizeof(when), "%22s", "");

        if (e->kind != BC_KIND_EXIT && s_text_base && e->ra >= s_text_base
            && e->ra < s_text_base + s_text_size) {
            l_error("[bc] %s %s %-28s <- libasphalt6.so+0x%08X",
                    when, kind, e->name, (unsigned)(e->ra - s_text_base));
        } else if (e->kind != BC_KIND_EXIT && e->ra) {
            l_error("[bc] %s %s %-28s <- 0x%08X (fuera del .so)",
                    when, kind, e->name, (unsigned)e->ra);
        } else {
            l_error("[bc] %s %s %s", when, kind, e->name);
        }
    }

}

void bc_dump(const char *why) {
    l_error("[bc] ---- ultimas llamadas interceptadas (%s) ----", why);
    bc_dump_ring(s_ring, BC_MASK,
                 atomic_load_explicit(&s_seq, memory_order_relaxed), BC_RING);

    l_error("[bc] ---- ultimas del hilo principal (sin ruido de workers) ----");
    bc_dump_ring(s_main_ring, BC_MAIN_MASK,
                 atomic_load_explicit(&s_main_seq, memory_order_relaxed), BC_MAIN_RING);

    l_error("[bc] ---- ultimas reservas de memoria (bucle caliente) ----");
    bc_dump_ring(s_hot, BC_HOT_MASK,
                 atomic_load_explicit(&s_hot_seq, memory_order_relaxed), BC_HOT_RING);

    // Quien quema el reloj (ver bc_clock_site en el .h): ra0 es el llamador
    // directo de gettimeofday (el wrapper hoja o CCondition::wait), ra1 su
    // llamador (el bucle) y ra2 el marco de arriba. El tid dice QUÉ hilo lo
    // quema: el contador del testigo es global y mezcla principal + workers.
    uint32_t ra0 = atomic_load_explicit(&s_clock_ra0, memory_order_relaxed);
    int ctid = atomic_load_explicit(&s_clock_tid, memory_order_relaxed);
    if (ra0) {
        if (bc_in_so(ra0))
            l_error("[bc] sitio de reloj: tid=0x%08X consulta desde libasphalt6.so+0x%08X",
                    (unsigned)ctid, (unsigned)(ra0 - (uint32_t)s_text_base));
        else
            l_error("[bc] sitio de reloj: tid=0x%08X consulta desde 0x%08X (fuera del .so)",
                    (unsigned)ctid, (unsigned)ra0);
    } else {
        l_error("[bc] sitio de reloj: sin consultas");
    }

    l_error("[bc] ---- fin del volcado (una ENTRA sin su 'sale' es donde se trabo) ----");
}
