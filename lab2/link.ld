OUTPUT_ARCH(riscv)
ENTRY(_start)

/* Relocation address for bootloader */
RELOC_ADDR = 0x84000000;

SECTIONS
{
    . = 0x80200000;
    
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
    _end = .;
    
    /* Calculate size for relocation */
    _load_size = _load_end - _load_start;
}
