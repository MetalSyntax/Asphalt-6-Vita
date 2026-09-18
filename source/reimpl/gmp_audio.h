/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  gmp_audio.h
 * @brief Musica y SFX del motor via `GLMediaPlayer` (loadMusic/playMusic/playSound/...).
 *
 * Sistema de audio TOTALMENTE DISTINTO de `vox::DriverAndroid`/`android/media/AudioTrack`
 * (ver `reimpl/audiotrack.c`). `Java_..._GLMediaPlayer_nativeInit` resuelve ~40 jmethodID
 * propios de Gameloft (`loadMusic`, `playMusic`, `playSound`, `registerSoundFile`, ...)
 * contra los que el motor llama directo -- confirmado en el pseudo-C
 * (`decompiled/.../out_ghidra.c:10033`). Hasta esta sesión solo `loadMovie` estaba
 * implementado, así que la música del menú y la mayoría de los efectos de sonido
 * (motor, UI, choques) nunca sonaban aunque `vox::DriverAndroid` (usado para otra cosa
 * durante la carrera) ya funcionara -- ver CLAUDE.md/port_progress.md.
 *
 * Los assets (`.wav` sueltos como `sfx_menu_start_race.wav`) NO existen como archivos --
 * viven empaquetados en `file00a.bin` (ver `reimpl/soundpack.h`). El motor los referencia
 * por índice entero; `registerSoundFile(index, nombre, flag)` es quien le dice a este
 * módulo qué nombre real corresponde a cada índice, ANTES de que `loadMusic`/`playMusic`/
 * `playSound` lo usen.
 */

#ifndef SOLOADER_GMP_AUDIO_H
#define SOLOADER_GMP_AUDIO_H

#include <falso_jni/FalsoJNI.h>

#ifdef __cplusplus
extern "C" {
#endif

// -- registro de nombres e índices --
void GLMediaPlayer_registerSoundFile(jmethodID id, va_list args);

// -- música (un solo canal, con loop) --
void GLMediaPlayer_loadMusic(jmethodID id, va_list args);
void GLMediaPlayer_unloadMusic(jmethodID id, va_list args);
jint GLMediaPlayer_playMusic(jmethodID id, va_list args);
void GLMediaPlayer_pauseMusic(jmethodID id, va_list args);
void GLMediaPlayer_pauseAllMusic(jmethodID id, va_list args);
void GLMediaPlayer_resumeMusic(jmethodID id, va_list args);
void GLMediaPlayer_resumeAllMusic(jmethodID id, va_list args);
void GLMediaPlayer_stopMusic(jmethodID id, va_list args);
void GLMediaPlayer_stopAllMusic(jmethodID id, va_list args);
void GLMediaPlayer_setVolumeOneMusic(jmethodID id, va_list args);
void GLMediaPlayer_setVolumeMusic(jmethodID id, va_list args);
jfloat GLMediaPlayer_getVolumeMusic(jmethodID id, va_list args);
jint GLMediaPlayer_isMusicLoaded(jmethodID id, va_list args);

// -- efectos de sonido (pool chico de voces) --
jint GLMediaPlayer_playSound(jmethodID id, va_list args);
void GLMediaPlayer_setPitch(jmethodID id, va_list args);
void GLMediaPlayer_stopAllSounds(jmethodID id, va_list args);

// -- volumen global --
void GLMediaPlayer_setMasterVolume(jmethodID id, va_list args);
jfloat GLMediaPlayer_getMasterVolume(jmethodID id, va_list args);

// -- llamado por el motor todos los frames; no hace falta que haga nada --
void GLMediaPlayer_update(jmethodID id, va_list args);

#ifdef __cplusplus
}
#endif

#endif // SOLOADER_GMP_AUDIO_H
