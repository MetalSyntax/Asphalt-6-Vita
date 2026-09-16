/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/glutil.h"

#include "utils/breadcrumb.h"
#include "utils/dialog.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/watchdog.h"

#include <stdarg.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <psp2/gxm.h>

// Helpers for our handling of shaders
GLboolean skip_next_compile = GL_FALSE;
char next_shader_fname[256];
void load_shader(GLuint shader, const char * string, size_t length);

/*
 * Traza por-llamada de las funciones GL interceptadas. Fue la herramienta que permitio
 * ubicar el cuelgue del frame 3 (ver port_progress.md, Bug #013), pero cada linea es un
 * sceIoWrite SIN buffering a la SD: dejarla prendida cuesta un orden de magnitud de
 * rendimiento y ensucia el log real. Se compila fuera por default; para reactivarla,
 * compilar con -DTRACE_GL_CALLS.
 */
#ifdef TRACE_GL_CALLS
#define gl_trace(...) l_error(__VA_ARGS__)
#else
#define gl_trace(...) do {} while (0)
#endif

/*
 * Traza GRUESA, siempre activa. A diferencia de gl_trace() (una linea por draw), esta es una
 * linea por evento raro -- compilar/linkear un shader, tocar un FBO, un latido cada N frames.
 * El costo es despreciable y es lo que permite ubicar un cuelgue en el log que se baja por
 * FTP despues, sin tener que reproducirlo con la traza pesada puesta.
 */
#define gl_info(...) l_error(__VA_ARGS__)

// Cuenta cuantas veces se presento un frame en pantalla. main.c lo usa para saber si el
// motor ya hizo swap por su cuenta durante nativeRender (via el callback JNI
// swapEGLBuffers -> CAndroidOSDevice::flush) y evitar asi presentar dos veces el mismo
// frame. Ver la nota en gl_swap().
unsigned int gl_swap_count = 0;

// Cada cuantos frames presentados se emite el latido del bucle de render.
#define GL_HEARTBEAT_EVERY 60

// Instancia interna del shader patcher de vitaGL (declarada en su shared.h, que no
// incluimos entero para no arrastrar todo su estado). Sus tres buffers son de tamaño FIJO
// (1 MiB cada uno, `shader_patcher_*_size` en lib/vitaGL/source/gxm.c) y no hay API publica
// para agrandarlos: si un juego registra muchos programas grandes, se agotan y
// sceGxmShaderPatcherCreate{Vertex,Fragment}Program empieza a fallar. Por eso se reporta.
extern SceGxmShaderPatcher *gxm_shader_patcher;

void gl_report_mem(const char *tag) {
    gl_info("[gl-mem] %s | vitaGL libres: RAM %u KiB, VRAM %u KiB, PHYCONT %u KiB | "
            "patcher: buffer %u KiB, USSE vert %u KiB, USSE frag %u KiB, host %u KiB",
            tag,
            (unsigned)(vglMemFree(VGL_MEM_RAM) / 1024),
            (unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024),
            (unsigned)(vglMemFree(VGL_MEM_PHYCONT) / 1024),
            (unsigned)(sceGxmShaderPatcherGetBufferMemAllocated(gxm_shader_patcher) / 1024),
            (unsigned)(sceGxmShaderPatcherGetVertexUsseMemAllocated(gxm_shader_patcher) / 1024),
            (unsigned)(sceGxmShaderPatcherGetFragmentUsseMemAllocated(gxm_shader_patcher) / 1024),
            (unsigned)(sceGxmShaderPatcherGetHostMemAllocated(gxm_shader_patcher) / 1024));
}

/*
 * Sumidero de los mensajes de error de vitaGL.
 *
 * vitaGL define `vgl_log` como `sceClibPrintf` (lib/vitaGL/source/utils/debug_utils.h), que
 * en una consola de retail no va a ningun lado que podamos leer. Ese header esta parcheado
 * para declarar esta funcion en su lugar, así los fallos internos de vitaGL (registro de
 * programas en el shader patcher, allocations fallidas, overrun del circular pool, errores
 * del compilador Cg) caen en el MISMO archivo de log que bajamos por FTP.
 */
