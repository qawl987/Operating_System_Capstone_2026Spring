.section ".text.boot"
.global _start
_start:
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
    tail start_kernel
