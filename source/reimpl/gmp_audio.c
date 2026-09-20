/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/gmp_audio.h"
#include "reimpl/soundpack.h"

#include "utils/logger.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#define MIX_RATE   48000  // SCE_AUDIO_OUT_PORT_TYPE_MAIN exige 48000 en este hardware
                           // (confirmado: a 44100 sceAudioOutOpenPort devuelve
                           // 0x80260008/INVALID_SAMPLE_FREQ -- ver Bug #036, log 058).
#define MIX_GRAIN  1024
#define MAX_VOICES 8
// El motor referencia musica/sonidos por un indice entero de registerSoundFile(); no hay
// forma de saber su rango real sin ver todos los call sites, asi que se acota generoso
// (el pack completo tiene 630 nombres, ver soundpack.c) y se descartan con un warning los
// que caigan afuera en vez de arriesgar un acceso fuera de array.
#define MAX_INDEX  1024

typedef struct {
    int16_t *pcm;      // PCM16 intercalado, mono o stereo
    unsigned frames;
    int channels;
    int rate;
} SfxSample;

typedef struct {
    SfxSample *smp;
    unsigned pos_int, pos_frac;
    unsigned step_int, step_frac;
    float gain, targetGain;
    int fadeFramesLeft;
    float gainStep;
    int loop;
    int paused;
    int index;
    int instance;
} Voice;

#define SFX_FAILED ((SfxSample *) -1)

static pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
static int gReady = 0;          // 1 una vez que el puerto+hilo arrancaron con exito
static int gPortFailed = 0;     // evita reintentar sceAudioOutOpenPort en cada llamada
static volatile int gQuit = 0;
static int gPort = -1;
static pthread_t gThread;

static char *gNames[MAX_INDEX];       // index -> nombre real (registerSoundFile)
static SfxSample *gCache[MAX_INDEX];  // index -> sample decodificado (o SFX_FAILED)

static Voice gMusic;
static Voice gVoices[MAX_VOICES];
static int gNextInstance = 1;

static float gMasterVolume = 1.0f;
static float gMusicVolume = 1.0f;

/*
 * Decodifica un WAV PCM16 mono/stereo desde un buffer en RAM (ya leido de file00a.bin por
 * soundpack_read()). Mismo parseo de chunks RIFF/fmt/data que usa Asphalt-5-Vita
 * (source/audio.cpp, decode_wav_file) para sus propios .wav sueltos -- acá lee de memoria
 * en vez de un FILE* porque el WAV completo ya está en RAM (soundpack lo entregó así).
 */
static int decode_wav_mem(const uint8_t *buf, size_t len, SfxSample *out) {
    if (len < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0)
        return 0;

    size_t pos = 12;
    int channels = 0, rate = 0, bits = 0;
    int haveFmt = 0, haveData = 0;
    const uint8_t *dataPtr = NULL;
    uint32_t dataSize = 0;

    while (pos + 8 <= len) {
        const uint8_t *hdr = buf + pos;
        uint32_t chunkSize = (uint32_t) hdr[4] | ((uint32_t) hdr[5] << 8) |
                             ((uint32_t) hdr[6] << 16) | ((uint32_t) hdr[7] << 24);

        if (memcmp(hdr, "fmt ", 4) == 0) {
            if (pos + 8 + 16 > len) break;
            const uint8_t *fb = buf + pos + 8;
            channels = fb[2] | (fb[3] << 8);
            rate = (int) ((uint32_t) fb[4] | ((uint32_t) fb[5] << 8) |
                          ((uint32_t) fb[6] << 16) | ((uint32_t) fb[7] << 24));
            bits = fb[14] | (fb[15] << 8);
            haveFmt = 1;
        } else if (memcmp(hdr, "data", 4) == 0) {
            if (pos + 8 + chunkSize > len) chunkSize = (uint32_t) (len - pos - 8);
            dataPtr = buf + pos + 8;
            dataSize = chunkSize;
            haveData = 1;
            break; // no hace falta seguir leyendo chunks despues de "data"
        }
        pos += 8 + chunkSize + (chunkSize & 1);
    }

    if (!haveFmt || !haveData || channels < 1 || channels > 2 || rate <= 0 ||
            dataSize == 0 || bits != 16)
        return 0;

    int16_t *pcm = (int16_t *) malloc(dataSize);
    if (!pcm) return 0;
    memcpy(pcm, dataPtr, dataSize);

    out->pcm = pcm;
    out->channels = channels;
    out->rate = rate;
    out->frames = dataSize / ((unsigned) channels * sizeof(int16_t));
    return 1;
}

