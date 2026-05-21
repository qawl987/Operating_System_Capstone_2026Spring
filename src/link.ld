OUTPUT_ARCH(riscv)
ENTRY(_start)

PAGE_OFFSET = 0xffffffc000000000;
LOAD_ADDR = 0x80200000;
RELOC_ADDR = 0x84000000;

SECTIONS
{
    . = LOAD_ADDR;
    _boot_start = .;
    _load_start = .;

    .text.boot : {
        *(.text.boot)
    }

    .data.boot : {
        *(.data.boot)
    }

    .bss.boot : {
        *(.bss.boot)
    }

    . = ALIGN(0x1000);
    _boot_end = .;

    . = PAGE_OFFSET + 0x80400000;
    _kernel_start = .;

    .text : AT(ADDR(.text) - PAGE_OFFSET) {
        *(.text.entry)
        *(.text)
    }

    .rodata : AT(ADDR(.rodata) - PAGE_OFFSET) { *(.rodata*) }

    .data : AT(ADDR(.data) - PAGE_OFFSET) { *(.data) }

    .got : AT(ADDR(.got) - PAGE_OFFSET) { *(.got) *(.got.plt) }

    .bss : AT(ADDR(.bss) - PAGE_OFFSET) {
        __bss_start = .;
        *(.sbss) *(.bss)
        __bss_stop = .;
    }

    . = ALIGN(16);
    _load_end = .;

    . = . + 0x4000;
    _kernel_end = .;
    _end = .;

    _load_size = _load_end - _load_start;
}
