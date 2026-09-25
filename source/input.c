/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "input.h"

#include "utils/logger.h"

#include <psp2/ctrl.h>
#include <psp2/touch.h>

#include <falso_jni/FalsoJNI.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * El motor recibe touch en el espacio declarado en GameRenderer_nativeInit
 * (960x544, ver main.c SCREEN_W/H). notifyTouchPress/Moved/Released
 * (0x3c8a00/0x3c897c/0x3c8a84) NO indexan ningun arreglo por id: arman un
 * SEvent en la pila (tipo en +0, id en +4, X en +8, Y en +12) y lo mandan a
 * glitch::IDevice::postEventFromUser. No hay riesgo de heap-corruption por id
 * grande como en cocos2d-x, pero press/release del mismo "dedo" tienen que
 * llevar el MISMO id chico y estable o el motor los trackea como gestos
 * distintos. Por eso dedos reales y touches sinteticos compiten por los
 * mismos slots 0-7 (nunca un slot "extra" inventado).
 */
#define INPUT_TARGET_W 960
#define INPUT_TARGET_H 544

// El panel frontal reporta en una grilla mucho mas fina que la pantalla
// (1920x1088 en las dos revisiones de consola). Se consulta con
// sceTouchGetPanelInfo() por si acaso, y esto queda solo como respaldo si
// esa llamada falla.
#define TOUCH_PANEL_W_FALLBACK 1920
#define TOUCH_PANEL_H_FALLBACK 1088

#define INPUT_MAX_SLOTS SCE_TOUCH_MAX_REPORT // 8

// Marca de slots ocupados por touches sinteticos de poll_pad(): nunca es un
// id real de SceTouchReport (0-255) ni el -1 de "libre". poll_touch() los
// salta (no debe trackearlos ni liberarlos); solo fake_touch_set/release los
// gestiona. Ver skill psvita-porting/references/input_handling.md.
#define FAKE_VITA_ID (-2)
#define FREE_VITA_ID (-1)

// Botones fisicos mapeados a taps sinteticos (estilo Asphalt-5-Vita).
// Pedido del usuario (2026-09-20, ver HUD en screenshots/eh/2026-09-20/):
// CROSS (X) = nitro unicamente; TRIANGLE/CIRCLE/START/SELECT no hacen nada;
// cruceta y SQUARE se quedan igual. BRAKE_R queda sin cablear (el pedal
// derecho solo existe como widget tactil): la ranura se conserva documentada
// por si se quiere reasignar sin renumerar.
#define FAKE_IDX_LEFT    0 // direccion izquierda
#define FAKE_IDX_RIGHT   1 // direccion derecha
#define FAKE_IDX_BRAKE_L 2 // freno, esquina inferior izquierda (SQUARE)
#define FAKE_IDX_BRAKE_R 3 // (sin cablear: ningun boton fisico)
#define FAKE_IDX_NITRO   4 // nitro flotante (CROSS)
#define FAKE_COUNT 5

/*
 * Keycodes Android que notifyKeyPressed/Released (0x3c8c84/0x3c9234)
 * traducen a GamePadManager::GamePadEvt(down, mascara, 0) -- confirmado
 * desensamblando con capstone (modo ARM) sobre libasphalt6.so:
 *
 *   103 (BUTTON_R1) -> mascara 4 | 108 (BUTTON_START) -> mascara 8
 *     (par opuesto en GamePadEvt + leidos como direccion por
 *     CarControl::UpdateSteeringOnscreenButtons: `ands r6,r3,#4` /
 *     `tst r3,#8`. CUAL es izquierda y cual derecha depende del signo del
 *     angulo -- si van al reves, intercambiar KEY_STEER_L/R, una linea.)
 *   106 (BUTTON_THUMBL) -> mascara 1 (freno segun el modo de control;
 *     SetManualInputFlags la mezcla en TODOS los modos, asi que vale tanto
 *     en botones en pantalla como en inclinacion). La tecla de accion del
 *     nitro/freno derecho (105, BUTTON_R2) ya NO se manda: CROSS solo toca el
 *     widget tactil del nitro y TRIANGLE/CIRCLE/START no mandan nada (pedido
 *     del usuario 2026-09-20).
 *   4 (BACK) y 82 (MENU) ya NO se mandan: ningun boton fisico los dispara.
 *   19-22 (DPAD_UP/DOWN/LEFT/RIGHT) -> IGNORADOS (retornan sin efecto).
 *
 * Por eso la cruceta NO se manda como DPAD sino como estos botones de
 * gamepad (el juego es de la era Xperia Play: no entiende DPAD).
 */
