# Registro de Progreso — Asphalt 6 (PS Vita)

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

## Fase 4: Bootstrap del loader y Primer Build (En progreso)
- [x] Extraer datos limpios a `ux0_data/asphalt6/` para entorno de pruebas.
- [x] Sincronizar submodulos de git (`FalsoJNI`) y arreglar errores de linkeo (`converter.c`).
- [x] Configurar `CMakeLists.txt` para cargar la ruta real (`ux0:data/asphalt6/lib/armeabi-v7a/libasphalt6.so`).
- [x] Primer build (`asphalt6.vpk`) compilado exitosamente.
- [x] Configurar tabla JNI (`java.c`) con los bindings necesarios para el motor.
- [ ] Ejecutar el `.vpk` en hardware real o emulador para revisar el primer crash de importaciones (`.psp2dmp`).

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
