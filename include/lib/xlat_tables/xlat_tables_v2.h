/*
 * Copyright (c) 2017, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __XLAT_TABLES_V2_H__
#define __XLAT_TABLES_V2_H__

#include <xlat_tables_defs.h>

#ifndef __ASSEMBLY__
#include <stddef.h>
#include <stdint.h>
#include <xlat_mmu_helpers.h>
#include <xlat_tables_v2_helpers.h>

/* Helper macro to define entries for mmap_region_t. It creates
 * identity mappings for each region.
 */
#define MAP_REGION_FLAT(adr, sz, attr) MAP_REGION(adr, adr, sz, attr)

/* Helper macro to define entries for mmap_region_t. It allows to
 * re-map address mappings from 'pa' to 'va' for each region.
 */
#define MAP_REGION(pa, va, sz, attr) {(pa), (va), (sz), (attr), (sz)}

/* Helper macro to define entries for mmap_region_t. It allows to
 * re-map address mappings from 'pa' to 'va' for each region and specify
 * the desired granularity of translation table to use.
 */
#define MAP_REGION_GRANULARITY(pa, va, sz, attr, gr) {(pa), (va), (sz), (attr), (gr)}

/*
 * Shifts and masks to access fields of an mmap_attr_t
 */
#define MT_TYPE_MASK		U(0x7)
#define MT_TYPE(_attr)		((_attr) & MT_TYPE_MASK)
/* Access permissions (RO/RW) */
#define MT_PERM_SHIFT		U(3)
/* Security state (SECURE/NS) */
#define MT_SEC_SHIFT		U(4)
/* Access permissions for instruction execution (EXECUTE/EXECUTE_NEVER) */
#define MT_EXECUTE_SHIFT	U(5)
/* All other bits are reserved */

/*
 * Memory mapping attributes
 */
typedef enum  {
	/*
	 * Memory types supported.
	 * These are organised so that, going down the list, the memory types
	 * are getting weaker; conversely going up the list the memory types are
	 * getting stronger.
	 */
	MT_DEVICE,
	MT_NON_CACHEABLE,
	MT_MEMORY,
	/* Values up to 7 are reserved to add new memory types in the future */

	MT_RO		= U(0) << MT_PERM_SHIFT,
	MT_RW		= U(1) << MT_PERM_SHIFT,

	MT_SECURE	= U(0) << MT_SEC_SHIFT,
	MT_NS		= U(1) << MT_SEC_SHIFT,

	/*
	 * Access permissions for instruction execution are only relevant for
	 * normal read-only memory, i.e. MT_MEMORY | MT_RO. They are ignored
	 * (and potentially overridden) otherwise:
	 *  - Device memory is always marked as execute-never.
	 *  - Read-write normal memory is always marked as execute-never.
	 */
	MT_EXECUTE		= U(0) << MT_EXECUTE_SHIFT,
	MT_EXECUTE_NEVER	= U(1) << MT_EXECUTE_SHIFT,
} mmap_attr_t;

#define MT_CODE		(MT_MEMORY | MT_RO | MT_EXECUTE)
#define MT_RO_DATA	(MT_MEMORY | MT_RO | MT_EXECUTE_NEVER)

/*
 * Structure for specifying a single region of memory.
 */
typedef struct mmap_region {
	unsigned long long	base_pa;
	uintptr_t		base_va;
	size_t			size;
	mmap_attr_t		attr;

	/*
	 * The granularity field is used by the mapping routine to know up to
	 * which translation level it is needed to split a region. This is
	 * useful if its attributes may change in the future because no further
	 * splitting would be needed and no more space would be required to
	 * change the attributes.
	 */
	size_t			granularity;
} mmap_region_t;

/*
 * Declare the translation context type.
 * Its definition is private.
 */
typedef struct xlat_ctx xlat_ctx_t;

/*
 * Statically allocate a translation context and associated structures. Also
 * initialize them.
 *
 * _ctx_name:
 *   Prefix for the translation context variable.
 *   E.g. If _ctx_name is 'foo', the variable will be called 'foo_xlat_ctx'.
 *   Useful to distinguish multiple contexts from one another.
 *
 * _mmap_count:
 *   Number of mmap_region_t to allocate.
 *   Would typically be MAX_MMAP_REGIONS for the translation context describing
 *   the BL image currently executing.
 *
 * _xlat_tables_count:
 *   Number of sub-translation tables to allocate.
 *   Would typically be MAX_XLAT_TABLES for the translation context describing
 *   the BL image currently executing.
 *   Note that this is only for sub-tables ; at the initial lookup level, there
 *   is always a single table.
 *
 * _virt_addr_space_size, _phy_addr_space_size:
 *   Size (in bytes) of the virtual (resp. physical) address space.
 *   Would typically be PLAT_VIRT_ADDR_SPACE_SIZE
 *   (resp. PLAT_PHY_ADDR_SPACE_SIZE) for the translation context describing the
 *   BL image currently executing.
 */
