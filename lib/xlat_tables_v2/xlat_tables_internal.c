/*
 * Copyright (c) 2017, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch.h>
#include <arch_helpers.h>
#include <assert.h>
#include <bitops.h>
#include <common_def.h>
#include <debug.h>
#include <errno.h>
#include <platform_def.h>
#include <string.h>
#include <types.h>
#include <utils.h>
#include <xlat_tables_arch.h>
#include <xlat_tables_defs.h>
#include <xlat_tables_v2.h>

#include "xlat_tables_private.h"

/*
 * Each platform can define the size of its physical and virtual address spaces.
 * If the platform hasn't defined one or both of them, default to
 * ADDR_SPACE_SIZE. The latter is deprecated, though.
 */
#if ERROR_DEPRECATED
# ifdef ADDR_SPACE_SIZE
#  error "ADDR_SPACE_SIZE is deprecated. Use PLAT_xxx_ADDR_SPACE_SIZE instead."
# endif
#elif defined(ADDR_SPACE_SIZE)
# ifndef PLAT_PHY_ADDR_SPACE_SIZE
#  define PLAT_PHY_ADDR_SPACE_SIZE	ADDR_SPACE_SIZE
# endif
# ifndef PLAT_VIRT_ADDR_SPACE_SIZE
#  define PLAT_VIRT_ADDR_SPACE_SIZE	ADDR_SPACE_SIZE
# endif
#endif

/*
 * Allocate and initialise the default translation context for the BL image
 * currently executing.
 */
REGISTER_XLAT_CONTEXT(tf, MAX_MMAP_REGIONS, MAX_XLAT_TABLES,
		PLAT_VIRT_ADDR_SPACE_SIZE, PLAT_PHY_ADDR_SPACE_SIZE);

#if PLAT_XLAT_TABLES_DYNAMIC

/*
 * The following functions assume that they will be called using subtables only.
 * The base table can't be unmapped, so it is not needed to do any special
 * handling for it.
 */

/*
 * Returns the index of the array corresponding to the specified translation
 * table.
 */
static int xlat_table_get_index(xlat_ctx_t *ctx, const uint64_t *table)
{
	for (unsigned int i = 0; i < ctx->tables_num; i++)
		if (ctx->tables[i] == table)
			return i;

	/*
	 * Maybe we were asked to get the index of the base level table, which
	 * should never happen.
	 */
	assert(0);

	return -1;
}

/* Returns a pointer to an empty translation table. */
static uint64_t *xlat_table_get_empty(xlat_ctx_t *ctx)
{
	for (unsigned int i = 0; i < ctx->tables_num; i++)
		if (ctx->tables_mapped_regions[i] == 0)
			return ctx->tables[i];

	return NULL;
}

/* Increments region count for a given table. */
static void xlat_table_inc_regions_count(xlat_ctx_t *ctx, const uint64_t *table)
{
	ctx->tables_mapped_regions[xlat_table_get_index(ctx, table)]++;
}

/* Decrements region count for a given table. */
static void xlat_table_dec_regions_count(xlat_ctx_t *ctx, const uint64_t *table)
{
	ctx->tables_mapped_regions[xlat_table_get_index(ctx, table)]--;
}

/* Returns 0 if the speficied table isn't empty, otherwise 1. */
static int xlat_table_is_empty(xlat_ctx_t *ctx, const uint64_t *table)
{
	return !ctx->tables_mapped_regions[xlat_table_get_index(ctx, table)];
}

#else /* PLAT_XLAT_TABLES_DYNAMIC */

/* Returns a pointer to the first empty translation table. */
static uint64_t *xlat_table_get_empty(xlat_ctx_t *ctx)
{
	assert(ctx->next_table < ctx->tables_num);

	return ctx->tables[ctx->next_table++];
}

#endif /* PLAT_XLAT_TABLES_DYNAMIC */

/* Returns a block/page table descriptor for the given level and attributes. */
static uint64_t xlat_desc(mmap_attr_t attr, unsigned long long addr_pa,
			  int level, uint64_t execute_never_mask)
{
	uint64_t desc;
	int mem_type;

	/* Make sure that the granularity is fine enough to map this address. */
	assert((addr_pa & XLAT_BLOCK_MASK(level)) == 0);

	desc = addr_pa;
	/*
	 * There are different translation table descriptors for level 3 and the
	 * rest.
	 */
	desc |= (level == XLAT_TABLE_LEVEL_MAX) ? PAGE_DESC : BLOCK_DESC;
	/*
	 * Always set the access flag, as TF doesn't manage access flag faults.
	 * Deduce other fields of the descriptor based on the MT_NS and MT_RW
	 * memory region attributes.
	 */
	desc |= (attr & MT_NS) ? LOWER_ATTRS(NS) : 0;
	desc |= (attr & MT_RW) ? LOWER_ATTRS(AP_RW) : LOWER_ATTRS(AP_RO);
	desc |= LOWER_ATTRS(ACCESS_FLAG);

	/*
	 * Deduce shareability domain and executability of the memory region
	 * from the memory type of the attributes (MT_TYPE).
	 *
	 * Data accesses to device memory and non-cacheable normal memory are
	 * coherent for all observers in the system, and correspondingly are
	 * always treated as being Outer Shareable. Therefore, for these 2 types
	 * of memory, it is not strictly needed to set the shareability field
	 * in the translation tables.
	 */
	mem_type = MT_TYPE(attr);
	if (mem_type == MT_DEVICE) {
		desc |= LOWER_ATTRS(ATTR_DEVICE_INDEX | OSH);
		/*
		 * Always map device memory as execute-never.
		 * This is to avoid the possibility of a speculative instruction
		 * fetch, which could be an issue if this memory region
		 * corresponds to a read-sensitive peripheral.
		 */
		desc |= execute_never_mask;

	} else { /* Normal memory */
		/*
		 * Always map read-write normal memory as execute-never.
		 * (Trusted Firmware doesn't self-modify its code, therefore
		 * R/W memory is reserved for data storage, which must not be
		 * executable.)
		 * Note that setting the XN bit here is for consistency only.
		 * The function that enables the MMU sets the SCTLR_ELx.WXN bit,
		 * which makes any writable memory region to be treated as
		 * execute-never, regardless of the value of the XN bit in the
		 * translation table.
		 *
		 * For read-only memory, rely on the MT_EXECUTE/MT_EXECUTE_NEVER
		 * attribute to figure out the value of the XN bit.
		 */
		if ((attr & MT_RW) || (attr & MT_EXECUTE_NEVER)) {
			desc |= execute_never_mask;
		}

		if (mem_type == MT_MEMORY) {
			desc |= LOWER_ATTRS(ATTR_IWBWA_OWBWA_NTR_INDEX | ISH);
		} else {
			assert(mem_type == MT_NON_CACHEABLE);
			desc |= LOWER_ATTRS(ATTR_NON_CACHEABLE_INDEX | OSH);
		}
	}

	return desc;
}

