.section ".text.boot"
.global _start

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
    // 檢查 Hart ID (a0)。如果不是 0 (Primary Core)，就跳轉到 park_core 進入休眠
    bnez a0, park_core

    // 保存 Hart ID (a0) 到 s0，保存 DTB address (a1) 到 s1
    mv   s0, a0
    mv   s1, a1

    // Check if we're already relocated (compare current PC with RELOC_DEST)
    li   t1, RELOC_DEST
    auipc t0, 0              // Get current PC
    bgeu t0, t1, relocated_entry
    
    // Check if we're running at LOAD_BASE (loaded by bootloader)
    // In this case, skip relocation - we want to run in place
    li   t1, LOAD_BASE
    // t0 still contains current PC from auipc
    li   t2, 0x1000          // Allow 4KB tolerance for PC offset
    sub  t3, t0, t1          // t3 = current_pc - LOAD_BASE
    bltu t3, t2, skip_relocation  // If within tolerance, skip relocation

    // === RELOCATION CODE ===
    // Copy bootloader from LOAD_BASE to RELOC_DEST
    li   t0, LOAD_BASE       // Source start
    la   t1, _load_end       // Source end (PC-relative)
    li   t2, RELOC_DEST      // Destination

copy_loop:
    bgeu t0, t1, copy_done
    ld   t3, 0(t0)           // Load 8 bytes
    sd   t3, 0(t2)           // Store 8 bytes
    addi t0, t0, 8
    addi t2, t2, 8
    j    copy_loop

copy_done:
    // Jump to relocated _start
    mv   a0, s0              // 恢復 Hart ID 到 a0
    mv   a1, s1              // 恢復 DTB address 到 a1
    li   t0, RELOC_DEST
    jr   t0

skip_relocation:
    // We're running at LOAD_BASE (loaded via UART bootloader)
    // Skip relocation and continue directly to bss clear
    j    relocated_entry

relocated_entry:
    // Now running from relocated address
    
    // Clear the .bss section
    la   t0, __bss_start
    la   t1, __bss_stop

bss_clear:
    bgeu t0, t1, bss_done
    sd   zero, 0(t0)         // Store 8 bytes of zeros
    addi t0, t0, 8
    j    bss_clear

bss_done:
    // Set up the stack pointer (stack grows downward, _end is top of stack)
    la   sp, _end

    // Call start_kernel with hartid in a0, DTB address in a1
    mv   a0, s0
    mv   a1, s1
    tail start_kernel

    // === 3. Park Core ===
    // 非主核心會陷入這個無窮迴圈
park_core:
    wfi                      // Wait for Interrupt
    j    park_core