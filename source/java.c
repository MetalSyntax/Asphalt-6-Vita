#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>

#include <string.h>

#include <psp2/kernel/processmgr.h>

#include "utils/glutil.h"
#include "utils/logger.h"

/*
 * JNI Methods
*/

/*
 * Methods declarations.
 *
 * Todos los handlers de las tablas Methods*[] de abajo tienen la MISMA firma
 * (jmethodID, va_list) -- son lo que FalsoJNI invoca cuando el .so hace un Call*Method,
 * no nativos JNI. Declararlos como (JNIEnv*, jobject) compilaba con warning y habría
 * leído basura de los registros si el motor llegara a invocarlos.
 */
jboolean GLGame_nativeIsXperia(jmethodID id, va_list args) {
    return JNI_FALSE;
}

jint GLGame_nativeGetLanguageIndex(jmethodID id, va_list args) {
    return 1; // English
}

/*
 * Callbacks resueltos por Java_com_gameloft_android_ANMP_GloftA6HP_GLGame_nativeInit()
 * al arrancar (motor Gameloft real, confirmado con Ghidra sobre libasphalt6.so). FalsoJNI
 * nunca crashea si GetMethodID no encuentra el nombre (loguea un error y devuelve NULL), y
 * un jmethodID no registrado en las tablas Method* de abajo tampoco crashea al invocarlo
 * (methodVoidCall/methodBooleanCall/etc. loguean un warning y devuelven el default seguro)
 * -- ver lib/falso_jni/FalsoJNI_ImplBridge.c. Por eso la mayoría de estos alcanza con un
 * no-op: el motor solo los guarda como jmethodID para usarlos más adelante (background,
 * IAP, tracking, browser, IGP...), no los llama durante nativeInit.
 *
 * Excepción: getMac/getIdentifier SÍ se invocan inmediatamente dentro de nativeInit y su
 * resultado se pasa a GetStringUTFChars() antes de reenviarlo a
 * glot::TrackingManager::appSetGlotIdentifiers() -- tienen que devolver un jstring real
 * (no NULL), por eso se resuelven en java_init_static_strings().
 *
 * Los tipos de retorno de IsWifiEnabled/IsInternetAvaliable/isExternalMusicActive/
 * IsFirmwareBefore22 (boolean) y getVersion/getHostName/getWifiIP (String) son mejor
 * esfuerzo por convención de nombre Java -- FalsoJNI no valida firmas JNI, así que si el
 * motor los invoca con un Call*Method de otro tipo hace falta corregir el arreglo
 * (methodsX[]) correspondiente. Confirmar con so-crash-triage si alguno cuelga o crashea.
 */

static jstring s_glgameMac;
static jstring s_glgameIdentifier;
// getVersion()/getHostName() estan declarados "()[B" en el .so (confirmado leyendo los
// GetStaticMethodID de GLGame_nativeInit con Ghidra + objdump), y nativeGetVersion()/
// nativegetHostName() los consumen con GetArrayLength()+GetByteArrayRegion(). Un jstring
// de FalsoJNI NO es un JavaDynArray, asi que GetArrayLength() devolvia 0 y el motor se
// quedaba con la cadena vacia -- tienen que ser jbyteArray de verdad.
static jbyteArray s_glgameVersion;
static jbyteArray s_glgameHostName;

static jbyteArray java_new_byte_array(const char *str) {
    jsize len = (jsize)strlen(str);
    jbyteArray arr = jni->NewByteArray(&jni, len);
    if (arr) jni->SetByteArrayRegion(&jni, arr, 0, len, (const jbyte *)str);
    return arr;
}

