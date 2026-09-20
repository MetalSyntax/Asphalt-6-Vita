/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <pthread.h>
#include <malloc.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/breadcrumb.h"
#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

// Fix confirmed con so-crash-triage (dump asphalt6-psp2core-1788277870-0x0007bc2dcb,
// log logs/live_session_20260901_115057.log): el motor no usa los assets embebidos en el
// APK/JNI (GLResLoader) para los "file000000.dat"/etc. -- los abre por fopen() directo con
// una ruta absoluta de Android hardcodeada ("/sdcard/gameloft/games/GloftA6HP/..."), que en
// la Vita no existe -> fopen() devuelve NULL -> glitch::io::CFileSystem::createAndOpenFile()
// devuelve NULL -> crash sin chequeo en addObfuscationFileMap(). Los mismos fileNNNNNN.dat ya
// están en ux0_data/asphalt6/data/ (extraídos junto con el resto de los datos en Fase 4) --
// solo hace falta redirigir el prefijo, igual que ya se hace para /proc/cpuinfo /proc/meminfo.
#define GAMELOFT_SDCARD_PREFIX "/sdcard/gameloft/games/GloftA6HP/"

// Rewrites an absolute Gameloft/Android sdcard path to its real location under DATA_PATH.
// Returns true and fills `out` if `path` matched the prefix; false (out left untouched)
// otherwise, so callers can fall through to using `path` unchanged.
static bool remap_gameloft_sdcard_path(const char * path, char * out, size_t out_size) {
    size_t prefix_len = strlen(GAMELOFT_SDCARD_PREFIX);
    if (strncmp(path, GAMELOFT_SDCARD_PREFIX, prefix_len) != 0)
        return false;

    snprintf(out, out_size, DATA_PATH"data/%s", path + prefix_len);
    return true;
}

// Fallback defensivo (Bug #023): si un asset hace referencia a "IPAD2a_<Name>",
// quita el prefijo "IPAD2a_" para intentar resolver "<Name>" en el data set de Android.
static bool strip_ipad2_prefix(const char * path, char * out, size_t out_size) {
    const char * p = strstr(path, "IPAD2a_");
    if (!p)
        return false;

    size_t head_len = (size_t)(p - path);
    snprintf(out, out_size, "%.*s%s", (int)head_len, path, p + strlen("IPAD2a_"));
    return true;
}

// Misses ESPERADOS que no aportan nada al log (log 060: cientos de lineas por
// carga): el motor sondea cada SFX por nombre suelto ("sfx_*.wav", "vfx_*",
// "m_*", "*.vxn" sin ruta -- esos viven en file00a.bin via soundpack, nunca como
// archivo suelto) y cada textura PVRTC por archivo suelto ("*.PVRTC4.tga",
// "*NOMIPMAP*" -- esas viven en los fileNNNNNN.dat via el mapa de ofuscacion).
// El negative cache ya evita repetir el acceso a la SD; esto evita ademas la
// linea de log en Debug, que es un sceIoWrite por miss.
static int io_expected_miss(const char *filename) {
    if (!strchr(filename, '/')) {
        size_t n = strlen(filename);
        if (n > 4 && (strcmp(filename + n - 4, ".wav") == 0 ||
                      strcmp(filename + n - 4, ".vxn") == 0 ||
                      strcmp(filename + n - 4, ".png") == 0 ||
                      strcmp(filename + n - 4, ".tga") == 0))
            return 1;
    }
    if (strstr(filename, "NOMIPMAP") || strstr(filename, "PVRTC4") ||
        strstr(filename, ".car") || strcmp(filename, "ux0:data/asphalt6/data/glsl.config") == 0)
        return 1;
    return 0;
}
#define FCACHE_ENABLED 1
#define FCACHE_MAX_ENTRIES 1024
#define FCACHE_MAX_FILE_SIZE (4 * 1024 * 1024)
#define FCACHE_MAX_TOTAL_BYTES (48 * 1024 * 1024)
#define FCACHE_MAX_HANDLES 64

typedef struct {
    char path[256];
    unsigned char *data;
    long size;
} FCacheEntry;

typedef struct {
    int entry_idx; // -1 = free slot
    long pos;
} FCacheHandle;

static FCacheEntry s_fcache_entries[FCACHE_MAX_ENTRIES];
static int s_fcache_entry_count = 0;
static long s_fcache_total_bytes = 0;
static FCacheHandle s_fcache_handles[FCACHE_MAX_HANDLES];
static int s_fcache_handles_init = 0;
static pthread_mutex_t s_fcache_lock = PTHREAD_MUTEX_INITIALIZER;

