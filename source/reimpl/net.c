/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  net.c
 * @brief Fail-fast wrappers for telemetry DNS lookups.
 *
 * Log 060: el TrackingManager (glot) dispara POSTs a ets.gameloft.com desde un
 * hilo propio durante menu y carrera. Sin red (edicion offline, o la Vita sin
 * WiFi) cada intento bloquea el hilo en DNS/connect hasta el timeout, y con
 * reintentos eso son segundos de CPU/hilos ocupados por carga. Practica de
 * Rinnegatamante en ports offline: fallar rapido lo que no tiene backend real.
 * Solo se intercepta el dominio de telemetria de Gameloft; cualquier otro host
 * pasa a la resolucion real sin cambios.
 */

#include "reimpl/net.h"

#include <string.h>
#include <netdb.h>

static int net_is_telemetry(const char *name) {
    return name && strstr(name, "gameloft.com") != NULL;
}

struct hostent *gethostbyname_soloader(const char *name) {
    if (net_is_telemetry(name))
        return NULL; // sin h_errno: newlib de Vita no lo expone; NULL basta
    return gethostbyname(name);
}

int getaddrinfo_soloader(const char *node, const char *service,
                         const struct addrinfo *hints, struct addrinfo **res) {
    if (net_is_telemetry(node))
        return EAI_NONAME;
    return getaddrinfo(node, service, hints, res);
}
