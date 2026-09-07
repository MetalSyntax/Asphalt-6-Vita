/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  touch.h
 * @brief Panel táctil frontal de la Vita -> eventos de puntero del motor.
 */

#ifndef SOLOADER_TOUCH_H
#define SOLOADER_TOUCH_H

#include <falso_jni/FalsoJNI.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Firma de los tres nativos de puntero de GLGame.
 *
 * Confirmado con objdump sobre libasphalt6.so: `nativeTouchPressed/Moved/Released` reciben
 * (JNIEnv*, jclass, jint x, jint y, jint id) y reenvían a notifyTouchPress/Moved/Released,
 * que arman un SEvent de tipo 1 (EET_MOUSE_INPUT_EVENT del Irrlicht del que deriva el motor
 * "glitch") con X en +8 e Y en +12. Son ENTEROS, en coordenadas de DEVICE_SCREEN_*.
 */
typedef void (*touch_native_fn)(void *env, void *clazz, jint x, jint y, jint id);

/**
 * @brief Arranca el muestreo del panel frontal y registra los nativos a los que despachar.
 *
 * Cualquiera de los tres punteros puede ser NULL (el símbolo podría no existir): en ese
 * caso ese tipo de evento simplemente no se emite.
 */
void touch_init(touch_native_fn pressed, touch_native_fn moved,
                touch_native_fn released);

/**
 * @brief Lee el panel y emite los press/move/release del frame. Llamar una vez por frame.
 */
void touch_poll(void);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_TOUCH_H
