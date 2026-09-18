/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/audiotrack.h"

#include <psp2/audioout.h>

#include "utils/logger.h"

/*
 * Constantes reales de `android.media.AudioTrack`/`AudioFormat` (no vienen de ningun .jar
 * acá -- son los valores que `vox::DriverAndroid::_InitAT` usa hardcodeados, confirmados
 * leyendo la llamada a `getMinBufferSize(0xac44, 0xc, 2)` y al constructor
 * `NewObject(cAudioTrack, mAudioTrack, 3, 0xac44, 0xc, 2, tam, 1)` en
 * `decompiled/libasphalt6_armeabi-v7a/ghidra/out_ghidra.c`, funcion `_InitAT`).
 */
#define AT_CHANNEL_OUT_MONO    4
#define AT_CHANNEL_OUT_STEREO  12   /* 0xc */
#define AT_ENCODING_PCM_16BIT  2

/*
 * Único puerto de audio del juego (musica + SFX, todo mezclado por VoxEngine antes de
 * llegar acá). Distinto y totalmente separado del puerto SCE_AUDIO_OUT_PORT_TYPE_VOICE que
 * usa `video.cpp` para el audio de las cinemáticas (ver CLAUDE.md) -- pueden convivir sin
 * pisarse porque son dos puertos de hardware distintos.
 */
typedef struct {
    int opened;
    int port;
    int channels;      // 1 (mono) o 2 (stereo)
    int grainFrames;   // frames por canal que espera sceAudioOutOutput() en cada llamada
} AudioTrackState;

static AudioTrackState g_at = { .opened = 0, .port = -1, .channels = 2, .grainFrames = 0 };

/*
 * `AudioTrack.getMinBufferSize(sampleRate, channelConfig, audioFormat)` -- estatico, se
 * resuelve una sola vez en `_InitAT` y el resultado (en BYTES, contrato real de la API
 * Android) determina el tamaño de todo lo que sigue. `_InitAT` hace, sobre el valor que
 * devolvemos (R):
 *
 *   framesRaw = R >> 2                    // NO es "R/(channels*bytesPerSample)": es un
 *                                          // shift fijo, hardcodeado, sin importar lo que
 *                                          // se haya pedido en channelConfig/audioFormat
 *   framesCapped = min(framesRaw, 1024)   // clamp propio del motor, fijo en el binario
 *   ctor.bufferSizeInBytes = framesRaw << 2      // == R si framesRaw <= 1024
 *   write().size            = framesCapped << 2  // == R si framesRaw <= 1024 (misma cuenta)
 *
 * **Bug real confirmado en `vox::DriverCallbackInterface::_FillBuffer`** (leído en
 * `decompiled/.../out_ghidra.c:990164`, no es una suposición): a esa función se le pasa
 * `framesCapped` como "cantidad de frames" (confirmado: reserva su acumulador mezclador con
 * `framesCapped << 3` bytes = framesCapped frames stereo de int32, y su loop de conversión a
 * `short` final escribe exactamente `framesCapped * 2` shorts). O sea que **solo la mitad**
 * de los `framesCapped << 2` shorts que `write()` dice tener que mandar fueron realmente
 * escritos esta vuelta -- la segunda mitad del array es memoria sin inicializar de
 * `NewShortArray`, nunca tocada por ningún call (confirmado en consola real, Log 054:
 * sonido horrible/con estatica constante apenas arranca el audio del juego, justo lo que
 * suena reproducir un buffer de basura fija intercalado con audio real todo el tiempo).
 *
 * Por eso el grain real del puerto de audio tiene que ser `bufferSizeInBytes >> 2`
 * (== framesCapped, mientras R <= 4096) y NO `bufferSizeInBytes / channels` -- ver
 * `AudioTrack_ctor`. Elegimos R = 4096: framesRaw = 1024 (no se activa el clamp de 1024),
 * el mismo AUDIO_GRAIN que ya usa `video.cpp` para las cinemáticas (~23ms de bloque).
 */
#define AT_MIN_BUFFER_BYTES 4096

