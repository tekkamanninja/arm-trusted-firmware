/*
 * Copyright (c) 2015-2016, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch_helpers.h>
#include <bl_common.h>
#include <platform_def.h>
#include <arm_xlat_tables.h>
#include "qemu_private.h"

#if SPM && defined(IMAGE_BL31)
# include <secure_partition.h>

#pragma weak plat_get_secure_partition_mmap
#pragma weak plat_get_secure_partition_boot_info
#endif

#define MAP_DEVICE0	MAP_REGION_FLAT(DEVICE0_BASE,			\
					DEVICE0_SIZE,			\
					MT_DEVICE | MT_RW | MT_SECURE)

#ifdef DEVICE1_BASE
#define MAP_DEVICE1	MAP_REGION_FLAT(DEVICE1_BASE,			\
					DEVICE1_SIZE,			\
					MT_DEVICE | MT_RW | MT_SECURE)
#endif

#ifdef DEVICE2_BASE
#define MAP_DEVICE2	MAP_REGION_FLAT(DEVICE2_BASE,			\
					DEVICE2_SIZE,			\
					MT_DEVICE | MT_RO | MT_SECURE)
#endif

#define MAP_SHARED_RAM	MAP_REGION_FLAT(SHARED_RAM_BASE,		\
					SHARED_RAM_SIZE,		\
					MT_DEVICE  | MT_RW | MT_SECURE)

#if SPD_tspd
#define MAP_BL32_MEM	MAP_REGION_FLAT(BL32_MEM_BASE, BL32_MEM_SIZE,	\
					MT_MEMORY | MT_RW | MT_SECURE)
#endif

#define MAP_NS_DRAM0	MAP_REGION_FLAT(NS_DRAM0_BASE, NS_DRAM0_SIZE,	\
					MT_MEMORY | MT_RW | MT_NS)

#define MAP_FLASH0	MAP_REGION_FLAT(QEMU_FLASH0_BASE, QEMU_FLASH0_SIZE, \
					MT_MEMORY | MT_RO | MT_SECURE)

#define MAP_BL31_SEC_DRAM	MAP_REGION_FLAT(BL31_BASE, 0x200000, \
					MT_MEMORY | MT_RW | MT_SECURE)


/*
 * Table of regions for various BL stages to map using the MMU.
 * This doesn't include TZRAM as the 'mem_layout' argument passed to
 * arm_configure_mmu_elx() will give the available subset of that,
 */
#ifdef IMAGE_BL1
static const mmap_region_t plat_qemu_mmap[] = {
	MAP_FLASH0,
	MAP_SHARED_RAM,
	MAP_DEVICE0,
#ifdef MAP_DEVICE1
	MAP_DEVICE1,
#endif
#ifdef MAP_DEVICE2
	MAP_DEVICE2,
#endif
	{0}
};
#endif
#ifdef IMAGE_BL2
static const mmap_region_t plat_qemu_mmap[] = {
	MAP_FLASH0,
	MAP_SHARED_RAM,
	MAP_DEVICE0,
#ifdef MAP_DEVICE1
	MAP_DEVICE1,
#endif
#ifdef MAP_DEVICE2
	MAP_DEVICE2,
#endif
	MAP_NS_DRAM0,
#if SPD_tspd
	MAP_BL32_MEM,
#endif
#if SPM
	SECURE_PARTITION_IMAGE_MMAP,
#endif
	MAP_BL31_SEC_DRAM,
	{0}
};
#endif
#ifdef IMAGE_BL31
static const mmap_region_t plat_qemu_mmap[] = {
	MAP_SHARED_RAM,
	MAP_DEVICE0,
#ifdef MAP_DEVICE1
	MAP_DEVICE1,
#endif
#if SPD_tspd
	MAP_BL32_MEM,
#endif
	{0}
};

#if SPM
const mmap_region_t plat_qemu_secure_partition_mmap[] = {
	MAP_DEVICE1, /* for the UART */
	SECURE_PARTITION_IMAGE_MMAP,
	SECURE_PARTITION_SPM_BUF_MMAP,
	SECURE_PARTITION_NS_BUF_MMAP,
	SECURE_PARTITION_RW_MMAP,
	{0}
};

/*
 * Boot information passed to a secure partition during initialisation. Linear
 * indices in MP information will be filled at runtime.
 */