#define KEY_STEER_L 103
#define KEY_STEER_R 108
#define KEY_ACT_A   106
// START = tecla MENU de Android (log 080, pedido del usuario): GS_Race::StateUpdate lee
// isMenuKeyPressed() (bMenuKey, que notifyKeyPressed pone con keycode 82) como
// interruptor: en carrera abre la pausa (PauseToIGM) y con la pausa abierta, si
// menu_main esta visible, llama a ResumeFromIGM.
#define KEY_MENU    82

/*
 * Posiciones de los taps sinteticos en el espacio 960x544. Son el equivalente
 * a los botones tactiles del HUD (cruceta izq/der, frenos en las esquinas
 * inferiores, nitro flotante a la derecha). Derivadas del layout probado en
 * Asphalt-5-Vita (800x480) reescalado + margenes de borde:
 * A5 steer (100,240)/(700,240) -> (120,272)/(840,272); freno (50,430) ->
 * (60,487); nitro (715,380) -> (858,431). Aca se redondean a numeros
 * cerrados y el nitro se separa del freno derecho para que no se solapen.
 *
 * Bug (2026-09-20, screenshots/eh/2026-09-20/2026-09-20-030001.jpg): con
 * (800,350) el tap sintetico de CROSS caia adentro de la zona tactil de
 * "girar a la derecha" (POS_STEER_R esta a solo ~54px) en vez de la del
 * icono de nitro real, asi que CROSS viraba el auto y nunca prendia el
 * nitro -- reproducible incluso con los botones virtuales ocultos, porque
 * la zona de "girar" no depende de que su widget se dibuje. Recalibrado
 * midiendo el centro real del icono (glow cian) en esa captura: bbox
 * x=[843..929] y=[366..430] -> centro (886,398), redondeado a (885,400).
 *
 * CALIBRACION: si un boton fisico "no hace nada" o pega en otro widget, las
 * lineas `[pad] FAKE ...` del log dan la coordenada exacta que se mando --
 * igual que se calibraron en A5 contra taps reales del panel (`[touch]`).
 */
#define POS_STEER_L_X 110
#define POS_STEER_L_Y 330
#define POS_STEER_R_X 850
#define POS_STEER_R_Y 330
#define POS_BRAKE_L_X 80
#define POS_BRAKE_L_Y 470
#define POS_BRAKE_R_X 880
#define POS_BRAKE_R_Y 470
#define POS_NITRO_X 885
#define POS_NITRO_Y 400

// Deadzone del stick analogico (rango ANALOG_WIDE 0-255, centro 128).
// Misma que Asphalt-5-Vita (pad.lx < 64 / > 192).
#define STICK_LOW  64
#define STICK_HIGH 192

typedef struct {
    bool active;
    int vita_id; // id crudo de SceTouchReport, FAKE_VITA_ID o FREE_VITA_ID
    int x, y;
} input_slot;

static input_slot s_slots[INPUT_MAX_SLOTS];
static fn_touch_evt s_pressed, s_moved, s_released;
static fn_key_evt s_key_down, s_key_up;

