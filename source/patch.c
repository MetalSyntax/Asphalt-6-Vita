/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 * Copyright (C) 2026 Asphalt-6-Vita contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <stdint.h>

#include <kubridge.h>
#include <so_util/so_util.h>

#include "utils/breadcrumb.h"
#include "utils/logger.h"
#include "utils/utils.h"

extern so_module so_mod;

__attribute__((naked, target("arm")))
void hooked_gameswf_root_advance() {
    __asm__ volatile(
        "cmp r0, #0\n"
        "bxeq lr\n"
        ".word 0xe92d4ff0\n" // push {r4-r9, sl, fp, lr}
        ".word 0xed2d8b02\n" // vpush {d8}
        "ldr pc, 1f\n"
        "1: .word 0x985fa06c\n"
    );
}

__attribute__((naked, target("arm")))
void hooked_RenderFX_Find_pt() {
    __asm__ volatile(
        "ldr r0, [r6]\n"
        "mov r1, r5\n"
        "cmp r0, #0\n"
        "bne 1f\n"
        "mov r0, #0\n"
        "pop {r4, r5, r6, r7, r8, pc}\n"
        "1:\n"
        "ldr pc, 2f\n"
        "2: .word 0x98683b00\n"
    );
}

__attribute__((naked, target("arm")))
void hooked_RenderFX_SetTextBufferingEnabled() {
    __asm__ volatile(
        "ldr r3, [r0, #60]\n"
        "cmp r3, #0\n"
        "bxeq lr\n"
        "strb r1, [r3, #133]\n"
        "bx lr\n"
    );
}

__attribute__((naked, target("arm")))
void hooked_RenderFX_SetAutoLoadGlyphsEnabled() {
    __asm__ volatile(
        "ldr r3, [r0, #60]\n"
        "cmp r3, #0\n"
        "bxeq lr\n"
        "strb r1, [r3, #135]\n"
        "bx lr\n"
    );
}

__attribute__((naked, target("arm")))
void hooked_RenderFX_SetRenderCachingEnabled() {
    __asm__ volatile(
        "ldr r3, [r0, #56]\n"
        "cmp r3, #0\n"
        "bxeq lr\n"
        "strb r1, [r3, #152]\n"
        "bx lr\n"
    );
}

/*
 * Diagnostico (bug nuevo tras #024/#025, log 035): Data Abort durante la carga
 * de Bahamas ("Launch Game by PN") con PC dentro de glitch::video::
 * CNullDriver::draw2DLine/getMaxUserClipPlanes (offsets 0x7f4eb0-0x7f4ec8), un
 * driver "no-op" que glitch::CAndroidOSDevice::createDriver() SI puede elegir
 * legitimamente (no es basura) -- pero ninguna instruccion en esa direccion
 * exacta puede dar Data Abort por si sola (ni el ARM "add sp,sp,#8" ni el Thumb
 * "beq.n" tocan memoria), y R10 valia 0xdeadbeef (patron de memoria liberada).
 * Hipotesis sin confirmar: un objeto que deberia usar el driver GLSL real
 * termina con el vtable de CNullDriver (puntero corrupto/liberado). Estos 3
 * hooks loguean el `this` (r0) de cada llamada real durante una corrida contra
 * Bahamas para confirmar CUALES de los 3 se invocan y con que `this` -- no
 * cambian el comportamiento (draw2DLine/getMaxUserClipPlanes son no-ops
 * triviales que se reemplazan 1:1; createBuffer conserva su cuerpo real
 * emulando sus 2 primeras palabras, igual que los demas hook_trace() de este
 * archivo).
 */
/*
 * Log 042 -- confirma que el spam de createBuffer NO es incidental: 1925
 * llamadas cada uno para SOLO 2 `this` (0x81280900/0x812808F8, separados por
 * apenas 8 bytes -- probablemente dos miembros adyacentes del mismo objeto
 * contenedor), sostenidas durante buena parte de la carga de Bahamas y
 * coincidiendo con el bloqueo de 40+ s reportado por el testigo (nativeRender
 * ENTRA #569 sin avanzar). Se agrega el `lr` (direccion de retorno real en
 * libasphalt6.so, capturada ANTES del `bl` a esta funcion) para que el proximo
 * log diga QUE clase/subsistema llama a createBuffer miles de veces -- sin
 * eso, la hipotesis de la sesion anterior ("vtable de CNullDriver en un
 * objeto que no deberia tenerlo") no se puede confirmar ni descartar: hoy solo
 * sabemos que el vtable real de CNullDriver esta puesto a proposito (el hook
 * es sobre la direccion de codigo, no sobre el objeto), pero no si eso es
 * legitimo (p.ej. buffers de colision/fisica, que no se dibujan) o si el auto
 * mismo esta terminando con este driver nulo (lo que explicaria que no se vea
 * el vehiculo en carrera).
 */
// Forward declarations (definidos mas abajo junto a la tabla de hooks): los
// stubs le pasan a cnulldriver_log_this los punteros de estos mismos arrays,
// asi que comparar punteros es exacto y gratis en el bucle caliente.
static const char __attribute__((used)) s_cnd_draw2dline[];
static const char __attribute__((used)) s_cnd_getmaxclip[];
static const char __attribute__((used)) s_cnd_createbuffer[];
void cnulldriver_log_this(const char *method, uint32_t this_ptr, uint32_t caller_lr) {
    // Log 060: durante la carga de pista (Bahamas) createBuffer se llama MILES de
    // veces en un bucle cerrado (llamadores 0x7E77C8/0x7E7F7C alternados) y cada
    // llamada era un sceIoWrite a la SD -- el propio diagnostico frenaba la carga
    // y el menu ingame se rompia con el hilo de render atascado escribiendo el log
    // (frames a +3/5s, strcmp por las nubes). Practica de Rinnegatamante: jamas
    // loguear por-llamada en un bucle caliente. Se loguean las 4 primeras por
    // metodo (ya identifican this/llamador) y despues solo un latido cada 4096.
    static uint32_t n_draw = 0, n_clip = 0, n_buf = 0;
    uint32_t *n = (method == s_cnd_createbuffer) ? &n_buf :
                  (method == s_cnd_getmaxclip) ? &n_clip : &n_draw;
    uint32_t i = (*n)++;
    if (i < 4 || (i % 4096) == 0)
        l_error("[patch] CNullDriver::%s this=0x%08X llamador=libasphalt6.so+0x%X%s",
                method, (unsigned)this_ptr,
                (unsigned)(caller_lr - (uint32_t)so_mod.text_base),
                (i >= 4) ? " (latido, silenciado entremedio)" : "");
}

__attribute__((naked, target("arm")))
void hooked_CNullDriver_draw2DLine() {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "mov r1, r0\n"
        "mov r2, lr\n"
        "ldr r0, 1f\n"
        "bl cnulldriver_log_this\n"
        "pop {r0-r3, r12, lr}\n"
        "bx lr\n"
        "1: .word s_cnd_draw2dline\n"
    );
}

__attribute__((naked, target("arm")))
void hooked_CNullDriver_getMaxUserClipPlanes() {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "mov r1, r0\n"
        "mov r2, lr\n"
        "ldr r0, 1f\n"
        "bl cnulldriver_log_this\n"
        "pop {r0-r3, r12, lr}\n"
        "mov r0, #0\n"
        "bx lr\n"
        "1: .word s_cnd_getmaxclip\n"
    );
}

__attribute__((naked, target("arm")))
void hooked_CNullDriver_createBuffer() {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "mov r1, r0\n"
        "mov r2, lr\n"
        "ldr r0, 1f\n"
        "bl cnulldriver_log_this\n"
        "pop {r0-r3, r12, lr}\n"
        ".word 0xe92d45f0\n" // push {r4, r5, r6, r7, r8, sl, lr}
        ".word 0xe24dd00c\n" // sub sp, sp, #12
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"
        "1: .word s_cnd_createbuffer\n"
        "2: .word g_resume_cnd_createbuffer\n"
    );
}

/*
 * Rastreo ENTER-only del tramo final de MenuScene::MenuScene (log 016).
 *
 * Lo que se sabe del 016: el hilo principal gira en codigo propio tras el swap
 * #12, sin mallocs, sin strstr, sin pthread/GL/archivos durante 12+ s. El tramo
 * sin checkpoints entre ese swap y el DisplayFrame final del constructor es:
 * RemoveChildNodeType -> re-parenting (inline, con strstr: descartado por +0
 * strstr) -> CBatchMesh/CustomBatchGrid/batchNode -> createAnimator ->
 * CLightSceneNode -> DisplayFrame.
 *
 * Estos hooks solo marcan ENTRADA (bc_enter) en el anillo del hilo principal y
 * reanudan la funcion original: la ultima ENTRADA fresca del volcado dice en que
 * llamada (o entre que dos llamadas) se traba, en UNA sola corrida. No hay hooks
 * de SALIDA a proposito: el intervalo entre dos ENTRADAS consecutivas ya acota.
 *
 * Mecanica: hook_arm() pisa los primeros 8 bytes del objetivo con
 * `LDR PC,[PC,#-4]; <stub>`. El stub salva args, llama a bc_enter, restaura,
 * EMULA esos 8 bytes (verbatim si no son PC-relativos, via direccion absoluta
 * precalculada si lo son: r12/r3 son scratch a la entrada y se pueden usar) y
 * salta a text_base+off+8. OJO: el `ldr rX,[pc,#N]` original carga el VALOR del
 * literal (`rX = *(pc+N)`), asi que la emulacion necesita DOBLE indireccion
 * (global -> direccion del literal -> valor). Los literales y reanudaciones se
 * calculan en so_patch() con el text_base real; hook_trace() verifica la
 * primera palabra del prologo antes de parchear para no enganchar basura si el
 * .so cambiara.
 */

// Offsets en archivo (estables: los parches binarios solo cambian instrucciones
// in-situ, nunca mueven codigo) + primera palabra esperada del prologo.
#define OFF_MENUSCENE_C1   0x4423C8u // _ZN9MenuSceneC1EPKc (push+ldr sl,[pc])
#define OFF_MENUSCENE_C2   0x441BC0u // _ZN9MenuSceneC2EPKc (push+ldr sl,[pc])
#define OFF_REMOVECHILD    0x46220Cu // RemoveChildNodeType (push+mov r6,r0)
#define OFF_GRID_CTOR      0x50AA8Cu // CustomBatchGridSceneNode::C2 (push+add)
#define OFF_CREATEANIM     0x50A14Cu // createAnimator(ISceneNode*,char*) (ldr+push)
#define OFF_CLIGHT_CTOR    0x74478Cu // CLightSceneNode::C1 6 args (push+vldr)
#define OFF_DISPLAYFRAME   0x4A4128u // Loading::DisplayFrame (ldr+push)
// Segundo nivel (log 018): el giro esta DENTRO del DisplayFrame final con
// +1850 gettimeofday/s y sin ninguna otra actividad, antes de su swap. Los 5
// son emulacion verbatim (ninguno PC-relativo): sin riesgo de indireccion.
#define OFF_IDEV_RUN       0x85A004u // glitch::IDevice::run (ldr+ldr)
#define OFF_RFX_UPDATE     0x681044u // RenderFX::Update(int,bool) (push+vpush)
#define OFF_RFX_RENDER     0x687CDCu // RenderFX::Render (push+mov)
#define OFF_ENDSCENE_GL    0x913CA8u // CCommonGLDriver::endScene (push+ldr)
#define OFF_ENDSCENE_IV    0x7ED3B0u // IVideoDriver::endScene (push+sub sp)
// Tercer nivel (log 022): los 5 de segundo nivel estan vivos pero `run` nunca
// se entra tras el DisplayFrame final, y el `ra0` del sitio de reloj apunta a
// CCondition::wait (un worker), no al hilo principal. Estos tres distinguen:
// getRealTime dice si el principal sondea el Timer; AfterDF (retorno del
// DisplayFrame FINAL en los dos caminos del ctor, 0x4421C0/0x4429C8: `ldr
// r3,[pc,#-0x1d0]` + `ldr r4,[sp,#0xb4]`, epilogo con `pop {...,pc}`) dice si
// DisplayFrame RETORNO (giro aguas abajo: Stop/audio/estados) o no volvio.
#define OFF_REALTIME       0x857DA0u // glitch::os::Timer::getRealTime (str+sub)
#define OFF_AFTERDF1       0x4421C0u // tras DisplayFrame final, camino 1 (ldr+ldr)
#define OFF_AFTERDF2       0x4429C8u // tras DisplayFrame final, camino 2 (ldr+ldr)
// Bug #019 (log/dump 023): cleanup de std::string con data NULL en el epilogo
// del ctor de MenuScene (los dos caminos): `sub r4,r4,#0xc` (Rep = data-12),
// `bne` al drop, `add r0,r4,#8` (&refcount) + `mvn r1,#0` +
// `bl __exchange_and_add` -> data abort con &refcount = -4 (data NULL).
// La guarda salta al camino "nada que liberar" (0x4421D8/0x4429E0: `mov r0,r6`
// + epilogo + `pop {...,pc}`) cuando data < 4 KB. Sin falsos positivos: el heap
// real vive en 0x81xxxxxx/0x82xxxxxx.
#define OFF_STRDROP1       0x44238Cu // drop camino 1 (add+mvn)
#define OFF_STRDROP2       0x442B94u // drop camino 2 (add+mvn)

