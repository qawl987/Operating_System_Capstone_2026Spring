#include <stddef.h>
#include <stdint.h>

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE 0x00000002
#define FDT_PROP 0x00000003
#define FDT_NOP 0x00000004
#define FDT_END 0x00000009

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

static inline uint32_t bswap32(uint32_t x) {
    return ((x & 0x000000FFU) << 24) |
           ((x & 0x0000FF00U) << 8)  |
           ((x & 0x00FF0000U) >> 8)  |
           ((x & 0xFF000000U) >> 24);
}

static inline uint64_t bswap64(uint64_t x) {
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8)  |
           ((x & 0x000000FF00000000ULL) >> 8)  |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
}

static inline const void *align_up(const void *ptr, size_t align) {
    return (const void *)(((uintptr_t)ptr + align - 1) & ~(align - 1));
}

static size_t my_strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

static int my_strcmp(const char *s1, const char *s2) {
    while (*s1 != '\0' && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

struct path {
    char path[64];
    int len;
};

struct nodeArr {
    int nodelen[32];
    int len;
};

// 檢查路徑中的節點名稱是否匹配
// s1: 目前在 DTB 掃描到的節點全路徑 (例如 "/memory@80000000")
// s2: 目標路徑 (例如 "/memory")
int path_matches(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        if (*s1 == *s2) {
            s1++;
            s2++;
        } else if (*s1 == '@' && (*s2 == '/' || *s2 == '\0')) {
            // 如果 DTB 目前到了 @，但目標路徑已經結束或是要進入下一層
            // 代表名稱匹配（忽略位址部分）
            // 跳過 s1 的位址部分直到遇到 '/' 或結束
            while (*s1 && *s1 != '/')
                s1++;
        } else {
            return 0; // 完全不匹配
        }
    }
    // 如果 s2 結束了，但 s1 還有位址資訊
    if (*s2 == '\0' && *s1 == '@')
        return 1;

    return (*s1 == *s2);
}

int fdt_path_offset(const void *fdt, const char *target_path) {
    struct fdt_header *header = (struct fdt_header *)fdt;
    uint32_t struct_off = bswap32(header->off_dt_struct);
    const char *p = (const char *)fdt + struct_off;

    struct path fpath;
    fpath.path[0] = '\0';
    fpath.len = 0;
    struct nodeArr node_arr;
    node_arr.len = 0;

    while (1) {
        uint32_t tag = bswap32(*(uint32_t *)p);
        const char *node_base_ptr = p;

        if (tag == FDT_BEGIN_NODE) {
            p += 4;
            const char *name = p;
            int added_len = 0;

            if (*name == '\0') {
                // 這是根節點
                if (fpath.len == 0) {
                    fpath.path[fpath.len++] = '/';
                    fpath.path[fpath.len] = '\0';
                    added_len = 1;
                }
            } else {
                // 非根節點
                // 修正：如果目前不是只有 "/"，才需要補 "/"
                if (fpath.len > 1 || (fpath.len == 1 && fpath.path[0] != '/')) {
                    fpath.path[fpath.len++] = '/';
                    added_len++;
                } else if (fpath.len == 0) {
                    // 理論上 BEGIN_NODE 之前應該會有 root，但為了保險加個邏輯
                    fpath.path[fpath.len++] = '/';
                    added_len++;
                }

                for (int i = 0; name[i] != '\0'; i++) {
                    fpath.path[fpath.len++] = name[i];
                    added_len++;
                }
                fpath.path[fpath.len] = '\0';
            }

            // 將這一層「總共增加的長度」推入 Stack
            node_arr.nodelen[node_arr.len++] = added_len;
            // printf("Comparing: '%s' with '%s'\n", fpath.path, target_path);
            if (path_matches(fpath.path, target_path)) {
                return (int)((const char *)node_base_ptr - (const char *)fdt);
            }

            p = (const char *)align_up(p + my_strlen(name) + 1, 4);

        } else if (tag == FDT_PROP) {
            p += 4;
            uint32_t len = bswap32(*(uint32_t *)p);
            p = (const char *)align_up(p + 8 + len, 4);

        } else if (tag == FDT_END_NODE) {
            p += 4;
            if (node_arr.len > 0) {
                int last_added_len = node_arr.nodelen[--node_arr.len];
                fpath.len -= last_added_len; // 核心修正：同步更新長度
                fpath.path[fpath.len] = '\0';
            }

        } else if (tag == FDT_NOP) {
            p += 4;

        } else if (tag == FDT_END) {
            break;

        } else {
            break;
        }
    }
    return -1;
}

const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name,
                        int *lenp) {
    struct fdt_header *header = (struct fdt_header *)fdt;
    uint32_t strings_off = bswap32(header->off_dt_strings);
    const char *strings_base = (const char *)fdt + strings_off;
    const char *p = (const char *)fdt + nodeoffset;

    uint32_t tag = bswap32(*(uint32_t *)p);
    if (tag != FDT_BEGIN_NODE)
        return NULL;
    p += 4;                                           // 跳過 Tag
    p = (const char *)align_up(p + my_strlen(p) + 1, 4); // 跳過 Node Name 並對齊

    while (1) {
        uint32_t tag = bswap32(*(uint32_t *)p);
        if (tag == FDT_PROP) {
            p += 4; // len
            uint32_t prop_len = bswap32(*(uint32_t *)p);
            p += 4; // name offset
            uint32_t name_off = bswap32(*(uint32_t *)p);
            p += 4; // data
            const char *prop_name = strings_base + name_off;
            if (my_strcmp(prop_name, name) == 0) {
                if (lenp)
                    *lenp = (int)prop_len;
                return p;
            }
            p = (const char *)align_up(p + prop_len, 4);
        } else if (tag == FDT_NOP) {
            p += 4;
        } else {
            break;
        }
    }
    return NULL;
}