void vgl_log(const char *fmt, ...) {
    // El compilador Cg emite una linea "I]" por cada nota informativa de cada shader; con
    // ubershaders de 8 KiB eso inunda el log y esconde lo que importa. Los warnings y
    // errores del compilador SI se dejan pasar.
    if (fmt && strstr(fmt, "Shader Compiler: I]"))
        return;

    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // vitaGL termina sus mensajes con \n; el logger ya agrega el suyo.
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';

    if (len > 0)
        l_error("[vitaGL] %s", buf);
}

void gl_preload() {
    if (!file_exists("ur0:/data/libshacccg.suprx")
        && !file_exists("ur0:/data/external/libshacccg.suprx")) {
        fatal_error("Error: libshacccg.suprx is not installed. "
                    "Google \"ShaRKBR33D\" for quick installation.");
    }

#ifdef USE_GLSL_SHADERS
    vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
#endif
}

void gl_init() {
    /*
     * "Escenas por frame" de los render targets de sceGxm. El default de vitaGL es (1, 1) y
     * eso alcanza sólo para motores que dibujan una única escena por frame en cada target.
     *
     * Este motor NO es de esos: el menú principal compone la escena 3D adentro de la UI
     * Flash vía render-to-texture (`T_SWFManager::SWFSet3DRenderTargets`, texturas
     * "MenuRenderTarget"/"MenuFlash_Screen_node03") y además tiene una cadena de
     * post-procesado (los shaders con `blurOffsetX`/`threshold`/`uvScale` del arranque).
     * Cada vez que se cambia de framebuffer, vitaGL cierra la escena actual y abre otra
     * (`scene_reset()` en lib/vitaGL/source/gxm.c), así que un frame del menú abre VARIAS
     * escenas sobre el mismo render target -- el de display incluido, cada vez que se vuelve
     * del FBO.
     *
     * Con scenesPerFrame=1, sceGxmBeginScene se BLOQUEA esperando que la escena anterior de
     * ese render target termine de mostrarse, cosa que no puede pasar hasta que se presente
     * el frame: deadlock, con el último frame presentado congelado en pantalla. 8 es el
     * máximo que admite sceGxm (MAX_SCENES_PER_FRAME).
     *
     * Tiene que llamarse ANTES de vglInit*: el render target del display se crea ahí adentro.
     */
    vglSetupRenderTargetScenesNum(8, 8);

    // 24 MiB de pool interno para vitaGL (paridad con optimizacion de Dungeon Hunter 2):
    // asegura espacio suficiente para compilacion de shaders GLSL en caliente y VBOs dinamicos.
    vglInitExtended(0, 960, 544, 24 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
}

/*
 * Unico punto del port que presenta un frame. Lo llaman DOS caminos distintos:
 *
 *  - el motor mismo, una vez por frame renderizado, via
 *    glitch::CAndroidOSDevice::flush() -> CallStaticVoidMethod(swapEGLBuffers) ->
 *    GameRenderer_swapEGLBuffers() en java.c. Este es el camino normal, y es el UNICO
 *    que existe mientras el motor esta adentro de un bucle de carga (Loading::DisplayFrame),
 *    donde nativeRender() no retorna por varios segundos.
 *
 *  - main.c, como respaldo, solo si el motor NO swapeo durante ese nativeRender (pasa
 *    mientras mbIsEnableSwapBuffer sigue en 0, o sea antes del primer Loading::Start()).
 */
void gl_swap() {
    BC_SCOPE("vglSwapBuffers");
    watchdog_mark("swap", (int)gl_swap_count);
    vglSwapBuffers(GL_FALSE);
    gl_swap_count++;

    // Latido del bucle de render. Es la diferencia entre "colgado" y "vivo pero lentísimo"
    // cuando el log se corta: si después del último evento siguen apareciendo latidos, el
    // hilo principal sigue girando y el problema está en otro lado.
    if ((gl_swap_count % GL_HEARTBEAT_EVERY) == 0)
        gl_info("[gl] latido: %u frames presentados", gl_swap_count);
}

void glShaderSource_soloader(GLuint shader, GLsizei count,
                             const GLchar **string, const GLint *_length) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glShaderSource<%p>(shader: %i, count: %i, string: %p, length: %p)\n", __builtin_return_address(0), shader, count, string, _length);
#endif
    if (!string) {
        l_error("<%p> Shader source string is NULL, count: %i",
                   __builtin_return_address(0), count);
        skip_next_compile = GL_TRUE;
        return;
    } else if (!*string) {
        l_error("<%p> Shader source *string is NULL, count: %i",
                   __builtin_return_address(0), count);
        skip_next_compile = GL_TRUE;
        return;
    }

    size_t total_length = 0;

    for (int i = 0; i < count; ++i) {
        if (!_length) {
            total_length += strlen(string[i]);
        } else {
            total_length += _length[i];
        }
    }

    char * str = malloc(total_length+1);
    size_t l = 0;

    for (int i = 0; i < count; ++i) {
        if (!_length) {
            memcpy(str + l, string[i], strlen(string[i]));
            l += strlen(string[i]);
        } else {
            memcpy(str + l, string[i], _length[i]);
            l += _length[i];
        }
    }
    str[total_length] = '\0';

    gl_trace("[gl] glShaderSource shader=%u len=%u head='%.32s'",
             (unsigned)shader, (unsigned)total_length, str);

    load_shader(shader, str, total_length);

    free(str);
}

