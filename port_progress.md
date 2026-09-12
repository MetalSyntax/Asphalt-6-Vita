# Registro de Progreso — Asphalt 6 (PS Vita)

> **Cómo leer este archivo:** es cronológico, y las entradas viejas se dejan como se
> escribieron aunque después se hayan demostrado equivocadas -- saber qué hipótesis se
> descartó, y por qué, vale tanto como el fix. Lo que está vigente hoy está acá arriba.

## Estado actual — 2026-09-11

**¡El juego ya llega hasta el menú principal con renderizado y presentación activa!**
Con la resolución del Bug #022 (fallback de idioma inglés en `StringManager::GetLanguageString`
y protección contra `std::string(NULL)`), el motor supera "First time launch the app", guarda
exitosamente `pn.dat`/`timespent.dat`, inicializa `FlashFXHandler` y la interfaz Flash (`gameswf`),
y entra por completo al menú principal (`GS_MenuMain`), presentando frames de forma continua.

Próximos objetivos: interactividad completa del menú (verificar touch/mapeo de botones físicos)
y emulación de audio (`android/media/AudioTrack`).

| Área | Estado |
|---|---|
| Carga del `.so`, relocación, resolución de símbolos | funciona (272/272 importados resueltos) |
| Tabla JNI / ciclo de vida `GLGame`+`GameRenderer` | funciona |
| Gráficos (vitaGL, GLES2, GLSL en caliente) | funciona: 66 shaders / 33 programas sin errores |
| Presentación de frames (`swapEGLBuffers`) | funciona, presentando frames continuamente en el menú |
| Assets (`fopen` sobre `ux0:data/asphalt6/data/`) | funciona |
| Menú principal (`MenuScene`, `OnLoad3DScene`, `GS_MenuMain`) | **funciona: llega al menú principal** (Bugs #015-#022 resueltos) |
| Input táctil | implementado, pendiente de prueba interactiva en el menú |
| Audio | **nada implementado** -- hay que emular `android/media/AudioTrack` por JNI |
| "First time launch" (guardado de `pn.dat`/`timespent.dat` en adelante) | funciona (Bug #022 resuelto) |

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
| #015-#021 | Cuelgue entrando al menú (`MenuScene`, bucle sin cota en `OnLoad3DScene`, `std::sort` sin strict weak ordering) | resueltos |
| #022 | `abort()` por `std::logic_error` en `StringManager::SetLanguage(NULL)` tras primer arranque | resuelto |

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

## Fase 5: Llegar al menú principal (Completada — 2026-09-11)
- [x] Que se vea algo en pantalla (Bug #013: `swapEGLBuffers`).
- [x] Instrumentación para diagnosticar cuelgues, no sólo crashes (Bug #015: hilo testigo con
      estado de kernel por hilo, anillo de migas, y los logs de `[vitaGL]`/`[FalsoJNI]`/`[ALOG]`
      unificados en el archivo que se baja por FTP).
- [x] Bugs #015-#021: identificar y arreglar el cuelgue dentro de `MenuScene::MenuScene`.
- [x] Bug #022: arreglar el crash al guardar `pn.dat` / "First time launch the app" (`StringManager` idioma NULL) permitiendo llegar al menú interactivo.
- [x] **¡Llegada confirmada al menú principal!**

## Fase 6: Navegación del menú, entrada a carrera y audio (En progreso)
- [ ] Verificar input táctil y mapear controles físicos (botones / analógicos de PS Vita).
- [ ] Implementar subsistema de audio (`vox::DriverAndroid` / `android/media/AudioTrack`).
- [ ] Probar transición de inicio de carrera y renderizado 3D en pista.


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

### Bug #015 (cont.) — log 021: sin crash (la guarda de `run` funciona), mismo giro de espera de tiempo — 2026-09-10

**Log:** `logs/asphalt6_021.log` (1306 líneas, Debug). El usuario la dejó 60+ s colgada
(`swap #12 hace 61833 ms` al final).

**Lo nuevo: ya no hay crash.** No se generó `.psp2dmp` y el juego llega al mismo muro de
siempre (66 shaders / 33 programas, texturas del menú 3D, 13 frames, último hito `swap
#12`). La guarda de `hook_run` (Bug #018, forzar camino de cola vacía) aguantó una corrida
completa hasta el menú — el crash intermitente del 019/020 quedó atrás.

**El giro es el mismo del 018, byte por byte:** principal `CORRIENDO`, `+0 reservas +0
strstr +0 strcmp` y `+~9k gettod` por latido sin parar, workers sanos, anillo del principal
congelado con la misma cola (`createAnimator` → `CLightSceneNode` → `DisplayFrame` y nada
después durante 46 s). Sigue siendo una espera activa que solo pregunta la hora.

**Lo que el 021 SÍ discrimina (y el 018 no podía): los 5 hooks de segundo nivel nunca
dispararon.** Ni una `ENTRA IDevice::run` / `RenderFX::Update` / `RenderFX::Render` /
`endScene` en todo el log, con el `DisplayFrame` final entrado hace 46 s.

**Verificación estática (capstone sobre el `.so` local, modo ARM):** `DisplayFrame`
(`0x4A4128`) es lineal hasta la llamada a `run` (`bl 0x85A004` en `0x4A41A8`): dos chequeos
de flags, `counter++`, UNA llamada a `getRealTime`, comparan `< 100` y retornan o siguen a
`run`. No hay ningún bucle antes de `run`, y el camino vacío de `run` (el que fuerza la
guarda: `cmp r3,ip / beq 0x85A074` → dos llamadas virtuales → epílogo) tampoco sondea el
reloj. La primera palabra de `run` es la esperada (`ldr ip,[r0,#0xC0]`), así que el hook
debería instalarse. Conclusión: el giro **no** está entre `DisplayFrame` y `run` — o bien
`DisplayFrame` retornó y el giro está en código posterior sin instrumentar (epílogo del
constructor de `MenuScene` → `Loading::Stop` → `ResumeAllSounds`, todos sin hooks), o bien
el `eboot` desplegado es anterior a los hooks de segundo nivel. El 021 no distingue ambas;
el 022 sí (ver abajo).

**Instrumentación agregada para zanjarlo en UNA corrida (build Debug verificado,
`asphalt6.vpk` regenerado):**

- `bc_clock_site()` (estaba declarado en `breadcrumb.h` pero sin implementar ni usar):
  `gettimeofday_soloader` guarda `__builtin_return_address(0/1/2)` en atómicos sin
  syscalls — ra1 es el llamador de `Timer::getRealTime`, es decir, el bucle. `bc_dump`
  lo imprime como `sitio de reloj: bucle en libasphalt6.so+0x…` (o fuera del `.so`).
- `hook_trace` ahora loguea cada hook instalado (`[patch] hook en +0x…`): el 022 prueba
  por sí mismo que los 12 hooks están vivos y descarta "build viejo" sin adivinar.

**Cómo leer el próximo log (022):** buscar `sitio de reloj` en el volcado — ese offset en
`out_ghidra.c` es la función del giro, fin de la cacería. Si además no hay líneas `[patch]
hook en +0x85A004…`, el build desplegado no traía los hooks y hay que redesplegar.

**Pendiente:** desplegar `eboot.bin`, correr, bajar `asphalt6_022.log`.

### Bug #015 (cont.) — log 022: los hooks de segundo nivel están vivos pero `run` no se entra; el reloj lo quema un worker — 2026-09-10

**Log:** `logs/asphalt6_022.log` (1114 líneas, Debug). Mismo muro (13 frames, `swap #12`,
`+~9k gettod` por latido con todo lo demás en 0, principal `CORRIENDO`).

**Tres cosas que el 022 deja confirmadas:**

1. **Los 12 hooks están vivos** (`[patch] hook en +0x…` x12 al arrancar, del `4423C8` al
   `7ED3B0`): queda descartado "build viejo". Y aun así, **ni una `ENTRA IDevice::run` /
   `RenderFX` / `endScene` en toda la corrida**, con el `DisplayFrame` final entrado hace
   13+ s. `DisplayFrame` (verificado lineal hasta `run` en el 021) no llega a `run` en
   microsegundos ni vuelve a entrar: **retornó por un early-exit de flags** (`ab0==0` /
   `ab9!=0`, el limitador de 100 ms no aguanta 13 s) y el giro está aguas abajo, en código
   sin instrumentar (epílogo del ctor → `DoStateChange` → `Loading::Stop` →
   `ResumeAllSounds` / máquina de estados). La conclusión del 018 ("giro DENTRO del
   `DisplayFrame` final") queda corregida: es DESPUÉS de su retorno.
2. **El `+9k gettod/s` no es el hilo principal sondeando el Timer.** El sitio de reloj
   dice `ra0 = libasphalt6.so+0x85B0A8`, y el disasm muestra que ahí no hay ningún wrapper
   de reloj: es el `gettimeofday` que `CCondition::wait` (`0x85B068`) llama para calcular el
   `abstime` antes del `timedwait`. Verificado de paso: `0x857DA0` = `Timer::getRealTime`
   (lineal), `0x857DE0` = `getMicroSeconds` (lineal), `0x3c538c` = PLT de `gettimeofday`.
   O sea, el flood de reloj son queries de `cond_timedwait` — ruido de worker (el anillo
   del principal no tiene ni un `cond_wait` fresco del principal). El giro del principal
   no toca NADA instrumentado: ni reloj-Timer, ni mutex, ni malloc, ni hooks.
3. **Mapa de los `DisplayFrame` del ctor** (barrido de `bl 0x4A4128` en C1/C2): C2 tiene 4
   (`0x441D18`, `0x4421BC`, `0x442520`, `0x4429C4`; C1 comparte cola). Los dos intermedios
   van seguidos de `movw/movt 'lght'` + `bl RemoveChildNodeType`; los dos últimos van
   seguidos del epílogo (`pop {…,pc}`) = `DisplayFrame` final + retorno del ctor.

**Instrumentación agregada para el 023** (build Debug verificado, `asphalt6.vpk`
regenerado): hook ENTER en `getRealTime` (`0x857DA0`, verbatim) — dice si el principal
sondea el Timer; hooks `AfterDF` tras el `bl` final en los dos caminos del ctor
(`0x4421C0`/`0x4429C8`, emulación con doble indirección como `hook_anim`, r3 scratch tras
retorno void) — si disparan, `DisplayFrame` retornó y el giro es `Stop`/audio/estados;
`bc_clock_site` ahora guarda también el tid (el contador es global y mezclaba hilos) y el
volcado imprime `sitio de reloj: tid=… consulta desde libasphalt6.so+0x…`.

**Regresión previa incluida en este build** (sin commitear, del análisis del dump 020):
`hook_run` usa r2 (no r12/ip) como scratch del salto final — con r12 se pisaba el `ip`
forzado y el `beq` de cola vacía nunca se tomaba.

**Cómo leer el 023:** `ENTRA AfterDF` fresca ⇒ retorno confirmado, a por `Stop`/
`ResumeAllSounds`; `ENTRA getRealTime` fresca del principal ⇒ sondea el Timer (el ra dice
qué bucle); ninguna de las dos + `sitio de reloj` de un worker ⇒ el principal gira en un
flag volátil/atómico sin syscalls y el paso siguiente es hookear `Loading::Stop`.

**Pendiente:** desplegar `eboot.bin`, correr, bajar `asphalt6_023.log`.

### Bug #019 — crash en el epílogo del ctor de `MenuScene`: drop de `std::string` con data NULL — 2026-09-10

**Log:** `logs/asphalt6_023.log` (termina abrupto en `link prog=33`, sin volcado del
testigo: crash, no cuelgue).
**Dump:** `logs/asphalt6-psp2core-1789012910-0x0002d7282f-eboot.bin.psp2dmp` (+
`.analysis.txt` / `.triage_summary.md` generados con `psvita-toolkit analyze --so-base
0x98000000`).

**Síntoma:** data abort en el hilo principal con PC en `libasphalt6.so + 0xA6C39C`
(`__exchange_and_add + 0x44`: `ldr r5,[r6]` con `r6 = 0xFFFFFFFC` (-4), `r8 = -1` = drop).

**Causa raíz (disasm + pila, sin adivinar).** La pila del dump trae la cadena completa:
`hook_dfret2` → retorno a `MenuScene::MenuScene + 0x7D8` (= `0x442BA0`). Ahí hay
(`0x442B94`):

```
add r0, r4, #8   ; r0 = &refcount (Rep+8)
mvn r1, #0       ; -1 = drop
bl __exchange_and_add   ; ← crash, r0 = -4
0x442BA0: cmp r0, #0    ; = la dirección de retorno de la pila, match exacto
```

Es el cleanup gnustl de un `basic_string` de pila del ctor (`Rep = data-12`, refcount en
`Rep+8`, `GlitchFree(Rep)` si llega a 0 — mismo idioma en el pseudo-C del tail de C2).
`&refcount = -4` ⇒ `Rep = -12` ⇒ **`data = NULL`**. La pila además trae `r4 = 0xFFFFFFF4`
(-12) en `0x81540ce0**, cerrando la aritmética. El chequeo previo (`data-12 !=
*sentinela`) no filtra NULL (`-12 != sentinela` → entra al drop igual).

**Descartado que lo cause `hook_dfret2`:** el hook preserva todo (push/pop balanceado,
r3/r4 recargados con los mismos valores que el código original, `sp` intacto) y el NULL
vive en `[sp,#0xB4]`, slot escrito antes del `DisplayFrame`. El hook solo observó: de paso
confirma lo predicho en el 022 — **`DisplayFrame` SÍ retornó esta corrida** (el `AfterDF`
disparó) y el flujo llegó al epílogo del ctor. Esta corrida pasó el punto donde 021/022 se
colgaban: la rama es no-determinista (heap/estado por corrida).

**Fix aplicado** (`source/patch.c`, filosofía del Bug #012: guarda en runtime, sin tocar
el `.so`): hooks `StrDrop` en los dos caminos del epílogo (`0x442B94` y su gemelo
`0x44238C`, mismo idioma verificado en disasm). Emulan `add`+`mvn`, pero si
`data (= r0+4) < 4 KB` saltan al camino "nada que liberar" (`0x4429E0`/`0x4421D8`: `mov
r0,r6` + epílogo, que no usa ningún registro que el stub toque) avisando en vivo
(`[patch] StrDrop: data=…`). Sin falsos positivos: el heap real vive en `0x81xxxxxx`+.
Verificación de dos palabras en `hook_trace` como siempre. Build verificado con
`psvita-toolkit build --preset debug` (limpio, `asphalt6.vpk` regenerado).

**Cómo leer el 024:** línea `[patch] StrDrop` ⇒ la guarda mordió (y el juego sigue: puede
reaparecer el cuelgue del Bug #015 aguas abajo — `Stop`/audio/estados — o avanzar al
menú). Sin esa línea + crash nuevo ⇒ triagear el dump nuevo.

**Pendiente:** desplegar `eboot.bin`, correr, bajar `asphalt6_024.log` (+ `.psp2dmp` si
hay).

### Bug #019 — REGRESIÓN: el hook `AfterDF` emula `ldr r4,[sp,#0xb4]` con el inmediato transpuesto — 2026-09-10

**Log:** `logs/asphalt6_023.log` (crash, no cuelgue: corta en `link prog=33` sin volcado del
testigo). **Dump:** `logs/asphalt6-psp2core-1789012910-0x0002d7282f-eboot.bin.psp2dmp`.

**Lo que el 023 sí confirma antes del crash:** los **15** hooks se instalan
(`[patch] hook en +0x…`, incluidos los tres nuevos `857DA0`/`4421C0`/`4429C8`).

**Síntoma:** data abort con `PC = 0x98a6c39c` → base real `0x98000000` → offset `0xA6C39C` =
`__gnu_cxx::__exchange_and_add + 0x44`, que es literalmente `ldr r5, [r6]` con
**`R6 = 0xFFFFFFFC`**. `LR` cae en nuestro propio `bc_push` (residuo de la instrumentación:
el `.so` llama a `pthread_mutex_lock` → wrapper → `bc_enter` → `bc_push`, y `__exchange_and_add`
hace sus atómicos por mutex — el `0xA6C394` que aparecía como dirección de retorno en el anillo
del principal en TODOS los logs anteriores es exactamente ese `bl pthread_mutex_lock`).

**Causa raíz (aritmética exacta, no hipótesis):** el stub `hook_dfret1`/`hook_dfret2` emula la
segunda de las dos palabras pisadas como `.word 0xe59d4b40`, pero la instrucción real del `.so`
es `e59d40b4` — **inmediato transpuesto**: `ldr r4,[sp,#0xb40]` (2880) en vez de
`ldr r4,[sp,#0xb4]` (180). El marco del ctor mide 196 bytes, así que r4 cargó basura (`0`) de
2.7 KB más arriba de la pila. Después el código original del ctor sigue tal cual:
`sub r4,r4,#12` → `0xFFFFFFF4`, el `cmp` contra `&_S_empty_rep_storage` no coincide, toma el
`bne` y llama `__exchange_and_add(r4 + 8, -1)` = **`0xFFFFFFFC`** → data abort. Es el
`std::string` que el ctor libera justo después del `DisplayFrame` final (pseudo-C
`out_ghidra.c:58601-58610`), o sea el hook se comió su propio sitio.

**Fix aplicado (dos partes):**

1. `source/patch.c`: `.word 0xe59d40b4` en los dos stubs `AfterDF`.
2. **Blindaje de la clase entera de bug:** `hook_trace()` ahora verifica también la **segunda**
   palabra del objetivo (`expect2`) antes de instalar, con la palabra real declarada al lado de
   cada hook (bloque `W2_*`, auditado con `objdump` para los 15 sitios). Antes solo miraba la
   primera, y los 8 bytes pisados son DOS instrucciones: la segunda se escribe a mano en el stub
   y esa mano ya falló dos veces, las dos con crash en consola en vez de un mensaje (Bug #017 =
   `ldr` PC-relativo sin doble indirección; este = inmediato transpuesto). Ahora un encoding mal
   escrito o un `.so` distinto se cazan al arrancar y ese hook no se instala.
3. El hook de `getRealTime` sale de `so_patch()` (el stub queda definido): el 022 ya atribuyó el
   flood de reloj al `cond_timedwait` de un worker de vox, y como empuja una miga por llamada
   inundaría el anillo de 32 del principal, borrando el contexto `DisplayFrame`/`CLightSceneNode`/
   `AfterDF` que hay que leer.

Build Debug verificado (limpio, sin warnings de no-usado) y `eboot.bin` desplegado.

**Cómo leer el 024** (la pregunta del 022 sigue abierta): `ENTRA AfterDF` fresca ⇒ el
`DisplayFrame` final retornó y el giro es aguas abajo (epílogo del ctor → `DoStateChange` →
`Loading::Stop` → `ResumeAllSounds`); sin `AfterDF` y con el `DisplayFrame` entrado hace
segundos ⇒ el giro está dentro de `DisplayFrame` después de todo, y el siguiente paso es hookear
sus early-exits.

**Pista adicional que dejó el 021 y que conviene tener a mano** (leída del pseudo-C, sin
confirmar en consola todavía): `GS_MenuMain::OnLoad3DScene` tiene una búsqueda lineal **sin
cota** justo después de `SortCars()` — `while (*p != carId) p++;` sobre el array de
`GetCarCount()` autos (`out_ghidra.c` ~22255). Si el auto por defecto no está en ese array
(y `BaseCarManager::GetPackFile` está parcheado a "no encontrado" para TODOS los autos desde el
Bug #005), ese bucle recorre memoria para siempre: puros loads y un `cmp`, sin malloc, sin
strcmp, sin syscalls — que es EXACTAMENTE la firma del giro del principal (el flood de reloj es
de un worker, no suyo). Es el sospechoso #1 si el 024 confirma que el giro es aguas abajo del
ctor. La última actividad de memoria del anillo caliente del 021 encaja: `_M_insert_aux` de un
`std::vector<int>` creciendo (= `SortCars`) y nada después.

### Bug #020 — CAUSA RAÍZ del cuelgue del menú: búsqueda lineal SIN COTA en `GS_MenuMain::OnLoad3DScene` — 2026-09-10

**Log:** `logs/asphalt6_024.log` (1132 líneas, Debug). **Sin crash y sin `.psp2dmp`**: el fix del
encoding de `AfterDF` (Bug #019) aguantó y la guarda `StrDrop` ni tuvo que morder (no aparece
la línea). El juego llega al mismo muro: 66 shaders / 33 programas, **13 frames**, último hito
`swap #12`, y de ahí 47 s sin avanzar.

**Lo que el 024 cierra (era la pregunta abierta del 022):** el anillo del principal termina en
`createAnimator` → `CLightSceneNode` → `ENTRA DisplayFrame` → **`ENTRA AfterDF`** y nada más
durante 15,9 s. O sea: **el `DisplayFrame` final del ctor de `MenuScene` retornó** y el giro
está aguas abajo, en código sin instrumentar. Queda confirmada la corrección que el 022 ya
había anticipado sobre la conclusión del 018.

**Causa raíz (disasm, no hipótesis).** `GS_MenuMain::OnLoad3DScene` (`_ZN11GS_MenuMain13OnLoad3DSceneEv`,
`0x3EED98`, 1780 bytes, sacado del `.dynsym` real) tiene esto justo después de `SortCars()`:

```
3ef004  bl Game::GetCarMgr
3ef008  bl BaseCarManager::GetCarCount
3ef00c  lsl r0, r0, #2
3ef010  bl operator new[]          ; <-- SIN inicializar
3ef014  str r0, [r5, #0x44]        ; this->carArray = array
3ef018  mov r0, r5
3ef01c  bl GS_MenuMain::SortCars   ; 0x3EFDD8
...
3ef04c  ldr r3, [r0, #0x20]        ; profile->carId
3ef050  cmn r3, #1
3ef054  beq 0x3ef364               ; == -1 -> al bloque de abajo
...
3ef364  ldr r2, [r5, #0x3c]        ; this->raceCar
3ef368  cmp r2, #0
3ef36c  beq 0x3ef058
3ef370  ldr r3, [r5, #0x44]        ; array
3ef374  ldr r1, [r2, #0x44]        ; aguja = raceCar->carIdx
3ef378  ldr r2, [r3]
3ef37c  cmp r2, r1
3ef380  beq 0x3ef390
3ef384  ldr r2, [r3, #4]!          ; <-- BUCLE
3ef388  cmp r2, r1
3ef38c  bne 0x3ef384               ; <-- SIN COTA
3ef390  mov r3, #0
3ef394  str r3, [r5, #0x48]
3ef398  b 0x3ef058
```

**La firma calza byte por byte con lo observado:** el bucle son puros `ldr` + `cmp`, sin
malloc, sin `strcmp`/`strstr`, sin mutex, sin syscalls y sin ninguna de las 15 funciones
hookeadas — que es exactamente lo que el testigo reporta latido tras latido
(`+0 reservas +0 strstr +0 strcmp`, principal `CORRIENDO` al 100 %, `cpu=+38,6 s` en 40 s de
pared). Por eso ningún hook lo vio nunca. El `+~9k gettod` es ruido del `cond_timedwait` de un
worker de vox, ya atribuido en el 022 (`sitio de reloj: tid=0x4001021F`, que no es el principal).

**Por qué el array no contiene la aguja.** `SortCars()` (`0x3EFDD8`) llena `array[0..n-1]` con
`EventManager::GetUnlockList()` — la lista de autos **desbloqueados**. Con perfil nuevo esa
lista viene vacía, el `operator new[]` no inicializa nada, y la búsqueda recorre el heap para
siempre. Peor: `SortCars` ya había hecho `profile->carId = GetCarInfo(array[0], 0)` sobre esa
misma basura, y si eso devuelve -1 el perfil sigue en -1 — que es justo la condición
(`0x3ef054`) para entrar al bloque del bucle.

**El bucle es código muerto del build original de Gameloft:** el puntero que calcula (`r3`) se
**descarta** en `0x3EF390` (`mov r3,#0`), y `r1`/`r2` están muertos después (`0x3EF058` los
reasigna). El único efecto de todo el bloque es `this->0x48 = 0`. Escribieron la búsqueda del
índice y después hardcodearon 0.

**Fix aplicado** (`source/patch.c`, filosofía del Bug #012: guarda en runtime, sin tocar el
`.so`). Dos guardas independientes, ambas demostrablemente equivalentes al original:

1. **`CarSeed`** (`0x3EF014`, emula `str r0,[r5,#0x44]` + `mov r0,r5`): siembra
   `array[0] = raceCar->carIdx` **antes** de `SortCars`. Efecto doble: (a) `SortCars` calcula
   `profile->carId = GetCarInfo(<auto por defecto>)` — un id válido en lugar de basura, que es
   exactamente lo que corresponde a un perfil nuevo; (b) con `profile->carId != -1`, el
   `beq 0x3ef364` **no se toma** y el bloque del bucle ni se ejecuta. Solo escribe si hay
   RaceCar (`this->0x3c != 0`), y eso **garantiza** que el array mide ≥ 4 bytes: el RaceCar
   solo se construye si `GetCarIdxFromId()` dio un índice válido (`0x3EEEB4`: `cmn r8,#1` /
   `beq`), o sea `GetCarCount() >= 1`. Si la lista de desbloqueos no estaba vacía, `SortCars`
   pisa `array[0]` igual y la siembra es inocua.
2. **`CarFind`** (`0x3EF378`, no reanuda: salta a `0x3EF390`): red de seguridad por si (1) no
   alcanza. Es equivalencia exacta, no heurística — ver arriba por qué el resultado del bucle
   se descarta.

Ambas con verificación de las dos palabras en `hook_trace` (`W_STR_R0_44`/`W2_MOV_R0R5` y
`W_LDR_R2R3`/`W2_CMP_R2R1`, leídas del `.so` desplegado) y aviso en vivo al log. Build Debug
verificado limpio; los stubs generados se auditaron con capstone sobre `build/asphalt6.elf`
(`r12` scratch confirmado: llega de tres `bl` seguidos; flags muertos hasta el `bl SortCars`).

**Nota sobre el paralelo con Asphalt-5-Vita:** el muro de A5 en este punto era otro
(`GS_TrailerMovie` esperando un flag que en Android limpia una `Activity` — Bug #4 de A5 — y
después `CMatrix::Mult` con el "short vector" de VFPv2 que el Cortex-A9 no implementa — Bug #6
de A5). Ninguno de los dos aplica acá: A6 ya renderiza 13 frames y su carga de assets no pasa
por el `GLResLoader` de JNI que A5 reimplementó, sino por `CFileSystem` + el mapa de
ofuscación. Lo que sí se trajo de A5 es el método: acotar con el log real y poner la guarda en
runtime en vez de tocar el `.so`.

**Cómo leer el 025:**
- `[patch] CarSeed: array[0]=…` ⇒ la siembra corrió. Si además **no** aparece `CarFind`, el
  bloque del bucle se saltó solo (camino previsto) y el juego debería pasar al menú.
- `[patch] CarFind: …` ⇒ la siembra no alcanzó (`GetCarInfo` devolvió -1 igual): el bucle se
  omitió por la red de seguridad y hay que revisar por qué el perfil sigue en -1.
- Ninguna de las dos + mismo cuelgue ⇒ el giro no es este bucle; siguiente sospechoso es el
  epílogo del ctor (`DoStateChange` → `Loading::Stop` → `ResumeAllSounds`).
- Crash nuevo aguas abajo ⇒ probablemente el efecto de tener `BaseCarManager::GetPackFile`
  parcheado a "no encontrado" para TODOS los autos (Bug #005) ahora que sí se pide un auto.
  Ese parche binario es el siguiente candidato a revertir: el Bug #012 demostró que los assets
  **no** faltaban (`Audi_RS3_2010.car` es uno de los `fileNNNNNN.dat` con la firma alterada).

**Pendiente:** desplegar `eboot.bin`, correr, bajar `asphalt6_025.log`.

### Bug #020 — RESULTADO del log 025: el bucle no era el culpable, pero las guardas acotaron el giro

**Log:** `logs/asphalt6_025.log` (1535 líneas, Debug). Los 17 hooks se instalan (`+0x3EF014` y
`+0x3EF378` incluidos). Mismo muro: 13 frames, `swap #12`, 87 s sin avanzar.

**Lo que aportó:** el anillo del principal ahora termina en
`DisplayFrame → AfterDF → StrDrop → pthread_mutex_lock → **ENTRA CarSeed**` y nada más durante
44 s. O sea `OnLoad3DScene` **sí** se entra, la asignación del array pasa, y el giro está entre
`0x3EF014` y el siguiente deref. `CarFind` no disparó (coherente: sin RaceCar, el `cmp r2,#0`
de `0x3EF368` ya salteaba el bloque). El `while` sin cota era real pero **no** era este cuelgue.

**Corrección sobre el Bug #020:** la hipótesis era correcta en la mecánica (búsqueda lineal sin
cota sobre datos de auto vacíos) pero apuntaba al bucle equivocado. El bucle real está una
llamada más adentro. Las dos guardas se dejan puestas: son equivalencias exactas, sin costo, y
`CarSeed` es ahora la miga que acotó el giro.

### Bug #021 — CAUSA RAÍZ del cuelgue del menú: `std::sort` con un comparador que no es strict weak ordering — 2026-09-10

**Cadena completa, cerrada con disasm (sin adivinar).**

`SortCars` → `EventManager::GetUnlockList` (`0x498110`) ordena la lista de desbloqueos con
`std::sort` (`0x49AD44` = `__introsort_loop`, `0x49AEDC` = `__final_insertion_sort`). El
comparador es `SceneHelper::CompareStars(int,int)` (`0x462A84`):

```
00462a90..00462ad0  a -> GetCarIdxFromId -> GetCarInfo(idx, 0x39)   ; estrellas
00462ad4  cmp   r4, r0
00462ad8  movgt r0, #0      ; a >  b -> false
00462adc  movle r0, #1      ; a <= b -> TRUE     <-- devuelve true en IGUALES
```

`comp(x,x) == true` **no es un strict weak ordering**. El `__unguarded_linear_insert` de
libstdc++ no lleva chequeo de límite — confía en que algún elemento corte
`while (comp(val, *(i-1))) --i;`. Con `comp` dando true en iguales y **todos** los elementos
iguales, se sale del array por delante y recorre el heap para siempre: puros loads más una
llamada hoja por vuelta. **Sin malloc, sin strcmp, sin strstr, sin syscalls, principal al
100 % de CPU** — la firma exacta que el testigo venía reportando desde el log 015.

**Por qué todos iguales — y acá se cierra el círculo con el Bug #005.**
`BaseCarManager::InitCarMng` (`0x48D3DC`) fija el conteo de autos hardcodeado (`0x48D41C`:
43 o 9, nunca 0) y después, por cada auto, llama **dos veces** a `GetPackFile`
(`0x48D658` y `0x48D6F8`) para leer sus datos. Con el parche binario del Bug #005
(`GetPackFile` → `mov r0,#0; bx lr`) las dos devolvían NULL, el `subs r4,r0,#0 / beq` salteaba
la lectura, y **los 43 autos quedaban con todos los campos en cero** — incluido el `0x39`
(estrellas) que lee el comparador. El bug de Gameloft es **latente** en Android (datos reales,
estrellas distintas, siempre hay un elemento que corta); nuestro parche lo convirtió en cuelgue
garantizado. También explica `raceCar == NULL`: con todos los ids en 0,
`GetCarIdxFromId(m_defaultCarID)` devuelve -1 y el RaceCar del menú nunca se construye.

**Fix aplicado — tres piezas, en orden causal:**

1. **`Stars`** (`0x462AD4`, hook que no reanuda: salta al epílogo `0x462AE0`): `cmp` +
   `movlt r0,#1` / `movge r0,#0`. Convierte `<=` en `<`: mismo orden para elementos distintos,
   y ahora sí es un strict weak ordering, así que el sort termina con **cualquier** dato. Sin
   `bc_enter` a propósito (el sort lo llama O(n log n) veces e inundaría el anillo de 32).
2. **`PackFile`** (`0x48DA84`) + **reversión del parche binario del Bug #005**. Se restauraron
   los 8 bytes originales de `GetPackFile` en `libasphalt6.so` (`0x48D9D8`,
   `f0412de9007052e2`) en las dos copias, y se sacó la entrada del
   `libasphalt6.so.binary_patches.json`. El deref sin chequeo que crasheaba en el #005
   (`0x48DA90: ldr r3,[r4]` con `r4 = createAndOpenFile() = NULL`) queda cubierto por una
   guarda en runtime que salta al camino de "no encontrado" que la propia función ya tiene
   (`0x48DB84`: `mov r4,#0` + epílogo propio). **Estrictamente mejor que el parche binario:**
   los autos cuyo pack sí resuelve ahora cargan de verdad, en vez de deshabilitarlos todos.
   El parche de `autoStartGame` (`0x3E4600`, Bug #006) se dejó intacto.
3. **`MenuCar`** (`0x3EF020`): red de seguridad aguas abajo. Si aun así no hay auto por
   defecto, `this->raceCar` queda NULL y `0x3EF028: ldr r3,[r3,#0x28]` aborta. El bloque solo
   hace `raceCar->node->setName("SelectableMenuCar")`, así que con NULL se saltea entero a
   `0x3EF040`. Emula un `ldr` PC-relativo con doble indirección (Bug #017).

`CarSeed` ahora loguea **siempre** (`raceCar=` y `array=`), que es lo que al 025 le faltó para
distinguir "array NULL" de "sin RaceCar". Build Debug limpio; los 4 stubs auditados con
capstone sobre `build/asphalt6.elf`; las 6 palabras de los sitios nuevos verificadas contra el
`.so` ya revertido en las dos copias.

**IMPORTANTE para desplegar:** esta vez hay que subir **dos** archivos, porque el `.so` cambió:
- `build/eboot.bin` → `ux0:/app/ASPHALT06/eboot.bin`
- `ux0_data/asphalt6/lib/armeabi-v7a/libasphalt6.so` → `ux0:/data/asphalt6/lib/armeabi-v7a/`

El `deploy --eboot` del toolkit **no** sube datos de juego (mismo tropiezo que el Bug #005).

**Cómo leer el 026:**
- `[patch] PackFileNull: …` ⇒ ese pack no resolvió; si aparecen ~86 líneas, ningún auto carga y
  el problema de datos sigue (candidato: los 92 `fileNNNNNN.dat` faltantes, 865–956).
- **Sin** `PackFileNull` ⇒ los autos cargan de verdad por primera vez.
- `[patch] CarSeed: raceCar=0x0…` ⇒ sigue sin auto por defecto; `MenuCarNull` debería aparecer
  justo después y el juego seguir igual (sin el auto 3D del menú, pero sin colgarse ni abortar).
- `[patch] CarSeed: raceCar=0x8…` ⇒ hay auto: el camino completo del menú está vivo.
- Si el giro persiste con la misma firma pese al fix de `Stars`, el siguiente sospechoso es el
  otro `std::sort` del mismo archivo (`GS_MenuMain` ~`0x3FFEB4`) o `TrackScene::SortCarsByCollectedItems`
  (`0x47268C`), que puede tener el mismo idioma de comparador.

**Pendiente:** subir los dos archivos, correr, bajar `asphalt6_026.log`.

### Bug #022 — `abort()` por excepción C++ sin capturar tras "First time launch" — 2026-09-10

**Log:** `logs/asphalt6_026.log` — confirma que el fix del Bug #021 funcionó de punta a
punta: **cero** `PackFileNull`, `CarSeed`/`MenuCarNull` no aparecen (hay auto por defecto real),
y el testigo reporta **frames presentándose** (`[wd] 23 frames (+2 en 5s) | ... | último hito:
swap #22`) — el giro del menú (Bugs #015-#021) está resuelto. El juego sigue de largo,
carga ~200 `.wav`/`.vxn` de audio (todos `fopen(...): 0x0` porque `vox::DriverAndroid` los pide
por nombre relativo sin `ux0:data/asphalt6/data/`, sin implementación de audio — no fatal,
el motor tolera la ausencia), llega a:
```
[INFO] [ALOG][XXX] First time launch the app
[DEBUG] [ALOG][HDVD] EventTracking: Adding Event with ID 14475
[DEBUG] fopen(ux0:data/asphalt6/data/timespent.dat, w): 0x817707f0
...
[ERROR] [ALOG][XXX] Launch game by PN: 0
[DEBUG] [ALOG][HDVD] EventTracking: Adding Event with ID 14489
[DEBUG] stat(pn.dat): -1
[DEBUG] fopen(ux0:data/asphalt6/data/pn.dat, w): 0x817707f0
...
[DEBUG] fopen(ux0:data/asphalt6/data/pn.dat, wb): 0x817707f0
...
[ERROR] [FalsoJNI] [WARN]...[methodVoidCall] method ID 0 not found!
[FATAL] Abort called from address 0x98a71e70
```

**Dump:** `logs/asphalt6-psp2core-1789020077-0x0000962489-eboot.bin.psp2dmp` — analizado con
`psvita-toolkit analyze --so-base 0x98000000`. El hilo en crash es el **principal**
(`ASPHALT06`), razón de parada "Undefined instruction" con `PC` dentro de `_kill_r` del propio
loader — **no es el bug real**: es el mecanismo con el que `so_util`/vitasdk generan el
`.psp2dmp` al recibir el `SIGABRT` de un `abort()` real (`raise` → `kill` → trampa
intencional), confirmado porque la pila, justo antes, tiene (en orden):

```
abort (asphalt6+0x9be41)
exit_soloader (source/reimpl/sys.c:161)          <- nuestro hook de abort()
__gnu_cxx::__verbose_terminate_handler()+0x48    <- libasphalt6.so+0xa71e70
__cxxabiv1::__terminate(void(*)())+0xc
std::terminate()+0x18
__cxa_rethrow+0x50
__gnu_cxx::__verbose_terminate_handler()+0xf8
__cxxabiv1::__terminate(void(*)())+0x28
__cxxabiv1::__pbase_type_info::~__pbase_type_info()+0x34
```

O sea: **una excepción C++ real quedó sin capturar** en algún punto del código nativo del
motor (hay un `throw;` explícito que la propaga, ve `__cxa_rethrow`), lo que dispara
`std::terminate()` → el terminate handler por defecto → `abort()`. **No es un NULL deref**:
ninguno de los parches de "saltar al camino feliz" de los Bugs #005-#021 aplica acá — hace
falta saber CUÁL excepción y desde dónde, no adivinar otro salto binario.

**Por qué no se puede leer el tipo/mensaje de la excepción todavía:** el mensaje que
`__gnu_cxx::__verbose_terminate_handler()` imprime normalmente (`terminate called after
throwing an instance of '%s'` / `  what():  %s`) sale por `fprintf(stderr, ...)` /
`write(2, ...)` — un camino que este loader **no** redirige a `[ALOG]` (a diferencia de
`__android_log_print`, que sí pasa por nuestro logger). Se perdió sin dejar rastro en el log.

**`__cxa_rethrow`/`__cxa_throw`/`__cxa_begin_catch`/`__cxa_end_catch` están definidos DENTRO
de `libasphalt6.so`** (confirmado con `objdump -T`: `DF .text`, no `UND`) — o sea, el motor
trae su propio libstdc++/libsupc++ estático, no los importa de nuestro `dynlib.c`. Se buscaron
los call-sites de `bl __cxa_rethrow` con `arm-vita-eabi-objdump -d` (sin `-M force-thumb`: esta
zona es ARM real, igual que el área de `basename@plt` ya documentada) — aparecen **100+**
sitios (patrón `catch(...) { ...; throw; }`, común en código C++ genérico/RAII), demasiados
para triangular a mano cuál corresponde a esta corrida sin más información.

**Fix aplicado — diagnóstico, NO el fix final** (`source/patch.c`, Bug #022): en vez de
adivinar con otro parche binario, se hookeó la entrada de `__cxa_throw` (offset `0xA6FEBC`,
`ldr ip,[pc,#144]` + `push {r4,r5,r6,r7,fp,lr}`) con el mismo mecanismo ENTER-only de
`hook_trace()`/`hook_addr()` que los Bugs #015-#021 — **una sola función real** por la que
pasa TODO `throw` del binario (confirmado: no importada, así que cualquier excepción, sea
cual sea el catch/rethrow en el que termine, se origina ahí). El hook llama a
`cxa_throw_log()`, que loguea `tinfo->name()` (offset `+4` del `std::type_info`, layout real
de Itanium C++ ABI — vtable en `+0`, `const char* __name` en `+4`) mangled, y reanuda la
función sin tocarle ningún comportamiento (doble indirección para el `ldr ip,[pc,#144]`
inicial, igual que `createAnimator`/`DisplayFrame`/`CLightSceneNode`; `r3` confirmado como
scratch libre por disasm — nada antes de su primera reasignación en la función real lee el
`r3` previo al hook). Build verificado con `psvita-toolkit build` (compila y linkea limpio,
`asphalt6.vpk` regenerado) — el deploy no se pudo completar esta sesión porque la consola no
tenía el FTP de VitaShell activo (`Connection refused` en `192.168.3.15:1337`).

**Pendiente:** abrir VitaShell en la consola (SELECT para activar FTP), correr
`psvita-toolkit deploy --eboot`, jugar hasta el mismo punto ("First time launch the app") y
bajar el log nuevo. La línea `[patch] __cxa_throw: tinfo=... type='...'` que aparezca justo
antes del `[FATAL] Abort` va a decir la clase C++ real lanzada (mangled) — con eso, buscar el
`throw` correspondiente en el pseudo-C de Ghidra (grep por el nombre demangleado o por
`__cxa_throw` cerca de la lógica de "first time launch"/`pn.dat`/tracking) para el fix real
en la próxima sesión.

**Actualización — log 027, el diagnóstico funcionó:** mismo punto exacto (justo después de
guardar `pn.dat`), mismo `abort()`. La línea nueva confirma:
```
[ERROR] [FalsoJNI] [WARN]...[methodVoidCall] method ID 0 not found!
[ERROR] [patch] __cxa_throw: tinfo=0x98bdd380 type='St11logic_error' (Bug #022, diagnostico)
[FATAL] Abort called from address 0x98a71e70
```
`tinfo=0x98bdd380` resuelve exacto contra `_ZTISt11logic_error` (`nm`) — es un
**`std::logic_error` plano**, no una subclase (`out_of_range`/`length_error`/
`invalid_argument` tienen su propio `type_info`, en otra dirección). Se buscaron los
call-sites de `bl __cxa_throw` (`arm-vita-eabi-objdump -d`, sin `-M force-thumb`: zona ARM
real) → **49** en total, todos dentro del rango `0xa4xxxx`-`0xa70xxx` (la porción estática de
libstdc++/libsupc++, no código de juego disperso). El que corresponde a `logic_error` es
`0xa44eac`, dentro de `_ZSt19__throw_logic_errorPKc` (`0xa44e4c`-`0xa44f18`, confirmado por
rango entre símbolos consecutivos en `nm`). Pero **ese helper lo llaman 57 sitios distintos**
en TODO el binario (buscado con `grep bl.*__throw_logic_error` sobre el disasm completo) —
patrón consistente con el chequeo de NULL que `basic_string(const char*)` hace inline en cada
sitio donde se construye un `std::string` desde un `char*` (mensaje típico de libstdc++:
`"basic_string::_M_construct null not valid"`) — demasiados sitios para auditar a mano sin
más información.

**Extensión aplicada al mismo hook** (`source/patch.c`, todavía Bug #022, sigue siendo
diagnóstico): además del `type_info`, ahora también lee el **mensaje** de la excepción ya
construida. Cuando `__cxa_throw` es llamado, el objeto ya está construido (el compilador hace
`__cxa_allocate_exception` + placement-new + `__cxa_throw`), así que `r0` (primer arg de
`__cxa_throw`) apunta a un `std::logic_error` real: `{ vtable_ptr; __cow_string _M_msg; }`
(`__cow_string` es el string COW liviano que libstdc++ usa SIEMPRE para el mensaje de las
excepciones estándar, sin importar `_GLIBCXX_USE_CXX11_ABI`) — mismo truco de offset `+4` que
ya usábamos para `type_info->__name`, ahora sobre `exc_obj` en vez de `tinfo`. `r0`/`r1` ya
llegan en el orden correcto para pasarlos directo a `cxa_throw_log(exc_obj, tinfo)` por AAPCS,
sin mover nada. Build verificado (`psvita-toolkit build`, compila y linkea limpio) y
**desplegado** (`psvita-toolkit deploy --eboot`, FTP ya activo esta vez).

**Pendiente:** correr de nuevo hasta el mismo punto y bajar el log. La línea
`[patch] __cxa_throw: type='St11logic_error' msg='...'` va a decir el mensaje literal — si es
`"basic_string::_M_construct null not valid"` (lo más probable dado el patrón de 57 sitios),
el siguiente paso es cazar CUÁL de los 57 sitios corre en este flujo exacto (breadcrumb/hook
adicional sobre el caller inmediato, `LR` en el momento del throw) para poder arreglar el NULL
en el origen real (probablemente un `jstring`/`char*` que nuestro `java.c` devuelve `NULL` y
que el motor envuelve en un `std::string` sin chequear, en algún callback de "first time
launch"/tracking/PN que todavía no está registrado).

**Actualización — log 028, mensaje real distinto al adivinado:** mismo punto exacto
(`ux0:data/asphalt6/data/pn.dat` recién escrito). El mensaje real es
`msg='basic_string::_S_construct NULL not valid'` (no `_M_construct` — el hook del mensaje
funcionó igual, el nombre exacto del assert cambia el símbolo a buscar):
```
[ERROR] [FalsoJNI] [WARN]...[methodVoidCall] method ID 0 not found!
[ERROR] [patch] __cxa_throw: type='St11logic_error' msg='basic_string::_S_construct NULL not valid' (Bug #022, diagnostico)
[FATAL] Abort called from address 0x98a71e70
```
Con el mensaje exacto se ubicó `std::string::_S_construct<char const*>` (`0xA6A660`, símbolo real
vía `nm -CD`) — el choke point compartido que tira ese assert (confirmado por disasm:
`cmp r0,#0` (beg) `; beq ...; cmp r5,#0` (end) `; bl __throw_logic_error` en
`0xa6a680`-`0xa6a6fc`). Pero por sí solo sigue siendo un choke point de decenas de sitios (4
wrappers de `std::string` puro le llaman: el ctor `(const char*, allocator)` en `0xA6A7E8`/
`0xA6A828`, `append(const char*)` en `0xA6AE20`, `assign(const char*)` en `0xA6BAEC`,
`operator=(const char*)` en `0xA6BB54` — **ojo**, no confundir con
`glitch::core::SAllocator`, que tiene su propio `_S_construct` en `0x475F08` y no aplica acá).

Descartadas 3 de las 4 por firma de crash incompatible: `append`/`assign`/`operator=` hacen
`bl strlen@plt` con el `char*` de entrada **sin chequeo de NULL antes** — si el motor les
pasara NULL, el crash sería un data abort dentro de `strlen` (SIGSEGV), no un
`std::logic_error` vía `__cxa_throw`. Solo el ctor `(const char*, allocator)` tiene el camino
especial: `subs r5,r1,#0; ...; mvneq r1,#0` — cuando el `char*` de entrada es NULL, fuerza
`end=-1` (centinela) ANTES de llamar a `_S_construct`, produciendo EXACTAMENTE la combinación
`beg=NULL/end!=0` que dispara el throw observado. De los dos símbolos del ctor (`C1`/`C2`,
idénticos), solo `C1` (`0xA6A7E8`) tiene llamadores reales en el `.so` (`C2` en `0xA6A828`: 0
sitios) — confirmado con `grep 'bl.*a6a828'`/`'bl.*a6a7e8'` sobre el disasm ARM completo
(291 llamadores de `C1`, ninguno de `C2`).

**Fix aplicado — todavía diagnóstico, no el fix final** (`source/patch.c`, Bug #022, hook
`hook_sconstruct` sobre `OFF_SCONSTRUCT = 0xA6A7E8`): hookea la ENTRADA del ctor (antes de que
su propio `push {r4,r5,r6,lr}` pise nada), chequea `r1` (el `const char*` de entrada) y, si es
NULL, loguea `LR` — que en ESE punto exacto todavía es la dirección de retorno real en código
del JUEGO (el llamador que armó el `std::string` con un `char*` NULL), no un frame intermedio
de libstdc++. Con `r1 != 0` (los otros ~290 llamadores legítimos) el costo es un `cmp`+`bne`
y nada más — no hay `bc_enter` ni llamada a ninguna función C en el camino caliente. Ninguna
de las dos palabras pisadas (`push {r4,r5,r6,lr}` + `subs r5,r1,#0`) es PC-relativa, así que
se emulan verbatim, sin doble indirección. Build verificado (`psvita-toolkit build`, compila y
linkea limpio, Debug).

**Actualización — log 030, llamador y causa raíz confirmados al 100%:**
El log `logs/asphalt6_030.log` reportó la dirección exacta del llamador gracias al hook diagnóstico:
```
[ERROR] [patch] std::string(NULL): llamador=libasphalt6.so+0x5A6E94 (Bug #022, diagnostico)
[ERROR] [patch] __cxa_throw: type='St11logic_error' msg='basic_string::_S_construct NULL not valid' (Bug #022, diagnostico)
[FATAL] Abort called from address 0x98a71e70
```

1. **Desensamblado de `0x5A6E94`:**
   Pertenece a `_ZN13StringManager11SetLanguageEPKc` (`StringManager::SetLanguage(const char* lang)` en `0x5A6E78`).
   En `0x5A6E90`:
   ```arm
   5a6e90: bl a6a7e8 <_ZNSsC1EPKcRKSaIcE> ; std::string(fp, r1, &allocator)
   5a6e94: ldr r3, [pc, #308]
   ```
   Recibe `r1` (`lang`) y construye un `std::string` sin verificar `NULL`. Al ser `r1 == 0`, libstdc++ tira `std::logic_error`.

2. **¿Quién llamó a `StringManager::SetLanguage` con `NULL`?**
   Rastreando `StringManager::SetLanguage` y los métodos de `FlashFXHandler`:
   En `GS_MenuMain::StateUpdate` (`0x3F43EC` - `0x3F43FC`):
   ```arm
   3f43ec: ldr r7, [r3, #36] ; vtable slot 0x24: FlashFXHandler::SetLanguage
   3f43f0: bl  4e94f0 <_ZN13StringManager17GetLanguageStringEv>
   3f43f4: mov r1, r0
   3f43f8: mov r0, r5
   3f43fc: blx r7
   ```
   `GS_MenuMain::StateUpdate` llama a `StringManager::GetLanguageString()` (`0x4E94F0`) y pasa el resultado directamente como segundo argumento a `FlashFXHandler::SetLanguage`.

3. **¿Por qué `GetLanguageString()` devuelve `NULL`?**
   `StringManager::GetLanguageString()` busca el idioma actual `m_languageId` en su mapa de idiomas soportados (1="english", 2="french", 3="italian", 4="german", 5="spanish", 6="japanese", 7="korean", 8="russian", 9="brazilian").
   En el primer arranque ("First time launch the app"), el perfil del jugador no existe y `m_languageId == 0`.
   Como el ID 0 no está en la tabla, `GetLanguageString()` retorna `0` (`NULL`).
   `GS_MenuMain::StateUpdate` pasa este `NULL` a `FlashFXHandler::SetLanguage`, que invoca `StringManager::SetLanguage(NULL)` -> `std::string(NULL)` -> `abort()`.

**Fix aplicado (`source/patch.c`):**
1. Hook en `StringManager::GetLanguageString` (`0x4E94F0`): Si el motor retorna `NULL` (idioma 0 no inicializado), se loguea y se retorna `"english"` como fallback por defecto.
2. Hook de defensa en profundidad en `StringManager::SetLanguage` (`0x5A6E78`): Si `lang == NULL`, se reemplaza `r1` por `"english"`.
3. Sanitizador global en `std::string::string(const char*, const allocator&)` (`0xA6A7E8`): Si algún otro componente del motor intenta construir un `std::string` desde `NULL`, se sustituye por `""` (`s_empty_str`) en vez de dejar que `_S_construct` arroje `std::logic_error` y cause un `abort()`.

Build verificado (`psvita-toolkit build`, compila y linkea limpio).


### Sesión 2026-09-11: quick wins de FPS por paridad con Asphalt-5-Vita (sin probar en hardware todavía)

**Contexto:** el juego llega al menú/title casi perfecto pero con FPS bajos (log 031: 3-8
frames por latido de 5 s en menú, ~5 fps durante carga de pista). Diff completo contra el
port hermano `Asphalt-5-Vita` (mismo motor/familia Gameloft, fluido): A5 renderiza a un FBO
offscreen de 720x432 y hace upscale-blit (40% menos píxeles que 960x544), y además elimina
todos los stalls/readbacks que A6 todavía pagaba. El downsample NO se porta tal cual: A5 es
GLES1.1 sin FBOs propios del motor, A6 es GLES2 con RTT de menú (`MenuRenderTarget`) y
post-procesado -- un FBO-ciego rompería el menú. Queda como fase 2 (variante FBO-aware).

**Cambios aplicados (build Debug OK con `psvita-toolkit build`; Release falla en link por
causa pre-existente en `source/patch.c` no tocada por esta sesión:
`undefined reference to s_tr_menucar/g_skip_menucar/s_empty_str` -- símbolos `static`
referenciados solo desde asm inline que el link de Release descarta):**
1. `dynlib.c`: `glFinish` -> `ret0` (bloquea hasta GPU idle, el stall más caro), `glReadPixels`
   -> `ret0` (readback CPU), `glCopyTexImage2D/SubImage2D` -> stubs rápidos nuevos (dummy
   1x1 / no-op, igual que A5). `glFlush` (no bloqueante) se deja real.
2. `glutil.c/.h`: `glTexImage2D_soloader` remapea internalformats S3TC/DXT a `GL_RGBA`
   (evita el encoder DXT por software de vitaGL, igual que A5); `glBindFramebuffer`,
   `glFramebufferTexture2D`, `glCheckFramebufferStatus` pasan de `gl_info` (sceIoWrite a la
   SD por llamada, también en Release -- el menú hace RTT cada frame) a `gl_trace`
   (solo con `-DTRACE_GL_CALLS`).
3. `main.c`: `sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT)` por frame (paridad con
   Bug #26 de A5: evita dim/suspend por idle en menús).

**Pendiente:** medir en consola real (log + FPS). Siguiente candidato si sigue bajo:
variante FBO-aware del downsample (solo fb 0 al offscreen, blit con shader GLES2).

### Bug #023 — Data Abort en `TrackScene::LoadLevelGeometry()` al entrar a la carrera ("IPAD2a_" asset crash) y optimizaciones de vitaGL — 2026-09-11

**Logs / Dumps:**
- `logs/asphalt6_032.log` (termina en carga de Bahamas con `[WARNING] fopen(ux0:data/asphalt6/data/IPAD2a_Bahamas.bdae, rb): 0x0`).
- `logs/asphalt6-psp2core-1789104516-0x00037d2f01-eboot.bin.psp2dmp` (triageado con `vita-parse-core` y `so-crash-triage`).

**Síntoma:**
- Data abort exception (0x30004) en hilo `ASPHALT06` (principal).
- PC: `0x9846b1bc` (offset `0x46B1BC` en `libasphalt6.so`), LR: `0x9846b1b8`, R4 = `0x00000000`.

**Causa Raíz (Metodología `so-crash-triage`):**
1. En `TrackScene::LoadLevelGeometry()` (`0x46AF38`), el motor comprueba el ID de la pista actual en `0x46AFD8`:
   ```arm
   46afd8: cmp   r3, #9    ; NewYork
   46afdc: cmpne r3, #6    ; Monaco
   46afe0: beq   46b114
   46afe4: cmp   r3, #3    ; Havana
   46afe8: beq   46b114
   46afec: cmp   r3, #7    ; Moscow
   46aff0: beq   46b114
   ```
2. Para New York, Monaco, Havana y Moscow salta a `0x46B114`, donde concatena directamente `<Track>.bdae` (5 bytes).
3. Para cualquier otro circuito (como Bahamas, ID 1), el código toma el camino alternativo en `0x46B0B8` que antepone la cadena `"IPAD2a_"` (7 bytes), construyendo `"IPAD2a_Bahamas.bdae"`.
4. El archivo `"IPAD2a_Bahamas.bdae"` es un remanente del motor iOS que no existe en el paquete de datos de Android (`file000000.dat` solo mapea `Bahamas.bdae` -> `file000554.dat`).
5. Por ende, `fopen(ux0:data/asphalt6/data/IPAD2a_Bahamas.bdae)` falla devolviendo `NULL`.
6. `CColladaDatabase::constructScene()` falla y retorna `NULL` (`r0 = 0`, guardado en `[fp, #12]`).
7. En `0x46B1B8`, se carga `r4 = [fp, #12] = 0x0`.
8. En `0x46B1BC`, se ejecuta `ldr r3, [r4]` (el inicio del `drop()` inlined del nodo devuelto), dereferenciando `0x0` y causando un Data Abort fatal.

**Solución Aplicada (Defensa en 3 capas):**
1. **Bypass del prefijo `IPAD2a_` en `source/patch.c`:**
   En `0x46AFD8`, se reemplaza `cmp r3, #9` (`0xE3530009`) por un salto incondicional `b 0x46B114` (`0xEA00004D`). Con esto, TODOS los circuitos cargan directamente `<Track>.bdae` resolviendo a través del mapa de ofuscación a su respectivo archivo `.dat` (`file000554.dat` en el caso de Bahamas).
2. **Guarda NULL en `TrackScene::LoadLevelGeometry` (`0x46B1B8`):**
   Hook `hook_loadgeom` sobre `0x46B1B8`: si `r4 == NULL`, se emite una advertencia al log y se salta limpiamente a `0x46B1EC` (`~CColladaDatabase`), evitando el desreferenciamiento de NULL.
3. **Fallback en `source/reimpl/io.c`:**
   En `fopen_soloader`, `open_soloader` y `stat_soloader`, si una apertura falla y la ruta contiene `"IPAD2a_"`, se remueve automáticamente el prefijo y se reintenta la llamada.

---

### Optimizaciones de Rendimiento de vitaGL y Render Loop (`README VITAGL.md`)

En respuesta al análisis de velocidad y la guía oficial de `README VITAGL.md`:
1. **Configuración de Flags en `CMakeLists.txt` (`VITAGL_MAKE_FLAGS`):**
   - `BUFFERS_SPEEDHACK=1`: Acelera drásticamente `glBufferSubData` escribiendo en memoria de GPU directamente en lugar de re-alocar búferes en cada frame dinámico.
   - `NO_TILE_CLIPPER=1`: Desactiva el clipping temprano de tiles para scissor test, reduciendo la carga en la CPU ARM Cortex-A9.
   - `NO_DMAC=1`: Reemplaza llamadas a `sceDmacMemcpy` (que pagan el costo de transición al kernel) por memcpy acelerado por NEON en espacio de usuario.
   - `NO_TEX_COMBINER=1`: Deshabilita combinadores de texturas de fixed function pipeline no utilizados (el juego usa shaders GLSL).
   - Se mantiene `DRAW_SPEEDHACK=2`, `HAVE_SHADER_CACHE=1`, `NO_DEBUG=1`, `NO_SPLASHSCREEN=1`, `SOFTFP_ABI=1`, `LOG_ERRORS=1`.
2. **Optimización del Render Loop en `source/utils/glutil.c`:**
   - Se eliminaron las llamadas de instrumentación pesada (`BC_SCOPE`, con llamadas al kernel `sceKernelGetProcessTimeLow()` y operaciones atómicas) dentro de funciones de llamada caliente (`glDrawArrays_soloader`, `glDrawElements_soloader`, `glUseProgram_soloader`, `glGetUniformLocation_soloader`, `glGetAttribLocation_soloader`), dejándolas activas únicamente bajo `#ifdef TRACE_GL_CALLS`. Esto ahorra miles de syscalls por segundo en el hilo de render.
3. **Corrección en `vita.cmake` para empaquetado de VPK:**
   - Se agregaron comillas y `UNIX_COMMAND` en `separate_arguments(VITA_PACK_VPK_FLAGS)` para que `vita-pack-vpk` soporte correctamente rutas con espacios (`PSVITA Develop`).

**Estado de Compilación:**
- Binarios `eboot.bin` y `asphalt6.vpk` compilados limpiamente al 100% con VitaSDK.

### Bug #024 — Data Abort en `RenderFX::SetTextBufferingEnabled` (`0x682aa8`) por ausencia de `178hud.swf` — 2026-09-11

**Logs / Dumps:**
- `logs/asphalt6_033.log` (termina en `[WARNING] fopen(ux0:data/asphalt6/data/178hud.swf, rb): 0x0`, `smart_ptr.h: operator->: 132`).
- `logs/asphalt6-psp2core-1789150269-0x00084b2edf-eboot.bin.psp2dmp` (triageado con `vita-parse-core` y `so-crash-triage`).

**Síntoma:**
- Data abort exception (0x30004) en hilo `ASPHALT06`.
- PC: `0x98682aa8` (`_ZN8RenderFX23SetTextBufferingEnabledEb + 0x1C`), LR: `0x98682af0`, R3 = `0x00000000`.

**Causa Raíz:**
1. Al comenzar la carrera, `T_SWFManager::SWFLoad()` carga `"178hud.swf"`.
2. `"178hud.swf"` no existía como archivo suelto en `ux0:data/asphalt6/data/`, por lo que `fopen` falló (`0x0`).
3. `RenderFX::Load` falló y dejó el puntero `m_root` (`[r0, #60]`) en `NULL`.
4. A continuación, `T_SWFManager::SWFLoad` invoca `RenderFX::SetTextBufferingEnabled(bool)`.
5. En `0x682a8c`:
   ```arm
   682a8c: ldr   r3, [r0, #60]   ; r3 = m_root (NULL)
   682a94: cmp   r3, #0
   682aa4: beq   682ab4          ; salta a loguear el assert "smart_ptr.h: operator->: 132"
   682aa8: strb  r5, [r3, #133]  ; <-- CRASH: desreferencia r3=0
   ...
   682aec: bl    __android_log_print
   682af0: ldr   r3, [r4, #60]
   682af4: b     682aa8          ; <-- tras el assert, salta incondicionalmente al store sobre NULL!
   ```
6. El compilador generó un assert que imprime el mensaje de advertencia pero luego reanuda la ejecución en la instrucción de escritura `strb` sobre NULL sin retornar, causando el Data Abort.

**Asset Faltante Encontrado (`file000483.dat`):**
- Se analizaron los 37 archivos SWF empaquetados en los `fileNNNNNN.dat` de `ux0_data/asphalt6/data/`.
- `file000483.dat` contiene las cadenas del HUD de carrera (`Hud.tga`, `number_flipper_lap`, `Button_halfScreen`, `mc_end_label`, `He's Ahead of You!`, `He's Behind You!`, `gear_up`, `gear_down`, `camera`).
- Header desofuscado: Magic `FWS`, versión 8, longitud 164,121 bytes (coincide exactamente con el tamaño del archivo).

**Solución Aplicada:**
1. **Instalación del Asset:**
   Se desofuscó `file000483.dat` y se instaló en `ux0_data/asphalt6/data/178hud.swf`.
   *(Nota: para transferir a la consola, se debe copiar `ux0_data/asphalt6/data/178hud.swf` a `ux0:data/asphalt6/data/178hud.swf`)*.
2. **Guardas en `RenderFX` (`source/patch.c`):**
   Se hookearon con código asm naked las funciones:
   - `_ZN8RenderFX23SetTextBufferingEnabledEb` (`0x682a8c`)
   - `_ZN8RenderFX24SetAutoLoadGlyphsEnabledEb` (`0x682b08`)
   - `_ZN8RenderFX23SetRenderCachingEnabledEb` (`0x682b84`)
   Si `m_root == NULL`, retornan de inmediato sin escribir en memoria ni crashear.

**Estado:**
- Compilación limpia al 100% de `eboot.bin` y `asphalt6.vpk`.

### Bug #025 — Recurrencia del crash de carrera: `178hud.swf` nunca se transfirió a la consola, más un Data Abort nuevo en `_Unwind_Backtrace` — 2026-09-11

**Logs / Dumps:**
- `logs/asphalt6_034.log` (mismo patrón EXACTO del Bug #024: `[WARNING] fopen(ux0:data/asphalt6/data/178hud.swf, rb): 0x0` x2, `menufx.cpp: Load: 354`, luego CUATRO `smart_ptr.h: operator->: 132` en vez de crashear de una — señal de que las 3 guardas de `RenderFX` sí están activas en el build actual).
- `logs/asphalt6-psp2core-1789160963-0x000e5c2ccd-eboot.bin.psp2dmp` (triageado con `psvita-toolkit analyze` + `so-crash-triage`).

**Triangulación:**
1. El Bug #024 dejó anotado explícitamente que faltaba transferir `ux0_data/asphalt6/data/178hud.swf` a `ux0:data/asphalt6/data/178hud.swf` en la consola — ese paso nunca se hizo.
2. Se verificó por FTP directo contra `192.168.3.15:1337` (`LIST` de `ux0:/data/asphalt6/data`): el archivo real en consola **no** incluía `178hud.swf` (sí estaban `178igMenu.swf`, `178loading.swf`, `178quickrace.swf`). Confirmado: el asset nunca llegó a la consola pese a existir localmente en `ux0_data/`.
3. Con el asset ausente, `T_SWFManager::SWFLoad("178hud.swf")` sigue fallando y `RenderFX::m_root` sigue en `NULL` — pero ahora las 3 guardas del Bug #024 absorben esos accesos (4 asserts no fatales en el log, contra 1 fatal antes del fix).
4. El juego avanza más allá del punto del Bug #024 y crashea en un lugar nuevo:
   - PC: `0x985f88bc` → `libasphalt6.so+0xa788bc`, dentro de `_Unwind_Backtrace` (prólogo, `push {r0-r9,sl,fp,ip}`), a 4 bytes de su entry point (`0xa788b8`).
   - LR: `0x985f88b0`, dentro del epílogo de `_Unwind_ForcedUnwind` (`add sp, sp, #72`).
   - R0/R4/R7/R8/R9 = `0x0`.
   - Backtrace de pila apunta a `__muldf3+0x1c8` como retorno anterior — consistente con desenrollado de una excepción C++ real (no un simple `strb` sobre NULL como el #024), probablemente disparada más adelante en el mismo flujo de `SWFLoad`/render del HUD al no encontrar `m_root` válido en un sitio NO cubierto por las 3 guardas existentes.

**Causa Raíz confirmada de ESTA corrida:** el archivo `178hud.swf` nunca se copió a la consola real — la corrida 034 repitió el escenario exacto del Bug #024 con el binario YA parcheado, así que las guardas hicieron su trabajo pero no eliminan la causa original (HUD ausente), solo evitan el primer crash de la cadena. El segundo crash (`_Unwind_Backtrace`) es una consecuencia de seguir operando varios frames con `m_root == NULL`, no un bug independiente confirmado todavía.

**Acción tomada:**
- Se subió `178hud.swf` (164.121 bytes, el mismo recuperado en el Bug #024 desde `file000483.dat`) por FTP a `ux0:/data/asphalt6/data/178hud.swf`. Verificado con `LIST` post-subida.
- **No se tocó código.** El fix de código del Bug #024 (las 3 guardas de `RenderFX`) ya estaba correcto y desplegado; solo faltaba el asset.

**Pendiente:** correr una carrera de nuevo con el asset ya presente y sacar un log fresco. Si `178hud.swf` carga bien, lo esperable es que ninguna de las 3 guardas se dispare y el crash de `_Unwind_Backtrace` no debería reproducirse (era downstream de `m_root == NULL`). Si el crash de `_Unwind_Backtrace` reaparece de todos modos, triagearlo como bug nuevo con el log/dump de esa corrida — no asumir la misma causa.

### Feature — Reproducción del FMV de intro (`intro.mp4`) vía SceAvPlayer — 2026-09-11

**Contexto:** `ux0_data/asphalt6/data/intro.mp4` (el intro real de Gameloft, 63s) nunca se reproducía —
no había ningún camino de código que lo tocara. Investigación cruzada contra los ports hermanos
"Prince of Persia" y "Dungeon Hunter 2" (mismo motor Gameloft que Asphalt 6 en el caso de DH2), que ya
tienen esto resuelto y probado en consola real.

**Hallazgo 1 — el archivo original no es reproducible tal cual:** `ffprobe` mostró que `intro.mp4`
viene codificado en **MPEG-4 Part 2** (`mp4v`, Simple Profile, 854x480). El decodificador de hardware
de la Vita (`SceAvPlayer`) **solo decodifica H.264+AAC**. Se recodificó con `ffmpeg`
(`-c:v libx264 -profile:v main -level 3.1 -pix_fmt yuv420p -c:a copy`, mismos parámetros confirmados
funcionando en el `intro.mp4` de Dungeon Hunter 2) manteniendo resolución/duración originales. El
archivo original se conservó como `ux0_data/asphalt6/data/intro_orig_mpeg4.mp4` por si hace falta
volver a codificar con otros parámetros.

**Hallazgo 2 — el punto de enganche real (Ghidra sobre `libasphalt6.so`):**
- `Java_...GLMediaPlayer_nativeInit` resuelve (junto con ~40 métodos de audio de `vox::DriverAndroid`,
  sin implementar todavía) los jmethodID de `"loadMovie"` `"(Ljava/lang/String;)V"` y
  `"isMediaPlaying"`.
- `nativeLoadMovie(const char*)` es una función **exportada** (no un callback JNI que llame la VM): arma
  un jstring con el nombre recibido y hace `CallStaticVoidMethod(GLMediaPlayer.class, loadMovie, jstr)`.
  Igual que `GLGame_nativeInit`/`GameRenderer_nativeInit`/etc., en Android real la dispara la Activity
  Java (acá no hay VM real que lo haga sola), así que se llama a mano desde `main.c`.
- `nativeIsMediaPlaying()` retorna `0` hardcodeado sin pasar por JNI (confirmado en el pseudo-C) —
  registrar `isMediaPlaying` es solo para evitar ruido de "method ID not found" si algún otro camino
  llegara a invocarlo.

**Implementación:**
1. `source/video.cpp` / `source/video.h` (nuevos): reproducción por `SceAvPlayer` con conversión manual
   YUV420p→RGB565 (tablas enteras BT.601 + NEON a mano) y dibujo por textura vitaGL, adaptado 1:1 del
   mecanismo ya probado en consola real en Prince of Persia (mismo esquema que usa vitaGL, a diferencia
   de Dungeon Hunter 2 que usa GLES2 puro). Incluye audio del video por un hilo y puerto
   `SCE_AUDIO_OUT_PORT_TYPE_VOICE` dedicados (totalmente separados del audio del juego, que no existe
   todavía). `video_play()` nunca cuelga: retorna al terminar el video, al saltearlo con Cruz/Start, o al
   fallar abrir/decodificar.
   - Nota de compatibilidad: el fork de vitaGL vendorizado acá NO tiene el enum `VGL_MEM_SLOW` que sí
     tiene el de Prince of Persia (usa `VGL_MEM_PHYCONT`) — se ajustó al adaptar el código. Este mismo
     fork además trae un helper `vglPhycontMemLazyInit()` y un sample completo en
     `lib/vitaGL/samples/video_playback/` con una ruta MUCHO más simple (textura
     `SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1`, conversión YUV→RGB por hardware/GXM, sin CPU/NEON) pero que
     requiere compilar vitaGL con `ENABLE_LEGACY_PIPELINE=1` (no activado hoy) y usar el pipeline
     `vgl*`/`vglDrawObjects` en vez de las llamadas GL1 estándar -- se dejó pendiente como posible
     optimización futura si el intro actual rinde mal, en vez de tocar flags de build para esto.
2. `source/java.c`: agregado `GLMediaPlayer_loadMovie` (`jstring` → `video_play()`, IDs `60`/`61` en las
   tablas `nameToMethodId`/`methodsVoid`/`methodsBoolean`) y `GLMediaPlayer_isMediaPlaying` (no-op,
   `JNI_FALSE`).
3. `source/main.c`: tras `gl_init()` (el allocator de framebuffers de video necesita GXM ya
   inicializado), se resuelve y llama `GLMediaPlayer_nativeInit` (registra el jmethodID real de
   `loadMovie` — si no corre antes, `nativeLoadMovie` llama con jmethodID `0` y FalsoJNI la descarta en
   silencio), después `video_init()` (carga `SCE_SYSMODULE_AVPLAYER`), y por último se llama
   `nativeLoadMovie("intro.mp4")` directamente.
4. `CMakeLists.txt`: agregado `source/video.cpp` a `add_executable` y `SceAvPlayer_stub`/
   `SceSysmodule_stub` a `target_link_libraries`.

**Estado:** compila limpio (`psvita-toolkit build --preset debug`, sin warnings nuevos). **Sin probar en
consola real todavía** -- la consola estaba offline/FTP inalcanzable al momento de este cambio. Falta
desplegar `build/eboot.bin` + el `intro.mp4` ya recodificado y confirmar con un log fresco que el video
se ve/oye bien y que el juego sigue de largo al menú al terminar.

**Actualización 2026-09-11 (log 035):** primer log con el eboot ya desplegado. `178hud.swf` cargó bien
(no hubo que redesplegar el asset, ya estaba). Pero **cero rastro de "video:" en todo el log de 9874
líneas**, ni siquiera un `l_warn`/`l_error` -- ninguno de los ~40 métodos que `GLMediaPlayer_nativeInit`
debería resolver (`loadMusic`, `playMusic`, `loadMovie`, etc.) aparece tampoco como "not found" en
ningún lado, así que la función completa (no solo `loadMovie`) parece no haber corrido, pese a que su
símbolo (`Java_..._GLMediaPlayer_nativeInit`) tiene el mismo tipo/binding que `GLGame_nativeInit` (que sí
funciona) y su primera instrucción sigue el MISMO patrón "Ghidra muestra 0 argumentos" que
`GLGame_nativeInit` ya usa sin problema -- descartada esa hipótesis. Se agregó logging explícito
alrededor de `so_symbol()`/la llamada a `GLMediaPlayer_nativeInit` y a `nativeLoadMovie` en `main.c`
(entrada + retorno de cada una) para que el próximo log confirme de una vez si el símbolo se resuelve y
si la llamada realmente ejecuta o se salta. **Sin diagnóstico confirmado todavía**, pendiente de la
próxima corrida.

### Bug nuevo (sin confirmar) — Data Abort durante la carga de Bahamas, posible vtable de `CNullDriver` en un objeto que no debería tenerlo — 2026-09-11

**Logs/Dumps:** `logs/asphalt6_035.log`, `logs/asphalt6-psp2core-1789177709-0x000c9a257b-eboot.bin.psp2dmp`.

**Contexto confirmado:**
- Pista **Bahamas**, "Launch Game by PN" (carrera rápida) -- misma pista del Bug #023.
- El log corta justo después de warnings no fatales de audio sin implementar
  (`sfx_ambience_beach.wav`, `vfx_intro_top3.wav`, `m_electro_7.wav`) -- el crash real ocurre
  construyendo la escena 3D de la pista, más allá de todo lo ya probado.
- `psvita-toolkit analyze`: Data Abort (0x30004), hilo ASPHALT06. PC dentro de
  `glitch::video::CNullDriver::draw2DLine`/`getMaxUserClipPlanes` (offsets 0x7f4eb0-0x7f4ec8, funciones
  no-op de 2-3 instrucciones). **El LR resuelto (`mpc_demux_init+0x64`) y la "secuencia de llamadas
  reconstruida" del stack son basura de "símbolo más cercano"/escaneo ingenuo de pila -- verificado que
  al menos una entrada ("CColladaDatabase::constructScene+0x1618") en realidad cae dentro de un template
  de animación de materiales sin relación.** No confiar en esos dos datos sin re-verificar.

**Ambigüedad de fondo (sin resolver):** ninguna interpretación (ARM ni Thumb) de la instrucción exacta
en el PC reportado puede causar un Data Abort por sí sola (ninguna de las dos toca memoria). El dump no
captura CPSR/bit Thumb del hilo (limitación confirmada de `vita-parse-core`), así que no se puede
confirmar el modo real ni descartar que el PC necesite el ajuste de -8 típico de un data abort en ARM.

**Hallazgo útil:** `glitch::CAndroidOSDevice::createDriver()` (0x701860) elige el driver por un campo de
tipo del objeto device: `CNullDriver` es una opción **legítima y seleccionable** (no es basura), y su
`createBuffer` (0x7f4ed0) hace `operator new`+`IBuffer::IBuffer(...)` real (no es un stub trivial) --
pero el driver GLSL real (`CProgrammableGLDriver<CGLSLShaderHandler>`) tiene sus PROPIOS
`createBuffer`/`getMaxUserClipPlanes` en direcciones totalmente distintas (0x914814/0x90e3b0). Hipótesis
sin confirmar: un objeto que debería usar el driver GLSL real termina con el vtable de `CNullDriver`
(puntero corrupto o de memoria liberada -- **R10 valía `0xdeadbeef`** en el crash, patrón típico de
memoria envenenada/liberada).

**Diagnóstico instalado (sin tocar comportamiento), `source/patch.c`:** 3 hooks nuevos que loguean el
`this` (r0) de cada invocación real durante una corrida:
- `hooked_CNullDriver_draw2DLine` / `hooked_CNullDriver_getMaxUserClipPlanes`: reemplazo 1:1 (son no-ops
  triviales), logean y hacen exactamente lo mismo que el original.
- `hooked_CNullDriver_createBuffer`: conserva el cuerpo real (emula sus 2 primeras palabras del
  prólogo, `push {r4,r5,r6,r7,r8,sl,lr}` + `sub sp,sp,#12`, verificadas contra el binario antes de
  instalar) y solo antepone el log.

**Pendiente:** correr una carrera contra Bahamas de nuevo con este build y revisar el log en busca de
líneas `[patch] CNullDriver::<método> this=0x...` -- confirmaría cuál(es) de los 3 métodos se invoca(n)
de verdad y con qué `this`, para saber si el objeto es válido o corrupto y de dónde viene la llamada.
Sin este dato, no hay fix propuesto todavía -- no adivinar la causa raíz sin esta confirmación.

### Reemplazo del reproductor de video de intro: SceAvPlayer → FFmpeg por software — 2026-09-11

**Motivo del cambio:** la implementación anterior (Bug/Feature de la sesión previa) recodificaba
`intro.mp4` de MPEG-4 Part 2 a H.264 porque `SceAvPlayer` (decodificador de hardware de la Vita) solo
soporta H.264+AAC. El usuario correctamente objetó: **un port que necesita modificar los archivos
originales del juego no es un port real** -- nadie que arme este port desde su propia copia legal del
APK podría reproducir ese paso de recodificación de la misma forma.

**Solución adoptada:** se investigó cómo lo resuelven los ports hermanos. **Shadow Guardian NO aplica**
como referencia -- se confirmó con `ffprobe` que su video (`logo.m4v`) YA viene en H.264 nativo, nunca
tuvo este problema. El port que sí resuelve exactamente esto es **Asphalt-5-Vita** (mismo motor
Gameloft, mismos assets `.mp4` en MPEG-4 Part 2 sin tocar): decodifica el archivo ORIGINAL enteramente
por software vía FFmpeg (`libavcodec`+`libavutil`+`libswresample`), con un demuxer MP4/ISO-BMFF
escrito a mano porque el build de `vdpm ffmpeg` para vitasdk **no incluye el demuxer `mov`** de
`libavformat` (confirmado ahí con `ar t libavformat.a | grep mov`: solo está el muxer, no `mov.o`) --
documentado en detalle en el header de `source/video.cpp` de ese port, con el log real del error
(`avformat_open_input failed: Invalid data found...`) que llevó a ese diagnóstico.

**Se adoptó ese mecanismo 1:1**, copiando y adaptando `source/video.cpp`/`video.h` de Asphalt-5-Vita
(2090 líneas, ya endurecidas en consola real: 3 hilos -- decode/audio/render --, ring de frames,
reloj de reproducción maestreado por el audio, NEON YUV420P→RGB565, `lowres=1` para decodificar a
mitad de resolución por costo de CPU). Ajustes específicos para Asphalt 6:
- `VIDEO_TARGET_W/H`: Asphalt 5 usa un FBO de downsample intermedio (`OFFSCREEN_W/H` en su
  `glutil.h`) que Asphalt 6 **no tiene** -- `gl_init()` acá llama `vglInitExtended(0, 960, 544, ...)`
  directo a la resolución real de pantalla. Se cambió a `960x544` fijo.
- Comentarios actualizados para reflejar los datos reales de Asphalt 6 (`intro.mp4`: 854x480,
  MPEG-4 Part 2 Simple Profile, un solo archivo, no 7) y aclarar que la advertencia sobre no usar un
  shader GLSL custom para el dibujo del video (regresión confirmada en Asphalt 5, motor GLES1.1) es
  una precaución heredada, no todavía confirmada en este port (cuyo motor es GLES2/GLSL puro, la
  situación inversa) -- se mantiene el camino de pipeline fijo de vitaGL por las dudas, sin costo real
  (es un draw de unos pocos frames, no un hot path).
- `source/java.c`/`source/main.c`: **sin cambios** -- la interfaz (`video_init()`/`video_play(name)`)
  es idéntica a la versión SceAvPlayer anterior, así que el enganche JNI (`GLMediaPlayer_loadMovie` →
  `video_play()`, `nativeLoadMovie()` llamado a mano desde `main.c`) sigue igual.
- `CMakeLists.txt`: se sacaron `SceAvPlayer_stub`/`SceSysmodule_stub` (ya no se usan) y se agregaron
  `avformat`/`avcodec`/`avutil`/`swresample`/`mp3lame` (paquetes `vdpm ffmpeg` + `vdpm lame` de
  vita-portlibs, ya instalados en este toolchain).
- Se **restauró** `ux0_data/asphalt6/data/intro.mp4` a su formato original (MPEG-4 Part 2, se había
  recodificado a H.264 en la sesión anterior) y se eliminó la copia recodificada -- el archivo que se
  despliega a la consola es ahora el mismo, byte a byte, que trae el APK.

**Estado:** compila y linkea limpio (`psvita-toolkit build --preset debug`, sin errores contra las
libs de FFmpeg). El eboot.bin creció de ~630KB a ~1.55MB por el link estático de FFmpeg -- esperado,
sin problema de espacio. **Sin probar en consola todavía** -- la consola estaba offline/FTP
inalcanzable al momento de este cambio. Pendiente: desplegar y confirmar en un log real que el video
decodifica y reproduce bien (buscar líneas `video: primer frame de video decodificado`, `video:
loop exited!` con sus estadísticas de fps/tiempos por etapa), y ya que estamos, revisar en el mismo
log si el misterio de la sesión anterior (`GLMediaPlayer_nativeInit` sin rastro alguno) se resuelve con
el logging de diagnóstico ya agregado en `main.c`.

### Log 038 — cuelgue antes del menú en `TrackingManager::updateSaveFile` (bucle de copia sin cota) — 2026-09-12

**Log:** `logs/asphalt6_038.log` (Debug, 2900+ líneas). Llega lejos: intro saltada,
66+ shaders, perfiles, `First time launch`, `pn.dat`/`timespent.dat` guardados, 3 POST
fallidos a `ets.gameloft.com`, `tracking_data2.dat` rb + `tracking_data1.dat` wb
abiertos con éxito, 140 frames presentados. Después, cuelgue duro en
`nativeRender ENTRA #28` (40-70 s sin frames, principal `CORRIENDO` al 100%,
`+0 reservas +0 strstr +0 strcmp`, `~9k gettod` por latido).

**Diagnóstico (log + pseudo-C + disasm ARM con capstone, sin adivinar):**
- El anillo del principal termina en `ENTRA nativeRender` → `ENTRA IDevice::run` →
  `pthread_mutex_lock` (sale) → `fopen` ×2 (ambos con sale) y nada más. O sea,
  trabado DENTRO de `run`, después de esos dos `fopen`, en código que no toca
  nada instrumentado.
- Esos dos `fopen` salen de `glot::TrackingManager::updateSaveFile()` (0x558624):
  las direcciones de retorno del anillo (0x5586CC/0x5586E0) caen dentro de esa
  función (confirmado por índice de símbolos), y el log muestra exactamente sus
  dos archivos justo antes del corte.
- El `+9k gettod` es ruido de un worker (`sitio de reloj: tid=0x40010223` desde
  `CCondition::wait` en 0x85B0A8, ya atribuido en el Bug #015): el principal gira
  en silencio, no sondea el Timer.
- Disasm ARM real del bucle de copia (0x558814-0x558844): `restante = tamaño -
  offset`; `while (restante > 0) { __n = fread(buf,1,0x19000,src);
  fwrite(buf,1,__n,dst); restante -= __n; }` — **sin chequear `__n == 0`**.
  Si `fread` devuelve 0 (EOF: `fseek` con SEEK_CUR más allá del fin, archivo
  truncado, etc.), `restante` nunca baja: `fread(→0)` + `fwrite(0)` para siempre.
  Puros loads, sin malloc/syscalls instrumentados — la firma exacta del 038.

**Fix aplicado (`source/patch.c`, filosofía Bugs #020/#021: guarda en runtime):**
hook en 0x558828 (`mov r1,#1` + `mov r3,r4`, ambas verbatim, r12 scratch libre):
si `r0 (__n) == 0`, loguea `[patch] TrackCopy:` y salta a 0x558848 (salir del
bucle al `fflush`). Con EOF no hay más que copiar; el tracking es analítica no
crítica. Verificación de dos palabras en `hook_trace` como siempre. Build Debug
verificado (`psvita-toolkit build --preset debug` limpio).

**Cómo leer el próximo log (039):** línea `[patch] TrackCopy: fread devolvio 0...`
⇒ la guarda mordió y el juego debería seguir al menú. Sin esa línea + mismo
cuelgue ⇒ el giro no es este bucle; reabrir con el anillo del principal.