/*
 * Carga perezosa: la primera vez que se pide un índice se lee y decodifica de una, las
 * siguientes veces devuelve el mismo puntero cacheado. Los WAV de este pack son chicos
 * (unos KB a ~2 MB el más grande visto) y la decodificación es un memcpy sin códec, así
 * que a diferencia de Asphalt-5-Vita (OGG/MP3, hasta 4+ segundos por archivo) no hizo
 * falta un hilo cargador aparte -- si algún WAV puntual resulta pesado y se nota como un
 * hitch al pedirlo por primera vez, ese es el próximo lugar para mirar.
 */
static SfxSample *sfx_get(int index) {
    if (index < 0 || index >= MAX_INDEX) return NULL;

    SfxSample *cached = gCache[index];
    if (cached == SFX_FAILED) return NULL;
    if (cached) return cached;

    const char *name = gNames[index];
    if (!name) {
        l_warn("[gmp_audio] index %d pedido sin registerSoundFile previo", index);
        gCache[index] = SFX_FAILED;
        return NULL;
    }

    const SoundPackEntry *e = soundpack_find(name);
    if (!e) {
        l_warn("[gmp_audio] '%s' (index %d) no esta en file00a.bin", name, index);
        gCache[index] = SFX_FAILED;
        return NULL;
    }

    uint8_t *raw = (uint8_t *) malloc(e->dataSize);
    if (!raw || !soundpack_read(e, raw)) {
        free(raw);
        gCache[index] = SFX_FAILED;
        return NULL;
    }

    SfxSample *s = (SfxSample *) calloc(1, sizeof(SfxSample));
    int ok = s && decode_wav_mem(raw, e->dataSize, s);
    free(raw);
    if (!ok) {
        free(s);
        l_warn("[gmp_audio] '%s' (index %d) no decodifico como WAV PCM16 valido", name, index);
        gCache[index] = SFX_FAILED;
        return NULL;
    }

    l_info("[gmp_audio] cargado '%s' (index %d, %d Hz, %d ch, %u frames)",
           name, index, s->rate, s->channels, s->frames);
    gCache[index] = s;
    return s;
}

static void voice_set_step(Voice *v, int rate, float pitch) {
    if (pitch < 0.25f) pitch = 0.25f;
    if (pitch > 3.0f) pitch = 3.0f;
    double step_d = ((double) rate / (double) MIX_RATE) * (double) pitch;
    v->step_int = (unsigned) step_d;
    v->step_frac = (unsigned) ((step_d - (double) v->step_int) * 4294967296.0);
}

// Mezcla por interpolacion lineal, igual patron que Asphalt-5-Vita (source/audio.cpp,
// mix_voice) -- el paso fraccionario ya resamplea de la frecuencia real del WAV a MIX_RATE
// sin necesitar un resampler aparte.
static void mix_voice(Voice *v, int *accL, int *accR, int frames) {
    if (!v->smp || v->paused) return;
    SfxSample *s = v->smp;

    for (int i = 0; i < frames; i++) {
        if (v->fadeFramesLeft > 0) {
            v->gain += v->gainStep;
            v->fadeFramesLeft--;
            if (v->fadeFramesLeft == 0) v->gain = v->targetGain;
        }

        unsigned f0 = v->pos_int;
        if (f0 >= s->frames) {
            if (v->loop && s->frames > 0) {
                while (v->pos_int >= s->frames) v->pos_int -= s->frames;
                f0 = v->pos_int;
            } else {
                v->smp = NULL;
                return;
            }
        }

        unsigned f1 = f0 + 1;
        if (f1 >= s->frames) f1 = v->loop ? 0 : f0;

        int frac = (int) (v->pos_frac >> 24);
        int sl0, sr0, sl1, sr1;
        if (s->channels == 1) {
            sl0 = sr0 = s->pcm[f0];
            sl1 = sr1 = s->pcm[f1];
        } else {
            sl0 = s->pcm[f0 * 2];
            sr0 = s->pcm[f0 * 2 + 1];
            sl1 = s->pcm[f1 * 2];
            sr1 = s->pcm[f1 * 2 + 1];
        }
        int sl = sl0 + (((sl1 - sl0) * frac) >> 8);
        int sr = sr0 + (((sr1 - sr0) * frac) >> 8);

        accL[i] += (int) (sl * v->gain);
        accR[i] += (int) (sr * v->gain);

        v->pos_frac += v->step_frac;
        if (v->pos_frac < v->step_frac) v->pos_int += v->step_int + 1;
        else v->pos_int += v->step_int;
    }
}

