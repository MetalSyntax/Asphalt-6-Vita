cmake_minimum_required(VERSION 3.14)

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Please define VITASDK env var to point to your SDK path or set CMAKE_TOOLCHAIN_FILE.")
endif ()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")

project(asphalt6 C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

# Vita application configuration
set(VITA_APP_NAME "Asphalt 6")
set(VITA_TITLEID "ASPHALT06")
set(VITA_VPKNAME "asphalt6")
set(VITA_VERSION "01.00")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

# Optional: used for "send" and "dump" targets
set(PSVITAIP "192.168.3.15" CACHE STRING "PSVita IP (for FTP access)")
set(PARSECOREPATH "$ENV{PARSECOREPATH}" CACHE STRING "vita-parse-core main.py")

# Game-specific definitions
set(DATA_PATH "ux0:data/asphalt6/" CACHE STRING "Path to data (with trailing /)")
set(SO_PATH "${DATA_PATH}lib/armeabi-v7a/libasphalt6.so" CACHE STRING "Path to .so")
add_definitions(-DDATA_PATH="${DATA_PATH}" -DSO_PATH="${SO_PATH}")

option(USE_SCELIBC_IO "Use SceLibcBridge for IO and some other std functions" ON)
if (USE_SCELIBC_IO)
  add_definitions(-DUSE_SCELIBC_IO)
endif()

set(SHADER_FORMAT "GLSL" CACHE STRING "Preferred shader format (one of 'GLSL', 'CG', 'GXP')")
if (${SHADER_FORMAT} STREQUAL "GLSL")
  add_definitions(-DUSE_GLSL_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "CG")
  add_definitions(-DUSE_CG_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "GXP")
  add_definitions(-DUSE_GXP_SHADERS)
else()
  message(FATAL_ERROR "SHADER_FORMAT option can only be one of: 'GLSL', 'CG', 'GXP'")
endif()

set(valid_dump_formats GLSL CG)
# Fix confirmado con so-crash-triage (dumps asphalt6-psp2core-1788286687 y -1788290455): la
# ruta de guardado (vglGetShaderBinary) Y la de recarga (glShaderBinary -> unserialize_shader
# -> sceGxmShaderPatcherRegisterProgram) del cache manual de shaders en source/utils/glutil.c
# terminan crasheando dentro de SceLibKernel (mismo PC exacto en ambos dumps, LR en cero) --
# esto no es el mecanismo HAVE_SHADER_CACHE oficial de vitaGL (que requeriría recompilar
# vitaGL desde fuente, no solo el loader), es un esquema manual via vglGetShaderBinary/
# glShaderBinary que no sobrevive un ciclo guardar+recargar dentro de la misma sesión. Como
# es solo una optimización de arranque (sin cache el juego ya compilaba shaders en caliente
# sin problemas en todas las corridas anteriores), se desactiva por default en vez de seguir
# depurando los internals de vitaGL.
option(DUMP_COMPILED_SHADERS "When using GLSL or CG shader format, cache compiled shaders on disk" OFF)
if (DUMP_COMPILED_SHADERS)
  if (SHADER_FORMAT IN_LIST valid_dump_formats)
    add_definitions(-DDUMP_COMPILED_SHADERS)
  else()
    message(WARNING "DUMP_COMPILED_SHADERS has no effect for the chosen shader format ${SHADER_FORMAT}")
  endif()
endif()

# Traza pesada por-llamada GL (un sceIoWrite por draw): solo para corridas de
# diagnostico, cuesta un orden de magnitud de rendimiento.
#
# Desde el bug #015 ya no hace falta prenderla para ubicar un cuelgue: el anillo de migas
# (source/utils/breadcrumb.c) guarda las ultimas 128 llamadas interceptadas en RAM, sin tocar
# la SD, y el hilo testigo lo vuelca al log cuando detecta que no avanzan los frames. Da la
# misma informacion en el momento del cuelgue, sin el costo por llamada ni el sesgo de timing.
option(TRACE_GL_CALLS "Log every intercepted GL call (diagnostic runs only)" OFF)
if (TRACE_GL_CALLS)
  add_definitions(-DTRACE_GL_CALLS)
endif()

# Unique debug definition for loader's logging to not interfere with anything
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG_SOLOADER)
  # Uncomment this for verbose FalsoJNI logging:
  #add_definitions(-DFALSOJNI_DEBUGLEVEL=0)