static int s_panel_x_min, s_panel_x_range;
static int s_panel_y_min, s_panel_y_range;

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void input_init(fn_touch_evt pressed, fn_touch_evt moved, fn_touch_evt released,
                fn_key_evt key_down, fn_key_evt key_up) {
    s_pressed  = pressed;
    s_moved    = moved;
    s_released = released;
    s_key_down = key_down;
    s_key_up   = key_up;
    memset(s_slots, 0, sizeof(s_slots));
    for (int s = 0; s < INPUT_MAX_SLOTS; s++)
        s_slots[s].vita_id = FREE_VITA_ID;

    // lx/ly en 0-255 para la direccion por stick (dialog.c pide el modo Ext
    // para los dialogos; este es el muestreo del juego).
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    SceTouchPanelInfo info;
    if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &info) == 0
        && info.maxDispX > info.minDispX && info.maxDispY > info.minDispY) {
        s_panel_x_min = info.minDispX;
        s_panel_y_min = info.minDispY;
        s_panel_x_range = (info.maxDispX - info.minDispX) + 1;
        s_panel_y_range = (info.maxDispY - info.minDispY) + 1;
        l_info("input: panel disp=[%d..%d, %d..%d] (rango %dx%d)",
               info.minDispX, info.maxDispX, info.minDispY, info.maxDispY,
               s_panel_x_range, s_panel_y_range);
    } else {
        l_error("input: sceTouchGetPanelInfo fallo, usando %dx%d como rango del panel",
                TOUCH_PANEL_W_FALLBACK, TOUCH_PANEL_H_FALLBACK);
        s_panel_x_min = 0;
        s_panel_y_min = 0;
        s_panel_x_range = TOUCH_PANEL_W_FALLBACK;
        s_panel_y_range = TOUCH_PANEL_H_FALLBACK;
    }

    if (!pressed || !moved || !released)
        l_error("input: falta algun nativeTouch*, el tactil no va a funcionar");
    if (!key_down || !key_up)
        l_error("input: falta nativeSetOnKey*, los botones no van a funcionar");
}

static int input_slot_of(int vita_id) {
    for (int i = 0; i < INPUT_MAX_SLOTS; i++)
        if (s_slots[i].active && s_slots[i].vita_id == vita_id)
            return i;
    return -1;
}

static int input_free_slot(void) {
    for (int i = 0; i < INPUT_MAX_SLOTS; i++)
        if (!s_slots[i].active)
            return i;
    return -1;
}

static void poll_touch(void *env, void *clazz) {
    SceTouchData td;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) < 0)
        return;

    int seen[INPUT_MAX_SLOTS] = {0};
    int n = (int)td.reportNum;
    if (n > INPUT_MAX_SLOTS) n = INPUT_MAX_SLOTS;

    for (int i = 0; i < n; i++) {
        int hw_id = td.report[i].id;

        int x = ((int)td.report[i].x - s_panel_x_min) * INPUT_TARGET_W / s_panel_x_range;
        int y = ((int)td.report[i].y - s_panel_y_min) * INPUT_TARGET_H / s_panel_y_range;
        x = clampi(x, 0, INPUT_TARGET_W - 1);
        y = clampi(y, 0, INPUT_TARGET_H - 1);

        // Los slots sinteticos (FAKE_VITA_ID) nunca matchean un dedo real:
        // input_slot_of solo encuentra vita_id == hw_id (0-255).
        int slot = input_slot_of(hw_id);
        if (slot < 0) {
            slot = input_free_slot();
            if (slot < 0)
                continue; // mas dedos que slots: se ignora este
            s_slots[slot].active = true;
            s_slots[slot].vita_id = hw_id;
            s_slots[slot].x = x;
            s_slots[slot].y = y;
            seen[slot] = true;
            l_error("[touch] PRESS slot=%d x=%d y=%d", slot, x, y);
            if (s_pressed) s_pressed(env, clazz, x, y, slot);
            continue;
        }

        seen[slot] = true;
        if (s_slots[slot].x != x || s_slots[slot].y != y) {
            s_slots[slot].x = x;
            s_slots[slot].y = y;
            if (s_moved) s_moved(env, clazz, x, y, slot);
        }
    }

    for (int slot = 0; slot < INPUT_MAX_SLOTS; slot++) {
        // Ojo: no liberar slots sinteticos aca -- esos los suelta
        // fake_touch_release() cuando se levanta el boton fisico.
        if (s_slots[slot].active && !seen[slot]
            && s_slots[slot].vita_id != FAKE_VITA_ID) {
            s_slots[slot].active = false;
            s_slots[slot].vita_id = FREE_VITA_ID;
            l_error("[touch] RELEASE slot=%d x=%d y=%d", slot, s_slots[slot].x, s_slots[slot].y);
            // El release lleva la ultima posicion conocida: el motor la usa
            // para decidir sobre que widget cayo el tap.
            if (s_released)
                s_released(env, clazz, s_slots[slot].x, s_slots[slot].y, slot);
        }
    }
}

