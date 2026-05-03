.section ".text.boot"
.global _start
.global relocate_and_continue

/*
 * Platform-specific addresses defined via preprocessor
 */
#ifdef PLATFORM_QEMU
    .equ RELOC_DEST, 0x84000000
    .equ LOAD_BASE,  0x80200000
#else
    .equ RELOC_DEST, 0x20000000
    .equ LOAD_BASE,  0x00200000
#endif

_start:
    // === 1. Multi-core Check ===
    bnez a0, park_core

    // Save Hart ID and DTB address
    mv   s0, a0
    mv   s1, a1

    // Clear .bss section
    la   t0, __bss_start
    la   t1, __bss_stop

bss_clear:
    bgeu t0, t1, bss_done
    sd   zero, 0(t0)
    addi t0, t0, 8
    j    bss_clear

bss_done:
    // Set stack pointer
    la   sp, _end

    // Call start_kernel
    mv   a0, s0
    mv   a1, s1
    tail start_kernel

.global handle_exception_entry
.align 2
handle_exception_entry:
    /*
     * Retrieve kernel sp from sscratch and
     * save interrupted sp to sscratch
     */
    csrrw sp, sscratch, sp
    bnez  sp, save_context
    csrr  sp, sscratch

save_context:
    /* Save context */
    addi sp, sp, -8 * 35
    sd ra,  8 *  0(sp)
    sd gp,  8 *  2(sp)
    sd tp,  8 *  3(sp)
    sd t0,  8 *  4(sp)
    sd t1,  8 *  5(sp)
    sd t2,  8 *  6(sp)
    sd s0,  8 *  7(sp)
    sd s1,  8 *  8(sp)
    sd a0,  8 *  9(sp)
    sd a1,  8 * 10(sp)
    sd a2,  8 * 11(sp)
    sd a3,  8 * 12(sp)
    sd a4,  8 * 13(sp)
    sd a5,  8 * 14(sp)
    sd a6,  8 * 15(sp)
    sd a7,  8 * 16(sp)
    sd s2,  8 * 17(sp)
    sd s3,  8 * 18(sp)
    sd s4,  8 * 19(sp)
    sd s5,  8 * 20(sp)
    sd s6,  8 * 21(sp)
    sd s7,  8 * 22(sp)
    sd s8,  8 * 23(sp)
    sd s9,  8 * 24(sp)
    sd s10, 8 * 25(sp)
    sd s11, 8 * 26(sp)
    sd t3,  8 * 27(sp)
    sd t4,  8 * 28(sp)
    sd t5,  8 * 29(sp)
    sd t6,  8 * 30(sp)
    csrr s0, sscratch
    csrr s1, sepc
    csrr s2, sstatus
    csrr s3, scause
    csrr s4, stval
    sd s0,  8 *  1(sp)
    sd s1,  8 * 31(sp)
    sd s2,  8 * 32(sp)
    sd s3,  8 * 33(sp)
    sd s4,  8 * 34(sp)

    csrw sscratch, zero
    mv   a0, sp
    call do_trap

.global ret_from_exception
ret_from_exception:
    ld s0,  8 * 32(sp)
    andi s0, s0, (1 << 8)
    bnez s0, 1f
    addi s0, sp, 8 * 35
    csrw sscratch, s0
    j 2f
1:
    csrw sscratch, zero
2:
    /* Restore context */
    ld s1,  8 * 31(sp)
    ld s2,  8 * 32(sp)
    csrw sepc, s1
    csrw sstatus, s2
    ld ra,  8 *  0(sp)
    ld gp,  8 *  2(sp)
    /*
     * Keep tp as the current thread pointer across user/kernel transitions.
     * The scheduler and syscall path use tp to find struct thread; restoring
     * the user trap-frame tp here would clobber it before the next ecall.
     */
    ld t0,  8 *  4(sp)
    ld t1,  8 *  5(sp)
    ld t2,  8 *  6(sp)
    ld s0,  8 *  7(sp)
    ld s1,  8 *  8(sp)
    ld a0,  8 *  9(sp)
    ld a1,  8 * 10(sp)
    ld a2,  8 * 11(sp)
    ld a3,  8 * 12(sp)
    ld a4,  8 * 13(sp)
    ld a5,  8 * 14(sp)
    ld a6,  8 * 15(sp)
    ld a7,  8 * 16(sp)
    ld s2,  8 * 17(sp)
    ld s3,  8 * 18(sp)
    ld s4,  8 * 19(sp)
    ld s5,  8 * 20(sp)
    ld s6,  8 * 21(sp)
    ld s7,  8 * 22(sp)
    ld s8,  8 * 23(sp)
    ld s9,  8 * 24(sp)
    ld s10, 8 * 25(sp)
    ld s11, 8 * 26(sp)
    ld t3,  8 * 27(sp)
    ld t4,  8 * 28(sp)
    ld t5,  8 * 29(sp)
    ld t6,  8 * 30(sp)
    ld sp,  8 *  1(sp)
    sret

// === Relocation Function ===
// void relocate_and_continue(void *dtb, void (*continue_func)(void *dtb))
// a0 = dtb, a1 = continue_func (address in high memory)
// 
// This function:
// 1. Copies bootloader from low to high memory
// 2. Jumps to continue_func in high memory
relocate_and_continue:
    // Save arguments
    mv   s0, a0              // s0 = dtb
    mv   s1, a1              // s1 = continue_func (high memory address)

    // Copy bootloader to high memory
    la   t0, _load_start     // Source start (PC-relative)
    la   t1, _load_end       // Source end
    li   t2, RELOC_DEST      // Destination

copy_loop:
    bgeu t0, t1, copy_done
    ld   t3, 0(t0)
    sd   t3, 0(t2)
    addi t0, t0, 8
    addi t2, t2, 8
    j    copy_loop

copy_done:
    // fence.i to sync instruction cache
    .insn i 0x0F, 0x1, x0, x0, 0x000

    // Jump to continue_func in high memory
    mv   a0, s0              // a0 = dtb
    jr   s1                  // Jump to continue_func

park_core:
    wfi
    j    park_core
