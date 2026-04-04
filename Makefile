# Makefile for CSC 452 Project 4 -- (Development utilities)

-include .env
SHELL:=/bin/bash

.PHONY: csc452 build install update copy_to_working copy_to_kernel backup fetch login dev_list


WRK := dev
COPY_FILES := \
	sys.c:kernel/sys.c \
	syscalls.h:include/linux/syscalls.h \
	unistd.h:include/uapi/asm-generic/unistd.h \
	syscall_64.tbl:arch/x86/entry/syscalls/syscall_64.tbl \
	syscall_32.tbl:arch/x86/entry/syscalls/syscall_32.tbl

GREEN=\033[0;32m
YELLOW=\033[0;33m
RESET=\033[0m

# Kernel ----------------------------------------------------------------------

csc452: copy_to_kernel build install update

build:
	@echo -e "$(GREEN)BUILD KERNEL CHANGES$(RESET)"
	make -C linux-6.1.106 -j2
	make -C linux-6.1.106 modules_install

install:
	@echo -e "$(GREEN)INSTALL KERNEL TO BOOT FOLDER$(RESET)"
	cp -v linux-6.1.106/arch/x86/boot/bzImage /boot/vmlinuz-6.1.106-csc452
	cp    linux-6.1.106/.config /boot/config-6.1.106-csc452
	cp    linux-6.1.106/System.map /boot/System.map-6.1.106-csc452
	update-initramfs -c -k 6.1.106-csc452

update:
	@echo -e "$(GREEN)UPDATE BOOTLOADER$(RESET)"
	grub-mkconfig -o /boot/grub/grub.cfg
	@echo -e "$(GREEN)TEST OS USING "reboot" AND SELECT OS ENDING "-csc452"$(RESET)"

copy_to_working:
	@echo -e "$(GREEN)COPY KERNEL FILES TO WORKING DIR$(RESET)"
	mkdir -p $(WRK)
	@$(foreach f,$(COPY_FILES), \
		src="linux-6.1.106/$(word 2,$(subst :, ,$(f)))"; \
		dst="$(WRK)/$(word 1,$(subst :, ,$(f)))"; \
		if cmp -s $$src $$dst; then \
			echo -e "$(GREEN)unchanged$(RESET): $$dst"; \
		else \
			cp $$src $$dst && echo -e "$(YELLOW)copied$(RESET):   $$dst"; \
		fi; )

copy_to_kernel:
	@echo -e "$(GREEN)COPY WORKING FILES TO KERNEL$(RESET)"
	@$(foreach f,$(COPY_FILES), \
		src="$(WRK)/$(word 1,$(subst :, ,$(f)))"; \
		dst="linux-6.1.106/$(word 2,$(subst :, ,$(f)))"; \
		if cmp -s $$src $$dst; then \
			echo -e "$(GREEN)unchanged$(RESET): $$dst"; \
		else \
			cp $$src $$dst && echo -e "$(YELLOW)copied$(RESET):   $$dst"; \
		fi; )

# SSH -------------------------------------------------------------------------

backup:
	@echo -e "$(YELLOW)(ignore err, intentional skip folders during wildcard)$(RESET)"
	-scp * ${SSH}:${SSH_PATH} 2>/dev/null
	@echo -e "$(YELLOW)(now uploading specifically kernel dev files)$(RESET)"
	scp -r dev ${SSH}:${SSH_PATH}

fetch:
	scp -r ${SSH}:${SSH_PATH}/* .

login:
	ssh ${SSH}

# Dev -------------------------------------------------------------------------

dev_list:
	@for item in $(COPY_FILES); do echo $$item; done
