#include "vfs.h"

#include "devfs.h"
#include "helper.h"
#include "kmalloc.h"
#include "ramfs.h"
#include "thread.h"
#include "tmpfs.h"

static struct mount root_mount;
static struct filesystem *filesystems;

static struct filesystem *find_filesystem(const char *name) {
    for (struct filesystem *fs = filesystems; fs != (void *)0; fs = fs->next) {
        if (strcmp(fs->name, name) == 0) {
            return fs;
        }
    }
    return (void *)0;
}

int register_filesystem(struct filesystem *fs) {
    if (fs == (void *)0 || fs->name == (void *)0 ||
        find_filesystem(fs->name) != (void *)0) {
        return -1;
    }
    fs->next = filesystems;
    filesystems = fs;
    return 0;
}

struct vnode *vfs_root(void) {
    return root_mount.root;
}

static struct vnode *follow_mount(struct vnode *node) {
    // If current node have many mount point, enter the deepest
    while (node != (void *)0 && node->mounted != (void *)0) {
        node = node->mounted->root;
    }
    return node;
}

static struct vnode *parent_of(struct vnode *root, struct vnode *node) {
    // node == /
    if (node == (void *)0 || node == root || node == root_mount.root) {
        return node;
    }
    // node == mount root
    if (node->mount != (void *)0 && node == node->mount->root &&
        node->mount->mountpoint != (void *)0) {
        // node == / -> return itself, node == /dev -> return /
        return node->mount->mountpoint->parent == (void *)0
                   ? node->mount->mountpoint
                   : node->mount->mountpoint->parent;
    }
    if (node->parent == (void *)0) {
        return node;
    }
    // node = /a/b -> return a
    return node->parent;
}

// p = "/a/b" -> p="/b", name = a
int vfs_next_path_component(const char **pp, char *name) {
    const char *p = *pp;
    int len = 0;

    // Skip leading '/' characters.
    while (*p == '/') {
        p++;
    }

    // No more path components.
    if (*p == '\0') {
        *pp = p;
        return 0;
    }

    // Copy the next path component until '/' or end of string.
    while (*p != '\0' && *p != '/') {
        if (len >= VFS_MAX_NAME) {
            return -1;
        }
        name[len++] = *p++;
    }
    name[len] = '\0';
    *pp = p;
    return 1;
}

int vfs_lookup_at(struct vnode *root, struct vnode *cwd, const char *pathname,
                  struct vnode **target) {
    if (pathname == (void *)0 || target == (void *)0 ||
        strlen(pathname) > VFS_MAX_PATH || root_mount.root == (void *)0) {
        return -1;
    }
    root = root == (void *)0 ? root_mount.root : root;
    cwd = cwd == (void *)0 ? root : cwd;
    struct vnode *cur = pathname[0] == '/' ? root : cwd;
    cur = follow_mount(cur);
    const char *p = pathname;
    char name[VFS_MAX_NAME + 1];

    while (1) {
        int ret = vfs_next_path_component(&p, name);
        if (ret < 0) {
            return -1;
        }
        // end of path
        if (ret == 0) {
            *target = follow_mount(cur);
            return 0;
        }
        if (strcmp(name, ".") == 0 || name[0] == '\0') {
            continue;
        }
        if (strcmp(name, "..") == 0) {
            cur = parent_of(root, cur);
            continue;
        }
        // if cur is another mount point, dive into, ex: /dev
        // cur = tmpfs /dev, -> devfs /
        cur = follow_mount(cur);
        // only dir can keep lookup, /a.txt/b invalid
        if (cur->type != VNODE_DIR || cur->v_ops == (void *)0 ||
            cur->v_ops->lookup(cur, &cur, name) < 0) {
            return -1;
        }
    }
}

int vfs_lookup(const char *pathname, struct vnode **target) {
    return vfs_lookup_at(root_mount.root, root_mount.root, pathname, target);
}

