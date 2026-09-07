/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/logger.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stdbool.h>
#include <stdatomic.h>

#define COLOR_RED    "\x1B[38;5;196m"
#define COLOR_PINK   "\x1B[38;5;212m"
#define COLOR_ORANGE "\x1B[38;5;202m"
#define COLOR_BLUE   "\x1B[38;5;32m"
#define COLOR_GREEN  "\x1B[32m"
#define COLOR_CYAN   "\x1B[36m"

#define COLOR_END    "\033[0m"

// Written on-device instead of streamed over UDP: `psvita-toolkit`'s FTP
// "download latest log" already looks in this exact directory
// (`vita_logs_dir` in .psvita-toolkit.json), so the log survives a run with
// no host-side listener that needs to be alive (and reachable) for the whole
// session. Fetch it with a plain FTP GET after the console reboots/reconnects.
#define LOG_DIR       DATA_PATH"logs"
#define LOG_INDEX_MIN 1
#define LOG_INDEX_MAX 999
#define LOG_NEXT_PATH LOG_DIR"/next.idx"

static SceKernelLwMutexWork _log_mutex;
static atomic_bool _log_mutex_ready = ATOMIC_VAR_INIT(false);

static SceUID _log_fd = -1;
static bool _log_file_tried = false;

// Buffer A is used to adjust the format string.
static char buffer_a[2048];
// Buffer B is used to compile the final log using the updated format string.
static char buffer_b[2048];
// Buffer C holds the plain (uncolourised) line written to the log file.
static char buffer_c[2048];

/** Short tag for the file sink, where ANSI colour is just noise. */
static const char * level_tag(int t) {
    switch (t) {
        case LT_DEBUG:   return "DEBUG";
        case LT_INFO:    return "INFO";
        case LT_WARN:    return "WARNING";
        case LT_ERROR:   return "ERROR";
        case LT_FATAL:   return "FATAL";
        case LT_SUCCESS: return "SUCCESS";
        case LT_WAIT:    return "WAITING";
        default:         return "?";
    }
}

/**
 * Read the remembered next index out of LOG_NEXT_PATH.
 *
 * @return A value in [LOG_INDEX_MIN, LOG_INDEX_MAX], or LOG_INDEX_MIN when the
 *         hint file is missing or unreadable.
 */
static int log_read_next_hint(void) {
    SceUID fd = sceIoOpen(LOG_NEXT_PATH, SCE_O_RDONLY, 0777);
    if (fd < 0)
        return LOG_INDEX_MIN;

    char buf[16];
    int read = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);

    if (read <= 0)
        return LOG_INDEX_MIN;
    buf[read] = '\0';

    int v = 0;
    for (const char * c = buf; *c >= '0' && *c <= '9'; ++c)
        v = v * 10 + (*c - '0');

    if (v < LOG_INDEX_MIN || v > LOG_INDEX_MAX)
        return LOG_INDEX_MIN;
    return v;
}

static void log_write_next_hint(int used_index) {
    int next = (used_index >= LOG_INDEX_MAX) ? LOG_INDEX_MIN : used_index + 1;

    SceUID fd = sceIoOpen(LOG_NEXT_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0)
        return;

    char buf[16];
    int len = sceClibSnprintf(buf, sizeof(buf), "%d\n", next);
    if (len > 0)
        sceIoWrite(fd, buf, len);
    sceIoClose(fd);
}

static bool log_slot_free(int i, char * path, unsigned int path_size) {
    sceClibSnprintf(path, path_size, LOG_DIR"/asphalt6_%03d.log", i);

    SceIoStat stat;
    return sceIoGetstat(path, &stat) < 0;
}

// Raw sceIo instead of stdio: every write goes straight to storage, so the
// last lines survive a data abort instead of sitting in an unflushed libc
// buffer. Safe to call more than once -- only the first call does anything.
//
// Files are numbered asphalt6_001.log .. asphalt6_999.log (LOG_NEXT_PATH
// remembers where to resume next boot) instead of a single overwritten
// filename, so a previous run's log isn't clobbered before it's been fetched.
static void log_file_open(void) {
    if (_log_file_tried) return;
    _log_file_tried = true;

    sceIoMkdir(LOG_DIR, 0777);

    int start = log_read_next_hint();
    char path[128];
    int claimed = -1;

    // Look for a free slot starting at the remembered index, wrapping once.
    for (int n = 0; n < LOG_INDEX_MAX; ++n) {
        int i = start + n;
        if (i > LOG_INDEX_MAX)
            i -= LOG_INDEX_MAX;
        if (log_slot_free(i, path, sizeof(path))) {
            claimed = i;
            break;
        }
    }

    // All 999 slots are taken: recycle the one the hint points at.
    if (claimed < 0) {
        claimed = start;
        sceClibSnprintf(path, sizeof(path), LOG_DIR"/asphalt6_%03d.log", claimed);
        sceIoRemove(path);
    }

    _log_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (_log_fd < 0)
        return;

    log_write_next_hint(claimed);
}