/*
 * Bug #020 (log 024): el giro del menu es un `while (*p != carIdx) p++;` SIN COTA
 * en GS_MenuMain::OnLoad3DScene (0x3EED98), justo despues de SortCars():
 *
 *   3ef364  ldr r2,[r5,#0x3c]   ; this->raceCar
 *   3ef368  cmp r2,#0
 *   3ef36c  beq 0x3ef058        ; sin auto: el bloque entero se saltea
 *   3ef370  ldr r3,[r5,#0x44]   ; array = new int[GetCarCount()]  (SIN inicializar)
 *   3ef374  ldr r1,[r2,#0x44]   ; aguja = raceCar->carIdx
 *   3ef378  ldr r2,[r3]
 *   3ef37c  cmp r2,r1
 *   3ef380  beq 0x3ef390
 *   3ef384  ldr r2,[r3,#4]!     ; <-- bucle
 *   3ef388  cmp r2,r1
 *   3ef38c  bne 0x3ef384        ; <-- sin cota
 *   3ef390  mov r3,#0
 *   3ef394  str r3,[r5,#0x48]
 *
 * Firma exacta del cuelgue observado: puros `ldr`+`cmp`, sin malloc, sin
 * strcmp/strstr, sin mutex, sin syscalls, principal CORRIENDO al 100% -- que es
 * literalmente lo que el testigo reporta (`+0 reservas +0 strstr +0 strcmp`) y
 * por eso ninguno de los 15 hooks anteriores lo vio.
 *
 * Por que el array no contiene la aguja: `SortCars()` (0x3EFDD8) solo llena
 * array[0..n-1] con la lista de desbloqueos (`EventManager::GetUnlockList`). Con
 * perfil nuevo esa lista viene vacia, el `operator new[]` no inicializa nada, y
 * la busqueda recorre el heap para siempre. Ademas SortCars ya habia hecho
 * `profile->carId = GetCarInfo(array[0], 0)` sobre esa misma basura: si eso
 * devuelve -1, el perfil sigue en -1 y por eso se entra al bloque.
 *
 * DOS guardas independientes, ambas demostrablemente equivalentes al original:
 *
 * 1. CARSEED (0x3EF014, tras `str r0,[r5,#0x44]`): siembra `array[0] =
 *    raceCar->carIdx` antes de SortCars. Efecto: (a) SortCars calcula
 *    `profile->carId = GetCarInfo(<auto por defecto>)` -- un id valido en vez de
 *    basura, que es justo lo que corresponde a un perfil nuevo; (b) con
 *    profile->carId != -1 el `beq 0x3ef364` de 0x3ef054 NO se toma y el bloque
 *    del bucle ni se ejecuta. Solo escribe si hay RaceCar (this->0x3c != 0), y
 *    eso GARANTIZA que el array mide >= 4 bytes: el RaceCar solo se construye si
 *    `GetCarIdxFromId()` dio un indice valido, o sea GetCarCount() >= 1. Si la
 *    lista de desbloqueos NO estaba vacia, SortCars pisa array[0] igual y la
 *    siembra es inocua.
 *
 * 2. CARFIND (0x3EF378): red de seguridad por si (1) no alcanza. Salta derecho a
 *    0x3EF390. Es equivalencia exacta, no una heuristica: el puntero que el bucle
 *    calcula (r3) se DESCARTA -- 0x3ef390 lo pisa con 0 -- y r1/r2 estan muertos
 *    despues. El unico efecto del bloque es `this->0x48 = 0`, que es lo que
 *    0x3EF390 hace. El bucle es codigo muerto del build original de Gameloft.
 */
#define OFF_CARSEED        0x3EF014u // OnLoad3DScene: str r0,[r5,#0x44] + mov r0,r5
#define OFF_CARFIND        0x3EF378u // OnLoad3DScene: ldr r2,[r3] + cmp r2,r1

/*
 * Bug #021 (log 025) — CAUSA RAIZ REAL del giro. El 025 la acoto sola: la ultima
 * miga del principal es `ENTRA CarSeed` (0x3EF014) y despues nada por 44 s, o sea
 * el giro esta entre esa miga y el siguiente deref. Lo unico sustancial ahi es
 * `bl SortCars` -> `EventManager::GetUnlockList` -> `std::sort`.
 *
 * `SceneHelper::CompareStars(int,int)` (0x462A84) es el comparador de ese sort:
 *
 *   00462ad4  cmp   r4, r0      ; r4 = estrellas(a), r0 = estrellas(b)
 *   00462ad8  movgt r0, #0      ; a >  b -> false
 *   00462adc  movle r0, #1      ; a <= b -> TRUE   <-- devuelve true en IGUALES
 *
 * Eso NO es un strict weak ordering (`comp(x,x)` da true). El
 * `__unguarded_linear_insert` de libstdc++ no tiene chequeo de limite: confia en
 * que algun elemento corte el `while (comp(val, *(i-1))) --i;`. Con `comp` dando
 * true en iguales y TODOS los elementos iguales, se sale del array por delante y
 * recorre el heap para siempre: puros loads + una llamada hoja por vuelta, sin
 * malloc, sin strcmp, sin syscalls. Exactamente la firma observada.
 *
 * Por que son todos iguales: `CompareStars` lee `GetCarInfo(idx, 0x39)`
 * (estrellas), y `BaseCarManager::InitCarMng` (0x48D3DC) llena esos datos leyendo
 * el pack de cada auto con DOS llamadas a `GetPackFile` (0x48D658/0x48D6F8). Con
 * el parche binario del Bug #005 (`mov r0,#0; bx lr`) las dos devolvian NULL, el
 * `beq` saltaba la lectura y los 43 autos quedaban en cero. El bug de Gameloft es
 * latente en Android (datos reales, estrellas distintas); nuestro parche lo
 * convirtio en cuelgue garantizado.
 *
 * Tres correcciones, en orden causal:
 *
 * 1. STARS (0x462AD4): `cmp` + `movlt r0,#1` / `movge r0,#0` y salto al epilogo
 *    (0x462AE0). Convierte `<=` en `<`: mismo orden para elementos distintos, y
 *    ahora si es un strict weak ordering, asi que el sort termina con CUALQUIER
 *    dato. Sin bc_enter a proposito: el sort lo llama O(n log n) veces y
 *    inundaria el anillo de 32 del principal.
 *
 * 2. PACKFILE (0x48DA84) + REVERSION del parche binario del Bug #005. Se
 *    restauraron los 8 bytes originales en `GetPackFile` (0x48D9D8) para que los
 *    autos carguen de verdad, y el deref sin chequeo que crasheaba en el #005
 *    (`0x48DA90: ldr r3,[r4]` con r4 = createAndOpenFile() = NULL) queda cubierto
 *    por una guarda en runtime que salta al camino de "no encontrado" que la
 *    propia funcion ya tiene (0x48DB84: `mov r4,#0` + epilogo propio). Esto es
 *    estrictamente mejor que el parche binario: los autos cuyo pack SI resuelve
 *    ahora cargan, en vez de deshabilitarlos todos.
 *
 * 3. MENUCAR (0x3EF020): red de seguridad aguas abajo. Si aun asi no hay auto por
 *    defecto, `this->raceCar` queda NULL y `0x3EF028: ldr r3,[r3,#0x28]` aborta.
 *    El bloque solo hace `raceCar->node->setName("SelectableMenuCar")`, asi que
 *    con raceCar NULL se salta entero a 0x3EF040. Emula un `ldr` PC-relativo:
 *    doble indireccion (Bug #017).
 */
#define OFF_STARS          0x462AD4u // SceneHelper::CompareStars: cmp + movgt
#define OFF_PACKFILE       0x48DA84u // BaseCarManager::GetPackFile: add r1,sp + mov r5,#0
#define OFF_MENUCAR        0x3EF020u // OnLoad3DScene: ldr r3,[r5,#0x3c] + ldr r1,[pc]

/*
 * Bug #022 (log 026) — diagnostico, no fix. El giro del menu (Bugs #015-#021) ya
 * quedo resuelto: el log 026 llega a "First time launch the app", guarda
 * timespent.dat/pn.dat y ABORTA con `__gnu_cxx::__verbose_terminate_handler`
 * en la pila (confirmado con el .psp2dmp de esa corrida: `__cxa_rethrow` ->
 * `std::terminate` -> `__cxxabiv1::__terminate` -> el terminate handler ->
 * `abort()` -> nuestro `abort_soloader`). O sea: una excepcion C++ real quedo
 * sin capturar -- NO es un NULL deref (nada que "adivinar" con un parche de
 * salto como los Bugs #005-#021). El mensaje de `__verbose_terminate_handler`
 * (`terminate called after throwing an instance of '%s'`) se pierde: va por
 * `fprintf(stderr,...)`/`write(2,...)`, que este loader no redirige a
 * `[ALOG]` como sí hace con `__android_log_print`.
 *
 * `__cxa_throw(void* obj, std::type_info* tinfo, void(*dtor)(void*))` es la
 * ÚNICA función por la que pasa TODO `throw` real del binario (confirmado:
 * está definida DENTRO de libasphalt6.so -- libstdc++/libsupc++ estático, no
 * importada de nuestro dynlib -- así que un solo hook acá ve el origen de
 * cualquier excepción, sin importar en qué catch/rethrow termine). Se
 * engancha su entrada (ENTER-only, como el resto de esta tabla) solo para
 * loguear `tinfo->name()` (offset +4 del `std::type_info` real, confirmado
 * con el layout de Itanium C++ ABI) antes de reanudar sin tocarle el
 * comportamiento -- así el PRÓXIMO log dice qué tipo se lanzó, en vez de
 * tener que adivinar con más parches binarios a ciegas.
 *
 * Primera palabra (`ldr ip,[pc,#144]`) es PC-relativa: doble indireccion como
 * en createAnimator/DisplayFrame/CLightSceneNode (`g_emu_cxathrow` precalculado
 * con el text_base real). Segunda palabra es el push real de la función. r3
 * es scratch seguro para el salto final -- confirmado por disasm que nada
 * antes de su primera reasignación (`ldr r3,[pc,#96]` mas adelante en la
 * función) lee el r3 previo al hook.
 */
#define OFF_CXA_THROW      0xA6FEBCu // __cxa_throw: ldr ip,[pc,#144] + push
#define LIT_CXA_THROW      0xA6FF54u // literal del ldr ip,[pc,#144] (start+8+0x90)

