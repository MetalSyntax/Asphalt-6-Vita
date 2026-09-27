/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022      GrapheneCt
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/pthr.h"

#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <stdatomic.h>

#include "utils/breadcrumb.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/watchdog.h"


#define BIONIC_PTHREAD_COND_INITIALIZER              0
#define BIONIC_PTHREAD_MUTEX_INITIALIZER             0
#define BIONIC_PTHREAD_RECURSIVE_MUTEX_INITIALIZER   0x4000
#define BIONIC_PTHREAD_ERRORCHECK_MUTEX_INITIALIZER  0x8000

enum {
    BIONIC_PTHREAD_MUTEX_NORMAL = 0,
    BIONIC_PTHREAD_MUTEX_RECURSIVE = 1,
    BIONIC_PTHREAD_MUTEX_ERRORCHECK = 2,

    BIONIC_PTHREAD_MUTEX_ERRORCHECK_NP = BIONIC_PTHREAD_MUTEX_ERRORCHECK,
    BIONIC_PTHREAD_MUTEX_RECURSIVE_NP  = BIONIC_PTHREAD_MUTEX_RECURSIVE,

    BIONIC_PTHREAD_MUTEX_DEFAULT = BIONIC_PTHREAD_MUTEX_NORMAL
};

#define PTHR_INLINE static inline __attribute__((always_inline))

static SceKernelLwMutexWork pthr_mutex;
static volatile short int pthr_mutex_inited = 0;

#define PTHR_LOCK \
    if (!pthr_mutex_inited) { \
        int ret = sceKernelCreateLwMutex(&pthr_mutex, "log_lock", 0, 0, NULL); \
        if (ret < 0) { \
            sceClibPrintf("Error: failed to create pthr mutex: 0x%x\n", ret); \
            return 0; \
        } \
        pthr_mutex_inited = 1; \
    } \
    sceKernelLockLwMutex(&pthr_mutex, 1, NULL);

#define PTHR_UNLOCK \
    if (pthr_mutex_inited) { \
        sceKernelUnlockLwMutex(&pthr_mutex, 1); \
    }

/*
 * Registro de mutex/cond ya inicializados (los de Bionic son 4 bytes que estas
 * funciones reemplazan por un puntero al objeto real; los estaticos llegan en 0 y se
 * inicializan en el primer uso).
 *
 * Antes era un array lineal de 1024 punteros recorrido ENTERO bajo un LwMutex global
 * en CADA pthread_mutex_lock/cond_wait del motor: miles de veces por frame, cada una
 * con hasta 1024 comparaciones y dos llamadas al kernel. Ahora es una tabla hash con
 * sondeo lineal cuya LECTURA no toma lock (palabras alineadas, atomicas en ARM); solo
 * alta/baja van bajo el LwMutex. Mismas semanticas que el array: "inicializado" =
 * puntero presente en la tabla. Las bajas dejan una lapida; cuando las lapidas se
 * acumulan se reconstruye en la OTRA tabla y se publica con un solo store, asi un
 * lector que todavia recorre la vieja ve un estado valido.
 */
#define PTHR_HASH_SIZE 4096u              // potencia de 2; carga maxima 50%
#define PTHR_HASH_MASK (PTHR_HASH_SIZE - 1u)
#define PTHR_TOMBSTONE ((void *) 1)

static void *pthr_table_a[PTHR_HASH_SIZE];
static void *pthr_table_b[PTHR_HASH_SIZE];
static void ** volatile pthr_table = pthr_table_a;
static unsigned pthr_used = 0;            // vivos + lapidas en la tabla actual
static unsigned pthr_live = 0;

static inline unsigned pthr_hash(const void *p) {
    return ((uint32_t)(uintptr_t) p * 2654435761u) >> 20; // 12 bits altos
}

