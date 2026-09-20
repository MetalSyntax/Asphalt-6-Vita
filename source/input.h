/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  input.h
 * @brief Panel tactil frontal + botones fisicos de la Vita -> entradas del motor.
 *
 * `libasphalt6.so` nunca lee input por si solo: en Android, la Activity
 * (`onTouchEvent`/`onKeyDown`/`onKeyUp`, mas el sensor) llama a estos entry
 * points nativos exportados (confirmados con `nm -D` sobre libasphalt6.so):
 *
 *   Java_..._GLGame_nativeTouchPressed/Moved/Released(env,clazz,x,y,id)
 *     enteros en el espacio 960x544 declarado en GameRenderer_nativeInit.
 *   Java_..._GLGame_nativeSetOnKeyDown/Up(env,clazz,keyCode)
 *     `mov r0,r2; b notifyKeyPressed/Released`: el 3er argumento es el keycode
 *     y cae en `GamePadManager::GamePadEvt(down, mascara, 0)` (0x48a46c).
 *   Java_..._GLGame_nativeAccelerometer(env,clazz,x,y,z)
 *     floats que solo se guardan en 3 globales (sin efecto colateral).
 *
 * Como aca no hay framework Android, `input_poll()` lee el panel tactil y el
 * pad una vez por frame y llama a esos nativos igual que `main.c` invoca
 * `nativeInit`/`nativeRender`.
 */
#ifndef SOLOADER_INPUT_H
#define SOLOADER_INPUT_H

#include <falso_jni/FalsoJNI.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (* fn_touch_evt)(void *env, void *clazz, jint x, jint y, jint id);
typedef void (* fn_key_evt)(void *env, void *clazz, jint keycode);

/** Guarda los entry points nativos resueltos. Cualquiera puede ser NULL. */
void input_init(fn_touch_evt pressed, fn_touch_evt moved, fn_touch_evt released,
                fn_key_evt key_down, fn_key_evt key_up);

/** Lee touch + pad y dispara los callbacks de `input_init()`. Una vez por frame. */
void input_poll(void *env, void *clazz);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_INPUT_H