jint AudioTrack_getMinBufferSize(jmethodID id, va_list args) {
    jint sampleRate    = va_arg(args, jint);
    jint channelConfig = va_arg(args, jint);
    jint audioFormat   = va_arg(args, jint);

    if (sampleRate != 44100 || channelConfig != AT_CHANNEL_OUT_STEREO ||
            audioFormat != AT_ENCODING_PCM_16BIT) {
        l_warn("AudioTrack.getMinBufferSize(%d, %d, %d): combinacion no confirmada en este "
               "port (se esperaba 44100/STEREO/PCM16) -- devolviendo el tamano fijo igual",
               sampleRate, channelConfig, audioFormat);
    }

    return AT_MIN_BUFFER_BYTES;
}

jobject AudioTrack_ctor(jmethodID id, va_list args) {
    jint streamType       = va_arg(args, jint);
    jint sampleRate        = va_arg(args, jint);
    jint channelConfig     = va_arg(args, jint);
    jint audioFormat       = va_arg(args, jint);
    jint bufferSizeInBytes = va_arg(args, jint);
    jint mode              = va_arg(args, jint);
    (void) streamType;
    (void) audioFormat;
    (void) mode;

    if (g_at.opened) {
        // El motor puede re-crear el AudioTrack en cada carrera (Init()/Shutdown() por
        // partida) -- no hay que dejar puertos de audio huerfanos entre una y la otra.
        l_warn("AudioTrack: constructor llamado con un puerto ya abierto (0x%x) -- lo cierro "
               "antes de abrir el nuevo", g_at.port);
        sceAudioOutReleasePort(g_at.port);
        g_at.opened = 0;
    }

    int channels = (channelConfig == AT_CHANNEL_OUT_MONO) ? 1 : 2;

    // Ver el comentario de AT_MIN_BUFFER_BYTES: el motor calcula esto con un shift fijo
    // (">>2"), NO dividiendo por `channels` -- reproducirlo tal cual, no "corregirlo" a
    // bufferSizeInBytes/channels (esa version fue la causa confirmada del sonido con
    // estatica: dejaba el grain del puerto al doble de los frames que _FillBuffer llena
    // de verdad, ver Log 054 en port_progress.md).
    int grainFrames = bufferSizeInBytes >> 2;
    if (grainFrames < SCE_AUDIO_MIN_LEN) {
        grainFrames = SCE_AUDIO_MIN_LEN;
    } else if (grainFrames > SCE_AUDIO_MAX_LEN) {
        grainFrames = SCE_AUDIO_MAX_LEN - (SCE_AUDIO_MAX_LEN % 64);
    } else if (grainFrames % 64 != 0) {
        grainFrames -= grainFrames % 64;
        if (grainFrames < SCE_AUDIO_MIN_LEN) grainFrames = SCE_AUDIO_MIN_LEN;
    }

    // Bug #036 (log 058): SCE_AUDIO_OUT_PORT_TYPE_MAIN en este hardware EXIGE 48000 Hz --
    // sceAudioOutOpenPort(MAIN, ..., 44100, ...) devuelve 0x80260008
    // (SCE_AUDIO_OUT_ERROR_INVALID_SAMPLE_FREQ), confirmado por el log ("sceAudioOutOpenPort
    // fallo (0x80260008)"). El comentario de audioout.h ("freq must be set to 48000 Hz" para
    // MAIN) era literal, no una nota generica -- REVERTIDO a BGM (que abre sin error a
    // 44100, ver logs 055-057) hasta poder resamplear a 48000 en software si BGM resulta
    // realmente inaudible (video.cpp ya trae swresample enlazado si hace falta). El reporte
    // de "no suena en el menu" (log 057) puede no ser un problema de tipo de puerto en
    // absoluto: `GLMediaPlayer_nativeInit` resuelve ~40 metodos de audio propios del motor
    // (`playMusic`/`playSound`/etc., ver comentario en java.c) de los que SOLO `loadMovie`
    // esta implementado -- si la musica de menu (a diferencia de la mezcla de carrera, que
    // sale por `vox::DriverAndroid`/este mismo AudioTrack) se dispara por ese camino
    // GLMediaPlayer en vez de por vox::DriverAndroid, el AudioTrack nunca iba a poder
    // arreglarla. Pendiente de confirmar con Ghidra cuál `playMusic`/`playSound` es real acá.
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, grainFrames, sampleRate,
                                    channels == 1 ? SCE_AUDIO_OUT_MODE_MONO
                                                  : SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        l_error("AudioTrack: sceAudioOutOpenPort(BGM, %d, %d, %s) fallo (0x%08x) -- sin "
                "audio esta partida", grainFrames, sampleRate, channels == 1 ? "mono" : "stereo",
                (unsigned) port);
        return NULL;
    }

    g_at.opened      = 1;
    g_at.port        = port;
    g_at.channels    = channels;
    g_at.grainFrames = grainFrames;

    l_info("AudioTrack: puerto BGM 0x%x abierto (%d Hz, %s, %d frames/bloque)",
           port, sampleRate, channels == 1 ? "mono" : "stereo", grainFrames);

    // El "objeto" solo necesita ser un puntero no-NULL estable: FalsoJNI nunca lo pasa de
    // vuelta a nuestros handlers (play/pause/stop/release/write no reciben `this`, ver
    // FalsoJNI_ImplBridge.c), asi que un unico estado global (g_at) alcanza -- el motor
    // real solo crea un AudioTrack a la vez.
    return (jobject) &g_at;
}

