RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy
CFLAGS_COMMON = -mcmodel=medany -ffreestanding -nostdlib -g -Wall -fPIE -I include
QEMU ?= qemu-system-riscv64
TARGET = kernel

# Source files
SRC_DIR = src
SRCS_C = $(SRC_DIR)/main.c $(SRC_DIR)/uart.c $(SRC_DIR)/bootloader.c $(SRC_DIR)/dtbParser.c $(SRC_DIR)/helper.c $(SRC_DIR)/initrd.c $(SRC_DIR)/sbi.c $(SRC_DIR)/buddy.c $(SRC_DIR)/kmalloc.c $(SRC_DIR)/startup_alloc.c $(SRC_DIR)/trap.c
SRCS_S = $(SRC_DIR)/start.S

build: clean
	$(CC) $(CFLAGS_COMMON) -DPLATFORM_QEMU -c $(SRCS_S) $(SRCS_C)
	$(LD) -T $(SRC_DIR)/link.ld -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).img

build_pi: clean
	$(CC) $(CFLAGS_COMMON) -DPLATFORM_PI -c $(SRCS_S) $(SRCS_C)
	$(LD) -T $(SRC_DIR)/link_pi.ld -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).img
	mkimage -f kernel.its kernel.fit

run: build
	$(QEMU) -M virt -m 8G -kernel $(TARGET).img -display none -serial stdio

run_initrd: build
	$(QEMU) -M virt -m 8G -kernel $(TARGET).img -initrd initramfs.cpio -display none -serial stdio

run_pty: build
	$(QEMU) -M virt -m 8G -kernel $(TARGET).img -display none -serial pty

run_pty_initrd: build
	$(QEMU) -M virt -m 8G -kernel $(TARGET).img -initrd initramfs.cpio -display none -serial pty

deploy: build_pi
	@echo "Deploying kernel.fit to SD card..."
	sudo mkdir -p /mnt
	sudo mount /dev/sdb1 /mnt
	sudo cp kernel.fit /mnt/
	sudo umount /mnt
	@echo "Deploy complete! You can safely remove the SD card."

clean:
	rm -f $(TARGET).img $(TARGET).elf *.o kernel.fit