/*
 * Enumeration of actions that can be made when mapping table entries depending
 * on the previous value in that entry and information about the region being
 * mapped.
 */
typedef enum {

	/* Do nothing */
	ACTION_NONE,

	/* Write a block (or page, if in level 3) entry. */
	ACTION_WRITE_BLOCK_ENTRY,

	/*
	 * Create a new table and write a table entry pointing to it. Recurse
	 * into it for further processing.
	 */
	ACTION_CREATE_NEW_TABLE,

	/*
	 * There is a table descriptor in this entry, read it and recurse into
	 * that table for further processing.
	 */
	ACTION_RECURSE_INTO_TABLE,

} action_t;

#if PLAT_XLAT_TABLES_DYNAMIC

/*
 * Recursive function that writes to the translation tables and unmaps the
 * specified region.
 */
static void xlat_tables_unmap_region(xlat_ctx_t *ctx, mmap_region_t *mm,
				     const uintptr_t table_base_va,
				     uint64_t *const table_base,
				     const int table_entries,
				     const unsigned int level)
{
	assert(level >= ctx->base_level && level <= XLAT_TABLE_LEVEL_MAX);

	uint64_t *subtable;
	uint64_t desc;

	uintptr_t table_idx_va;
	uintptr_t table_idx_end_va; /* End VA of this entry */

	uintptr_t region_end_va = mm->base_va + mm->size - 1;

	int table_idx;

	if (mm->base_va > table_base_va) {
		/* Find the first index of the table affected by the region. */
		table_idx_va = mm->base_va & ~XLAT_BLOCK_MASK(level);

		table_idx = (table_idx_va - table_base_va) >>
			    XLAT_ADDR_SHIFT(level);

		assert(table_idx < table_entries);
	} else {
		/* Start from the beginning of the table. */
		table_idx_va = table_base_va;
		table_idx = 0;
	}

	while (table_idx < table_entries) {

		table_idx_end_va = table_idx_va + XLAT_BLOCK_SIZE(level) - 1;

		desc = table_base[table_idx];
		uint64_t desc_type = desc & DESC_MASK;

		action_t action = ACTION_NONE;

		if ((mm->base_va <= table_idx_va) &&
		    (region_end_va >= table_idx_end_va)) {

			/* Region covers all block */

			if (level == 3) {
				/*
				 * Last level, only page descriptors allowed,
				 * erase it.
				 */
				assert(desc_type == PAGE_DESC);

				action = ACTION_WRITE_BLOCK_ENTRY;
			} else {
				/*
				 * Other levels can have table descriptors. If
				 * so, recurse into it and erase descriptors
				 * inside it as needed. If there is a block
				 * descriptor, just erase it. If an invalid
				 * descriptor is found, this table isn't
				 * actually mapped, which shouldn't happen.
				 */
				if (desc_type == TABLE_DESC) {
					action = ACTION_RECURSE_INTO_TABLE;
				} else {
					assert(desc_type == BLOCK_DESC);
					action = ACTION_WRITE_BLOCK_ENTRY;
				}
			}

		} else if ((mm->base_va <= table_idx_end_va) ||
			   (region_end_va >= table_idx_va)) {

			/*
			 * Region partially covers block.
			 *
			 * It can't happen in level 3.
			 *
			 * There must be a table descriptor here, if not there
			 * was a problem when mapping the region.
			 */

			assert(level < 3);

			assert(desc_type == TABLE_DESC);

			action = ACTION_RECURSE_INTO_TABLE;
		}

		if (action == ACTION_WRITE_BLOCK_ENTRY) {

			table_base[table_idx] = INVALID_DESC;
			xlat_arch_tlbi_va_el(table_idx_va,
					     ctx->exception_level);

		} else if (action == ACTION_RECURSE_INTO_TABLE) {

			subtable = (uint64_t *)(uintptr_t)(desc & TABLE_ADDR_MASK);

			/* Recurse to write into subtable */
			xlat_tables_unmap_region(ctx, mm, table_idx_va,
						 subtable, XLAT_TABLE_ENTRIES,
						 level + 1);

			/*
			 * If the subtable is now empty, remove its reference.
			 */
			if (xlat_table_is_empty(ctx, subtable)) {
				table_base[table_idx] = INVALID_DESC;
				xlat_arch_tlbi_va_el(table_idx_va,
						     ctx->exception_level);
			}

		} else {
			assert(action == ACTION_NONE);
		}

		table_idx++;
		table_idx_va += XLAT_BLOCK_SIZE(level);

		/* If reached the end of the region, exit */
		if (region_end_va <= table_idx_va)
			break;
	}

	if (level > ctx->base_level)
		xlat_table_dec_regions_count(ctx, table_base);
}

#endif /* PLAT_XLAT_TABLES_DYNAMIC */

/*
 * From the given arguments, it decides which action to take when mapping the
 * specified region.
 */
