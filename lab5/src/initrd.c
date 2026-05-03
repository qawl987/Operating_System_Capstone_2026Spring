#include "helper.h"
#include "uart.h"

struct cpio_t {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
};

void initrd_list(const void *start, const void *end) {
    struct cpio_t *cpio_header = (struct cpio_t *)start;

    while ((void *)cpio_header < end) {
        if (strncmp(cpio_header->magic, "070701", 6) != 0) {
            printf("magic wrong\n");
            return;
        }
        int name_size = hextoi(cpio_header->namesize, 8);
        int file_size = hextoi(cpio_header->filesize, 8);
        char *filename = (char *)cpio_header + 110;

        // Check for TRAILER (end marker)
        if (strcmp(filename, "TRAILER!!!") == 0) {
            break;
        }

        // Print file info (skip "." directory)
        if (strcmp(filename, ".") != 0) {
            printf("%d %s\n", file_size, filename);
        }

        // Next header = current + align(110 + name_size, 4) + align(file_size,
        // 4)
        size_t header_plus_name = align_up_val(110 + name_size, 4);
        size_t total_offset = header_plus_name + align_up_val(file_size, 4);
        cpio_header = (struct cpio_t *)((char *)cpio_header + total_offset);
    }
}

void initrd_cat(const void *start, const void *end,
                const char *target_filename) {
    struct cpio_t *cpio_header = (struct cpio_t *)start;

    while ((void *)cpio_header < end) {
        if (strncmp(cpio_header->magic, "070701", 6) != 0) {
            printf("magic wrong\n");
            return;
        }
        int name_size = hextoi(cpio_header->namesize, 8);
        int file_size = hextoi(cpio_header->filesize, 8);
        char *filename = (char *)cpio_header + 110;

        // Check for TRAILER
        if (strcmp(filename, "TRAILER!!!") == 0) {
            break;
        }

        if (strcmp(filename, target_filename) == 0) {
            // Found the file, print its content
            size_t header_plus_name = align_up_val(110 + name_size, 4);
            char *content = (char *)cpio_header + header_plus_name;
            for (int i = 0; i < file_size; i++) {
                uart_putc(content[i]);
            }
            return;
        }

        // Skip to next entry
        size_t header_plus_name = align_up_val(110 + name_size, 4);
        size_t total_offset = header_plus_name + align_up_val(file_size, 4);
        cpio_header = (struct cpio_t *)((char *)cpio_header + total_offset);
    }
    printf("cat: %s: No such file\n", target_filename);
}

const void *initrd_find_file(const void *start, const void *end,
                             const char *target_filename, size_t *size) {
    struct cpio_t *cpio_header = (struct cpio_t *)start;

    while ((void *)cpio_header < end) {
        if (strncmp(cpio_header->magic, "070701", 6) != 0) {
            return (void *)0;
        }

        int name_size = hextoi(cpio_header->namesize, 8);
        int file_size = hextoi(cpio_header->filesize, 8);
        char *filename = (char *)cpio_header + 110;

        if (strcmp(filename, "TRAILER!!!") == 0) {
            break;
        }

        if (strcmp(filename, target_filename) == 0) {
            size_t header_plus_name = align_up_val(110 + name_size, 4);
            if (size) {
                *size = (size_t)file_size;
            }
            return (const void *)((char *)cpio_header + header_plus_name);
        }

        size_t header_plus_name = align_up_val(110 + name_size, 4);
        size_t total_offset = header_plus_name + align_up_val(file_size, 4);
        cpio_header = (struct cpio_t *)((char *)cpio_header + total_offset);
    }

    return (void *)0;
}
