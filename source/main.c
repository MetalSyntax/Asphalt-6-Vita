#include "utils/init.h"
#include "utils/glutil.h"

#include <stdlib.h>

#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module so_mod;

extern void java_init_static_strings(void);

int main() {
    soloader_init_all();

    int (* JNI_OnLoad)(void *jvm) = (void *)so_symbol(&so_mod, "JNI_OnLoad");
    JNI_OnLoad(&jvm);

    // Los stubs de getMac/getIdentifier/Build.MANUFACTURER/Build.MODEL necesitan un
    // JNIEnv válido (jni) para fabricar sus jstring -- eso recién existe después de
    // JNI_OnLoad, así que no se puede hacer en la inicialización estática de java.c.
    java_init_static_strings();

    gl_init();

    // Ciclo de vida real del motor (Gameloft GLGame/GameRenderer), resuelto por nombre
    // ya que el .so no llama a estas funciones por sí solo (las llama la VM de Android
    // normalmente) -- ver PORTING_PLAN.md sección 3 y port_progress.md Fase 4.
    // Orden: dispositivo -> cargador de recursos -> ciclo de vida del juego -> renderer.
    void (* GLUtils_Device_nativeInit)(void *env, void *clazz) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GLUtils_Device_nativeInit");
    void (* GLResLoader_nativeInit)(void *env, void *clazz) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GLResLoader_nativeInit");
    void (* GLGame_nativeInit)(void *env, void *thiz) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeInit");
    void (* GameRenderer_nativeInit)(void *env, void *thiz, jint w, jint h) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GameRenderer_nativeInit");
    void (* GameRenderer_nativeResize)(void *env, void *thiz, jint w, jint h) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GameRenderer_nativeResize");
    void (* GameRenderer_nativeRender)(void *env, void *thiz) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GameRenderer_nativeRender");

    // Input táctil: resueltos pero todavía sin conectar a sceTouch (no hay scaffold de
    // touch en utils/ todavía) -- pendiente, ver port_progress.md.
    void (* GLGame_nativeTouchPressed)(void *env, void *thiz, jint id, jfloat x, jfloat y) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeTouchPressed");
    void (* GLGame_nativeTouchMoved)(void *env, void *thiz, jint id, jfloat x, jfloat y) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeTouchMoved");
    void (* GLGame_nativeTouchReleased)(void *env, void *thiz, jint id, jfloat x, jfloat y) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeTouchReleased");
    (void)GLGame_nativeTouchPressed;
    (void)GLGame_nativeTouchMoved;
    (void)GLGame_nativeTouchReleased;

    // Fix confirmed con so-crash-triage (dump asphalt6-psp2core-1788232196-0x0000752183):
    // GLGame_nativeInit hace "*lockPointer4 = 1;" como su segunda instruccion real (sin
    // chequeo de NULL), pero `lockPointer4` solo se malloc'ea dentro de nativeStart(),
    // parte del flujo real de licencia/DRM (installer.GameInstaller / LicenseCheck) que
    // corre en la Activity "Installer" de Android ANTES de lanzar la Activity del juego.
    // Nuestro main.c salta directo a GLGame_nativeInit sin pasar por ese flujo (correcto:
    // es la edicion offline, no hace falta re-implementar el LVL de verdad), asi que
    // `lockPointer4` queda en 0 (bss) y el store crashea con data abort. Replicamos a
    // mano el unico efecto que a nosotros nos importa de nativeStart(): que el puntero
    // exista y apunte a un entero en 1.
    int **p_lockPointer4 = (int **)so_symbol(&so_mod, "lockPointer4");
    if (p_lockPointer4) {
        int *lock_value = (int *)malloc(sizeof(int));
        *lock_value = 1;
        *p_lockPointer4 = lock_value;
    }

    // Fix confirmado con so-crash-triage (dump asphalt6-psp2core-1788241162-0x00002927af):
    // mismo patron que lockPointer4 arriba, pero con `lockPointer3`. GLGame_nativeInit hace
    // "*lockPointer3 = *lockPointer3 + 1;" tres veces (Ghidra pseudo-C lineas 9458/9461/9472,
    // confirmado con disasm ARM real en libasphalt6.so+0x3cfdc4: "ldr lr,[ip]" con
    // R12(ip)==0x00000000 en los registros del dump). `lockPointer3` solo se malloc'ea dentro
    // del flujo de licencia/DRM (nativeStart(), pseudo-C linea ~9135: "lockPointer3 =
    // malloc(4); *lockPointer3 = 1;") que no corremos (edicion offline). Replicamos ese unico
    // efecto: el puntero existe y apunta a un entero en 1.
    int **p_lockPointer3 = (int **)so_symbol(&so_mod, "lockPointer3");
    if (p_lockPointer3) {
        int *lock_value = (int *)malloc(sizeof(int));
        *lock_value = 1;
        *p_lockPointer3 = lock_value;
    }

    if (GLUtils_Device_nativeInit) GLUtils_Device_nativeInit(&jni, NULL);
    if (GLResLoader_nativeInit) GLResLoader_nativeInit(&jni, NULL);
    if (GLGame_nativeInit) GLGame_nativeInit(&jni, NULL);
    if (GameRenderer_nativeInit) GameRenderer_nativeInit(&jni, NULL, 960, 544);
    if (GameRenderer_nativeResize) GameRenderer_nativeResize(&jni, NULL, 960, 544);

    while (1) {
        if (GameRenderer_nativeRender) GameRenderer_nativeRender(&jni, NULL);
        gl_swap();
    }

    sceKernelExitDeleteThread(0);
}