/*
 * Bug #022 (log 028) -- segundo nivel de diagnostico. El 027/028 confirmaron
 * type='St11logic_error' msg='basic_string::_S_construct NULL not valid', el
 * mensaje EXACTO que tira `std::string::_S_construct<char const*>` (0xA6A660)
 * cuando `beg==NULL` (confirmado por disasm: `cmp r0,#0; beq ...; cmp r5,#0
 * (=end); ... bl __throw_logic_error` en 0xa6a680-0xa6a6fc). Ese `_S_construct`
 * es un choke point COMPARTIDO por decenas de sitios (via 4 wrappers de
 * std::string puro -- NO glitch::core::SAllocator, que tiene su propio
 * `_S_construct` en 0x475f08 y no aplica aca), demasiados para auditar a mano.
 *
 * Pero el ctor `std::string(const char*, allocator)` (0xA6A7E8/0xA6A828, C1/C2)
 * hace algo mas especifico: `subs r5,r1,#0; ...; mvneq r1,#0` -- cuando el
 * `const char*` de ENTRADA (r1) es NULL, fuerza `end=-1` (centinela) ANTES de
 * llamar a `_S_construct`, produciendo EXACTAMENTE la combinacion
 * beg=NULL/end!=0 que dispara el throw. O sea: hookear la ENTRADA de este ctor
 * y mirar r1 ahi (antes de que el `subs` lo transforme) da la condicion real
 * SIN falsos positivos, y el LR en ese punto (todavia no pisado por el `push
 * {r4,r5,r6,lr}` original) es la direccion de retorno REAL en codigo del
 * juego -- el llamador que armo el std::string con un char* NULL, sin tener
 * que auditar los 291 sitios que llaman a este ctor.
 *
 * Ninguna de las dos palabras pisadas es PC-relativa (`push` normal + `subs`
 * con inmediato): sin doble indireccion, se emulan verbatim.
 */
#define OFF_SCONSTRUCT     0xA6A7E8u // std::string(const char*, allocator) C1: push+subs
#define OFF_GETLANG        0x4E94F0u // StringManager::GetLanguageString()
#define OFF_SETLANG        0x5A6E78u // StringManager::SetLanguage(const char*)

/*
 * Bug #023: Al entrar a una carrera en circuitos que no sean NewYork(9),
 * Monaco(6), Havana(3) o Moscow(7), TrackScene::LoadLevelGeometry (0x46AF38)
 * antepone "IPAD2a_" al nombre de la pista (0x46B0B8), buscando p.ej. "IPAD2a_Bahamas.bdae".
 * Ese asset no existe en el mapeo de Android (solo existe Bahamas.bdae -> file000554.dat),
 * por lo que CColladaDatabase::constructScene() devuelve NULL (r0=0 -> [fp, #12]=0).
 * En 0x46B1BC, "ldr r3, [r4]" intenta hacer drop() inlined del nodo desreferenciando r4=0,
 * produciendo Data Abort (0x30004).
 *
 * Solucion doble:
 * 1. Parche binario en 0x46AFD8: saltar incondicionalmente a 0x46B114 (b 0x46b114)
 *    para que TODOS los circuitos carguen "<Track>.bdae" sin el prefijo "IPAD2a_".
 * 2. Guarda en 0x46B1B8: si constructScene aun asi devuelve NULL (r4 == 0), omitir
 *    el drop() y SetNodeFogLightingEnabled, saltando directamente a 0x46B1EC (~CColladaDatabase).
 */
#define OFF_LOADGEOM_IPAD2 0x46AFD8u // TrackScene::LoadLevelGeometry: cmp r3, #9
#define W_CMP_R3_9         0xE3530009u
#define B_LOADGEOM_SKIP_IPAD2 0xEA00004Du // b 0x46b114

#define OFF_LOADGEOM_NODE  0x46B1B8u // ldr r4, [fp, #12] + ldr r3, [r4]
#define W_LDR_R4FP12       0xE59B400Cu // ldr r4, [fp, #12]
#define W2_LDR_R3R4        0xE5943000u // ldr r3, [r4]

/*
 * Log 038 — cuelgue antes del menú en `glot::TrackingManager::updateSaveFile`
 * (0x558624): el hilo principal entra a `nativeRender` -> `IDevice::run`, hace
 * los dos `fopen` de tracking (`tracking_data2.dat` rb + `tracking_data1.dat`
 * wb, ambas con éxito en el log) y después gira al 100% sin malloc/strcmp/mutex
 * ni ninguna otra llamada instrumentada. El disasm ARM real muestra el bucle
 * de copia en 0x558814-0x558844:
 *
 *   558804  rsb r5, r5, fp    ; r5 = tamaño - offset (restante)
 *   558810  ble 0x558848      ; si <= 0, salta (bien)
 *   558814  ...               ; prepara fread(buf, 1, 0x19000, src)
 *   558824  bl fread          ; r0 = __n
 *   558828  mov r1, #1
 *   55882c  mov r3, r4
 *   558830  mov r2, r0
 *   558834  rsb r5, r0, r5    ; restante -= __n
 *   558838  mov r0, r6
 *   55883c  bl fwrite
 *   558840  cmp r5, #0
 *   558844  bgt 0x558814      ; <-- sin chequear __n
 *
 * Si `fread` devuelve 0 (EOF/error: offset más allá del fin tras un `fseek`
 * con SEEK_CUR, archivo truncado entre `ftell` y lectura, etc.), `r5` nunca
 * baja y el bucle gira para siempre en `fread(->0)` + `fwrite(0)`: puros
 * loads, sin malloc ni syscalls instrumentados — la firma exacta del 038.
 * En Android es latente (archivos de tracking sanos); acá quedó expuesto tras
 * los 3 POST fallidos a ets.gameloft.com + la rotación de tracking_data.
 *
 * Guarda: si `__n == 0`, se salta el resto de la copia a 0x558848 (el `fflush`
 * del destino). Es equivalencia práctica, no heurística: con EOF no hay más
 * bytes que copiar y el original solo "funcionaba" porque nunca llegaba a
 * ese caso. Avisa en vivo para que el próximo log lo confirme.
 */
#define OFF_TRACKCOPY      0x558828u // updateSaveFile: mov r1,#1 + mov r3,r4 (tras fread)
#define W_MOV_R1_1         0xE3A01001u // mov r1, #1
#define W2_MOV_R3R4        0xE1A03004u // mov r3, r4

/*
 * Log 040 + dump 1789184428 — Data Abort en `GS_Race::StateRender + 0x158`
 * (0x412ebc: `strb r3,[r0,#0x9b]` con r0 = NULL). El `r0` es el retorno de
 * `RenderFX::Find(this, "custom_controls_btn")` (bl en 0x412eb4), usado SIN
 * chequear NULL. El pseudo-C muestra el mismo idioma en TRES sitios de la
 * función (los otros dos con `= 1`): 0x412e90, 0x412ebc (el crash), 0x412f24.
 * El cuarto `Find` de la función (`"component_controls"`, 0x412fxx) SÍ chequea
 * `!= 0` antes de tocar el resultado — o sea, el propio código de Gameloft ya
 * contempla que `Find` devuelva NULL ("elemento no encontrado") y lo salta; los
 * tres sitios son la inconsistencia, no la norma. El elemento falta
 * probablemente por variante de HUD/tipo de control (perfil), no por asset
 * corrupto: `178hud.swf` SÍ cargó en esta corrida.
 *
 * Guarda: si el resultado es NULL, se omite el store del flag 0x9b (un byte de
 * visibilidad) y se sigue en +8, igual que hace el camino chequeado. r0/r3 no
 * viven después (el siguiente `bl` los pisa) y los flags mueren en el `bl`;
 * r12 es scratch libre (el `bl Find` previo puede pisar ip por AAPCS y nadie
 * lo lee hasta el próximo `bl`). Avisa en vivo con el sitio para que el log
 * diga cuál de los tres mordió.
 */
#define OFF_STATER1        0x412E8Cu // StateRender: mov r3,#1 + strb r3,[r0,#0x9b] (=1)
#define OFF_STATER0        0x412EB8u // StateRender: mov r3,#0 + strb r3,[r0,#0x9b] (=0, crash 040)
#define OFF_STATER2        0x412F20u // StateRender: mov r3,#1 + strb r3,[r0,#0x9b] (=1)
#define W_MOV_R3_1         0xE3A03001u // mov r3, #1
#define W_MOV_R3_0         0xE3A03000u // mov r3, #0
#define W2_STRB_R3R0_9B   0xE5C0309Bu // strb r3, [r0, #0x9b]

#define OFF_IGM_VIS        0x418AC4u // IGMUpdate: movw r1,#134 + ldrb r3,[r7,#0x9b] (crash 041)
#define W_MOVW_R1_86       0xE3001086u // movw r1, #134
#define W2_LDRB_R3R7_9B    0xE5D7309Bu // ldrb r3, [r7, #0x9b]

/*
 * Bug #033 (log 052 + dump 1789608605) — Data abort en
 * BaseCarManager::GetPackFilename(int) al elegir un auto (Nissan 370Z Nismo 2010)
 * en el garage/tuning. Disasm ARM real confirmado con objdump + lectura de bytes
 * cruda (sin adivinar el layout de instrucciones, a diferencia de thisAppendBatch):
 *
 *   48d364: ldr r4, [r3, r2]   ; r4 = this->packNames[index] (un char* de std::string)
 *   48d368: sub r4, r4, #12   ; r4 = &_Rep (data_ptr - sizeof(_Rep), COW libstdc++)
 *   48d36c: ldr r3, [r4, #8]  ; CRASH: lee _Rep::_M_refcount -- r4 = 0xFFFFFFF4 (=-12)
 *
 * O sea: this->packNames[index] es un std::string CRUDO (memoria en cero, nunca
 * construido) para este auto/indice -- un char* NULL no es un string vacio valido
 * bajo COW (eso apuntaria al singleton _S_empty_rep_storage, nunca a 0). Distinto
 * del Bug #021/PACKFILE (createAndOpenFile() devuelve NULL porque el .pak no abre):
 * este crash es ANTES, al armar el nombre del archivo, no al abrirlo -- mismo auto
 * (u otro con el mismo indice de pack) tiene la entrada de nombre directamente sin
 * poblar. Sigue sin confirmarse POR QUE ese indice queda sin poblar para este auto
 * en particular (candidato: InitCarMng solo llena `packNames` para los tipos de
 * pack que ese auto realmente usa, y este call site pide un indice que no aplica
 * -- necesitaria RE de GetPackFile/InitCarMng para confirmarlo).
 *
 * Fix: si el string cargado es NULL, se devuelve el string vacio INMORTAL real del
 * motor (mismo allocator, `glitch::core::SAllocator<char,...>::_Rep::_S_empty_rep_storage`,
 * resuelto por simbolo en so_patch() -- no un literal nuestro) en vez de desreferenciar
 * el puntero armado con -12. Replica el mismo camino de retorno que la funcion ya
 * usa para el string vacio real (0x48D388-0x48D398), asi que el resultado es un
 * std::string 100% valido para quien lo use despues (ToString, fopen, etc.), no
 * solo "no crashea".
 */
#define OFF_GETPACKFILENAME 0x48D364u // GetPackFilename: ldr r4,[r3,r2] + sub r4,r4,#12
#define W_LDR_R4R3R2       0xE7934002u // ldr r4, [r3, r2]
#define W2_SUB_R4_12       0xE244400Cu // sub r4, r4, #12

