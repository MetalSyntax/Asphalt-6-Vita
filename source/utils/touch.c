/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/touch.h"

#include "utils/logger.h"

#include <psp2/touch.h>

#include <falso_jni/FalsoJNI.h>

// El motor recibe las coordenadas en el espacio que le declaramos en
// GameRenderer_nativeInit (DEVICE_SCREEN_WIDTH/HEIGHT), o sea la pantalla de la Vita.
#define TOUCH_TARGET_W 960
#define TOUCH_TARGET_H 544

// El panel frontal reporta en una grilla mucho más fina que la pantalla (1920x1088 en las
// dos revisiones de consola). Igual se consulta con sceTouchGetPanelInfo() por si acaso, y
// esto queda solo como respaldo si esa llamada falla.
#define TOUCH_PANEL_W_FALLBACK 1920
#define TOUCH_PANEL_H_FALLBACK 1088

// El panel frontal reporta hasta 6 dedos simultáneos; SCE_TOUCH_MAX_REPORT es 8.
#define TOUCH_MAX_FINGERS SCE_TOUCH_MAX_REPORT

typedef struct {
    int active;
    int hw_id;  // SceTouchReport.id del dedo que ocupa este slot
    int x;
    int y;
} touch_finger;

static touch_native_fn s_pressed;
static touch_native_fn s_moved;
static touch_native_fn s_released;

static int s_panel_x_min, s_panel_x_range;
static int s_panel_y_min, s_panel_y_range;

/*
 * Estado del frame anterior. Se indexa por SLOT propio, no por SceTouchReport.id: ese id es
 * un contador del hardware que crece con cada toque nuevo (llega a valores >= 8 y envuelve),
 * así que no sirve como índice de un arreglo chico. El slot además es justo lo que el motor
 * espera como "pointer id": un entero chico y estable mientras el dedo no se levanta.
 */
static touch_finger s_fingers[TOUCH_MAX_FINGERS];

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int touch_slot_of(int hw_id) {
    for (int i = 0; i < TOUCH_MAX_FINGERS; i++)
        if (s_fingers[i].active && s_fingers[i].hw_id == hw_id)
            return i;
    return -1;
}

static int touch_free_slot(void) {
    for (int i = 0; i < TOUCH_MAX_FINGERS; i++)
        if (!s_fingers[i].active)
            return i;
    return -1;
}

void touch_init(touch_native_fn pressed, touch_native_fn moved,
                touch_native_fn released) {
    s_pressed = pressed;
    s_moved = moved;
    s_released = released;

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    SceTouchPanelInfo info;
    if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &info) == 0
        && info.maxDispX > info.minDispX && info.maxDispY > info.minDispY) {
        s_panel_x_min = info.minDispX;
        s_panel_y_min = info.minDispY;
        s_panel_x_range = (info.maxDispX - info.minDispX) + 1;
        s_panel_y_range = (info.maxDispY - info.minDispY) + 1;
        l_info("touch_init: panel disp=[%d..%d, %d..%d] (rango %dx%d)",
               info.minDispX, info.maxDispX, info.minDispY, info.maxDispY,
               s_panel_x_range, s_panel_y_range);
    } else {
        l_error("sceTouchGetPanelInfo falló, usando %dx%d como rango del panel",
                TOUCH_PANEL_W_FALLBACK, TOUCH_PANEL_H_FALLBACK);
        s_panel_x_min = 0;
        s_panel_y_min = 0;
        s_panel_x_range = TOUCH_PANEL_W_FALLBACK;
        s_panel_y_range = TOUCH_PANEL_H_FALLBACK;
    }
}

void touch_poll(void) {
    SceTouchData td;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) < 0)
        return;

    int seen[TOUCH_MAX_FINGERS] = {0};
    int n = (int)td.reportNum;
    if (n > TOUCH_MAX_FINGERS) n = TOUCH_MAX_FINGERS;

    for (int i = 0; i < n; i++) {
        int hw_id = td.report[i].id;

        int x = ((int)td.report[i].x - s_panel_x_min) * TOUCH_TARGET_W / s_panel_x_range;
        int y = ((int)td.report[i].y - s_panel_y_min) * TOUCH_TARGET_H / s_panel_y_range;
        x = clampi(x, 0, TOUCH_TARGET_W - 1);
        y = clampi(y, 0, TOUCH_TARGET_H - 1);

        int slot = touch_slot_of(hw_id);
        if (slot < 0) {
            slot = touch_free_slot();
            if (slot < 0)
                continue;  // más dedos que slots: se ignora este
            s_fingers[slot].active = 1;
            s_fingers[slot].hw_id = hw_id;
            s_fingers[slot].x = x;
            s_fingers[slot].y = y;
            seen[slot] = 1;
            l_error("[touch] PRESS slot=%d x=%d y=%d", slot, x, y);
            if (s_pressed) s_pressed(&jni, NULL, x, y, slot);
            continue;
        }

        seen[slot] = 1;
        if (s_fingers[slot].x != x || s_fingers[slot].y != y) {
            s_fingers[slot].x = x;
            s_fingers[slot].y = y;
            if (s_moved) s_moved(&jni, NULL, x, y, slot);
        }
    }

    for (int slot = 0; slot < TOUCH_MAX_FINGERS; slot++) {
        if (s_fingers[slot].active && !seen[slot]) {
            s_fingers[slot].active = 0;
            l_error("[touch] RELEASE slot=%d x=%d y=%d", slot, s_fingers[slot].x, s_fingers[slot].y);
            // El release lleva la última posición conocida: el motor la usa para decidir
            // sobre qué widget cayó el tap.
            if (s_released)
                s_released(&jni, NULL, s_fingers[slot].x, s_fingers[slot].y, slot);
        }
    }
}