int isObjectInitialized(const void * mut) {
    void **t = pthr_table;
    for (unsigned n = 0, i = pthr_hash(mut); n < PTHR_HASH_SIZE; ++n, i = (i + 1) & PTHR_HASH_MASK) {
        void *v = __atomic_load_n(&t[i], __ATOMIC_ACQUIRE);
        if (v == mut) return 1;
        if (v == NULL) return 0;
    }
    return 0;
}

// Bajo PTHR_LOCK. Reconstruye sin lapidas en la tabla que no esta en uso.
static void pthr_rehash_locked(void) {
    void **old = pthr_table;
    void **nt = (old == pthr_table_a) ? pthr_table_b : pthr_table_a;
    memset(nt, 0, sizeof(pthr_table_a));
    unsigned live = 0;
    for (unsigned j = 0; j < PTHR_HASH_SIZE; ++j) {
        void *v = old[j];
        if (v == NULL || v == PTHR_TOMBSTONE) continue;
        unsigned i = pthr_hash(v);
        while (nt[i]) i = (i + 1) & PTHR_HASH_MASK;
        nt[i] = v;
        live++;
    }
    __atomic_store_n(&pthr_table, nt, __ATOMIC_RELEASE);
    pthr_used = pthr_live = live;
}

int rememberObject(void * mut) {
    PTHR_LOCK
    if (pthr_live >= PTHR_HASH_SIZE / 2) {
        PTHR_UNLOCK
        return 0;
    }
    if (pthr_used >= PTHR_HASH_SIZE / 2)
        pthr_rehash_locked();
    void **t = pthr_table;
    unsigned i = pthr_hash(mut);
    while (t[i] != NULL && t[i] != PTHR_TOMBSTONE && t[i] != mut)
        i = (i + 1) & PTHR_HASH_MASK;
    if (t[i] != mut) {
        if (t[i] == NULL) pthr_used++;
        __atomic_store_n(&t[i], mut, __ATOMIC_RELEASE);
        pthr_live++;
    }
    PTHR_UNLOCK
    return 1;
}

int forgetObject(const void * mut) {
    PTHR_LOCK
    void **t = pthr_table;
    for (unsigned n = 0, i = pthr_hash(mut); n < PTHR_HASH_SIZE; ++n, i = (i + 1) & PTHR_HASH_MASK) {
        if (t[i] == mut) {
            __atomic_store_n(&t[i], PTHR_TOMBSTONE, __ATOMIC_RELEASE);
            pthr_live--;
            PTHR_UNLOCK
            return 1;
        }
        if (t[i] == NULL) break;
    }
    PTHR_UNLOCK
    return 0;
}

// null check for `attr` must be performed before this
PTHR_INLINE int _attr_t_static_init(pthread_attr_t_bionic * attr) {
    if (attr->magic != 0x42424242) {
        attr->magic = 0x42424242;
        attr->real_ptr = malloc(sizeof(pthread_attr_t));
        return pthread_attr_init(attr->real_ptr);
    }
    return 0;
}

// null check for `mutex` param must be performed before this, `attr` is fine as null
PTHR_INLINE int _mutex_t_static_init(pthread_mutex_t_bionic * mutex, const pthread_mutexattr_t * attr) {
    int ret = 0, kind = PTHREAD_MUTEX_NORMAL;

    if (isObjectInitialized(mutex)) {
        //logv_debug("mutex already initialized: %p", mutex);
        return ret;
    }

    if (attr) {
        pthread_mutexattr_gettype((pthread_mutexattr_t *) attr, &kind);
    } else {
        if (* (int *) mutex == BIONIC_PTHREAD_MUTEX_INITIALIZER) kind = PTHREAD_MUTEX_NORMAL;
        else if (* (int *) mutex == BIONIC_PTHREAD_RECURSIVE_MUTEX_INITIALIZER) kind = PTHREAD_MUTEX_RECURSIVE;
        else if (* (int *) mutex == BIONIC_PTHREAD_ERRORCHECK_MUTEX_INITIALIZER) kind = PTHREAD_MUTEX_ERRORCHECK;
    }

    pthread_mutex_t mut;
    mutex->real_ptr = malloc(sizeof(pthread_mutex_t));
    sceClibMemcpy(mutex->real_ptr, &mut, sizeof(pthread_mutex_t));

    pthread_mutexattr_t mutattr;
    pthread_mutexattr_init(&mutattr);
    pthread_mutexattr_settype(&mutattr, kind);
    ret = pthread_mutex_init(mutex->real_ptr, &mutattr);
    pthread_mutexattr_destroy(&mutattr);

    if (ret == 0) {
        rememberObject(mutex);
    } else {
        l_error("mutex initialization for %p has failed", mutex);
    }

    return ret;
}

