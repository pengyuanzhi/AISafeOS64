# Makefile for AISafe64 RTOS
# Author: AISafe64 Team
# Date: 2025-01-08

# 工具链
CROSS_COMPILE = aarch64-none-elf-
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

# 项目名称
PROJECT = aisafe64

# 源文件和目录
SRC_DIR = src
BUILD_DIR = build
INCLUDE_DIR = $(SRC_DIR)/include

# 汇编源文件
ASM_SOURCES = $(SRC_DIR)/arch/arm64/boot/start.S \
             $(SRC_DIR)/arch/arm64/boot/context_switch.S

# C源文件
C_SOURCES = $(SRC_DIR)/kernel/main.c \
           $(SRC_DIR)/kernel/syscall.c \
           $(SRC_DIR)/kernel/time.c \
           $(SRC_DIR)/kernel/timer.c \
           $(SRC_DIR)/kernel/irq/irq.c \
           $(SRC_DIR)/kernel/irq/gic.c \
           $(SRC_DIR)/kernel/mm/page.c \
           $(SRC_DIR)/kernel/mm/kheap.c \
           $(SRC_DIR)/lib/printk.c \
           $(SRC_DIR)/lib/bitmap.c \
           $(SRC_DIR)/lib/rbtree.c \
           $(SRC_DIR)/kernel/sync/spinlock.c \
           $(SRC_DIR)/kernel/sync/mutex.c \
           $(SRC_DIR)/kernel/sync/semaphore.c \
           $(SRC_DIR)/drivers/uart/uart.c \
           $(SRC_DIR)/kernel/sched.c \
           $(SRC_DIR)/kernel/sched_fifo.c \
           $(SRC_DIR)/kernel/sched_rr.c \
           $(SRC_DIR)/kernel/sched_cfs.c \
           $(SRC_DIR)/kernel/sched_edf.c \
           $(SRC_DIR)/kernel/sched_idle.c \
           $(SRC_DIR)/kernel/sched_extra.c \
           $(SRC_DIR)/kernel/task.c \
           $(SRC_DIR)/kernel/loader.c \
           $(SRC_DIR)/kernel/shell.c

# 目标文件
ASM_OBJECTS = $(ASM_SOURCES:$(SRC_DIR)/%.s=$(BUILD_DIR)/%.o)
C_OBJECTS = $(C_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# 链接脚本
LINKER_SCRIPT = $(SRC_DIR)/arch/arm64/boot/linker.ld

# 编译选项
CFLAGS = -Wall -Wextra -Werror -nostdlib -nostartfiles -ffreestanding
CFLAGS += -march=armv8-a -mtune=cortex-a53
CFLAGS += -I$(INCLUDE_DIR)
CFLAGS += -I$(SRC_DIR)/arch/arm64/include
CFLAGS += -O2 -g

# 汇编选项
ASFLAGS = -march=armv8-a -g

# 链接选项
LDFLAGS = -nostdlib -nostartfiles -T $(LINKER_SCRIPT)
LDFLAGS += -Map=$(BUILD_DIR)/$(PROJECT).map

# 默认目标
all: $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin $(BUILD_DIR)/$(PROJECT).dis

# 创建构建目录
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/arch/arm64/boot
	mkdir -p $(BUILD_DIR)/arch/arm64/include
	mkdir -p $(BUILD_DIR)/kernel
	mkdir -p $(BUILD_DIR)/kernel/mm
	mkdir -p $(BUILD_DIR)/kernel/irq
	mkdir -p $(BUILD_DIR)/kernel/sync
	mkdir -p $(BUILD_DIR)/lib
	mkdir -p $(BUILD_DIR)/drivers/uart
	mkdir -p $(BUILD_DIR)/kernel/loader

# 编译汇编文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)
	@echo "AS $<"
	@$(AS) $(ASFLAGS) $< -o $@

# 编译C文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# 链接
$(BUILD_DIR)/$(PROJECT).elf: $(OBJECTS) | $(BUILD_DIR)
	@echo "LD $@"
	@$(LD) $(LDFLAGS) $(OBJECTS) -o $@

# 生成二进制文件
$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf | $(BUILD_DIR)
	@echo "OBJCOPY $@"
	@$(OBJCOPY) -O binary $< $@

# 生成反汇编文件
$(BUILD_DIR)/$(PROJECT).dis: $(BUILD_DIR)/$(PROJECT).elf | $(BUILD_DIR)
	@echo "OBJDUMP $@"
	@$(OBJDUMP) -d -S $< > $@

# 清理
clean:
	@echo "CLEAN"
	@rm -rf $(BUILD_DIR)

# 运行（QEMU）
qemu: $(BUILD_DIR)/$(PROJECT).bin
	@echo "QEMU: Starting AISafe64 RTOS..."
	@qemu-system-aarch64 -M virt -cpu cortex-a53 -m 1G \
		-nographic -serial mon:stdio \
		-kernel $(BUILD_DIR)/$(PROJECT).bin

# 调试（QEMU + GDB）
debug: $(BUILD_DIR)/$(PROJECT).bin
	@echo "QEMU: Starting AISafe64 RTOS with GDB server on port 1234..."
	@qemu-system-aarch64 -M virt -cpu cortex-a53 -m 1G \
		-nographic -serial mon:stdio \
		-kernel $(BUILD_DIR)/$(PROJECT).bin \
		-S -s

# 依赖关系
-include $(OBJECTS:.o=.d)

.PHONY: all clean qemu debug