#define W_PUSH9  0xe92d4ff0u // push {r4-r9, sl, fp, lr}
#define W_PUSH6a 0xe92d41f0u // push {r4-r8, lr}
#define W_PUSH8  0xe92d47f0u // push {r4-r9, sl, lr}
#define W_LDR_R3a 0xe59f3030u // ldr r3, [pc, #48]  (createAnimator)
#define W_LDR_R3b 0xe59f3114u // ldr r3, [pc, #276] (DisplayFrame)
#define W_LDR_IP 0xe590c0c0u // ldr ip, [r0, #192] (IDevice::run)
#define W_PUSH3  0xe92d4030u // push {r4, r5, lr}   (RenderFX::Render)
#define W_PUSH1  0xe92d4010u // push {r4, lr}       (endScene x2)
#define W_STR_LR 0xe52de004u // str lr, [sp, #-4]!  (getRealTime)
#define W_LDR_R3c 0xe51f31d0u // ldr r3, [pc, #-464] (retorno DisplayFrame final)
#define W_ADD_R0 0xe2840008u // add r0, r4, #8 (drop de string, Bug #019)
#define W_STR_R0_44 0xe5850044u // str r0, [r5, #68]  (siembra del array de autos)
#define W_LDR_R2R3  0xe5932000u // ldr r2, [r3]      (busqueda sin cota de autos)
#define W_CMP_R4R0  0xe1540000u // cmp r4, r0        (CompareStars)
#define W_ADD_R1SP  0xe28d1018u // add r1, sp, #24   (GetPackFile)
#define W_LDR_R3_3C 0xe595303cu // ldr r3, [r5, #60] (OnLoad3DScene: raceCar)
#define W_LDR_IP_90 0xe59fc090u // ldr ip, [pc, #144] (__cxa_throw)
#define W_PUSH_CXA  0xe92d48f0u // push {r4,r5,r6,r7,fp,lr} (__cxa_throw)
#define W_PUSH456LR 0xe92d4070u // push {r4,r5,r6,lr} (std::string ctor const char*)
#define W_SUBS_R5R1 0xe2515000u // subs r5, r1, #0    (std::string ctor const char*)

/*
 * SEGUNDA palabra de cada objetivo, verificada en hook_trace().
 *
 * Por que existe esto: hook_trace() solo miraba la PRIMERA palabra, y los 8
 * bytes que el trampolin pisa son DOS instrucciones -- la segunda se emula a
 * mano en el stub. Esa mano ya falló dos veces, y las dos veces el sintoma fue
 * un crash en consola en vez de un mensaje: Bug #017 (un `ldr` PC-relativo
 * emulado sin la doble indireccion) y el log 023 (`ldr r4,[sp,#0xb4]` escrito
 * como `0xe59d4b40`, o sea `[sp,#0xb40]` -- inmediato transpuesto: r4 quedo en
 * basura, el ctor calculo `0-12+8` y `__exchange_and_add(0xFFFFFFFC)` abortó).
 * Con la palabra real declarada al lado del stub, un encoding mal escrito o un
 * .so distinto se cazan al arrancar y ese hook simplemente no se instala.
 *
 * Valores tomados del `.so` real con objdump (auditados uno por uno).
 */
#define W2_LDR_SL    0xe59fa410u // ldr sl, [pc, #1040]  (MenuScene C1/C2)
#define W2_MOV_R6R0  0xe1a06000u // mov r6, r0           (RemoveChildNodeType)
#define W2_ADD_R6    0xe2816004u // add r6, r1, #4       (CustomBatchGrid C2)
#define W2_PUSH_ANIM 0xe92d4070u // push {r4-r6, lr}     (createAnimator)
#define W2_VLDR_S14  0xed9f7a7au // vldr s14, [pc, #488] (CLightSceneNode C1)
#define W2_LDR_R3_D0 0xe59030d0u // ldr r3, [r0, #208]   (IDevice::run: se REEMPLAZA a proposito)
#define W2_VPUSH_D8  0xed2d8b02u // vpush {d8}           (RenderFX::Update)
#define W2_MOV_R4R0  0xe1a04000u // mov r4, r0           (RenderFX::Render)
#define W2_LDR_R3R0  0xe5903000u // ldr r3, [r0]         (CCommonGLDriver::endScene)
#define W2_SUB_SP8   0xe24dd008u // sub sp, sp, #8       (IVideoDriver::endScene)
#define W2_SUB_SP12  0xe24dd00cu // sub sp, sp, #12      (getRealTime)
#define W2_LDR_R4SP  0xe59d40b4u // ldr r4, [sp, #180]   (retorno DisplayFrame final)
#define W2_MVN_R1    0xe3e01000u // mvn r1, #0            (drop de string, Bug #019)
#define W2_MOV_R0R5  0xe1a00005u // mov r0, r5           (siembra del array de autos)
#define W2_CMP_R2R1  0xe1520001u // cmp r2, r1           (busqueda sin cota de autos)
#define W2_MOVGT_R0  0xc3a00000u // movgt r0, #0         (CompareStars: se REEMPLAZA)
#define W2_MOV_R5_0  0xe3a05000u // mov r5, #0           (GetPackFile)
#define W2_LDR_R1PC  0xe59f1434u // ldr r1, [pc, #1076]  (OnLoad3DScene: PC-relativo)
#define W2_LDR_R3_48 0xe5903030u // ldr r3, [r0, #48]    (StringManager::GetLanguageString)
#define W2_SUB_SP68  0xe24dd044u // sub sp, sp, #68      (StringManager::SetLanguage)

// NOTA (log 061, build Release): estos simbolos solo se referencian desde
// strings de basic-asm opaco (".word s_tr_x"), invisibles para el analisis de
// liveness de GCC -- con -O3 los elimina y el link falla con undefined
// reference (en Debug/-O0 si se emiten, por eso antes linkeaba). 'used' fuerza
// su emision en cualquier nivel de optimizacion.
__attribute__((used))
static uint32_t g_resume_c1, g_resume_c2, g_resume_rm, g_resume_grid,
                g_resume_anim, g_resume_light, g_resume_frame,
                g_resume_run, g_resume_update, g_resume_render,
                g_resume_endgl, g_resume_endiv,
                g_resume_rt, g_resume_dfret1, g_resume_dfret2,
                g_resume_strdrop1, g_resume_strdrop2,
                g_skip_strdrop1, g_skip_strdrop2,
                g_resume_carseed, g_resume_carfind, g_skip_carfind,
                g_resume_stars, g_skip_stars,
                g_resume_packfile, g_skip_packfile,
                g_resume_menucar, g_skip_menucar, g_emu_menucar,
                g_resume_cxathrow, g_resume_sconstruct,
                g_resume_getlang, g_resume_setlang,
                g_resume_loadgeom, g_skip_loadgeom,
                g_resume_trackcopy, g_skip_trackcopy,
                g_resume_sr0, g_resume_sr1, g_resume_sr2,
                g_resume_igm, g_skip_igm,
                g_resume_cnd_createbuffer,
                g_resume_getpackfilename;
__attribute__((used))
static uint32_t g_emu_c1, g_emu_c2, g_emu_anim, g_emu_light, g_emu_frame,
                g_emu_dfret1, g_emu_dfret2, g_emu_cxathrow;
// Puntero de datos del string vacio inmortal del motor (mismo allocator que
// BaseCarManager::packNames), resuelto por simbolo en so_patch() -- ver Bug #033.
__attribute__((used))
static uint32_t g_empty_rep_data;

static const char __attribute__((used)) s_tr_c1[] = "MenuScene::MenuScene";
static const char __attribute__((used)) s_tr_rm[] = "RemoveChildNodeType";
static const char __attribute__((used)) s_tr_grid[] = "CustomBatchGrid";
static const char __attribute__((used)) s_tr_anim[] = "createAnimator";
static const char __attribute__((used)) s_tr_light[] = "CLightSceneNode";
static const char __attribute__((used)) s_tr_frame[] = "DisplayFrame";
static const char __attribute__((used)) s_tr_run[] = "IDevice::run";
static const char __attribute__((used)) s_tr_update[] = "RenderFX::Update";
static const char __attribute__((used)) s_tr_render[] = "RenderFX::Render";
static const char __attribute__((used)) s_tr_endgl[] = "endScene-GL";
static const char __attribute__((used)) s_tr_endiv[] = "endScene-IV";
static const char __attribute__((used)) s_cnd_draw2dline[] = "draw2DLine";
static const char __attribute__((used)) s_cnd_getmaxclip[] = "getMaxUserClipPlanes";
static const char __attribute__((used)) s_cnd_createbuffer[] = "createBuffer";
static const char __attribute__((used)) s_tr_rt[] = "getRealTime";
static const char __attribute__((used)) s_tr_dfret[] = "AfterDF";
static const char __attribute__((used)) s_tr_strdrop[] = "StrDrop";
static const char __attribute__((used)) s_tr_carseed[] = "CarSeed";
static const char __attribute__((used)) s_tr_carfind[] = "CarFind";
static const char __attribute__((used)) s_tr_packfile[] = "PackFileNull";
static const char __attribute__((used)) s_tr_menucar[] = "MenuCarNull";
static const char __attribute__((used)) s_tr_trackcopy[] = "TrackCopy";
static const char __attribute__((used)) s_tr_staterender[] = "StateRenderNull";
static const char __attribute__((used)) s_tr_getpackfilename[] = "PackFilenameNull";

// Bug #019: aviso en vivo cuando la guarda omite un drop (raro: una vez por
// corrida como mucho, sin costo de timing).
void strdrop_skipped(uint32_t data, uint32_t site) {
    l_error("[patch] StrDrop: data=0x%08X en camino %u, drop omitido (Bug #019)",
            (unsigned)data, (unsigned)site);
}

// Plantilla push+push (RemoveChildNodeType, CustomBatchGrid C2): los 8 bytes
// desplazados se copian tal cual porque ninguno es PC-relativo.
__attribute__((naked, target("arm")))
static void hook_rm(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        ".word 0xe92d41f0\n" // push {r4-r8, lr}
        "mov r6, r0\n"
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_rm\n"
        "3: .word g_resume_rm\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_grid(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        ".word 0xe92d47f0\n" // push {r4-r9, sl, lr}
        "add r6, r1, #4\n"
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_grid\n"
        "3: .word g_resume_grid\n"
    );
}

// Plantilla push + ldr-pc (MenuScene C1/C2, CLight C1): el ldr se emula via la
// direccion absoluta del literal, precalculada con text_base (r12 es scratch).
__attribute__((naked, target("arm")))
static void hook_c1(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        ".word 0xe92d4ff0\n" // push {r4-r9, sl, fp, lr}
        "ldr r12, 2f\n"
        "ldr r12, [r12]\n"   // r12 = direccion del literal
        "ldr sl, [r12]\n"    // ldr sl, [pc, #1040] (el VALOR, doble indireccion)
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_c1\n"
        "2: .word g_emu_c1\n"
        "3: .word g_resume_c1\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_c2(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        ".word 0xe92d4ff0\n" // push {r4-r9, sl, fp, lr}
        "ldr r12, 2f\n"
        "ldr r12, [r12]\n"   // r12 = direccion del literal
        "ldr sl, [r12]\n"    // ldr sl, [pc, #1040] (el VALOR, doble indireccion)
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_c1\n"
        "2: .word g_emu_c2\n"
        "3: .word g_resume_c2\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_light(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        ".word 0xe92d4ff0\n" // push {r4-r9, sl, fp, lr}
        "ldr r12, 2f\n"
        "ldr r12, [r12]\n"   // r12 = direccion del literal
        "vldr s14, [r12]\n"  // vldr s14, [pc, #488] (el VALOR, doble indireccion)
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_light\n"
        "2: .word g_emu_light\n"
        "3: .word g_resume_light\n"
    );
}

// Plantilla ldr-pc + push (createAnimator, DisplayFrame): r3 es scratch a la
// entrada (2 args o ninguno), asi que emular su ldr es seguro.
__attribute__((naked, target("arm")))
static void hook_anim(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 2f\n"
        "ldr r12, [r12]\n"   // r12 = direccion del literal
        "ldr r3, [r12]\n"    // ldr r3, [pc, #48] (el VALOR, doble indireccion)
        ".word 0xe92d4070\n" // push {r4-r6, lr}
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_anim\n"
        "2: .word g_emu_anim\n"
        "3: .word g_resume_anim\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_frame(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 2f\n"
        "ldr r12, [r12]\n"   // r12 = direccion del literal
        "ldr r3, [r12]\n"    // ldr r3, [pc, #276] (el VALOR, doble indireccion)
        ".word 0xe92d41f0\n" // push {r4-r8, lr}
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_frame\n"
        "2: .word g_emu_frame\n"
        "3: .word g_resume_frame\n"
    );
}

