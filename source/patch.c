/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
#include <so_util/so_util.h>

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

void so_patch(void) {
    hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN7gameswf4root7advanceEfb"), (uintptr_t)&hooked_gameswf_root_advance);
    hook_addr((uintptr_t)(so_mod.text_base + 0x683af8), (uintptr_t)&hooked_RenderFX_Find_pt);
}
void shark_set_shader_association_path(const char *path) {}
