/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  watchdog.h
 * @brief Hilo testigo que distingue "colgado" de "vivo pero lentísimo".
 *
 * Cuando el motor se traba adentro de `nativeRender()`, el hilo principal deja de escribir
 * al log y no hay forma de saber, mirando el archivo que se baja por FTP, si el proceso
 * murió, si está bloqueado en sceGxm esperando a la GPU, o si simplemente está tardando
 * muchísimo en algo. Este hilo aparte late cada pocos segundos con el último hito que marcó
 * el hilo principal y hace cuánto: si los latidos siguen apareciendo después del corte, el
 * proceso está vivo y el bloqueo es del hilo principal.
 */

#ifndef SOLOADER_WATCHDOG_H
#define SOLOADER_WATCHDOG_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Arranca el hilo testigo. Llamar una sola vez, después de gl_init(). */
void watchdog_start(void);

/**
 * @brief Suma un hilo a la lista que el testigo inspecciona en cada latido.
 *
 * El testigo consulta `sceKernelGetThreadInfo()` de cada hilo registrado: eso dice si está
 * RUNNING/READY (girando: gasta CPU) o WAITING (bloqueado, y sobre QUÉ objeto del kernel),
 * y cuánta CPU consumió desde el latido anterior. Es lo que separa un deadlock real de un
 * hilo que gira en un bucle cerrado y de uno que quedó muerto.
 *
 * @param uid  UID del hilo. Los hilos que crea el .so llegan como `pthread_t`; si ese valor
 *             no fuera un UID válido, el testigo lo reporta una vez y lo ignora.
 * @param name Etiqueta. DEBE ser un literal (se guarda el puntero, no se copia).
 */
void watchdog_register_thread(int uid, const char *name);

/**
 * @brief Registra el último hito alcanzado por el hilo principal.
 *
 * @param tag   Etiqueta corta. DEBE ser un literal (se guarda el puntero, no se copia).
 * @param value Número asociado al hito (id de shader, de programa, lo que aplique).
 */
void watchdog_mark(const char *tag, int value);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_WATCHDOG_H