static action_t xlat_tables_map_region_action(const mmap_region_t *mm,
		const int desc_type, const unsigned long long dest_pa,
		const uintptr_t table_entry_base_va, const unsigned int level)
{
	uintptr_t mm_end_va = mm->base_va + mm->size - 1;
	uintptr_t table_entry_end_va =
			table_entry_base_va + XLAT_BLOCK_SIZE(level) - 1;

	/*
	 * The descriptor types allowed depend on the current table level.
	 */

	if ((mm->base_va <= table_entry_base_va) &&
	    (mm_end_va >= table_entry_end_va)) {

		/*
		 * Table entry is covered by region
		 * --------------------------------
		 *
		 * This means that this table entry can describe the whole
		 * translation with this granularity in principle.
		 */

		if (level == 3) {
			/*
			 * Last level, only page descriptors are allowed.
			 */
			if (desc_type == PAGE_DESC) {
				/*
				 * There's another region mapped here, don't
				 * overwrite.
				 */
				return ACTION_NONE;
			} else {
				assert(desc_type == INVALID_DESC);
				return ACTION_WRITE_BLOCK_ENTRY;
			}

		} else {

			/*
			 * Other levels. Table descriptors are allowed. Block
			 * descriptors too, but they have some limitations.
			 */

			if (desc_type == TABLE_DESC) {
				/* There's already a table, recurse into it. */
				return ACTION_RECURSE_INTO_TABLE;

			} else if (desc_type == INVALID_DESC) {
				/*
				 * There's nothing mapped here, create a new
				 * entry.
				 *
				 * Check if the destination granularity allows
				 * us to use a block descriptor or we need a
				 * finer table for it.
				 *
				 * Also, check if the current level allows block
				 * descriptors. If not, create a table instead.
				 */
				if ((dest_pa & XLAT_BLOCK_MASK(level)) ||
				    (level < MIN_LVL_BLOCK_DESC) ||
				    (mm->granularity < XLAT_BLOCK_SIZE(level)))
					return ACTION_CREATE_NEW_TABLE;
				else
					return ACTION_WRITE_BLOCK_ENTRY;

			} else {
				/*
				 * There's another region mapped here, don't
				 * overwrite.
				 */
				assert(desc_type == BLOCK_DESC);

				return ACTION_NONE;
			}
		}

	} else if ((mm->base_va <= table_entry_end_va) ||
		   (mm_end_va >= table_entry_base_va)) {

		/*
		 * Region partially covers table entry
		 * -----------------------------------
		 *
		 * This means that this table entry can't describe the whole
		 * translation, a finer table is needed.

		 * There cannot be partial block overlaps in level 3. If that
		 * happens, some of the preliminary checks when adding the
		 * mmap region failed to detect that PA and VA must at least be
		 * aligned to PAGE_SIZE.
		 */
		assert(level < 3);

		if (desc_type == INVALID_DESC) {
			/*
			 * The block is not fully covered by the region. Create
			 * a new table, recurse into it and try to map the
			 * region with finer granularity.
			 */
			return ACTION_CREATE_NEW_TABLE;

		} else {
			assert(desc_type == TABLE_DESC);
			/*
			 * The block is not fully covered by the region, but
			 * there is already a table here. Recurse into it and
			 * try to map with finer granularity.
			 *
			 * PAGE_DESC for level 3 has the same value as
			 * TABLE_DESC, but this code can't run on a level 3
			 * table because there can't be overlaps in level 3.
			 */
			return ACTION_RECURSE_INTO_TABLE;
		}
	}

	/*
	 * This table entry is outside of the region specified in the arguments,
	 * don't write anything to it.
	 */
	return ACTION_NONE;
}

/*
 * Recursive function that writes to the translation tables and maps the
 * specified region. On success, it returns the VA of the last byte that was
 * succesfully mapped. On error, it returns the VA of the next entry that
 * should have been mapped.
 */
static uintptr_t xlat_tables_map_region(xlat_ctx_t *ctx, mmap_region_t *mm,
				   const uintptr_t table_base_va,
				   uint64_t *const table_base,
				   const int table_entries,
				   const unsigned int level)
{
	assert(level >= ctx->base_level && level <= XLAT_TABLE_LEVEL_MAX);

	uintptr_t mm_end_va = mm->base_va + mm->size - 1;

	uintptr_t table_idx_va;
	unsigned long long table_idx_pa;

	uint64_t *subtable;
	uint64_t desc;

	int table_idx;

	if (mm->base_va > table_base_va) {
		/* Find the first index of the table affected by the region. */
		table_idx_va = mm->base_va & ~XLAT_BLOCK_MASK(level);

		table_idx = (table_idx_va - table_base_va) >>
			    XLAT_ADDR_SHIFT(level);

		assert(table_idx < table_entries);
	} else {
		/* Start from the beginning of the table. */
		table_idx_va = table_base_va;
		table_idx = 0;
	}

#if PLAT_XLAT_TABLES_DYNAMIC
	if (level > ctx->base_level)
		xlat_table_inc_regions_count(ctx, table_base);
#endif

	while (table_idx < table_entries) {

		desc = table_base[table_idx];

		table_idx_pa = mm->base_pa + table_idx_va - mm->base_va;

		action_t action = xlat_tables_map_region_action(mm,
			desc & DESC_MASK, table_idx_pa, table_idx_va, level);

		if (action == ACTION_WRITE_BLOCK_ENTRY) {

			table_base[table_idx] =
				xlat_desc(mm->attr, table_idx_pa, level,
					  ctx->execute_never_mask);

		} else if (action == ACTION_CREATE_NEW_TABLE) {

			subtable = xlat_table_get_empty(ctx);
			if (subtable == NULL) {
				/* Not enough free tables to map this region */
				return table_idx_va;
			}

			/* Point to new subtable from this one. */
			table_base[table_idx] = TABLE_DESC | (unsigned long)subtable;

			/* Recurse to write into subtable */
			uintptr_t end_va = xlat_tables_map_region(ctx, mm, table_idx_va,
					       subtable, XLAT_TABLE_ENTRIES,
					       level + 1);
			if (end_va != table_idx_va + XLAT_BLOCK_SIZE(level) - 1)
				return end_va;

		} else if (action == ACTION_RECURSE_INTO_TABLE) {

			subtable = (uint64_t *)(uintptr_t)(desc & TABLE_ADDR_MASK);
			/* Recurse to write into subtable */
			uintptr_t end_va =  xlat_tables_map_region(ctx, mm, table_idx_va,
					       subtable, XLAT_TABLE_ENTRIES,
					       level + 1);
			if (end_va != table_idx_va + XLAT_BLOCK_SIZE(level) - 1)
				return end_va;

		} else {

			assert(action == ACTION_NONE);

		}

		table_idx++;
		table_idx_va += XLAT_BLOCK_SIZE(level);

		/* If reached the end of the region, exit */
		if (mm_end_va <= table_idx_va)
			break;
	}

	return table_idx_va - 1;
}

void print_mmap(mmap_region_t *const mmap)
{
#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
	tf_printf("mmap:\n");
	mmap_region_t *mm = mmap;

	while (mm->size) {
		tf_printf(" VA:%p  PA:0x%llx  size:0x%zx  attr:0x%x",
			  (void *)mm->base_va, mm->base_pa, mm->size, mm->attr);
		if (mm->size != mm->granularity)
			tf_printf(" granularity:0x%zx\n", mm->granularity);
		else
			tf_printf("\n");
		++mm;
	};
	tf_printf("\n");
#endif
}

