#include "tmpfs.h"

#include "helper.h"
#include "kmalloc.h"
#include "vfs.h"

#define TMPFS_MAX_ENTRIES 16
#define TMPFS_MAX_FILE_SIZE 4096

struct tmpfs_node {
    char name[VFS_MAX_NAME + 1];
    int type;
    int readonly;
    struct vnode vnode;
    struct tmpfs_node *parent;
    const char *ro_data;
    size_t size;
    char data[TMPFS_MAX_FILE_SIZE];
    int child_count;
    int child_cap;
    struct tmpfs_node **children;
};

static struct file_operations tmpfs_file_ops;
static struct vnode_operations tmpfs_vnode_ops;

static int name_too_long(const char *name) {
    return strlen(name) > VFS_MAX_NAME;
}

static struct tmpfs_node *vnode_to_tmpfs(struct vnode *vnode) {
    if (vnode == (void *)0) {
        return (void *)0;
    }
    return (struct tmpfs_node *)vnode->internal;
}

static int tmpfs_init_children(struct tmpfs_node *node, int cap) {
    node->children = (struct tmpfs_node **)allocate(
        sizeof(struct tmpfs_node *) * (unsigned int)cap);
    if (node->children == (void *)0) {
        return -1;
    }
    memset(node->children, 0, sizeof(struct tmpfs_node *) * (unsigned int)cap);
    node->child_cap = cap;
    return 0;
}

static struct tmpfs_node *tmpfs_alloc_node(struct mount *mnt,
                                           struct tmpfs_node *parent,
                                           const char *name, int type,
                                           int readonly, int child_cap) {
    if (name == (void *)0 || name_too_long(name)) {
        return (void *)0;
    }

    struct tmpfs_node *node =
        (struct tmpfs_node *)allocate(sizeof(struct tmpfs_node));
    if (node == (void *)0) {
        return (void *)0;
    }
    memset(node, 0, sizeof(*node));
    strncpy(node->name, name, VFS_MAX_NAME);
    node->name[VFS_MAX_NAME] = '\0';
    node->type = type;
    node->readonly = readonly;
    node->parent = parent;
    if (type == VNODE_DIR && tmpfs_init_children(node, child_cap) < 0) {
        free(node);
        return (void *)0;
    }
    node->vnode.mount = mnt;
    node->vnode.parent = parent == (void *)0 ? (void *)0 : &parent->vnode;
    node->vnode.v_ops = &tmpfs_vnode_ops;
    node->vnode.f_ops = &tmpfs_file_ops;
    node->vnode.internal = node;
    node->vnode.type = type;
    return node;
}

static int tmpfs_add_child(struct tmpfs_node *dir, struct tmpfs_node *child) {
    if (dir == (void *)0 || child == (void *)0 || dir->type != VNODE_DIR ||
        dir->child_count >= dir->child_cap) {
        return -1;
    }
    dir->children[dir->child_count++] = child;
    return 0;
}

static struct tmpfs_node *tmpfs_find_child(struct tmpfs_node *dir,
                                           const char *name) {
    if (dir == (void *)0 || dir->type != VNODE_DIR || name == (void *)0) {
        return (void *)0;
    }
    for (int i = 0; i < dir->child_count; i++) {
        if (strcmp(dir->children[i]->name, name) == 0) {
            return dir->children[i];
        }
    }
    return (void *)0;
}

static int tmpfs_lookup(struct vnode *dir_node, struct vnode **target,
                        const char *component_name) {
    struct tmpfs_node *dir = vnode_to_tmpfs(dir_node);
    if (dir == (void *)0 || target == (void *)0 ||
        component_name == (void *)0 || dir->type != VNODE_DIR) {
        return -1;
    }

    struct tmpfs_node *child = tmpfs_find_child(dir, component_name);
    if (child == (void *)0) {
        return -1;
    }
    *target = &child->vnode;
    return 0;
}

static int tmpfs_create_common(struct vnode *dir_node, struct vnode **target,
                               const char *name, int type) {
    struct tmpfs_node *dir = vnode_to_tmpfs(dir_node);
    if (dir == (void *)0 || target == (void *)0 || name == (void *)0 ||
        dir->type != VNODE_DIR || dir->readonly || name[0] == '\0' ||
        name_too_long(name) || tmpfs_find_child(dir, name) != (void *)0) {
        return -1;
    }

    struct tmpfs_node *child = tmpfs_alloc_node(
        dir_node->mount, dir, name, type, 0, TMPFS_MAX_ENTRIES);
    if (child == (void *)0 || tmpfs_add_child(dir, child) < 0) {
        if (child != (void *)0) {
            free(child);
        }
        return -1;
    }
    *target = &child->vnode;
    return 0;
}

static int tmpfs_create(struct vnode *dir_node, struct vnode **target,
                        const char *component_name) {
    return tmpfs_create_common(dir_node, target, component_name, VNODE_FILE);
}

static int tmpfs_mkdir(struct vnode *dir_node, struct vnode **target,
                       const char *component_name) {
    return tmpfs_create_common(dir_node, target, component_name, VNODE_DIR);
}