static int s_fake_slot[FAKE_COUNT] = { -1, -1, -1, -1, -1 };
static bool s_fake_down[FAKE_COUNT] = { false, false, false, false, false };
static int s_fake_x[FAKE_COUNT] = {
    POS_STEER_L_X, POS_STEER_R_X, POS_BRAKE_L_X, POS_BRAKE_R_X, POS_NITRO_X
};
static int s_fake_y[FAKE_COUNT] = {
    POS_STEER_L_Y, POS_STEER_R_Y, POS_BRAKE_L_Y, POS_BRAKE_R_Y, POS_NITRO_Y
};

static void fake_touch_release(void *env, void *clazz, int idx) {
    if (idx < 0 || idx >= FAKE_COUNT)
        return;
    int slot = s_fake_slot[idx];
    if (slot >= 0 && slot < INPUT_MAX_SLOTS && s_slots[slot].active
        && s_slots[slot].vita_id == FAKE_VITA_ID) {
        l_error("[pad] FAKE RELEASE idx=%d slot=%d x=%d y=%d",
                idx, slot, s_slots[slot].x, s_slots[slot].y);
        if (s_released)
            s_released(env, clazz, s_slots[slot].x, s_slots[slot].y, slot);
        s_slots[slot].active = false;
        s_slots[slot].vita_id = FREE_VITA_ID;
    }
    s_fake_slot[idx] = -1;
    s_fake_down[idx] = false;
}

/*
 * El nitro es un one-shot que el motor consume en el propio evento que cae
 * dentro de su rect (igual que en A5): girando (1 slot) + frenando (2do
 * slot) no quedaba slot libre justo cuando se quiere el nitro a la salida
 * de la curva y el press se perdia en silencio. Expulsa una accion no vital
 * (frenos) para que el nitro siempre tenga slot ya; el boton expulsado
 * reclama otro al proximo frame libre, como si se hubiera soltado un momento.
 */
static void fake_touch_evict_for(void *env, void *clazz, int idx) {
    for (int s = 0; s < INPUT_MAX_SLOTS; s++)
        if (!s_slots[s].active)
            return; // ya hay un slot libre, nada que expulsar
    for (int other = 0; other < FAKE_COUNT; other++) {
        if (other == idx || other == FAKE_IDX_LEFT || other == FAKE_IDX_RIGHT)
            continue;
        if (s_fake_slot[other] >= 0) {
            l_error("[pad] FAKE nitro expulsa idx=%d por un slot libre", other);
            fake_touch_release(env, clazz, other);
            return;
        }
    }
}

static void fake_touch_set(void *env, void *clazz, int idx, bool down) {
    if (idx < 0 || idx >= FAKE_COUNT)
        return;
    int x = s_fake_x[idx], y = s_fake_y[idx];
    if (down == s_fake_down[idx] && (!down || s_fake_slot[idx] >= 0))
        return;
    if (!down) {
        fake_touch_release(env, clazz, idx);
        return;
    }
    if (idx == FAKE_IDX_NITRO && s_fake_slot[idx] < 0)
        fake_touch_evict_for(env, clazz, idx);
    // Press: reclamar un slot real libre, nunca uno ya ocupado por un dedo
    // (input_free_slot solo devuelve inactivos).
    if (s_fake_slot[idx] < 0) {
        int slot = input_free_slot();
        if (slot < 0)
            return; // todo ocupado (dedos + fakes) -- se reintenta el proximo frame
        s_fake_slot[idx] = slot;
        s_slots[slot].active = true;
        s_slots[slot].vita_id = FAKE_VITA_ID;
        s_slots[slot].x = x;
        s_slots[slot].y = y;
    }
    s_fake_down[idx] = true;
    l_error("[pad] FAKE PRESS idx=%d slot=%d x=%d y=%d", idx, s_fake_slot[idx], x, y);
    if (s_pressed)
        s_pressed(env, clazz, x, y, s_fake_slot[idx]);
}