static int lookup_parent(struct vnode *root, struct vnode *cwd,
                         const char *pathname, struct vnode **parent,
                         char *last) {
    if (pathname == (void *)0 || parent == (void *)0 || last == (void *)0 ||
        pathname[0] == '\0' || strlen(pathname) > VFS_MAX_PATH) {
        return -1;
    }
    root = root == (void *)0 ? root_mount.root : root;
    cwd = cwd == (void *)0 ? root : cwd;
    struct vnode *cur = pathname[0] == '/' ? root : cwd;
    const char *p = pathname;
    char name[VFS_MAX_NAME + 1];
    int saw = 0;

    while (1) {
        int ret = vfs_next_path_component(&p, name);
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            // ex: /
            if (!saw) {
                return -1;
            }
            // return dir ex: /a/b/
            *parent = follow_mount(cur);
            return 0;
        }
        saw = 1;
        const char *save = p;
        char probe[VFS_MAX_NAME + 1];
        // peek next token
        int has_next = vfs_next_path_component(&save, probe);
        if (has_next < 0) {
            return -1;
        }
        // reach the end
        if (has_next == 0) {
            // ex: /a/., a is not the parent
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                return -1;
            }
            strncpy(last, name, VFS_MAX_NAME + 1);
            *parent = follow_mount(cur);
            return 0;
        }
        if (strcmp(name, ".") == 0) {
            continue;
        }
        if (strcmp(name, "..") == 0) {
            cur = parent_of(root, cur);
            continue;
        }
        // enter mount if have
        cur = follow_mount(cur);
        if (cur->type != VNODE_DIR || cur->v_ops == (void *)0 ||
            cur->v_ops->lookup(cur, &cur, name) < 0) {
            return -1;
        }
    }
}

static int has_trailing_slash(const char *pathname) {
    size_t len = strlen(pathname);
    size_t end = len;
    while (end > 0 && pathname[end - 1] == '/') {
        end--;
    }
    return end > 0 && end < len;
}

int vfs_open_at(struct vnode *root, struct vnode *cwd, const char *pathname,
                int flags, struct file **target) {
    if (target == (void *)0) {
        return -1;
    }
    struct vnode *node = (void *)0;
    // check if file exist
    if (vfs_lookup_at(root, cwd, pathname, &node) < 0) {
        if ((flags & VFS_O_CREAT) == 0) {
            return -1;
        }
        // block non-exist O_CREAT /a/b/
        if (has_trailing_slash(pathname)) {
            return -1;
        }
        char name[VFS_MAX_NAME + 1];
        struct vnode *parent = (void *)0;
        // /a/b.txt -> get /a, b.txt
        if (lookup_parent(root, cwd, pathname, &parent, name) < 0 ||
            parent->v_ops == (void *)0 ||
            parent->v_ops->create(parent, &node, name) < 0) {
            return -1;
        }
    }
    node = follow_mount(node);
    if (node->f_ops == (void *)0 || node->f_ops->open == (void *)0) {
        return -1;
    }
    int ret = node->f_ops->open(node, target);
    if (ret == 0) {
        // save flag but not used
        (*target)->flags = flags;
    }
    return ret;
}

int vfs_open(const char *pathname, int flags, struct file **target) {
    return vfs_open_at(root_mount.root, root_mount.root, pathname, flags,
                       target);
}

void vfs_file_get(struct file *file) {
    if (file != (void *)0) {
        file->refcnt++;
    }
}

int vfs_close(struct file *file) {
    if (file == (void *)0) {
        return -1;
    }
    file->refcnt--;
    if (file->refcnt > 0) {
        return 0;
    }
    // If no fork process shard file, clean it
    if (file->f_ops != (void *)0 && file->f_ops->close != (void *)0) {
        return file->f_ops->close(file);
    }
    // fallback
    free(file);
    return 0;
}

int vfs_read(struct file *file, void *buf, size_t len) {
    if (file == (void *)0 || file->f_ops == (void *)0 ||
        file->f_ops->read == (void *)0) {
        return -1;
    }
    return file->f_ops->read(file, buf, len);
}

int vfs_write(struct file *file, const void *buf, size_t len) {
    if (file == (void *)0 || file->f_ops == (void *)0 ||
        file->f_ops->write == (void *)0) {
        return -1;
    }
    return file->f_ops->write(file, buf, len);
}

long vfs_lseek64(struct file *file, long offset, int whence) {
    if (file == (void *)0 || file->f_ops == (void *)0 ||
        file->f_ops->lseek64 == (void *)0) {
        return -1;
    }
    return file->f_ops->lseek64(file, offset, whence);
}