/*
 * Function that verifies that a region can be mapped.
 * Returns:
 *        0: Success, the mapping is allowed.
 *   EINVAL: Invalid values were used as arguments.
 *   ERANGE: The memory limits were surpassed.
 *   ENOMEM: There is not enough memory in the mmap array.
 *    EPERM: Region overlaps another one in an invalid way.
 */
static int mmap_add_region_check(xlat_ctx_t *ctx, unsigned long long base_pa,
				 uintptr_t base_va, size_t size,
				 mmap_attr_t attr, size_t granularity)
{
	mmap_region_t *mm = ctx->mmap;
	unsigned long long end_pa = base_pa + size - 1;
	uintptr_t end_va = base_va + size - 1;

	if (!IS_PAGE_ALIGNED(base_pa) || !IS_PAGE_ALIGNED(base_va) ||
		!IS_PAGE_ALIGNED(size) || !IS_PAGE_ALIGNED(granularity))
		return -EINVAL;

	/* Check for overflows */
	if ((base_pa > end_pa) || (base_va > end_va))
		return -ERANGE;

	if ((base_va + (uintptr_t)size - (uintptr_t)1) > ctx->va_max_address)
		return -ERANGE;

	if ((base_pa + (unsigned long long)size - 1ULL) > ctx->pa_max_address)
		return -ERANGE;

	/* Check that there is space in the mmap array */
	if (ctx->mmap[ctx->mmap_num - 1].size != 0)
		return -ENOMEM;

	/* Check for PAs and VAs overlaps with all other regions */
	for (mm = ctx->mmap; mm->size; ++mm) {

		uintptr_t mm_end_va = mm->base_va + mm->size - 1;

		/*
		 * Check if one of the regions is completely inside the other
		 * one.
		 */
		int fully_overlapped_va =
			((base_va >= mm->base_va) && (end_va <= mm_end_va)) ||
			((mm->base_va >= base_va) && (mm_end_va <= end_va));

		/*
		 * Full VA overlaps are only allowed if both regions are
		 * identity mapped (zero offset) or have the same VA to PA
		 * offset. Also, make sure that it's not the exact same area.
		 * This can only be done with static regions.
		 */
		if (fully_overlapped_va) {

#if PLAT_XLAT_TABLES_DYNAMIC
			if ((attr & MT_DYNAMIC) || (mm->attr & MT_DYNAMIC))
				return -EPERM;
#endif /* PLAT_XLAT_TABLES_DYNAMIC */
			if ((mm->base_va - mm->base_pa) != (base_va - base_pa))
				return -EPERM;

			if ((base_va == mm->base_va) && (size == mm->size))
				return -EPERM;

		} else {
			/*
			 * If the regions do not have fully overlapping VAs,
			 * then they must have fully separated VAs and PAs.
			 * Partial overlaps are not allowed
			 */

			unsigned long long mm_end_pa =
						     mm->base_pa + mm->size - 1;

			int separated_pa =
				(end_pa < mm->base_pa) || (base_pa > mm_end_pa);
			int separated_va =
				(end_va < mm->base_va) || (base_va > mm_end_va);

			if (!(separated_va && separated_pa))
				return -EPERM;
		}
	}

	return 0;
}

void mmap_add_region_ctx(xlat_ctx_t *ctx, const mmap_region_t *mm)
{
	mmap_region_t *mm_cursor = ctx->mmap;
	mmap_region_t *mm_last = mm_cursor + ctx->mmap_num;
	unsigned long long end_pa = mm->base_pa + mm->size - 1;
	uintptr_t end_va = mm->base_va + mm->size - 1;
	int ret;

	/* Ignore empty regions */
	if (!mm->size)
		return;

	/* Static regions must be added before initializing the xlat tables. */
	assert(!ctx->initialized);

	ret = mmap_add_region_check(ctx, mm->base_pa, mm->base_va, mm->size,
				mm->attr, mm->granularity);
	if (ret != 0) {
		ERROR("mmap_add_region_check() failed. error %d\n", ret);
		assert(0);
		return;
	}

	/*
	 * Find correct place in mmap to insert new region.
	 *
	 * 1 - Lower region VA end first.
	 * 2 - Smaller region size first.
	 *
	 * VA  0                                   0xFF
	 *
	 * 1st |------|
	 * 2nd |------------|
	 * 3rd                 |------|
	 * 4th                            |---|
	 * 5th                                   |---|
	 * 6th                            |----------|
	 * 7th |-------------------------------------|
	 *
	 * This is required for overlapping regions only. It simplifies adding
	 * regions with the loop in xlat_tables_init_internal because the outer
	 * ones won't overwrite block or page descriptors of regions added
	 * previously.
	 *
	 * Overlapping is only allowed for static regions.
	 */

	while ((mm_cursor->base_va + mm_cursor->size - 1) < end_va
	       && mm_cursor->size)
		++mm_cursor;

	while ((mm_cursor->base_va + mm_cursor->size - 1 == end_va)
	       && (mm_cursor->size < mm->size))
		++mm_cursor;

	/* Make room for new region by moving other regions up by one place */
	memmove(mm_cursor + 1, mm_cursor,
		(uintptr_t)mm_last - (uintptr_t)mm_cursor);

	/*
	 * Check we haven't lost the empty sentinel from the end of the array.
	 * This shouldn't happen as we have checked in mmap_add_region_check
	 * that there is free space.
	 */
	assert(mm_last->size == 0);

	mm_cursor->base_pa = mm->base_pa;
	mm_cursor->base_va = mm->base_va;
	mm_cursor->size = mm->size;
	mm_cursor->attr = mm->attr;
	mm_cursor->granularity = mm->granularity;

	if (end_pa > ctx->max_pa)
		ctx->max_pa = end_pa;
	if (end_va > ctx->max_va)
		ctx->max_va = end_va;
}

void mmap_add_region(unsigned long long base_pa,
				uintptr_t base_va,
				size_t size,
				mmap_attr_t attr)
{
	mmap_region_t mm = {
		.base_va = base_va,
		.base_pa = base_pa,
		.size = size,
		.attr = attr,
	};
	mmap_add_region_ctx(&tf_xlat_ctx, &mm);
}


