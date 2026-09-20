/*
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  net.h
 * @brief Fail-fast wrappers for telemetry DNS lookups.
 */

#ifndef SOLOADER_NET_H
#define SOLOADER_NET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <netdb.h>
#include <sys/socket.h>

struct hostent *gethostbyname_soloader(const char *name);
int getaddrinfo_soloader(const char *node, const char *service,
                         const struct addrinfo *hints, struct addrinfo **res);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_NET_H