// null check for `cond` param must be performed before this, `attr` is fine as null
PTHR_INLINE int _cond_t_static_init(pthread_cond_t_bionic * cond, const pthread_condattr_t * attr) {
    int ret = 0;

    if (isObjectInitialized(cond)) {
        //logv_debug("cond already initialized: %p", cond);
        return ret;
    }

    pthread_cond_t c;
    cond->real_ptr = malloc(sizeof(pthread_cond_t));
    sceClibMemcpy(cond->real_ptr, &c, sizeof(pthread_cond_t));

    ret = pthread_cond_init(cond->real_ptr, attr);

    if (ret == 0) {
        rememberObject(cond);
    } else {
        l_error("cond initialization for %p has failed", cond);
    }

    return ret;
}

int pthread_create_soloader(pthread_t *thread, const pthread_attr_t_bionic *attr, void *(*start)(void *), void *param) {
    int ret;

    if (!attr) {
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setstacksize(&a, 512 * 1024);
        ret = pthread_create(thread, &a, start, param);
        pthread_attr_destroy(&a);
    } else{
        _attr_t_static_init((pthread_attr_t_bionic *) attr);
        pthread_attr_setstacksize(attr->real_ptr, 512 * 1024);
        ret = pthread_create(thread, attr->real_ptr, start, param);
    }

    /*
     * Todo hilo que crea el .so entra a la lista del hilo testigo. En la implementacion de
     * pthreads de vitasdk el `pthread_t` ES el UID del hilo de sceKernel, asi que se puede
     * consultar con sceKernelGetThreadInfo(); si algun dia dejara de serlo, el testigo lo
     * reporta una vez y sigue. Sin esto no hay forma de ver, cuando el juego se traba, si
     * hay un hilo del motor girando en un bucle cerrado o bloqueado en un mutex.
     */
    bc_event("pthread_create", BC_RA);
    if (ret == 0 && thread)
        watchdog_register_thread((int)*thread, "hilo-del-.so");

    return ret;
}

int pthread_mutexattr_init_soloader(pthread_mutexattr_t *attr)
{
    return pthread_mutexattr_init(attr);
}

int pthread_mutexattr_settype_soloader(pthread_mutexattr_t *attr, int type)
{
    return pthread_mutexattr_settype(attr, type);
}

int pthread_mutexattr_destroy_soloader(pthread_mutexattr_t *attr)
{
    return pthread_mutexattr_destroy(attr);
}

int pthread_kill_soloader(pthread_t thread, int sig)
{
    return pthread_kill(thread, sig);
}

int pthread_mutex_init_soloader(pthread_mutex_t_bionic *uid, const pthread_mutexattr_t *attr)
{
    if (!uid) return EINVAL;
    return _mutex_t_static_init(uid, attr);
}

int pthread_mutex_destroy_soloader(pthread_mutex_t_bionic *mutex)
{
    if (!mutex) return 0;
    forgetObject(mutex);
    int ret = pthread_mutex_destroy(mutex->real_ptr);
    if (mutex->real_ptr) free(mutex->real_ptr);
    mutex->real_ptr = 0x0;
    return ret;
}

