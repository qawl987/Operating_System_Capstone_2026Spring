RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy
CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall
QEMU ?= qemu-system-riscv64
TARGET = kernel

build: clean
	$(CC) $(CFLAGS) -c *.S *.c
	$(LD) -T link.ld -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET)

build_pi: clean
	$(CC) $(CFLAGS) -c *.S *.c
	$(LD) -T link_pi.ld -o $(TARGET).elf start.o main.o uart.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin
	mkimage -f kernel.its kernel.fit

run: build
	$(QEMU) -M virt -m 8G -kernel $(TARGET) -display none -serial stdio

run_initrd: build
	$(QEMU) -M virt -m 8G -kernel $(TARGET) -initrd initramfs.cpio -display none -serial stdio

run_pty: build
	$(QEMU) -M virt -m 8G -kernel $(TARGET) -display none -serial pty

run_pty_initrd: build
	$(QEMU) -M virt -m 8G -kernel $(TARGET) -initrd initramfs.cpio -display none -serial pty

deploy: build_pi
	@echo "開始部署 kernel.fit 到 SD 卡..."
	sudo mkdir -p /mnt
	sudo mount /dev/sdb1 /mnt
	sudo cp kernel.fit /mnt/
	sudo umount /mnt
	@echo "部署完成！你可以安全拔除 SD 卡了。"

clean:
	rm -f $(TARGET) $(TARGET).bin $(TARGET).elf *.o kernel.fit