// Flanco de subida/bajada de un keycode Android (sin auto-repeat mientras
// sigue apretado). Devuelve el estado nuevo para guardarlo en el flag.
static bool dispatch_key(void *env, void *clazz, bool is_down, bool was_down, int keycode) {
    if (is_down == was_down)
        return was_down;
    if (is_down) {
        l_error("[pad] KEYDOWN %d", keycode);
        if (s_key_down) s_key_down(env, clazz, keycode);
    } else {
        l_error("[pad] KEYUP %d", keycode);
        if (s_key_up) s_key_up(env, clazz, keycode);
    }
    return is_down;
}

static bool s_key_left_down, s_key_right_down;
static bool s_key_brakel_down;
static bool s_key_menu_down;

static void poll_pad(void *env, void *clazz) {
    SceCtrlData pad;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0)
        return;

    /*
     * Mapeo fisico -> tactil+gamepad (estilo Asphalt-5-Vita, pedido del
     * usuario 2026-09-20 segun el HUD de carrera):
     * - Cruceta IZQ/DER (mas L1/R1 y stick izquierdo) = direccion: tap en la
     *   zona tactil izq/der de la pantalla + key de gamepad (bits 4/8, el par
     *   de direccion de CarControl en todos los modos de control).
     * - SQUARE = freno de la esquina inferior izquierda + key (bit 1).
     * - CROSS (X) = nitro flotante (solo tactil, one-shot con prioridad).
     * - START = tecla MENU (82): abre/cierra el menu de pausa.
     * - TRIANGLE/CIRCLE/SELECT = nada (muertos a proposito).
     */
    bool left_down = (pad.buttons & (SCE_CTRL_LEFT | SCE_CTRL_LTRIGGER)) != 0
        || pad.lx < STICK_LOW;
    bool right_down = (pad.buttons & (SCE_CTRL_RIGHT | SCE_CTRL_RTRIGGER)) != 0
        || pad.lx > STICK_HIGH;
    bool brakel_down = (pad.buttons & SCE_CTRL_SQUARE) != 0;
    bool nitro_down = (pad.buttons & SCE_CTRL_CROSS) != 0;
    bool menu_down = (pad.buttons & SCE_CTRL_START) != 0;

    // Primero el flanco de tecla, despues el tap: al soltar se libera el tap
    // antes de la tecla, en orden espejo.
    s_key_left_down = dispatch_key(env, clazz, left_down, s_key_left_down, KEY_STEER_L);
    fake_touch_set(env, clazz, FAKE_IDX_LEFT, left_down);

    s_key_right_down = dispatch_key(env, clazz, right_down, s_key_right_down, KEY_STEER_R);
    fake_touch_set(env, clazz, FAKE_IDX_RIGHT, right_down);

    s_key_brakel_down = dispatch_key(env, clazz, brakel_down, s_key_brakel_down, KEY_ACT_A);
    fake_touch_set(env, clazz, FAKE_IDX_BRAKE_L, brakel_down);

    fake_touch_set(env, clazz, FAKE_IDX_NITRO, nitro_down);

    s_key_menu_down = dispatch_key(env, clazz, menu_down, s_key_menu_down, KEY_MENU);
}

void input_poll(void *env, void *clazz) {
    poll_touch(env, clazz);
    poll_pad(env, clazz);
}
