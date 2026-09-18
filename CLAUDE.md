# Asphalt 6 — Port a PS Vita

Port de `Asphalt-6-Adrenaline-v1.3.3-offline.apk` (Android) a PS Vita vía soloader. Generado con **psvita-port-toolkit**.

## Estructura

- `asphalt6_extract/` — APK extraído (gitignored).
- `decompiled/` — Java (jadx) y pseudo-C (Ghidra) del/los .so (gitignored, regenerable).
- `source/`, `lib/so_util`, `lib/falso_jni` — scaffold del boilerplate (SoLoader + FalsoJNI).
- `PORTING_PLAN.md` — plan vivo, actualizar a medida que se confirman cosas del motor real.
- `port_progress.md` — bitácora, un bug confirmado a la vez.
- `.psvita-toolkit.json` — config para el toolkit standalone (build/deploy/logs/LiveArea/crash dumps).

Este port **no** tiene una copia local de `porting_tools/` -- todo el build/deploy/debug se maneja
desde **psvita-port-toolkit**, la herramienta standalone (fuera de este repo). Abrí el toolkit y
elegí "Continuar con un port existente" apuntando a esta carpeta.

## Motor (CONFIRMADO en consola real, no asumir lo que decía la detección automática)

- ABI: armeabi, armeabi-v7a (se usa armeabi-v7a). Paquete Java: `com.gameloft.android.ANMP.GloftA6HP`.
- **Motor: "Glitch" de Gameloft** (derivado de Irrlicht) + `gameswf` para la UI Flash + `vox`
  para audio. Se ve en los símbolos del pseudo-C: `glitch::video::CGLSLShaderManager`,
  `glitch::scene::ISceneNode`, `glitch::collada::CColladaDatabase`, `gameswf::FlashFX`,
  `vox::VoxEngine`. **NO es Unity/il2cpp** -- eso era una heurística del generador y es falso.
- **GLES 2.0**: el motor compila GLSL en caliente y todos sus shaders empiezan con
  `#define GLITCH_OPENGLES_2`. No hay camino de pipeline fijo.
- **El .so real no está en `lib/` del APK**: `lib/armeabi-v7a/libinject.so` es un inyector
  pirata y `libnativedummy.so` está vacío. El motor (13.1 MB) sale del archivo oculto
  `copy.inject` del APK (un ZIP con el directorio de datos ya "licenciado": `libs/libasphalt6.so`,
  `pack.info`, `shared_prefs/*.xml`).
- **El swap de frames NO pasa por EGL**: el motor llama al callback JNI `swapEGLBuffers`
  desde adentro de `nativeRender`. Ver Bug #013 en `port_progress.md`.
- **Audio: DOS sistemas separados, ambos implementados ahora.**
  1. `vox::DriverAndroid` maneja `android/media/AudioTrack` por JNI crudo (`FindClass` +
     `GetMethodID("<init>"/"play"/"write"/...)`) para la mezcla de carrera -- implementado en
     `source/reimpl/audiotrack.c` (`java.c`, ids 70-76), puerto `SCE_AUDIO_OUT_PORT_TYPE_BGM`
     a 44100 Hz (separado del `VOICE` de `video.cpp`). **Bug #034 (resuelto):** el grain real
     del puerto es `bufferSizeInBytes >> 2`, NO `bufferSizeInBytes / channels` (`_FillBuffer`
     solo llena la mitad de los shorts que `write()` dice mandar -- confirmado en el
     pseudo-C). **Bug #036 (resuelto):** `SCE_AUDIO_OUT_PORT_TYPE_MAIN` en este hardware EXIGE
     48000 Hz (`sceAudioOutOpenPort` devuelve `0x80260008` a 44100, log 058) -- quedó en `BGM`.
  2. `GLMediaPlayer` (`loadMusic`/`playMusic`/`playSound`/`registerSoundFile`/...,
     `Java_..._GLMediaPlayer_nativeInit` resuelve ~40 métodos) es la música de menú y la
     mayoría de los SFX -- **NUNCA sonaba** porque solo `loadMovie` estaba implementado, sin
     relación con lo bien o mal que anduviera el `AudioTrack` de arriba (log 057/058). Los
     `.wav` no son archivos sueltos: están empaquetados en `file00a.bin` (630 entradas, formato
     propio descifrado a mano -- ver `reimpl/soundpack.c`). Implementado en
     `source/reimpl/gmp_audio.c` (mezclador propio, puerto `SCE_AUDIO_OUT_PORT_TYPE_MAIN` a
     48000 Hz, música con loop + pool de 8 voces de SFX). Sin probar en consola todavía.
