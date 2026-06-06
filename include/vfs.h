#ifndef VFS_H
#define VFS_H

#include <stddef.h>

#define VFS_MAX_PATH 255
#define VFS_MAX_NAME 15
#define VFS_MAX_FD 16
#define VFS_O_CREAT 00000100

#define VFS_SEEK_SET 0

enum vnode_type {
    VNODE_DIR = 1,
    VNODE_FILE = 2,
};

struct vnode;
struct file;
struct mount;
struct filesystem;

struct file_operations {
    int (*open)(struct vnode *file_node, struct file **target);
    int (*close)(struct file *file);
    int (*read)(struct file *file, void *buf, size_t len);
    int (*write)(struct file *file, const void *buf, size_t len);
    long (*lseek64)(struct file *file, long offset, int whence);
};

struct vnode_operations {
    int (*lookup)(struct vnode *dir_node, struct vnode **target,
                  const char *component_name);
    int (*create)(struct vnode *dir_node, struct vnode **target,
                  const char *component_name);
    int (*mkdir)(struct vnode *dir_node, struct vnode **target,
                 const char *component_name);
};

struct vnode {
    struct mount *mount;
    struct mount *mounted;
    struct vnode *parent;
    struct vnode_operations *v_ops;
    struct file_operations *f_ops;
    void *internal;
    int type;
};

struct file {
    struct vnode *vnode;
    size_t f_pos;
    struct file_operations *f_ops;
    int flags;
    int refcnt;
};

struct mount {
    struct vnode *root;
    struct vnode *mountpoint;
    struct filesystem *fs;
};

struct filesystem {
    const char *name;
    int (*setup_mount)(struct filesystem *fs, struct mount *mount);
    struct filesystem *next;
};

struct thread;

int vfs_init(unsigned long initrd_start, unsigned long initrd_end);
struct vnode *vfs_root(void);
int register_filesystem(struct filesystem *fs);
int vfs_open_at(struct vnode *root, struct vnode *cwd, const char *pathname,
                int flags, struct file **target);
int vfs_open(const char *pathname, int flags, struct file **target);
int vfs_close(struct file *file);
int vfs_read(struct file *file, void *buf, size_t len);
int vfs_write(struct file *file, const void *buf, size_t len);
long vfs_lseek64(struct file *file, long offset, int whence);
int vfs_mkdir_at(struct vnode *root, struct vnode *cwd, const char *pathname);
int vfs_mkdir(const char *pathname);
int vfs_mount_at(struct vnode *root, struct vnode *cwd, const char *target,
                 const char *filesystem);
int vfs_mount(const char *target, const char *filesystem);
int vfs_lookup_at(struct vnode *root, struct vnode *cwd, const char *pathname,
                  struct vnode **target);
int vfs_lookup(const char *pathname, struct vnode **target);
int vfs_chdir(struct thread *task, const char *path);
int vfs_thread_init(struct thread *task);
void vfs_thread_cleanup(struct thread *task);
void vfs_file_get(struct file *file);
int vfs_next_path_component(const char **pp, char *name);

#endif /* VFS_H */