int vfs_ioctl(struct file *file, unsigned long request, void *arg) {
    if (file == (void *)0 || file->f_ops == (void *)0 ||
        file->f_ops->ioctl == (void *)0) {
        return -1;
    }
    return file->f_ops->ioctl(file, request, arg);
}

int vfs_mkdir_at(struct vnode *root, struct vnode *cwd, const char *pathname) {
    char name[VFS_MAX_NAME + 1];
    struct vnode *parent = (void *)0;
    struct vnode *node = (void *)0;
    // find parent, let parent create child dir
    if (lookup_parent(root, cwd, pathname, &parent, name) < 0 ||
        parent->v_ops == (void *)0 || parent->v_ops->mkdir == (void *)0) {
        return -1;
    }
    return parent->v_ops->mkdir(parent, &node, name);
}

int vfs_mkdir(const char *pathname) {
    return vfs_mkdir_at(root_mount.root, root_mount.root, pathname);
}

int vfs_mount_at(struct vnode *root, struct vnode *cwd, const char *target,
                 const char *filesystem) {
    struct vnode *mp = (void *)0;
    // get file system
    struct filesystem *fs = find_filesystem(filesystem);
    // get mount point vnode requested outside 
    if (fs == (void *)0 || fs->setup_mount == (void *)0 ||
        vfs_lookup_at(root, cwd, target, &mp) < 0 || mp->type != VNODE_DIR ||
        mp->mounted != (void *)0) {
        return -1;
    }
    struct mount *mnt = (struct mount *)allocate(sizeof(struct mount));
    if (mnt == (void *)0) {
        return -1;
    }
    memset(mnt, 0, sizeof(*mnt));
    mnt->mountpoint = mp;
    if (fs->setup_mount(fs, mnt) < 0) {
        free(mnt);
        return -1;
    }
    mnt->root->parent = mp;
    mp->mounted = mnt;
    return 0;
}

int vfs_mount(const char *target, const char *filesystem) {
    return vfs_mount_at(root_mount.root, root_mount.root, target, filesystem);
}

int vfs_chdir(struct thread *task, const char *path) {
    struct vnode *node = (void *)0;
    // if path is directory, change to it
    if (task == (void *)0 ||
        vfs_lookup_at(task->fs_root, task->cwd, path, &node) < 0 ||
        node->type != VNODE_DIR) {
        return -1;
    }
    task->cwd = node;
    return 0;
}

int vfs_thread_init(struct thread *task) {
    if (task == (void *)0 || root_mount.root == (void *)0) {
        return -1;
    }
    task->fs_root = root_mount.root;
    task->cwd = root_mount.root;
    for (int i = 0; i < VFS_MAX_FD; i++) {
        task->files[i] = (void *)0;
    }
    // setup stdin/stdout/stderr to /dev/uart
    for (int i = 0; i < 3; i++) {
        if (vfs_open_at(task->fs_root, task->cwd, "/dev/uart", 0,
                        &task->files[i]) < 0) {
            task->files[i] = (void *)0;
        }
    }
    return 0;
}

void vfs_thread_cleanup(struct thread *task) {
    if (task == (void *)0) {
        return;
    }
    for (int i = 0; i < VFS_MAX_FD; i++) {
        if (task->files[i] != (void *)0) {
            vfs_close(task->files[i]);
            task->files[i] = (void *)0;
        }
    }
}

int vfs_init(unsigned long initrd_start, unsigned long initrd_end) {
    memset(&root_mount, 0, sizeof(root_mount));
    filesystems = (void *)0;
    if (tmpfs_register() < 0 || ramfs_register(initrd_start, initrd_end) < 0 ||
        devfs_register() < 0) {
        return -1;
    }

    struct filesystem *fs = find_filesystem("tmpfs");
    // root mount, root_mount.root = tmpfs vnode
    if (fs == (void *)0 || fs->setup_mount(fs, &root_mount) < 0) {
        return -1;
    }
    if (vfs_mkdir("/ramfs") < 0) {
        return -1;
    }
    if (vfs_mount("/ramfs", "ramfs") < 0) {
        return -1;
    }
    if (vfs_mkdir("/dev") < 0) {
        return -1;
    }
    return vfs_mount("/dev", "devfs");
}
