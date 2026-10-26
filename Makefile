# Aurora OS - build a bootable ISO (BIOS + UEFI) with Limine
#
# Requirements: gcc, make, python3; xorriso is optional. qemu-system-x86_64 to run
#
#   make                 normal build           -> aurora.iso
#   make FAULT=pf        deliberately crash at the end of boot to show the
#                        panic screen (FAULT = div | pf | ud | df)
#   make run             boot in QEMU, serial log on your terminal
#   make clean

CC      ?= gcc
LD      ?= ld

VARIANT := $(if $(FAULT),-$(FAULT)-test)
BUILD   := build$(VARIANT)
ISO     := aurora$(VARIANT).iso
KERNEL  := $(BUILD)/kernel.elf

FAULTDEF := $(if $(FAULT),-DAURORA_FAULT_$(shell echo $(FAULT) | tr a-z A-Z))

CFLAGS  := -std=gnu11 -O2 -g -Wall -Wextra -pipe -MMD -MP \
           -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto \
           -fno-PIC -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
           -mno-red-zone -mcmodel=kernel -fno-omit-frame-pointer $(FAULTDEF)
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T kernel/linker.ld

SRC := $(wildcard kernel/*.c) $(wildcard kernel/*.S)
OBJ := $(patsubst kernel/%.c,$(BUILD)/%.o,$(filter %.c,$(SRC))) \
       $(patsubst kernel/%.S,$(BUILD)/%.o,$(filter %.S,$(SRC)))

.PHONY: all run clean
all: $(ISO)

$(BUILD)/%.o: kernel/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: kernel/%.S
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJ) kernel/linker.ld
	$(LD) $(LDFLAGS) $(OBJ) -o $@

$(ISO): $(KERNEL) kernel/limine.conf tools/mkiso.py assets/fonts/SegoeUI.ttf assets/fonts/SegoeUI-Font-Notice.txt assets/certificates/ca-certificates.crt kernel/wallpapers.h limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin limine/BOOTX64.EFI
	@if command -v xorriso >/dev/null 2>&1; then \
	  rm -rf iso_root$(VARIANT) && mkdir -p iso_root$(VARIANT)/boot/limine iso_root$(VARIANT)/EFI/BOOT iso_root$(VARIANT)/boot/assets; \
	  cp $(KERNEL) iso_root$(VARIANT)/boot/kernel.elf; \
	  cp kernel/limine.conf iso_root$(VARIANT)/boot/limine/limine.conf; \
	  cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root$(VARIANT)/boot/limine/; \
	  cp limine/BOOTX64.EFI iso_root$(VARIANT)/EFI/BOOT/; \
	  cp assets/fonts/*.ttf assets/fonts/LICENSE-Go-Fonts.txt iso_root$(VARIANT)/boot/assets/; \
	  cp assets/certificates/ca-certificates.crt iso_root$(VARIANT)/boot/assets/; \
	  xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin --efi-boot-part --efi-boot-image --protective-msdos-label iso_root$(VARIANT) -o $(ISO); \
	  ./limine/limine bios-install $(ISO); \
	else \
	  python3 tools/mkiso.py --output $(ISO) --kernel $(KERNEL); \
	fi

run: $(ISO)
	qemu-system-x86_64 -machine pc -vga std -m 1G -cdrom $(ISO) -boot order=d -serial stdio

clean:
	rm -rf build* iso_root* aurora*.iso

-include $(OBJ:.o=.d)