void glLinkProgram_soloader(GLuint program) {
    uint32_t t0 = sceKernelGetProcessTimeLow();
    gl_info("[gl] link BEGIN program=%u", (unsigned)program);
    watchdog_mark("link", (int)program);
    bc_enter("glLinkProgram", BC_RA);
    glLinkProgram(program);
    bc_exit("glLinkProgram");
    gl_info("[gl] link END program=%u (%u ms)", (unsigned)program,
            (unsigned)((sceKernelGetProcessTimeLow() - t0) / 1000));

    // Con VGL_MODE_POSTPONED el registro real de los programas en el shader patcher pasa
    // acá adentro, así que este es el punto donde se ve crecer sus buffers fijos.
    {
        char tag[32];
        snprintf(tag, sizeof(tag), "tras link prog=%u", (unsigned)program);
        gl_report_mem(tag);
    }
    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        char info[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(program, (GLsizei)sizeof(info) - 1, &len, info);
        info[(len > 0 ? len : 0)] = '\0';
        l_error("[gl] program=%u LINK FAILED: %s", (unsigned)program, info);
    }
}

void glUseProgram_soloader(GLuint program) {
#ifdef TRACE_GL_CALLS
    BC_SCOPE("glUseProgram");
    gl_trace("[gl] glUseProgram program=%u", (unsigned)program);
#endif
    glUseProgram(program);
}

void glDrawArrays_soloader(GLenum mode, GLint first, GLsizei count) {
#ifdef TRACE_GL_CALLS
    BC_SCOPE("glDrawArrays");
    gl_trace("[gl] glDrawArrays mode=0x%x first=%d count=%d",
             (unsigned)mode, (int)first, (int)count);
#endif
    glDrawArrays(mode, first, count);
}

void glDrawElements_soloader(GLenum mode, GLsizei count, GLenum type, const void *indices) {
#ifdef TRACE_GL_CALLS
    BC_SCOPE("glDrawElements");
    gl_trace("[gl] glDrawElements mode=0x%x count=%d type=0x%x idx=%p",
             (unsigned)mode, (int)count, (unsigned)type, indices);
#endif
    glDrawElements(mode, count, type, indices);
}

void glFinish_soloader(void) {
    BC_SCOPE("glFinish");
    gl_trace("[gl] glFinish BEGIN");
    glFinish();
    gl_trace("[gl] glFinish END");
}