// fieldsObject[] no se puede inicializar con NewStringUTF() en tiempo de compilación
// (jni todavía no existe), así que se completa acá -- llamar una sola vez, después de
// JNI_OnLoad y antes de invocar GLGame_nativeInit (ver main.c).
void java_init_static_strings(void) {
    s_glgameMac = jni->NewStringUTF(&jni, "00:00:00:00:00:00");
    s_glgameIdentifier = jni->NewStringUTF(&jni, "PSVITA-ASPHALT6");
    s_glgameVersion = java_new_byte_array("1.3.3");
    s_glgameHostName = java_new_byte_array("localhost");

    for (size_t i = 0; i < fieldsObject_size() / sizeof(FieldsObject); i++) {
        if (fieldsObject[i].id == 2) fieldsObject[i].value = jni->NewStringUTF(&jni, "sony");
        if (fieldsObject[i].id == 3) fieldsObject[i].value = jni->NewStringUTF(&jni, "pch-1000");
    }
}

jobject GLGame_getMac(jmethodID id, va_list args) {
    return s_glgameMac;
}

jobject GLGame_getIdentifier(jmethodID id, va_list args) {
    return s_glgameIdentifier;
}

jobject GLGame_getVersion(jmethodID id, va_list args) {
    return s_glgameVersion;
}

jobject GLGame_getHostName(jmethodID id, va_list args) {
    return s_glgameHostName;
}

// getWifiIP() esta declarado "()I" en el .so (no String): sin red, 0.0.0.0.
jint GLGame_getWifiIP(jmethodID id, va_list args) {
    return 0;
}

/*
 * Edición offline (Asphalt-6-Adrenaline-v1.3.3-offline.apk): sin red real, se declara
 * todo apagado para que el motor tome el camino sin conexión.
 *
 * OJO con el tipo: los cuatro estan declarados "()I" en el .so y el motor los invoca con
 * CallStaticIntMethod (vtable +0x204), no con CallStaticBooleanMethod -- registrarlos como
 * METHOD_TYPE_BOOLEAN hacia que methodIntCall() no los encontrara ("method ID N not
 * found!") y devolviera 0 por descarte.
 */
jint GLGame_IsWifiEnabled(jmethodID id, va_list args) {
    return 0;
}

jint GLGame_IsInternetAvaliable(jmethodID id, va_list args) {
    return 0;
}

jint GLGame_isExternalMusicActive(jmethodID id, va_list args) {
    return 0;
}

jint GLGame_IsFirmwareBefore22(jmethodID id, va_list args) {
    return 0;
}

/*
 * ESTE es el swap real del motor -- la razón de la pantalla negra hasta el Bug #013.
 *
 * glitch::CAndroidOSDevice::flush() (0x701898 en el .so) hace
 * `CallStaticVoidMethod(env, GameRenderer.class, swapEGLBuffers)`, y GameRenderer_nativeInit
 * es quien resuelve ese jmethodID (junto a getKeyboardText/setKeyboard/isKeyboardVisible).
 * O sea: el .so NUNCA llama eglSwapBuffers -- presenta cada frame por este callback JNI.
 * Como no estaba en la tabla, ningún frame llegaba al display.
 *
 * Además es el ÚNICO camino de swap mientras el motor está adentro de un bucle de carga
 * (Loading::Start() prende mbIsEnableSwapBuffer y Loading::DisplayFrame() dibuja+swapea
 * cada 100 ms): ahí nativeRender() no retorna por varios segundos, así que el swap del
 * bucle principal de main.c no corre. Sin swap, vitaGL nunca cierra la escena de sceGxm ni
 * recicla su circular pool, y el juego terminaba colgado dentro del frame 3 (log 009).
 */
void GameRenderer_swapEGLBuffers(jmethodID id, va_list args) {
    gl_swap();
}

// getKeyboardText()[B / setKeyboard(ILjava/lang/String;I)V / isKeyboardVisible()I: los
// resuelve GameRenderer_nativeInit. Sin teclado virtual en el port, no-ops explícitos
// (registrarlos evita el ruido de "GetStaticMethodID: not found" en el log).
jobject GameRenderer_getKeyboardText(jmethodID id, va_list args) {
    return java_new_byte_array("");
}

