/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2020 Google LLC
 */

#ifndef _XNU_H_
#define _XNU_H_

#include <stdint.h>

/*
 * Mach-O executable file format
 */

#ifdef CONFIG_PHYS_64BIT
#define MACH_O_MAGIC            0xfeedfacf
#define LOAD_COMMAND_SEGMENT    0x19
#else
#define MACH_O_MAGIC            0xfeedface
#define LOAD_COMMAND_SEGMENT    0x1
#endif
#define LOAD_COMMAND_UNIXTHREAD 0x5
#define MACH_O_EXEC             0x2

struct mach_o_header {
	u32 magic;
	u32 cpu_type;
	u32 cpu_subtype;
	u32 file_type;
	u32 commands_nb;
	u32 commands_len;
	u32 flags;
#ifdef CONFIG_PHYS_64BIT
	u32 reserved;
#endif
};

struct mach_o_load_command {
	u32 command;
	u32 command_size;
};

struct mach_o_segment_command {
	struct mach_o_load_command load_command;
	char segment_name[16];
	uintptr_t dst;
	uintptr_t dst_len;
	uintptr_t src_offset;
	uintptr_t src_len;
	u32 max_protection;
	u32 initial_protection;
	u32 sections_nb;
	u32 flags;
};

struct thread_command {
	struct mach_o_load_command load_command;
	u32 flavor;
	u32 count;
	struct {
#ifdef CONFIG_ARM64
		u64 x[29];
		u64 fp;
		u64 lr;
		u64 sp;
		u64 pc;
		u32 cpsr;
		u32 flags;
#else
		// Other architectures should list their registers here
		u64 pc;
#endif
	} state;
};

/*
 * XNU boot parameters
 */

#define XNU_CMDLINE_LEN 608

struct xnu_boot_arguments {
	u16 revision;
	u16 version;

	u64 virt_base;
	u64 phys_base;
	u64 mem_size;
	u64 phys_end;

	struct xnu_video_information {
		u64 base_addr;
		u64 display;
		u64 bytes_per_row;
		u64 width;
		u64 height;
		u64 depth;
	} video_information;

	u32 machine_type;
	uintptr_t afdt;
	u32 afdt_length;
	char command_line[XNU_CMDLINE_LEN];
	u64 boot_flags;
	u64 mem_size_actual;
};

/*
 * Apple Flattened Device Tree (AFDT)
 */

struct afdt_node {
	u32 properties_nb;
	u32 children_nb;
};

struct afdt_property {
	char name[32];
	u32 length;
};

#endif /* _XNU_H_ */