static secure_partition_mp_info_t sp_mp_info[] = {
	[0] = {0x80000000, 0},
	[1] = {0x80000001, 0},
	[2] = {0x80000002, 0},
	[3] = {0x80000003, 0},
	[4] = {0x80000100, 0},
	[5] = {0x80000101, 0},
	[6] = {0x80000102, 0},
	[7] = {0x80000103, 0},
};

const secure_partition_boot_info_t plat_qemu_secure_partition_boot_info = {
	.h.type              = PARAM_SECURE_PARTITION_BOOT_INFO,
	.h.version           = VERSION_1,
	.h.size              = sizeof(secure_partition_boot_info_t),
	.h.attr              = 0,
	.sp_mem_base         = SECURE_PARTITION_BASE,
	.sp_mem_limit        = BL32_LIMIT,
	.sp_image_base       = SECURE_PARTITION_BASE,
	.sp_stack_base       = SECURE_PARTITION_STACK_BASE,
	.sp_heap_base        = SECURE_PARTITION_HEAP_BASE,
	.sp_ns_comm_buf_base = SECURE_PARTITION_NS_BUF_BASE,
	.sp_shared_buf_base  = SECURE_PARTITION_SPM_BUF_BASE,
	.sp_image_size       = SECURE_PARTITION_SIZE,
	.sp_pcpu_stack_size  = SECURE_PARTITION_STACK_PCPU_SIZE,
	.sp_heap_size        = SECURE_PARTITION_HEAP_SIZE,
	.sp_ns_comm_buf_size = SECURE_PARTITION_NS_BUF_SIZE,
	.sp_pcpu_shared_buf_size = SECURE_PARTITION_SPM_BUF_PCPU_SIZE,
	.num_sp_mem_regions  = SECURE_PARTITION_NUM_MEM_REGIONS,
	.num_cpus            = PLATFORM_CORE_COUNT,
	.mp_info             = &sp_mp_info[0],
};

const mmap_region_t *plat_get_secure_partition_mmap(void *cookie)
{
	return plat_qemu_secure_partition_mmap;
}

const secure_partition_boot_info_t *plat_get_secure_partition_boot_info(
	void *cookie)
{
	return &plat_qemu_secure_partition_boot_info;
}

#endif /* SPM */
#endif /* IMAGE_BL31 */

/*******************************************************************************
 * Macro generating the code for the function setting up the pagetables as per
 * the platform memory map & initialize the mmu, for the given exception level
 ******************************************************************************/

static void plat_qemu_mmap_add_secure_partition_region (void)
{
#if SPM && defined(IMAGE_BL31)
	mmap_add_region(SECURE_PARTITION_XLAT_TABLES_BASE,
			SECURE_PARTITION_XLAT_TABLES_BASE,
			SECURE_PARTITION_XLAT_TABLES_SIZE,
			MT_MEMORY | MT_RW | MT_SECURE);

	mmap_add_region(SECURE_PARTITION_SPM_BUF_BASE,
			SECURE_PARTITION_SPM_BUF_BASE,
			SECURE_PARTITION_SPM_BUF_SIZE,
			MT_MEMORY | MT_RW | MT_SECURE);
#endif
	return;
}

#define DEFINE_CONFIGURE_MMU_EL(_el)					\
	void qemu_configure_mmu_el##_el(unsigned long total_base,	\
				   unsigned long total_size,		\
				   unsigned long ro_start,		\
				   unsigned long ro_limit,		\
				   unsigned long coh_start,		\
				   unsigned long coh_limit)		\
	{								\
		mmap_add_region(total_base, total_base,			\
				total_size,				\
				MT_MEMORY | MT_RW | MT_SECURE);		\
		mmap_add_region(ro_start, ro_start,			\
				ro_limit - ro_start,			\
				MT_MEMORY | MT_RO | MT_SECURE);		\
		mmap_add_region(coh_start, coh_start,			\
				coh_limit - coh_start,			\
				MT_DEVICE | MT_RW | MT_SECURE);		\
		plat_qemu_mmap_add_secure_partition_region();		\
		mmap_add(plat_qemu_mmap);				\
		init_xlat_tables();					\
									\
		enable_mmu_el##_el(0);					\
	}

/* Define EL1 and EL3 variants of the function initialising the MMU */
DEFINE_CONFIGURE_MMU_EL(1)
DEFINE_CONFIGURE_MMU_EL(3)


