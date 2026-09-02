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

## Hallazgos de motor (automáticos, sin confirmar)

- ABI: armeabi, armeabi-v7a (preferida: armeabi-v7a)
- GLES: AndroidManifest.xml no declara glEsVersion -- usar heurística (sin señal clara por símbolos (posible Unity/libil2cpp) -- revisar con Ghidra)
- Paquete Java: com.gameloft.android.ANMP.GloftA6HP

## Flujo de trabajo esperado

1. Análisis de símbolos antes de tocar loader/source -- skill `psvita-port-init` cubrió la Fase 0-2.
2. Bootstrap del loader guiado por la skill `psvita-porting`.
3. Build/deploy con el toolkit standalone → probar en consola real.
4. Un bug a la vez, guiado por el log real -- skill `so-crash-triage`.
5. Actualizar `port_progress.md` con cada bug confirmado.