static int tmpfs_open(struct vnode *file_node, struct file **target) {
    if (file_node == (void *)0 || target == (void *)0) {
        return -1;
    }
    struct file *file = (struct file *)allocate(sizeof(struct file));
    if (file == (void *)0) {
        return -1;
    }
    memset(file, 0, sizeof(*file));
    file->vnode = file_node;
    file->f_ops = file_node->f_ops;
    file->refcnt = 1;
    *target = file;
    return 0;
}

static int tmpfs_close(struct file *file) {
    if (file == (void *)0) {
        return -1;
    }
    free(file);
    return 0;
}

static int tmpfs_read(struct file *file, void *buf, size_t len) {
    struct tmpfs_node *node =
        file == (void *)0 ? (void *)0 : vnode_to_tmpfs(file->vnode);
    if (node == (void *)0 || node->type != VNODE_FILE) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if (buf == (void *)0) {
        return -1;
    }
    if (file->f_pos >= node->size) {
        return 0;
    }

    size_t n = node->size - file->f_pos;
    if (n > len) {
        n = len;
    }
    const char *src = node->ro_data != (void *)0 ? node->ro_data : node->data;
    memcpy(buf, src + file->f_pos, n);
    file->f_pos += n;
    return (int)n;
}

static int tmpfs_write(struct file *file, const void *buf, size_t len) {
    struct tmpfs_node *node =
        file == (void *)0 ? (void *)0 : vnode_to_tmpfs(file->vnode);
    if (node == (void *)0 || node->type != VNODE_FILE || node->readonly) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if (buf == (void *)0) {
        return -1;
    }
    if (file->f_pos > TMPFS_MAX_FILE_SIZE) {
        return -1;
    }

    size_t n = TMPFS_MAX_FILE_SIZE - file->f_pos;
    if (n > len) {
        n = len;
    }
    memcpy(node->data + file->f_pos, buf, n);
    file->f_pos += n;
    if (file->f_pos > node->size) {
        node->size = file->f_pos;
    }
    return (int)n;
}

static long tmpfs_lseek64(struct file *file, long offset, int whence) {
    struct tmpfs_node *node =
        file == (void *)0 ? (void *)0 : vnode_to_tmpfs(file->vnode);
    if (node == (void *)0 || node->type != VNODE_FILE ||
        whence != VFS_SEEK_SET || offset < 0 ||
        (unsigned long)offset > TMPFS_MAX_FILE_SIZE) {
        return -1;
    }
    file->f_pos = (size_t)offset;
    return offset;
}

static struct file_operations tmpfs_file_ops = {
    .open = tmpfs_open,
    .close = tmpfs_close,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .lseek64 = tmpfs_lseek64,
};

static struct vnode_operations tmpfs_vnode_ops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .mkdir = tmpfs_mkdir,
};

int tmpfs_setup_mount(struct filesystem *fs, struct mount *mnt, int readonly,
                      int child_cap) {
    struct tmpfs_node *root =
        tmpfs_alloc_node(mnt, (void *)0, "", VNODE_DIR, readonly, child_cap);
    if (root == (void *)0) {
        return -1;
    }
    mnt->fs = fs;
    mnt->root = &root->vnode;
    return 0;
}

int tmpfs_add_readonly_file(struct vnode *root_vnode, const char *path,
                            const char *data, size_t size, int child_cap) {
    struct tmpfs_node *root = vnode_to_tmpfs(root_vnode);
    struct tmpfs_node *dir = root;
    const char *p = path;
    char name[VFS_MAX_NAME + 1];

    while (1) {
        int ret = vfs_next_path_component(&p, name);
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            return 0;
        }

        const char *save = p;
        char probe[VFS_MAX_NAME + 1];
        int last = vfs_next_path_component(&save, probe) == 0;
        if (last) {
            struct tmpfs_node *file = tmpfs_find_child(dir, name);
            if (file == (void *)0) {
                file = tmpfs_alloc_node(root_vnode->mount, dir, name,
                                        VNODE_FILE, 1, 0);
                if (file == (void *)0 || tmpfs_add_child(dir, file) < 0) {
                    return -1;
                }
            } else if (file->type != VNODE_FILE) {
                return -1;
            }
            file->ro_data = data;
            file->size = size;
            return 0;
        }

        struct tmpfs_node *next = tmpfs_find_child(dir, name);
        if (next == (void *)0) {
            next = tmpfs_alloc_node(root_vnode->mount, dir, name, VNODE_DIR, 1,
                                    child_cap);
            if (next == (void *)0 || tmpfs_add_child(dir, next) < 0) {
                return -1;
            }
        }
        if (next->type != VNODE_DIR) {
            return -1;
        }
        dir = next;
    }
}

static int tmpfs_mount(struct filesystem *fs, struct mount *mnt) {
    return tmpfs_setup_mount(fs, mnt, 0, TMPFS_MAX_ENTRIES);
}

static struct filesystem tmpfs = {
    .name = "tmpfs",
    .setup_mount = tmpfs_mount,
};

int tmpfs_register(void) {
    return register_filesystem(&tmpfs);
}