static void *mixer_thread(void *arg) {
    (void) arg;
    // Core 1, prioridad alta -- mismo lugar/prioridad que el mezclador de
    // Asphalt-5-Vita (source/audio.cpp): separado del render (Core 2, ver main.c) y del
    // hilo de audio de las cinematicas (video.cpp, tambien Core 2).
    sceKernelChangeThreadPriority(0, 0x40);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);

    static int accL[MIX_GRAIN];
    static int accR[MIX_GRAIN];
    static int16_t outBuf[MIX_GRAIN * 2];

    while (!gQuit) {
        memset(accL, 0, sizeof(accL));
        memset(accR, 0, sizeof(accR));

        pthread_mutex_lock(&gLock);
        int active = (gMusic.smp && !gMusic.paused) ? 1 : 0;
        for (int v = 0; v < MAX_VOICES; v++)
            if (gVoices[v].smp && !gVoices[v].paused) active++;

        mix_voice(&gMusic, accL, accR, MIX_GRAIN);
        for (int v = 0; v < MAX_VOICES; v++)
            mix_voice(&gVoices[v], accL, accR, MIX_GRAIN);
        pthread_mutex_unlock(&gLock);

        // Compensacion de ganancia + soft limiter (mismo esquema que Asphalt-5-Vita):
        // varias voces a full-scale simultaneas exceden int16 facil.
        float comp = 0.85f / sqrtf(active > 1 ? (float) active : 1.0f);
        for (int i = 0; i < MIX_GRAIN; i++) {
            float l = ((float) accL[i] * comp) / 32768.0f;
            float r = ((float) accR[i] * comp) / 32768.0f;
            l = tanhf(l);
            r = tanhf(r);
            outBuf[i * 2] = (int16_t) (l * 32767.0f);
            outBuf[i * 2 + 1] = (int16_t) (r * 32767.0f);
        }

        sceAudioOutOutput(gPort, outBuf);
    }
    return NULL;
}

// Arranque perezoso: recien abre el puerto/lanza el hilo cuando el motor pide su primer
// registerSoundFile/loadMusic/playSound de verdad, no en el boot.
static void gmp_audio_ensure_ready(void) {
    if (gReady || gPortFailed) return;

    gPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, MIX_GRAIN, MIX_RATE,
                                 SCE_AUDIO_OUT_MODE_STEREO);
    if (gPort < 0) {
        l_error("[gmp_audio] sceAudioOutOpenPort(MAIN, %d, %d, stereo) fallo (0x%08x) -- "
                "sin musica/SFX de menu esta partida", MIX_GRAIN, MIX_RATE, (unsigned) gPort);
        gPortFailed = 1;
        return;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 128 * 1024);
    int rc = pthread_create(&gThread, &attr, mixer_thread, NULL);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        l_error("[gmp_audio] no se pudo crear el hilo mezclador (rc=%d)", rc);
        sceAudioOutReleasePort(gPort);
        gPort = -1;
        gPortFailed = 1;
        return;
    }

    gReady = 1;
    l_success("[gmp_audio] listo (sceAudioOut MAIN %d Hz stereo, grain=%d)", MIX_RATE, MIX_GRAIN);
}

// -------------------- JNI BRIDGE --------------------

