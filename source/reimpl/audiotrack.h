/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  audiotrack.h
 * @brief Emulacion minima de `android/media/AudioTrack` sobre `sceAudioOut`.
 *
 * `vox::DriverAndroid` (motor "Glitch" de Gameloft, ver CLAUDE.md) no usa OpenSL/OpenAL --
 * habla con `android/media/AudioTrack` por JNI crudo desde `_InitAT`/`UpdateThreadedAT`
 * (confirmado en `decompiled/.../out_ghidra.c`, funcion `vox::DriverAndroid::_InitAT`). Estos
 * handlers son los que FalsoJNI invoca para esos jmethodID -- ver su registro en `java.c` y
 * el detalle de la aritmetica de buffers en el comentario de `AudioTrack_getMinBufferSize`
 * en `audiotrack.c`.
 */

#ifndef SOLOADER_AUDIOTRACK_H
#define SOLOADER_AUDIOTRACK_H

#include <falso_jni/FalsoJNI.h>

#ifdef __cplusplus
extern "C" {
#endif

jint    AudioTrack_getMinBufferSize(jmethodID id, va_list args);
jobject AudioTrack_ctor(jmethodID id, va_list args);
void    AudioTrack_play(jmethodID id, va_list args);
void    AudioTrack_pause(jmethodID id, va_list args);
void    AudioTrack_stop(jmethodID id, va_list args);
void    AudioTrack_release(jmethodID id, va_list args);
jint    AudioTrack_write(jmethodID id, va_list args);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_AUDIOTRACK_H
