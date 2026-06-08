#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

struct framebuffer_info {
    unsigned int width;
    unsigned int height;
    unsigned int bpp;
};

int framebuffer_init(void);
int framebuffer_display(const unsigned int *bmp_image, unsigned int width,
                        unsigned int height);
int framebuffer_get_info(struct framebuffer_info *info);
long framebuffer_write(unsigned long offset, const void *buf,
                       unsigned long count);

#endif /* FRAMEBUFFER_H */