int pthread_mutex_lock_soloader(pthread_mutex_t_bionic *mutex)
{
    if (!mutex) return EINVAL;
    _mutex_t_static_init(mutex, NULL);
    // Camino rapido sin migas: el motor (y vox) toman mutex miles de veces por frame y
    // bc_push cuesta dos syscalls + registro en el testigo por cada ENTRA/SALE. Un
    // cuelgue en un lock siempre es un lock CONTENDIDO, asi que las migas solo se dejan
    // cuando trylock falla -- el diagnostico de cuelgues queda igual.
    bc_spin_mutex();
    if (pthread_mutex_trylock(mutex->real_ptr) == 0)
        return 0;
    BC_SCOPE("pthread_mutex_lock");
    return pthread_mutex_lock(mutex->real_ptr);
}

int pthread_mutex_trylock_soloader(pthread_mutex_t_bionic *mutex)
{
    if (!mutex) return EINVAL;
    _mutex_t_static_init(mutex, NULL);
    return pthread_mutex_trylock(mutex->real_ptr);
}

int pthread_mutex_unlock_soloader(pthread_mutex_t_bionic *mutex)
{
    if (!mutex) return EINVAL;
    if (!mutex->real_ptr) return EINVAL;
    return pthread_mutex_unlock(mutex->real_ptr);
}

int pthread_join_soloader(pthread_t thread, void **value_ptr)
{
    BC_SCOPE("pthread_join");
    return pthread_join(thread, value_ptr);
}

int pthread_condattr_init_soloader(pthread_condattr_t *attr)
{
    if (!attr) return EINVAL;
    return pthread_condattr_init(attr);
}

int pthread_condattr_destroy_soloader(pthread_condattr_t *attr)
{
    if (!attr) return EINVAL;
    return pthread_condattr_destroy(attr);
}

int pthread_cond_init_soloader(pthread_cond_t_bionic *cond,
                               const pthread_condattr_t *attr)
{
    if (!cond) return EINVAL;

    return _cond_t_static_init(cond, attr);
}

int pthread_cond_destroy_soloader(pthread_cond_t_bionic *cond)
{
    if (!cond) return 0;
    forgetObject(cond);
    int ret = pthread_cond_destroy(cond->real_ptr);
    if (cond->real_ptr) free(cond->real_ptr);
    cond->real_ptr = 0x0;
    return ret;
}

int pthread_cond_signal_soloader(pthread_cond_t_bionic *cond)
{
    if (!cond) return EINVAL;

    _cond_t_static_init(cond, NULL);

    return pthread_cond_signal(cond->real_ptr);
}

int pthread_cond_timedwait_soloader(pthread_cond_t_bionic *cond, pthread_mutex_t_bionic *mutex, struct timespec *abstime)
{
    if (!cond || !mutex) return EINVAL;

    _cond_t_static_init(cond, NULL);
    _mutex_t_static_init(mutex, NULL);

    BC_SCOPE("pthread_cond_timedwait");
    return pthread_cond_timedwait(cond->real_ptr, mutex->real_ptr, abstime);
}


int pthread_cond_wait_soloader(pthread_cond_t_bionic *cond, pthread_mutex_t_bionic *mutex)
{
    if (!cond || !mutex) return EINVAL;

    _cond_t_static_init(cond, NULL);
    _mutex_t_static_init(mutex, NULL);

    BC_SCOPE("pthread_cond_wait");
    return pthread_cond_wait(cond->real_ptr, mutex->real_ptr);
}

int pthread_cond_broadcast_soloader(pthread_cond_t_bionic *cond)
{
    if (!cond) return EINVAL;

    _cond_t_static_init(cond, NULL);

    return pthread_cond_broadcast(cond->real_ptr);
}

int pthread_attr_init_soloader(pthread_attr_t_bionic *attr)
{
    if (!attr) return EINVAL;

    return _attr_t_static_init(attr);
}

int pthread_attr_destroy_soloader(pthread_attr_t_bionic *attr)
{
    if (!attr) return 0;
    if (attr->magic != 0x42424242) return 0;

    int ret = pthread_attr_destroy(attr->real_ptr);
    free(attr->real_ptr);
    attr->magic = 0x0;

    return ret;
}

