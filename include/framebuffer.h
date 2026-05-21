#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

int framebuffer_init(void);
int framebuffer_display(const unsigned int *bmp_image, unsigned int width,
                        unsigned int height);

#endif /* FRAMEBUFFER_H */
