// SPDX-License-Identifier: GPL-2.0+
/*
 *  flexible mmap layout support
 *
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * Started by Ingo Molnar <mingo@elte.hu>
 */

#include <linux/elf-randomize.h>
#include <linux/personality.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/random.h>
#include <linux/compat.h>
#include <linux/security.h>
#include <linux/hugetlb.h>
#include <asm/elf.h>

static unsigned long stack_maxrandom_size(void)
{
	if (!(current->flags & PF_RANDOMIZE))
		return 0;
	return STACK_RND_MASK << PAGE_SHIFT;
}

static inline int mmap_is_legacy(struct rlimit *rlim_stack)
{
	if (current->personality & ADDR_COMPAT_LAYOUT)
		return 1;
	if (rlim_stack->rlim_cur == RLIM_INFINITY)
		return 1;
	return sysctl_legacy_va_layout;
}

unsigned long arch_mmap_rnd(void)
{
	return (get_random_u32() & MMAP_RND_MASK) << PAGE_SHIFT;
}

static unsigned long mmap_base_legacy(unsigned long rnd)
{
	return TASK_UNMAPPED_BASE + rnd;
}

static inline unsigned long mmap_base(unsigned long rnd,
				      struct rlimit *rlim_stack)
{
	unsigned long gap = rlim_stack->rlim_cur;
	unsigned long pad = stack_maxrandom_size() + stack_guard_gap;

	/* Values close to RLIM_INFINITY can overflow. */
	if (gap + pad > gap)
		gap += pad;

	/*
	 * Top of mmap area (just below the process stack).
	 * Leave at least a ~128 MB hole.
	 */
	gap = clamp(gap, SZ_128M, (STACK_TOP / 6) * 5);

	return PAGE_ALIGN(STACK_TOP - gap - rnd);
}

static int get_align_mask(struct file *filp, unsigned long flags)
{
	if (filp && is_file_hugepages(filp))
		return huge_page_mask_align(filp);
	if (!(current->flags & PF_RANDOMIZE))
		return 0;
	if (filp || (flags & MAP_SHARED))
		return MMAP_ALIGN_MASK << PAGE_SHIFT;
	return 0;
}

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
				     unsigned long len, unsigned long pgoff,
				     unsigned long flags, vm_flags_t vm_flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct vm_unmapped_area_info info = {};

	if (len > TASK_SIZE - mmap_min_addr)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		goto check_asce_limit;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			goto check_asce_limit;
	}

	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = TASK_SIZE;
	info.align_mask = get_align_mask(filp, flags);
	if (!(filp && is_file_hugepages(filp)))
		info.align_offset = pgoff << PAGE_SHIFT;
	addr = vm_unmapped_area(&info);
	if (offset_in_page(addr))
		return addr;

check_asce_limit:
	return check_asce_limit(mm, addr, len);
}

unsigned long arch_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
					     unsigned long len, unsigned long pgoff,
					     unsigned long flags, vm_flags_t vm_flags)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	struct vm_unmapped_area_info info = {};

	/* requested length too big for entire address space */
	if (len > TASK_SIZE - mmap_min_addr)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		goto check_asce_limit;

	/* requesting a specific address */
	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr && addr >= mmap_min_addr &&
				(!vma || addr + len <= vm_start_gap(vma)))
			goto check_asce_limit;
	}

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = PAGE_SIZE;
	info.high_limit = mm->mmap_base;
	info.align_mask = get_align_mask(filp, flags);
	if (!(filp && is_file_hugepages(filp)))
		info.align_offset = pgoff << PAGE_SHIFT;
	addr = vm_unmapped_area(&info);

	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	if (offset_in_page(addr)) {
		VM_BUG_ON(addr != -ENOMEM);
		info.flags = 0;
		info.low_limit = TASK_UNMAPPED_BASE;
		info.high_limit = TASK_SIZE;
		addr = vm_unmapped_area(&info);
		if (offset_in_page(addr))
			return addr;
	}

check_asce_limit:
	return check_asce_limit(mm, addr, len);
}

/*
 * This function, called very early during the creation of a new
 * process VM image, sets up which VM layout function to use:
 */
void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
	unsigned long random_factor = 0UL;

	if (current->flags & PF_RANDOMIZE)
		random_factor = arch_mmap_rnd();

	/*
	 * Fall back to the standard layout if the personality
	 * bit is set, or if the expected stack growth is unlimited:
	 */
	if (mmap_is_legacy(rlim_stack)) {
		mm->mmap_base = mmap_base_legacy(random_factor);
		clear_bit(MMF_TOPDOWN, &mm->flags);
	} else {
		mm->mmap_base = mmap_base(random_factor, rlim_stack);
		set_bit(MMF_TOPDOWN, &mm->flags);
	}
}

static pgprot_t protection_map[16] __ro_after_init;

void __init setup_protection_map(void)
{
	pgprot_t *pm = protection_map;

	pm[VM_NONE]					= PAGE_NONE;
	pm[VM_READ]					= PAGE_RO;
	pm[VM_WRITE]					= PAGE_RO;
	pm[VM_WRITE | VM_READ]				= PAGE_RO;
	pm[VM_EXEC]					= PAGE_RX;
	pm[VM_EXEC | VM_READ]				= PAGE_RX;
	pm[VM_EXEC | VM_WRITE]				= PAGE_RX;
	pm[VM_EXEC | VM_WRITE | VM_READ]		= PAGE_RX;
	pm[VM_SHARED]					= PAGE_NONE;
	pm[VM_SHARED | VM_READ]				= PAGE_RO;
	pm[VM_SHARED | VM_WRITE]			= PAGE_RW;
	pm[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_RW;
	pm[VM_SHARED | VM_EXEC]				= PAGE_RX;
	pm[VM_SHARED | VM_EXEC | VM_READ]		= PAGE_RX;
	pm[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_RWX;
	pm[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_RWX;
}

DECLARE_VM_GET_PAGE_PROT
