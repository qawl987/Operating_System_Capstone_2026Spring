#ifndef CONFIG_H
#define CONFIG_H

/*
 * Log Level Configuration
 * Change this to control verbosity during demo:
 * - LOG_LEVEL_NONE  (0): No logs
 * - LOG_LEVEL_SPEC  (1): Only spec-required logs (for demo)
 * - LOG_LEVEL_INFO  (2): Spec + initialization info
 * - LOG_LEVEL_DEBUG (3): All logs (default)
 */
#define LOG_LEVEL 1 /* Set to 1 for demo, 3 for development */

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
