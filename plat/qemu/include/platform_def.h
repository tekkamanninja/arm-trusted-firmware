/*
 * Copyright (c) 2015-2016, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __PLATFORM_DEF_H__
#define __PLATFORM_DEF_H__

#include <arch.h>
#include <common_def.h>
#include <tbbr_img_def.h>

/* Special value used to verify platform parameters from BL2 to BL3-1 */
#define QEMU_BL31_PLAT_PARAM_VAL	0x0f1e2d3c4b5a6978ULL

#define PLATFORM_STACK_SIZE 0x1000

#define PLATFORM_MAX_CPUS_PER_CLUSTER	4
#define PLATFORM_CLUSTER_COUNT		2
#define PLATFORM_CLUSTER0_CORE_COUNT	PLATFORM_MAX_CPUS_PER_CLUSTER
#define PLATFORM_CLUSTER1_CORE_COUNT	PLATFORM_MAX_CPUS_PER_CLUSTER
#define PLATFORM_CORE_COUNT		(PLATFORM_CLUSTER0_CORE_COUNT + \
					 PLATFORM_CLUSTER1_CORE_COUNT)

#define QEMU_PRIMARY_CPU		0

#define PLAT_NUM_PWR_DOMAINS		(PLATFORM_CLUSTER_COUNT + \
					PLATFORM_CORE_COUNT)
#define PLAT_MAX_PWR_LVL		MPIDR_AFFLVL1

#define PLAT_MAX_RET_STATE		1
#define PLAT_MAX_OFF_STATE		2

/* Local power state for power domains in Run state. */
#define PLAT_LOCAL_STATE_RUN		0
/* Local power state for retention. Valid only for CPU power domains */
#define PLAT_LOCAL_STATE_RET		1
/*
 * Local power state for OFF/power-down. Valid for CPU and cluster power
 * domains.
 */
#define PLAT_LOCAL_STATE_OFF		2

/*
 * Macros used to parse state information from State-ID if it is using the
 * recommended encoding for State-ID.
 */
#define PLAT_LOCAL_PSTATE_WIDTH		4
#define PLAT_LOCAL_PSTATE_MASK		((1 << PLAT_LOCAL_PSTATE_WIDTH) - 1)

/*
 * Some data must be aligned on the biggest cache line size in the platform.
 * This is known only to the platform as it might have a combination of
 * integrated and external caches.
 */
#define CACHE_WRITEBACK_SHIFT		6
#define CACHE_WRITEBACK_GRANULE		(1 << CACHE_WRITEBACK_SHIFT)

/*
 * Partition memory into secure ROM, non-secure DRAM, secure "SRAM",
 * and secure DRAM.
 */
#define SEC_ROM_BASE			0x00000000
#define SEC_ROM_SIZE			0x00020000

#define NS_DRAM0_BASE			0x40000000
#define NS_DRAM0_SIZE			0x3de00000

#define SEC_SRAM_BASE			0x0e000000
#define SEC_SRAM_SIZE			0x00040000

#define SEC_DRAM_BASE			0x0e100000
#define SEC_DRAM_SIZE			0x00f00000

/*
 * Additional secure DRAM for BL31, if SPM is enable
 * This require a modification on qemu source.
 */
#if SPM
# define SEC_DRAM2_BASE			0x0f000000
# define SEC_DRAM2_SIZE			0x01000000
#endif

/*
 * ARM-TF lives in SRAM, partition it here
 */

#define SHARED_RAM_BASE			SEC_SRAM_BASE
#define SHARED_RAM_SIZE			0x00001000

#define PLAT_QEMU_TRUSTED_MAILBOX_BASE	SHARED_RAM_BASE
#define PLAT_QEMU_TRUSTED_MAILBOX_SIZE	(8 + PLAT_QEMU_HOLD_SIZE)
#define PLAT_QEMU_HOLD_BASE		(PLAT_QEMU_TRUSTED_MAILBOX_BASE + 8)
#define PLAT_QEMU_HOLD_SIZE		(PLATFORM_CORE_COUNT * \
					 PLAT_QEMU_HOLD_ENTRY_SIZE)
#define PLAT_QEMU_HOLD_ENTRY_SIZE	8
#define PLAT_QEMU_HOLD_STATE_WAIT	0
#define PLAT_QEMU_HOLD_STATE_GO		1

#define BL_RAM_BASE			(SHARED_RAM_BASE + SHARED_RAM_SIZE)
#define BL_RAM_SIZE			(SEC_SRAM_SIZE - SHARED_RAM_SIZE)

/*
 * BL1 specific defines.
 *
 * BL1 RW data is relocated from ROM to RAM at runtime so we need 2 sets of
 * addresses.
 * Put BL1 RW at the top of the Secure SRAM. BL1_RW_BASE is calculated using
 * the current BL1 RW debug size plus a little space for growth.
 */
#define BL1_RO_BASE			SEC_ROM_BASE
#define BL1_RO_LIMIT			(SEC_ROM_BASE + SEC_ROM_SIZE)
#define BL1_RW_BASE			(BL1_RW_LIMIT - 0x12000)
#define BL1_RW_LIMIT			(BL_RAM_BASE + BL_RAM_SIZE)