void GLMediaPlayer_registerSoundFile(jmethodID id, va_list args) {
    (void) id;
    jint index = va_arg(args, jint);
    jstring name = va_arg(args, jstring);
    (void) va_arg(args, jint); // flag: sin uso confirmado todavia

    if (index < 0 || index >= MAX_INDEX) {
        l_warn("[gmp_audio] registerSoundFile: index %d fuera de rango (max %d)",
               (int) index, MAX_INDEX);
        return;
    }
    if (!name) return;

    const char *cname = jni->GetStringUTFChars(&jni, name, NULL);
    if (!cname) return;

    free(gNames[index]);
    gNames[index] = strdup(cname);
    jni->ReleaseStringUTFChars(&jni, name, (char *) cname);
}

void GLMediaPlayer_loadMusic(jmethodID id, va_list args) {
    (void) id;
    jint index = va_arg(args, jint);
    gmp_audio_ensure_ready();
    sfx_get((int) index);
}

void GLMediaPlayer_unloadMusic(jmethodID id, va_list args) {
    (void) id;
    (void) va_arg(args, jint);
}

jint GLMediaPlayer_playMusic(jmethodID id, va_list args) {
    (void) id;
    jint index = va_arg(args, jint);
    jfloat vol = (jfloat) va_arg(args, double); // float variadico: SIEMPRE llega como double
    jint loop = va_arg(args, jint);

    gmp_audio_ensure_ready();
    if (!gReady) return -1;

    SfxSample *s = sfx_get((int) index);
    if (!s) return -1;

    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;

    pthread_mutex_lock(&gLock);
    voice_set_step(&gMusic, s->rate, 1.0f);
    gMusic.pos_int = 0;
    gMusic.pos_frac = 0;
    gMusic.gain = 0.0f;
    gMusic.targetGain = vol * gMasterVolume;
    gMusic.fadeFramesLeft = 512; // ~11ms a 48kHz, evita un click al arrancar
    gMusic.gainStep = gMusic.targetGain / 512.0f;
    gMusic.loop = loop ? 1 : 0;
    gMusic.paused = 0;
    gMusic.index = (int) index;
    gMusic.smp = s;
    pthread_mutex_unlock(&gLock);

    l_info("[gmp_audio] playMusic index=%d vol=%.2f loop=%d", (int) index, (double) vol, (int) loop);
    return 0;
}

void GLMediaPlayer_pauseMusic(jmethodID id, va_list args) {
    (void) id;
    jint index = va_arg(args, jint);
    pthread_mutex_lock(&gLock);
    if (gMusic.smp && gMusic.index == (int) index) gMusic.paused = 1;
    pthread_mutex_unlock(&gLock);
}

void GLMediaPlayer_pauseAllMusic(jmethodID id, va_list args) {
    (void) id; (void) args;
    pthread_mutex_lock(&gLock);
    gMusic.paused = 1;
    pthread_mutex_unlock(&gLock);
}

void GLMediaPlayer_resumeMusic(jmethodID id, va_list args) {
    (void) id;
    jint index = va_arg(args, jint);
    jfloat vol = (jfloat) va_arg(args, double);
    pthread_mutex_lock(&gLock);
    if (gMusic.smp && gMusic.index == (int) index) {
        gMusic.paused = 0;
        if (vol > 0.0f) gMusic.gain = gMusic.targetGain = vol * gMasterVolume;
    }
    pthread_mutex_unlock(&gLock);
}

void GLMediaPlayer_resumeAllMusic(jmethodID id, va_list args) {
    (void) id;
    (void) va_arg(args, double); // float vol global: se ignora, no cambia el canal activo
    pthread_mutex_lock(&gLock);
    gMusic.paused = 0;
    pthread_mutex_unlock(&gLock);
}

void GLMediaPlayer_stopMusic(jmethodID id, va_list args) {
    (void) id;
    jint index = va_arg(args, jint);
    pthread_mutex_lock(&gLock);
    if (gMusic.smp && gMusic.index == (int) index) gMusic.smp = NULL;
    pthread_mutex_unlock(&gLock);
}

void GLMediaPlayer_stopAllMusic(jmethodID id, va_list args) {
    (void) id; (void) args;
    pthread_mutex_lock(&gLock);
    gMusic.smp = NULL;
    pthread_mutex_unlock(&gLock);
}

