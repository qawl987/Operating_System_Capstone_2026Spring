#ifndef CONFIG_H
#define CONFIG_H

#ifdef PLATFORM_QEMU
/* QEMU virt machine addresses */
#define LOAD_ADDR 0x80200000ULL
#define RELOC_ADDR 0x84000000ULL
#else
/* Orange Pi RV2 addresses */
#define LOAD_ADDR 0x00200000ULL
#define RELOC_ADDR 0x20000000ULL
#endif

/* Common definitions */
#define BOOT_MAGIC 0x544F4F42 /* "BOOT" in hex */

/* Linker script symbols */
extern char _kernel_start[];
extern char _kernel_end[];
extern char _load_start[];
extern char _load_end[];

#endif /* CONFIG_H */
