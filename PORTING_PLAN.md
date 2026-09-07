# Plan de Port — Asphalt 6 (PS Vita)

> Generado por psvita-port-toolkit el 2026-08-23. Las secciones 1-2 son la detección
> automática original y **algunas de sus conclusiones ya se demostraron falsas** (ver
> "Correcciones a la detección automática" abajo). Lo confirmado a mano está marcado como tal;
> la bitácora con el detalle de cada bug es `port_progress.md`.

## Estado actual (2026-09-07)

Arranca en consola real, carga el motor, muestra la **pantalla de carga animada**, compila los
66 shaders / 33 programas del menú... y se **cuelga al entrar al menú principal**. El giro
quedó acotado con hooks ENTER al **`Loading::DisplayFrame` final del constructor de
`MenuScene`** (logs 016→018 descartaron re-parenting, batching, animator y luces), con firma
de espera de tiempo (`+9256 gettimeofday` cada 5 s, resto en 0). No es un crash: el proceso
sigue vivo, es el hilo principal el que gira. Bloqueantes: Bug #015 (el giro) y Bug #018
(crash intermitente en `IDevice::run` drenando una cola de eventos que nadie llena en este
port — con guarda ya aplicada en `hook_run`). Ver `port_progress.md`.

Lo que ya funciona: carga del `.so` y relocación, tabla JNI, ciclo de vida
`GLGame`/`GameRenderer`, vitaGL + compilación de shaders en caliente, presentación de frames
vía el callback `swapEGLBuffers`, lectura de assets por `fopen`, e input táctil.
Lo que no: audio (nada implementado) y todo lo que viene después del menú.

## Correcciones a la detección automática

| Lo que dijo el generador | Lo confirmado |
|---|---|
| "sin señal clara por símbolos (posible Unity/libil2cpp)" | **Falso.** Motor "Glitch" de Gameloft (derivado de Irrlicht) + `gameswf` (UI Flash) + `vox` (audio). |
| "GLES: no declarado, usar heurística" | **GLES 2.0.** Todos los shaders del motor empiezan con `#define GLITCH_OPENGLES_2`. |
| El `.so` del juego está en `lib/armeabi-v7a/` | **Falso.** Ahí hay un inyector pirata (`libinject.so`) y un dummy vacío. El motor real sale de `copy.inject`. |

## 0. Contexto

- **Juego:** Asphalt 6
- **Paquete Java:** com.gameloft.android.ANMP.GloftA6HP
- **APK original:** `Asphalt-6-Adrenaline-v1.3.3-offline.apk`
- **TITLEID asignado:** `ASPHALT06`

**¿Motor conocido?** Revisar si algún port hermano (bajo la misma BASE_DIR) comparte motor antes de
reusar su código -- confirmar con símbolos JNI reales, no por analogía superficial.

## 1. Detección automática

- **ABI(s):** armeabi, armeabi-v7a
- **ABI elegida:** armeabi-v7a
- **Nota de arquitectura:** armeabi-v7a presente -> ARMv7 (hard-float/NEON disponible). El CPU de Vita (Cortex-A9) corre esto sin traducción. Hay más de una ABI (armeabi, armeabi-v7a) -- se eligió armeabi-v7a para el análisis.
- **Versión de GLES:** AndroidManifest.xml no declara glEsVersion -- usar heurística (sin señal clara por símbolos (posible Unity/libil2cpp) -- revisar con Ghidra)

## 2. .so encontrados (ABI armeabi-v7a)

- `asphalt6_extract/lib/armeabi-v7a/libinject.so` (21 KB) - **No es el juego**, es un inyector pirata (muzhiwan).
- `asphalt6_extract/lib/armeabi-v7a/libnativedummy.so` (0 KB)
- `copy_inject_extracted/libs/libasphalt6.so` (13.1 MB) - **Este es el motor real del juego**, extraído del archivo oculto `copy.inject` en el APK original.

## 3. Exports JNI (convención `Java_*`)

El archivo `libasphalt6.so` exporta funciones JNI estándar de los motores clásicos de Gameloft. Algunos críticos identificados vía `objdump -T`:
- `Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeInit`
- `Java_com_gameloft_android_ANMP_GloftA6HP_GameRenderer_nativeRender`
- `Java_com_gameloft_android_ANMP_GloftA6HP_GameRenderer_nativeInit`
- `Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeTouchPressed`
- `Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeSetOnKeyDown`
- `Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeAccelerometer`

El juego implementa el ciclo de vida habitual de los juegos de Gameloft (GLGame/GameRenderer/GLMediaPlayer). Podemos basarnos en la arquitectura y bindings JNI usados en ports de N.O.V.A., Dungeon Hunter o Modern Combat.

### Callbacks Java que el motor espera (confirmado, sesión 2026-08-31)

`Java_..._GLGame_nativeInit` resuelve estos nombres vía `GetMethodID` sobre el objeto que
recibe (visto completo en el pseudo-C de Ghidra, antes de que la decompilación se corte --
ver nota sobre `basename@plt` más abajo): `sendAppToBackground`, `setFullyLoaded`,
`IsWifiEnabled`, `IsInternetAvaliable`, `isExternalMusicActive`, `IsFirmwareBefore22`,
`getMac`, `getIdentifier`, `showIAPDialog`, `launchGetGames`, `OpenBrowser`, `getVersion`,
`getHostName`, `OpenGLive`, `NotifyTrophy`, `getWifiIP`, `OpenIGP`, `onLaunchGame1`, más los
estáticos `android/os/Build.MANUFACTURER`/`MODEL` y `Build$VERSION.SDK_INT`. Todos
implementados como stubs en `source/java.c` (la mayoría no-op seguro; `getMac`/
`getIdentifier` devuelven un `jstring` real porque su resultado se usa inmediatamente).