// Firma real confirmada: "(IF)V" -- index primero, volumen despues.
void GLMediaPlayer_setVolumeOneMusic(jmethodID id, va_list args) {
    (void) id;
    jint index = va_arg(args, jint);
    jfloat vol = (jfloat) va_arg(args, double);
    pthread_mutex_lock(&gLock);
    if (gMusic.smp && gMusic.index == (int) index)
        gMusic.gain = gMusic.targetGain = vol * gMasterVolume;
    pthread_mutex_unlock(&gLock);
}

// Firma real confirmada: "(FI)V" -- volumen primero, index despues (al reves que la de
// arriba -- son dos metodos Java distintos, no un error de tipeo).
void GLMediaPlayer_setVolumeMusic(jmethodID id, va_list args) {
    (void) id;
    jfloat vol = (jfloat) va_arg(args, double);
    (void) va_arg(args, jint); // index: un solo canal de musica, no hace falta distinguir

    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    gMusicVolume = vol;

    pthread_mutex_lock(&gLock);
    if (gMusic.smp) gMusic.gain = gMusic.targetGain = vol * gMasterVolume;
    pthread_mutex_unlock(&gLock);
}

jfloat GLMediaPlayer_getVolumeMusic(jmethodID id, va_list args) {
    (void) id; (void) args;
    return gMusicVolume;
}

jint GLMediaPlayer_isMusicLoaded(jmethodID id, va_list args) {
    (void) id;
    jint index = va_arg(args, jint);
    if (index < 0 || index >= MAX_INDEX) return 0;
    SfxSample *s = gCache[index];
    return (s && s != SFX_FAILED) ? 1 : 0;
}

static Voice *voice_find(int index, int instance) {
    for (int i = 0; i < MAX_VOICES; i++)
        if (gVoices[i].smp && gVoices[i].index == index && gVoices[i].instance == instance)
            return &gVoices[i];
    for (int i = 0; i < MAX_VOICES; i++)
        if (gVoices[i].smp && gVoices[i].index == index)
            return &gVoices[i];
    return NULL;
}

jint GLMediaPlayer_playSound(jmethodID id, va_list args) {
    (void) id;
    jint index = va_arg(args, jint);
    jfloat vol = (jfloat) va_arg(args, double);
    jint flags = va_arg(args, jint);
    jfloat pitch = (jfloat) va_arg(args, double);
    (void) flags; // significado real sin confirmar (¿loop? ¿categoria?) -- un solo tiro por ahora

    gmp_audio_ensure_ready();
    if (!gReady) return -1;

    SfxSample *s = sfx_get((int) index);
    if (!s) return -1;

    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    if (pitch <= 0.0f) pitch = 1.0f;

    int instance = gNextInstance++;

    pthread_mutex_lock(&gLock);
    Voice *v = NULL;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!gVoices[i].smp) { v = &gVoices[i]; break; }
    }
    if (!v) v = &gVoices[0]; // pool lleno: robar la mas vieja, sin heuristica extra

    voice_set_step(v, s->rate, pitch);
    v->pos_int = 0;
    v->pos_frac = 0;
    v->gain = 0.0f;
    v->targetGain = vol * gMasterVolume;
    v->fadeFramesLeft = 64;
    v->gainStep = v->targetGain / 64.0f;
    v->loop = 0;
    v->paused = 0;
    v->index = (int) index;
    v->instance = instance;
    v->smp = s;
    pthread_mutex_unlock(&gLock);

    return instance;
}

void GLMediaPlayer_setPitch(jmethodID id, va_list args) {
    (void) id;
    jint index = va_arg(args, jint);
    jint instance = va_arg(args, jint);
    jfloat pitch = (jfloat) va_arg(args, double);
    pthread_mutex_lock(&gLock);
    Voice *v = voice_find((int) index, (int) instance);
    if (v && v->smp) voice_set_step(v, v->smp->rate, pitch);
    pthread_mutex_unlock(&gLock);
}

void GLMediaPlayer_stopAllSounds(jmethodID id, va_list args) {
    (void) id; (void) args;
    pthread_mutex_lock(&gLock);
    for (int i = 0; i < MAX_VOICES; i++) gVoices[i].smp = NULL;
    pthread_mutex_unlock(&gLock);
}

