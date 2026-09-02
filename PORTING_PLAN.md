# Plan de Port — Asphalt 6 (PS Vita)

> Generado por psvita-port-toolkit el 2026-08-23. Punto de partida con lo detectado automáticamente --
confirmar todo con objdump/Ghidra/jadx a mano antes de asumirlo como cierto.

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
NULL/0) -- si el motor los usa para leer `ux0_data/asphalt6/data/*.dat`, hay que
implementarlos de verdad. Ver `port_progress.md` sesión 2026-08-31.

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
- [ ] Primer arranque en consola real.
- [ ] Gráficos (wrappers GL según versión detectada).
- [ ] Input, Audio, Assets, LiveArea/VPK.
- [ ] Pruebas en hardware real.

## 5. Herramientas

Este port se gestiona con **psvita-port-toolkit** (standalone, fuera de este repo). Desde el
toolkit: `Continuar con un port existente` → elegí esta carpeta (ya tiene `.psvita-toolkit.json`).