`Java_..._GLResLoader_nativeInit` (decompilada completa, sin el bug de `basename`) resuelve
`getResourceFull`, `getResourceBytes`, `getResourceLength`, `getSoundRaw`,
`getResourceLengthSoundRaw` -- **posible camino de carga de assets vía JNI en vez de
fopen() directo**. Registrados en `java.c` pero sin implementación real todavía (devuelven
NULL/0). **Confirmado en consola (log 011): los assets del arranque NO pasan por acá**, se
leen con `fopen()` directo de `ux0:data/asphalt6/data/*.dat` -- así que estos stubs no son
un bloqueante para llegar al menú.

`Java_..._GameRenderer_nativeInit` resuelve **otros cuatro** que no salían en el pseudo-C
truncado y hubo que sacar del disasm a mano (Bug #013): `getKeyboardText()[B`,
`setKeyboard(ILjava/lang/String;I)V`, `isKeyboardVisible()I` y **`swapEGLBuffers()V`**. El
último es crítico: **es el único camino por el que el motor presenta un frame**, incluido
todo el bucle de carga. Sin él la pantalla queda negra Y el juego se cuelga (la escena de
sceGxm nunca se cierra).

**Camino JNI que sigue sin implementar: el audio.** `vox::DriverAndroid::_InitAT` no usa
ningún callback de Gameloft, va directo contra la clase de Android:
`FindClass("android/media/AudioTrack")` + `GetMethodID` de `<init>`, `getMinBufferSize`,
`play`, `pause`, `stop`, `release`, `write`; después arranca un hilo (`UpdateThreadedAT`)
que llena un `short[]` y lo manda con `write()`. En Android el ritmo del audio lo daba el
bloqueo de `AudioTrack.write()`; con FalsoJNI todo eso devuelve al instante. El `.so` **no
importa OpenSL ni OpenAL** (0 símbolos de audio entre sus 272 indefinidos), así que
implementar sonido significa emular esa clase, no enchufar un backend de audio.

### Bug de decompilación confirmado: Ghidra marca mal `basename@plt` como "no retorna"

En `GLGame_nativeInit`, `GameRenderer_nativeInit` y `GLUtils_Device_nativeInit`, Ghidra
decompila un `bl basename@plt` y lo marca `WARNING: Subroutine does not return`, cortando
el resto de la función. Confirmado con desensamblado ARM real que **es falso**: el código
real hace `basename(ruta) -> __android_log_print(...)` (un simple log de debug con el
nombre del archivo fuente) y sigue ejecutando después. Para `GameRenderer_nativeInit` y
`GLUtils_Device_nativeInit` el log está casi al principio de la función, así que la mayor
parte de su lógica real **no está visible en el pseudo-C** -- confirmar su comportamiento
real con `so-crash-triage` en consola física si el arranque falla ahí. (Nota: el archivo
`decompiled/disasm/full_libasphalt6.so.md` pre-generado está desincronizado porque asume
Thumb y el código real de estas funciones es ARM -- para desensamblar a mano usar
`objdump -d --triple=armv7-none-eabi --start-address=0x... --stop-address=0x...` sobre
`ux0_data/asphalt6/libasphalt6.so`, no ese `.md`.)


## 4. Checklist

- [x] Repo creado desde soloader-boilerplate, git init, .gitignore anti-DMCA.
- [x] APK decompilado (jadx) y .so decompilado(s) (Ghidra) -- ver sección 2/3.
- [x] Análisis del motor real (ciclo de vida nativo, reuso de otro port o boilerplate genérico).
- [x] Bootstrap del loader: so_file_load/so_relocate/so_resolve, primer build.
- [x] Tabla JNI (FalsoJNI): registrar exports + callbacks hacia "Java".
- [x] Primer arranque en consola real.
- [x] Gráficos: vitaGL + GLES2, shaders GLSL compilados en caliente (66 shaders / 33 programas
      del menú compilan y linkean sin errores).
- [x] Presentación de frames (`swapEGLBuffers`, Bug #013) -- la pantalla de carga se ve y anima.
- [x] Assets: `fopen` directo sobre `ux0:data/asphalt6/data/`.
- [x] Input táctil (`source/utils/touch.c`, firma real confirmada en el disasm).
- [x] Instrumentación de diagnóstico (hilo testigo + anillo de migas + logs unificados).
- [ ] **Llegar al menú principal** -- bloqueante actual, Bug #015.
- [ ] Audio (hay que emular `android/media/AudioTrack` por JNI; no hay OpenSL/OpenAL).
- [ ] Input de botones/sticks (hoy solo táctil).
- [ ] Gameplay, LiveArea/VPK final, pruebas largas en hardware.

## 5. Herramientas

Este port se gestiona con **psvita-port-toolkit** (standalone, fuera de este repo). Desde el
toolkit: `Continuar con un port existente` → elegí esta carpeta (ya tiene `.psvita-toolkit.json`).
