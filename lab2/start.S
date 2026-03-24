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

    // 保存 Hart ID 和 DTB address
    mv   s0, a0
    mv   s1, a1

    // 清除 .bss section
    la   t0, __bss_start
    la   t1, __bss_stop

bss_clear:
    bgeu t0, t1, bss_done
    sd   zero, 0(t0)
    addi t0, t0, 8
    j    bss_clear

bss_done:
    // 設置 stack pointer
    la   sp, _end

    // 呼叫 start_kernel
    mv   a0, s0
    mv   a1, s1
    tail start_kernel

// === Relocation Function ===
// void relocate_and_continue(void *dtb, void (*continue_func)(void *dtb))
// a0 = dtb, a1 = continue_func (在高位的地址)
// 
// 此函數：
// 1. 把 bootloader 從低位搬到高位
// 2. 跳轉到高位的 continue_func 繼續執行
relocate_and_continue:
    // 保存參數
    mv   s0, a0              // s0 = dtb
    mv   s1, a1              // s1 = continue_func (高位地址)

    // 複製 bootloader 到高位
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
    // fence.i 確保 instruction cache 同步
    .insn i 0x0F, 0x1, x0, x0, 0x000

    // 跳轉到高位的 continue_func
    mv   a0, s0              // a0 = dtb
    jr   s1                  // 跳到 continue_func

park_core:
    wfi
    j    park_core