// Segundo nivel: emulacion verbatim (los 8 bytes desplazados no dependen del PC,
// se copian tal cual). r0 es `this` (preservado por push/pop); r1-r3 scratch.
//
// EXCEPCION (Bug #018): hook_run NO emula el `ldr r3,[r0,#208]` original: fuerza
// r3 = ip para que el `cmp r3,ip / beq` posterior tome SIEMPRE el camino de "cola
// vacia". Esa cola (`std::deque<SEvent>` en this+0xB8) solo la llena
// postEventFromUser(...,true), y TODOS los llamadores del .so pasan false
// (notifyTouch*, el propio run(), postMouseEventFromUser no se usa en este port:
// verificado por disasm) -- en este port la cola esta vacia por construccion y
// el camino `beq` es el normal. Entrar al drenado con punteros basura = data
// abort en `ldm lr!,{r0-r3}` (log 019). El despacho directo (touch) no toca la
// cola y sigue funcionando.
//
// REGRESION (Bug #018, log 020): la primera version de este stub usaba r12
// como scratch para el salto final (`ldr r12,3f; ldr pc,[r12]`), pero r12 ES
// ip -- lo pisaba justo despues de forzar `r3 = ip`, asi que en la reanudacion
// (`cmp r3,ip` real, dentro del .so) ip ya no valia lo mismo que r3 (valia la
// DIRECCION de `g_resume_run`) y el `beq` nunca se tomaba: el giro seguia
// entrando al drenado igual, ahora dereferenciando esa direccion en vez del
// puntero real de la cola (mismo sintoma exacto: `ldm lr!,{r0-r3}` con LR/R7
// apuntando justo a `g_resume_run`/`+0x18`, confirmado byte a byte contra el
// dump de la 020). Fix: usar r2 como scratch para el salto (confirmado por
// disasm que el camino de salida del `beq`, offsets 0x74-0xa0 de la funcion,
// no lee r2 en ningun punto) y dejar r12/ip intacto desde el `mov r3,ip` hasta
// el `cmp r3,ip` real de la reanudacion.
__attribute__((naked, target("arm")))
static void hook_run(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr ip, [r0, #192]\n"
        "mov r3, ip\n"       // Bug #018: cola siempre vacia (NO el ldr original)
        "ldr r2, 3f\n"       // r2 (NO r12): no pisar el ip recien forzado
        "ldr pc, [r2]\n"
        "1: .word s_tr_run\n"
        "3: .word g_resume_run\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_update(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        ".word 0xe92d4ff0\n" // push {r4-r9, sl, fp, lr}
        ".word 0xed2d8b02\n" // vpush {d8}
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_update\n"
        "3: .word g_resume_update\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_render(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        ".word 0xe92d4030\n" // push {r4, r5, lr}
        "mov r4, r0\n"
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_render\n"
        "3: .word g_resume_render\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_endgl(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        ".word 0xe92d4010\n" // push {r4, lr}
        "ldr r3, [r0]\n"
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_endgl\n"
        "3: .word g_resume_endgl\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_endiv(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        ".word 0xe92d4010\n" // push {r4, lr}
        "sub sp, sp, #8\n"
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_endiv\n"
        "3: .word g_resume_endiv\n"
    );
}

// Tercer nivel (log 022): getRealTime es verbatim (str+sub, nada PC-relativo).
__attribute__((naked, target("arm")))
static void hook_realtime(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        ".word 0xe52de004\n" // str lr, [sp, #-4]!
        ".word 0xe24dd00c\n" // sub sp, sp, #12
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_rt\n"
        "3: .word g_resume_rt\n"
    );
}

// Retorno del DisplayFrame final (los dos caminos del ctor): el `ldr r3`
// PC-relativo se emula con doble indireccion (r3 es scratch tras un retorno
// void) y el `ldr r4,[sp,#0xb4]` va verbatim.
__attribute__((naked, target("arm")))
static void hook_dfret1(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 2f\n"
        "ldr r12, [r12]\n"   // r12 = direccion del literal
        "ldr r3, [r12]\n"    // ldr r3, [pc, #-464] (el VALOR, doble indireccion)
        ".word 0xe59d40b4\n" // ldr r4, [sp, #0xb4] (OJO: 0x0b4, no 0xb40 -- ver cabecera)
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_dfret\n"
        "2: .word g_emu_dfret1\n"
        "3: .word g_resume_dfret1\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_dfret2(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 2f\n"
        "ldr r12, [r12]\n"   // r12 = direccion del literal
        "ldr r3, [r12]\n"    // ldr r3, [pc, #-464] (el VALOR, doble indireccion)
        ".word 0xe59d40b4\n" // ldr r4, [sp, #0xb4] (OJO: 0x0b4, no 0xb40 -- ver cabecera)
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_dfret\n"
        "2: .word g_emu_dfret2\n"
        "3: .word g_resume_dfret2\n"
    );
}

// Bug #019: guarda del drop de string (los dos caminos del epilogo del ctor).
// Emula `add r0,r4,#8` + `mvn r1,#0`, pero si data (= r0+4) cae bajo 4 KB es un
// puntero bogus (NULL visto en el dump 023: &refcount = -4) y se salta al camino
// "nada que liberar" avisando en vivo. r12 es scratch en el borde (las 8
// palabras originales no lo leen) y los flags mueren en el `bl`/salto.
__attribute__((naked, target("arm")))
static void hook_strdrop1(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        "add r0, r4, #8\n"     // emu: r0 = &refcount
        "add r12, r0, #4\n"    // r12 = data candidata
        "cmp r12, #0x1000\n"
        "blo 2f\n"
        "mvn r1, #0\n"         // emu: drop real
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "2:\n"
        "mov r0, r12\n"        // arg1 = data bogus
        "mov r1, #1\n"         // arg2 = camino 1
        "bl strdrop_skipped\n"
        "ldr r12, 4f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_strdrop\n"
        "3: .word g_resume_strdrop1\n"
        "4: .word g_skip_strdrop1\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_strdrop2(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        "add r0, r4, #8\n"     // emu: r0 = &refcount
        "add r12, r0, #4\n"    // r12 = data candidata
        "cmp r12, #0x1000\n"
        "blo 2f\n"
        "mvn r1, #0\n"         // emu: drop real
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "2:\n"
        "mov r0, r12\n"        // arg1 = data bogus
        "mov r1, #2\n"         // arg2 = camino 2
        "bl strdrop_skipped\n"
        "ldr r12, 4f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_strdrop\n"
        "3: .word g_resume_strdrop2\n"
        "4: .word g_skip_strdrop2\n"
    );
}

// Bug #020: avisos en vivo (una vez por carga de menu, sin costo de timing).
void carseed_applied(uint32_t race_car, uint32_t array) {
    l_error("[patch] CarSeed: raceCar=0x%08X array=0x%08X (Bug #020/#021)",
            (unsigned)race_car, (unsigned)array);
}

void packfile_null(void) {
    l_error("[patch] PackFileNull: createAndOpenFile devolvio NULL, retorno limpio (Bug #021)");
}

void menucar_null(void) {
    l_error("[patch] MenuCarNull: no hay auto por defecto, se saltea setName (Bug #021)");
}

void carfind_skipped(void) {
    l_error("[patch] CarFind: busqueda sin cota omitida -- la siembra no alcanzo (Bug #020)");
}

/*
 * Bug #020, guarda 1: siembra array[0] = raceCar->carIdx antes de SortCars().
 * Emula `str r0,[r5,#0x44]` + `mov r0,r5`. r12 es scratch en el borde (viene de
 * tres `bl` seguidos) y los flags mueren en el `bl SortCars` de 0x3EF01C.
 */
__attribute__((naked, target("arm")))
static void hook_carseed(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "pop {r0-r3, r12, lr}\n"
        "str r0, [r5, #0x44]\n"   // emu: this->carArray = new[]
        "ldr r12, [r5, #0x3c]\n"  // RaceCar* (0 si no se construyo)
        "cmp r12, #0\n"
        "cmpne r0, #0\n"
        "beq 2f\n"                // sin array o sin RaceCar: nada que sembrar
        "ldr r12, [r12, #0x44]\n" // aguja = raceCar->carIdx
        "str r12, [r0]\n"         // array[0] = aguja (>=4 bytes garantizados)
        "2:\n"
        "push {r0-r3, r12, lr}\n" // log SIEMPRE: distingue array NULL de raceCar NULL
        "mov r1, r0\n"
        "ldr r0, [r5, #0x3c]\n"
        "bl carseed_applied\n"
        "pop {r0-r3, r12, lr}\n"
        "mov r0, r5\n"            // emu: r0 = this (arg de SortCars)
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_carseed\n"
        "3: .word g_resume_carseed\n"
    );
}

/*
 * Bug #020, guarda 2: saltea la busqueda sin cota. NO reanuda -- salta a
 * 0x3EF390 (`mov r3,#0` + `str r3,[r5,#0x48]`), que es el unico efecto real del
 * bloque: el puntero que el bucle calcula se descarta ahi mismo. r5 queda
 * intacto (bc_enter preserva r4-r11 por AAPCS).
 */
__attribute__((naked, target("arm")))
static void hook_carfind(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_enter\n"
        "bl carfind_skipped\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_carfind\n"
        "2: .word g_skip_carfind\n"
    );
}

/*
 * Bug #021, guarda 1: convierte CompareStars en un strict weak ordering.
 * Reemplaza `cmp r4,r0` + `movgt r0,#0` (y saltea el `movle r0,#1` de 0x462ADC)
 * saltando directo al epilogo 0x462AE0 (`pop {r4,r5,r6,pc}`). r12 es scratch: la
 * funcion esta por hacer su pop. SIN bc_enter: el sort lo llama O(n log n) veces.
 */
__attribute__((naked, target("arm")))
static void hook_stars(void) {
    __asm__ volatile(
        "cmp r4, r0\n"
        "movlt r0, #1\n"          // a <  b -> true
        "movge r0, #0\n"          // a >= b -> false (antes: true en iguales)
        "ldr r12, 1f\n"
        "ldr pc, [r12]\n"
        "1: .word g_skip_stars\n"
    );
}

/*
 * Bug #021, guarda 2: NULL de createAndOpenFile() en GetPackFile (el crash del
 * Bug #005, ahora que el parche binario se revirtio). Emula `add r1,sp,#0x18` +
 * `mov r5,#0`; si r4 es NULL salta al camino de "no encontrado" que la propia
 * funcion ya tiene (0x48DB84), que hace su propio epilogo.
 */
__attribute__((naked, target("arm")))
static void hook_packfile(void) {
    __asm__ volatile(
        "add r1, sp, #0x18\n"     // emu
        "mov r5, #0\n"            // emu
        "cmp r4, #0\n"
        "bne 2f\n"
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_event\n"
        "bl packfile_null\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 4f\n"
        "ldr pc, [r12]\n"         // -> 0x48DB84 (mov r4,#0 + epilogo propio)
        "2:\n"
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
        "1: .word s_tr_packfile\n"
        "3: .word g_resume_packfile\n"
        "4: .word g_skip_packfile\n"
    );
}

/*
 * Bug #021, guarda 3: raceCar NULL en OnLoad3DScene. El bloque solo hace
 * raceCar->node->setName("SelectableMenuCar"); con raceCar NULL se saltea entero
 * a 0x3EF040. La segunda palabra emulada es `ldr r1,[pc,#0x434]`: DOBLE
 * indireccion (global -> direccion del literal -> valor), ver Bug #017.
 */
