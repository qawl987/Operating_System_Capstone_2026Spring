#include "devfs.h"

#include "config.h"
#include "framebuffer.h"
#include "helper.h"
#include "kmalloc.h"
#include "thread.h"
#include "uart.h"
#include "vfs.h"

#define DEVFS_ENTRIES 2
#define FB_IOCTL_GET_INFO 0

struct devfs_node {
    const char *name;
    struct vnode vnode;
};

struct devfs_mount {
    struct devfs_node root;
    struct devfs_node nodes[DEVFS_ENTRIES];
};

static struct file_operations devfs_dir_ops;
static struct file_operations uart_file_ops;
static struct file_operations fb_file_ops;
static struct vnode_operations devfs_vnode_ops;

static int devfs_open_common(struct vnode *file_node, struct file **target) {
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

static int devfs_close(struct file *file) {
    if (file == (void *)0) {
        return -1;
    }
    free(file);
    return 0;
}

static int uart_open(struct vnode *file_node, struct file **target) {
    return devfs_open_common(file_node, target);
}

static int uart_read_file(struct file *file, void *buf, size_t len) {
    // not used but unify interface
    (void)file;
    if (len == 0) {
        return 0;
    }
    if (buf == (void *)0) {
        return -1;
    }
    char *out = (char *)buf;
    for (size_t i = 0; i < len; i++) {
        while (uart_try_getc(&out[i]) < 0) {
            schedule();
        }
    }
    return (int)len;
}

static int uart_write_file(struct file *file, const void *buf, size_t len) {
    (void)file;
    if (len == 0) {
        return 0;
    }
    if (buf == (void *)0) {
        return -1;
    }
    const char *in = (const char *)buf;
    for (size_t i = 0; i < len; i++) {
        uart_putc(in[i]);
    }
    return (int)len;
}

static long no_lseek64(struct file *file, long offset, int whence) {
    (void)file;
    (void)offset;
    (void)whence;
    return -1;
}

static int no_ioctl(struct file *file, unsigned long request, void *arg) {
    (void)file;
    (void)request;
    (void)arg;
    return -1;
}

static int fb_open(struct vnode *file_node, struct file **target) {
    if (framebuffer_init() < 0) {
        return -1;
    }
    return devfs_open_common(file_node, target);
}

static int fb_read_file(struct file *file, void *buf, size_t len) {
    (void)file;
    (void)buf;
    (void)len;
    return -1;
}

static int no_read_file(struct file *file, void *buf, size_t len) {
    (void)file;
    (void)buf;
    (void)len;
    return -1;
}

static int no_write_file(struct file *file, const void *buf, size_t len) {
    (void)file;
    (void)buf;
    (void)len;
    return -1;
}

static int fb_write_file(struct file *file, const void *buf, size_t len) {
    if (file == (void *)0) {
        return -1;
    }
    long n = framebuffer_write(file->f_pos, buf, len);
    if (n > 0) {
        file->f_pos += (size_t)n;
    }
    return (int)n;
}

static long fb_lseek64(struct file *file, long offset, int whence) {
    if (file == (void *)0 || whence != VFS_SEEK_SET || offset < 0 ||
        (unsigned long)offset > FRAMEBUFFER_SIZE) {
        return -1;
    }
    file->f_pos = (size_t)offset;
    return offset;
}

static int fb_ioctl(struct file *file, unsigned long request, void *arg) {
    (void)file;
    if (request != FB_IOCTL_GET_INFO) {
        return -1;
    }
    return framebuffer_get_info((struct framebuffer_info *)arg);
}

static int devfs_lookup(struct vnode *dir_node, struct vnode **target,
                        const char *component_name) {
    if (dir_node == (void *)0 || target == (void *)0 ||
        component_name == (void *)0 || dir_node->type != VNODE_DIR) {
        return -1;
    }
    struct devfs_mount *dev = (struct devfs_mount *)dir_node->internal;
    if (dev == (void *)0) {
        return -1;
    }
    for (int i = 0; i < DEVFS_ENTRIES; i++) {
        if (strcmp(dev->nodes[i].name, component_name) == 0) {
            *target = &dev->nodes[i].vnode;
            return 0;
        }
    }
    return -1;
}

static int devfs_create(struct vnode *dir_node, struct vnode **target,
                        const char *component_name) {
    (void)dir_node;
    (void)target;
    (void)component_name;
    return -1;
}

static int devfs_mkdir(struct vnode *dir_node, struct vnode **target,
                       const char *component_name) {
    (void)dir_node;
    (void)target;
    (void)component_name;
    return -1;
}

static struct file_operations devfs_dir_ops = {
    .open = devfs_open_common,
    .close = devfs_close,
    .read = no_read_file,
    .write = no_write_file,
    .lseek64 = no_lseek64,
    .ioctl = no_ioctl,
};

static struct file_operations uart_file_ops = {
    .open = uart_open,
    .close = devfs_close,
    .read = uart_read_file,
    .write = uart_write_file,
    .lseek64 = no_lseek64,
    .ioctl = no_ioctl,
};

static struct file_operations fb_file_ops = {
    .open = fb_open,
    .close = devfs_close,
    .read = fb_read_file,
    .write = fb_write_file,
    .lseek64 = fb_lseek64,
    .ioctl = fb_ioctl,
};

static struct vnode_operations devfs_vnode_ops = {
    .lookup = devfs_lookup,
    .create = devfs_create,
    .mkdir = devfs_mkdir,
};

static void devfs_init_node(struct mount *mnt, struct devfs_node *node,
                            struct devfs_node *root,
                            const char *name, int type,
                            struct file_operations *f_ops) {
    node->name = name;
    node->vnode.mount = mnt;
    node->vnode.mounted = (void *)0;
    node->vnode.parent = type == VNODE_DIR ? (void *)0 : &root->vnode;
    node->vnode.v_ops = &devfs_vnode_ops;
    node->vnode.f_ops = f_ops;
    node->vnode.internal = type == VNODE_DIR ? root->vnode.internal : node;
    node->vnode.type = type;
}

static int devfs_mount(struct filesystem *fs, struct mount *mnt) {
    struct devfs_mount *dev = (struct devfs_mount *)allocate(sizeof(*dev));
    if (dev == (void *)0) {
        return -1;
    }
    memset(dev, 0, sizeof(*dev));
    dev->root.vnode.internal = dev;
    // put different file ops to corresponding node
    devfs_init_node(mnt, &dev->root, &dev->root, "", VNODE_DIR, &devfs_dir_ops);
    devfs_init_node(mnt, &dev->nodes[0], &dev->root, "uart", VNODE_FILE,
                    &uart_file_ops);
    devfs_init_node(mnt, &dev->nodes[1], &dev->root, "fb", VNODE_FILE,
                    &fb_file_ops);
    mnt->fs = fs;
    mnt->root = &dev->root.vnode;
    return 0;
}

static struct filesystem devfs = {
    .name = "devfs",
    .setup_mount = devfs_mount,
};

int devfs_register(void) {
    return register_filesystem(&devfs);
}
