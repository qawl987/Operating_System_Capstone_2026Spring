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

.equ RELOC_OFFSET, (RELOC_DEST - LOAD_BASE)

_start:
    // Save a1 (DTB address) to s1 before anything
    mv   s1, a1

    // Check if we're already relocated
    li   t1, RELOC_DEST
    auipc t0, 0              // Get current PC
    bgeu t0, t1, relocated_entry

    // === RELOCATION CODE ===
    // Copy bootloader from LOAD_BASE to RELOC_DEST
    li   t0, LOAD_BASE       // Source start
    la   t1, _load_end       // Source end
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
    mv   a1, s1              // Restore DTB address to a1
    li   t0, RELOC_DEST
    jr   t0

relocated_entry:
    // Now running from relocated address
    mv   s1, a1

    // === FIX GOT ENTRIES ===
    // GOT contains pointers to the old location, need to add offset
    li   s2, RELOC_OFFSET    // s2 = relocation offset
    
    la   t0, _got_start
    add  t0, t0, s2          // Adjust _got_start to new location
    la   t1, _got_end
    add  t1, t1, s2          // Adjust _got_end to new location

fix_got:
    bgeu t0, t1, got_done
    ld   t2, 0(t0)           // Load GOT entry
    add  t2, t2, s2          // Add relocation offset
    sd   t2, 0(t0)           // Store fixed entry
    addi t0, t0, 8
    j    fix_got

got_done:
    // Clear the .bss section (now GOT is fixed, la works correctly)
    la   t0, __bss_start
    add  t0, t0, s2          // Still need manual adjustment here
    la   t1, __bss_stop
    add  t1, t1, s2

bss_clear:
    bgeu t0, t1, bss_done
    sb   zero, 0(t0)
    addi t0, t0, 1
    j    bss_clear

bss_done:
    // Set up the stack pointer
    la   sp, _end
    add  sp, sp, s2

    // Call start_kernel with DTB address
    mv   a0, s1
    tail start_kernel
