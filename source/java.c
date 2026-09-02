#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>

/*
 * JNI Methods
*/

// Methods declarations
jboolean GLGame_nativeIsXperia(JNIEnv *env, jobject thiz) {
    return JNI_FALSE;
}

jint GLGame_nativeGetLanguageIndex(JNIEnv *env, jobject thiz) {
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
static jstring s_glgameVersion;
static jstring s_glgameHostName;

// fieldsObject[] no se puede inicializar con NewStringUTF() en tiempo de compilación
// (jni todavía no existe), así que se completa acá -- llamar una sola vez, después de
// JNI_OnLoad y antes de invocar GLGame_nativeInit (ver main.c).
void java_init_static_strings(void) {
    s_glgameMac = jni->NewStringUTF(&jni, "00:00:00:00:00:00");
    s_glgameIdentifier = jni->NewStringUTF(&jni, "PSVITA-ASPHALT6");
    s_glgameVersion = jni->NewStringUTF(&jni, "1.3.3");
    s_glgameHostName = jni->NewStringUTF(&jni, "localhost");

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

jobject GLGame_getWifiIP(jmethodID id, va_list args) {
    return NULL;
}

// Edición offline (Asphalt-6-Adrenaline-v1.3.3-offline.apk): sin red real, se
// declara todo apagado para que el motor tome el camino sin conexión.
jboolean GLGame_IsWifiEnabled(jmethodID id, va_list args) {
    return JNI_FALSE;
}

jboolean GLGame_IsInternetAvaliable(jmethodID id, va_list args) {
    return JNI_FALSE;
}

jboolean GLGame_isExternalMusicActive(jmethodID id, va_list args) {
    return JNI_FALSE;
}

jboolean GLGame_IsFirmwareBefore22(jmethodID id, va_list args) {
    return JNI_FALSE;
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
    { 22, "IsWifiEnabled", METHOD_TYPE_BOOLEAN },
    { 23, "IsInternetAvaliable", METHOD_TYPE_BOOLEAN },
    { 24, "isExternalMusicActive", METHOD_TYPE_BOOLEAN },
    { 25, "IsFirmwareBefore22", METHOD_TYPE_BOOLEAN },
    { 26, "getMac", METHOD_TYPE_OBJECT },
    { 27, "getIdentifier", METHOD_TYPE_OBJECT },
    { 28, "showIAPDialog", METHOD_TYPE_VOID },
    { 29, "launchGetGames", METHOD_TYPE_VOID },
    { 30, "OpenBrowser", METHOD_TYPE_VOID },
    { 31, "getVersion", METHOD_TYPE_OBJECT },
    { 32, "getHostName", METHOD_TYPE_OBJECT },
    { 33, "OpenGLive", METHOD_TYPE_VOID },
    { 34, "NotifyTrophy", METHOD_TYPE_VOID },
    { 35, "getWifiIP", METHOD_TYPE_OBJECT },
    { 36, "OpenIGP", METHOD_TYPE_VOID },
    { 37, "onLaunchGame1", METHOD_TYPE_VOID },

    { 40, "getResourceFull", METHOD_TYPE_OBJECT },
    { 41, "getResourceBytes", METHOD_TYPE_OBJECT },
    { 42, "getResourceLength", METHOD_TYPE_INT },
    { 43, "getSoundRaw", METHOD_TYPE_OBJECT },
    { 44, "getResourceLengthSoundRaw", METHOD_TYPE_INT },
};

MethodsBoolean methodsBoolean[] = {
    { 10, GLGame_nativeIsXperia },
    { 22, GLGame_IsWifiEnabled },
    { 23, GLGame_IsInternetAvaliable },
    { 24, GLGame_isExternalMusicActive },
    { 25, GLGame_IsFirmwareBefore22 },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};
MethodsInt methodsInt[] = {
    { 11, GLGame_nativeGetLanguageIndex },
    { 42, GLGame_getResourceLength },
    { 44, GLGame_getResourceLengthSoundRaw },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { 26, GLGame_getMac },
    { 27, GLGame_getIdentifier },
    { 31, GLGame_getVersion },
    { 32, GLGame_getHostName },
    { 35, GLGame_getWifiIP },
    { 40, GLGame_getResourceFull },
    { 41, GLGame_getResourceBytes },
    { 43, GLGame_getSoundRaw },
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