__attribute__((naked, target("arm")))
static void hook_menucar(void) {
    __asm__ volatile(
        "ldr r3, [r5, #0x3c]\n"   // emu: r3 = this->raceCar
        "cmp r3, #0\n"
        "beq 2f\n"
        "ldr r12, 3f\n"           // emu: ldr r1,[pc,#0x434] (doble indireccion)
        "ldr r12, [r12]\n"
        "ldr r1, [r12]\n"
        "ldr r12, 4f\n"
        "ldr pc, [r12]\n"         // resume 0x3EF028
        "2:\n"
        "push {r0-r3, r12, lr}\n"
        "ldr r0, 1f\n"
        "mov r1, #0\n"
        "bl bc_event\n"
        "bl menucar_null\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 5f\n"
        "ldr pc, [r12]\n"         // -> 0x3EF040
        "1: .word s_tr_menucar\n"
        "3: .word g_emu_menucar\n"
        "4: .word g_resume_menucar\n"
        "5: .word g_skip_menucar\n"
    );
}

/*
 * Bug #022: log del tipo Y MENSAJE reales de CUALQUIER excepcion C++ lanzada
 * por el .so. `exc_obj`/`tinfo` son el primer/segundo argumento de
 * `__cxa_throw` (r0/r1 a la entrada, ya en el orden correcto para pasarlos
 * directo a esta funcion en C -- ver AAPCS).
 *
 * El log 027 confirmo `type='St11logic_error'` -- un `std::logic_error` PLANO
 * (no una subclase como `out_of_range`/`length_error`/`invalid_argument`, que
 * tienen su propio `type_info`), y hay **57** sitios en el .so que llaman a
 * `_ZSt19__throw_logic_errorPKc` (grep sobre el disasm ARM completo) --
 * demasiados para adivinar cual sin mas datos. Pero el objeto excepcion YA
 * esta construido cuando se llama a `__cxa_throw` (el compilador hace
 * `__cxa_allocate_exception` + placement-new + `__cxa_throw`), asi que
 * `exc_obj` apunta a un `std::logic_error` real: `{ vtable_ptr;
 * __cow_string _M_msg; }` (layout confirmado por la ABI de Itanium C++ mas
 * `__cow_string`, el string COW liviano que libstdc++ usa SIEMPRE para el
 * mensaje de las excepciones estandar, independiente de `_GLIBCXX_USE_CXX11_ABI`).
 * O sea: mismo offset +4 que el `type_info->__name` de abajo, pero leido
 * sobre `exc_obj` en vez de `tinfo` -- el mensaje literal (p. ej.
 * "basic_string::_M_construct null not valid") dice EXACTAMENTE cual de los
 * 57 sitios disparo, sin tener que auditarlos a mano.
 */
void cxa_throw_log(void *exc_obj, void *tinfo) {
    const char *type_name = "?";
    if (tinfo) {
        const char *n = *(const char **)((uint8_t *)tinfo + 4);
        if (n) type_name = n;
    }
    const char *msg = "?";
    if (exc_obj) {
        const char *m = *(const char **)((uint8_t *)exc_obj + 4);
        if (m) msg = m;
    }
    l_error("[patch] __cxa_throw: type='%s' msg='%s' (Bug #022, diagnostico)", type_name, msg);
}

__attribute__((naked, target("arm")))
static void hook_cxa_throw(void) {
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        // r0 = exc_obj, r1 = tinfo: ya son los dos primeros args de __cxa_throw,
        // en el orden correcto para cxa_throw_log(exc_obj, tinfo) por AAPCS.
        "bl cxa_throw_log\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 1f\n"
        "ldr r12, [r12]\n"      // r12 = direccion del literal (doble indireccion)
        "ldr ip, [r12]\n"       // ip = VALOR (ldr ip,[pc,#144] original)
        ".word 0xe92d48f0\n"    // push {r4,r5,r6,r7,fp,lr}
        "ldr r3, 2f\n"          // r3: scratch confirmado libre (ver comentario arriba)
        "ldr pc, [r3]\n"
        "1: .word g_emu_cxathrow\n"
        "2: .word g_resume_cxathrow\n"
    );
}

static const char __attribute__((used)) s_english_str[] = "english";
static const char __attribute__((used)) s_empty_str[] = "";

/*
 * Bug #022 (log 030) — FIX REAL de la excepcion C++ en "First time launch".
 *
 * El hook de diagnostico de sconstruct confirmo:
 *   [patch] std::string(NULL): llamador=libasphalt6.so+0x5A6E94
 *
 * El llamador es StringManager::SetLanguage (0x5A6E78) llamado con lang == NULL.
 * Fue invocado desde GS_MenuMain::StateUpdate (0x3F43F0), que hace:
 *   uVar20 = StringManager::GetLanguageString();
 *   FlashFXHandler::SetLanguage(uVar20);  // slot 0x24
 *
 * Con perfil nuevo (o first launch), m_languageId vale 0. GetLanguageString (0x4E94F0)
 * busca en su mapa (solo contiene IDs 1="english", 2="french", ..., 9="korean").
 * Al no encontrar el 0, devuelve NULL (0). StateUpdate pasa ese NULL a
 * FlashFXHandler::SetLanguage -> StringManager::SetLanguage -> std::string(NULL)
 * -> __cxa_throw(std::logic_error) -> abort().
 *
 * Triple solucion:
 * 1. StringManager::GetLanguageString: si no encuentra idioma, devuelve "english" (ID 1).
 * 2. StringManager::SetLanguage: si recibe lang == NULL, lo sustituye por "english".
 * 3. std::string ctor (OFF_SCONSTRUCT): si recibe NULL, lo sustituye por "" para que
 *    ningun otro sitio de libstdc++ lance logic_error.
 */

const char *getlang_fallback(void *this_ptr) {
    l_error("[patch] GetLanguageString: id=%d no encontrado en mapa, fallback a 'english' (Bug #022)",
            this_ptr ? *(int *)this_ptr : -1);
    return s_english_str;
}

__attribute__((naked, target("arm")))
static const char *orig_GetLanguageString(void *this_ptr) {
    __asm__ volatile(
        ".word 0xe92d4070\n"    // emu: push {r4,r5,r6,lr}
        ".word 0xe5903030\n"    // emu: ldr r3, [r0, #48]
        "ldr r12, 1f\n"
        "ldr pc, [r12]\n"
        "1: .word g_resume_getlang\n"
    );
}

static const char *hooked_GetLanguageString(void *this_ptr) {
    const char *res = orig_GetLanguageString(this_ptr);
    if (!res) {
        return getlang_fallback(this_ptr);
    }
    return res;
}

void setlang_null_warn(void) {
    l_error("[patch] StringManager::SetLanguage(NULL) recibido, fallback a 'english' (Bug #022)");
}

__attribute__((naked, target("arm")))
static void hook_setlang(void) {
    __asm__ volatile(
        "cmp r1, #0\n"
        "bne 1f\n"
        "push {r0-r3, r12, lr}\n"
        "bl setlang_null_warn\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r1, 2f\n"             // r1 = "english"
        "1:\n"
        ".word 0xe92d4ff0\n"       // emu: push {r4-r9, sl, fp, lr}
        ".word 0xe24dd044\n"       // emu: sub sp, sp, #68
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"          // jump to 0x5a6e80
        "2: .word s_english_str\n"
        "3: .word g_resume_setlang\n"
    );
}

void sconstruct_null_log(uint32_t lr) {
    l_error("[patch] std::string(NULL): llamador=libasphalt6.so+0x%X, sustituido por \"\" (Bug #022)",
            (unsigned)(lr - (uint32_t)so_mod.text_base));
}

__attribute__((naked, target("arm")))
static void hook_sconstruct(void) {
    __asm__ volatile(
        "cmp r1, #0\n"
        "bne 1f\n"
        "push {r0-r3, r12, lr}\n"
        "mov r0, lr\n"          // arg = direccion de retorno real del juego
        "bl sconstruct_null_log\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r1, 3f\n"          // r1 = "" (evita que _S_construct lance logic_error)
        "1:\n"
        ".word 0xe92d4070\n"    // emu: push {r4,r5,r6,lr}
        ".word 0xe2515000\n"    // emu: subs r5,r1,#0
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"
        "2: .word g_resume_sconstruct\n"
        "3: .word s_empty_str\n"
    );
}

void loadgeom_null_warn(void) {
    l_error("[patch] TrackScene::LoadLevelGeometry: constructScene devolvio NULL, saltando drop (Bug #023)");
}

__attribute__((naked, target("arm")))
static void hook_loadgeom(void) {
    __asm__ volatile(
        "ldr r4, [fp, #12]\n"
        "cmp r4, #0\n"
        "bne 1f\n"
        "push {r0-r3, r12, lr}\n"
        "bl loadgeom_null_warn\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"          // saltar a 0x46b1ec (~CColladaDatabase)
        "1:\n"
        ".word 0xe5943000\n"       // emu: ldr r3, [r4]
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"          // resume en 0x46b1c0
        "2: .word g_resume_loadgeom\n"
        "3: .word g_skip_loadgeom\n"
    );
}

void trackcopy_empty(uint32_t remaining) {
    l_error("[patch] TrackCopy: fread devolvio 0 con restante=%u, se omite el resto (log 038)",
            (unsigned)remaining);
}

// Guarda del bucle de copia de updateSaveFile (log 038). A la entrada, r0 es
// el retorno de fread (__n). Las dos palabras pisadas (`mov r1,#1` + `mov
// r3,r4`) no tocan r0/r12, así que se emulan verbatim y r12 queda libre como
// scratch para los saltos. Si __n == 0, no hay progreso posible: se salta a
// 0x558848 (fflush del destino) en vez de girar para siempre.
__attribute__((naked, target("arm")))
static void hook_trackcopy(void) {
    __asm__ volatile(
        ".word 0xe3a01001\n"   // emu: mov r1, #1
        ".word 0xe1a03004\n"   // emu: mov r3, r4
        "cmp r0, #0\n"
        "bne 1f\n"
        "push {r0-r3, r12, lr}\n"
        "mov r0, r5\n"         // arg = restante sin copiar
        "bl trackcopy_empty\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"      // -> 0x558848 (salir del bucle)
        "1:\n"
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"      // resume en 0x558830
        "2: .word g_resume_trackcopy\n"
        "3: .word g_skip_trackcopy\n"
    );
}

void staterender_null(uint32_t site) {
    l_error("[patch] StateRenderNull: Find devolvio NULL en sitio %u, store omitido (log 040)",
            (unsigned)site);
}