void GLMediaPlayer_setMasterVolume(jmethodID id, va_list args) {
    (void) id;
    jfloat vol = (jfloat) va_arg(args, double);
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    gMasterVolume = vol;
}

jfloat GLMediaPlayer_getMasterVolume(jmethodID id, va_list args) {
    (void) id; (void) args;
    return gMasterVolume;
}

void GLMediaPlayer_update(jmethodID id, va_list args) {
    (void) id; (void) args;
}

/*
 * Log 065: 17 metodos que GLMediaPlayer_nativeInit resuelve (0x3d0b00) no
 * estaban en la tabla de FalsoJNI -> el motor guardaba jmethodID 0 y cada
 * llamada caia en "method ID 0 not found!" con el default seguro
 * (int=-1, boolean=FALSE, void=no-op). Dos de esos defaults eran gates
 * cerrados que explican el silencio total fuera del motor (vox/AudioTrack):
 * - isRecoveringAudio() poll por frame devolvia -1 (!= 0 = "recuperando") y
 * - isFinishBackground() devolvia FALSE ("fondo sin terminar"),
 * asi que el motor jamas llegaba a loadMusic/playMusic/playSound (en el 065
 * no hay NI UNA linea [gmp_audio]: sfx_get nunca corrio).
 * Firmas confirmadas una por una en el disasm (pares nombre/firma en r2/r3
 * antes de cada `ldr pc,[ip,#0x1c4]`).
 */
void GLMediaPlayer_onRecoverAudio(jmethodID id, va_list args) {
    (void) id; (void) args;
}

void GLMediaPlayer_recoverAudio(jmethodID id, va_list args) {
    (void) id; (void) args;
}

void GLMediaPlayer_reinit(jmethodID id, va_list args) {
    (void) id; (void) args;
}

jint GLMediaPlayer_isRecoveringAudio(jmethodID id, va_list args) {
    (void) id; (void) args;
    return 0; // 0 = audio sano, no hay recovery pendiente
}

void GLMediaPlayer_loadBackground(jmethodID id, va_list args) {
    (void) id; (void) args;
    // no-op: nuestras cargas (sfx_get) son sincronas, no hay fondo pendiente
}

jboolean GLMediaPlayer_isFinishBackground(jmethodID id, va_list args) {
    (void) id; (void) args;
    return JNI_TRUE; // carga sincrona = siempre "terminada", no traba el wait-loop
}

void GLMediaPlayer_loadSoundGroup(jmethodID id, va_list args) {
    (void) id;
    (void) va_arg(args, jint);
    (void) va_arg(args, jint); // boolean llega como int en varargs
}

void GLMediaPlayer_swapPool(jmethodID id, va_list args) {
    (void) id;
    (void) va_arg(args, jint);
}

void GLMediaPlayer_swapAllPools(jmethodID id, va_list args) {
    (void) id; (void) args;
}

void GLMediaPlayer_setSwapTimer(jmethodID id, va_list args) {
    (void) id; (void) args;
}

void GLMediaPlayer_setEnableSwapPool(jmethodID id, va_list args) {
    (void) id; (void) args;
}

jboolean GLMediaPlayer_isEmitterPlaying(jmethodID id, va_list args) {
    (void) id; (void) args;
    return JNI_FALSE; // no trackeamos emisores 3D; el one-shot de base igual suena
}

void GLMediaPlayer_setEmitterVolume(jmethodID id, va_list args) {
    (void) id; (void) args;
}

void GLMediaPlayer_setEmitterPitch(jmethodID id, va_list args) {
    (void) id; (void) args;
}

void GLMediaPlayer_setEmitterParams(jmethodID id, va_list args) {
    (void) id; (void) args;
}

void GLMediaPlayer_stopEmitter(jmethodID id, va_list args) {
    (void) id; (void) args;
}

void GLMediaPlayer_pauseEmitter(jmethodID id, va_list args) {
    (void) id; (void) args;
}

void GLMediaPlayer_resumeEmitter(jmethodID id, va_list args) {
    (void) id; (void) args;
}

void GLMediaPlayer_getEmitter(jmethodID id, va_list args) {
    (void) id; (void) args;
}
