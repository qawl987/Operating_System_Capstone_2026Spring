OUTPUT_ARCH(riscv)
ENTRY(_start)

/* Orange Pi RV2 addresses */
RELOC_ADDR = 0x20000000;

SECTIONS
{
    . = 0x00200000;
    
    _kernel_start = .;
    _load_start = .;
    
    .text : {
        *(.text.boot)
        *(.text)
    }
    
    .rodata : { *(.rodata*) }
    
    .data : { *(.data) }
    
    .got : { *(.got) *(.got.plt) }
    
    .bss : {
        __bss_start = .;
        *(.sbss) *(.bss)
        __bss_stop = .;
    }
    
    . = ALIGN(16);
    _load_end = .;
    
    /* Stack space */
    . = . + 0x4000;
    _kernel_end = .;
    _end = .;
    
    /* Calculate size for relocation */
    _load_size = _load_end - _load_start;
}
