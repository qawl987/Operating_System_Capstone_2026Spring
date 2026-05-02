#include "framebuffer.h"

#include "config.h"
#include "helper.h"

#define XRGB8888 875713112U
#define QEMU_PACKED __attribute__((packed))

struct QEMU_PACKED ramfb_cfg {
    uint64_t addr;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

#ifdef PLATFORM_QEMU
#define FW_CFG_BASE 0x10100000UL
#define FW_CFG_DMA_CTL_ERROR 0x01U
#define FW_CFG_DMA_CTL_READ 0x02U
#define FW_CFG_DMA_CTL_SELECT 0x08U
#define FW_CFG_DMA_CTL_WRITE 0x10U
#define FW_CFG_FILE_DIR 0x19U

struct QEMU_PACKED fw_cfg_file {
    uint32_t size;
    uint16_t select;
    uint16_t reserved;
    char name[56];
};

struct QEMU_PACKED fw_cfg_dma_access {
    uint32_t control;
    uint32_t length;
    uint64_t address;
};

static volatile uint64_t *const fw_cfg_dma =
    (volatile uint64_t *)(FW_CFG_BASE + 0x10);

static uint16_t bswap16(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}

static void fw_cfg_dma_transfer(void *address, uint32_t length,
                                uint32_t control) {
    struct fw_cfg_dma_access access = {
        .control = bswap32(control),
        .length = bswap32(length),
        .address = bswap64((uint64_t)address),
    };
    *fw_cfg_dma = bswap64((uint64_t)&access);
    while ((bswap32(access.control) & ~FW_CFG_DMA_CTL_ERROR) != 0) {
    }
}

static void fw_cfg_read_entry(void *buf, uint32_t entry, uint32_t len) {
    uint32_t control = (entry << 16) | FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_READ;
    fw_cfg_dma_transfer(buf, len, control);
}

static void fw_cfg_write_entry(void *buf, uint32_t entry, uint32_t len) {
    uint32_t control = (entry << 16) | FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE;
    fw_cfg_dma_transfer(buf, len, control);
}

static int fw_cfg_find_file(const char *name) {
    uint32_t count = 0;
    fw_cfg_read_entry(&count, FW_CFG_FILE_DIR, sizeof(count));
    count = bswap32(count);
    for (uint32_t i = 0; i < count; i++) {
        struct fw_cfg_file file;
        fw_cfg_dma_transfer(&file, sizeof(file), FW_CFG_DMA_CTL_READ);
        if (strncmp(name, file.name, sizeof(file.name)) == 0) {
            return bswap16(file.select);
        }
    }
    return -1;
}
#endif

#ifdef PLATFORM_PI
#define CACHE_BLOCK_SIZE 64UL

static void cbo_flush_line(unsigned long addr) {
    asm volatile("mv a0, %0\n\t.word 0x0025200F"
                 :
                 : "r"(addr)
                 : "memory", "a0");
}

static void flush_dcache(void *addr, unsigned long len) {
    unsigned long start = (unsigned long)addr & ~(CACHE_BLOCK_SIZE - 1UL);
    unsigned long end = (unsigned long)addr + len;
    asm volatile("fence rw, rw" ::: "memory");
    for (unsigned long line = start; line < end; line += CACHE_BLOCK_SIZE) {
        cbo_flush_line(line);
    }
    asm volatile("fence rw, rw" ::: "memory");
}
#else
static void flush_dcache(void *addr, unsigned long len) {
    (void)addr;
    (void)len;
}
#endif

int framebuffer_init(void) {
#ifdef PLATFORM_QEMU
    int ramfb = fw_cfg_find_file("etc/ramfb");
    if (ramfb < 0) {
        return -1;
    }
    struct ramfb_cfg cfg = {
        .addr = bswap64(FRAMEBUFFER_BASE),
        .fourcc = bswap32(XRGB8888),
        .flags = bswap32(0),
        .width = bswap32(FRAMEBUFFER_WIDTH),
        .height = bswap32(FRAMEBUFFER_HEIGHT),
        .stride = bswap32(FRAMEBUFFER_WIDTH * FRAMEBUFFER_BPP),
    };
    fw_cfg_write_entry(&cfg, (uint32_t)ramfb, sizeof(cfg));
#endif
    return 0;
}

int framebuffer_display(const unsigned int *bmp_image, unsigned int width,
                        unsigned int height) {
    if (bmp_image == (void *)0 || width == 0 || height == 0 ||
        width > FRAMEBUFFER_WIDTH || height > FRAMEBUFFER_HEIGHT) {
        return -1;
    }

    unsigned int *fb = (unsigned int *)FRAMEBUFFER_BASE;
    unsigned int start_x = (FRAMEBUFFER_WIDTH - width) / 2;
    unsigned int start_y = (FRAMEBUFFER_HEIGHT - height) / 2;
    for (unsigned int y = 0; y < height; y++) {
        unsigned int *dst = fb + (start_y + y) * FRAMEBUFFER_WIDTH + start_x;
        memcpy(dst, bmp_image + y * width, width * sizeof(unsigned int));
        flush_dcache(dst, width * sizeof(unsigned int));
    }
    return 0;
}