void mmap_add_ctx(xlat_ctx_t *ctx, const mmap_region_t *mm)
{
	while (mm->size) {
		mmap_add_region_ctx(ctx, mm);
		mm++;
	}
}

void mmap_add(const mmap_region_t *mm)
{
	mmap_add_ctx(&tf_xlat_ctx, mm);
}

#if PLAT_XLAT_TABLES_DYNAMIC

int mmap_add_dynamic_region_ctx(xlat_ctx_t *ctx, mmap_region_t *mm)
{
	mmap_region_t *mm_cursor = ctx->mmap;
	mmap_region_t *mm_last = mm_cursor + ctx->mmap_num;
	unsigned long long end_pa = mm->base_pa + mm->size - 1;
	uintptr_t end_va = mm->base_va + mm->size - 1;
	int ret;

	/* Nothing to do */
	if (!mm->size)
		return 0;

	ret = mmap_add_region_check(ctx, mm->base_pa, mm->base_va, mm->size,
				    mm->attr | MT_DYNAMIC, mm->granularity);
	if (ret != 0)
		return ret;

	/*
	 * Find the adequate entry in the mmap array in the same way done for
	 * static regions in mmap_add_region_ctx().
	 */

	while ((mm_cursor->base_va + mm_cursor->size - 1) < end_va && mm_cursor->size)
		++mm_cursor;

	while ((mm_cursor->base_va + mm_cursor->size - 1 == end_va) && (mm_cursor->size < mm->size))
		++mm_cursor;

	/* Make room for new region by moving other regions up by one place */
	memmove(mm_cursor + 1, mm_cursor, (uintptr_t)mm_last - (uintptr_t)mm_cursor);

	/*
	 * Check we haven't lost the empty sentinal from the end of the array.
	 * This shouldn't happen as we have checked in mmap_add_region_check
	 * that there is free space.
	 */
	assert(mm_last->size == 0);

	mm_cursor->base_pa = mm->base_pa;
	mm_cursor->base_va = mm->base_va;
	mm_cursor->size = mm->size;
	mm_cursor->attr = mm->attr | MT_DYNAMIC;
	mm_cursor->granularity = mm->granularity;

	/*
	 * Update the translation tables if the xlat tables are initialized. If
	 * not, this region will be mapped when they are initialized.
	 */
	if (ctx->initialized) {
		uintptr_t end_va = xlat_tables_map_region(ctx, mm_cursor, 0, ctx->base_table,
				ctx->base_table_entries, ctx->base_level);

		/* Failed to map, remove mmap entry, unmap and return error. */
		if (end_va != mm_cursor->base_va + mm_cursor->size - 1) {
			memmove(mm_cursor, mm_cursor + 1, (uintptr_t)mm_last - (uintptr_t)mm_cursor);

			/*
			 * Check if the mapping function actually managed to map
			 * anything. If not, just return now.
			 */
			if (mm_cursor->base_va >= end_va)
				return -ENOMEM;

			/*
			 * Something went wrong after mapping some table entries,
			 * undo every change done up to this point.
			 */
			mmap_region_t unmap_mm = {
					.base_pa = 0,
					.base_va = mm->base_va,
					.size = end_va - mm->base_va,
					.attr = 0
			};
			xlat_tables_unmap_region(ctx, &unmap_mm, 0, ctx->base_table,
							ctx->base_table_entries, ctx->base_level);

			return -ENOMEM;
		}

		/*
		 * Make sure that all entries are written to the memory. There
		 * is no need to invalidate entries when mapping dynamic regions
		 * because new table/block/page descriptors only replace old
		 * invalid descriptors, that aren't TLB cached.
		 */
		dsbishst();
	}

	if (end_pa > ctx->max_pa)
		ctx->max_pa = end_pa;
	if (end_va > ctx->max_va)
		ctx->max_va = end_va;

	return 0;
}

int mmap_add_dynamic_region(unsigned long long base_pa,
			    uintptr_t base_va, size_t size, mmap_attr_t attr)
{
	mmap_region_t mm = {
		.base_va = base_va,
		.base_pa = base_pa,
		.size = size,
		.attr = attr,
	};
	return mmap_add_dynamic_region_ctx(&tf_xlat_ctx, &mm);
}

/*
 * Removes the region with given base Virtual Address and size from the given
 * context.
 *
 * Returns:
 *        0: Success.
 *   EINVAL: Invalid values were used as arguments (region not found).
 *    EPERM: Tried to remove a static region.
 */
int mmap_remove_dynamic_region_ctx(xlat_ctx_t *ctx, uintptr_t base_va,
				   size_t size)
{
	mmap_region_t *mm = ctx->mmap;
	mmap_region_t *mm_last = mm + ctx->mmap_num;
	int update_max_va_needed = 0;
	int update_max_pa_needed = 0;

	/* Check sanity of mmap array. */
	assert(mm[ctx->mmap_num].size == 0);

	while (mm->size) {
		if ((mm->base_va == base_va) && (mm->size == size))
			break;
		++mm;
	}

	/* Check that the region was found */
	if (mm->size == 0)
		return -EINVAL;

	/* If the region is static it can't be removed */
	if (!(mm->attr & MT_DYNAMIC))
		return -EPERM;

	/* Check if this region is using the top VAs or PAs. */
	if ((mm->base_va + mm->size - 1) == ctx->max_va)
		update_max_va_needed = 1;
	if ((mm->base_pa + mm->size - 1) == ctx->max_pa)
		update_max_pa_needed = 1;

	/* Update the translation tables if needed */
	if (ctx->initialized) {
		xlat_tables_unmap_region(ctx, mm, 0, ctx->base_table,
					 ctx->base_table_entries,
					 ctx->base_level);
		xlat_arch_tlbi_va_sync();
	}

	/* Remove this region by moving the rest down by one place. */
	memmove(mm, mm + 1, (uintptr_t)mm_last - (uintptr_t)mm);

	/* Check if we need to update the max VAs and PAs */
	if (update_max_va_needed) {
		ctx->max_va = 0;
		mm = ctx->mmap;
		while (mm->size) {
			if ((mm->base_va + mm->size - 1) > ctx->max_va)
				ctx->max_va = mm->base_va + mm->size - 1;
			++mm;
		}
	}

	if (update_max_pa_needed) {
		ctx->max_pa = 0;
		mm = ctx->mmap;
		while (mm->size) {
			if ((mm->base_pa + mm->size - 1) > ctx->max_pa)
				ctx->max_pa = mm->base_pa + mm->size - 1;
			++mm;
		}
	}

	return 0;
}