int pthread_attr_setdetachstate_soloader(pthread_attr_t_bionic *attr, int state)
{
    if (!attr) return -1;
    _attr_t_static_init(attr);
    return pthread_attr_setdetachstate(attr->real_ptr, state);
}

int pthread_attr_setstacksize_soloader(pthread_attr_t_bionic *attr, size_t stacksize) {
    if (!attr) return -1;
    _attr_t_static_init(attr);
    return pthread_attr_setstacksize(attr->real_ptr, stacksize);
}

int pthread_setschedparam_soloader(pthread_t thread, int policy,
                                   const struct sched_param *param)
{
   return pthread_setschedparam(thread, policy, param);
}

int pthread_getschedparam_soloader(pthread_t thread, int *policy,
                                   struct sched_param *param)
{
    return pthread_getschedparam(thread, policy, param);
}

int pthread_detach_soloader(pthread_t thread)
{
    return pthread_detach(thread);
}

int pthread_equal_soloader(const pthread_t t1, const pthread_t t2)
{
    if (t1 == t2)
        return 1;
    if (!t1 || !t2)
        return 0;
    return pthread_equal(t1, t2);
}

pthread_t pthread_self_soloader()
{
    return pthread_self();
}

int pthread_once_soloader(volatile int *once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine)
        return -1;
    if (__sync_lock_test_and_set(once_control, 1) == 0)
        (*init_routine)();
    return 0;
}

#ifndef MAX_TASK_COMM_LEN
#define MAX_TASK_COMM_LEN 16
#endif

int pthread_setname_np_soloader(pthread_t thread, const char* thread_name) {
    if (thread == 0 || thread_name == NULL) {
        return EINVAL;
    }
    size_t thread_name_len = strlen(thread_name);
    if (thread_name_len >= MAX_TASK_COMM_LEN) {
        return ERANGE;
    }

    sceClibPrintf("PTHREAD: pthread_setname_np with name %s for thread:0x%x\n", thread_name, pthread_self());

    return 0;
}

int sem_destroy_soloader(int * uid) {
    if (sceKernelDeleteSema(*uid) < 0)
        return -1;
    return 0;
}

int sem_getvalue_soloader (int * uid, int * sval) {
    SceKernelSemaInfo info;
    info.size = sizeof(SceKernelSemaInfo);

    if (sceKernelGetSemaInfo(*uid, &info) < 0) return -1;
    if (!sval) sval = malloc(sizeof(int32_t));
    *sval = info.currentCount;
    return 0;
}

int sem_init_soloader (int * uid, int pshared, unsigned int value) {
    *uid = sceKernelCreateSema("sema", 0, (int) value, 0x7fffffff, NULL);
    if (*uid < 0)
        return -1;
    return 0;
}

int sem_post_soloader (int * uid) {
    if (sceKernelSignalSema(*uid, 1) < 0)
        return -1;
    return 0;
}

int sem_timedwait_soloader (int * uid, const struct timespec * abstime) {
    uint timeout = 1000;
    if (sceKernelWaitSema(*uid, 1, &timeout) >= 0)
        return 0;
    if (!abstime) return -1;
    long long now = (long long) current_timestamp_ms() * 1000; // us
    long long _timeout = abstime->tv_sec * 1000 * 1000 + abstime->tv_nsec / 1000; // us
    if (_timeout-now >= 0) return -1;
    uint timeout_real = _timeout - now;
    if (sceKernelWaitSema(*uid, 1, &timeout_real) < 0)
        return -1;
    return 0;
}

int sem_trywait_soloader (int * uid) {
    uint timeout = 1000;
    if (sceKernelWaitSema(*uid, 1, &timeout) < 0)
        return -1;
    return 0;
}

int sem_wait_soloader (int * uid) {
    if (sceKernelWaitSema(*uid, 1, NULL) < 0)
        return -1;
    return 0;
}
