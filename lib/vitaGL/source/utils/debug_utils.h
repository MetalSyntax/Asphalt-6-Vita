/*
 * This file is part of vitaGL
 * Copyright 2017, 2018, 2019, 2020 Rinnegatamante
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* 
 * gpu_utils.h:
 * Header file for the debug utilities.
 */

#ifndef _DEBUG_UTILS_H_
#define _DEBUG_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

// Debugging tool
char *get_gxm_error_literal(uint32_t code);
#ifdef LOG_ERRORS
/*
 * PARCHE DEL PORT (Asphalt-6-Vita): upstream esto es `#define vgl_log sceClibPrintf`, que en
 * una consola de retail escribe a un puerto de debug que no podemos leer. Acá se declara en
 * su lugar una funcion que implementa el loader (source/utils/glutil.c) reenviando al mismo
 * archivo de log que bajamos por FTP, para que los errores internos de vitaGL (fallos del
 * shader patcher, allocations fallidas, overrun del circular pool, errores del compilador Cg)
 * aparezcan junto al resto de la traza.
 */
void vgl_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#else
#define vgl_log(...)
#endif

#ifdef __cplusplus
}
#endif

#endif