endif()

# makes sincos, sincosf, etc. visible
add_definitions(-D_GNU_SOURCE -D__POSIX_VISIBLE=999999)

# reimpl/egl.c a propósito redefine símbolos que también trae libvitaGL.a (para poder
# parchear su comportamiento) -- toolchains de ld recientes (binutils >= 2.36) rechazan
# esa duplicación por default; --allow-multiple-definition hace que gane el .o local.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wl,--allow-multiple-definition -O3 -g -ffast-math -mfloat-abi=softfp -Wno-deprecated")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++20 -Wno-write-strings -Wno-psabi")

add_executable(${CMAKE_PROJECT_NAME}
               source/main.c
               source/dynlib.c
               source/java.c
               source/patch.c
               source/reimpl/bits/_ctype.c
               source/reimpl/egl.c
               source/reimpl/errno.c
               source/reimpl/io.c
               source/reimpl/log.c
               source/reimpl/mem.c
               source/reimpl/pthr.c
               source/reimpl/sys.c
               source/reimpl/time64.c
               source/reimpl/asset_manager.cpp
               source/utils/breadcrumb.c
               source/utils/dialog.c
               source/utils/glutil.c
               source/utils/init.c
               source/utils/logger.c
               source/utils/settings.c
               source/utils/touch.c
               source/utils/utils.c
               source/utils/watchdog.c
               lib/falso_jni/FalsoJNI.c
               lib/falso_jni/FalsoJNI_ImplBridge.c
               lib/falso_jni/FalsoJNI_Logger.c
               lib/falso_jni/converter.c
               lib/sha1/sha1.c
               lib/fios/fios.c
               lib/so_util/so_util.c)

add_subdirectory(lib/libc_bridge)
add_dependencies(${CMAKE_PROJECT_NAME} SceLibcBridge)

set(VITAGL_DIR ${CMAKE_SOURCE_DIR}/lib/vitaGL)
set(VITAGL_LIB ${VITAGL_DIR}/libvitaGL.a)

# Mismo set probado en Asphalt-5-Vita (y asphalt8-vita-main): DRAW_SPEEDHACK=2
# (-DSAFER_DRAW_SPEEDHACK) evita que draws grandes agoten/corrompan el temp
# pool circular de vertices (causa de GPU hangs en A5 Bugs #19/#20/#22);
# HAVE_SHADER_CACHE=1 activa el cache oficial de vitaGL (default
# ux0:data/shader_cache, sin llamar nada desde el loader); sin MATH_SPEEDHACK
# ni HAVE_SHARK_LOG.
# LOG_ERRORS=1 hace que vitaGL reporte sus fallos internos (registro de programas en el
# shader patcher, allocations fallidas, overrun del circular pool, errores del compilador Cg
# via vitaShaRK). Su macro `vgl_log` está parcheada en
# lib/vitaGL/source/utils/debug_utils.h para caer en NUESTRO archivo de log (implementación
# en source/utils/glutil.c) en vez de en sceClibPrintf, que en retail no se puede leer.
set(VITAGL_MAKE_FLAGS "SOFTFP_ABI=1 NO_DEBUG=1 LOG_ERRORS=1 HAVE_SHADER_CACHE=1 NO_SPLASHSCREEN=1 DRAW_SPEEDHACK=2")
separate_arguments(VITAGL_MAKE_ARGS UNIX_COMMAND "${VITAGL_MAKE_FLAGS}")

# Forzar rebuild completo de vitaGL cuando VITAGL_MAKE_FLAGS cambie (la regla
# OUTPUT de abajo sola no reconstruye si la .a ya existe -- sin esto, editar
# los flags no tiene efecto y se linkea la .a vieja sin avisar).
set(VITAGL_FLAGS_STAMP ${CMAKE_BINARY_DIR}/vitagl_flags.stamp)
set(vitagl_old_flags "")
if (EXISTS ${VITAGL_FLAGS_STAMP})
  file(READ ${VITAGL_FLAGS_STAMP} vitagl_old_flags)