#define NEG_CACHE_SIZE 2048
static char s_neg_cache[NEG_CACHE_SIZE][256];
static int s_neg_cache_count = 0;

static void fcache_init_handles_locked(void) {
    if (s_fcache_handles_init) return;
    for (int i = 0; i < FCACHE_MAX_HANDLES; i++) s_fcache_handles[i].entry_idx = -1;
    s_fcache_handles_init = 1;
}

static int fcache_find_entry_locked(const char *path) {
    for (int i = 0; i < s_fcache_entry_count; i++) {
        if (strcmp(s_fcache_entries[i].path, path) == 0) return i;
    }
    return -1;
}

static int fcache_is_handle(void *f) {
    uintptr_t p = (uintptr_t)f;
    uintptr_t base = (uintptr_t)s_fcache_handles;
    uintptr_t end = base + sizeof(s_fcache_handles);
    return p >= base && p < end && ((p - base) % sizeof(FCacheHandle)) == 0;
}

static FILE *fcache_open_handle_locked(int entry_idx) {
    fcache_init_handles_locked();
    for (int i = 0; i < FCACHE_MAX_HANDLES; i++) {
        if (s_fcache_handles[i].entry_idx == -1) {
            s_fcache_handles[i].entry_idx = entry_idx;
            s_fcache_handles[i].pos = 0;
            return (FILE *)&s_fcache_handles[i];
        }
    }
    return NULL;
}

static int fcache_is_cacheable_mode(const char *mode) {
#if !FCACHE_ENABLED
    (void)mode;
    return 0;
#else
    return strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0;
#endif
}

static void fcache_populate(const char *path, FILE *real_file) {
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
    pthread_mutex_lock(&s_fcache_lock);
    fcache_init_handles_locked();
    if (s_fcache_entry_count >= FCACHE_MAX_ENTRIES || strlen(path) >= sizeof(s_fcache_entries[0].path)) {
        pthread_mutex_unlock(&s_fcache_lock);
        return;
    }
    if (fcache_find_entry_locked(path) >= 0) {
        pthread_mutex_unlock(&s_fcache_lock);
        return;
    }
    pthread_mutex_unlock(&s_fcache_lock);

#ifdef USE_SCELIBC_IO
    sceLibcBridge_fseek(real_file, 0, SEEK_END);
    long size = sceLibcBridge_ftell(real_file);
    sceLibcBridge_fseek(real_file, 0, SEEK_SET);
#else
    fseek(real_file, 0, SEEK_END);
    long size = ftell(real_file);
    fseek(real_file, 0, SEEK_SET);
#endif
    if (size <= 0 || size > FCACHE_MAX_FILE_SIZE) return;

    pthread_mutex_lock(&s_fcache_lock);
    if (s_fcache_total_bytes + size > FCACHE_MAX_TOTAL_BYTES) {
        pthread_mutex_unlock(&s_fcache_lock);
        return;
    }
    pthread_mutex_unlock(&s_fcache_lock);

    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf) return;

#ifdef USE_SCELIBC_IO
    size_t got = sceLibcBridge_fread(buf, 1, (size_t)size, real_file);
    sceLibcBridge_fseek(real_file, 0, SEEK_SET);
#else
    size_t got = fread(buf, 1, (size_t)size, real_file);
    fseek(real_file, 0, SEEK_SET);
#endif
    if (got != (size_t)size) {
        free(buf);
        return;
    }

    pthread_mutex_lock(&s_fcache_lock);
    if (s_fcache_entry_count >= FCACHE_MAX_ENTRIES || fcache_find_entry_locked(path) >= 0) {
        pthread_mutex_unlock(&s_fcache_lock);
        free(buf);
        return;
    }
    FCacheEntry *e = &s_fcache_entries[s_fcache_entry_count++];
    strncpy(e->path, path, sizeof(e->path) - 1);
    e->path[sizeof(e->path) - 1] = '\0';
    e->data = buf;
    e->size = size;
    s_fcache_total_bytes += size;
    pthread_mutex_unlock(&s_fcache_lock);

    l_debug("[fcache] cached %s (%ld bytes, %ld/%d bytes total in %d files)",
            path, size, s_fcache_total_bytes, FCACHE_MAX_TOTAL_BYTES, s_fcache_entry_count);
}

