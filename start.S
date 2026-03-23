.section ".text.boot"
.global _start

/* Import linker symbols */
.extern RELOC_ADDR
.extern _load_start
.extern _load_end

_start:
    // Save a1 (DTB address) to s1 before anything
    mv   s1, a1

    // Check if we're already relocated
    // Load RELOC_ADDR from linker script
    la   t1, RELOC_ADDR
    auipc t0, 0              // Get current PC
    bgeu t0, t1, relocated_entry

    // === RELOCATION CODE ===
    // Copy bootloader from _load_start to RELOC_ADDR
    la   t0, _load_start     // Source start
    la   t1, _load_end       // Source end
    la   t2, RELOC_ADDR      // Destination

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
    la   t0, RELOC_ADDR
    jr   t0

relocated_entry:
    // Now running from relocated region
    mv   s1, a1

    // Clear the .bss section
    // Use PC-relative addressing - works because code is copied as a whole
    la   t0, __bss_start
    la   t1, __bss_stop

bss_clear:
    bgeu t0, t1, bss_done
    sb   zero, 0(t0)
    addi t0, t0, 1
    j    bss_clear

bss_done:
    // Set up the stack pointer
    la   sp, _end

    // Call start_kernel with DTB address
    mv   a0, s1
    tail start_kernel