// El motor arranca/pausa el puerto pisando su propio flag (mbPaused, offset +0x60 de
// DriverAndroid, confirmado en `UpdateThreadedAT`): mientras esta en pausa simplemente deja
// de llamar a write(), no hace falta que play()/pause() hagan nada del lado del puerto real.
void AudioTrack_play(jmethodID id, va_list args) { (void) args; }
void AudioTrack_pause(jmethodID id, va_list args) { (void) args; }

// stop() se llama justo antes de release() al final de UpdateThreadedAT -- no hay nada que
// hacer de forma separada (sceAudioOutReleasePort ya corta la salida).
void AudioTrack_stop(jmethodID id, va_list args) { (void) args; }

void AudioTrack_release(jmethodID id, va_list args) {
    (void) args;
    if (!g_at.opened) return;

    sceAudioOutReleasePort(g_at.port);
    l_info("AudioTrack: puerto BGM 0x%x cerrado", g_at.port);
    g_at.opened = 0;
    g_at.port = -1;
}

/*
 * `AudioTrack.write(short[] audioData, int offsetInShorts, int sizeInShorts)`. En Android
 * real, `sizeInShorts` le dice a `write()` cuanto leer del array; acá no hace falta creerle
 * ese numero para nada mas que loguear una discrepancia, porque `sceAudioOutOutput()` no
 * recibe un largo -- lee siempre, exactamente, los `grainFrames` frames por canal con los
 * que se abrio el puerto (ver `sceAudioOutOpenPort` en audioout.h). Por construccion (ver
 * AT_MIN_BUFFER_BYTES) el array que nos llega tiene sitio de sobra para eso.
 */
jint AudioTrack_write(jmethodID id, va_list args) {
    jshortArray arr    = va_arg(args, jshortArray);
    jint        offset = va_arg(args, jint);
    jint        size    = va_arg(args, jint);

    if (!g_at.opened || !arr) return 0;

    jshort *data = jni->GetShortArrayElements(&jni, arr, NULL);
    if (!data) return 0;

    // La relacion real (ver AT_MIN_BUFFER_BYTES) es size == grainFrames*4 -- un shift
    // fijo del motor, no size == grainFrames*channels (eso fue justo el Bug #034).
    static int warnedMismatch = 0;
    if (!warnedMismatch && size != g_at.grainFrames * 4) {
        warnedMismatch = 1;
        l_warn("AudioTrack.write(): size=%d no coincide con grainFrames*4=%d -- "
               "se sigue sacando el bloque fijo de %d frames igual",
               size, g_at.grainFrames * 4, g_at.grainFrames);
    }

    sceAudioOutOutput(g_at.port, data + offset);

    return size;
}