/*
 * BL2 specific defines.
 *
 * Put BL2 just below BL3-1. BL2_BASE is calculated using the current BL2 debug
 * size plus a little space for growth.
 */
#if SPM
# define BL2_BASE			(BL_RAM_BASE + BL_RAM_SIZE - 0x3D000)
# define BL2_LIMIT			(BL_RAM_BASE + BL_RAM_SIZE - 0x20000)
#else
# define BL2_BASE			(BL31_BASE - 0x1D000)
# define BL2_LIMIT			BL31_BASE
#endif

/*
 * BL3-1 specific defines.
 *
 * Put BL3-1 at the top of the Trusted SRAM. BL31_BASE is calculated using the
 * current BL3-1 debug size plus a little space for growth.
 */
#if SPM
# define BL31_BASE			(BL31_LIMIT - 0x200000)
# define BL31_LIMIT			(SEC_DRAM2_BASE + SEC_DRAM2_SIZE)
# define BL31_PROGBITS_LIMIT		BL31_LIMIT
#else
# define BL31_BASE			(BL31_LIMIT - 0x20000)
# define BL31_LIMIT			(BL_RAM_BASE + BL_RAM_SIZE)
# define BL31_PROGBITS_LIMIT		BL1_RW_BASE
#endif

/*
 * BL3-2 specific defines.
 *
 * BL3-2 can execute from Secure SRAM, or Secure DRAM.
 */
#define BL32_SRAM_BASE			BL_RAM_BASE
#if SPM
# define BL32_SRAM_LIMIT		(BL_RAM_BASE + 0x1F000)
#else
# define BL32_SRAM_LIMIT		BL31_BASE
#endif
#define BL32_DRAM_BASE			SEC_DRAM_BASE
#define BL32_DRAM_LIMIT			(SEC_DRAM_BASE + SEC_DRAM_SIZE)

#define SEC_SRAM_ID			0
#define SEC_DRAM_ID			1

#if SPM && BL32_RAM_LOCATION_ID == SEC_SRAM_ID
# error " BL32_RAM_LOCATION_ID can not be SEC_SRAM_ID while SPM is enabled"
#endif

#if BL32_RAM_LOCATION_ID == SEC_SRAM_ID
# define BL32_MEM_BASE			BL_RAM_BASE
# define BL32_MEM_SIZE			BL_RAM_SIZE
# define BL32_BASE			BL32_SRAM_BASE
# define BL32_LIMIT			BL32_SRAM_LIMIT
#elif BL32_RAM_LOCATION_ID == SEC_DRAM_ID
# define BL32_MEM_BASE			SEC_DRAM_BASE
# define BL32_MEM_SIZE			SEC_DRAM_SIZE
# define BL32_BASE			BL32_DRAM_BASE
# define BL32_LIMIT			BL32_DRAM_LIMIT
#else
# error "Unsupported BL32_RAM_LOCATION_ID value"
#endif

#define NS_IMAGE_OFFSET			0x60000000

#define ADDR_SPACE_SIZE			(1ull << 32)
#if SPM
# define MAX_MMAP_REGIONS		9
# define MAX_XLAT_TABLES		7
#else
# define MAX_MMAP_REGIONS		8
# define MAX_XLAT_TABLES		6
#endif
#define MAX_IO_DEVICES			3
#define MAX_IO_HANDLES			4

#if SPM && defined(IMAGE_BL31)
# define SECURE_PARTITION_MMAP_REGIONS		7
# define SECURE_PARTITION_MAX_XLAT_TABLES	14
#endif

/*
 * PL011 related constants
 */
#define UART0_BASE			0x09000000
#define UART1_BASE			0x09040000
#define UART0_CLK_IN_HZ			1
#define UART1_CLK_IN_HZ			1

#define PLAT_QEMU_BOOT_UART_BASE	UART0_BASE
#define PLAT_QEMU_BOOT_UART_CLK_IN_HZ	UART0_CLK_IN_HZ

#define PLAT_QEMU_CRASH_UART_BASE	UART1_BASE
#define PLAT_QEMU_CRASH_UART_CLK_IN_HZ	UART1_CLK_IN_HZ

#define PLAT_QEMU_CONSOLE_BAUDRATE	115200

#define QEMU_FLASH0_BASE		0x04000000
#define QEMU_FLASH0_SIZE		0x04000000

#define PLAT_QEMU_FIP_BASE		QEMU_FLASH0_BASE
#define PLAT_QEMU_FIP_MAX_SIZE		QEMU_FLASH0_SIZE

#define DEVICE0_BASE			0x08000000
#define DEVICE0_SIZE			0x00021000
#define DEVICE1_BASE			0x09000000
#define DEVICE1_SIZE			0x00011000

/*
 * GIC related constants
 */

#define GICD_BASE			0x8000000
#define GICC_BASE			0x8010000
#define GICR_BASE			0