- **Elementos 3D intermitentes/transparentes, YA DESDE EL MENÚ (no específico de carrera):**
  confirmado con evidencia real (logs 057/058) que un grupo FIJO de ~4 mallas (762-1371
  vértices) se dibuja con `GL_BLEND` activo (`SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA`) desde el
  primer frame del `GS_MenuMain`, mientras OTRAS mallas grandes (hasta 5589 vértices) sí
  salen con blend apagado -- esto DESCARTA que vitaGL aplique mal el estado (log 058) y
  confirma que es clasificación por objeto/material (algunas mallas van a la lista
  "transparente", patrón Irrlicht de dos listas). Se descartaron también
  `RENDER_CULLING_BYPASS` y `CNullDriver::createBuffer` como causa (este último resultó ser
  diseño legítimo, no un driver nulo colado por error -- ver `port_progress.md`). El grupo
  fijo de 4 mallas que sale siempre en blend podría ser decoración fija del menú (vidrio/
  brillo), no necesariamente el auto -- el diagnóstico (`[gl-blend]` en el log, cap de 40
  líneas) se agota siempre durante menú/garage, antes de llegar a una carrera real; falta un
  log que sí llegue a pista con el auto visible. Sin patch todavía sobre la clasificación de
  materiales -- mismo motivo de cautela que con `thisAppendBatch`/
  `CustomResFactory::getTexture` (funciones "monstruo" no seguras de tocar a ciegas sin
  Ghidra real).
- **Estado actual: ¡Llega al menú principal!** Tras resolver los cuelgues durante la carga
  (Bugs #015-#021) y el abort por excepción C++ con idioma no inicializado al primer arranque
  (Bug #022 en `StringManager`), el juego supera el guardado de perfil, carga la UI Flash
  (`gameswf`) y alcanza el menú principal (`GS_MenuMain`), presentando frames continuamente.
  Pendiente: verificación interactiva de controles físicos/touch, confirmar en consola real
  que el cambio de puerto de audio a `MAIN` (log 057) se escucha, y diagnosticar la
  transparencia/intermitencia de mallas 3D (ver más arriba).


## Depuración: qué hay disponible antes de inventar nada

- `source/utils/watchdog.c` — hilo testigo: late cada 5 s con frames presentados, último hito,
  reservas de memoria, y el estado de kernel de CADA hilo (`sceKernelGetThreadInfo`:
  bloqueado/girando, sobre qué objeto, y CPU consumida por latido).
- `source/utils/breadcrumb.c` — anillo en RAM con las últimas 128 llamadas interceptadas
  (ENTRADA/SALIDA + dirección de retorno como `libasphalt6.so+0xNNNN`). Se vuelca solo cuando
  el testigo detecta que no avanzan los frames. **Reemplaza a `TRACE_GL_CALLS`** para ubicar
  cuelgues, sin el `sceIoWrite` por llamada.
- `source/patch.c` — hooks ENTER-only con `hook_addr` sobre funciones internas del `.so`
  (tramo `MenuScene` + interior de `DisplayFrame`): marcan `bc_enter` en el anillo del hilo
  principal y reanudan emulando los 8 bytes pisados. Ojo: un `ldr` PC-relativo emulado
  necesita **doble indirección** (Bug #017 fue esa regresión).
- Contadores atómicos sin syscalls en el latido del testigo: `+N reservas` (malloc),
  `+N strstr`, `+N strcmp` (strcmp/strncmp/memcmp), `+N gettod` (gettimeofday).
- Los tres sumideros de log están unificados en el archivo que se baja por FTP:
  `[ALOG]` (el `__android_log_print` del juego, **solo en build Debug**), `[vitaGL]` y
  `[FalsoJNI]`. Los dos últimos escribían a `sceClibPrintf`, invisible en retail.
- **Para diagnosticar SIEMPRE compilar Debug**: en Release se pierden los `[ALOG]` del motor y
  la traza de `fopen`, que es la mitad de la información útil.

## Flujo de trabajo esperado

1. Análisis de símbolos antes de tocar loader/source -- skill `psvita-port-init` cubrió la Fase 0-2.
2. Bootstrap del loader guiado por la skill `psvita-porting`.
3. Build/deploy con el toolkit standalone → probar en consola real.
4. Un bug a la vez, guiado por el log real -- skill `so-crash-triage`.
5. Actualizar `port_progress.md` con cada bug confirmado.
