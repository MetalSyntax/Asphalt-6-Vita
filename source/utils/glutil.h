/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  glutil.h
 * @brief OpenGL API initializer, related functions.
 */

#ifndef SOLOADER_GLUTIL_H
#define SOLOADER_GLUTIL_H

#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

void gl_init();

void gl_preload();

void gl_swap();

/**
 * @brief Cantidad de frames presentados desde el arranque (incrementa gl_swap()).
 *
 * El motor presenta sus propios frames desde adentro de nativeRender (bucles de carga),
 * llamando al callback JNI `swapEGLBuffers`. main.c compara este contador antes y despues
 * de cada nativeRender para no presentar el mismo frame dos veces.
 */
extern unsigned int gl_swap_count;

/**
 * @brief Vuelca al log la memoria libre de vitaGL y el uso de los buffers del shader patcher.
 *
 * Los tres buffers del patcher (`buffer`, USSE de vértices y USSE de fragmentos) son de
 * tamaño fijo -- 1 MiB cada uno en `lib/vitaGL/source/gxm.c` -- y no hay API pública para
 * agrandarlos: si se agotan, registrar/parchear programas empieza a fallar en silencio.
 */
void gl_report_mem(const char *tag);

void glCompileShader_soloader(GLuint shader);

void glLinkProgram_soloader(GLuint program);

// Envoltorios de traza (silenciosos salvo que se compile con -DTRACE_GL_CALLS, ver
// glutil.c); los de shader/program ademas reportan siempre los fallos de compile/link.
void glUseProgram_soloader(GLuint program);
void glDrawArrays_soloader(GLenum mode, GLint first, GLsizei count);
void glDrawElements_soloader(GLenum mode, GLsizei count, GLenum type, const void *indices);
void glFinish_soloader(void);
void glFlush_soloader(void);
void glBindFramebuffer_soloader(GLenum target, GLuint framebuffer);
void glFramebufferTexture2D_soloader(GLenum target, GLenum attachment,
                                     GLenum textarget, GLuint texture, GLint level);
GLenum glCheckFramebufferStatus_soloader(GLenum target);
GLint glGetUniformLocation_soloader(GLuint program, const GLchar *name);
GLint glGetAttribLocation_soloader(GLuint program, const GLchar *name);
void glReadPixels_soloader(GLint x, GLint y, GLsizei width, GLsizei height,
                           GLenum format, GLenum type, void *pixels);
// Filtros de enums benignos que vitaGL rechaza con GL_INVALID_ENUM (ver .c).
void glEnable_soloader(GLenum cap);
void glDisable_soloader(GLenum cap);
void glPixelStorei_soloader(GLenum pname, GLint param);

void glShaderSource_soloader(GLuint shader, GLsizei count,
                             const GLchar **string, const GLint *_length);

// Envoltorios que solo dejan migas (utils/breadcrumb.h) alrededor de llamadas de vitaGL que
// pueden esperar a la GPU o pedir memoria: sin ellos, un cuelgue adentro de una de estas no
// deja ningun rastro en el log.
void glTexImage2D_soloader(GLenum target, GLint level, GLint internalformat,
                           GLsizei width, GLsizei height, GLint border,
                           GLenum format, GLenum type, const void *pixels);
void glTexSubImage2D_soloader(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                              GLsizei width, GLsizei height, GLenum format, GLenum type,
                              const void *pixels);
void glCompressedTexImage2D_soloader(GLenum target, GLint level, GLenum internalformat,
                                     GLsizei width, GLsizei height, GLint border,
                                     GLsizei imageSize, const void *data);
void glDeleteTextures_soloader(GLsizei n, const GLuint *textures);
void glGenTextures_soloader(GLsizei n, GLuint *textures);
void glBindTexture_soloader(GLenum target, GLuint texture);
void glGenerateMipmap_soloader(GLenum target);
void glBufferData_soloader(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void glClear_soloader(GLbitfield mask);
void glGenFramebuffers_soloader(GLsizei n, GLuint *framebuffers);
void glDeleteFramebuffers_soloader(GLsizei n, const GLuint *framebuffers);
void glRenderbufferStorage_soloader(GLenum target, GLenum internalformat,
                                    GLsizei width, GLsizei height);
void glDeleteProgram_soloader(GLuint program);
void glDeleteShader_soloader(GLuint shader);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_GLUTIL_H