int mmap_remove_dynamic_region(uintptr_t base_va, size_t size)
{
	return mmap_remove_dynamic_region_ctx(&tf_xlat_ctx,
					base_va, size);
}

#endif /* PLAT_XLAT_TABLES_DYNAMIC */

#if LOG_LEVEL >= LOG_LEVEL_VERBOSE

/* Print the attributes of the specified block descriptor. */
static void xlat_desc_print(uint64_t desc, uint64_t execute_never_mask)
{
	int mem_type_index = ATTR_INDEX_GET(desc);

	if (mem_type_index == ATTR_IWBWA_OWBWA_NTR_INDEX) {
		tf_printf("MEM");
	} else if (mem_type_index == ATTR_NON_CACHEABLE_INDEX) {
		tf_printf("NC");
	} else {
		assert(mem_type_index == ATTR_DEVICE_INDEX);
		tf_printf("DEV");
	}

	tf_printf(LOWER_ATTRS(AP_RO) & desc ? "-RO" : "-RW");
	tf_printf(LOWER_ATTRS(NS) & desc ? "-NS" : "-S");
	tf_printf(execute_never_mask & desc ? "-XN" : "-EXEC");
}

static const char * const level_spacers[] = {
	"[LV0] ",
	"  [LV1] ",
	"    [LV2] ",
	"      [LV3] "
};

static const char *invalid_descriptors_ommited =
		"%s(%d invalid descriptors omitted)\n";

/*
 * Recursive function that reads the translation tables passed as an argument
 * and prints their status.
 */
static void xlat_tables_print_internal(const uintptr_t table_base_va,
		uint64_t *const table_base, const int table_entries,
		const unsigned int level, const uint64_t execute_never_mask)
{
	assert(level <= XLAT_TABLE_LEVEL_MAX);

	uint64_t desc;
	uintptr_t table_idx_va = table_base_va;
	int table_idx = 0;

	size_t level_size = XLAT_BLOCK_SIZE(level);

	/*
	 * Keep track of how many invalid descriptors are counted in a row.
	 * Whenever multiple invalid descriptors are found, only the first one
	 * is printed, and a line is added to inform about how many descriptors
	 * have been omitted.
	 */
	int invalid_row_count = 0;

	while (table_idx < table_entries) {

		desc = table_base[table_idx];

		if ((desc & DESC_MASK) == INVALID_DESC) {

			if (invalid_row_count == 0) {
				tf_printf("%sVA:%p size:0x%zx\n",
					  level_spacers[level],
					  (void *)table_idx_va, level_size);
			}
			invalid_row_count++;

		} else {

			if (invalid_row_count > 1) {
				tf_printf(invalid_descriptors_ommited,
					  level_spacers[level],
					  invalid_row_count - 1);
			}
			invalid_row_count = 0;

			/*
			 * Check if this is a table or a block. Tables are only
			 * allowed in levels other than 3, but DESC_PAGE has the
			 * same value as DESC_TABLE, so we need to check.
			 */
			if (((desc & DESC_MASK) == TABLE_DESC) &&
					(level < XLAT_TABLE_LEVEL_MAX)) {
				/*
				 * Do not print any PA for a table descriptor,
				 * as it doesn't directly map physical memory
				 * but instead points to the next translation
				 * table in the translation table walk.
				 */
				tf_printf("%sVA:%p size:0x%zx\n",
					  level_spacers[level],
					  (void *)table_idx_va, level_size);

				uintptr_t addr_inner = desc & TABLE_ADDR_MASK;

				xlat_tables_print_internal(table_idx_va,
					(uint64_t *)addr_inner,
					XLAT_TABLE_ENTRIES, level+1,
					execute_never_mask);
			} else {
				tf_printf("%sVA:%p PA:0x%llx size:0x%zx ",
					  level_spacers[level],
					  (void *)table_idx_va,
					  (unsigned long long)(desc & TABLE_ADDR_MASK),
					  level_size);
				xlat_desc_print(desc, execute_never_mask);
				tf_printf("\n");
			}
		}

		table_idx++;
		table_idx_va += level_size;
	}

	if (invalid_row_count > 1) {
		tf_printf(invalid_descriptors_ommited,
			  level_spacers[level], invalid_row_count - 1);
	}
}

#endif /* LOG_LEVEL >= LOG_LEVEL_VERBOSE */

void xlat_tables_print(xlat_ctx_t *ctx)
{
#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
	VERBOSE("Translation tables state:\n");
	VERBOSE("  Targeted EL: %i\n", ctx->exception_level);
	VERBOSE("  Max allowed PA:  0x%llx\n", ctx->pa_max_address);
	VERBOSE("  Max allowed VA:  %p\n", (void *) ctx->va_max_address);
	VERBOSE("  Max mapped PA:   0x%llx\n", ctx->max_pa);
	VERBOSE("  Max mapped VA:   %p\n", (void *) ctx->max_va);

	VERBOSE("  Initial lookup level: %i\n", ctx->base_level);
	VERBOSE("  Entries @initial lookup level: %i\n",
		ctx->base_table_entries);

	int used_page_tables;
#if PLAT_XLAT_TABLES_DYNAMIC
	used_page_tables = 0;
	for (unsigned int i = 0; i < ctx->tables_num; ++i) {
		if (ctx->tables_mapped_regions[i] != 0)
			++used_page_tables;
	}
#else
	used_page_tables = ctx->next_table;
#endif
	VERBOSE("  Used %i sub-tables out of %i (spare: %i)\n",
		used_page_tables, ctx->tables_num,
		ctx->tables_num - used_page_tables);

	xlat_tables_print_internal(0, ctx->base_table, ctx->base_table_entries,
				   ctx->base_level, ctx->execute_never_mask);
#endif /* LOG_LEVEL >= LOG_LEVEL_VERBOSE */
}