void glFlush_soloader(void) {
    // BEGIN+END (no un solo log previo): el 012 se corta justo en "glFlush" y
    // hay que distinguir "bloqueado DENTRO del flush real (GPU)" de "el flush
    // volvio y el cuelgue esta despues (endScene/registerFrame/swap)".
    BC_SCOPE("glFlush");
    gl_trace("[gl] glFlush BEGIN");
    glFlush();
    gl_trace("[gl] glFlush END");
}

GLint glGetUniformLocation_soloader(GLuint program, const GLchar *name) {
#ifdef TRACE_GL_CALLS
    BC_SCOPE("glGetUniformLocation");
    GLint loc = glGetUniformLocation(program, name ? name : "");
    gl_trace("[gl] glGetUniformLocation prog=%u name='%.64s' -> %d",
             (unsigned)program, name ? name : "(null)", (int)loc);
    return loc;
#else
    return glGetUniformLocation(program, name ? name : "");
#endif
}

GLint glGetAttribLocation_soloader(GLuint program, const GLchar *name) {
#ifdef TRACE_GL_CALLS
    BC_SCOPE("glGetAttribLocation");
    GLint loc = glGetAttribLocation(program, name ? name : "");
    gl_trace("[gl] glGetAttribLocation prog=%u name='%.64s' -> %d",
             (unsigned)program, name ? name : "(null)", (int)loc);
    return loc;
#else
    return glGetAttribLocation(program, name ? name : "");
#endif
}

/*
 * Framebuffers. El menú de este motor compone la escena 3D adentro de la UI Flash vía
 * render-to-texture (`T_SWFManager::SWFRelease3DRenderTargets`/`On3DLoad`), y en vitaGL cada
 * FBO crea su propio render target de sceGxm -- un recurso escaso y un candidato clásico a
 * cuelgue de GPU. Son llamadas raras, así que se registran siempre.
 *
 * NOTA FPS (paridad con Asphalt-5-Vita, que va fluido): estos tres wrappers usaban
 * gl_info() (l_error, un sceIoWrite a la SD por llamada, también en Release). El menú
 * hace RTT cada frame (MenuRenderTarget), así que eran 2-3 escrituras a archivo POR FRAME.
 * Pasan a gl_trace() (solo con -DTRACE_GL_CALLS, compilado fuera por default).
 */
void glBindFramebuffer_soloader(GLenum target, GLuint framebuffer) {
    BC_SCOPE("glBindFramebuffer");
    gl_trace("[gl] glBindFramebuffer fb=%u", (unsigned)framebuffer);
    glBindFramebuffer(target, framebuffer);
}

