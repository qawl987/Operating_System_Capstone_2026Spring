#include "helper.h"

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

struct path {
    char path[256];
    int len;
};

struct nodeArr {
    int nodelen[32];
    int len;
};

// Check if path node names match
// s1: current full path scanned in DTB (e.g., "/memory@80000000")
// s2: target path (e.g., "/memory")
int path_matches(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        if (*s1 == *s2) {
            s1++;
            s2++;
        } else if (*s1 == '@' && (*s2 == '/' || *s2 == '\0')) {
            // If DTB reached '@' but target path ended or entering next level
            // Names match (ignoring address part)
            // Skip address part in s1 until '/' or end
            while (*s1 && *s1 != '/')
                s1++;
        } else {
            return 0; // No match
        }
    }
    // If s2 ended but s1 still has address info
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
                // This is root node
                if (fpath.len == 0) {
                    fpath.path[fpath.len++] = '/';
                    fpath.path[fpath.len] = '\0';
                    added_len = 1;
                }
            } else {
                // Non-root node
                // Fix: only add "/" if current path is not just "/"
                if (fpath.len > 1 || (fpath.len == 1 && fpath.path[0] != '/')) {
                    fpath.path[fpath.len++] = '/';
                    added_len++;
                } else if (fpath.len == 0) {
                    // Theoretically BEGIN_NODE should have root before, but add safety check
                    fpath.path[fpath.len++] = '/';
                    added_len++;
                }

                for (int i = 0; name[i] != '\0'; i++) {
                    fpath.path[fpath.len++] = name[i];
                    added_len++;
                }
                fpath.path[fpath.len] = '\0';
            }

            // Push total added length to stack
            node_arr.nodelen[node_arr.len++] = added_len;
            // printf("Comparing: '%s' with '%s'\n", fpath.path, target_path);
            if (path_matches(fpath.path, target_path)) {
                return (int)((const char *)node_base_ptr - (const char *)fdt);
            }

            p = (const char *)align_up(p + strlen(name) + 1, 4);

        } else if (tag == FDT_PROP) {
            p += 4;
            uint32_t len = bswap32(*(uint32_t *)p);
            p = (const char *)align_up(p + 8 + len, 4);

        } else if (tag == FDT_END_NODE) {
            p += 4;
            if (node_arr.len > 0) {
                int last_added_len = node_arr.nodelen[--node_arr.len];
                fpath.len -= last_added_len; // Key fix: sync update length
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
    p += 4;                                           // Skip tag
    p = (const char *)align_up(p + strlen(p) + 1, 4); // Skip node name and align

    while (1) {
        uint32_t tag = bswap32(*(uint32_t *)p);
        if (tag == FDT_PROP) {
            p += 4; // len
            uint32_t prop_len = bswap32(*(uint32_t *)p);
            p += 4; // name offset
            uint32_t name_off = bswap32(*(uint32_t *)p);
            p += 4; // data
            const char *prop_name = strings_base + name_off;
            if (strcmp(prop_name, name) == 0) {
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
