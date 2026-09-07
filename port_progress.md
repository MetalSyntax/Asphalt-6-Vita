# Registro de Progreso — Asphalt 6 (PS Vita)

> **Cómo leer este archivo:** es cronológico, y las entradas viejas se dejan como se
> escribieron aunque después se hayan demostrado equivocadas -- saber qué hipótesis se
> descartó, y por qué, vale tanto como el fix. Lo que está vigente hoy está acá arriba.

## Estado actual — 2026-09-05

**Arranca, se ve la pantalla de carga animada, y se cuelga al entrar al menú principal.**
Determinista: el corte cae en el mismo punto en las 5 corridas con log comparable
(009, 011, 012, 013, 014); la 010 ya lo mostraba igual según el Bug #014. No es
un crash -- el hilo testigo sigue latiendo, no se genera `.psp2dmp`: es el hilo principal el
que se traba. Bloqueante actual: **Bug #015**.

| Área | Estado |
|---|---|
| Carga del `.so`, relocación, resolución de símbolos | funciona (272/272 importados resueltos) |
| Tabla JNI / ciclo de vida `GLGame`+`GameRenderer` | funciona |
| Gráficos (vitaGL, GLES2, GLSL en caliente) | funciona: 66 shaders / 33 programas sin errores |
| Presentación de frames (`swapEGLBuffers`) | funciona (Bug #013) |
| Assets (`fopen` sobre `ux0:data/asphalt6/data/`) | funciona |
| Input táctil | implementado, sin verificar en el menú (no se llega) |
| Audio | **nada implementado** -- hay que emular `android/media/AudioTrack` por JNI |
| Menú principal en adelante | **bloqueado, Bug #015** |

### Correcciones a lo que dicen las Fases 1-2 de más abajo

- El motor **no** es Unity/il2cpp (eso era una heurística del generador). Es el motor
  **"Glitch" de Gameloft** (derivado de Irrlicht) + `gameswf` (UI Flash) + `vox` (audio).
- La versión de GLES **sí** está determinada: **GLES 2.0** (todos los shaders del motor
  empiezan con `#define GLITCH_OPENGLES_2`).

### Índice de bugs

| # | Qué era | Estado |
|---|---|---|
| #001, #002 | NULL deref en `GLGame_nativeInit`: `lockPointer3`/`lockPointer4` sólo se reservan en el flujo de licencia/DRM que no corremos | resueltos |
| #003 | `Unknown symbol "uname"` al resolver imports | resuelto |
| #004 | NULL deref en `CFileSystem::addObfuscationFileMap` (ruta de Android hardcodeada) | resuelto |
| #005, #006 | NULL deref por assets ausentes (`BaseCarManager::GetPackFile`, `autoStartGame`) | resueltos |
| #007, #008, #011 | Mismo patrón en tres sitios de `RenderFX` (asserts "blandos" que siguen con el puntero nulo) | resueltos |
| #009 | Heap corruption: buffer fijo de 32 KB en el volcado de shaders | resuelto |
| #010 | El cache manual de shaders crashea al guardar Y al recargar | desactivado |
| #012 | **Causa raíz** de toda la cadena `smart_ptr`: los assets con nombre sí estaban, ocultos | resuelto |
| #013 | **Causa raíz** de la pantalla negra: el motor presenta por el callback JNI `swapEGLBuffers`, no por EGL | resuelto |
| #014 | Hipótesis: `scenesPerFrame=1` en los render targets de sceGxm | **descartada** en #015 |
| #015 | Cuelgue entrando al menú, acotado a `MenuScene::MenuScene` | **abierto** |

## Fase 1: Configuración y Preparación (Completada — 2026-08-23)
- Repo creado desde soloader-boilerplate, `.gitignore` anti-DMCA.
- APK `Asphalt-6-Adrenaline-v1.3.3-offline.apk` copiado y extraído.
- ABI detectada: armeabi, armeabi-v7a (elegida: armeabi-v7a).
- GLES detectado: AndroidManifest.xml no declara glEsVersion -- usar heurística (sin señal clara por símbolos (posible Unity/libil2cpp) -- revisar con Ghidra)

## Fase 2: Decompilación (Completada — 2026-08-23)
- jadx: corrido, resultados en decompiled/apk_jadx/.
- Ghidra (.so): corrido para cada .so.

## Fase 3: Análisis del Motor Real (Completada — 2026-08-27)
- [x] Confirmar si comparte motor con algún port hermano: Se detectó que el juego tiene un wrapper/inyector de terceros (`libinject.so`). El motor real estaba empaquetado en el archivo `copy.inject` del APK.
- [x] Leer decompiled/apk_jadx/sources/ para el ciclo de vida nativo real: Se usarán las referencias de las funciones JNI estándar (`GLGame`, `GameRenderer`) que encontramos en `libasphalt6.so` (13.1 MB).
- [x] Confirmar exports JNI reales y si hay RegisterNatives: Se confirmaron exportaciones JNI explícitas como `Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeInit`. No hace falta lidiar con un RegisterNatives ofuscado.

## Fase 4: Bootstrap del loader y Primer Build (Completada — 2026-09-05)
- [x] Extraer datos limpios a `ux0_data/asphalt6/` para entorno de pruebas.
- [x] Sincronizar submodulos de git (`FalsoJNI`) y arreglar errores de linkeo (`converter.c`).
- [x] Configurar `CMakeLists.txt` para cargar la ruta real (`ux0:data/asphalt6/lib/armeabi-v7a/libasphalt6.so`).
- [x] Primer build (`asphalt6.vpk`) compilado exitosamente.
- [x] Configurar tabla JNI (`java.c`) con los bindings necesarios para el motor.
- [x] Ejecutar el `.vpk` en hardware real y triagear los crashes de arranque (Bugs #001-#013,
      todos con `.psp2dmp` real -- ver las entradas de abajo).

## Fase 5: Llegar al menú principal (En progreso)
- [x] Que se vea algo en pantalla (Bug #013: `swapEGLBuffers`).
- [x] Instrumentación para diagnosticar cuelgues, no sólo crashes (Bug #015: hilo testigo con
      estado de kernel por hilo, anillo de migas, y los logs de `[vitaGL]`/`[FalsoJNI]`/`[ALOG]`
      unificados en el archivo que se baja por FTP).
- [ ] Bug #015: identificar y arreglar el cuelgue dentro de `MenuScene::MenuScene`.
- [ ] Verificar el input táctil ya en el menú.

### Sesión 2026-08-31: tabla JNI + ciclo de vida GLGame/GameRenderer

**Hallazgo importante -- Ghidra marca mal `basename@plt` como "no retorna":** en varias
funciones (`GLGame_nativeInit`, `GameRenderer_nativeInit`, `GLUtils_Device_nativeInit`)
Ghidra decompila un `bl basename@plt` seguido de un comentario `WARNING: Subroutine does
not return` y corta la función ahí. Confirmado con desensamblado ARM real
(`objdump -d --triple=armv7-none-eabi --start-address=... --stop-address=...` sobre
`ux0_data/asphalt6/libasphalt6.so` -- el `.md` pre-generado en `decompiled/disasm/` está
desincronizado porque asume Thumb y el código real es ARM, así que ahí sale basura) que
`basename()` **sí retorna**: el patrón real es
`basename(ruta_debug) -> __android_log_print(ANDROID_LOG_INFO, tag, fmt, resultado_basename, ...)`,
es decir, un log de debug con el nombre del archivo fuente, y la función sigue después.
Ghidra simplemente tiene mal marcada esa firma en su base de datos de imports y aborta el
análisis ahí -- el pseudo-C de estas funciones está **truncado, no completo**. Para
`GLGame_nativeInit` el log está al final (ya se ve toda la lógica real antes del corte).
Para `GameRenderer_nativeInit` y `GLUtils_Device_nativeInit` el log está casi al principio
(0x238 y más bytes de la función real quedan sin decompilar) -- lo que hacen de verdad no
se pudo confirmar esta sesión; queda para so-crash-triage en consola real.

**FalsoJNI es tolerante a callbacks no registrados** (confirmado leyendo
`lib/falso_jni/FalsoJNI.c` y `FalsoJNI_ImplBridge.c`): `GetMethodID`/`GetStaticMethodID`
devuelven `NULL` si el nombre no está en `nameToMethodId[]` (solo loguean error), y
`methodVoidCall`/`methodBooleanCall`/`methodObjectCall`/etc. con un `jmethodID` no
registrado en las tablas `MethodsX[]` correspondientes también loguean warning y devuelven
un default seguro (`NULL`/`false`/`0`) -- **no crashea**. `GetStringUTFChars(NULL)` también
es seguro. Esto significa que no hace falta registrar cada callback que el motor busca,
solo los que de verdad importan para el arranque.

**`java.c` ampliado** con los callbacks que `GLGame_nativeInit` resuelve vía `GetMethodID`
(confirmados leyendo el pseudo-C real, antes del corte de Ghidra): `sendAppToBackground`,
`setFullyLoaded`, `IsWifiEnabled`, `IsInternetAvaliable`, `isExternalMusicActive`,
`IsFirmwareBefore22`, `getMac`, `getIdentifier`, `showIAPDialog`, `launchGetGames`,
`OpenBrowser`, `getVersion`, `getHostName`, `OpenGLive`, `NotifyTrophy`, `getWifiIP`,
`OpenIGP`, `onLaunchGame1`, más `Build.MANUFACTURER`/`Build.MODEL` (static fields) y
`Build$VERSION.SDK_INT` (ya existía). La mayoría son no-ops seguros (fire-and-forget,
confirmado por `jni_stubs.md`: no hay patrón de "esperar callback de completado" en
ninguno de estos). **Excepción confirmada:** `getMac`/`getIdentifier` SÍ se llaman
inmediatamente dentro de `nativeInit` y su resultado pasa por `GetStringUTFChars()` antes
de `glot::TrackingManager::appSetGlotIdentifiers()` -- por eso son los únicos que devuelven
un `jstring` real (vía `jni->NewStringUTF`, fabricado en `java_init_static_strings()`
porque `jni` no existe todavía en la inicialización estática del array). Booleans de
red (`IsWifiEnabled`/`IsInternetAvaliable`) devuelven `false` a propósito -- el APK
original es la edición offline (`Asphalt-6-Adrenaline-v1.3.3-offline.apk`).

**Hallazgo para la próxima sesión -- posible bloqueante de assets:**
`Java_..._GLResLoader_nativeInit` (ésta SÍ la decompiló Ghidra completa, sin el bug de
`basename`) resuelve 5 callbacks Java: `getResourceFull`, `getResourceBytes`,
`getResourceLength`, `getSoundRaw`, `getResourceLengthSoundRaw`. Si el motor usa este
camino JNI (en vez de `fopen()` directo redirigido por `reimpl/io.c`) para cargar los
`.dat` de `ux0_data/asphalt6/data/`, hace falta implementarlos de verdad devolviendo un
`jbyteArray` con el contenido leído del filesystem -- por ahora están registrados en
`java.c` pero devuelven `NULL`/`0` (ver comentario en el código). Confirmar con
so-crash-triage si el juego se cuelga o falla al cargar el primer asset.

**`main.c`** ahora resuelve por nombre (`so_symbol`) y llama en orden
`GLUtils_Device_nativeInit` → `GLResLoader_nativeInit` → `GLGame_nativeInit` →
`GameRenderer_nativeInit(960,544)` → `GameRenderer_nativeResize(960,544)`, y el loop
llama `GameRenderer_nativeRender()` + `gl_swap()`. Los punteros de
`GLGame_nativeTouchPressed/Moved/Released` se resuelven pero **no** se conectan a
`sceTouch` todavía (no hay scaffold de touch en `source/utils/` -- `SceTouch_stub` y
`SceMotion_stub` ya están linkeados en `CMakeLists.txt`, así que agregarlo después es
directo).

**Fix de build no relacionado, necesario para poder compilar:** el linker fallaba con
"multiple definition" de `eglInitialize` y otras funciones EGL porque `source/reimpl/egl.c`
redefine a propósito símbolos que también trae `libvitaGL.a`, y el `ld` del toolchain
instalado ya no tolera duplicados por default. Se agregó `-Wl,--allow-multiple-definition`
a `CMAKE_C_FLAGS` en `CMakeLists.txt`. Con ese fix, **el build compila y linkea limpio**
(`build/asphalt6.vpk` generado) -- verificado con `psvita-toolkit build` en esta sesión.

**Próximo paso concreto:** desplegar `asphalt6.vpk` a la consola real (`psvita-toolkit
deploy`) y mirar el log/`.psp2dmp` del primer arranque con la skill `so-crash-triage`. Lo
más probable, en orden de sospecha: (1) algo dentro del cuerpo truncado de
`GameRenderer_nativeInit`/`GLUtils_Device_nativeInit` que Ghidra no mostró, (2) un pedido
de asset por el camino `GLResLoader` sin implementar todavía.

## Bugs confirmados (numeración incremental 001–999)

A partir de acá, cada bug confirmado en consola real (o via dump `.psp2dmp`) se numera
secuencialmente, un bug por entrada, siguiendo el flujo `so-crash-triage` de CLAUDE.md.

### Bug #001 — NULL deref en `GLGame_nativeInit` (`lockPointer4`) — 2026-08-31

**Dump:** `logs/asphalt6-psp2core-1788232196-0x0000752183-eboot.bin.psp2dmp`

**Síntoma:** data abort al arrancar, PC en `libasphalt6.so + 0x3cf970` (dentro de
`Java_..._GLGame_nativeInit + 0x44`). El `triage_summary.md`/`analysis.txt` automáticos
mostraban la instrucción causante como `movs r0, #0` (Thumb) -- **es un falso positivo**:
el mismo bug de decodificación que ya se había documentado en la sesión anterior (Ghidra/el
analizador asumen Thumb por defecto, pero el código real de esta zona es ARM). Redisasemblado
en modo ARM forzado (`objdump -d --triple=armv7-none-eabi`) sobre el `.so` real confirmó que
la instrucción real en `0x3cf970` es `str r2, [r3]` con `r3 == 0` (confirmado también por los
registros del dump: `R3 : 0x00000000`).

**Causa raíz confirmada** (cruzando el pseudo-C de Ghidra línea 9399-9411 con el disasm real):
`GLGame_nativeInit` hace `*lockPointer4 = 1;` como su segunda instrucción real, sin chequeo de
NULL. `lockPointer4` es un símbolo global exportado (`nm -D`, `STB_GLOBAL STT_OBJECT` en
`.dynsym`, dirección `0xd12ba8` en `.bss`) que **solo se `malloc()`ea dentro de `nativeStart()`**
(línea ~8727: `if (lockPointer3 != 0) { lockPointer4 = malloc(...); *lockPointer4 = 1; ...}`),
parte del flujo real de licencia/DRM de Android (`installer.GameInstaller` /
`LicenseCheck.startGame`, ver también `lockPointer1/2/3` y
`Java_..._installer_GameInstaller_getPublicKey`). Ese flujo corre en la Activity "Installer"
de Android **antes** de lanzar la Activity del juego real. Nuestro `main.c` resuelve y llama
`GLGame_nativeInit` directo (correcto -- no hace falta reimplementar el LVL real, es la
edición offline), así que `lockPointer4` nunca se allocatea y queda en `0` (bss zero-init) →
`str` a NULL.

**Fix aplicado** (`source/main.c`, antes de llamar `GLGame_nativeInit`): resolver
`lockPointer4` por nombre con `so_symbol` y replicar a mano el único efecto que nos importa
de `nativeStart()` -- que el puntero exista y apunte a un entero en `1` (`malloc(sizeof(int))`
+ `*valor = 1` + asignárselo a `*lockPointer4` via el símbolo resuelto). Build verificado con
`psvita-toolkit build` (compila y linkea limpio, `asphalt6.vpk` regenerado).

**Pendiente para el futuro (no bloqueante hoy):** `lockPointer1`/`lockPointer2`
tienen el mismo patrón de dependencia del flujo de licencia/DRM (`GameInstaller`/
`LicenseCheck`) y podrían crashear más adelante si algún otro camino del motor los toca sin
chequeo de NULL (ver `nativeStart()` línea ~8723, `Java_..._GameInstaller_getPublicKey` línea
~8708). No se tocan todavía porque no hay un crash real confirmado que los involucre --
esperar el próximo `.psp2dmp` real antes de tocarlos (política "un bug a la vez").
`lockPointer3` se confirmó y arregló en el Bug #002 (ver abajo).

### Bug #002 — NULL deref en `GLGame_nativeInit` (`lockPointer3`) — 2026-09-01

**Dump:** `logs/asphalt6-psp2core-1788241162-0x00002927af-eboot.bin.psp2dmp`

**Síntoma:** data abort al arrancar, PC en `libasphalt6.so + 0x3cfdc4` (dentro de
`Java_..._GLGame_nativeInit + 0x498`, más adelante en la misma función que el Bug #001).
El `analysis.txt` automático marcó PC/LR como "FUERA DE RANGO" porque `vita-parse-core`
auto-detectó una base de memoria incorrecta (`0x80d4c000`) para el `.so`; recalculando con
la base real (`0x98000000`, consistente con el rango documentado en la skill
`so-crash-triage` y con la dirección de `lockPointer4` del Bug #001 -- `LR`/`R7` del dump
caen a 4 bytes de `0xd12ba8`), el offset de PC (`0x3cfdc4`) cae dentro del tamaño real del
`.so` y resuelve limpio contra `Java_..._GLGame_nativeInit`.

**Causa raíz confirmada** (cruzando pseudo-C de Ghidra línea 9458 con disasm ARM real):
`GLGame_nativeInit` hace `*lockPointer3 = *lockPointer3 + 1;` tres veces (líneas 9458, 9461,
9472 del pseudo-C) sin chequeo de NULL -- justo después de resolver
`Build$VERSION.SDK_INT` (`im_sdkVersion`, que coincide con el símbolo justo antes de
`lockPointer3` en `.bss`, confirmando la base recalculada). Disasm ARM real en
`libasphalt6.so+0x3cfdc4` (`arm-vita-eabi-objdump -d` sin `-M force-thumb`, esta zona de la
función es ARM, no Thumb -- mismo patrón ya documentado para el área de `basename@plt`):
`ldr lr, [ip]` con `R12 (ip) == 0x00000000` en los registros del dump, exactamente
`*lockPointer3` con `lockPointer3 == NULL`. Igual que `lockPointer4` en el Bug #001,
`lockPointer3` solo se `malloc()`ea dentro del flujo de licencia/DRM (`nativeStart()`,
pseudo-C línea ~9135: `lockPointer3 = malloc(4); *lockPointer3 = 1;`) que no corremos
(edición offline, no hace falta reimplementar el LVL real).

**Fix aplicado** (`source/main.c`, antes de llamar `GLGame_nativeInit`, junto al fix de
`lockPointer4`): resolver `lockPointer3` por nombre con `so_symbol` y replicar el mismo
único efecto que nos importa de `nativeStart()` -- que el puntero exista y apunte a un
entero en `1`. Build verificado con `psvita-toolkit build` (compila y linkea limpio,
`asphalt6.vpk` regenerado).

**Pendiente para el futuro:** `lockPointer1`/`lockPointer2` siguen sin confirmar -- mismo
patrón, esperar el próximo `.psp2dmp` real antes de tocarlos.

### Bug #003 — `Unknown symbol "uname"` al resolver imports del `.so` — 2026-09-01

**Síntoma:** no fue un crash/dump sino el mensaje fatal del propio dynamic linker de
`so_util` (`fatal_error("Unknown symbol \"%s\" (%p).")`, `lib/so_util/so_util.c:411`) al
resolver las relocations del `.so` -- `libasphalt6.so` importa `uname()` (probablemente para
detectar el kernel/dispositivo Android) y esa función no estaba en la tabla de imports
resueltos por el loader (`source/dynlib.c`, `default_dynlib[]`).

**Causa raíz:** `dynlib.c` solo expone al `.so` las funciones libc explícitamente listadas en
`default_dynlib[]` (no toda la libc real) -- `uname` nunca se había necesitado hasta ahora.
Confirmado que vitasdk **sí** trae una implementación real de `uname()` en `libc.a`
(`nm libc.a | grep uname` -> `T uname`), con el prototipo/`struct utsname` declarado en
`sys/utsname.h`.

**Fix aplicado** (`source/dynlib.c`): agregado `#include <sys/utsname.h>` y la entrada
`{ "uname", (uintptr_t)&uname }` en la sección "Syscalls" de `default_dynlib[]`, apuntando
directo a la implementación real de vitasdk (no un stub -- no hace falta fingir valores).
Build verificado con `psvita-toolkit build` (compila y linkea limpio, `asphalt6.vpk`
regenerado).

### Bug #004 — NULL deref en `CFileSystem::addObfuscationFileMap` (ruta Android hardcodeada) — 2026-09-01

**Dump:** `logs/asphalt6-psp2core-1788273208-0x0001a02679-eboot.bin.psp2dmp` -- sin log de
la corrida (no se capturó `logs-live`/dashboard en simultáneo, así que no hay línea
`fopen()`/`open()` de `source/reimpl/io.c` para esta corrida).

**Confirmado con el dump solo** (base de memoria recalculada a `0x98000000` -- la
auto-detectada `0x80d4c000` vuelve a dar "fuera de rango", mismo patrón que Bug #002):
`glitch::io::CFileSystem::addObfuscationFileMap(char const*, unsigned char, std::string
const&) + 0x24` hace `ldr r3, [r0]` con `R0 == 0x00000000`. `r0` es el valor de retorno de
`this->createAndOpenFile(filename)` (resuelto por vtable slot 9 / offset 0x24 de
`_ZTVN6glitch2io11CFileSystemE`, dirección coincide exacta con
`_ZN6glitch2io11CFileSystem17createAndOpenFileEPKc`), usado sin chequeo de `NULL` (confirmado
también en el pseudo-C de Ghidra, línea ~772272-772273 -- no hay ninguna comprobación entre
la llamada y el primer uso). `createAndOpenFile` prueba abrir el archivo dentro de los ZIP
montados y como archivo real (`createReadFile`); si ninguna vía funciona, devuelve `NULL` --
o sea, el motor pidió un archivo que no está (o no está donde lo espera) en
`ux0_data/asphalt6/`.

**Confirmado con el `netlog` UDP** (ver infra abajo) -- `logs/live_session_20260901_115057.log`
muestra, como últimas líneas antes del crash:
```
[WARNING] fopen(/sdcard/gameloft/games/GloftA6HP/file000000.dat, rb): 0x0
[WARNING] fopen(/sdcard/gameloft/games/GloftA6HP/file000000.dat, rb): 0x0
```
Mismo `PC`/`LR` exactos (`0x988b6ed0`/`0x98419fa0`) que el dump anterior -- confirmado que es
el mismo crash. El motor **no** usa el camino JNI/`GLResLoader` para los `fileNNNNNN.dat` --
los abre directo por `fopen()` con una ruta absoluta de Android hardcodeada
(`/sdcard/gameloft/games/GloftA6HP/...`), que no existe en la Vita. El archivo en sí **sí**
está disponible -- ya estaba en `ux0_data/asphalt6/data/file000000.dat` desde la extracción de
datos de la Fase 4 -- solo faltaba redirigir el prefijo de la ruta, igual que ya se hacía para
`/proc/cpuinfo`/`/proc/meminfo`.

**Fix aplicado** (`source/reimpl/io.c`): nueva función `remap_gameloft_sdcard_path()` que
reescribe cualquier ruta con prefijo `/sdcard/gameloft/games/GloftA6HP/` a
`DATA_PATH"data/" + resto` (`ux0:data/asphalt6/data/...`), aplicada en `fopen_soloader`,
`open_soloader` y `stat_soloader` (las tres funciones de `io.c` que reciben una ruta cruda sin
traducir). Build verificado con `psvita-toolkit build` (compila y linkea limpio,
`asphalt6.vpk` regenerado). Pendiente: confirmar en consola real que el juego avanza más allá
de este punto -- si otro `fileNNNNNN.dat`/ruta Android distinta aparece sin traducir, el mismo
patrón (`remap_gameloft_sdcard_path`) ya está listo para extenderse.

### Infra — sink UDP `netlog` (`debugnet`-compatible) — 2026-09-01

**Motivo:** `psvita-toolkit logs-live`/TUI/web no capturaban nada porque el logger del
loader (`source/utils/logger.c`) solo escribía por `sceClibPrintf` (salida de debug del
kernel, no de red) -- nunca había ningún envío UDP.

**Agregado:** `source/utils/netlog.{h,c}` (portado del sink `debugnet`-compatible ya probado
en el port hermano `Asphalt-5-Vita`, mismo motor/familia Gameloft) -- socket UDP no
bloqueante (`SCE_NET_MSG_DONTWAIT`, nunca frena el hilo del juego), lee
`ux0:data/asphalt6/netlog.txt` (formato `ip <addr>` / `port <n>`, puerto por defecto **9999**
para calzar con el default de `psvita-toolkit`'s `logs-live`/TUI/web) y cae en silencio si no
hay red o el archivo no existe. `source/utils/logger.c` ahora llama a `netlog_init()`
perezosamente y manda una copia sin colores ANSI de cada línea logueada
(`_log_print`) por ese socket. `CMakeLists.txt` actualizado: nueva fuente `netlog.c` +
libs `SceNet_stub`/`SceNetCtl_stub`. Build verificado con `psvita-toolkit build` (compila y
linkea limpio).

**Pendiente para poder usarlo:** el deploy normal (`psvita-toolkit deploy`) solo sube
`eboot.bin`/`.vpk`, no la carpeta de datos -- hay que subir a mano
`ux0_data/asphalt6/netlog.txt` a `ux0:data/asphalt6/netlog.txt` en la consola (FTP/VitaShell)
con la IP de la PC que va a correr `psvita-toolkit logs-live`. Se creó con `ip 192.168.3.35`
(interfaz `en0` de esta Mac, mismo `/24` que `vita_ip` en `.psvita-toolkit.json`) -- confirmar
que sea la interfaz correcta (hay también `en1: 192.168.3.31` en la misma red) antes de asumir
que el archivo ya está bien.

### Bug #005 — NULL deref en `BaseCarManager::GetPackFile` (asset ausente, `Audi_RS3_2010.car`) — 2026-09-01

**Log:** `logs/live_session_20260901_115934.log` -- confirma que el fix del Bug #004 funcionó
(`fopen(ux0:data/asphalt6/data/file000000.dat, rb): 0x81700010`, ya no falla). El juego avanza
mucho más: shaders, decenas de `fileNNNNNN.dat` cargando bien. Últimas líneas antes del
crash:
```
[WARNING] fopen(ux0:data/asphalt6/data/Audi_RS3_2010.car, rb): 0x0
[WARNING] fopen(ux0:data/asphalt6/data/Audi_RS3_2010.car, rb): 0x0
```

**Dump:** `logs/asphalt6-psp2core-1788278391-0x0000e72f0d-eboot.bin.psp2dmp` -- PC en
`libasphalt6.so + 0x48da90` (base recalculada a `0x98000000`, mismo patrón de "fuera de
rango" del auto-detector que en los bugs #002/#004), dentro de
`BaseCarManager::GetPackFile(int, int) + 0xb8`. Mismo patrón exacto que el Bug #004
(`ldr r3, [r4]` con `R4 == 0`, confirmado en registros): la función llama a
`this->createAndOpenFile(nombre)` (mismo vtable slot 3 = `createAndOpenFile` que en
`addObfuscationFileMap`) con un nombre construido por `GetPackFilename()`, y usa el resultado
sin chequear `NULL` cuando el archivo no existe.

**Investigación de la causa del archivo faltante:** no se encontró `Audi_RS3_2010.car` (ni
ningún `.car`) en ningún lado de lo extraído del APK (`asphalt6_extract/`,
`decompiled/apk_jadx/`, `ux0_data/asphalt6/data/`, ni en el listado de `Asphalt-6-Clean.apk`
-- que además tiene `assets/` completamente vacío, señal de que los `fileNNNNNN.dat` vinieron
de otro lado, probablemente `copy_inject_extracted/`, ya no presente en el árbol). No se pudo
confirmar si el archivo existe en el juego original real (no queda el APK original ni el
`copy.inject` para volver a extraer) -- "Audi RS3 2010" es consistente con contenido
descargable/Gameloft LIVE que la edición offline podría no incluir.

**Fix aplicado (decisión del usuario: parche binario directo, sin más investigación):** parche
binario real vía `psvita-toolkit so-patch --apply-patch 0x48d9d8 --so
decompiled/apk_jadx/resources/lib/armeabi-v7a/libasphalt6.so --mode arm` -- reemplaza el
**inicio** de `BaseCarManager::GetPackFile` (antes de su prólogo `push {r4-r8,lr}`) por
`mov r0, #0 ; bx lr`. No es un parche a ciegas: el `0` que devuelve es exactamente el mismo
valor que la función YA devuelve en su propio camino legítimo de "índice inválido" (rama
`blt`/`bge` al principio de la función, pseudo-C/disasm confirmado) -- o sea, estamos
forzando el camino de "pack no encontrado" que el código original ya sabe manejar, no
inventando un contrato nuevo. Al parchear en el punto de entrada (antes del `push`), no hace
falta rebalancear pila ni preservar registros -- la única salida del parche es la de un
"return" limpio. Backup automático (`.orig`) generado por `so-patch`. Copiado también a
`ux0_data/asphalt6/lib/armeabi-v7a/libasphalt6.so` (ruta real que usa `SO_PATH` en
`source/utils/init.c`/`CMakeLists.txt` -- la copia vieja en la raíz de `ux0_data/asphalt6/`
estaba en la ruta equivocada).

**Efecto secundario aceptado:** `GetPackFile` queda deshabilitado para TODOS los autos, no
solo el que falta -- si en esta corrida se llega a necesitar un auto que sí está presente,
también devolverá "no encontrado". Se aceptó este alcance más amplio a cambio de un parche
mecánicamente simple y seguro (sin tocar mitad de función/balance de pila). Si aparece un
crash distinto por "no hay autos disponibles", ese es el próximo paso a investigar.

**Pendiente:** subir el `.so` parcheado a la consola
(`ux0:data/asphalt6/lib/armeabi-v7a/libasphalt6.so`, por FTP/VitaShell -- el deploy normal no
sube datos de juego) y volver a probar.

**Actualización -- hallazgo posterior con el APK descomprimido (`Asphalt-6-Adrenaline/`,
provisto por el usuario):** `copy.inject` (dentro de ese APK) resultó ser un ZIP real (volcado
de datos privados de la app -- `cache/`, `databases/`, `shared_prefs/`, `libs/libasphalt6.so`
-- NO los assets del juego). Pero incluye `pack.info` (manifiesto binario de descarga) que
lista `file000000.dat` hasta **`file000956.dat` (957 archivos)**. Nuestro
`ux0_data/asphalt6/data/` solo tiene hasta `file000864.dat` (865 archivos, **sin huecos**,
cortado limpio al final) -- **faltan los archivos 865-956 (92 archivos)**, no están en ningún
lado de lo que tenemos localmente.

Esto cambia la hipótesis del asset faltante: en vez de "Audi_RS3_2010.car" siendo contenido
DLC ausente de la edición offline, es más probable que sea uno de esos 92 `fileNNNNNN.dat`
faltantes (resuelto por nombre vía el mapa de ofuscación de `file000000.dat`, que sí carga
bien). `shared_prefs/GLGamePrefs.xml` (mismo ZIP) confirma
`SDFolder: /sdcard/gameloft/games/GloftA6HP`, coincidiendo con la ruta real vista en los logs.

El parche binario del Bug #005 sigue siendo válido como red de seguridad (evita el crash pase
lo que pase), pero si el usuario puede conseguir los archivos 865-956 reales (del dispositivo
original o de otra fuente), sería la fix más completa -- restauraría el contenido real en vez
de deshabilitar `GetPackFile` para todos los autos. Pendiente para una sesión futura si
aparece esa fuente.

### Bug #006 — NULL deref en `autoStartGame` (`strcpy` con `src == NULL`) — 2026-09-01

**Log:** `logs/live_session_20260901_125804.log` -- confirma que el fix del Bug #005 funcionó
(el juego avanza mucho más: perfiles cargados, `SaveCurrentProfile()`, shaders). Última línea
antes del crash: `[INFO] [ALOG][XXX] Launch Game by PN`.

**Dump:** `logs/asphalt6-psp2core-1788280907-0x00075320db-eboot.bin.psp2dmp` -- esta vez el PC
SÍ resolvió correcto de entrada (`asphalt6 + 0x932de`, `strcpy`, dentro de nuestro propio
loader/vitasdk) porque cae en un símbolo real del `.elf` del loader, no del `.so`. Por la
metodología de `so-crash-triage` (si PC cae en el loader/una lib real, `LR` es el frame que
importa): `LR` resuelve, con la base recalculada a `0x98000000`, a
`autoStartGame(int, char const*) + 0xc8`. Registros: `R1 == 0x00000000` (el `src` de
`strcpy`). Pseudo-C de Ghidra línea 17864: `strcpy(Application::m_autoStartRoomName,
param_2);` -- `param_2` (el nombre de sala a auto-unirse, típicamente vía notificación push)
llegó `NULL`. Símbolos confirman el flujo: `APushNotification::LaunchGamebyNotification()`
solo hace `CallStaticIntMethod` hacia Java (`GLGame.LaunchGamebyNotification`) -- en un port
offline sin push notifications reales, este camino nunca debería disparar `autoStartGame` con
datos válidos.

**Fix aplicado:** parche binario vía `so-patch --apply-patch 0x3e4600 --mode arm` -- reemplaza
el inicio de `autoStartGame` (antes de su `push {r4-r9,sl,lr}`) por `mov r0,#0 ; bx lr`. Es
`void`, así que el valor de r0 no importa -- el parche simplemente hace que la función no haga
nada. Justificado: esta función solo sirve para auto-unirse a una sala multijugador vía
notificación push/deep-link, algo sin sentido en un port offline sin backend real.

### Bug #007 — NULL deref en `RenderFX::Load` (assert "blando" que sigue igual) — 2026-09-01

**Log:** `logs/live_session_20260901_125804.log` (misma corrida que el Bug #006, después de su
fix) -- avanza mucho más: perfiles, `Launch Game by PN` ya no crashea, `GameRenderer_nativeInit`
termina. Últimas líneas antes del corte:
```
[WARNING] fopen(ux0:data/asphalt6/data/178loading.swf, rb): 0x0
[WARNING] fopen(ux0:data/asphalt6/data/178loading.swf, rb): 0x0
[ERROR] [ALOG][ASSERT] menufx.cpp: Load: 354
[ERROR] [ALOG][ASSERT] smart_ptr.h: operator->: 132
```

**Dump:** `logs/asphalt6-psp2core-1788281926-0x00040b25bb-eboot.bin.psp2dmp` -- Data abort real
(no un `abort()` limpio). Con la base recalculada a `0x98000000`, tanto `PC` como `LR` caen
dentro de `RenderFX::Load(char const*, gameswf::player_context*)`. La cadena de bytes
"178loading.swf" aparece literal en la pila del dump, confirmando qué asset faltaba.

**Causa raíz (cruzando disasm ARM real con el patrón de log):** el "assert" de este motor
(visto en decenas de sitios via `basename("jni/.../smart_ptr.h")`, el mismo patrón de
decompilación ya documentado) es **puramente diagnóstico** -- loguea "archivo: función: línea"
pero **no detiene la ejecución**: sigue de largo con el puntero nulo. Secuencia real
(`0x680dbc`-`0x680e34`): loguea "menufx.cpp: Load: 354", re-chequea el puntero (`r3 =
*(r4+0x3c)`), si sigue en `NULL` loguea "smart_ptr.h: operator->: 132", vuelve a leer el mismo
puntero (sigue en `NULL`, nada lo cambió) y hace `b 0x680c6c` **incondicional** -- ahí
`ldr r1, [r3, #16]` con `r3 == NULL` es el data abort real. `0x680c6c` también es alcanzado por
otros dos caminos que SÍ chequean `r3 != 0` antes (por eso no se puede tocar esa dirección sin
romper el camino feliz).

**Fix aplicado (parche binario a medida, no la plantilla estándar de `so-patch`):** dado que
`RenderFX::Load` es una función real usada para cargar SWF de menús en general (parchear su
entrada completa, como en los Bugs #005/#006, habría desactivado TODA la carga de menús Flash,
no solo este caso), se parcheó puntualmente el salto incondicional en
`libasphalt6.so` offset `0x680e34` (`b 0x680c6c` → `b 0x680c78`), saltando el
`ldr r1,[r3,#16]` + `RenderFX::SetContext(...)` problemáticos y aterrizando directo en el
chequeo de stack-canary/epílogo de la función (confirmado que ese destino no depende de nada
que la llamada saltada hubiera producido). Encoding ARM calculado a mano
(`objdump`/Python, `b` = `0xEA000000 | imm24`, `imm24 = (target - (pc_addr+8)) / 4`) y escrito
directo al `.so` con `python3` (no vía `so-patch --apply-patch`, que solo soporta la plantilla
fija `mov r0,#0 ; bx lr`) -- backup `.orig` ya existente de un patch anterior se preservó
(no se pisó).

**Patrón recurrente para tener en cuenta:** este mismo "assert blando que sigue de largo" con
`basename("jni/.../smart_ptr.h")` aparece en **más de 20 sitios** del binario -- cualquier
asset faltante que pase por un `smart_ptr` de este tipo puede producir el mismo crash en un
lugar distinto. Si reaparece, aplicar el mismo patrón de diagnóstico (ubicar el salto
incondicional posterior al segundo log de assert, redirigirlo al punto de retorno seguro de la
función en vez de neutralizar la función entera).

### Bug #008 — mismo patrón que #007, ahora en `RenderFX::SetViewport` — 2026-09-01

**Log:** `logs/live_session_20260901_135529.log` -- confirma que el fix del Bug #007 funcionó
(ya no crashea en el primer "smart_ptr.h: operator->: 132"). Pero aparece un SEGUNDO
"smart_ptr.h: operator->: 132" inmediatamente después, y ahí sí crashea.

**Dump:** `logs/asphalt6-psp2core-1788285409-0x0002662ef5-eboot.bin.psp2dmp` -- con la base
recalculada a `0x98000000`, PC cae en `gameswf::root::set_display_viewport(int,int,int,int)
+ 0x4` (primer acceso a `this`), llamado desde `RenderFX::SetViewport(int,int,int,int)`.
Exactamente el mismo patrón que el Bug #007: `SetViewport` llama a `gameswf::player::get_root()`,
si devuelve `NULL` loguea el assert (`basename(".../smart_ptr.h")` + "operator->: 132") y
luego hace `b 0x68778c` **incondicional** -- salta de vuelta al camino feliz sin volver a
chequear, terminando en `set_display_viewport(NULL, ...)`.

**Fix aplicado:** mismo tipo de parche a medida que el Bug #007 -- en `libasphalt6.so` offset
`0x687800`, `b 0x68778c` → `b 0x6877bc` (el epílogo limpio de `SetViewport`, el mismo destino
que ya usa el chequeo posterior `cmp r4,0 / beq 6877bc` que existe más adelante en la función
para el caso "root sigue siendo null"). Mismo método de encoding a mano
(`0xEA000000 | imm24`). Build/deploy: no hace falta recompilar el loader para esto (es un
parche al `.so`, no a `source/`) -- solo resubir `libasphalt6.so` parcheado.

**Patrón para las próximas sesiones:** cada vez que aparezca un crash con el mismo mensaje
"smart_ptr.h: operator->: 132" en un PC/LR nuevo, es la MISMA clase de bug (soft-assert que
sigue de largo) en otra función distinta -- repetir: ubicar la función vía PC/LR con base
`0x98000000`, buscar el `bl __android_log_print` seguido de un salto incondicional de vuelta
al camino feliz, y redirigir ese salto al punto de retorno seguro de ESA función específica
(no reusar direcciones de otro Bug -- cada función tiene su propio epílogo).

### Hallazgo — GLSL sí funciona, pero falta `file_mkpath()` antes de cachear shaders — 2026-09-01

**Contexto:** el usuario pidió comparar con `asphalt8-vita-main` (que sí corre) por las dudas
de que faltara soporte GLSL, dado que en TODOS los logs aparece
`fopen(.../glsl.config, rb): 0x0` y `file_save: Could not open ... .gxp` de forma constante
(no fatal, pero repetitivo).

**Confirmado:** Asphalt 6 sí emite GLSL real vía `glShaderSource`/`glCompileShader` (llamadas
interceptadas en `source/utils/glutil.c`), y el build ya usa `SHADER_FORMAT=GLSL` (default de
`CMakeLists.txt`) con `DUMP_COMPILED_SHADERS=ON` -- vitaGL traduce y compila esos shaders con
su propio traductor GLSL→Cg (confirmado en `README VITAGL.md`). Esto **ya funciona** -- no es
un feature faltante (a diferencia de `asphalt8-vita-main`, que sí necesitó un compat-layer
GLSL ES3→ES1 propio en `source/reimpl/shader_source_compat.c` porque corre un motor distinto,
basado en GLES3 real).

`glsl.config` es un archivo propio del juego (no de vitaGL) que simplemente no existe en
nuestros datos -- el motor ya tolera su ausencia (no crashea), así que no es prioritario.

El error real (aunque no fatal) es en `glCompileShader_soloader` (`glutil.c:105`):
`file_save(next_shader_fname, bin, len);` se llama **sin** crear antes el directorio
`ux0:data/asphalt6/gxp/` -- a diferencia del branch CG de `load_shader` (línea ~177), que sí
hace `file_mkpath()` antes. Como la carpeta nunca existe, el caché de shaders compilados
nunca se guarda -- el juego funciona pero recompila TODOS los shaders desde cero en cada
arranque (más lento, no incorrecto).

**Fix aplicado** (`source/utils/glutil.c`): agregado `file_mkpath(next_shader_fname, 0777);`
antes del `file_save()` en `glCompileShader_soloader`, igual que ya se hacía en el otro
branch. Build verificado con `psvita-toolkit build` (compila limpio). `eboot.bin` redesplegado.

### Bug #009 — heap corruption por buffer de 32KB fijo en el volcado de shaders — 2026-09-01

**Log:** `logs/live_session_20260901_140545.log` -- confirma que el fix de `file_mkpath` (de
la sesión anterior) funcionó: ya no aparece NINGÚN `file_save: Could not open...gxp` en todo
el log. El corte esta vez no tiene ningún mensaje previo (crashea en medio de carga de
texturas/shaders sin ningún warning/error inmediatamente antes).

**Dump:** `logs/asphalt6-psp2core-1788286687-0x0008b42785-eboot.bin.psp2dmp` -- tipo de crash
nuevo: PC cae en `SceLibKernel` real (`seg1+0x38`), `LR: 0x0` (no rastreable por LR). La pila
sí tiene símbolos reales del **loader propio** (no del `.so`, con debug info real):
`unserialize_shader at .../vitaGL/source/custom_shaders.c:602` y
`load_shader at .../source/utils/glutil.c:129`.

**Causa raíz confirmada** (bajando el código fuente real de vitaGL,
`github.com/Rinnegatamante/vitaGL/source/custom_shaders.c`, vía `curl` sobre
`raw.githubusercontent.com`): `glCompileShader_soloader` (`glutil.c`) reserva un buffer FIJO
de 32 KiB (`vglMalloc(32*1024)`) para volcar el shader recién compilado
(`vglGetShaderBinary(shader, 32*1024, &len, bin)`). Pero `vglGetShaderBinary()` **ignora por
completo el parámetro `bufSize`** -- solo lo usa para un chequeo de signo, y llama a
`serialize_shader(binary, length, s, GL_FALSE)`, que calcula el tamaño real serializado y hace
`memcpy` de esa cantidad exacta en `binary` **sin importar cuánto reservó el caller**. Cualquier
shader que compile a más de 32 KiB desborda nuestro buffer y corrompe memoria heap adyacente
-- el crash real ocurre DESPUÉS, en una asignación no relacionada (coincide exactamente con el
síntoma: `PC` dentro de `SceLibKernel`, `LR` en cero, la pila mostrando `unserialize_shader`/
`sceGxmShaderPatcherRegisterProgram` de OTRO shader distinto al que desbordó).

**Fix aplicado** (`source/utils/glutil.c`): buffer de volcado ampliado de 32 KiB a 512 KiB
(no hay forma de consultar el tamaño exacto de antemano vía la API pública de vitaGL -- el
único remedio es sobredimensionar). **Además** se limpió a mano la carpeta
`ux0:data/asphalt6/gxp/` en la consola (6 archivos de 8 bytes cada uno, de corridas
anteriores) antes de volver a probar, por las dudas de que algún caché quedara en un estado
raro -- aunque el análisis sugiere que el contenido de los ARCHIVOS guardados probablemente
era correcto (`serialize_shader` escribe el tamaño real sin importar el buffer de destino), la
corrupción ocurre en la memoria heap circundante durante el volcado, no en el archivo en sí.
Build verificado con `psvita-toolkit build`, `eboot.bin` redesplegado.

**Contexto -- por qué se investigó esto:** el usuario preguntó si Asphalt 6 necesitaría GLSL
real, señalando el port hermano `asphalt8-vita-main` (que sí corre) como referencia. Se
confirmó que Asphalt 6 SÍ usa GLSL real (`glShaderSource`/`glCompileShader` interceptados en
`glutil.c`) y que el build ya usa `SHADER_FORMAT=GLSL` -- vitaGL ya lo traduce/compila
correctamente (confirmado en `README VITAGL.md`); no hace falta el compat-layer GLES3→GLES1
que sí necesita `asphalt8-vita-main` (motor distinto, real GLES3). El hallazgo real de esta
investigación fue el bug de buffer fijo, no una carencia de soporte GLSL.

### Bug #010 — el esquema manual de cache de shaders sigue crasheando (guardado Y recarga) -- desactivado — 2026-09-01

**Log:** `logs/live_session_20260901_142853.log` -- confirma que el buffer de 512 KiB (fix del
Bug #009) funcionó para el guardado (cero errores `file_save` en todo el log). Corte sin
ningún mensaje previo, en medio de carga de texturas/shaders.

**Dump:** `logs/asphalt6-psp2core-1788290455-0x0009ea2b59-eboot.bin.psp2dmp` -- **mismo `PC`
exacto** que el Bug #009 (`0xe0002c68`, `SceLibKernel seg1+0x38`, `LR: 0x0`), pero esta vez la
pila muestra `load_shader at .../glutil.c:139` justo después de la línea
`glShaderBinary(1, &shader, 0, buffer, size)` -- o sea, ahora crashea en la ruta de **recarga**
del cache (`file_exists(gxp_path)` → `file_load` → `glShaderBinary` → `unserialize_shader` →
`sceGxmShaderPatcherRegisterProgram`), no en el guardado.

**Investigación adicional** (bajado también `source/gxm.c` real de vitaGL vía
`raw.githubusercontent.com`): el shader patcher de sceGxm usa memoria fija
(`shader_patcher_buffer_size`, 1 MiB por defecto) para el código de los programas registrados,
separada de la memoria "host" (que sí es dinámica vía callback). vitaGL además tiene su PROPIO
mecanismo de cache oficial y probado (`HAVE_SHADER_CACHE`, ver `README VITAGL.md`), que
requeriría recompilar `libvitaGL.a` desde fuente (no está vendorizado en este repo, se instala
precompilado via `vdpm`) -- no es algo que se pueda activar solo tocando el loader. Lo que
tenemos en `glutil.c` (`vglGetShaderBinary` + `file_save`/`file_load` +
`glShaderBinary` a mano) es un esquema manual, no el cache oficial, y no sobrevive un ciclo
completo guardar→recargar dentro de la misma sesión (mismo `PC` de kernel fallando en ambos
sentidos es demasiada coincidencia para ser ruido de stack).

**Fix aplicado:** en vez de seguir depurando los internals de `sceGxmShaderPatcher`,
`DUMP_COMPILED_SHADERS` pasa a `OFF` por defecto en `CMakeLists.txt` (antes `ON`) -- es
puramente una optimización de arranque (evita recompilar shaders ya vistos), no algo
funcionalmente necesario: sin ella, `load_shader` cae en la rama simple
(`#elif defined(USE_GLSL_SHADERS)`, `glutil.c` línea ~148) que solo hace
`glShaderSource(shader,1,&string,&length)` sin ningún cache -- exactamente el comportamiento
que ya veníamos usando (y que funcionaba) antes del Bug #009. Confirmado que
`psvita-toolkit build` no interactivo relee `CMakeLists.txt` en cada build y respeta el
default declarado ahí (`_discover_cmake_options`/`_prompt_cmake_options` en
`build_deploy.py` del toolkit) -- no hace falta pasar ninguna flag extra a mano. Build
verificado (`-DDUMP_COMPILED_SHADERS=OFF` confirmado en el log de `cmake`), `eboot.bin`
redesplegado.

**Pendiente para el futuro (no bloqueante):** si en algún momento se quiere recuperar el
cache de shaders para arranques más rápidos, la vía correcta es recompilar `vitaGL` desde
fuente con `HAVE_SHADER_CACHE=1` (mecanismo oficial, probado) en vez de reintentar el
esquema manual de `vglGetShaderBinary`/`glShaderBinary`.

### Bug #011 — tercer sitio del mismo patrón (`RenderFX::Update` llama `root::advance` con `root == NULL`) — 2026-09-01

**Log:** `logs/live_session_20260901_155707.log` -- confirma que desactivar
`DUMP_COMPILED_SHADERS` (Bug #010) funcionó: cero crashes de `SceLibKernel`, avanza mucho más
(perfiles, `Launch Game by PN`, un montón de texturas). Corte con el mismo trío de líneas de
siempre para `178loading.swf`, pero esta vez aparecen **tres** "smart_ptr.h: operator->: 132"
en vez de dos -- señal de un tercer sitio con el mismo patrón.

**Dump:** `logs/asphalt6-psp2core-1788293377-0x00093c2421-eboot.bin.psp2dmp` -- con la base
recalculada a `0x98000000`, PC cae en `gameswf::listener::advance(float) + 0x4` (primer acceso
a `this`), `LR` en `gameswf::root::advance(float, bool) + 0x2c` -- ahí es donde `root::advance`
llama a `this->m_listener.advance(dt)` con `this` (el propio `root`) en `NULL` (confirmado:
`R0` en el crash vale literalmente `0xb8`, que es `NULL + 184`, el offset del sub-objeto
`m_listener` embebido en `root`).

**Causa raíz** (pseudo-C de Ghidra, `RenderFX::Update(int, bool)` línea ~346563): mismo patrón
exacto que #007/#008, pero ahora el NULL nace en el CALLER de `root::advance`, no adentro:
```c
this = gameswf::player::get_root();
if (this == NULL) { /* log assert, decompilación cortada por el bug de basename@plt */ }
gameswf::root::advance(this, ...);   // this sigue NULL, sin volver a chequear
```
Disasm confirma: en `libasphalt6.so` offset `0x6811d4`, tras loguear "smart_ptr.h:
operator->: 132", hace `b 0x681090` **incondicional** -- vuelve al camino feliz (que llama
`add_ref()` y `root::advance()` sobre `sl` = resultado de `get_root()`, todavía `NULL`) sin
volver a chequear `sl`.

**Fix aplicado:** mismo tipo de parche a medida -- en `libasphalt6.so` offset `0x6811d4`,
`b 0x681090` → `b 0x6810ac` (justo DESPUÉS de la llamada a `root::advance`, salteando tanto
`add_ref()` como `advance()`). A diferencia de los Bugs #007/#008, el destino NO es el epílogo
de la función completa -- es el punto medio donde sigue el resto de la lógica de
`RenderFX::Update` (actualizar hasta 4 "effects" en `this+0x74`, chequeo de stack-canary), que
no depende de `root` y ya maneja `sl == NULL` correctamente más adelante (hay un
`cmp sl,#0 / beq` antes del único uso restante de `sl`, el `drop_ref()`). Build/deploy: solo
`.so`, no hace falta recompilar el loader.

**Patrón consolidado para las próximas sesiones:** cuando aparezca un nuevo
"smart_ptr.h: operator->: 132" en un PC/LR nuevo, el bug puede tener DOS formas: (a) el NULL
se genera y usa en la MISMA función (como #007/#008, redirigir al epílogo de esa función), o
(b) el NULL se genera en el caller y se pasa a OTRA función que lo desreferencia (como #011,
donde hay que mirar quién llama a la función que crashea, no la función en sí). En ambos
casos: ubicar el `bl __android_log_print` del segundo/último assert, y redirigir el salto
incondicional posterior al punto MÁS CERCANO donde el resto de la lógica no dependa del
puntero nulo (no necesariamente el epílogo completo -- a veces hay lógica útil e independiente
un poco más adelante, como en este caso).

### Bug #012 — Crashes en `root::advance` y `RenderFX::Find` causados por punteros nulos en `smart_ptr` — 2026-09-01

**Log:** `logs/live_session_20260901_202046.log` y varios dumps `asphalt6-psp2core-*-eboot.bin.psp2dmp.analysis.txt`

**Síntoma:** El juego sufría crash (Data Abort) repetidos en `gameswf::listener::advance` (debido a `r0 = 0xb8`) o en `RenderFX::Find` (`ldr r2, [r0, #16]` con `r0 = 0`). Ambos originados por dereferenciar punteros a objetos `gameswf::root` que no habían sido instanciados o cargados a tiempo, retornando punteros `NULL` a través de los envoltorios de `smart_ptr`.

**Causa raíz (RenderFX::Update -> root::advance):**
`RenderFX::Update` obtenía un `root` de `get_root()` y lo pasaba a `root::advance` asumiendo que no era `NULL`. Sin embargo, `root::advance` aplicaba `add r6, r0, #184` (para llamar a `m_listener.advance()`) e indirectamente provocaba el crash en `listener::advance` con el offset inválido `0xb8`.

**Causa raíz (RenderFX::Find -> smart_ptr::pt()):**
`RenderFX::Find` extraía directamente el puntero crudo del `smart_ptr` (en `this->field_3c`) sin verificar, procediendo a leer el campo `[r0, #16]`. Al ser nulo, el acceso fallaba catastróficamente provocando Data Abort en `0x98683b00`.

**Fix aplicado:** En lugar de parchar el archivo `.so` a nivel de bytes, se decidió usar una estrategia mucho más robusta y limpia: interceptación con "Naked Hooks" en C dentro del framework de SoLoader (`source/patch.c`):
1. **Hook para `root::advance`:** Un `__attribute__((naked))` en la dirección de `_ZN7gameswf4root7advanceEfb`. Comprueba si `r0 == 0`; si lo es, hace `bx lr` (salida segura temprana). Si es válido, emula las instrucciones reemplazadas (`push`, `vpush`) y salta (`ldr pc`) de vuelta a `0x985fa06c`.
2. **Hook para `RenderFX::Find`:** Se interceptó específicamente el punto de fallo en `0x683af8` (`RenderFX::Find`). El hook extrae el puntero del `smart_ptr` manualmente (`ldr r0, [r6]`), verifica si es `NULL`, y si lo es, aborta la función `RenderFX::Find` limpiando el stack (`pop {r4-r8, pc}`) y devolviendo `NULL`, emulando semánticamente un "elemento no encontrado". En caso válido, restaura el control de flujo saltando a `0x98683b00`.

**Beneficio del método:** Evita la corrupción o bloqueos sin alterar el binario `.so` (mantenibilidad) y protege contra fallos de temporización del motor al intentar acceder a la película SWF del HUD/Menú durante la carga. Recompilado en `asphalt6.vpk`.

### Bug #012 — CAUSA RAÍZ de toda la cadena `smart_ptr`: los assets con nombre SÍ estaban, ocultos — 2026-09-01

**Contexto:** tras los Bugs #007/#008/#011 (y los hooks de runtime en `source/patch.c`), el
mismo assert `smart_ptr.h: operator->: 132` seguía reapareciendo en sitios nuevos (ya iban
**cinco** en `logs/live_session_20260901_202811.log`). Era whack-a-mole: todos los consumidores
del `root` nulo que deja `RenderFX::Load` al no encontrar `178loading.swf`.

**El hallazgo.** En los logs había una asimetría clave que no habíamos explotado:
```
fopen(.../sphere_normal.tga): 0x0        <- falla el archivo suelto
fopen(.../file000622.dat):    0x817707f0 <- pero el motor abre el .dat correcto
"Loaded texture from file: sphere_normal.tga"   <- y lo carga bien
```
Las texturas sí se resolvían; los `.swf` no. Investigando `file000112.dat` apareció el patrón:
su header es `51 4d 06 08`, y del byte 4 en adelante es **idéntico** a un header ZIP local
estándar. Descifrado: **los primeros 4 bytes de cada asset están alterados con
`out[i] = in[i] + (i+1)`** (en `file00a.bin` la variante es `+1` en los 4). Revirtiéndolo:
- `51 4d 06 08` → `PK\x03\x04` (ZIP)
- `CTHW` → `BRES` · `bvwv` → `attr` · `vplj` → `unif` · `=A{q` → `<?xm` · `$fhj` → `#def`
- **`GYV\r` → `FWS\x09` y `GYV\x0c` → `FWS\x08` = archivos SWF**

**Conclusión: cada asset con nombre es un `fileNNNNNN.dat` individual con esos 4 bytes
alterados.** No faltaba nada -- estaba todo ahí, sin nombre. Inventario completo de los 865
`.dat`: 428 ZIP (bundles de modelos con `dependancies.txt`), 104 TGA, 102 GLSL, 45 BRES,
**37 SWF**, 18 XML, 1 BMP, resto formatos varios. (`file00a.bin`, 143 MB, es el pack de audio:
624 `.wav` + 6 `.vxn`, mismo truco de firma.) Los 37 SWF validan perfecto: el largo declarado
en el header SWF coincide **exactamente** con el tamaño del archivo en los 37 casos.

**Nombres reales:** la tabla de nombres de SWF está en el `.so` en `0xad0518`:
`splash_screen`, `main_menu`, `career_mode_menu`, `quickrace`, `main_menu_2d_hud`,
`car_selection`, `loading`, `igMenu`, `result_screen`, `End_Race_Multi`, `unlocks_screen`,
`End_Race_Single`, `all_tunning_menu`, `tuning_shop`, `main_menu_option`, `AStore`,
`AStore_Stars`, `AStore_Stars_PopOut`, `AStore_Cash`, `First_Language` (20). El prefijo `178`
se arma en runtime; en el `.so` conviven `"171"` y `"178"` (0xae2328/0xae232c) = los dos
perfiles. Los SWF vienen en dos familias de resolución: **1152x768** y **1024x768**, y el par
de `main_menu_2d_hud` lo desambigua: el de 1024x768 (`file000769`) contiene `CarSelect_ipad`
y el de 1152x768 (`file000056`) contiene `CarSelect_new` → **1024x768 = perfil iPad, 1152x768 =
perfil "178"**, el que usa nuestra build.

**Fix aplicado:** extraídos y demangleados, instalados como archivos sueltos en
`ux0_data/asphalt6/data/` (la ruta de carga suelta espera el SWF plano, ya que el motor
demanglea solo por su cuenta en su propia ruta de pack):

| archivo instalado | origen | archivo instalado | origen |
|---|---|---|---|
| `178loading.swf` | file000704 | `178End_Race_Single.swf` | file000435 |
| `178splash_screen.swf` | file000044 | `178quickrace.swf` | file000643 |
| `178main_menu.swf` | file000384 | `178car_selection.swf` | file000457 |
| `178main_menu_2d_hud.swf` | file000056 | `178career_mode_menu.swf` | file000719 |
| `178main_menu_option.swf` | file000092 | `178igMenu.swf` | file000353 |
| `178result_screen.swf` | file000091 | `178unlocks_screen.swf` | file000642 |
| `178End_Race_Multi.swf` | file000172 | `178tuning_shop.swf` | file000827 |
| `178all_tunning_menu.swf` | file000550 | `bg.bmp` | file000394 |

`178loading.swf` (file000704) es de alta confianza: junto con `file000293` (el par de 1024x768)
son los dos únicos SWF que contienen **solo** "loading" como referencia de pantalla, más
`initLoadingScreen`/`loading_sym`/`Loading02.tga`. `bg.bmp` también: es el **único** BMP del
pack y valida (magic `BM`, tamaño declarado 196662 = real, 256x256 24bpp). El resto del mapeo
es por contenido interno (símbolos ActionScript distintivos) y es **estimado** -- si alguno
está cruzado, el juego mostrará la pantalla equivocada pero **no crasheará** (el SWF es válido
y `root` deja de ser nulo), y el log dirá qué nombre pidió.

**Corrección de una conclusión previa:** los Bugs #004/#005 dieron por "contenido ausente de la
edición offline" a `Audi_RS3_2010.car` y a los `.swf`. Eso era **incorrecto**: no faltaban,
estaban con la firma alterada y sin nombre. Los 92 `fileNNNNNN.dat` que faltan (865-956) siguen
faltando de verdad, pero son muchos menos críticos de lo que parecía.

### Infra — log en archivo en vez de UDP — 2026-09-02

**Motivo:** el sink UDP (`netlog`, agregado 2026-09-01) requería tener `psvita-toolkit
logs-live` corriendo y la consola alcanzable por red durante TODA la sesión -- se caía
seguido (la consola entra en sleep/se desconecta, el proceso en background muere) y había que
estar reiniciándolo constantemente. Además el pack `sceIoWrite`, `sceIoOpen` ya solucionan el
problema real: sobrevivir hasta el último dato antes de un data abort.

**Cambio:** `source/utils/logger.c` reescrito para escribir cada línea (sin colores ANSI) con
`sceIoWrite` directo (sin buffering de libc) a `ux0:data/asphalt6/logs/session.txt` --
`psvita-toolkit` ya sabe descargar el "último log" de exactamente esa carpeta
(`vita_logs_dir` en `.psvita-toolkit.json`) vía FTP, el mismo mecanismo que ya se usa para
bajar los `.psp2dmp`. No hace falta ningún listener corriendo durante la sesión -- alcanza con
un `curl`/FTP GET después, con la consola en VitaShell.

**Eliminado:** `source/utils/netlog.{h,c}`, la entrada en `CMakeLists.txt`, las libs
`SceNet_stub`/`SceNetCtl_stub` (sin más usos en el proyecto), y `ux0_data/asphalt6/netlog.txt`.
Build verificado con `psvita-toolkit build` (compila limpio, sin depender de `SceNet`).

**Pendiente:** desplegar el `eboot.bin` nuevo (la consola tenía el juego corriendo, no
VitaShell con FTP activo -- reintentar cuando esté en VitaShell) y, después de la próxima
corrida, bajar `ux0:data/asphalt6/logs/session.txt` por FTP en vez de depender de
`logs-live`.

**Actualización -- numeración incremental 001-999:** en vez de un único `session.txt`
sobrescrito en cada corrida, ahora usa el mismo esquema de rotación que `Asphalt-5-Vita`
(port hermano, mismo patrón ya probado): `ux0:data/asphalt6/logs/asphalt6_NNN.log`
(`NNN` de `001` a `999`), con un archivo `next.idx` en la misma carpeta que recuerda el
próximo índice a usar entre arranques (busca el primer slot libre desde ahí, envolviendo una
vez; si los 999 están ocupados, recicla el que apunta el hint). Así una corrida anterior no
se pisa antes de bajarla por FTP. Build verificado con `psvita-toolkit build`, `eboot.bin`
redesplegado.

### Bug #013 — CAUSA RAÍZ de la pantalla negra: el motor swapea por JNI (`swapEGLBuffers`), no por EGL — 2026-09-05

**Log:** `logs/asphalt6_009.log` (build Release con la traza `[HANG-DBG]` de `glutil.c`).

**Síntoma:** pantalla negra permanente. La traza muestra `nativeRender ENTER/EXIT` para los
frames 0, 1 y 2, y después **`nativeRender ENTER frame=3` sin su EXIT**: el motor se queda
adentro de esa llamada compilando shaders (pares `file000647`/`file000526`, programas 18-33) y
dibujando ~12 veces (`glUseProgram` 10/1 → `glDrawArrays` → `glDrawElements` → `glFlush`), y
después el log **deja de crecer** — cuelgue duro, no bucle rápido.

**Causa raíz (confirmada leyendo el `.so`, no adivinada).** Este motor (`glitch`, un fork de
Irrlicht) **nunca llama `eglSwapBuffers`**. Presenta cada frame con un callback JNI:

```
glitch::CAndroidOSDevice::flush()            (libasphalt6.so 0x701898)
  if (mbIsEnableSwapBuffer == 0) return;
  env = NVThreadGetCurrentJNIEnv();
  CallStaticVoidMethod(env, GameRenderer.class, swapEGLBuffers);   // vtable +0x234
```

El `jmethodID` lo resuelve `GameRenderer_nativeInit` (`0x3ceffc`), que hace cuatro
`GetStaticMethodID` (vtable +0x1c4). Los nombres, resueltos a mano desde el disasm
(`ldr rX,[pc,#N]` + `add rX,pc,rX`) y leídos del `.rodata`, son:
`getKeyboardText()[B`, `setKeyboard(ILjava/lang/String;I)V`, `isKeyboardVisible()I` y
**`swapEGLBuffers()V`**.

Ninguno de los cuatro estaba en la tabla `nameToMethodId[]` de `source/java.c`, así que
`GetStaticMethodID` devolvía NULL y `CallStaticVoidMethod` caía en el `methodVoidCall()` de
FalsoJNI que solo loguea "method ID 0 not found" — **el swap era un no-op**.

Eso explica los DOS síntomas de una sola vez:

1. **Nada llegaba al display durante la carga.** `Loading::Start()` prende
   `mbIsEnableSwapBuffer` (arranca en 0, está en `.bss`) y después `Loading::DisplayFrame()`
   dibuja + `endScene()` (de ahí el `glFlush`) + `driver->swapBuffers(2)` cada 100 ms, todo
   **adentro** de una sola llamada a `nativeRender()` que no retorna en varios segundos. El
   `gl_swap()` que `main.c` hacía después de `nativeRender` nunca corría en esa ventana.
2. **El cuelgue.** vitaGL difiere `sceGxmEndScene` hasta `vglSwapBuffers` (`scene_reset()` en
   `lib/vitaGL/source/gxm.c:565-592`). Sin swap, la escena de sceGxm **nunca se cierra**: los
   ring buffers de vértices/fragmentos del contexto GXM se llenan y `sceGxmDraw` se bloquea
   esperando a una GPU que no puede drenar hasta el `EndScene` que nunca llega. Deadlock, con
   el log cortado justo después del último `glFlush`. (Además el circular pool de vitaGL
   tampoco se recicla: `vgl_circular_idx` sólo avanza en el swap.)

**Fix aplicado:**

- `source/java.c`: registrados `swapEGLBuffers` (id 50) → `gl_swap()`, más
  `getKeyboardText`/`setKeyboard`/`isKeyboardVisible` como no-ops explícitos y `Exit` (id 38,
  el `nativeExit()` del motor) → `sceKernelExitProcess(0)`.
- `source/utils/glutil.c/.h`: `gl_swap()` ahora incrementa `gl_swap_count`.
- `source/main.c`: el bucle principal presenta el frame **sólo si el motor no lo hizo** durante
  ese `nativeRender` (compara `gl_swap_count` antes/después). Hace falta el respaldo porque en
  el juego normal (fuera de carga) `Game::GameRender()` termina en `endScene()` y **no** llama
  a `swapBuffers` — en Android eso lo hacía el `GLSurfaceView` de Java al volver de
  `onDrawFrame`; acá lo hacemos nosotros.
- `source/reimpl/egl.c`: `eglSwapBuffers()` (que este motor no usa) también pasa por
  `gl_swap()`, para que el contador valga aunque alguna ruta del `.so` lo llamara.

**Hallazgos secundarios corregidos en la misma pasada (todos confirmados con disasm):**

- **`GameRenderer_nativeInit` toma CINCO argumentos**, no cuatro:
  `nativeInit(env, clazz, w, h, language)` → `appInit(r2, r3, [sp,#48])` y `appInit` guarda el
  tercero en `mCurrentLanguage`. `main.c` pasaba sólo 4, así que el idioma salía de basura de
  la pila. Ahora se pasa 0 (inglés).
- **Firma real del input táctil**: `nativeTouchPressed/Moved/Released(env, clazz, jint x,
  jint y, jint id)` — **enteros**, no `jfloat`, y el orden es (x, y, id), no (id, x, y).
  `notifyTouchPress` arma un `SEvent` de Irrlicht con `EventType=1` en +0, X en +8, Y en +12 y
  el código de evento en +20 (0=press, 3=release, 6=move); el 3er argumento de
  `nativeTouchPressed` ni se lee (el motor pisa +4 con su propio flag de doble-tap).
  Nuevo `source/utils/touch.{c,h}`: panel frontal → eventos del motor, escalado del rango real
  del panel (`sceTouchGetPanelInfo`) a 960x544, con seguimiento multi-dedo por slot propio
  (el `SceTouchReport.id` del hardware crece sin límite, no sirve de índice).
- **Tipos JNI equivocados en `java.c`**: `IsWifiEnabled`, `IsInternetAvaliable`,
  `isExternalMusicActive`, `IsFirmwareBefore22` y `getWifiIP` están declarados `()I` en el `.so`
  y el motor los invoca con `CallStaticIntMethod` (+0x204) — estaban registrados como
  `METHOD_TYPE_BOOLEAN`/`OBJECT`, así que `methodIntCall()` no los encontraba nunca.
  `getVersion`/`getHostName` están declarados `()[B` y sus consumidores
  (`nativeGetVersion`/`nativegetHostName`) hacen `GetArrayLength` + `GetByteArrayRegion`: un
  `jstring` de FalsoJNI no es un `JavaDynArray`, así que devolvían largo 0 — ahora son
  `jbyteArray` de verdad. Además `GLGame_nativeIsXperia`/`nativeGetLanguageIndex` estaban
  declarados con firma `(JNIEnv*, jobject)` dentro de tablas que esperan
  `(jmethodID, va_list)` (compilaban con warning).
- **Traza `[HANG-DBG]` desactivada**: cada línea era un `sceIoWrite` sin buffering a la SD, una
  por `glDrawArrays`/`glDrawElements`/`glUseProgram`/`glGetUniformLocation`. Los envoltorios
  quedan, pero el log ahora se compila fuera por default (`-DTRACE_GL_CALLS` para reactivarlo);
  los fallos de compile/link de shaders se siguen reportando siempre.

**Pendiente de verificar en consola real:** que con el swap arreglado el frame 3 termine y
aparezca la pantalla de carga → splash → menú. Si vuelve a cortarse, el log ya no tiene el
ruido de la traza GL y el corte va a marcar el punto real.

### Bug #014 — se traba entrando al menú (render targets del menú 3D) — 2026-09-05

**Log:** `logs/asphalt6_010.log` (build Debug, primer arranque con el fix del Bug #013).

**Confirmado del fix anterior:** el usuario **ve la pantalla de carga y el indicador se mueve**
-- el swap por `swapEGLBuffers` funciona. Ya no es pantalla negra.

**Dónde se traba, con precisión.** El corte es determinista y cae en el MISMO punto que el log
009 (el de antes del fix), contado de dos formas independientes:

| medición | log 009 (Release, traza GL) | log 010 (Debug) |
|---|---|---|
| shaders compilados | 66 (`compile OK`) | 66 (132 intentos de `glsl.config`, 2 por shader) |
| programas linkeados | 33 (`link OK`) | -- |
| pares del ubershader `file000647`/`file000526` | 16 | 16 |

O sea: llega hasta el final del preload del menú (perfiles → `Launch Game by PN` →
`178loading.swf` → `178splash_screen.swf` → `file000688.dat` con todas las texturas del menú
3D: `Menu_MultimapMain`, `MenuLogo_A62`, `Menu_Plants`, `MenuMattepaint`, `lightmap_full`),
compila los 16 programas del ubershader del menú, dibuja un frame de carga más y se congela con
ese frame en pantalla.

**Hipótesis principal (y fix aplicado): `scenesPerFrame = 1` en los render targets de sceGxm.**

El menú de este motor NO dibuja una sola escena por frame:
`T_SWFManager::SWFSet3DRenderTargets()` (`0x4d0…`, llamada desde `On3DLoad()`) compone la
escena 3D adentro de la UI Flash vía render-to-texture -- crea texturas `MenuRenderTarget` y
`MenuFlash_Screen_node03` con `SceneHelper::GetGenericTexture` --, y además hay una cadena de
post-procesado (los shaders con `blurOffsetX`/`blurOffsetY`/`threshold`/`uvScale`/`scale` que
se compilan en el arranque).

En vitaGL, cada cambio de framebuffer cierra la escena de sceGxm y abre otra
(`scene_reset()`, `lib/vitaGL/source/gxm.c:565`). Un frame del menú abre entonces VARIAS
escenas sobre el mismo render target -- el del display incluido, cada vez que se vuelve del
FBO. Y vitaGL crea sus render targets con `scenesPerFrame = 1` por default
(`gxm_display_rt_size`/`gxm_fbo_rt_size`, `gxm.c:115-116`). Con ese valor, el segundo
`sceGxmBeginScene` del frame **se bloquea** esperando que la escena anterior de ese target
termine de mostrarse, cosa que no puede pasar hasta que se presente el frame: deadlock, con el
último frame presentado (el de carga) congelado en pantalla. Encaja exactamente con el
síntoma.

**Fix aplicado** (`source/utils/glutil.c`, `gl_init()`): `vglSetupRenderTargetScenesNum(8, 8)`
antes de `vglInitExtended` (8 = `MAX_SCENES_PER_FRAME`, el máximo de sceGxm; tiene que ir antes
porque el render target del display se crea dentro de `vglInit*`).

**Instrumentación agregada en la misma pasada** -- para que, si el cuelgue persiste, el
siguiente log lo ubique sin tener que reproducirlo otra vez:

- **`vgl_log` → nuestro archivo de log.** vitaGL define su macro de log como `sceClibPrintf`
  (`lib/vitaGL/source/utils/debug_utils.h`), que en retail no se puede leer. Ese header está
  parcheado para declarar una función que implementa `source/utils/glutil.c` y reenvía al log
  que bajamos por FTP, y se compila vitaGL con `LOG_ERRORS=1`. A partir de ahora aparecen en
  el log, con prefijo `[vitaGL]`: fallos de `sceGxmBeginScene`, fallos al crear render targets,
  fallos de registro/parcheo de programas en el shader patcher, allocations de GPU fallidas
  (incluido el fallback "forcefully free"), overrun del circular pool y los warnings/errores
  del compilador Cg. (Se filtran las líneas `Shader Compiler: I]`, puro ruido informativo.)
- **Traza gruesa siempre activa** (`gl_info` en `glutil.c`): `compile BEGIN/END` y
  `link BEGIN/END` **con milisegundos** -- distingue "colgado en el compilador" de "avanzando
  muy lento" --, los tres `glBindFramebuffer`/`glFramebufferTexture2D`/
  `glCheckFramebufferStatus` (que es justo el camino del render-to-texture del menú), y un
  latido cada 60 frames presentados.
- **Reporte de memoria** (`gl_report_mem`): memoria libre de vitaGL (RAM/VRAM/PHYCONT) y uso de
  los tres buffers FIJOS de 1 MiB del shader patcher (`shader_patcher_*_size` en `gxm.c`, sin
  API pública para agrandarlos). Se emite tras `vglInit`, tras `nativeInit` y después de CADA
  link -- con 66 shaders y 33 programas, agotar esos buffers era el otro candidato serio.
- **Hilo testigo** (`source/utils/watchdog.{c,h}`): late cada 5 s con los frames presentados y
  el último hito del hilo principal. Corre con prioridad MÁS ALTA que el hilo principal a
  propósito: si éste se queda girando en un bucle cerrado sin bloquearse, un hilo de menor
  prioridad no llegaría a correr y no habría latido -- justo el caso a diagnosticar. Es lo que
  separa "el proceso murió" de "el hilo principal está bloqueado en la GPU" de "sigue vivo pero
  lentísimo".

**Cómo leer el próximo log:**
- Latidos `[wd]` que siguen apareciendo pero `frames` que no sube → el hilo principal está
  bloqueado (GPU / `sceGxmBeginScene`); mirar el último hito y las líneas `[vitaGL]` previas.
- Sin latidos → el proceso murió: buscar el `.psp2dmp` y usar `so-crash-triage`.
- `link END ... (N ms)` con N enorme → no está colgado, está compilando; bajar
  `vglSetupRuntimeShaderCompiler` a `SHARK_OPT_SAFE` y/o confiar en el cache de shaders.
- `glCheckFramebufferStatus -> ... (INCOMPLETO!)` o `[vitaGL] Failed to create a rendertarget`
  → el problema es el render-to-texture del menú.

### Bug #015 — el cuelgue del menú NO era `scenesPerFrame`: descartado, y acotado a `MenuScene::MenuScene` — 2026-09-05

**Log:** `logs/asphalt6_014.log` (Release + traza GL). Cruzado con 009, 011, 012 y 013.

**El fix del #014 no movió nada.** Con `vglSetupRenderTargetScenesNum(8, 8)` puesto, el corte
cae en el MISMO punto, y el final del log es idéntico en las **cinco** corridas comparables
(009, 011, 012, 013, 014 -- y la 010 ya lo mostraba igual):

```
... link END program=33  (16 pares del ubershader del menú)
glGetUniformLocation prog=33 name='Sampler0' -> ...
glDrawElements mode=0x4 count=72 type=0x1403
glFlush BEGIN / glFlush END
[wd] 15 frames (+1 en 5s) | último hito: swap #14 hace 2659 ms
[wd] 15 frames (+0 en 5s) | ... (para siempre)
```

Dos cosas quedan **confirmadas** por ese log:

1. **No es un cuelgue de GPU en el render-to-texture del menú.** `glBindFramebuffer` aparece
   UNA sola vez en toda la corrida (`fb=0`, al arrancar). El motor nunca llegó a tocar un FBO,
   así que la hipótesis del #014 (varias escenas de sceGxm por frame sobre el mismo render
   target) no puede ser la causa: se cuelga antes. El `vglSetupRenderTargetScenesNum(8, 8)` se
   deja igual porque es correcto para lo que viene después, pero no es el fix de esto.
2. **El hilo principal no está bloqueado en vitaGL.** `watchdog_mark("swap", n)` se marca al
   ENTRAR a `gl_swap()` y `gl_swap_count` se incrementa al SALIR: el testigo reporta
   `15 frames` con último hito `swap #14`, o sea que el swap número 15 **terminó**. El motor
   volvió de vitaGL y se trabó en código propio.

**Dónde se traba, acotado con el pseudo-C (hipótesis, sin confirmar en consola).**
En todo el `.so` hay sólo 8 llamadas a `Loading::DisplayFrame()` — las dos de
`StateStack::DoStateChange`/`PopState` y **seis dentro de los dos constructores de
`MenuScene::MenuScene(char const*)`**. O sea: cada frame de la pantalla de carga que se ve
en el log sale de uno de esos puntos, y el último frame dibujado es el que está justo después
de `glitch::collada::CColladaDatabase::constructScene()` — que es lo que compila los 16
programas del ubershader al crear los materiales de la escena del menú. El tramo sin
checkpoint que sigue, y donde por lo tanto está el cuelgue, es:

```
constructScene(file000688.dat)  -> compila shaders 35..66 / linkea programas 18..33
Loading::DisplayFrame()         <- ÚLTIMO frame que se ve (swap #14)
SceneHelper::RemoveChildNodeType(scene, 0x7468676c /* 'lght' */)
bucle de re-parenting: strstr(nodo->getName(), "_node")  -> addChild()
CBatchMesh + CustomBatchGridSceneNode(16x16)  <- batching de la geometría del menú
ISceneManager::vtable+0x58 (batchNode)
CustomAnimator::createAnimator(scene, nombre)
CLightSceneNode
Loading::DisplayFrame()         <- NUNCA llega
```

`Loading::Stop()` (que sigue después, ya fuera del constructor) llama a
`SoundManager::ResumeAllSounds()`, así que el sonido es el otro sospechoso — pero está
*después* del `DisplayFrame` final del constructor, y ese frame no se ve.

**Dos puntos ciegos del port encontrados en el camino, ya corregidos:**

- **Todos los mensajes de FalsoJNI eran invisibles.** `FalsoJNI_Logger.c` imprime con
  `sceClibPrintf`, que en una consola retail no va a ningún lado que podamos leer -- así que
  sus avisos más útiles (`method ID 0 not found`, `GetMethodID: not found`, `Could not find
  the array`) nunca aparecieron en ningún log de este port. Ahora se reenvían al MISMO archivo
  que se baja por FTP, con prefijo `[FalsoJNI]` (`fjni_log_sink()` en `source/utils/logger.c`,
  mismo truco que ya se usaba para `vgl_log`). Importa para este bug: el motor toca
  `android/media/AudioTrack` por JNI crudo (`vox::DriverAndroid::_InitAT` hace `FindClass` +
  `GetMethodID("<init>"/"play"/"write"/...)`), nada de eso está en la tabla de `java.c`, y
  hasta ahora no había forma de ver qué devolvía.
- **Tres símbolos importados que caían en el stub dummy.** Comparando los 272 símbolos
  indefinidos del `.so` (`readelf --dyn-syms`) contra la tabla de `dynlib.c` faltaban `div`,
  `cosh` e `inet_addr`. `div` es el que duele: devuelve una struct por puntero oculto en `r0`,
  así que el stub que devuelve 0 dejaba `quot`/`rem` con basura de pila --
  `SceneHelper::TimeToStr`/`TimeToStrMSC` (los tiempos de vuelta del menú) daban números
  aleatorios. Los tres registrados.

**Instrumentación agregada para la próxima corrida** — pensada para responder de una sola vez
la pregunta que ningún log anterior podía responder: *¿el hilo principal está bloqueado o
girando, y en qué?*

- **Anillo de migas** (`source/utils/breadcrumb.{c,h}`): las últimas 128 llamadas
  interceptadas, guardadas en RAM (5 palabras por llamada, sin locks y sin tocar la SD). Se
  registra **ENTRADA y SALIDA** de cada una, así que una `ENTRA` sin su `sale` marca
  exactamente en qué llamada quedó trabado. Cada entrada guarda la dirección de retorno y se
  imprime como `libasphalt6.so+0xNNNN`, que es el offset que se busca directo en
  `decompiled/.../out_ghidra.c`. Instrumentadas: todas las GL que ya se envolvían **más**
  las que iban derecho a vitaGL y pueden esperar a la GPU (`glTexImage2D`,
  `glCompressedTexImage2D`, `glTexSubImage2D`, `glDeleteTextures`, `glBufferData`, `glClear`,
  `glGenFramebuffers`, ...), `pthread_mutex_lock`/`cond_wait`/`cond_timedwait`/`join`,
  `usleep`/`nanosleep`/`sched_yield`, y `fopen`/`open`/`stat`/`opendir`.
- **Anillo caliente aparte** para `malloc`/`calloc`/`realloc`/`free` (32 entradas, sin
  syscalls: sólo nombre y dirección de retorno). Es el que cubre el peor caso -- el motor
  girando en un bucle de código puro que no toca ni GL ni archivos --, porque casi cualquier
  bucle de este motor reserva memoria y el anillo queda lleno de las direcciones de retorno
  DE ESE BUCLE. Justamente el caso del batching de `MenuScene`.
- **Estado real de cada hilo** en el hilo testigo (`sceKernelGetThreadInfo`): estado
  (`CORRIENDO`/`LISTO`/`BLOQUEADO`/`DORMIDO`), tipo e id del objeto que espera, prioridad,
  core y **microsegundos de CPU consumidos desde el latido anterior**. Eso separa las tres
  posibilidades de una: `BLOQUEADO` + `cpu=+0` es un deadlock (y `espera(tipo,id)` dice sobre
  qué); `CORRIENDO` + `cpu=+5.000.000` es un bucle cerrado; y si el que gira es un hilo del
  motor con prioridad más alta que el principal, explica que el principal no avance sin estar
  bloqueado en nada. Los hilos que crea el `.so` se registran desde
  `pthread_create_soloader()`.
- **Contador de reservas por latido** en la línea de latido (`+N reservas`): la señal más
  barata de "sigue girando" vs "no toca nada".
- **`nativeRender` marcado a la entrada y a la salida**: durante la carga UNA sola llamada
  puede tardar segundos, así que "trabado adentro de nativeRender" y "trabado entre dos
  nativeRender" son bugs distintos y ahora se distinguen.
- El volcado completo (hilos + los dos anillos) sale solo, cuando pasan 2 latidos (10 s) sin
  un frame nuevo, y se repite cada 30 s -- repetirlo distingue "trabado en la MISMA llamada"
  de "girando y cambiando de llamada".
- **`TRACE_GL_CALLS` vuelve a OFF**: el anillo da la misma información en el momento del
  cuelgue, sin un `sceIoWrite` por draw ni el sesgo de timing que eso mete.

**Cómo leer el próximo log:**
- `[wd] ... principal ... estado=BLOQUEADO ... espera(tipo=0x… id=0x…) cpu=+0 us` → deadlock.
  Mirar la última `ENTRA` sin `sale` del anillo: dice en qué primitiva, y el
  `libasphalt6.so+0x…` dice qué función del motor la llamó.
- `estado=CORRIENDO/LISTO` con `cpu` cerca de 5.000.000 → bucle cerrado. Si `+N reservas` es
  grande, el anillo caliente tiene las direcciones de retorno del bucle; buscar ese offset en
  el pseudo-C. Si es 0, el bucle no reserva nada y hay que subir la instrumentación un nivel
  (candidato: proteger el `.text` del `.so` con `kuKernelMemProtect` desde el testigo para
  forzar un `.psp2dmp` con el PC exacto del hilo trabado).
- Otro hilo `CORRIENDO` con prioridad **más chica** (más alta) que la del principal → no es un
  deadlock, es inanición; el candidato natural es el hilo de audio
  (`vox::DriverAndroid::UpdateThreadedAT`), cuyo pacing en Android lo daba
  `AudioTrack.write()` y acá vuelve al instante.
- Líneas `[FalsoJNI]` cerca del corte → qué llamada JNI se quedó sin implementar.

### Infra — paridad de vendor/config con Asphalt-5-Vita (hermano que sí corre) — 2026-09-05

**Motivo:** el 008 se cortaba en el mismo punto con la vitaGL vendorizada compilada con flags
distintos a los del port hermano. Se igualó todo lo portable (verificado por diff directo
contra `/Volumes/Seagate/PSVITA Develop/Asphalt-5-Vita`):

- **Flags vitaGL** → `SOFTFP_ABI=1 NO_DEBUG=1 HAVE_SHADER_CACHE=1 NO_SPLASHSCREEN=1
  DRAW_SPEEDHACK=2` (antes `MATH_SPEEDHACK=1 DRAW_SPEEDHACK=1 HAVE_SHARK_LOG=1`, sin
  `HAVE_SHADER_CACHE`). `DRAW_SPEEDHACK=2` es el fix de A5 para draws grandes (Bugs
  #19/#20/#22); `HAVE_SHADER_CACHE=1` activa el cache oficial (default
  `ux0:data/shader_cache`, sin llamar nada desde el loader). Rebuild limpio verificado
  (`-mfloat-abi=softfp` en el log de compilación).
- **Parche `HAVE_RAZOR`** en `lib/vitaGL/source/vgl.c` (`vglSetShaderAssociationPath`),
  copiado tal cual de A5: protege la llamada devkit-only en vez del stub falso en
  `source/patch.c` (eliminado — con `--allow-multiple-definition` el stub podía pisar al
  símbolo real de un futuro vitashark).
- **Stamp anti-stale en `CMakeLists.txt`** (misma idea que A5): si cambian
  `VITAGL_MAKE_FLAGS`, se borran los `.o`/`.a` viejos automáticamente — antes, editar los
  flags no reconstruía nada y se linkeaba la `.a` vieja sin avisar. Más target `vitagl-clean`.
- **`vglInitExtended(0, 960, 544, 12 MiB, SCE_GXM_MULTISAMPLE_NONE)`** — el EGL que
  reportamos dice `SAMPLES=0`, así que forzar 4X era inconsistente.

**No copiado a propósito:** el FBO de downsample de A5 (su motor renderiza a otra resolución
y reescala viewports; A6 declara 960x544 nativos).

**Resultado:** el 012 se corta en el MISMO punto (draw 72 índices → flush → nada) — no eran
los flags. Pero la paridad se queda: es la config probada del hermano.

### Infra — enums benignos que vitaGL rechaza (`[vitaGL] INVALID_ENUM`, log 011) — 2026-09-05

Tres rechazos, verificados uno por uno contra el fuente de vitaGL — **ninguno es la causa del
cuelgue**, pero ensuciaban el log y el usuario pidió corregirlos:

- `glDisable 0x0BD0` = `GL_DITHER` (el juego lo apaga; en GXM no aplica).
- `glDisable 0x809E/0x80A0` = `GL_SAMPLE_ALPHA_TO_COVERAGE`/`GL_SAMPLE_COVERAGE` (estados de
  MSAA; corremos `MSAA_NONE`, ignorarlos es lo correcto).
- `glPixelStorei 0xD05` = `GL_PACK_ALIGNMENT` (vitaGL sólo acepta `GL_UNPACK_ROW_LENGTH` en
  `glPixelStorei` — `lib/vitaGL/source/textures.c` — e ignora el alignment internamente).

**Fix** (`source/utils/glutil.c`, `glEnable/glDisable/glPixelStorei_soloader` + remapeo en
`dynlib.c`): se absorben en el loader, el vendor queda intacto.

### Infra — relojes del motor (portado de A5) + `TRACE_GL_CALLS` como opción — 2026-09-05

- `source/reimpl/sys.{c,h}` + `dynlib.c`: `gettimeofday`→reloj de proceso monotónico,
  `usleep`/`nanosleep` sub-ms→no-op, `sched_yield`→no-op (idéntico a A5). El motor vive de
  `glitch::os::Timer::getRealTime()` (= `gettimeofday` → ms), usado por el pacing de
  `Loading::DisplayFrame` (cada 100 ms) y `CFPSCounter::registerFrame`.
- `CMakeLists.txt`: opción `TRACE_GL_CALLS` (default OFF) para la traza pesada por-llamada,
  sin editar código. **TEMP: ON para la corrida 013**, volver a OFF después.

### Análisis — el corte cae entre `glFlush` y el swap, dentro de `endScene` (decompiled) — 2026-09-05

**Log:** `logs/asphalt6_012.log` (TRACE ON). Últimas líneas: queries del programa 33 →
`glDrawElements(0x4, 72)` → `glFlush` → nada (watchdog: 15 swaps, hito `swap #14`, parado).

Cruzado con `decompiled/libasphalt6_armeabi-v7a/ghidra/out_ghidra.c`:

- `CCommonGLDriver::endScene()` hace, en orden: `(vtable+0x1fc)()` → **`glFlush()`** →
  `IVideoDriver::endScene()` (`getRealTime` + `CFPSCounter::registerFrame`) → retorno →
  `swapBuffers(2)` → `swapBuffersImpl` → `CAndroidOSDevice::flush()` (JNI → `gl_swap`).
- El wrapper anterior de `glFlush` logueaba una sola línea ANTES de llamar al flush real, así
  que "corte en glFlush" era ambiguo. Ahora traza BEGIN+END.

**Dos hipótesis, que el 013 discrimina de una línea:**

- `glFlush END` presente → el flush volvió y el cuelgue está en código del juego
  (`registerFrame`/camino al swap; relojes ya corregidos arriba).
- Corte en `glFlush BEGIN` sin END → GPU hang en el draw de 72 índices: inspeccionar esos
  datos de vértices/índices (`idx=0x8232af40`) + estado GXM.

### Bug #015 (cont.) — log 015: giro de CPU en `MenuScene`, y dos agujeros en la instrumentación — 2026-09-06

**Log:** `logs/asphalt6_015.log` (Debug, 1024 líneas). Llega al mismo muro que los 5 logs
anteriores: 66 shaders / 33 programas (los 16 pares del ubershader del menú
`file000647`/`file000526`), texturas del menú 3D (`file000688.dat`), 13 frames, último
hito `swap #12`. Después, 30+ s sin un frame nuevo.

**Lo que el 015 sí discrimina (primera vez con el testigo + migas):**

- Principal `CORRIENDO` (no `BLOQUEADO`), `espera(tipo=0 id=0xFFFFFFFF)`, CPU alta →
  **giro en código propio, no deadlock ni GPU hang**. El swap #13 terminó (el contador
  `gl_swap_count` sube al SALIR de `gl_swap`), así que el `DisplayFrame` posterior a
  `constructScene()` SÍ se presentó y el giro está en el tramo siguiente del constructor
  de `MenuScene::MenuScene` (pseudo-C `out_ghidra.c:58302`): `RemoveChildNodeType` →
  bucle de re-parenting (`strstr(nombre, "_node")`) → `CBatchMesh`/`CustomBatchGrid` →
  `createAnimator` → `CLightSceneNode` → `DisplayFrame` final (nunca llega).
- `+0 reservas` en todos los latidos colgados → el giro **no aloca**: encaja con el bucle
  de re-parenting o `RemoveChildNodeType` (puras llamadas virtuales + `strstr`, sin
  `malloc`), no con el batching/animator (que sí reservan).
- El volcado final no trae NI UNA entrada del hilo principal: las 128 son
  `pthread_cond_timedwait`/`mutex_lock`/`usleep` de 3 workers. Y el volcado de hilos
  lista SOLO a `principal`, aunque esos 3 workers siguen vivos.

**Dos agujeros reales en la instrumentación, ambos corregidos en esta pasada:**

1. **El anillo común lo inundan los workers.** Llenan 128 entradas en milisegundos, así
   que la última llamada del principal queda expulsada antes del volcado: "cero entradas
   del principal" NO prueba "giro en código puro", es un artefacto de expulsión.
   Fix (`source/utils/breadcrumb.{c,h}`): `bc_set_main_tid()` + anillo aparte de 32
   solo para el principal (`---- ultimas del hilo principal (sin ruido de workers) ----`
   en `bc_dump`), que los workers no pueden expulsar.
2. **Hilos del .so sin registrar.** Los workers del volcado final nacen en `JNI_OnLoad`
   (antes de `watchdog_start`) o por vías que no pasan por `pthread_create_soloader`,
   así que el testigo no los conocía y no podía diagnosticar inanición. Fix en dos
   capas: `main.c` arranca testigo + migas + `bc_set_main_tid` ANTES de `JNI_OnLoad`, y
   `bc_push` auto-registra (`watchdog_register_thread`, ahora con dedup por uid) todo
   hilo que toque un wrapper.

**Contador `strstr` (`source/dynlib.c` + latido del testigo):** el `strstr` que importa el
`.so` pasa por `strstr_soloader` (un atómico, sin syscalls ni log, misma semántica). El
latido ahora dice `+%u reservas +%u strstr`: giro en el re-parenting ⇒ `strstr` explota
con reservas en 0; giro en `RemoveChildNodeType`/batching/animator ⇒ ambos en 0.

**Cómo leer el próximo log (actualiza la guía del #015):**

- Sección `hilo principal` del volcado con una `ENTRA` sin `sale` ⇒ ahí se traba (por
  fin fiable: ya no la expulsan los workers). El offset `libasphalt6.so+0x…` se busca
  en `out_ghidra.c`.
- `+N strstr` grande por latido + reservas en 0 ⇒ bucle de re-parenting
  (`MenuScene::MenuScene`, `out_ghidra.c` ~línea 58440-58470): mirar lista de hijos
  corrupta/cíclica. Ambos en 0 ⇒ `RemoveChildNodeType`, batching (`vtable+0x58`) o
  `createAnimator`: el siguiente paso es hookear esas 3 con ENTER/EXIT (ver `patch.c`).
- Volcado de hilos ahora con TODOS los hilos: otro hilo `CORRIENDO` con prioridad más
  alta (número más chico) que el principal ⇒ inanición, no giro propio.

Build verificado con `psvita-toolkit build --preset debug` (compila y linkea limpio,
`asphalt6.vpk` regenerado). **Pendiente:** desplegar el `eboot.bin` nuevo a la consola y
bajar el próximo `asphalt6_NNN.log` por FTP.

### Bug #015 (cont.) — log 016: giro silencioso tras el swap #12, re-parenting descartado — 2026-09-06

**Log:** `logs/asphalt6_016.log` (Debug, 1096 líneas). Llega al MISMO muro: shaders
hasta `compile #65/#66` + `link prog=33` (los 16 pares del ubershader del menú),
13 frames, último hito `swap #12`, y ahí se queda (dos latidos `+0 reservas +0
strstr` con el hito envejeciendo 5→10 s + volcado final).

**Dos cosas nuevas que el 016 sí enseña:**

1. **El primer volcado del testigo fue un falso positivo, y el sistema lo manejó
   bien.** A los ~10 s sin frames (en plena tormenta de compiles, shaders 5-15) el
   testigo volcó estado... y el juego SIGUIÓ: 4 frames (`swap #3`), texturas del
   menú 3D, splash, 12-13 frames. Ese volcado temprano sirve igual: muestra al
   principal sano (locks en `0xA6C394` + `fopen` en `0x3C6B2C` + mutex en
   `0x8B6384`, anillo caliente con malloc/free) = carga normal. `stalled_beats` se
   resetea al avanzar los frames, así que no hubo spam de volcados.
2. **El cuelgue final es un giro sin NINGUNA llamada interceptada.** Principal
   `CORRIENDO`, `+0 reservas +0 strstr` en 10+ s, y la sección del hilo principal
   del volcado tiene sus 32 entradas TODAS con 12 s de antigüedad (ráfagas de
   `pthread_mutex_lock` desde `0xA6C394`, balanceadas ENTRADA/SALIDA: no está
   bloqueado adentro de ninguna). Los workers vox tickean normal
   (`mutex_lock`+`usleep`, `cond_timedwait` cada ~30 ms, `VoxThread::_Update`,
   `AccessController Get/ReleaseRead/WriteAccess`, `PriorityBankManager::Update`):
   ni deadlock niinanición ni hot-spin de audio.

**Offsets del anillo resueltos a símbolo** (readelf `-W`, base `0x98000000`):

| offset | símbolo real |
|---|---|
| `0xA6C394` | `__gnu_cxx::__exchange_and_add` — los atomics de bionic van por mutex: cada `add_ref`/`drop_ref` de `smart_ptr` pasa por `pthread_mutex_lock`. Las ráfagas del principal son refcounting, no locks lógicos |
| `0x85B0EC` | `glitch::thread::CCondition::wait` (workers sanos) |
| `0x8B6384` | `CFileSystem::createAndOpenFileFromArchives` (carga sana) |

**Descartes que el 016 permite (todos por `+0` en la ventana colgada):**

- Bucle de re-parenting (`strstr(nombre,"_node")` por hijo, inline en el
  constructor, `out_ghidra.c:58444-58466): haría CIENTOS de `strstr` por latido si
  la lista fuera cíclica. `+0 strstr` lo descarta.
- Todo lo que alloque (`CBatchMesh`/`CustomBatchGrid`/`createAnimator`/
  `CLightSceneNode` hacen `operator_new`): `+0 reservas` descarta que el giro esté
  DENTRO de esas construcciones (puede estar ENTRE ellas, en código que no alloca).
- `sched_yield`/`usleep`/`nanosleep`, GL, `fopen`/`open`/`stat`: silencio total.

**Sospechoso principal ahora: `SceneHelper::RemoveChildNodeType`
(`out_ghidra.c:77769`, `0x46220C`, 128 bytes).** Recorre hijos recursiva +
iterativamente (`while (sentinel != cur) { recurse(hijo); cur = *cur; }`) con
puras llamadas virtuales (`getType` en `+0xbc`, `drop` en `+0x68`): cero syscalls,
cero mallocs (si no hay nodos `'lght'` no hay `free`), cero strstr. Si la lista de
hermanos que dejó `constructScene`+`addChild` es cíclica, gira PARA SIEMPRE con
exactamente esta firma. Candidato #2: crunch del batching (`vtable+0x58` tras el
ctor del grid) sobre una escena muy grande a 444 MHz — mismo silencio pero
TERMINARÍA (lento, no infinito).

**Instrumentación agregada para zanjarlo en UNA corrida más** (build Debug
verificado, `asphalt6.vpk` regenerado):

- `source/patch.c`: 7 hooks ENTER-only (marcan `bc_enter` en el anillo del
  principal y reanudan la función emulando sus 8 bytes pisados) sobre el tramo en
  orden: `MenuScene::C1/C2` (`0x4423C8`/`0x441BC0`), `RemoveChildNodeType`
  (`0x46220C`), `CustomBatchGrid::C2` (`0x50AA8C`), `createAnimator(char*)`
  (`0x50A14C`), `CLightSceneNode::C1` (`0x74478C`), `Loading::DisplayFrame`
  (`0x4A4128`). `hook_trace()` verifica la primera palabra del prólogo antes de
  parchear y lo reporta si el `.so` no es el esperado. Sin hooks de SALIDA a
  propósito: dos ENTRADAS consecutivas ya acotan el intervalo del giro.
- Contadores nuevos (misma técnica atómica sin syscalls que `strstr`):
  `strcmp`/`strncmp`/`memcmp` (`dynlib.c`) y `gettimeofday` (`sys.c`); el latido
  ahora dice `+N reservas +N strstr +N strcmp +N gettod`.

**Cómo leer el próximo log (017):**

- Última `ENTRA X` FRESCA en `---- ultimas del hilo principal ----` ⇒ el giro
  está dentro de X (o entre X y la siguiente ENTRADA esperada). Si es
  `RemoveChildNodeType` ⇒ lista cíclica confirmada (siguiente paso: hook con
  contador de iteraciones + watchdog de ese bucle, o parchear la condición).
  Si es `CustomBatchGrid` sin `createAnimator` ⇒ crunch del batching (siguiente
  paso: paciencia/perfilar, no es infinito).
- `+N strcmp` grande ⇒ parseo (animator/XML); `+N gettod` grande ⇒ espera activa
  de tiempo; ambos en 0 + última ENTRADA vieja ⇒ giro en código inline del propio
  constructor (re-parenting ya descartado: quedaría batching inline).
- Dejar la consola quieta: el volcado se repite cada 30 s solo — dos volcados
  idénticos separan "lento" de "infinito" sin adivinar.

**Pendiente:** desplegar `eboot.bin` nuevo, correr, bajar `asphalt6_017.log`.

### Bug #017 — REGRESIÓN: los hooks ENTER crashean en el primer `DisplayFrame` (falta una indirección) — 2026-09-06

**Log:** `logs/asphalt6_017.log` (557 líneas, muere a los ~30 s sin NINGÚN frame).
**Dump:** `logs/asphalt6-psp2core-1788744603-0x0010723fe9-eboot.bin.psp2dmp`.

**Síntoma:** data abort con PC en `libasphalt6.so + 0x4A4138` (base recalculada a
`0x98000000`, como siempre) = `ldrb r3, [r3]` dentro de `Loading::DisplayFrame +
0x10`. La pila muestra el frame de `hook_frame` (`bl bc_enter` ya retornado):
el hook disparó, emuló, reanudó... y 8 bytes después crasheó.

**Causa raíz (mía, aritmética exacta, no hipótesis):** la emulación del
`ldr r3, [pc, #N]` cargaba la DIRECCIÓN del literal en vez de su VALOR (una
indirección de menos). Prueba: `R3 = 0x3094837c` en el dump es exactamente
`0x984A413C (pc del add) + 0x984A4244 (= text_base + 0x4A4244, la dirección del
literal que puse en r3) = 0x13094837C → 0x3094837C`. El `add r3, pc, r3` posterior
fabrica una dirección basura y el `ldrb` aborta. Afectaba a los 5 stubs con
`ldr`/`vldr` PC-relativo (c1, c2, light, anim, frame); los 2 verbatim
(RemoveChildNodeType, CustomBatchGrid) estaban bien. El juego nunca llegó al
menú: el PRIMER `DisplayFrame` con hook (tras `file000112.dat`, shaders 27-30)
lo mató — por eso 0 frames en todo el log.

**Fix aplicado** (`source/patch.c`): doble indirección en los 5 stubs
(`ldr r12,=global; ldr r12,[r12]; ldr rX,[r12]` = el VALOR del literal, igual que
el `ldr` original) + comentario de la trampa en la cabecera. Build verificado
con `psvita-toolkit build --preset debug` (limpio, `asphalt6.vpk` regenerado).

**Dato útil que el 017 sí deja (líneas base de los contadores nuevos, carga
temprana sana):** `+41k strcmp` y `+6-9k gettod` por latido de 5 s durante
compiles — el parseo XML es strcmp-pesado y `Timer::getRealTime` sondea mucho;
ninguno de los dos es el giro (el juego avanzaba). En el 018, un giro real se
vería como MILLONES por latido de uno de ellos con el resto en 0.

**Pendiente:** redesplegar `eboot.bin`, correr, bajar `asphalt6_018.log` (+
`.psp2dmp` si hay). Si los hooks están bien esta vez, la última `ENTRA` fresca
del anillo principal acota el giro del Bug #015 según la guía del 016.

### Bug #015 (cont.) — log 018: los hooks ubican el giro DENTRO del `DisplayFrame` final, con firma de espera de tiempo — 2026-09-06

**Log:** `logs/asphalt6_018.log` (1308 líneas; el usuario la dejó 70+ s colgada:
`swap #12 hace 70613 ms` al final — infinito confirmado, no "lento").

**Los hooks del 016 funcionan (doble indirección OK, cero crashes):** el anillo
del principal muestra la cola completa del constructor —
`... createAnimator` → (refcounting) → `CLightSceneNode` → `DisplayFrame` — y
nada después durante 12-44 s. O sea: se pasó `RemoveChildNodeType`, re-parenting,
batching, `createAnimator` y el bloque `CLight`; **el giro está dentro del
`DisplayFrame` FINAL del constructor** (o justo después, antes del próximo
`ENTRA` esperado).

**Firma nueva y decisiva:** `+0 reservas +0 strstr +0 strcmp` pero **`+9256
gettod` cada 5 s sin parar** (~1850 `gettimeofday`/s, un solo syscall+comparación
por vuelta). El reloj NO está congelado (el `since` del testigo, que sale del
mismo `sceKernelGetProcessTimeLow`, avanza normal): es un bucle que pregunta la
hora ~1850 veces/s sin ninguna otra actividad, antes del swap de ese
`DisplayFrame` (el hito sigue en `swap #12`: su `gl_swap` nunca se entró).

**Candidatos dentro de `DisplayFrame`** (pseudo-C `out_ghidra.c:111080`):
`IDevice::run()` → `beginScene` → `RenderFX::Update` → `RenderFX::Render` →
`endScene` (`getRealTime` + `registerFrame`) → `swapBuffers`. Cualquiera con un
`while (tiempo...)` que nunca se cumple (frame-limiter, sync de animación con
audio que no avanza porque `AudioTrack.write` es no-op, lista circular con
timestamp por nodo) da exactamente esta firma.

**Instrumentación agregada** (`source/patch.c`, 5 hooks ENTER-only más, todos de
emulación verbatim — ningún literal PC-relativo, sin el riesgo del Bug #017):
`IDevice::run` (`0x85A004`), `RenderFX::Update` (`0x681044`),
`RenderFX::Render` (`0x687CDC`), `CCommonGLDriver::endScene` (`0x913CA8`) e
`IVideoDriver::endScene` (`0x7ED3B0`). La última `ENTRA` fresca del 019 dice cuál
contiene el giro (o, si no hay ninguna nueva, que está DESPUÉS de `DisplayFrame`:
epílogo del constructor / `Loading::Stop` / `ResumeAllSounds`). Build verificado
con `psvita-toolkit build --preset debug` (limpio, `asphalt6.vpk` regenerado).

**Nota sobre velocidad (pedido del usuario en esta sesión):** la carga hasta el
`swap #12` tarda ~30 s y es estructural (parseo collada con +41k strcmp/5 s,
66 shaders compilados a Cg en runtime, ~300 texturas, todo en CPU a 444 MHz) —
"segundos" en frío no es realista en este hardware. Lo que parecía "loading
eterno" es este giro infinito, no lentitud: con el giro resuelto, el resto es
optimización (build Release + verificar hits del `shader_cache` de vitaGL) para
una pasada posterior, cuando haya gameplay que medir.

**Pendiente:** desplegar `eboot.bin`, correr, bajar `asphalt6_019.log`.

### Bug #018 — crash en `IDevice::run` drenando una cola de eventos que nadie llena — 2026-09-06

**Log:** `logs/asphalt6_019.log` (600 líneas, muere en `link prog=17`, 0 frames).
**Dump:** `logs/asphalt6-psp2core-1788749289-0x00122030fb-eboot.bin.psp2dmp`.

**Síntoma:** data abort con PC en `libasphalt6.so + 0x85A030` (base `0x98000000`)
= `ldm lr!, {r0-r3}` dentro de `IDevice::run + 0x2C` (el hook de rastreo ya había
vuelto: el crash está 0x24 después de su reanudación). `R9=R11=0xdeadbeef` y zona
de pila con `deadbeef`: punteros basura.

**Causa raíz (disasm + pseudo-C `out_ghidra.c:702701`):** `run()` drena una
`std::deque<SEvent>` (en `this+0xB8`, `_M_cur` en `+0xC0`/`+0xD0`) copiando cada
evento a pila y re-despachándolo. El `ldm` lee del read-ptr, que apunta a memoria
no mapeada (región `0x814e...` vs buffer en `0x8231...`). Y esa cola **nadie la
llena en este port**: `IDevice::postEventFromUser` solo encola con `bool=true`,
y TODOS sus llamadores en el `.so` pasan `false` (los 3 `notifyTouch*`, el propio
`run()`; `postMouseEventFromUser` no lo llama nadie acá — verificado barriendo
los `bl 0x85a6b8` del disasm). Con la cola vacía por construcción, el camino
legítimo es el `beq` que salta el drenado; entrar igual es lotería de heap
(016/018 la esquivaron, 019 no).

**Fix aplicado** (`source/patch.c`, `hook_run`): el stub ya no emula el
`ldr r3,[r0,#208]` original sino que fuerza `r3 = ip`, así el `cmp r3,ip / beq`
toma siempre el camino de cola vacía que el propio código maneja. El despacho
directo de eventos (touch → `postEventFromUser(...,false)`) no toca la cola y
sigue intacto. Si reaparecen punteros salvajes en OTRO sitio (corrupción de heap
real y no solo esta víctima), el próximo dump lo dirá — no se asume nada más.
Build verificado con `psvita-toolkit build --preset debug` (limpio).

**Pendiente:** desplegar `eboot.bin`, correr, bajar `asphalt6_020.log`.

### Sesión 2026-09-07 — snapshot: todo el trabajo sin commitear se documenta y commitea

Hasta acá había 2000+ líneas sin commitear (desde el port inicial): tabla JNI,
`swapEGLBuffers`, input táctil, `div`/`cosh`/`inet_addr`, relojes, `vglSetupRenderTargetScenesNum`,
paridad vitaGL con A5, enums benignos, testigo+anillo+logs unificados, contadores,
12 hooks ENTER y la guarda de `IDevice::run`. Todo eso ya estaba narrado arriba bug por bug;
este commit lo congela tal cual, con un mensaje que dice lo que es: **intento de
localización del giro del menú, verificado solo con build local** (`psvita-toolkit build
--preset debug` limpio). Nada de lo agregado se probó todavía en consola real: la
verificación pendiente es desplegar el `eboot.bin` de este commit, correr y traer el
`asphalt6_020.log` (los hooks de segundo nivel dirán si el giro está en `run`/`Update`/
`Render`/`endScene` o después de `DisplayFrame`). Si el 020 trae un crash nuevo, se
triagea con `so-crash-triage` como siempre. Archivos que quedan fuera del commit a
propósito: `logs/` y `*.psp2dmp` (gitignored), `compile_commands.json` y los `Makefile`
de `lib/vitaGL/samples/` (generados).