void init_xlat_tables_ctx(xlat_ctx_t *ctx)
{
	mmap_region_t *mm = ctx->mmap;

	assert(!ctx->initialized);

	print_mmap(mm);

	if (ctx->exception_level == XLAT_CTX_EXCEPTION_LEVEL_CURRENT)
		ctx->exception_level = xlat_arch_current_el();

	assert((ctx->exception_level >= 1) && (ctx->exception_level <= 3));

	ctx->execute_never_mask = xlat_arch_get_xn_desc(ctx->exception_level);

	/* All tables must be zeroed before mapping any region. */

	for (unsigned int i = 0; i < ctx->base_table_entries; i++)
		ctx->base_table[i] = INVALID_DESC;

	for (unsigned int j = 0; j < ctx->tables_num; j++) {
#if PLAT_XLAT_TABLES_DYNAMIC
		ctx->tables_mapped_regions[j] = 0;
#endif
		for (unsigned int i = 0; i < XLAT_TABLE_ENTRIES; i++)
			ctx->tables[j][i] = INVALID_DESC;
	}

	while (mm->size) {
		uintptr_t end_va = xlat_tables_map_region(ctx, mm, 0, ctx->base_table,
				ctx->base_table_entries, ctx->base_level);

		if (end_va != mm->base_va + mm->size - 1) {
			ERROR("Not enough memory to map region:\n"
			      " VA:%p  PA:0x%llx  size:0x%zx  attr:0x%x\n",
			      (void *)mm->base_va, mm->base_pa, mm->size, mm->attr);
			panic();
		}

		mm++;
	}

	assert(ctx->pa_max_address <= xlat_arch_get_max_supported_pa());
	assert(ctx->max_va <= ctx->va_max_address);
	assert(ctx->max_pa <= ctx->pa_max_address);

	ctx->initialized = 1;

	xlat_tables_print(ctx);
}

void init_xlat_tables(void)
{
	assert(!is_mmu_enabled());
	init_xlat_tables_ctx(&tf_xlat_ctx);
}

/*
 * If dynamic allocation of new regions is disabled then by the time we call the
 * function enabling the MMU, we'll have registered all the memory regions to
 * map for the system's lifetime. Therefore, at this point we know the maximum
 * physical address that will ever be mapped.
 *
 * If dynamic allocation is enabled then we can't make any such assumption
 * because the maximum physical address could get pushed while adding a new
 * region. Therefore, in this case we have to assume that the whole address
 * space size might be mapped.
 */
#ifdef PLAT_XLAT_TABLES_DYNAMIC
#define MAX_PHYS_ADDR	tf_xlat_ctx.pa_max_address
#else
#define MAX_PHYS_ADDR	tf_xlat_ctx.max_pa
#endif

#ifdef AARCH32

void enable_mmu_secure(unsigned int flags)
{
	enable_mmu_arch(flags, tf_xlat_ctx.base_table, MAX_PHYS_ADDR,
			tf_xlat_ctx.va_max_address);
}

#else

void enable_mmu_el1(unsigned int flags)
{
	enable_mmu_arch(flags, tf_xlat_ctx.base_table, MAX_PHYS_ADDR,
			tf_xlat_ctx.va_max_address);
}

void enable_mmu_el3(unsigned int flags)
{
	enable_mmu_arch(flags, tf_xlat_ctx.base_table, MAX_PHYS_ADDR,
			tf_xlat_ctx.va_max_address);
}

#endif /* AARCH32 */

////////////////////////////////////////////////////////////////////

static inline uint64_t *get_next_table_addr(uint64_t table_desc)
{
	assert((table_desc & DESC_MASK) == TABLE_DESC);

	/* See section D4.3 page 1775 in the ARMv8 ARM (rev k) */
	uint64_t next_level_table_addr = table_desc & TABLE_ADDR_MASK;

	return (uint64_t *)next_level_table_addr;
}

static inline int get_xlat_table_idx(uintptr_t virtual_addr, int level)
{
	return (virtual_addr >> XLAT_ADDR_SHIFT(level)) & XLAT_TABLE_IDX_MASK;
}

/*
 * This function assumes that the given descriptor is a page or block
 * descriptor, i.e. not a table descriptor.
 */
static void print_block_or_page_desc_attr(uint64_t desc)
{
	/* XXX: Define all bit shifts as architectural defs */
	VERBOSE("  Upper attributes:\n");
	VERBOSE("    XN    = %i\n", (int)((desc >> 54) & 1));
	VERBOSE("    PXN   = %i\n", (int)((desc >> 53) & 1));
	VERBOSE("    Cont  = %i\n", (int)((desc >> 52) & 1));

	VERBOSE("  Lower attributes:\n");
	VERBOSE("    nG    = %i\n", (int)((desc >> 11) & 1));
	VERBOSE("    AF    = %i\n", (int)((desc >> 10) & 1));
	VERBOSE("    SH    = %i\n", (int)((desc >>  8) & 3));
	VERBOSE("    AP    = %i\n", (int)((desc >>  6) & 3));
	VERBOSE("    NS    = %i\n", (int)((desc >>  5) & 1));
	VERBOSE("    idx   = %i\n", (int)((desc >>  2) & 7));
}

/*
 * Note: The level argument is required to distinguish a page descriptor from a
 * table descriptor, as this can't be deduced from the descriptor format itself.
 */
static void print_block_or_page_desc(uint64_t desc, int level)
{
#if ENABLE_ASSERTIONS
	uint64_t desc_type = desc & DESC_MASK;
#endif
	assert(((level == XLAT_TABLE_LEVEL_MAX) && (desc_type == PAGE_DESC)) ||
	       ((level != XLAT_TABLE_LEVEL_MAX) && (desc_type == BLOCK_DESC)));

	VERBOSE("Mapped memory region starts from %p (size: 0x%lx)\n",
		(void *)(desc & TABLE_ADDR_MASK),
		XLAT_BLOCK_SIZE(level));

	VERBOSE("(%s descriptor)\n",
		(level == XLAT_TABLE_LEVEL_MAX) ? "page" : "block");

	print_block_or_page_desc_attr(desc);
}

/*
 * Update memory attributes in the given block or page descriptor.
 */
static void change_desc_attributes(uint64_t *desc,
				   mmap_attr_t new_attr)
{
	/*
	 * Change the AP[2] & XN bits in the descriptor according to the
	 * requested high-level attributes.
	 */
	int read_write = (new_attr & (1 << MT_PERM_SHIFT)) == MT_RW;
	int ap2_bit = read_write ? AP2_RW : AP2_RO;
	int executable = (new_attr & (1 << MT_EXECUTE_SHIFT)) == MT_EXECUTE;
	int xn_bit = executable ? 0 : 1;

	set_bit(desc, ap2_bit, AP2_SHIFT);
	set_bit(desc, xn_bit, XN_SHIFT);
}

