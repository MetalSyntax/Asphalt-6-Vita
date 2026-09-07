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

#define W_PUSH9  0xe92d4ff0u // push {r4-r9, sl, fp, lr}
#define W_PUSH6a 0xe92d41f0u // push {r4-r8, lr}
#define W_PUSH8  0xe92d47f0u // push {r4-r9, sl, lr}
#define W_LDR_R3a 0xe59f3030u // ldr r3, [pc, #48]  (createAnimator)
#define W_LDR_R3b 0xe59f3114u // ldr r3, [pc, #276] (DisplayFrame)
#define W_LDR_IP 0xe590c0c0u // ldr ip, [r0, #192] (IDevice::run)
#define W_PUSH3  0xe92d4030u // push {r4, r5, lr}   (RenderFX::Render)
#define W_PUSH1  0xe92d4010u // push {r4, lr}       (endScene x2)

static uint32_t g_resume_c1, g_resume_c2, g_resume_rm, g_resume_grid,
                g_resume_anim, g_resume_light, g_resume_frame,
                g_resume_run, g_resume_update, g_resume_render,
                g_resume_endgl, g_resume_endiv;
static uint32_t g_emu_c1, g_emu_c2, g_emu_anim, g_emu_light, g_emu_frame;

static const char s_tr_c1[] = "MenuScene::MenuScene";
static const char s_tr_rm[] = "RemoveChildNodeType";
static const char s_tr_grid[] = "CustomBatchGrid";
static const char s_tr_anim[] = "createAnimator";
static const char s_tr_light[] = "CLightSceneNode";
static const char s_tr_frame[] = "DisplayFrame";
static const char s_tr_run[] = "IDevice::run";
static const char s_tr_update[] = "RenderFX::Update";
static const char s_tr_render[] = "RenderFX::Render";
static const char s_tr_endgl[] = "endScene-GL";
static const char s_tr_endiv[] = "endScene-IV";

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
        "ldr r12, 3f\n"
        "ldr pc, [r12]\n"
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

// Engancha text_base+off con stub tras verificar la primera palabra del prologo.
// emu_lit_off = offset del literal que cargaba el ldr PC-relativo (0 si no hay).
static void hook_trace(uint32_t off, uint32_t expect1, void (*stub)(void),
                       uint32_t emu_lit_off, uint32_t *resume_out, uint32_t *emu_out) {    uint32_t w = *(volatile uint32_t *)(so_mod.text_base + off);
    if (w != expect1) {
        l_error("[patch] sin hook en +0x%X: primera palabra 0x%08X != 0x%08X esperada",
                (unsigned)off, (unsigned)w, (unsigned)expect1);
        return;
    }
    *resume_out = (uint32_t)(so_mod.text_base + off + 8);
    if (emu_lit_off && emu_out)
        *emu_out = (uint32_t)(so_mod.text_base + emu_lit_off);
    hook_addr((uintptr_t)(so_mod.text_base + off), (uintptr_t)stub);
}

void so_patch(void) {
    hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN7gameswf4root7advanceEfb"), (uintptr_t)&hooked_gameswf_root_advance);
    hook_addr((uintptr_t)(so_mod.text_base + 0x683af8), (uintptr_t)&hooked_RenderFX_Find_pt);

    // Rastreo del tramo MenuScene (ver comentario arriba): si el .so no es el
    // esperado, hook_trace lo reporta y sigue sin parchear ese punto.
    hook_trace(OFF_MENUSCENE_C1, W_PUSH9, hook_c1, 0x4427E4u, &g_resume_c1, &g_emu_c1);
    hook_trace(OFF_MENUSCENE_C2, W_PUSH9, hook_c2, 0x441FDCu, &g_resume_c2, &g_emu_c2);
    hook_trace(OFF_REMOVECHILD, W_PUSH6a, hook_rm, 0, &g_resume_rm, NULL);
    hook_trace(OFF_GRID_CTOR, W_PUSH8, hook_grid, 0, &g_resume_grid, NULL);
    hook_trace(OFF_CREATEANIM, W_LDR_R3a, hook_anim, 0x50A184u, &g_resume_anim, &g_emu_anim);
    hook_trace(OFF_CLIGHT_CTOR, W_PUSH9, hook_light, 0x744980u, &g_resume_light, &g_emu_light);
    hook_trace(OFF_DISPLAYFRAME, W_LDR_R3b, hook_frame, 0x4A4244u, &g_resume_frame, &g_emu_frame);
    // Segundo nivel dentro de DisplayFrame (giro con +1850 gettod/s, log 018).
    hook_trace(OFF_IDEV_RUN, W_LDR_IP, hook_run, 0, &g_resume_run, NULL);
    hook_trace(OFF_RFX_UPDATE, W_PUSH9, hook_update, 0, &g_resume_update, NULL);
    hook_trace(OFF_RFX_RENDER, W_PUSH3, hook_render, 0, &g_resume_render, NULL);
    hook_trace(OFF_ENDSCENE_GL, W_PUSH1, hook_endgl, 0, &g_resume_endgl, NULL);
    hook_trace(OFF_ENDSCENE_IV, W_PUSH1, hook_endiv, 0, &g_resume_endiv, NULL);
}