#define QEMU_IRQ_SEC_SGI_0		8
#define QEMU_IRQ_SEC_SGI_1		9
#define QEMU_IRQ_SEC_SGI_2		10
#define QEMU_IRQ_SEC_SGI_3		11
#define QEMU_IRQ_SEC_SGI_4		12
#define QEMU_IRQ_SEC_SGI_5		13
#define QEMU_IRQ_SEC_SGI_6		14
#define QEMU_IRQ_SEC_SGI_7		15

/*
 * DT related constants
 */
#define PLAT_QEMU_DT_BASE		NS_DRAM0_BASE
#define PLAT_QEMU_DT_MAX_SIZE		0x10000

/*
 * System counter
 */
#define SYS_COUNTER_FREQ_IN_TICKS	((1000 * 1000 * 1000) / 16)

#if SPM
/* The maximum size of the S-EL0 payload can be 3MB */
# define SECURE_PARTITION_BASE		BL32_BASE
# define SECURE_PARTITION_SIZE		ULL(0x300000)

#ifdef IMAGE_BL2
/* In BL2 all memory allocated to the SPM Payload image is marked as RW. */
#define SECURE_PARTITION_IMAGE_MMAP		MAP_REGION_FLAT(		\
						SECURE_PARTITION_BASE,		\
						SECURE_PARTITION_SIZE,		\
						MT_MEMORY | MT_RW | MT_SECURE)
#endif
#ifdef IMAGE_BL31
/* All SPM Payload memory is marked as code in S-EL1 */
#define SECURE_PARTITION_IMAGE_MMAP		MAP_REGION_GRANULARITY(		\
						SECURE_PARTITION_BASE,		\
						SECURE_PARTITION_BASE,		\
						SECURE_PARTITION_SIZE,		\
						MT_CODE | MT_SECURE,		\
						PAGE_SIZE)
#endif

/*
 * SPM Payload memory is followed by memory shared between EL3 and S-EL0. It is
 * used by the latter to push data into the former and hence mapped with RO
 * permission.
 */
#define SECURE_PARTITION_SPM_BUF_BASE		(SECURE_PARTITION_BASE + 	\
						 SECURE_PARTITION_SIZE)
#define SECURE_PARTITION_SPM_BUF_PCPU_SIZE	ULL(0x20000)
#define SECURE_PARTITION_SPM_BUF_SIZE		(PLATFORM_CORE_COUNT *		\
						SECURE_PARTITION_SPM_BUF_PCPU_SIZE)
#define SECURE_PARTITION_SPM_BUF_MMAP		MAP_REGION_GRANULARITY(		\
						SECURE_PARTITION_SPM_BUF_BASE,	\
						SECURE_PARTITION_SPM_BUF_BASE,	\
						SECURE_PARTITION_SPM_BUF_SIZE,	\
						MT_MEMORY | MT_RO | MT_SECURE,	\
						PAGE_SIZE)

/*
 * Shared memory is followed by memory shared between Normal world and S-EL0 for
 * passing data during service requests. It will be marked as RW and NS.
 */
#define SECURE_PARTITION_NS_BUF_BASE		(PLAT_QEMU_DT_BASE +\
						 PLAT_QEMU_DT_MAX_SIZE)
#define SECURE_PARTITION_NS_BUF_SIZE		ULL(0x10000)
#define SECURE_PARTITION_NS_BUF_MMAP		MAP_REGION_GRANULARITY(		\
						SECURE_PARTITION_NS_BUF_BASE,	\
						SECURE_PARTITION_NS_BUF_BASE,	\
						SECURE_PARTITION_NS_BUF_SIZE,	\
						MT_MEMORY | MT_RW | MT_NS,	\
						PAGE_SIZE)

/*
 * Memory shared with Normal world is followed by RW memory. First there is
 * stack memory for all CPUs and then there is the common heap memory. Both are
 * marked with RW permissions.
 */
#define SECURE_PARTITION_STACK_BASE		(SECURE_PARTITION_SPM_BUF_BASE +\
						 SECURE_PARTITION_SPM_BUF_SIZE)
#define SECURE_PARTITION_STACK_PCPU_SIZE	ULL(0x2000)
#define SECURE_PARTITION_STACK_TOTAL_SIZE	(PLATFORM_CORE_COUNT *		\
					 SECURE_PARTITION_STACK_PCPU_SIZE)

#define SECURE_PARTITION_HEAP_BASE		(SECURE_PARTITION_STACK_BASE +	\
					 SECURE_PARTITION_STACK_TOTAL_SIZE)
#define SECURE_PARTITION_HEAP_SIZE		BL32_LIMIT - SECURE_PARTITION_HEAP_BASE

#define SECURE_PARTITION_RW_MMAP		MAP_REGION_GRANULARITY(		\
						SECURE_PARTITION_STACK_BASE,	\
						SECURE_PARTITION_STACK_BASE,	\
						(BL32_LIMIT - 			\
						 SECURE_PARTITION_STACK_BASE),	\
						MT_MEMORY | MT_RW | MT_SECURE,	\
						PAGE_SIZE)

/* Total number of memory regions with distinct properties */
#define SECURE_PARTITION_NUM_MEM_REGIONS	6
#endif

#endif /* __PLATFORM_DEF_H__ */
