/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  breadcrumb.h
 * @brief Ring de "migas de pan": las ultimas N llamadas interceptadas, sin tocar la SD.
 *
 * El problema que resuelve: cuando el hilo principal se cuelga, el log por FTP corta en la
 * ultima linea que ALCANZO a escribirse -- y escribir cuesta un sceIoWrite sin buffering por
 * linea, asi que no se puede dejar traza fina prendida en todas las llamadas.
 *
 * Aca cada llamada interceptada solo escribe 5 palabras en un anillo en RAM (sin locks, sin
 * IO). Cuando el hilo testigo (watchdog.c) detecta que no avanzan los frames, VUELCA el
 * anillo al log de una sola vez. Como se registra ENTRADA y SALIDA de cada llamada, una
 * ENTRADA sin su SALIDA marca exactamente en que llamada se quedo trabado -- que es la unica
 * pregunta que el log actual no puede responder.
 */

#ifndef SOLOADER_BREADCRUMB_H
#define SOLOADER_BREADCRUMB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Direccion de retorno del llamador: cae dentro del .so, y con la base se vuelve un offset. */
#define BC_RA __builtin_return_address(0)

/** Evento instantaneo (no puede bloquear): una sola entrada en el anillo. */
void bc_event(const char *name, const void *ra);

/**
 * @brief Igual que bc_event(), pero en un anillo APARTE, para llamadas muy frecuentes.
 *
 * malloc/free se llaman miles de veces por frame: si compartieran anillo con el resto lo
 * vaciarian de contexto util en microsegundos. En el suyo propio siguen sirviendo para lo
 * que importa -- si el motor esta girando en un bucle de construccion de escena, el anillo
 * caliente queda lleno de direcciones de retorno DE ESE BUCLE.
 */
void bc_hot(const char *name, const void *ra);

/** Total de eventos calientes desde el arranque. El delta por latido dice si el hilo
 *  trabado sigue reservando memoria (bucle caliente) o no toca nada (bloqueado). */
unsigned bc_hot_count(void);

/** Entrada a una llamada que PODRIA bloquear. Siempre emparejar con bc_exit(). */
void bc_enter(const char *name, const void *ra);

/** Salida de una llamada marcada con bc_enter(). */
void bc_exit(const char *name);

/**
 * @brief Fija que hilo es el principal (el que corre nativeRender).
 *
 * El anillo comun lo inundan los hilos de trabajo del .so (pthread_cond_timedwait en
 * bucle): en el log 015 las 128 entradas del volcado final eran todas de workers, cero
 * del principal, asi que su ultima llamada quedo expulsada y no se pudo ver. Con el tid
 * principal registrado, bc_push duplica sus eventos en un anillo aparte que los workers
 * no pueden expulsar. Llamar una vez desde main, antes de JNI_OnLoad.
 */
void bc_set_main_tid(int tid);

/**
 * @brief Contador barato de strstr() del .so, sin syscalls.
 *
 * El bucle de re-parenting de MenuScene::MenuScene (el tramo donde se cuelga el log 015:
 * despues del ultimo DisplayFrame, sin reservas de memoria ni llamadas interceptadas)
 * hace un strstr(nombre, "_node") por nodo hijo. Si el hilo principal gira ahi, este
 * contador explota mientras "+N reservas" se queda en 0; si se queda en 0, el giro esta
 * en otro lado (RemoveChildNodeType, batching, animator). El testigo lo imprime como
 * delta por latido ("+N strstr").
 */
void bc_spin_strstr(void);

/** Total de strstr() del .so desde el arranque. */
unsigned bc_spin_strstr_count(void);

/**
 * @brief Contador barato de strcmp()/strncmp()/memcmp() del .so, sin syscalls.
 *
 * El log 016 descarto el bucle de re-parenting (+0 strstr) pero el giro sigue
 * sin mallocs ni llamadas interceptadas: si el hilo principal gira en un parseo
 * (p. ej. constructAnimator sobre XML) este contador explota mientras "+N
 * reservas" se queda en 0. Misma tecnica que bc_spin_strstr.
 */
void bc_spin_strcmp(void);

/** Total de strcmp()/strncmp()/memcmp() del .so desde el arranque. */
unsigned bc_spin_strcmp_count(void);

/**
 * @brief Contador barato de gettimeofday()/clock_gettime() del .so, sin syscalls.
 *
 * Si el giro es una espera activa de tiempo (p. ej. el pacing de 100 ms de
 * Loading::DisplayFrame con un reloj que no avanza), este contador explota
 * mientras todo lo demas se queda en 0.
 */
void bc_spin_time(void);

/** Total de consultas de reloj del .so desde el arranque. */
unsigned bc_spin_time_count(void);

/**
 * @brief Vuelca el anillo entero al log, del mas viejo al mas nuevo.
 * @param why Motivo, para poder ubicar el volcado en el log.
 */
void bc_dump(const char *why);

/** Base y tamaño del .text del .so cargado, para imprimir offsets en vez de punteros. */
void bc_set_base(uintptr_t text_base, uint32_t text_size);

static inline void _bc_scope_end(const char **name) { bc_exit(*name); }

/**
 * Marca ENTRADA al entrar al bloque y SALIDA al salir por CUALQUIER camino (return incluido).
 * Uso: primera linea de la funcion envoltorio.
 */
#define BC_SCOPE(nm) \
    const char *_bc_name __attribute__((cleanup(_bc_scope_end), unused)) = (nm); \
    bc_enter(_bc_name, BC_RA)

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_BREADCRUMB_H
