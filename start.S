.section ".text.boot"
.global _start
_start:
    // Save a1 (DTB address) to s1 before clearing BSS
    mv   s1, a1

    // Clear the .bss section
    la   t0, __bss_start
    la   t1, __bss_stop
bss_clear:
    bgeu t0, t1, bss_done
    sb   zero, 0(t0)
    addi t0, t0, 1
    j    bss_clear
bss_done:
    // Set up the stack pointer (stack grows downward from _end)
    la   sp, _end

    // Restore DTB address to a0 as first argument to start_kernel
    mv   a0, s1
    tail start_kernel