void fcache_invalidate(const char *path) {
    if (!path) return;
    pthread_mutex_lock(&s_fcache_lock);
    for (int i = 0; i < s_fcache_entry_count; i++) {
        if (strcmp(s_fcache_entries[i].path, path) == 0) {
            free(s_fcache_entries[i].data);
            s_fcache_total_bytes -= s_fcache_entries[i].size;
            s_fcache_entries[i] = s_fcache_entries[--s_fcache_entry_count];
            l_debug("[fcache] invalidated %s (write/delete, %d files remain)",
                    path, s_fcache_entry_count);
            break;
        }
    }
    for (int i = 0; i < s_neg_cache_count; ) {
        if (strcmp(s_neg_cache[i], path) == 0) {
            if (i + 1 < s_neg_cache_count) {
                memmove(&s_neg_cache[i], &s_neg_cache[i + 1],
                        (size_t)(s_neg_cache_count - i - 1) * sizeof(s_neg_cache[0]));
            }
            s_neg_cache_count--;
        } else {
            i++;
        }
    }
    pthread_mutex_unlock(&s_fcache_lock);
}

FILE * fopen_soloader(const char * filename, const char * mode) {
    bc_event("fopen", BC_RA);
    if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

    char remapped[256];
    if (remap_gameloft_sdcard_path(filename, remapped, sizeof(remapped))) {
        return fopen_soloader(remapped, mode);
    }

    // Negative cache lookup: if previously failed to open for read, return NULL immediately
    if (strchr(mode, 'r')) {
        pthread_mutex_lock(&s_fcache_lock);
        for (int i = 0; i < s_neg_cache_count; i++) {
            if (strcmp(s_neg_cache[i], filename) == 0) {
                pthread_mutex_unlock(&s_fcache_lock);
                return NULL;
            }
        }
        pthread_mutex_unlock(&s_fcache_lock);
    }

    // Fast-path: RAM in-memory cache hit
    if (fcache_is_cacheable_mode(mode)) {
        pthread_mutex_lock(&s_fcache_lock);
        fcache_init_handles_locked();
        int entry_idx = fcache_find_entry_locked(filename);
        FILE *cached = (entry_idx >= 0) ? fcache_open_handle_locked(entry_idx) : NULL;
        pthread_mutex_unlock(&s_fcache_lock);
        if (cached) {
            l_debug("fopen(%s, %s): %p (cache hit)", filename, mode, cached);
            return cached;
        }
    }

#ifdef USE_SCELIBC_IO
    FILE* ret = sceLibcBridge_fopen(filename, mode);
#else
    FILE* ret = fopen(filename, mode);
#endif

    if (!ret && strip_ipad2_prefix(filename, remapped, sizeof(remapped))) {
        l_info("[io] Fallback IPAD2a_: reintentando fopen('%s')", remapped);
#ifdef USE_SCELIBC_IO
        ret = sceLibcBridge_fopen(remapped, mode);
#else
        ret = fopen(remapped, mode);
#endif
    }

    // Invalidate caches if opening with write intent
    if (mode && strpbrk(mode, "wa+") != NULL) {
        if (ret) fcache_invalidate(filename);
    }

    if (ret) {
#ifdef USE_SCELIBC_IO
        // Configure 64KB stdio buffer for high throughput streaming (TheFloW / Rinnegatamante practice)
        sceLibcBridge_setvbuf(ret, NULL, _IOFBF, 64 * 1024);
#endif
        if (fcache_is_cacheable_mode(mode)) {
            fcache_populate(filename, ret);
        }
        l_debug("fopen(%s, %s): %p", filename, mode, ret);
    } else {
        // Record non-existent file in negative cache to eliminate repeated SD searches
        if (strchr(mode, 'r')) {
            pthread_mutex_lock(&s_fcache_lock);
            if (s_neg_cache_count < NEG_CACHE_SIZE) {
                strncpy(s_neg_cache[s_neg_cache_count], filename, 255);
                s_neg_cache[s_neg_cache_count][255] = '\0';
                s_neg_cache_count++;
            }
            pthread_mutex_unlock(&s_fcache_lock);
        }
        if (!io_expected_miss(filename))
            l_warn("fopen(%s, %s): %p", filename, mode, ret);
    }

    return ret;
}