void glFramebufferTexture2D_soloader(GLenum target, GLenum attachment,
                                     GLenum textarget, GLuint texture, GLint level) {
    BC_SCOPE("glFramebufferTexture2D");
    gl_trace("[gl] glFramebufferTexture2D attach=0x%x tex=%u level=%d",
            (unsigned)attachment, (unsigned)texture, (int)level);
    glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

GLenum glCheckFramebufferStatus_soloader(GLenum target) {
    BC_SCOPE("glCheckFramebufferStatus");
    GLenum st = glCheckFramebufferStatus(target);
    gl_trace("[gl] glCheckFramebufferStatus -> 0x%x%s", (unsigned)st,
            st == GL_FRAMEBUFFER_COMPLETE ? " (COMPLETE)" : " (INCOMPLETO!)");
    return st;
}

void glReadPixels_soloader(GLint x, GLint y, GLsizei width, GLsizei height,
                           GLenum format, GLenum type, void *pixels) {
    BC_SCOPE("glReadPixels");
    gl_trace("[gl] glReadPixels BEGIN %dx%d fmt=0x%x",
             (int)width, (int)height, (unsigned)format);
    glReadPixels(x, y, width, height, format, type, pixels);
    gl_trace("[gl] glReadPixels END");
}

/*
 * Enums que el motor usa y vitaGL rechaza con GL_INVALID_ENUM (ruido [vitaGL] en
 * el log, log 011). Todos benignos con nuestra config, asi que se filtran aca
 * en vez de tocar el vendor:
 *
 * - GL_DITHER (0x0BD0): el juego lo apaga; en GXM el dithering no aplica igual.
 * - GL_SAMPLE_ALPHA_TO_COVERAGE (0x809E) / GL_SAMPLE_COVERAGE (0x80A0): estados
 *   de MSAA; corremos con SCE_GXM_MULTISAMPLE_NONE y el EGL reporta SAMPLES=0,
 *   asi que ignorarlos es lo correcto.
 * - GL_PACK_ALIGNMENT (0x0D05) / GL_UNPACK_ALIGNMENT (0x0CF5): vitaGL solo
 *   acepta GL_UNPACK_ROW_LENGTH en glPixelStorei e ignora el alignment
 *   internamente de todos modos.
 */
static int gl_cap_benigno(GLenum cap) {
    return cap == 0x0BD0 || cap == 0x809E || cap == 0x80A0;
}

void glEnable_soloader(GLenum cap) {
    if (gl_cap_benigno(cap)) return;
    glEnable(cap);
}

void glDisable_soloader(GLenum cap) {
    if (gl_cap_benigno(cap)) return;
    glDisable(cap);
}

void glPixelStorei_soloader(GLenum pname, GLint param) {
    if (pname == 0x0D05 || pname == 0x0CF5) return;
    glPixelStorei(pname, param);
}


/*
 * Envoltorios SOLO para dejar migas. Estas llamadas iban derecho a vitaGL, así que si el
 * cuelgue está adentro de una de ellas el log se corta sin decir cuál: son justo las que
 * pueden esperar a la GPU (subir/liberar una textura en uso, un glClear que abre escena) o
 * pedir memoria de GPU. En el anillo de migas aparecen como una ENTRADA sin su SALIDA.
 */
void glTexImage2D_soloader(GLenum target, GLint level, GLint internalformat,
                           GLsizei width, GLsizei height, GLint border,
                           GLenum format, GLenum type, const void *pixels) {
    BC_SCOPE("glTexImage2D");
    /*
     * Paridad con Asphalt-5-Vita (probado en hardware): glTexImage2D() con un
     * internalformat S3TC/DXT y datos de píxeles SIN comprimir deja el write_cb de
     * vitaGL en NULL, lo que manda el upload por gpu_alloc_compressed_texture() ->
     * dxt_compress() -- un encoder DXT por software, sincrónico, bloque por bloque
     * 4x4, en el hilo que llama. No hay encoder DXT por hardware en el SGX543; el
     * costo es inherente a ese camino. Remapear a GL_RGBA mantiene válidos los datos
     * fuente (el caller ya entrega raw según `format`/`type`) a cambio de unos KB
     * extra de VRAM en esa textura. Los assets PVRTC del juego (vía
     * glCompressedTexImage2D) no pasan por aquí y no se tocan.
     */
    switch (internalformat) {
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_SRGB_S3TC_DXT1:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5:
        case GL_COMPRESSED_SRGB:
        case GL_COMPRESSED_SRGB_ALPHA:
            internalformat = GL_RGBA;
            break;
        default:
            break;
    }
    glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

/*
 * Paridad con Asphalt-5-Vita: glCopyTexImage2D/glCopyTexSubImage2D implican un
 * readback CPU del framebuffer (lento en vitaGL/GXM). El motor los usa para efectos
 * (el menú tiene cadena de post-procesado con blur/threshold); degradan el efecto a
 * una textura dummy / no-op en vez de frenar el frame. Mismo trade-off aceptado en A5.
 */
void glCopyTexImage2D_soloader(GLenum target, GLint level, GLenum internalformat,
                               GLint x, GLint y, GLsizei width, GLsizei height,
                               GLint border) {
    BC_SCOPE("glCopyTexImage2D");
    // Textura dummy 1x1 para satisfacer a la GPU sin el costo del readback.
    glTexImage2D(target, level, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

void glCopyTexSubImage2D_soloader(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                  GLint x, GLint y, GLsizei width, GLsizei height) {
    BC_SCOPE("glCopyTexSubImage2D");
    // No-op a propósito: evita el readback lento.
}

void glTexSubImage2D_soloader(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                              GLsizei width, GLsizei height, GLenum format, GLenum type,
                              const void *pixels) {
    BC_SCOPE("glTexSubImage2D");
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

void glCompressedTexImage2D_soloader(GLenum target, GLint level, GLenum internalformat,
                                     GLsizei width, GLsizei height, GLint border,
                                     GLsizei imageSize, const void *data) {
    BC_SCOPE("glCompressedTexImage2D");
    glCompressedTexImage2D(target, level, internalformat, width, height, border,
                           imageSize, data);
}

void glDeleteTextures_soloader(GLsizei n, const GLuint *textures) {
    BC_SCOPE("glDeleteTextures");
    glDeleteTextures(n, textures);
}

void glGenTextures_soloader(GLsizei n, GLuint *textures) {
    BC_SCOPE("glGenTextures");
    glGenTextures(n, textures);
}

void glBindTexture_soloader(GLenum target, GLuint texture) {
    BC_SCOPE("glBindTexture");
    glBindTexture(target, texture);
}

void glGenerateMipmap_soloader(GLenum target) {
    BC_SCOPE("glGenerateMipmap");
    glGenerateMipmap(target);
}

void glBufferData_soloader(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    BC_SCOPE("glBufferData");
    glBufferData(target, size, data, usage);
}

void glClear_soloader(GLbitfield mask) {
    BC_SCOPE("glClear");
    glClear(mask);
}

void glGenFramebuffers_soloader(GLsizei n, GLuint *framebuffers) {
    BC_SCOPE("glGenFramebuffers");
    glGenFramebuffers(n, framebuffers);
}

void glDeleteFramebuffers_soloader(GLsizei n, const GLuint *framebuffers) {
    BC_SCOPE("glDeleteFramebuffers");
    glDeleteFramebuffers(n, framebuffers);
}

void glRenderbufferStorage_soloader(GLenum target, GLenum internalformat,
                                    GLsizei width, GLsizei height) {
    BC_SCOPE("glRenderbufferStorage");
    glRenderbufferStorage(target, internalformat, width, height);
}

void glDeleteProgram_soloader(GLuint program) {
    BC_SCOPE("glDeleteProgram");
    glDeleteProgram(program);
}

void glDeleteShader_soloader(GLuint shader) {
    BC_SCOPE("glDeleteShader");
    glDeleteShader(shader);
}

void glCompileShader_soloader(GLuint shader) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glCompileShader<%p>(shader: %i)\n", __builtin_return_address(0), shader);
#endif

#ifndef USE_GXP_SHADERS
    if (!skip_next_compile) {
        /*
         * Compilar es lo más caro y lento del arranque: vitaGL traduce el GLSL a Cg y lo
         * compila en caliente con SceShaccCg. Con los ubershaders de este motor (fuentes de
         * ~8 KiB) cada uno puede tardar segundos, así que se registra SIEMPRE el tiempo: es
         * la única forma de distinguir "colgado en el compilador" de "avanzando muy lento".
         */
        uint32_t t0 = sceKernelGetProcessTimeLow();
        gl_info("[gl] compile BEGIN shader=%u", (unsigned)shader);
        watchdog_mark("compile", (int)shader);
        bc_enter("glCompileShader", BC_RA);
        glCompileShader(shader);
        bc_exit("glCompileShader");
        gl_info("[gl] compile END shader=%u (%u ms)", (unsigned)shader,
                (unsigned)((sceKernelGetProcessTimeLow() - t0) / 1000));
        GLint status = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (!status) {
            char info[1024];
            GLsizei len = 0;
            glGetShaderInfoLog(shader, (GLsizei)sizeof(info) - 1, &len, info);
            info[(len > 0 ? len : 0)] = '\0';
            l_error("[gl] shader=%u COMPILE FAILED: %s", (unsigned)shader, info);
        }
#ifdef DUMP_COMPILED_SHADERS
        // vglGetShaderBinary() (vitaGL's custom_shaders.c) ignores the bufSize
        // argument entirely -- it only sign-checks it, then serialize_shader()
        // memcpys the real (unbounded) serialized size into our buffer
        // regardless of how much we allocated. A shader whose compiled size
        // exceeds this buffer silently overflows the heap here, corrupting
        // unrelated allocations that crash much later (confirmed via a real
        // .psp2dmp: PC ended up inside SceLibKernel, called from vitaGL's own
        // unserialize_shader()/sceGxmShaderPatcherRegisterProgram() on a
        // *different* shader). 512 KiB comfortably covers any GXM shader this
        // engine compiles; there's no API to query the exact size up front.
        void *bin = vglMalloc(512 * 1024);
        GLsizei len;
        vglGetShaderBinary(shader, 512 * 1024, &len, bin);
        file_mkpath(next_shader_fname, 0777);
        file_save(next_shader_fname, bin, len);
        vglFree(bin);
#endif
    }
    skip_next_compile = GL_FALSE;
#endif
}

#if defined(USE_GLSL_SHADERS) && defined(DUMP_COMPILED_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char gxp_path[256];
    snprintf(gxp_path, sizeof(gxp_path), DATA_PATH"gxp/%s.gxp", sha_name);

    if (file_exists(gxp_path)) {
        uint8_t *buffer;
        size_t size;

        file_load(gxp_path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
        skip_next_compile = GL_TRUE;
    } else {
        glShaderSource(shader, 1, &string, &length);
        strcpy(next_shader_fname, gxp_path);
    }

    free(sha_name);
}
#elif defined(USE_GLSL_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    glShaderSource(shader, 1, &string, &length);
}
#elif defined(USE_CG_SHADERS) && defined(DUMP_COMPILED_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char gxp_path[256];
    char cg_path[256];
    snprintf(gxp_path, sizeof(gxp_path), DATA_PATH"gxp/%s.gxp", sha_name);
    snprintf(cg_path, sizeof(cg_path), DATA_PATH"cg/%s.cg", sha_name);

    if (file_exists(gxp_path)) {
        uint8_t *buffer;
        size_t size;

        file_load(gxp_path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
        skip_next_compile = GL_TRUE;
    } else if (file_exists(cg_path)) {
        char *buffer;
        size_t size;

        file_load(cg_path, (uint8_t **) &buffer, &size);

        glShaderSource(shader, 1, &string, &size);
        strcpy(next_shader_fname, gxp_path);

        free(buffer);
        skip_next_compile = GL_FALSE;
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", sha_name);

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), DATA_PATH"glsl/%s.glsl", sha_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        if (strstr(string, "gl_FragColor")) {
            const char *dummy_shader = "float4 main() { return float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        } else {
            const char *dummy_shader = "void main(float4 out gl_Position : POSITION ) { gl_Position = float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        }

        skip_next_compile = GL_FALSE;
    }

    free(sha_name);
}
#elif defined(USE_CG_SHADERS) || defined(USE_GXP_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char path[256];
#ifdef USE_CG_SHADERS
    snprintf(path, sizeof(path), DATA_PATH"cg/%s.cg", sha_name);
#else
    snprintf(path, sizeof(path), DATA_PATH"gxp/%s.gxp", sha_name);
#endif

    if (file_exists(path)) {
#ifdef USE_CG_SHADERS
        char *buffer;
        size_t size;

        file_load(path, (uint8_t **) &buffer, &size);

        glShaderSource(shader, 1, &string, &size);

        free(buffer);
#else
        uint8_t *buffer;
        size_t size;

        file_load(path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
#endif
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", sha_name);

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), DATA_PATH"glsl/%s.glsl", sha_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        if (strstr(string, "gl_FragColor")) {
            const char *dummy_shader = "float4 main() { return float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        } else {
            const char *dummy_shader = "void main(float4 out gl_Position : POSITION ) { gl_Position = float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        }
    }

    free(sha_name);
}
#else
#error "Define one of (USE_GLSL_SHADERS, USE_CG_SHADERS, USE_GXP_SHADERS)"
#endif