#define REGISTER_XLAT_CONTEXT(_ctx_name, _mmap_count, _xlat_tables_count,	\
			_virt_addr_space_size, _phy_addr_space_size)		\
	_REGISTER_XLAT_CONTEXT(_ctx_name, _mmap_count, _xlat_tables_count,	\
		_virt_addr_space_size, _phy_addr_space_size)

/*
 * Same as REGISTER_XLAT_CONTEXT but allows to target a different exception
 * level than the current one.
 */
#define REGISTER_XLAT_CONTEXT_EL(_ctx_name, _mmap_count, _xlat_tables_count,	\
				 _virt_addr_space_size, _phy_addr_space_size,	\
				 _exception_level)				\
	_REGISTER_XLAT_CONTEXT_EL(_ctx_name, _mmap_count, _xlat_tables_count,	\
				  _virt_addr_space_size, _phy_addr_space_size,	\
				  _exception_level)

/******************************************************************************
 * Generic translation table APIs.
 * Each API comes in 2 variants:
 * - one that acts on the current translation context for this BL image
 * - another that acts on the given translation context instead. This variant
 *   is named after the 1st version, with an additional '_ctx' suffix.
 *****************************************************************************/

/*
 * Initialize translation tables from the current list of mmap regions. Calling
 * this function marks the transition point after which static regions can no
 * longer be added.
 */
void init_xlat_tables(void);
void init_xlat_tables_ctx(xlat_ctx_t *ctx);

/*
 * Add a static region with defined base PA and base VA. This function can only
 * be used before initializing the translation tables. The region cannot be
 * removed afterwards.
 */
void mmap_add_region(unsigned long long base_pa, uintptr_t base_va,
				size_t size, mmap_attr_t attr);
void mmap_add_region_ctx(xlat_ctx_t *ctx, const mmap_region_t *mm);

/*
 * Add an array of static regions with defined base PA and base VA. This
 * function can only be used before initializing the translation tables. The
 * regions cannot be removed afterwards.
 */
void mmap_add(const mmap_region_t *mm);
void mmap_add_ctx(xlat_ctx_t *ctx, const mmap_region_t *mm);


#if PLAT_XLAT_TABLES_DYNAMIC
/*
 * Add a dynamic region with defined base PA and base VA. This type of region
 * can be added and removed even after the translation tables are initialized.
 *
 * Returns:
 *        0: Success.
 *   EINVAL: Invalid values were used as arguments.
 *   ERANGE: Memory limits were surpassed.
 *   ENOMEM: Not enough space in the mmap array or not enough free xlat tables.
 *    EPERM: It overlaps another region in an invalid way.
 */
int mmap_add_dynamic_region(unsigned long long base_pa, uintptr_t base_va,
				size_t size, mmap_attr_t attr);
int mmap_add_dynamic_region_ctx(xlat_ctx_t *ctx, mmap_region_t *mm);

/*
 * Remove a region with the specified base VA and size. Only dynamic regions can
 * be removed, and they can be removed even if the translation tables are
 * initialized.
 *
 * Returns:
 *        0: Success.
 *   EINVAL: The specified region wasn't found.
 *    EPERM: Trying to remove a static region.
 */
int mmap_remove_dynamic_region(uintptr_t base_va, size_t size);
int mmap_remove_dynamic_region_ctx(xlat_ctx_t *ctx,
				uintptr_t base_va,
				size_t size);

#endif /* PLAT_XLAT_TABLES_DYNAMIC */

/*
 * Change the memory attributes of the memory region starting from a given
 * virtual address in a set of translation tables.
 *
 * The base address of the memory region must be aligned on a page boundary.
 * The size of this memory region must be a multiple of a page size.
 * The memory region must be already mapped by the given translation tables,
 * and it must be mapped at the lowest possible granularity.
 *
 * Return 0 on success, a negative value on error.
 *
 * In case of error, the memory attributes remain unchanged and this function
 * has no effect.
 *
 * ctx
 *   Translation context to work on.
 * base_va:
 *   Virtual address of the 1st page to change the attributes of.
 * size:
 *   Size in bytes of the memory region.
 * attributes:
 *   New attributes of the page tables.
 *
 * NOTE: The caller of this function must be able to write to the translation
 * tables, i.e. the memory where they are stored must be mapped with read-write
 * access permissions. This function assumes it is the case. If this is not
 * the case then this function might trigger a data abort exception.
 */
int change_mem_attributes(xlat_ctx_t *ctx, uintptr_t base_va, size_t size,
			  mmap_attr_t attributes);

#endif /*__ASSEMBLY__*/
#endif /* __XLAT_TABLES_V2_H__ */