int open_soloader(const char * path, int oflag, ...) {
    bc_event("open", BC_RA);
    if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    }

    char remapped[256];
    if (remap_gameloft_sdcard_path(path, remapped, sizeof(remapped))) {
        return open_soloader(remapped, oflag);
    }

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    int write_intent = (oflag & (BIONIC_O_WRONLY | BIONIC_O_RDWR | BIONIC_O_CREAT |
                                 BIONIC_O_TRUNC | BIONIC_O_APPEND)) != 0;
    oflag = oflags_bionic_to_newlib(oflag);
    int ret = open(path, oflag, mode);
    if (ret < 0 && strip_ipad2_prefix(path, remapped, sizeof(remapped))) {
        l_info("[io] Fallback IPAD2a_: reintentando open('%s')", remapped);
        ret = open(remapped, oflag, mode);
    }

    if (write_intent && ret >= 0) {
        fcache_invalidate(path);
    }

    if (ret >= 0)
        l_debug("open(%s, %x): %i", path, oflag, ret);
    else
        l_warn("open(%s, %x): %i", path, oflag, ret);
    return ret;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    bc_event("stat", BC_RA);
    char remapped[256];
    if (remap_gameloft_sdcard_path(path, remapped, sizeof(remapped))) {
        return stat_soloader(remapped, buf);
    }

    // Negative cache lookup for stat
    pthread_mutex_lock(&s_fcache_lock);
    for (int i = 0; i < s_neg_cache_count; i++) {
        if (strcmp(s_neg_cache[i], path) == 0) {
            pthread_mutex_unlock(&s_fcache_lock);
            return -1;
        }
    }

    // FCache lookup for stat
    int entry_idx = fcache_find_entry_locked(path);
    if (entry_idx >= 0) {
        long size = s_fcache_entries[entry_idx].size;
        pthread_mutex_unlock(&s_fcache_lock);
        memset(buf, 0, sizeof(stat64_bionic));
        buf->st_mode = S_IFREG | 0666;
        buf->st_size = size;
        buf->st_blksize = 4096;
        buf->st_blocks = (size + 511) / 512;
        l_debug("stat(%s): 0 (fcache hit)", path);
        return 0;
    }
    pthread_mutex_unlock(&s_fcache_lock);

    struct stat st;
    int res = stat(path, &st);
    if (res != 0 && strip_ipad2_prefix(path, remapped, sizeof(remapped))) {
        l_info("[io] Fallback IPAD2a_: reintentando stat('%s')", remapped);
        res = stat(remapped, &st);
    }

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s): %i", path, res);
    return res;
}

