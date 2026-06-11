#include "ramfs.h"

#include "helper.h"
#include "initrd.h"
#include "tmpfs.h"
#include "vfs.h"

#define RAMFS_MAX_ENTRIES 64
#define CPIO_MODE_TYPE_MASK 0170000
#define CPIO_MODE_DIR 0040000

static unsigned long ramfs_initrd_start;
static unsigned long ramfs_initrd_end;

static int ramfs_populate(struct mount *mnt) {
    if (ramfs_initrd_start == 0 || ramfs_initrd_end == 0) {
        return 0;
    }

    struct initrd_iter it;
    initrd_iter_begin(&it, (void *)ramfs_initrd_start,
                      (void *)ramfs_initrd_end);
    // iterate file in initrd
    while (initrd_iter_next(&it) == 0) {
        // skip directory
        if (strcmp(it.name, ".") == 0 || strcmp(it.name, "TRAILER!!!") == 0 ||
            (it.mode & CPIO_MODE_TYPE_MASK) == CPIO_MODE_DIR) {
            continue;
        }
        // accept multi level, ex: /dir/a.txt
        if (tmpfs_add_readonly_file(mnt->root, it.name, (const char *)it.data,
                                    it.size, RAMFS_MAX_ENTRIES) < 0) {
            return -1;
        }
    }
    return 0;
}

static int ramfs_mount(struct filesystem *fs, struct mount *mnt) {
    // use tmpfs but readonly=true
    if (tmpfs_setup_mount(fs, mnt, 1, RAMFS_MAX_ENTRIES) < 0) {
        return -1;
    }
    return ramfs_populate(mnt);
}

static struct filesystem ramfs = {
    .name = "ramfs",
    .setup_mount = ramfs_mount,
};

int ramfs_register(unsigned long initrd_start, unsigned long initrd_end) {
    ramfs_initrd_start = initrd_start;
    ramfs_initrd_end = initrd_end;
    return register_filesystem(&ramfs);
}