// Guardas de GS_Race::StateRender (log 040 + dump 1789184428): el `mov r3,#N`
// + `strb r3,[r0,#0x9b]` pisados no tocan r0/r12, así que r0 (retorno de Find)
// sigue intacto a la entrada y r12 es scratch libre. Si r0 == NULL se omite el
// store y se sigue en +8, igual que el camino chequeado de
// "component_controls". Ambos caminos convergen en el mismo resume.
__attribute__((naked, target("arm")))
static void hook_sr1(void) {
    __asm__ volatile(
        ".word 0xe3a03001\n"   // emu: mov r3, #1
        "cmp r0, #0\n"
        "bne 1f\n"
        "push {r0-r3, r12, lr}\n"
        "mov r0, #1\n"
        "bl staterender_null\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"      // resume en +8 (store omitido)
        "1:\n"
        ".word 0xe5c0309b\n"   // emu: strb r3, [r0, #0x9b]
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"
        "2: .word g_resume_sr1\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_sr0(void) {
    __asm__ volatile(
        ".word 0xe3a03000\n"   // emu: mov r3, #0
        "cmp r0, #0\n"
        "bne 1f\n"
        "push {r0-r3, r12, lr}\n"
        "mov r0, #0\n"
        "bl staterender_null\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"      // resume en +8 (store omitido)
        "1:\n"
        ".word 0xe5c0309b\n"   // emu: strb r3, [r0, #0x9b]
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"
        "2: .word g_resume_sr0\n"
    );
}

__attribute__((naked, target("arm")))
static void hook_sr2(void) {
    __asm__ volatile(
        ".word 0xe3a03001\n"   // emu: mov r3, #1
        "cmp r0, #0\n"
        "bne 1f\n"
        "push {r0-r3, r12, lr}\n"
        "mov r0, #2\n"
        "bl staterender_null\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"      // resume en +8 (store omitido)
        "1:\n"
        ".word 0xe5c0309b\n"   // emu: strb r3, [r0, #0x9b]
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"
        "2: .word g_resume_sr2\n"
    );
}

void igm_null(uint32_t menu_main, uint32_t back_btn) {
    l_error("[patch] IGMUpdate: Find devolvio NULL (menu_main=0x%08X, back_btn=0x%08X), copy omitido (log 041)",
            (unsigned)menu_main, (unsigned)back_btn);
}

// Guarda de GS_Race::IGMUpdate (log 041 + dump 1789185959): r7 = Find("menu_main"),
// r0 = Find("back_btn_main"). Si alguno es NULL, se omite el ldrb/strb de visibilidad
// en +0x9b y se resume en 0x418AD4 con r1 = 0x20086 ya configurado para GetString.
__attribute__((naked, target("arm")))
static void hook_igm_vis(void) {
    __asm__ volatile(
        ".word 0xe3001086\n"   // emu: movw r1, #0x86
        ".word 0xe3401002\n"   // emu: movt r1, #0x2  (r1 = 0x20086)
        "cmp r7, #0\n"
        "beq 1f\n"
        "cmp r0, #0\n"
        "beq 1f\n"
        ".word 0xe5d7309b\n"   // emu: ldrb r3, [r7, #0x9b]
        ".word 0xe5c0309b\n"   // emu: strb r3, [r0, #0x9b]
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"      // resume en 0x418AD4 (g_skip_igm)
        "1:\n"
        "push {r0-r3, r12, lr}\n"
        "mov r1, r0\n"         // arg2: back_btn_main
        "mov r0, r7\n"         // arg1: menu_main
        "bl igm_null\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r12, 2f\n"
        "ldr pc, [r12]\n"      // resume en 0x418AD4 (g_skip_igm)
        "2: .word g_skip_igm\n"
    );
}

// Bug #033: aviso en vivo (deberia ser raro -- una vez por auto/indice afectado,
// no por frame, asi que no hace falta bc_event ni colapso de logger).
void packfilename_null(void) {
    l_error("[patch] PackFilenameNull: this->packNames[i] sin construir, se devuelve "
            "\"\" real del motor en vez de crashear (Bug #033)");
}

/*
 * Bug #033, guarda: BaseCarManager::GetPackFilename NULL. Emula `ldr r4,[r3,r2]`;
 * si el string cargado es NULL, arma el retorno con el string vacio INMORTAL real
 * (misma allocator que el motor usa, resuelto por simbolo -- ver g_empty_rep_data
 * en so_patch()) replicando el camino de exito de la propia funcion (0x48D388-
 * 0x48D398: `*sret = data_ptr; return sret;` con el mismo epilogo `add sp,#12;
 * pop {r4,r5,pc}`), en vez de saltar a una direccion del medio de la funcion que
 * asume r4 == r3 (el singleton) para no repetir esa comparacion a mano. r5 (sret)
 * y la pila ya estan en el estado que esa cola espera: solo `sub sp,sp,#12` corrio
 * antes de este punto, nada mas toco r4/r5 desde el `push {r4,r5,lr}` inicial.
 */
__attribute__((naked, target("arm")))
static void hook_getpackfilename(void) {
    __asm__ volatile(
        "ldr r4, [r3, r2]\n"     // emu
        "cmp r4, #0\n"
        "bne 1f\n"
        "push {r0-r3, r12, lr}\n"
        "bl packfilename_null\n"
        "pop {r0-r3, r12, lr}\n"
        "ldr r0, 2f\n"
        "ldr r0, [r0]\n"          // r0 = g_empty_rep_data (puntero de datos, no direccion del global)
        "str r0, [r5]\n"          // *sret = puntero al string vacio real
        "mov r0, r5\n"            // retorno = sret (funcion devuelve por puntero oculto)
        "add sp, sp, #12\n"       // deshace el "sub sp,sp,#12" de 0x48D360
        "pop {r4, r5, pc}\n"      // deshace el "push {r4,r5,lr}" de 0x48D354
        "1:\n"
        ".word 0xe244400c\n"      // emu: sub r4, r4, #12
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"         // resume en 0x48D36C
        "2: .word g_empty_rep_data\n"
        "3: .word g_resume_getpackfilename\n"
    );
}

// Engancha text_base+off con stub tras verificar la primera palabra del prologo.
// emu_lit_off = offset del literal que cargaba el ldr PC-relativo (0 si no hay).
static void hook_trace(uint32_t off, uint32_t expect1, uint32_t expect2,
                       void (*stub)(void),
                       uint32_t emu_lit_off, uint32_t *resume_out, uint32_t *emu_out) {
    uint32_t w = *(volatile uint32_t *)(so_mod.text_base + off);
    if (w != expect1) {
        l_error("[patch] sin hook en +0x%X: primera palabra 0x%08X != 0x%08X esperada",
                (unsigned)off, (unsigned)w, (unsigned)expect1);
        return;
    }
    // Las dos palabras pisadas se emulan en el stub: si la segunda no es la que
    // el stub cree, no instalar (ver el bloque W2_* arriba: esto es lo que
    // convierte un encoding mal escrito en una linea de log y no en un crash).
    uint32_t w2 = *(volatile uint32_t *)(so_mod.text_base + off + 4);
    if (w2 != expect2) {
        l_error("[patch] sin hook en +0x%X: segunda palabra 0x%08X != 0x%08X esperada",
                (unsigned)off, (unsigned)w2, (unsigned)expect2);
        return;
    }
    *resume_out = (uint32_t)(so_mod.text_base + off + 8);
    if (emu_lit_off && emu_out)
        *emu_out = (uint32_t)(so_mod.text_base + emu_lit_off);
    hook_addr((uintptr_t)(so_mod.text_base + off), (uintptr_t)stub);
    l_error("[patch] hook en +0x%X", (unsigned)off);
}

void so_patch(void) {
    hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN7gameswf4root7advanceEfb"), (uintptr_t)&hooked_gameswf_root_advance);
    hook_addr((uintptr_t)(so_mod.text_base + 0x683af8), (uintptr_t)&hooked_RenderFX_Find_pt);

    uintptr_t sym_tb = (uintptr_t)so_symbol(&so_mod, "_ZN8RenderFX23SetTextBufferingEnabledEb");
    if (sym_tb) hook_addr(sym_tb, (uintptr_t)&hooked_RenderFX_SetTextBufferingEnabled);
    uintptr_t sym_ag = (uintptr_t)so_symbol(&so_mod, "_ZN8RenderFX24SetAutoLoadGlyphsEnabledEb");
    if (sym_ag) hook_addr(sym_ag, (uintptr_t)&hooked_RenderFX_SetAutoLoadGlyphsEnabled);
    uintptr_t sym_rc = (uintptr_t)so_symbol(&so_mod, "_ZN8RenderFX23SetRenderCachingEnabledEb");
    if (sym_rc) hook_addr(sym_rc, (uintptr_t)&hooked_RenderFX_SetRenderCachingEnabled);

#ifdef RENDER_CULLING_BYPASS
    // Bypass culling de ISceneNode y bounding box para evitar que el vehiculo / entidades
    // parpadeen o se borren durante la carrera (optimizacion/fix de Dungeon Hunter 2).
    uintptr_t sym_is_culled_node = (uintptr_t)so_symbol(&so_mod, "_ZNK6glitch5scene13CSceneManager8isCulledEPKNS0_10ISceneNodeE");
    if (sym_is_culled_node) {
        hook_addr(sym_is_culled_node, (uintptr_t)&ret0);
        l_info("[patch] Hooked CSceneManager::isCulled(ISceneNode*) -> ret0");
    }
    uintptr_t sym_is_culled_box = (uintptr_t)so_symbol(&so_mod, "_ZNK6glitch5scene13CSceneManager8isCulledERKNS_4core8aabbox3dIfEENS0_14E_CULLING_TYPEE");
    if (sym_is_culled_box) {
        hook_addr(sym_is_culled_box, (uintptr_t)&ret0);
        l_info("[patch] Hooked CSceneManager::isCulled(aabbox3d, E_CULLING_TYPE) -> ret0");
    }

    // Causa raiz real de vehiculos (jugador, rivales, trafico) y poderes (nitro, cash, emp, etc.)
    // que desaparecian o eran intermitentes durante la carrera:
    // NINGUNO de ellos consulta CSceneManager::isCulled(). Todos llaman a Camera::IsInViewFrustrum(&bbox):
    // 1) RaceCar::UpdateMeshes: si IsInViewFrustrum devuelve 0, ejecuta setVisible(false) en los 43 nodos
    //    del auto, haciendolo completamente invisible (tanto al jugador como a los competidores).
    // 2) TrafficCar::IsViewable: devuelve Camera::IsInViewFrustrum(&bbox); si es 0, el auto de calle se oculta.
    // 3) BaseSceneObject::SceneObjUpdateCull: calcula *(item+0x1a) = IsInViewFrustrum ^ 1; si es 0,
    //    marca isCulled=1 y llama setVisible(false) sobre el nodo del poder/pickup.
    // Forzar Camera::IsInViewFrustrum -> ret1 (1 = dentro del frustum = visible) garantiza que los vehiculos
    // y poderes se mantengan siempre visibles y actualizados.
    uintptr_t sym_is_in_view_frustum = (uintptr_t)so_symbol(&so_mod, "_ZN6Camera16IsInViewFrustrumERKN6glitch4core8aabbox3dIfEE");
    if (sym_is_in_view_frustum) {
        hook_addr(sym_is_in_view_frustum, (uintptr_t)&ret1);
        l_info("[patch] Hooked Camera::IsInViewFrustrum -> ret1 (Vehiculos y poderes siempre visibles)");
    }

    // CustomSceneManager::isCulledCustom devuelve 1 si culled, 0 si visible.
    // Al forzarlo a ret0, ningun nodo de escena personalizado es descartado por el culling frustum custom.
    uintptr_t sym_is_culled_custom = (uintptr_t)so_symbol(&so_mod, "_ZNK18CustomSceneManager14isCulledCustomEPKN6glitch5scene10ISceneNodeE9CULL_TYPE");
    if (sym_is_culled_custom) {
        hook_addr(sym_is_culled_custom, (uintptr_t)&ret0);
        l_info("[patch] Hooked CustomSceneManager::isCulledCustom -> ret0");
    }
#else
    l_info("[patch] RENDER_CULLING_BYPASS off -- isCulled() sin tocar (build de diagnostico)");
#endif

    // Diagnostico CNullDriver (ver comentario arriba de los hooked_CNullDriver_*):
    // draw2DLine/getMaxUserClipPlanes se reemplazan 1:1 (son no-ops triviales).
    // createBuffer se habia desactivado tras el log 044 para eliminar el spam de
    // 3850 lineas y la pausa de 40s en storage -- el Bug #028 (colapso de lineas
    // repetidas por sitio de llamada en logger.c) elimina ese costo de raiz, asi
    // que se reactiva para retomar la pregunta que quedo abierta en el log 042:
    // ¿el auto (u otros elementos que siguen desapareciendo) termina con este
    // vtable nulo, o son 100% los 2 `this` de batching ya confirmados? Sin el
    // `lr` de esta corrida no hay forma de saberlo sin adivinar.
    uintptr_t sym_cnd_line = (uintptr_t)so_symbol(&so_mod, "_ZN6glitch5video11CNullDriver10draw2DLineERKNS_4core10position2dIiEES6_NS0_6SColorE");
    if (sym_cnd_line) hook_addr(sym_cnd_line, (uintptr_t)&hooked_CNullDriver_draw2DLine);
    uintptr_t sym_cnd_clip = (uintptr_t)so_symbol(&so_mod, "_ZNK6glitch5video11CNullDriver20getMaxUserClipPlanesEv");
    if (sym_cnd_clip) hook_addr(sym_cnd_clip, (uintptr_t)&hooked_CNullDriver_getMaxUserClipPlanes);
    uintptr_t sym_cnd_createbuffer = (uintptr_t)so_symbol(&so_mod,
        "_ZN6glitch5video11CNullDriver12createBufferENS0_13E_BUFFER_TYPEENS0_14E_BUFFER_USAGEEjPvb");
    if (sym_cnd_createbuffer) {
        // Prologo verificado con objdump: push {r4-r8,sl,lr} + sub sp,sp,#12,
        // las mismas 2 palabras que el stub emula -- resume 8 bytes despues.
        g_resume_cnd_createbuffer = (uint32_t)(sym_cnd_createbuffer + 8);
        hook_addr(sym_cnd_createbuffer, (uintptr_t)&hooked_CNullDriver_createBuffer);
    }

    // Rastreo del tramo MenuScene (ver comentario arriba): si el .so no es el
    // esperado, hook_trace lo reporta y sigue sin parchear ese punto.
    hook_trace(OFF_MENUSCENE_C1, W_PUSH9, W2_LDR_SL, hook_c1, 0x4427E4u, &g_resume_c1, &g_emu_c1);
    hook_trace(OFF_MENUSCENE_C2, W_PUSH9, W2_LDR_SL, hook_c2, 0x441FDCu, &g_resume_c2, &g_emu_c2);
    hook_trace(OFF_REMOVECHILD, W_PUSH6a, W2_MOV_R6R0, hook_rm, 0, &g_resume_rm, NULL);
    hook_trace(OFF_GRID_CTOR, W_PUSH8, W2_ADD_R6, hook_grid, 0, &g_resume_grid, NULL);
    hook_trace(OFF_CREATEANIM, W_LDR_R3a, W2_PUSH_ANIM, hook_anim, 0x50A184u, &g_resume_anim, &g_emu_anim);
    hook_trace(OFF_CLIGHT_CTOR, W_PUSH9, W2_VLDR_S14, hook_light, 0x744980u, &g_resume_light, &g_emu_light);
    hook_trace(OFF_DISPLAYFRAME, W_LDR_R3b, W_PUSH6a, hook_frame, 0x4A4244u, &g_resume_frame, &g_emu_frame);
    // Segundo nivel dentro de DisplayFrame (giro con +1850 gettod/s, log 018).
    hook_trace(OFF_IDEV_RUN, W_LDR_IP, W2_LDR_R3_D0, hook_run, 0, &g_resume_run, NULL);
    hook_trace(OFF_RFX_UPDATE, W_PUSH9, W2_VPUSH_D8, hook_update, 0, &g_resume_update, NULL);
    hook_trace(OFF_RFX_RENDER, W_PUSH3, W2_MOV_R4R0, hook_render, 0, &g_resume_render, NULL);
    hook_trace(OFF_ENDSCENE_GL, W_PUSH1, W2_LDR_R3R0, hook_endgl, 0, &g_resume_endgl, NULL);
    hook_trace(OFF_ENDSCENE_IV, W_PUSH1, W2_SUB_SP8, hook_endiv, 0, &g_resume_endiv, NULL);
    /*
     * Tercer nivel (log 022): ¿retornó el DisplayFrame final del ctor? Si estas
     * dos disparan, el giro es aguas abajo (epílogo del ctor -> DoStateChange ->
     * Loading::Stop -> ResumeAllSounds), no dentro de DisplayFrame.
     *
     * El hook de `getRealTime` (OFF_REALTIME/hook_realtime, que sigue definido)
     * queda FUERA a proposito: el 022 ya atribuyó el flood de reloj al
     * `cond_timedwait` de un worker de vox, así que su pregunta ya está
     * contestada -- y como empuja una miga por llamada, inundaría el anillo de
     * 32 del hilo principal y borraría justamente el contexto (DisplayFrame /
     * CLightSceneNode / AfterDF) que hay que leer en el próximo volcado.
     */
    hook_trace(OFF_AFTERDF1, W_LDR_R3c, W2_LDR_R4SP, hook_dfret1, 0x441FF8u, &g_resume_dfret1, &g_emu_dfret1);
    hook_trace(OFF_AFTERDF2, W_LDR_R3c, W2_LDR_R4SP, hook_dfret2, 0x442800u, &g_resume_dfret2, &g_emu_dfret2);
    // Bug #019 (dump 023): drop de string con data NULL en el epilogo del ctor.
    g_skip_strdrop1 = (uint32_t)(so_mod.text_base + 0x4421D8u);
    hook_trace(OFF_STRDROP1, W_ADD_R0, W2_MVN_R1, hook_strdrop1, 0, &g_resume_strdrop1, NULL);
    g_skip_strdrop2 = (uint32_t)(so_mod.text_base + 0x4429E0u);
    hook_trace(OFF_STRDROP2, W_ADD_R0, W2_MVN_R1, hook_strdrop2, 0, &g_resume_strdrop2, NULL);
    /*
     * Bug #020 (log 024): el `while (*p != carIdx) p++;` sin cota de
     * OnLoad3DScene. Ver el bloque de comentario de OFF_CARSEED/OFF_CARFIND.
     * Las dos guardas son independientes: si la siembra alcanza, CarFind no
     * llega a dispararse nunca (y el log lo dice).
     */
    hook_trace(OFF_CARSEED, W_STR_R0_44, W2_MOV_R0R5, hook_carseed, 0, &g_resume_carseed, NULL);
    g_skip_carfind = (uint32_t)(so_mod.text_base + 0x3EF390u);
    hook_trace(OFF_CARFIND, W_LDR_R2R3, W2_CMP_R2R1, hook_carfind, 0, &g_resume_carfind, NULL);
    /*
     * Bug #021 (log 025): el giro real es el std::sort de GetUnlockList con un
     * comparador que no es strict weak ordering, sobre datos de auto todos en
     * cero por el parche binario del Bug #005 (ya revertido). Ver el bloque de
     * comentario de OFF_STARS/OFF_PACKFILE/OFF_MENUCAR.
     */
    g_skip_stars = (uint32_t)(so_mod.text_base + 0x462AE0u);
    hook_trace(OFF_STARS, W_CMP_R4R0, W2_MOVGT_R0, hook_stars, 0, &g_resume_stars, NULL);
    g_skip_packfile = (uint32_t)(so_mod.text_base + 0x48DB84u);
    hook_trace(OFF_PACKFILE, W_ADD_R1SP, W2_MOV_R5_0, hook_packfile, 0, &g_resume_packfile, NULL);
    g_skip_menucar = (uint32_t)(so_mod.text_base + 0x3EF040u);
    hook_trace(OFF_MENUCAR, W_LDR_R3_3C, W2_LDR_R1PC, hook_menucar, 0x3EF460u, &g_resume_menucar, &g_emu_menucar);

    /*
     * Bug #022: Excepcion C++ al entrar al menu ("First time launch" -> std::string(NULL)).
     * Diagnostico confirmado con logs 028 y 030: GetLanguageString devolvio NULL con perfil
     * nuevo y StateUpdate intento construir std::string(NULL) via FlashFXHandler::SetLanguage.
     */
    hook_trace(OFF_CXA_THROW, W_LDR_IP_90, W_PUSH_CXA, hook_cxa_throw, LIT_CXA_THROW, &g_resume_cxathrow, &g_emu_cxathrow);
    hook_trace(OFF_SCONSTRUCT, W_PUSH456LR, W_SUBS_R5R1, hook_sconstruct, 0, &g_resume_sconstruct, NULL);
    hook_trace(OFF_GETLANG, W_PUSH456LR, W2_LDR_R3_48, (void (*)(void))&hooked_GetLanguageString, 0, &g_resume_getlang, NULL);
    hook_trace(OFF_SETLANG, W_PUSH9, W2_SUB_SP68, hook_setlang, 0, &g_resume_setlang, NULL);

    /*
     * Bug #023: Crash previo a la carrera (TrackScene::LoadLevelGeometry).
     * 1) Bypass del prefijo "IPAD2a_" para que todos los circuitos carguen <Track>.bdae.
     * 2) Guarda NULL en el drop() del nodo retornado por constructScene().
     */
    uint32_t w_ipad2 = *(volatile uint32_t *)(so_mod.text_base + OFF_LOADGEOM_IPAD2);
    if (w_ipad2 == W_CMP_R3_9) {
        uint32_t b_skip = B_LOADGEOM_SKIP_IPAD2;
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + OFF_LOADGEOM_IPAD2), &b_skip, sizeof(b_skip));
        l_info("[patch] TrackScene::LoadLevelGeometry: bypass IPAD2a_ instalado en +0x%X (b 0x46b114)",
               (unsigned)OFF_LOADGEOM_IPAD2);
    } else {
        l_error("[patch] TrackScene::LoadLevelGeometry: sin bypass IPAD2a_ en +0x%X (palabra 0x%08X != 0x%08X)",
                (unsigned)OFF_LOADGEOM_IPAD2, (unsigned)w_ipad2, (unsigned)W_CMP_R3_9);
    }

    g_skip_loadgeom = (uint32_t)(so_mod.text_base + 0x46B1ECu);
    hook_trace(OFF_LOADGEOM_NODE, W_LDR_R4FP12, W2_LDR_R3R4, hook_loadgeom, 0, &g_resume_loadgeom, NULL);

    // Log 038: bucle de copia sin cota en TrackingManager::updateSaveFile.
    g_skip_trackcopy = (uint32_t)(so_mod.text_base + 0x558848u);
    hook_trace(OFF_TRACKCOPY, W_MOV_R1_1, W2_MOV_R3R4, hook_trackcopy, 0, &g_resume_trackcopy, NULL);

    // Log 040 + dump 1789184428: Find("custom_controls_btn") NULL en StateRender.
    hook_trace(OFF_STATER1, W_MOV_R3_1, W2_STRB_R3R0_9B, hook_sr1, 0, &g_resume_sr1, NULL);
    hook_trace(OFF_STATER0, W_MOV_R3_0, W2_STRB_R3R0_9B, hook_sr0, 0, &g_resume_sr0, NULL);
    hook_trace(OFF_STATER2, W_MOV_R3_1, W2_STRB_R3R0_9B, hook_sr2, 0, &g_resume_sr2, NULL);

    // Log 041 + dump 1789185959: Find("menu_main") / Find("back_btn_main") NULL en IGMUpdate.
    g_skip_igm = (uint32_t)(so_mod.text_base + 0x418AD4u);
    hook_trace(OFF_IGM_VIS, W_MOVW_R1_86, W2_LDRB_R3R7_9B, hook_igm_vis, 0, &g_resume_igm, NULL);

    // Log 052 + dump 1789608605: BaseCarManager::GetPackFilename NULL al elegir auto.
    // g_empty_rep_data = direccion de DATOS del string vacio inmortal (rep + 12,
    // saltando length/capacity/refcount) -- mismo allocator que usa BaseCarManager
    // para packNames, resuelto por simbolo real en vez de un literal inventado.
    uintptr_t sym_empty_rep = (uintptr_t)so_symbol(&so_mod,
        "_ZNSbIcSt11char_traitsIcEN6glitch4core10SAllocatorIcLNS1_6memory13E_MEMORY_HINTE0EEEE4_Rep20_S_empty_rep_storageE");
    if (sym_empty_rep) {
        g_empty_rep_data = (uint32_t)(sym_empty_rep + 12);
        hook_trace(OFF_GETPACKFILENAME, W_LDR_R4R3R2, W2_SUB_R4_12, hook_getpackfilename,
                   0, &g_resume_getpackfilename, NULL);
    } else {
        l_error("[patch] sin hook en GetPackFilename: no se encontro _S_empty_rep_storage");
    }
}
