#include "utils/breadcrumb.h"
#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/touch.h"
#include "utils/watchdog.h"

#include <stdlib.h>

#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

int _newlib_heap_size_user = 256 * 1024 * 1024;

// Resolución de la pantalla de la Vita. Es también el espacio de coordenadas en el que el
// motor espera el input táctil (DEVICE_SCREEN_WIDTH/HEIGHT los fija appInit desde acá).
#define SCREEN_W 960
#define SCREEN_H 544

// GetDeviceLanguage() del motor; 0 = inglés.
#define GAME_LANGUAGE_ENGLISH 0

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module so_mod;

extern void java_init_static_strings(void);

int main() {
    soloader_init_all();

    // El testigo y las migas van ANTES de JNI_OnLoad: el .so crea sus hilos de trabajo
    // ahi adentro, y si el testigo arranca despues esos hilos nunca quedan registrados
    // (log 015: el volcado final solo mostraba "principal" aunque 3 hilos del .so
    // seguian activos tocando wrappers). Como red de seguridad, bc_push ademas
    // auto-registra todo hilo que toque un wrapper (ver breadcrumb.c).
    // Con la base del .text, las direcciones de retorno que guarda el anillo de migas se
    // imprimen como "libasphalt6.so+0xNNNN" -- el offset que se busca directo en el
    // pseudo-C de Ghidra (decompiled/) para sacar el nombre de la funcion del motor.
    bc_set_base(so_mod.text_base, (uint32_t)so_mod.text_size);
    watchdog_start();
    bc_set_main_tid(sceKernelGetThreadId());
    watchdog_mark("jni_onload", 0);

    int (* JNI_OnLoad)(void *jvm) = (void *)so_symbol(&so_mod, "JNI_OnLoad");
    JNI_OnLoad(&jvm);

    // Los stubs de getMac/getIdentifier/Build.MANUFACTURER/Build.MODEL necesitan un
    // JNIEnv válido (jni) para fabricar sus jstring -- eso recién existe después de
    // JNI_OnLoad, así que no se puede hacer en la inicialización estática de java.c.
    java_init_static_strings();

    gl_init();
    gl_report_mem("tras vglInit");

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
    // Ojo con la firma: nativeInit(env, clazz, width, height, language). El 5to argumento
    // llega hasta appInit() como `mCurrentLanguage` (disasm: appInit(r2, r3, [sp,#48])).
    // Pasar solo 4 argumentos dejaba ese registro con basura de la pila y el motor
    // arrancaba con un índice de idioma indefinido.
    void (* GameRenderer_nativeInit)(void *env, void *thiz, jint w, jint h, jint lang) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GameRenderer_nativeInit");
    void (* GameRenderer_nativeResize)(void *env, void *thiz, jint w, jint h) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GameRenderer_nativeResize");
    void (* GameRenderer_nativeRender)(void *env, void *thiz) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GameRenderer_nativeRender");

    // Input táctil. Firma real (disasm de notifyTouchPress/Moved/Released + el SEvent que
    // arman para glitch::IDevice::postEventFromUser): (env, clazz, jint x, jint y, jint id),
    // enteros -- no floats -- en las coordenadas del "device screen" que le declaramos en
    // nativeInit, o sea 960x544. El 3er argumento de nativeTouchPressed ni se lee (el motor
    // pone ahí su propio flag de doble-tap).
    void (* GLGame_nativeTouchPressed)(void *env, void *thiz, jint x, jint y, jint id) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeTouchPressed");
    void (* GLGame_nativeTouchMoved)(void *env, void *thiz, jint x, jint y, jint id) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeTouchMoved");
    void (* GLGame_nativeTouchReleased)(void *env, void *thiz, jint x, jint y, jint id) =
        (void *)so_symbol(&so_mod, "Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeTouchReleased");

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
    if (GameRenderer_nativeInit)
        GameRenderer_nativeInit(&jni, NULL, SCREEN_W, SCREEN_H, GAME_LANGUAGE_ENGLISH);
    if (GameRenderer_nativeResize) GameRenderer_nativeResize(&jni, NULL, SCREEN_W, SCREEN_H);

    touch_init(GLGame_nativeTouchPressed, GLGame_nativeTouchMoved,
               GLGame_nativeTouchReleased);

    gl_report_mem("tras nativeInit");
    watchdog_mark("bucle principal", 0);

    /*
     * Bucle principal.
     *
     * El motor presenta sus propios frames desde ADENTRO de nativeRender(), llamando al
     * callback JNI `swapEGLBuffers` (glitch::CAndroidOSDevice::flush -> java.c). Eso es
     * imprescindible durante los bucles de carga (Loading::DisplayFrame), donde una sola
     * llamada a nativeRender() puede tardar segundos y dibujar decenas de frames.
     *
     * Pero ese camino solo está activo con `mbIsEnableSwapBuffer != 0`, y esa variable
     * arranca en 0 (.bss) hasta el primer Loading::Start(). Así que acá presentamos el
     * frame nosotros SOLO si el motor no lo hizo, comparando gl_swap_count -- si swapeáramos
     * siempre, cada frame de carga se mostraría dos veces (parpadeo) y se perdería medio
     * frame de trabajo de GPU.
     */
    unsigned int frame = 0;
    while (1) {
        touch_poll();

        unsigned int swaps_before = gl_swap_count;
        if (GameRenderer_nativeRender) {
            // Marcar entrada y salida por separado importa: durante la carga UNA sola
            // llamada a nativeRender puede tardar segundos, asi que "trabado adentro de
            // nativeRender" y "trabado entre dos nativeRender" son dos bugs distintos y
            // el hilo testigo tiene que poder distinguirlos.
            watchdog_mark("nativeRender ENTRA", (int)frame);
            bc_enter("nativeRender", 0);
            GameRenderer_nativeRender(&jni, NULL);
            bc_exit("nativeRender");
            watchdog_mark("nativeRender sale", (int)frame);
        }
        if (gl_swap_count == swaps_before) gl_swap();
        frame++;
    }

    sceKernelExitDeleteThread(0);
}