int fclose_soloader(FILE * f) {
    if (fcache_is_handle(f)) {
        pthread_mutex_lock(&s_fcache_lock);
        ((FCacheHandle *)f)->entry_idx = -1;
        pthread_mutex_unlock(&s_fcache_lock);
        l_debug("fclose(%p): 0 (cache handle released)", f);
        return 0;
    }
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

size_t fread_soloader(void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (fcache_is_handle(f)) {
        pthread_mutex_lock(&s_fcache_lock);
        FCacheHandle *h = (FCacheHandle *)f;
        FCacheEntry *e = &s_fcache_entries[h->entry_idx];
        long remaining = e->size - h->pos;
        if (remaining < 0) remaining = 0;
        size_t avail_items = (size == 0) ? 0 : ((size_t)remaining) / size;
        size_t items = avail_items < nmemb ? avail_items : nmemb;
        if (items > 0) {
            memcpy(ptr, e->data + h->pos, items * size);
            h->pos += (long)(items * size);
        }
        pthread_mutex_unlock(&s_fcache_lock);
        return items;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fread(ptr, size, nmemb, f);
#else
    return fread(ptr, size, nmemb, f);
#endif
}

size_t fwrite_soloader(const void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (fcache_is_handle(f)) {
        l_warn("fwrite(%p): refused, read-only cache handle", f);
        return 0;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fwrite(ptr, size, nmemb, f);
#else
    return fwrite(ptr, size, nmemb, f);
#endif
}

int fseek_soloader(FILE *f, long offset, int whence) {
    if (fcache_is_handle(f)) {
        pthread_mutex_lock(&s_fcache_lock);
        FCacheHandle *h = (FCacheHandle *)f;
        FCacheEntry *e = &s_fcache_entries[h->entry_idx];
        long base = (whence == SEEK_SET) ? 0 : (whence == SEEK_CUR) ? h->pos : e->size;
        long newpos = base + offset;
        int ok = newpos >= 0;
        if (ok) h->pos = newpos;
        pthread_mutex_unlock(&s_fcache_lock);
        return ok ? 0 : -1;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fseek(f, offset, whence);
#else
    return fseek(f, offset, whence);
#endif
}

long ftell_soloader(FILE *f) {
    if (fcache_is_handle(f)) {
        return ((FCacheHandle *)f)->pos;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_ftell(f);
#else
    return ftell(f);
#endif
}

int fseeko_soloader(FILE *f, off_t offset, int whence) {
    return fseek_soloader(f, (long)offset, whence);
}

off_t ftello_soloader(FILE *f) {
    return (off_t)ftell_soloader(f);
}

void rewind_soloader(FILE *f) {
    if (fcache_is_handle(f)) {
        pthread_mutex_lock(&s_fcache_lock);
        ((FCacheHandle *)f)->pos = 0;
        pthread_mutex_unlock(&s_fcache_lock);
        return;
    }
    rewind(f);
}

int feof_soloader(FILE *f) {
    if (fcache_is_handle(f)) {
        pthread_mutex_lock(&s_fcache_lock);
        FCacheHandle *h = (FCacheHandle *)f;
        int at_eof = h->pos >= s_fcache_entries[h->entry_idx].size;
        pthread_mutex_unlock(&s_fcache_lock);
        return at_eof;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_feof(f);
#else
    return feof(f);
#endif
}

int ferror_soloader(FILE *f) {
    if (fcache_is_handle(f)) return 0;
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_ferror(f);
#else
    return ferror(f);
#endif
}

int fflush_soloader(FILE *f) {
    if (fcache_is_handle(f)) return 0;
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fflush(f);
#else
    return fflush(f);
#endif
}

int fgetc_soloader(FILE *f) {
    if (fcache_is_handle(f)) {
        unsigned char c;
        return fread_soloader(&c, 1, 1, f) == 1 ? (int)c : EOF;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fgetc(f);
#else
    return fgetc(f);
#endif
}

int getc_soloader(FILE *f) {
    return fgetc_soloader(f);
}

int fputc_soloader(int c, FILE *f) {
    if (fcache_is_handle(f)) {
        l_warn("fputc(%p): refused, read-only cache handle", f);
        return EOF;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fputc(c, f);
#else
    return fputc(c, f);
#endif
}

int putc_soloader(int c, FILE *f) {
    return fputc_soloader(c, f);
}

char *fgets_soloader(char *str, int n, FILE *f) {
    if (fcache_is_handle(f)) {
        if (n <= 0) return NULL;
        int i = 0;
        for (; i < n - 1; i++) {
            int c = fgetc_soloader(f);
            if (c == EOF) {
                if (i == 0) return NULL;
                break;
            }
            str[i] = (char)c;
            if (c == '\n') { i++; break; }
        }
        str[i] = '\0';
        return str;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fgets(str, n, f);
#else
    return fgets(str, n, f);
#endif
}

int fputs_soloader(const char *str, FILE *f) {
    if (fcache_is_handle(f)) {
        l_warn("fputs(%p): refused, read-only cache handle", f);
        return EOF;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fputs(str, f);
#else
    return fputs(str, f);
#endif
}

int fileno_soloader(FILE *f) {
    if (fcache_is_handle(f)) {
        return -1;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fileno(f);
#else
    return fileno(f);
#endif
}

int setvbuf_soloader(FILE *f, char *buf, int mode, size_t size) {
    if (fcache_is_handle(f)) return 0;
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_setvbuf(f, buf, mode, size);
#else
    return setvbuf(f, buf, mode, size);
#endif
}

int ungetc_soloader(int c, FILE *f) {
    if (fcache_is_handle(f)) {
        pthread_mutex_lock(&s_fcache_lock);
        FCacheHandle *h = (FCacheHandle *)f;
        int ok = h->pos > 0;
        if (ok) h->pos--;
        pthread_mutex_unlock(&s_fcache_lock);
        return ok ? c : EOF;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_ungetc(c, f);
#else
    return ungetc(c, f);
#endif
}

int remove_soloader(const char *pathname) {
    fcache_invalidate(pathname);
    return remove(pathname);
}

int unlink_soloader(const char *pathname) {
    fcache_invalidate(pathname);
    return unlink(pathname);
}

int close_soloader(int fd) {
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    bc_event("opendir", BC_RA);
    DIR* ret = opendir(_pathname);
    l_debug("opendir(\"%s\"): %p", _pathname, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}