void _log_print(int t, const char* fmt, ...) {
    if (!atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        int ret = sceKernelCreateLwMutex(&_log_mutex, "log_lock", 0, 0, NULL);
        if (ret < 0) {
            sceClibPrintf("Error: failed to create log mutex: 0x%x\n", ret);
            return;
        }
        atomic_store_explicit(&_log_mutex_ready, true, memory_order_relaxed);
    }
    sceKernelLockLwMutex(&_log_mutex, 1, NULL);

    log_file_open();

    switch (t) {
        case LT_DEBUG:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s• debug%s    %s\n",
                            COLOR_PINK, COLOR_END, fmt); break;
        case LT_INFO:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %sℹ info%s     %s\n",
                            COLOR_BLUE, COLOR_END, fmt); break;
        case LT_WARN:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⚠ warning%s  %s\n",
                            COLOR_ORANGE, COLOR_END, fmt); break;
        case LT_ERROR:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⨯ error%s    %s\n",
                            COLOR_RED, COLOR_END, fmt); break;
        case LT_FATAL:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! fatal%s    %s\n",
                            COLOR_RED, COLOR_END, fmt); break;
        case LT_SUCCESS:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! success%s  %s\n",
                            COLOR_GREEN, COLOR_END, fmt); break;
        case LT_WAIT:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s… waiting%s  %s\n",
                            COLOR_CYAN, COLOR_END, fmt); break;
        default:
            if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
                sceKernelUnlockLwMutex(&_log_mutex, 1);
            }
            return;
    }

    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(buffer_b, sizeof(buffer_b), buffer_a, list);
    va_end(list);
    sceClibPrintf(buffer_b);

    if (_log_fd >= 0) {
        // Re-wrap fmt with a plain (no ANSI) prefix, then render it against a
        // fresh traversal of the same variadic args -- va_list can only be
        // consumed once per va_start/va_end pair, so buffer_a from above
        // can't be reused here.
        sceClibSnprintf(buffer_a, sizeof(buffer_a), "[%s] %s\n",
                        level_tag(t), fmt);

        va_list list2;
        va_start(list2, fmt);
        int len = sceClibVsnprintf(buffer_c, sizeof(buffer_c), buffer_a, list2);
        va_end(list2);

        if (len > 0) {
            if ((unsigned int)len >= sizeof(buffer_c))
                len = (int)sizeof(buffer_c) - 1;
            sceIoWrite(_log_fd, buffer_c, (unsigned int)len);
        }
    }

    if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        sceKernelUnlockLwMutex(&_log_mutex, 1);
    }
}

/*
 * Sumidero de los mensajes de FalsoJNI (lib/falso_jni/FalsoJNI_Logger.c).
 *
 * FalsoJNI imprime sus avisos con sceClibPrintf, que en una consola retail no va a ningun
 * lado que se pueda leer -- asi que sus mensajes mas utiles ("method ID 0 not found",
 * "GetMethodID: not found", "Could not find the array") eran invisibles. Aca se reenvian al
 * MISMO archivo que se baja por FTP.
 *
 * La linea ya viene formateada y con ANSI adentro; se le sacan los colores y el \n final
 * porque _log_print() pone los suyos.
 */
void fjni_log_sink(const char *line) {
    if (!line || !*line)
        return;

    char clean[1024];
    size_t o = 0;
    for (const char *p = line; *p && o + 1 < sizeof(clean); ++p) {
        if (*p == '\x1B') {
            // Secuencia ANSI: descartar hasta la letra final inclusive.
            while (*p && *p != 'm')
                ++p;
            if (!*p)
                break;
            continue;
        }
        if (*p == '\n' || *p == '\r')
            continue;
        clean[o++] = *p;
    }
    clean[o] = '\0';

    if (o > 0)
        _log_print(LT_ERROR, "[FalsoJNI] %s", clean);
}