/*
 * Do a translation table walk to find the block or page descriptor that maps
 * virtual_addr.
 *
 * On success, return the address of the descriptor (within the translation
 * table). On error, return NULL.
 *
 * Error cases:
 * - virtual_addr is not mapped.
 *
 * xlat_table_base: Base address for the initial lookup level.
 * xlat_table_base_entries: Number of entries in the translation table for the
 *   initial lookup level.
 * XXX: out_level is ugly, find a better solution.
 */
static uint64_t *find_xlat_table_entry(uintptr_t virtual_addr,
				       void *xlat_table_base,
				       int xlat_table_base_entries,
				       uintptr_t virt_addr_space_size,
				       int *out_level)
{
	VERBOSE("\n%s(%p)\n", __func__, (void *)virtual_addr);
	VERBOSE("Starting translation table walk from level %u\n",
		GET_XLAT_TABLE_LEVEL_BASE(virt_addr_space_size));

	uint64_t *table = xlat_table_base;
	int entries = xlat_table_base_entries;

	for (int level = GET_XLAT_TABLE_LEVEL_BASE(virt_addr_space_size);
	     level     <= XLAT_TABLE_LEVEL_MAX;
	     ++level) {
		VERBOSE("Table address: %p\n", (void *)table);

		int idx = get_xlat_table_idx(virtual_addr, level);

		VERBOSE("Index into level-%i table: %i\n", level, idx);
		if (idx >= entries) {
			VERBOSE("Invalid address\n");
			return NULL;
		}

		uint64_t desc = table[idx];
		uint64_t desc_type = desc & DESC_MASK;

		VERBOSE("Descriptor at level %i:\n", level);
		VERBOSE("Value = 0x%llx\n", (unsigned long long)desc);

		if (desc_type == INVALID_DESC) {
			VERBOSE("Invalid entry (memory not mapped)\n");
			return NULL;
		}

		if (desc_type == BLOCK_DESC) {
			VERBOSE("Descriptor mapping a memory block (size: 0x%lx)\n",
				XLAT_BLOCK_SIZE(level));
			*out_level = level;
			return &table[idx];
		}

		assert((desc_type == TABLE_DESC) || (desc_type == PAGE_DESC));
		if (level == XLAT_TABLE_LEVEL_MAX) {
			VERBOSE("Descriptor mapping a memory page (size: 0x%lx)\n",
				XLAT_BLOCK_SIZE(XLAT_TABLE_LEVEL_MAX));
			*out_level = level;
			return &table[idx];
		}

		VERBOSE("Table descriptor, walking...\n");
		table = get_next_table_addr(desc);
		entries = XLAT_TABLE_ENTRIES;
	}

	return 0;
}

int change_mem_attributes(xlat_ctx_t *ctx, uintptr_t base_va, size_t size,
			  mmap_attr_t attributes)
{
	void *xlat_table_base = ctx->base_table;
	int xlat_table_base_entries = ctx->base_table_entries;
	uintptr_t virt_addr_space_size = ctx->va_max_address + 1;

	/*
	 * Sanity-check arguments
	 */
	assert(ctx->initialized);

	if (!IS_PAGE_ALIGNED(base_va)) {
		ERROR("%s: Address 0x%llx is not aligned on a page boundary\n",
		      __func__, (unsigned long long)base_va);
		return -EINVAL;
	}

	if (size == 0) {
		ERROR("%s: Size is 0\n", __func__);
		return -EINVAL;
	}

	if ((size % PAGE_SIZE) != 0) {
		ERROR("%s: Size 0x%zx is not a multiple of a page size\n",
		      __func__, size);
		return -EINVAL;
	}

	if (((attributes & (1 << MT_EXECUTE_SHIFT)) == MT_EXECUTE) &&
	    ((attributes & (1 << MT_PERM_SHIFT)) == MT_RW)) {
		ERROR("%s: Read-write + execute is forbidden\n", __func__);
		return -EINVAL;
	}

	int pages_count = size / PAGE_SIZE;

	VERBOSE("Changing memory attributes of %i pages starting from address %p\n",
		pages_count, (void *)base_va);

	uintptr_t base_va_original = base_va;

	/* Check that all the required pages are mapped at a page granularity */
	for (int i = 0; i < pages_count; ++i) {
		uint64_t *entry;
		int level;

		entry = find_xlat_table_entry(base_va,
					      xlat_table_base,
					      xlat_table_base_entries,
					      virt_addr_space_size,
					      &level);
		if (!entry) {
			ERROR("Address %p is not mapped\n", (void *)base_va);
			return -EINVAL;
		}

		if (((*entry & DESC_MASK) != PAGE_DESC) ||
		    (level != XLAT_TABLE_LEVEL_MAX)) {
			ERROR("Address %p is not mapped at the right granularity\n",
			      (void *)base_va);
			ERROR("Granularity is 0x%lx, should be 0x%x\n",
			      XLAT_BLOCK_SIZE(level), PAGE_SIZE);
			return -EINVAL;
		}

		base_va += PAGE_SIZE;
	}

	VERBOSE("\n%s: OK, all pages are already mapped, lets change the attributes\n\n",
		__func__);

	/* Restore original value */
	base_va = base_va_original;

	/* All pages are in place, so let's change their attributes now */
	for (int i = 0; i < pages_count; ++i) {
		uint64_t *entry;
		int level;

		entry = find_xlat_table_entry(base_va,
					      xlat_table_base,
					      xlat_table_base_entries,
					      virt_addr_space_size,
					      &level);

		/* We just checked that, but who knows?... */
		assert(entry && ((*entry & DESC_MASK) == PAGE_DESC) &&
		       (level == XLAT_TABLE_LEVEL_MAX));

		VERBOSE("Old attributes:\n");
		print_block_or_page_desc(*entry, level);

		change_desc_attributes(entry, attributes);

		VERBOSE("New attributes:\n");
		print_block_or_page_desc(*entry, level);

		xlat_arch_tlbi_va_el(base_va, 1);

		base_va += PAGE_SIZE;
	}

	xlat_arch_tlbi_va_sync();

	return 0;
}
