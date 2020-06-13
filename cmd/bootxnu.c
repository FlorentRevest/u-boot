// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2020 Google LLC
 */

#include <common.h>
#include <cpu_func.h>
#include <xnu.h>

#define XNU_LOAD_OFFSET  0x4000
#define XNU_LOAD_ADDR CONFIG_SYS_LOAD_ADDR + XNU_LOAD_OFFSET

/*
 * Returns the length of an Apple Flattened Device Tree pointed to by afdt
 */
u32 afdt_length(void *afdt)
{
	struct afdt_node *node = afdt;
	u32 offset = sizeof(*node);
	unsigned int i;

	for (i = 0; i < node->properties_nb; ++i) {
		struct afdt_property *property = (afdt + offset);

		offset += sizeof(*property) + roundup(property->length, 4);
	}

	for (i = 0; i < node->children_nb; ++i)
		offset += afdt_length(afdt + offset);

	return offset;
}

/*
 * A very simple Mach-O loader. Returns a mach_o_load_info structure with
 * virtual addresses of the first loaded instruction, the entry point and the
 * last loaded instruction. On loading error, the end address is below the base
 */
struct mach_o_load_info {
	uintptr_t base;
	uintptr_t entry;
	uintptr_t end;
};

static struct mach_o_load_info load_mach_o_image(void *mach_o_image)
{
	struct mach_o_header *header;
	struct mach_o_segment_command *sc;
	struct mach_o_load_command *lc;
	struct mach_o_load_info ret;
	void *dst, *src;
	int i;

	ret.end = 0;
	ret.base = ~0;

	/* Sanity-check the Mach-O header */
	header = mach_o_image;
	if (header->magic != MACH_O_MAGIC || header->file_type != MACH_O_EXEC)
		return ret;

	/* Find the virtual memory range used by the kernel */
	lc = (struct mach_o_load_command *)(header + 1);
	for (i = 0; i < header->commands_nb; ++i) {
		if (lc->command == LOAD_COMMAND_SEGMENT) {
			sc = (struct mach_o_segment_command *)lc;

			ret.end  = max_t(uintptr_t,
					 sc->dst + sc->dst_len, ret.end);
			ret.base = min_t(uintptr_t, sc->dst, ret.base);
		}
		lc = ((void *)lc) + lc->command_size;
	}

	/* Load all segments into memory */
	lc = (struct mach_o_load_command *)(header + 1);
	for (i = 0; i < header->commands_nb; ++i) {
		if (lc->command == LOAD_COMMAND_SEGMENT) {
			sc = (struct mach_o_segment_command *)lc;

			dst = (void *)(sc->dst - ret.base + XNU_LOAD_ADDR);
			src = (void *)mach_o_image + sc->src_offset;

			if (sc->src_len && sc->dst_len)
				memcpy(dst, src, sc->src_len);
			if (sc->src_len != sc->dst_len && sc->dst_len)
				memset(dst + sc->src_len, 0,
				       sc->dst_len - sc->src_len);
		} else if (lc->command == LOAD_COMMAND_UNIXTHREAD) {
			ret.entry = ((struct thread_command *)lc)->state.pc;
		}
		lc = ((void *)lc) + lc->command_size;
	}

	return ret;
}

/*
 * Interpreter command to boot XNU from a memory image.
 */
int do_bootxnu(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	void *kernel_image_addr, *fdt_image_addr;
	struct xnu_boot_arguments *boot_args;
	struct mach_o_load_info load_info;
	void *xnu_entry, *xnu_end;
	char *command_line;

	// Extract the bootxnu command arguments
	if (argc < 3) {
		printf("Usage: bootxnu kernel_addr fdt_addr");
		return 1;
	}
	kernel_image_addr = (void *)simple_strtoul(argv[1], NULL, 16);
	fdt_image_addr = (void *)simple_strtoul(argv[2], NULL, 16);

	// Load XNU into memory
	load_info = load_mach_o_image(kernel_image_addr);
	if (load_info.base > load_info.end) {
		printf("No Mach-O image at address %p\n", kernel_image_addr);
		return 2;
	}
	xnu_entry = (void *)load_info.entry - load_info.base + XNU_LOAD_ADDR;
	xnu_end   = (void *)load_info.end   - load_info.base + XNU_LOAD_ADDR;

	// Append the XNU boot arguments structure
	boot_args = xnu_end;
	memset(boot_args, 0, sizeof(*boot_args));
	boot_args->revision = 2;
	boot_args->version = 2;
	boot_args->virt_base = load_info.base;
	boot_args->phys_base = XNU_LOAD_ADDR;
	boot_args->mem_size = CONFIG_SYS_SDRAM_SIZE;
	boot_args->phys_end = (uintptr_t)(boot_args + 1);
	command_line = env_get("bootargs");
	if (command_line)
		strlcpy(boot_args->command_line, command_line, XNU_CMDLINE_LEN);

	// Append the Apple flattened device tree
	boot_args->afdt = boot_args->phys_end;
	boot_args->afdt_length = afdt_length(fdt_image_addr);
	memcpy((void *)boot_args->afdt, fdt_image_addr, boot_args->afdt_length);
	boot_args->phys_end += boot_args->afdt_length;
	boot_args->phys_end = roundup(boot_args->phys_end, 0x10000);

	// Jump into the XNU entry point with data cache disabled
	printf("## Starting XNU at %p ...\n", xnu_entry);
	dcache_disable();
#ifdef CONFIG_ARM64
	armv8_switch_to_el1((u64)boot_args, 0, 0, 0,
			    (u64)xnu_entry, ES_TO_AARCH64);
#else
	((void (*)(struct xnu_boot_arguments *))xnu_entry)(boot_args);
#endif

	// Shouldn't ever happen
	printf("## XNU terminated\n");
	return 3;
}

U_BOOT_CMD(
	bootxnu, 3, 0, do_bootxnu,
	"Boot XNU from a Mach-O image\n",
	" [kernel_address] - load address of XNU Mach-O image.\n"
	" [fdt_address] - load address of Apple flattened device tree image.\n"
);
