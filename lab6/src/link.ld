OUTPUT_ARCH(riscv)
ENTRY(_start)

PAGE_OFFSET = 0xffffffc000000000;
LOAD_ADDR = 0x80200000;
RELOC_ADDR = 0x84000000;

SECTIONS
{
    . = PAGE_OFFSET + LOAD_ADDR;
    _load_start = .;
    _kernel_start = .;

    .text : AT(ADDR(.text) - PAGE_OFFSET) {
        *(.text.boot)
        *(.text.entry)
        *(.text)
    }

    .rodata : AT(ADDR(.rodata) - PAGE_OFFSET) { *(.rodata*) }

    .data : AT(ADDR(.data) - PAGE_OFFSET) { *(.data.boot) *(.data) }

    .got : AT(ADDR(.got) - PAGE_OFFSET) { *(.got) *(.got.plt) }

    .bss : AT(ADDR(.bss) - PAGE_OFFSET) {
        __bss_start = .;
        *(.bss.boot) *(.sbss) *(.bss)
        __bss_stop = .;
    }

    . = ALIGN(16);
    _load_end = .;

    . = . + 0x4000;
    _kernel_end = .;
    _end = .;

    _load_size = _load_end - _load_start;
}