void GameRenderer_setKeyboard(jmethodID id, va_list args) {}

jint GameRenderer_isKeyboardVisible(jmethodID id, va_list args) {
    return 0;
}

// nativeExit() -> GLGame.Exit(): el motor pide cerrar la aplicación.
void GLGame_Exit(jmethodID id, va_list args) {
    l_error("El juego pidió salir (GLGame.Exit)");
    sceKernelExitProcess(0);
}

void GLGame_sendAppToBackground(jmethodID id, va_list args) {}
void GLGame_setFullyLoaded(jmethodID id, va_list args) {}
void GLGame_showIAPDialog(jmethodID id, va_list args) {}
void GLGame_launchGetGames(jmethodID id, va_list args) {}
void GLGame_OpenBrowser(jmethodID id, va_list args) {}
void GLGame_OpenGLive(jmethodID id, va_list args) {}
void GLGame_NotifyTrophy(jmethodID id, va_list args) {}
void GLGame_OpenIGP(jmethodID id, va_list args) {}
void GLGame_onLaunchGame1(jmethodID id, va_list args) {}

/*
 * Java_com_gameloft_android_ANMP_GloftA6HP_GLResLoader_nativeInit() resuelve estos 5
 * nombres contra la clase "resource loader" de Java -- a diferencia de los de arriba,
 * Ghidra pudo decompilar esta función entera (no pisó el bug de basename@plt marcado
 * "no retorna", ver PORTING_PLAN.md sección 3), así que la lista está confirmada
 * completa. TODAVÍA NO IMPLEMENTADOS: si el motor usa este camino JNI (en vez de
 * fopen() directo vía reimpl/io.c) para cargar los .dat de ux0:data/asphalt6/data/,
 * hace falta devolver un jbyteArray real con el contenido del recurso pedido -- por
 * ahora devuelven NULL/0 (safe default de FalsoJNI), lo cual va a manifestarse como
 * un crash o assets faltantes en la primera corrida real. Confirmar con so-crash-triage.
 */
jobject GLGame_getResourceFull(jmethodID id, va_list args) { return NULL; }
jobject GLGame_getResourceBytes(jmethodID id, va_list args) { return NULL; }
jint GLGame_getResourceLength(jmethodID id, va_list args) { return 0; }
jobject GLGame_getSoundRaw(jmethodID id, va_list args) { return NULL; }
jint GLGame_getResourceLengthSoundRaw(jmethodID id, va_list args) { return 0; }

NameToMethodID nameToMethodId[] = {
    { 10, "nativeIsXperia", METHOD_TYPE_BOOLEAN },
    { 11, "nativeGetLanguageIndex", METHOD_TYPE_INT },

    { 20, "sendAppToBackground", METHOD_TYPE_VOID },
    { 21, "setFullyLoaded", METHOD_TYPE_VOID },
    { 22, "IsWifiEnabled", METHOD_TYPE_INT },
    { 23, "IsInternetAvaliable", METHOD_TYPE_INT },
    { 24, "isExternalMusicActive", METHOD_TYPE_INT },
    { 25, "IsFirmwareBefore22", METHOD_TYPE_INT },
    { 26, "getMac", METHOD_TYPE_OBJECT },
    { 27, "getIdentifier", METHOD_TYPE_OBJECT },
    { 28, "showIAPDialog", METHOD_TYPE_VOID },
    { 29, "launchGetGames", METHOD_TYPE_VOID },
    { 30, "OpenBrowser", METHOD_TYPE_VOID },
    { 31, "getVersion", METHOD_TYPE_OBJECT },
    { 32, "getHostName", METHOD_TYPE_OBJECT },
    { 33, "OpenGLive", METHOD_TYPE_VOID },
    { 34, "NotifyTrophy", METHOD_TYPE_VOID },
    { 35, "getWifiIP", METHOD_TYPE_INT },
    { 36, "OpenIGP", METHOD_TYPE_VOID },
    { 37, "onLaunchGame1", METHOD_TYPE_VOID },
    { 38, "Exit", METHOD_TYPE_VOID },

    // Resueltos por GameRenderer_nativeInit (jni_GameRenderer.c del motor).
    { 50, "swapEGLBuffers", METHOD_TYPE_VOID },
    { 51, "getKeyboardText", METHOD_TYPE_OBJECT },
    { 52, "setKeyboard", METHOD_TYPE_VOID },
    { 53, "isKeyboardVisible", METHOD_TYPE_INT },

    { 40, "getResourceFull", METHOD_TYPE_OBJECT },
    { 41, "getResourceBytes", METHOD_TYPE_OBJECT },
    { 42, "getResourceLength", METHOD_TYPE_INT },
    { 43, "getSoundRaw", METHOD_TYPE_OBJECT },
    { 44, "getResourceLengthSoundRaw", METHOD_TYPE_INT },
};