endif()
if (NOT vitagl_old_flags STREQUAL VITAGL_MAKE_FLAGS)
  file(GLOB_RECURSE vitagl_stale_objs ${VITAGL_DIR}/source/*.o)
  file(REMOVE ${vitagl_stale_objs} ${VITAGL_LIB})
  file(WRITE ${VITAGL_FLAGS_STAMP} "${VITAGL_MAKE_FLAGS}")
endif()

add_custom_command(
    OUTPUT ${VITAGL_LIB}
    COMMAND ${CMAKE_MAKE_PROGRAM} ${VITAGL_MAKE_ARGS}
    WORKING_DIRECTORY ${VITAGL_DIR}
    COMMENT "Building vendorized vitaGL..."
)

add_custom_target(vitaGL_build DEPENDS ${VITAGL_LIB})
add_dependencies(${CMAKE_PROJECT_NAME} vitaGL_build)

# Helper para forzar rebuild de vitaGL tras cambiar sus flags (ver NOTA arriba).
add_custom_target(vitagl-clean
    COMMAND ${CMAKE_MAKE_PROGRAM} clean
    WORKING_DIRECTORY ${VITAGL_DIR}
    COMMENT "Cleaning vendorized vitaGL..."
)

target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM
                           PUBLIC ${CMAKE_SOURCE_DIR}/lib
                           PUBLIC ${VITAGL_DIR}/source
                           )

target_include_directories(${CMAKE_PROJECT_NAME}
                           PUBLIC ${CMAKE_SOURCE_DIR}/source
                           )

target_link_libraries(${CMAKE_PROJECT_NAME}
                      -Wl,--whole-archive pthread -Wl,--no-whole-archive
                      stdc++
                      ${VITAGL_LIB}
                      vitashark
                      SceShaccCgExt
                      taihen_stub
                      SceShaccCg_stub
                      z
                      m
                      ScePgf_stub
                      mathneon
                      kubridge_stub
                      ${CMAKE_BINARY_DIR}/lib/libc_bridge/stubs/libSceLibcBridge_stub.a
                      SceHid_stub
                      SceTouch_stub
                      SceMotion_stub
                      SceAppMgr_stub
                      SceAppUtil_stub
                      SceAudio_stub
                      SceAudioIn_stub
                      SceCommonDialog_stub
                      SceDisplay_stub
                      SceKernelDmacMgr_stub
                      SceFios2_stub
                      SceCtrl_stub
                      SceGxm_stub
                      ScePower_stub
                      SceTouch_stub
                      SceVshBridge_stub
                      )

vita_create_self(eboot.bin ${CMAKE_PROJECT_NAME} UNSAFE NOASLR)
vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
                VERSION ${VITA_VERSION}
                NAME ${VITA_APP_NAME}
                FILE ${CMAKE_SOURCE_DIR}/extras/cpuinfo cpuinfo
                ${CMAKE_SOURCE_DIR}/extras/meminfo meminfo
                ${CMAKE_SOURCE_DIR}/extras/livearea/icon0.png sce_sys/icon0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/pic0.png sce_sys/pic0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/startup.png sce_sys/livearea/contents/startup.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/bg0.png sce_sys/livearea/contents/bg0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/template.xml sce_sys/livearea/contents/template.xml
                )

add_custom_target(send
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ${VITA_TITLEID} | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(send_kvdb
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ux0:/app/${VITA_TITLEID}/eboot.bin | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(dump
                  COMMAND ${CMAKE_SOURCE_DIR}/extras/scripts/get_dump.sh ${PSVITAIP}
                  COMMAND python3 ${PARSECOREPATH} coredump ${CMAKE_PROJECT_NAME}
                  )

add_custom_target(reboot
                  COMMAND echo reboot | nc ${PSVITAIP} 1338
                  )
