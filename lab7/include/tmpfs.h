#ifndef TMPFS_H
#define TMPFS_H

#include <stddef.h>

#include "vfs.h"

int tmpfs_register(void);
int tmpfs_setup_mount(struct filesystem *fs, struct mount *mnt, int readonly,
                      int child_cap);
int tmpfs_add_readonly_file(struct vnode *root_vnode, const char *path,
                            const char *data, size_t size, int child_cap);

#endif /* TMPFS_H */