MethodsBoolean methodsBoolean[] = {
    { 10, GLGame_nativeIsXperia },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};
MethodsInt methodsInt[] = {
    { 11, GLGame_nativeGetLanguageIndex },
    { 22, GLGame_IsWifiEnabled },
    { 23, GLGame_IsInternetAvaliable },
    { 24, GLGame_isExternalMusicActive },
    { 25, GLGame_IsFirmwareBefore22 },
    { 35, GLGame_getWifiIP },
    { 42, GLGame_getResourceLength },
    { 44, GLGame_getResourceLengthSoundRaw },
    { 53, GameRenderer_isKeyboardVisible },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { 26, GLGame_getMac },
    { 27, GLGame_getIdentifier },
    { 31, GLGame_getVersion },
    { 32, GLGame_getHostName },
    { 40, GLGame_getResourceFull },
    { 41, GLGame_getResourceBytes },
    { 43, GLGame_getSoundRaw },
    { 51, GameRenderer_getKeyboardText },
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { 20, GLGame_sendAppToBackground },
    { 21, GLGame_setFullyLoaded },
    { 28, GLGame_showIAPDialog },
    { 29, GLGame_launchGetGames },
    { 30, GLGame_OpenBrowser },
    { 33, GLGame_OpenGLive },
    { 34, GLGame_NotifyTrophy },
    { 36, GLGame_OpenIGP },
    { 37, GLGame_onLaunchGame1 },
    { 38, GLGame_Exit },
    { 50, GameRenderer_swapEGLBuffers },
    { 52, GameRenderer_setKeyboard },
};

/*
 * JNI Fields
*/

// System-wide constant that applications sometimes request
// https://developer.android.com/reference/android/content/Context.html#WINDOW_SERVICE
char WINDOW_SERVICE[] = "window";

// System-wide constant that's often used to determine Android version
// https://developer.android.com/reference/android/os/Build.VERSION.html#SDK_INT
// Possible values: https://developer.android.com/reference/android/os/Build.VERSION_CODES
const int SDK_INT = 19; // Android 4.4 / KitKat

NameToFieldID nameToFieldId[] = {
		{ 0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT },
		{ 1, "SDK_INT", FIELD_TYPE_INT },
		{ 2, "MANUFACTURER", FIELD_TYPE_OBJECT },
		{ 3, "MODEL", FIELD_TYPE_OBJECT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {
		{ 1, SDK_INT },
};
// MANUFACTURER/MODEL se completan en java_init_static_strings() -- GLGame_nativeInit
// les hace GetStringUTFChars() directo, así que necesitan un jstring real (NewStringUTF),
// no un char* crudo como WINDOW_SERVICE (ese nunca pasa por GetStringUTFChars).
FieldsObject fieldsObject[] = {
		{ 0, WINDOW_SERVICE },
		{ 2, NULL },
		{ 3, NULL },
};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
