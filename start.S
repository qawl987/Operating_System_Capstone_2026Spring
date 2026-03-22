.section ".text.boot"
.global _start

_start:
    // Save a1 (DTB address) to s1 before anything
    mv   s1, a1

    // Check if we're already relocated
    // If PC is >= 0x84000000, we're running from relocated address
    auipc t0, 0              // Get current PC
    li   t1, 0x84000000
    bgeu t0, t1, relocated_entry

    // === RELOCATION CODE ===
    // Copy bootloader from 0x80200000 to 0x84000000
    li   t0, 0x80200000      // Source start
    la   t1, _load_end       // Source end
    li   t2, 0x84000000      // Destination

copy_loop:
    bgeu t0, t1, copy_done
    ld   t3, 0(t0)           // Load 8 bytes
    sd   t3, 0(t2)           // Store 8 bytes
    addi t0, t0, 8
    addi t2, t2, 8
    j    copy_loop

copy_done:
    // Jump to relocated _start (0x84000000)
    mv   a1, s1              // Restore DTB address to a1
    li   t0, 0x84000000
    jr   t0

relocated_entry:
    // Now running from 0x84000000 region
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
